#include "Emerald/Core/Application.h"

#include <string>

#include <SDL3/SDL.h>

#include "Emerald/Core/Defines.h"
#include "Emerald/Core/Log.h"

#if EMERALD_WITH_IMGUI
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>
#endif

namespace Emerald {

Application::Application(const ApplicationSpec& spec)
    : m_Spec(spec), m_FixedTimestep(spec.FixedUpdateRate, spec.MaxFixedStepsPerFrame)
{
    Log::Init(spec.LogFile);
    EM_CORE_INFO("Emerald starting (SDL {}.{}.{})", SDL_MAJOR_VERSION, SDL_MINOR_VERSION,
                 SDL_MICRO_VERSION);

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

    m_Renderer = std::make_unique<Renderer>();
    if (!m_Renderer->Init(m_Window->GetNativeWindow(), spec.Window.VSync, spec.ShaderFormats)) {
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
    InitImGui();
}

Application::~Application()
{
    m_ThreadPool.reset(); // stops and joins the workers while everything they might use exists
    ShutdownImGui();
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

    m_Running = true;
    OnStart();

    u64 last = SDL_GetTicksNS();
    while (m_Running) {
        // Everything allocated from the frame arena last frame is gone from here on.
        m_FrameArena->Reset();

        m_Keyboard.BeginFrame();
        m_Gamepads.BeginFrame();
        SDL_Event event;
        while (SDL_PollEvent(&event))
            ProcessEvent(event);

        const u64 now = SDL_GetTicksNS();
        const u64 elapsedNs = now - last;
        last = now;

        // Fixed-rate simulation first (0..MaxFixedStepsPerFrame steps), then the per-frame update.
        const u32 steps = m_FixedTimestep.Advance(elapsedNs);
        for (u32 i = 0; i < steps && m_Running; ++i) {
            m_Keyboard.BeginFixedStep();
            m_Gamepads.BeginFixedStep();
            OnFixedUpdate(m_FixedTimestep.GetStepSeconds());
            m_Keyboard.EndFixedStep();
            m_Gamepads.EndFixedStep();
        }
        OnUpdate(static_cast<f32>(elapsedNs) / 1e9f);
        RenderFrame();

        ++m_FrameCount;
        if (m_Spec.MaxFrames != 0 && m_FrameCount >= m_Spec.MaxFrames) {
            EM_CORE_INFO("Reached frame limit ({}), exiting", m_Spec.MaxFrames);
            m_Running = false;
        }
    }

    // Let background tasks finish before the app releases the resources they may use.
    m_ThreadPool->WaitIdle();
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
#else
    const bool imguiWantsKeys = false;
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
}

void Application::RenderFrame()
{
#if EMERALD_WITH_IMGUI
    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    OnImGui();
    ImGui::Render();
    ImDrawData* drawData = ImGui::GetDrawData();
#endif

    if (!m_Renderer->BeginFrame())
        return; // minimized: nothing to draw this frame

    // Copy passes (uploads) must happen before the render pass: first record the app's 2D shapes
    // and upload them, then ImGui's vertex/index data.
    SDL_GPUCommandBuffer* cmd = m_Renderer->GetCommandBuffer();
    OnRender2D(*m_Renderer2D);
    m_Renderer2D->Upload(cmd);
#if EMERALD_WITH_IMGUI
    ImGui_ImplSDLGPU3_PrepareDrawData(drawData, cmd);
#endif

    // Draw order: the app's own draw calls, then the 2D shapes, then the ImGui overlay on top.
    SDL_GPURenderPass* pass = m_Renderer->BeginRenderPass(m_Spec.ClearColor);
    OnRender(pass);
    m_Renderer2D->Render(cmd, pass, m_Renderer->GetFrameWidth(), m_Renderer->GetFrameHeight());
#if EMERALD_WITH_IMGUI
    ImGui_ImplSDLGPU3_RenderDrawData(drawData, cmd, pass);
#endif
    m_Renderer->EndRenderPass();
    m_Renderer->EndFrame();
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
