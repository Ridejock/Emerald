#include "Emerald/Core/Application.h"

#include <SDL3/SDL.h>

#include "Emerald/Core/Defines.h"
#include "Emerald/Core/Log.h"

#if EMERALD_WITH_IMGUI
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
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
    if (m_Window->IsValid())
        InitImGui();
}

Application::~Application()
{
    ShutdownImGui();
    m_Window.reset();
    if (m_SdlInitialized)
        SDL_Quit();
    EM_CORE_INFO("Emerald shut down");
    Log::Shutdown();
}

int Application::Run()
{
    if (!m_Window || !m_Window->IsValid()) {
        EM_CORE_ERROR("Cannot run: window/renderer initialization failed");
        return 1;
    }

    SDL_Renderer* renderer = m_Window->GetRenderer();
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

        const std::uint64_t now = SDL_GetTicksNS();
        const float dt = static_cast<float>(now - last) / 1e9f;
        last = now;

        OnUpdate(dt);

#if EMERALD_WITH_IMGUI
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        OnImGui();
        ImGui::Render();
#endif

        SDL_SetRenderDrawColor(renderer, 18, 24, 22, 255);
        SDL_RenderClear(renderer);
        OnRender();
#if EMERALD_WITH_IMGUI
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
#endif
        SDL_RenderPresent(renderer);

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

void Application::InitImGui()
{
#if EMERALD_WITH_IMGUI
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();

    ImGui_ImplSDL3_InitForSDLRenderer(m_Window->GetNativeWindow(), m_Window->GetRenderer());
    ImGui_ImplSDLRenderer3_Init(m_Window->GetRenderer());
    m_ImGuiInitialized = true;
    EM_CORE_INFO("Dear ImGui {} initialized", IMGUI_VERSION);
#endif
}

void Application::ShutdownImGui()
{
#if EMERALD_WITH_IMGUI
    if (!m_ImGuiInitialized)
        return;
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    m_ImGuiInitialized = false;
#endif
}

} // namespace Emerald
