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

Application::Application(const ApplicationSpec& spec) : m_Spec(spec)
{
    Log::Init();
    EM_CORE_INFO("Emerald starting (SDL {}.{}.{})", SDL_MAJOR_VERSION, SDL_MINOR_VERSION,
                 SDL_MICRO_VERSION);

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        EM_CORE_ERROR("SDL_Init failed: {}", SDL_GetError());
        return;
    }
    m_SdlInitialized = true;
    EM_CORE_INFO("SDL video driver: {}", SDL_GetCurrentVideoDriver());

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
    InitImGui();
}

Application::~Application()
{
    ShutdownImGui();
    m_Renderer.reset(); // GPU device must go before the window it renders into
    m_Window.reset();
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
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
#if EMERALD_WITH_IMGUI
            ImGui_ImplSDL3_ProcessEvent(&event);
#endif
            if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
                m_Running = false;
            OnEvent(event);
        }

        const u64 now = SDL_GetTicksNS();
        const f32 dt = static_cast<f32>(now - last) / 1e9f;
        last = now;

        OnUpdate(dt);
        RenderFrame();

        ++m_FrameCount;
        if (m_Spec.MaxFrames != 0 && m_FrameCount >= m_Spec.MaxFrames) {
            EM_CORE_INFO("Reached frame limit ({}), exiting", m_Spec.MaxFrames);
            m_Running = false;
        }
    }

    OnShutdown();
    EM_CORE_INFO("Main loop ended after {} frames", m_FrameCount);
    return 0;
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

#if EMERALD_WITH_IMGUI
    // Uploads ImGui's vertex/index data with a copy pass, which must happen outside a render pass.
    ImGui_ImplSDLGPU3_PrepareDrawData(drawData, m_Renderer->GetCommandBuffer());
#endif

    SDL_GPURenderPass* pass = m_Renderer->BeginRenderPass(m_Spec.ClearColor);
    OnRender(pass);
#if EMERALD_WITH_IMGUI
    ImGui_ImplSDLGPU3_RenderDrawData(drawData, m_Renderer->GetCommandBuffer(), pass);
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
