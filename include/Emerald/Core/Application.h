#pragma once

#include <filesystem>
#include <memory>

#include <SDL3/SDL_pixels.h>

#include "Emerald/Core/Defines.h"
#include "Emerald/Core/Log.h"
#include "Emerald/Core/Window.h"
#include "Emerald/Renderer/Renderer.h"

#if EMERALD_WITH_ENTT
#include <entt/entt.hpp>
#endif

union SDL_Event;
struct SDL_GPURenderPass;

namespace Emerald {

struct ApplicationSpec {
    WindowSpec Window;
    // Color the swapchain is cleared to at the start of every frame.
    SDL_FColor ClearColor{0.07f, 0.094f, 0.086f, 1.0f};
    // Shader formats the application ships (see cmake/Shaders.cmake). Only GPU backends that
    // accept one of these are considered. emerald_add_shaders() defines EMERALD_SHADER_FORMATS
    // for its target with exactly the formats it generated.
    SDL_GPUShaderFormat ShaderFormats =
        SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL;
    // Stop after this many frames (0 = run until the window is closed). Useful for CI/headless
    // runs.
    u64 MaxFrames = 0;
    // Log file written in addition to the console. Relative paths are relative to the executable's
    // directory; the file is truncated on each run. Empty = no log file.
    std::filesystem::path LogFile = Log::DefaultFile;
};

// Initializes SDL, owns the main window and GPU renderer, and drives the main loop.
// Derive from it and override the hooks you need.
class Application {
public:
    explicit Application(const ApplicationSpec& spec = {});
    virtual ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    // Runs the main loop; returns a process exit code (nonzero if initialization failed).
    int Run();
    void Quit() { m_Running = false; }

    [[nodiscard]] Window& GetWindow() { return *m_Window; }
    [[nodiscard]] Renderer& GetRenderer() { return *m_Renderer; }
    [[nodiscard]] u64 GetFrameCount() const { return m_FrameCount; }

#if EMERALD_WITH_ENTT
    [[nodiscard]] entt::registry& GetRegistry() { return m_Registry; }
#endif

protected:
    // Create GPU resources (buffers, pipelines) here; the renderer is ready.
    virtual void OnStart() {}
    virtual void OnEvent(const SDL_Event& /*event*/) {}
    virtual void OnUpdate(f32 /*deltaSeconds*/) {}
    // Record draw calls into the frame's main render pass (already cleared to ClearColor).
    // Not called for frames that are skipped, e.g. while the window is minimized.
    virtual void OnRender(SDL_GPURenderPass* /*renderPass*/) {}
    virtual void OnImGui() {} // Only called when built with EMERALD_USE_IMGUI=ON
    // Release GPU resources here; the renderer is still alive.
    virtual void OnShutdown() {}

private:
    void InitImGui();
    void ShutdownImGui();
    void RenderFrame();

    ApplicationSpec m_Spec;
    std::unique_ptr<Window> m_Window;
    std::unique_ptr<Renderer> m_Renderer;
    bool m_SdlInitialized = false;
    bool m_RendererInitialized = false;
#if EMERALD_WITH_IMGUI
    bool m_ImGuiInitialized = false;
#endif
    bool m_Running = false;
    u64 m_FrameCount = 0;

#if EMERALD_WITH_ENTT
    entt::registry m_Registry;
#endif
};

} // namespace Emerald
