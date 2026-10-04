#include "Emerald/Core/Application.h"

#include <cstdio>
#include <filesystem>
#include <random>
#include <string>
#include <system_error>

#include <SDL3/SDL.h>

#include "Emerald/Core/Defines.h"
#include "Emerald/Core/Log.h"
#include "Emerald/Core/Paths.h"
#include "Emerald/Core/Profile.h"

#if EMERALD_WITH_IMGUI
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>
#endif

namespace Emerald {

namespace {

// --capture's file for a frame (1 = the first): dir/frame_000001.png.
std::filesystem::path CaptureFile(const std::filesystem::path& dir, u64 frame)
{
    char name[32];
    std::snprintf(name, sizeof(name), "frame_%06llu.png", static_cast<unsigned long long>(frame));
    return dir / name;
}

} // namespace

Application::Application(const ApplicationSpec& spec)
    : m_Spec(spec), m_FixedTimestep(spec.FixedUpdateRate, spec.MaxFixedStepsPerFrame)
{
    Log::Init(spec.LogFile);
    EM_CORE_INFO("Emerald starting (SDL {}.{}.{})", SDL_MAJOR_VERSION, SDL_MINOR_VERSION,
                 SDL_MICRO_VERSION);

    // Development options (--frames, --replay, ...). A replay is loaded now, so its seed is
    // known before the game's OnStart.
    m_Dev = ParseDevOptions(spec.Args);
    if (m_Dev.Frames != 0)
        m_Spec.MaxFrames = m_Dev.Frames;
    m_Seed = std::random_device{}();
    if (!m_Dev.Replay.empty()) {
        m_Replay = InputRecording::Load(m_Dev.Replay);
        m_DevFailed = !m_Replay;
        if (m_Replay)
            m_Seed = m_Replay->Seed;
    }

    m_FrameArena = std::make_unique<FrameArena>(spec.FrameArenaSize);
    m_ThreadPool = std::make_unique<ThreadPool>(spec.WorkerThreads);
    EM_CORE_INFO("Thread pool: {} workers; frame arena: {} KiB", m_ThreadPool->GetThreadCount(),
                 spec.FrameArenaSize / 1024);

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        EM_CORE_ERROR("SDL_Init failed: {}", SDL_GetError());
        return;
    }
    m_SdlInitialized = true;
    EM_CORE_INFO("SDL video driver: {}", SDL_GetCurrentVideoDriver());

    // Gamepads: SDL's HIDAPI drivers talk to PS4/PS5 and Switch pads directly (also over
    // Bluetooth), which gives the proper layout, labels and rumble. They are on by default; the
    // hints just make that explicit (environment variables still override them). Not fatal if it
    // fails: the game then runs with the keyboard only. Pads that are already connected arrive as
    // SDL_EVENT_GAMEPAD_ADDED events like hot-plugged ones.
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS4, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS5, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_SWITCH, "1");
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD))
        EM_CORE_WARN("Gamepad support unavailable: {}", SDL_GetError());
    // Audio is optional too: without a device, Audio::Play does nothing.
    if (SDL_InitSubSystem(SDL_INIT_AUDIO))
        m_Audio.Init();
    else
        EM_CORE_WARN("Audio unavailable: {}", SDL_GetError());

    m_Window = std::make_unique<Window>(spec.Window);
    if (!m_Window->IsValid())
        return;

    // --gpu picks the backend; without it SDL uses SDL_GPU_DRIVER or picks one itself.
    std::optional<std::string_view> driver;
    if (const std::optional<std::string_view> arg = FindGpuArg(spec.Args)) {
        driver = NormalizeGpuDriver(*arg);
        if (!driver) {
            EM_CORE_WARN("Unknown --gpu '{}' (expected vulkan, d3d12, metal or auto); using auto",
                         *arg);
            driver = std::string_view();
        }
    }
    m_Renderer = std::make_unique<Renderer>();
    if (!m_Renderer->Init(m_Window->GetNativeWindow(), spec.Window.VSync, spec.ShaderFormats,
                          driver)) {
        EM_CORE_ERROR("Failed to create SDL GPU device: {}", SDL_GetError());
        // Hide the (empty) main window so only the explanation is visible.
        SDL_HideWindow(m_Window->GetNativeWindow());
        const std::string message =
            std::string("Emerald could not initialize the GPU.\n\n"
                        "A graphics driver supporting Vulkan, Direct3D 12 or Metal\n"
                        "is required. Please update your graphics drivers.\n\n"
                        "Details: ") +
            SDL_GetError();
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Emerald - unsupported GPU", message.c_str(),
                                 nullptr);
        return;
    }
    m_RendererInitialized = true;

    // Not fatal if it fails (logged): the app still runs, only 2D shapes are not drawn.
    m_Renderer2D = std::make_unique<Renderer2D>();
    m_Renderer2D->Init(m_Renderer->GetDevice(), m_Renderer->GetSwapchainFormat());
    m_Assets = std::make_unique<Assets>(std::make_unique<GpuAssetLoader>(m_Renderer->GetDevice()),
                                        Paths::GetBasePath(), m_ThreadPool.get());
    InitImGui();
}

Application::~Application()
{
    m_ThreadPool.reset(); // stops and joins the workers while everything they might use exists
    m_Scenes.ExitAll();   // scenes hold asset handles and GPU resources
    m_World.Clear();      // so may components
    m_Assets.reset();     // textures before the GPU device
    ShutdownImGui();
    m_Crt.reset();
    m_Renderer2D.reset();
    m_Renderer.reset(); // GPU device must go before the window it renders into
    m_Window.reset();
    m_Gamepads.CloseAll();
    m_Audio.Shutdown();
    if (m_SdlInitialized)
        SDL_Quit();
    EM_CORE_INFO("Emerald shut down");
    Log::Shutdown();
}

int Application::Run()
{
    if (!m_RendererInitialized) {
        EM_CORE_ERROR("Cannot run: window/GPU initialization failed");
        return 1;
    }
    if (m_DevFailed) {
        EM_CORE_ERROR("Cannot run: the --replay file could not be loaded");
        return 1;
    }

    m_Running = true;
    OnStart();
    StartInputSession(); // after OnStart: the game has bound its actions

    u64 last = SDL_GetTicksNS();
    while (m_Running) {
        // Everything allocated from the frame arena last frame is gone from here on.
        m_FrameArena->Reset();

        m_Keyboard.BeginFrame();
        m_Gamepads.BeginFrame();
        m_Mouse.BeginFrame();
        {
            EM_PROFILE_SCOPE("Events");
            SDL_Event event;
            while (SDL_PollEvent(&event))
                ProcessEvent(event);
        }

        const u64 now = SDL_GetTicksNS();
        u64 elapsedNs = now - last;
        last = now;
        // A capture runs at a fixed frame time; a replay uses the recorded one (and ends).
        if (m_CaptureFrameNs != 0)
            elapsedNs = m_CaptureFrameNs;
        if (!m_Session.BeginFrame(elapsedNs)) {
            EM_CORE_INFO("Replay finished after {} frames", m_FrameCount);
            break;
        }
        // Hot reload changed files and unload unused assets, before the game looks at them.
        {
            EM_PROFILE_SCOPE("Assets");
            m_Assets->Update(static_cast<f32>(elapsedNs) / 1e9f);
        }

        // Fixed-rate simulation first (0..MaxFixedStepsPerFrame steps), then the per-frame update.
        const u32 steps = m_FixedTimestep.Advance(elapsedNs);
        for (u32 i = 0; i < steps && m_Running; ++i) {
            m_Keyboard.BeginFixedStep();
            m_Gamepads.BeginFixedStep();
            m_Mouse.BeginFixedStep();
            m_Session.BeginStep(); // records or replays this step's actions
            EM_PROFILE_SCOPE("FixedUpdate");
            OnFixedUpdate(m_FixedTimestep.GetStepSeconds());
            m_Scenes.FixedUpdate(m_FixedTimestep.GetStepSeconds());
            m_Keyboard.EndFixedStep();
            m_Gamepads.EndFixedStep();
            m_Mouse.EndFixedStep();
        }
        m_FrameSeconds = static_cast<f32>(elapsedNs) / 1e9f;
        m_Session.BeginUpdate();
        {
            EM_PROFILE_SCOPE("Update");
            OnUpdate(m_FrameSeconds);
            m_Scenes.Update(m_FrameSeconds); // then the stack changes the scenes asked for
            m_Audio.Update();
        }
        RequestCaptures();
        {
            EM_PROFILE_SCOPE("Render"); // includes waiting for the swapchain (vsync)
            RenderFrame();
        }
        FinishCaptures();
        EM_PROFILE_FRAME();

        ++m_FrameCount;
        if (m_Spec.MaxFrames != 0 && m_FrameCount >= m_Spec.MaxFrames) {
            EM_CORE_INFO("Reached frame limit ({}), exiting", m_Spec.MaxFrames);
            m_Running = false;
        }
    }

    // Let background tasks finish before the app releases the resources they may use.
    m_ThreadPool->WaitIdle();
    if (m_Session.IsRecording())
        m_Session.GetRecording().Save(m_Dev.Record);
    m_Session.Stop();
    m_Scenes.ExitAll(); // the scenes go first: they may use what OnShutdown releases
    OnShutdown();
    const FrameArena::Stats arena = m_FrameArena->GetStats();
    EM_CORE_INFO("Main loop ended after {} frames (frame arena peak {} of {} bytes, {} frames "
                 "overflowed)",
                 m_FrameCount, arena.PeakBytes, arena.Capacity, arena.OverflowFrames);
    return 0;
}

void Application::ProcessEvent(const SDL_Event& event)
{
#if EMERALD_WITH_IMGUI
    ImGui_ImplSDL3_ProcessEvent(&event);
    // While an ImGui text field is being edited, key presses go to ImGui only. (WantTextInput,
    // not WantCaptureKeyboard: with keyboard navigation on, the latter is true whenever an ImGui
    // window has focus, which would block the game.) Key-ups always pass, so no key stays stuck.
    const bool imguiWantsKeys = ImGui::GetIO().WantTextInput;
    const bool imguiWantsMouse = ImGui::GetIO().WantCaptureMouse; // the cursor is over ImGui
#else
    const bool imguiWantsKeys = false;
    const bool imguiWantsMouse = false;
#endif

    switch (event.type) {
    case SDL_EVENT_QUIT:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        m_Running = false;
        break;
    case SDL_EVENT_KEY_DOWN:
        if (!event.key.repeat && !imguiWantsKeys)
            m_Keyboard.OnKeyDown(event.key.scancode);
        break;
    case SDL_EVENT_KEY_UP:
        m_Keyboard.OnKeyUp(event.key.scancode);
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        // We will not see key-ups (or gamepad events) while another window has focus.
        m_Keyboard.ReleaseAll();
        m_Gamepads.ReleaseAll();
        m_Mouse.ReleaseAll();
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        // Clicks on the ImGui overlay are for ImGui (releases always go through).
        if (!event.button.down || !imguiWantsMouse)
            m_Mouse.OnButton(static_cast<MouseButton>(event.button.button), event.button.down);
        break;
    case SDL_EVENT_MOUSE_MOTION:
        m_Mouse.OnMotion({event.motion.x, event.motion.y});
        break;
    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        m_Mouse.OnLeave();
        break;
    case SDL_EVENT_GAMEPAD_ADDED:
        m_Gamepads.Open(event.gdevice.which);
        break;
    case SDL_EVENT_GAMEPAD_REMOVED:
        m_Gamepads.Close(event.gdevice.which);
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
        m_Gamepads.OnButton(event.gbutton.which, static_cast<GamepadButton>(event.gbutton.button),
                            event.gbutton.down);
        break;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        // Sticks are -32768..32767, triggers 0..32767.
        m_Gamepads.OnAxis(event.gaxis.which, static_cast<GamepadAxis>(event.gaxis.axis),
                          static_cast<f32>(event.gaxis.value) / 32767.0f);
        break;
    default:
        break;
    }
    OnEvent(event);
    m_Scenes.OnEvent(event);
}

void Application::RenderFrame()
{
#if EMERALD_WITH_IMGUI
    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    OnImGui();
    m_Scenes.ImGui();
    ImGui::Render();
    ImDrawData* drawData = ImGui::GetDrawData();
#endif

    if (!m_Renderer->BeginFrame())
        return; // minimized: nothing to draw this frame

    // Copy passes (uploads) must happen before the render pass: first record the app's 2D shapes
    // and upload them, then ImGui's vertex/index data.
    SDL_GPUCommandBuffer* cmd = m_Renderer->GetCommandBuffer();
    // The scenes first, then the app's own 2D (e.g. a debug overlay over every scene).
    m_Scenes.Render2D(*m_Renderer2D, Vec2(GetWindowSize()));
    OnRender2D(*m_Renderer2D);
    m_Renderer2D->Upload(cmd);
#if EMERALD_WITH_IMGUI
    ImGui_ImplSDLGPU3_PrepareDrawData(drawData, cmd);
#endif

    // Draw order: the app's own draw calls, then the 2D shapes, then the ImGui overlay on top.
    // With the CRT effect the scene goes to its offscreen texture first; the effect then draws
    // it into the frame's target, and ImGui is drawn over that (unaffected).
    const u32 width = m_Renderer->GetFrameWidth();
    const u32 height = m_Renderer->GetFrameHeight();
    SDL_GPUTexture* output = m_Renderer->GetRenderTarget();
    SDL_GPUTexture* scene = m_CrtEnabled ? m_Crt->GetSceneTarget(width, height) : nullptr;
    SDL_GPURenderPass* pass =
        m_Renderer->BeginRenderPass(scene ? scene : output, m_Spec.ClearColor);
    OnRender(pass);
    m_Scenes.Render(pass);
    m_Renderer2D->Render(cmd, pass, width, height);
    if (scene) {
        m_Renderer->EndRenderPass();
        m_Crt->Apply(cmd, output, m_FrameSeconds);
        pass = nullptr;
    }
#if EMERALD_WITH_IMGUI
    if (!pass)
        pass = m_Renderer->BeginRenderPass(output, m_Spec.ClearColor, false); // keep the image
    ImGui_ImplSDLGPU3_RenderDrawData(drawData, cmd, pass);
#endif
    if (pass)
        m_Renderer->EndRenderPass();
    m_Renderer->EndFrame();
}

void Application::StartInputSession()
{
    const f64 rate = m_Spec.FixedUpdateRate;
    if (m_Replay) {
        if (!m_Dev.Record.empty())
            EM_CORE_WARN("--record is ignored while replaying");
        if (m_Replay->EngineVersion != EMERALD_VERSION)
            EM_CORE_WARN("Replay: recorded with Emerald {}, this is {}", m_Replay->EngineVersion,
                         EMERALD_VERSION);
        const usize frames = m_Replay->Frames.size();
        if (m_Session.StartReplay(m_Input, std::move(*m_Replay), rate))
            EM_CORE_INFO("Replaying {} ({} frames)", m_Dev.Replay.string(), frames);
        else
            m_Running = false;
        m_Replay.reset();
    } else if (!m_Dev.Record.empty()) {
        m_Session.StartRecording(m_Input, EMERALD_VERSION, m_Seed, rate);
        EM_CORE_INFO("Recording input to {}", m_Dev.Record.string());
    }

    // The screenshot frame: the last one of a --frames run or a replay, otherwise frame 60.
    const u64 replayFrames = m_Session.IsReplaying() ? m_Session.GetRecording().Frames.size() : 0;
    m_ShotFrame = m_Spec.MaxFrames != 0 ? m_Spec.MaxFrames : 60;
    if (replayFrames != 0 && (m_Spec.MaxFrames == 0 || replayFrames < m_Spec.MaxFrames))
        m_ShotFrame = replayFrames;

    if (!m_Dev.CaptureDir.empty()) {
        std::error_code error;
        std::filesystem::create_directories(m_Dev.CaptureDir, error);
        if (error)
            EM_CORE_ERROR("--capture: cannot create {}: {}", m_Dev.CaptureDir.string(),
                          error.message());
        if (!m_Session.IsReplaying())
            m_CaptureFrameNs = static_cast<u64>(1e9 / m_Dev.CaptureFps + 0.5);
        EM_CORE_INFO("Capturing every frame to {}", m_Dev.CaptureDir.string());
    }
}

void Application::RequestCaptures()
{
    // One screenshot per frame at most: a capture frame that is also the --screenshot frame
    // is copied to the screenshot path afterwards (FinishCaptures).
    const u64 frame = m_FrameCount + 1;
    if (!m_Dev.CaptureDir.empty())
        m_Renderer->RequestScreenshot(CaptureFile(m_Dev.CaptureDir, frame));
    else if (!m_Dev.Screenshot.empty() && frame == m_ShotFrame)
        m_Renderer->RequestScreenshot(m_Dev.Screenshot);
}

void Application::FinishCaptures()
{
    const u64 frame = m_FrameCount + 1;
    if (m_Dev.CaptureDir.empty() || m_Dev.Screenshot.empty() || frame != m_ShotFrame)
        return;
    std::error_code error;
    std::filesystem::copy_file(CaptureFile(m_Dev.CaptureDir, frame), m_Dev.Screenshot,
                               std::filesystem::copy_options::overwrite_existing, error);
    if (error)
        EM_CORE_ERROR("Screenshot {}: {}", m_Dev.Screenshot.string(), error.message());
}

bool Application::SetCrtEnabled(bool enabled)
{
    if (enabled && !m_CrtInitialized) {
        if (!m_RendererInitialized ||
            !m_Crt->Init(m_Renderer->GetDevice(), m_Renderer->GetSwapchainFormat())) {
            m_CrtEnabled = false;
            return false;
        }
        m_CrtInitialized = true;
    }
    if (enabled && !m_CrtEnabled)
        m_Crt->ResetAfterglow(); // no stale trails from before it was switched off
    m_CrtEnabled = enabled;
    return true;
}

void Application::InitImGui()
{
#if EMERALD_WITH_IMGUI
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();

    ImGui_ImplSDL3_InitForSDLGPU(m_Window->GetNativeWindow());
    ImGui_ImplSDLGPU3_InitInfo info{};
    info.Device = m_Renderer->GetDevice();
    info.ColorTargetFormat = m_Renderer->GetSwapchainFormat(); // ImGui draws into the swapchain
    info.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
    ImGui_ImplSDLGPU3_Init(&info);
    m_ImGuiInitialized = true;
    EM_CORE_INFO("Dear ImGui {} initialized (SDL GPU backend)", IMGUI_VERSION);
#endif
}

void Application::ShutdownImGui()
{
#if EMERALD_WITH_IMGUI
    if (!m_ImGuiInitialized)
        return;
    SDL_WaitForGPUIdle(m_Renderer->GetDevice());
    ImGui_ImplSDLGPU3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    m_ImGuiInitialized = false;
#endif
}

} // namespace Emerald
