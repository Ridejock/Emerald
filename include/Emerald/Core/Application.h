#pragma once

#include <memory>

#include "Emerald/Core/Defines.h"
#include "Emerald/Core/Window.h"

#if EMERALD_WITH_ENTT
#include <entt/entt.hpp>
#endif

union SDL_Event;

namespace Emerald {

struct ApplicationSpec {
    WindowSpec Window;
    // Stop after this many frames (0 = run until the window is closed). Useful for CI/headless
    // runs.
    u64 MaxFrames = 0;
};

// Initializes SDL, owns the main window and drives the main loop.
// Derive from it and override the hooks you need.
class Application {
public:
    explicit Application(const ApplicationSpec& spec = {});
    virtual ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    // Runs the main loop; returns a process exit code.
    int Run();
    void Quit() { m_Running = false; }

    [[nodiscard]] Window& GetWindow() { return *m_Window; }
    [[nodiscard]] u64 GetFrameCount() const { return m_FrameCount; }

#if EMERALD_WITH_ENTT
    [[nodiscard]] entt::registry& GetRegistry() { return m_Registry; }
#endif

protected:
    virtual void OnStart() {}
    virtual void OnEvent(const SDL_Event& /*event*/) {}
    virtual void OnUpdate(f32 /*deltaSeconds*/) {}
    virtual void OnRender() {}
    virtual void OnImGui() {} // Only called when built with EMERALD_USE_IMGUI=ON
    virtual void OnShutdown() {}

private:
    void InitImGui();
    void ShutdownImGui();

    ApplicationSpec m_Spec;
    std::unique_ptr<Window> m_Window;
    bool m_SdlInitialized = false;
    bool m_ImGuiInitialized = false;
    bool m_Running = false;
    u64 m_FrameCount = 0;

#if EMERALD_WITH_ENTT
    entt::registry m_Registry;
#endif
};

} // namespace Emerald
