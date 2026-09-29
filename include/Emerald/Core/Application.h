#pragma once

#include <filesystem>
#include <memory>
#include <memory_resource>

#include <SDL3/SDL_pixels.h>

#include "Emerald/Core/Defines.h"
#include "Emerald/Core/FixedTimestep.h"
#include "Emerald/Core/Log.h"
#include "Emerald/Core/ThreadPool.h"
#include "Emerald/Core/Window.h"
#include "Emerald/Input/Input.h"
#include "Emerald/Memory/FrameArena.h"
#include "Emerald/Memory/TrackingResource.h"
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
    // OnFixedUpdate runs this many times per second, independent of the frame rate.
    f64 FixedUpdateRate = 120.0;
    // At most this many fixed steps per frame; if a frame is slower than that, the simulation
    // slows down instead of falling further and further behind (see FixedTimestep.h).
    u32 MaxFixedStepsPerFrame = 8;
    // Log file written in addition to the console. Relative paths are relative to the executable's
    // directory; the file is truncated on each run. Empty = no log file.
    std::filesystem::path LogFile = Log::DefaultFile;
    // Worker threads in the application's ThreadPool (0 = hardware threads - 1, at least 1).
    u32 WorkerThreads = 0;
    // Size of the per-frame scratch arena (GetFrameAllocator). Frames that need more fall back to
    // the heap and log a warning once.
    usize FrameArenaSize = 1024 * 1024;
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
    [[nodiscard]] const Input& GetInput() const { return m_Input; }
    [[nodiscard]] u64 GetFrameCount() const { return m_FrameCount; }

    // Client-area size in window coordinates / in pixels (see Window.h).
    [[nodiscard]] Vec2i GetWindowSize() const { return m_Window->GetSize(); }
    [[nodiscard]] Vec2i GetWindowSizeInPixels() const { return m_Window->GetSizeInPixels(); }

    // Seconds per OnFixedUpdate step (1 / ApplicationSpec::FixedUpdateRate).
    [[nodiscard]] f32 GetFixedDeltaSeconds() const { return m_FixedTimestep.GetStepSeconds(); }
    // How far the current frame is between the last fixed step and the next one (0..1), for
    // interpolating positions when drawing. Most simple games can ignore it.
    [[nodiscard]] f32 GetFixedUpdateAlpha() const { return m_FixedTimestep.GetAlpha(); }

    // Worker threads for background/parallel work (see ThreadPool.h).
    [[nodiscard]] ThreadPool& GetThreadPool() { return *m_ThreadPool; }

    // Memory (see Memory/). Pass these to pmr containers, e.g. PmrVector<T> v(GetFrameAllocator()).
    // Scratch memory for the current frame; everything is freed when the next frame starts.
    // Main thread only.
    [[nodiscard]] std::pmr::memory_resource* GetFrameAllocator() { return m_FrameArena.get(); }
    [[nodiscard]] const FrameArena& GetFrameArena() const { return *m_FrameArena; }
    // Pooled memory for long-lived objects that are created and destroyed often, main thread only
    // (std::pmr::unsynchronized_pool_resource: no locking, so it is fast).
    [[nodiscard]] std::pmr::memory_resource* GetPoolAllocator() { return &m_Pool; }
    // The same, but safe to use from any thread (std::pmr::synchronized_pool_resource).
    [[nodiscard]] std::pmr::memory_resource* GetSharedPoolAllocator() { return &m_SharedPool; }
    // Counts the heap memory both pools have taken (they get their memory in big chunks).
    [[nodiscard]] TrackingResource::Stats GetPoolStats() const { return m_PoolHeap.GetStats(); }

#if EMERALD_WITH_ENTT
    [[nodiscard]] entt::registry& GetRegistry() { return m_Registry; }
#endif

protected:
    // Create GPU resources (buffers, pipelines) here; the renderer is ready.
    virtual void OnStart() {}
    virtual void OnEvent(const SDL_Event& /*event*/) {}
    // Game logic at a fixed rate (ApplicationSpec::FixedUpdateRate); dt is always the same.
    // Runs zero or more times per frame, before OnUpdate.
    virtual void OnFixedUpdate(f32 /*dt*/) {}
    // Once per frame with the real (variable) frame time.
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
    void ProcessEvent(const SDL_Event& event);
    void RenderFrame();

    ApplicationSpec m_Spec;

    // Memory resources first, so they outlive everything below that might use them. The pools
    // get their chunks from the heap through m_PoolHeap, which counts them.
    TrackingResource m_PoolHeap{std::pmr::new_delete_resource()};
    std::pmr::unsynchronized_pool_resource m_Pool{&m_PoolHeap};
    std::pmr::synchronized_pool_resource m_SharedPool{&m_PoolHeap};
    std::unique_ptr<FrameArena> m_FrameArena;
    std::unique_ptr<ThreadPool> m_ThreadPool;

    std::unique_ptr<Window> m_Window;
    std::unique_ptr<Renderer> m_Renderer;
    Input m_Input;
    FixedTimestep m_FixedTimestep;
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
