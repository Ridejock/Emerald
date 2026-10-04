#pragma once

#include <filesystem>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>

#include <SDL3/SDL_pixels.h>

#include "Emerald/Assets/Assets.h"
#include "Emerald/Audio/Audio.h"
#include "Emerald/Core/Defines.h"
#include "Emerald/Core/DevOptions.h"
#include "Emerald/Core/FixedTimestep.h"
#include "Emerald/Core/Log.h"
#include "Emerald/Core/ThreadPool.h"
#include "Emerald/Core/Window.h"
#include "Emerald/Entity/World.h"
#include "Emerald/Input/Input.h"
#include "Emerald/Input/InputRecording.h"
#include "Emerald/Memory/FrameArena.h"
#include "Emerald/Memory/TrackingResource.h"
#include "Emerald/Renderer/CrtEffect.h"
#include "Emerald/Renderer/Renderer.h"
#include "Emerald/Renderer/Renderer2D.h"
#include "Emerald/Scene/SceneStack.h"

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
    // The command line (main's argv), for engine options; other arguments are ignored:
    //   --gpu vulkan|d3d12|direct3d12|metal|auto   GPU backend (wins over SDL_GPU_DRIVER)
    //   --frames, --screenshot, --capture, --record, --replay: see DevOptions.h
    // Set it with `spec.Args = {argv, static_cast<usize>(argc)};`. It must outlive the constructor.
    std::span<char* const> Args;
    // Stop after this many frames (0 = run until the window is closed). Useful for CI/headless
    // runs. --frames N on the command line wins over it.
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
    [[nodiscard]] Renderer2D& GetRenderer2D() { return *m_Renderer2D; }
    // Action-based input: bind actions/axes to keys (e.g. in OnStart), then query them.
    [[nodiscard]] Input& GetInput() { return m_Input; }
    // Sound playback (see Audio.h; load sounds with LoadSound).
    [[nodiscard]] Audio& GetAudio() { return m_Audio; }
    // Textures, atlases, fonts and sounds by path, loaded once and hot reloaded in debug builds.
    [[nodiscard]] Assets& GetAssets() { return *m_Assets; }
    [[nodiscard]] u64 GetFrameCount() const { return m_FrameCount; }
    // The development options from the command line (--frames, --replay, ...; DevOptions.h).
    [[nodiscard]] const DevOptions& GetDevOptions() const { return m_Dev; }
    // A seed for the game's random numbers: new each run, and the recorded one in a replay (it
    // is saved in the recording). Seed generators from it so replays with randomness match.
    [[nodiscard]] u64 GetSeed() const { return m_Seed; }
    // Playing back a --replay file: the Input answers from the recording, not the devices.
    [[nodiscard]] bool IsReplaying() const { return m_Session.IsReplaying(); }
    // Title, gameplay, pause...: scenes pushed here get their hooks called after the app's own
    // (see SceneStack.h). An app that never pushes a scene works as before.
    [[nodiscard]] SceneStack& GetScenes() { return m_Scenes; }

    // Optional CRT monitor post-process over the whole frame (below the ImGui overlay), off by
    // default. Enabling it the first time creates its pipelines; returns false if that failed
    // (logged), and the frame is then drawn without it. Tune it with GetCrtParams (CrtEffect.h).
    bool SetCrtEnabled(bool enabled);
    [[nodiscard]] bool IsCrtEnabled() const { return m_CrtEnabled; }
    [[nodiscard]] CrtParams& GetCrtParams() { return m_Crt->GetParams(); }

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

    // An app-wide World, for apps without scenes (a scene usually owns its own; see
    // Entity/World.h). Its entities are destroyed before the assets are released.
    [[nodiscard]] World& GetWorld() { return m_World; }
    // The world's EnTT registry (kept for code written against it).
    [[nodiscard]] entt::registry& GetRegistry() { return m_World.GetRegistry(); }

protected:
    // Create GPU resources (buffers, pipelines) here; the renderer is ready.
    virtual void OnStart() {}
    virtual void OnEvent(const SDL_Event& /*event*/) {}
    // Game logic at a fixed rate (ApplicationSpec::FixedUpdateRate); dt is always the same.
    // Runs zero or more times per frame, before OnUpdate.
    virtual void OnFixedUpdate(f32 /*dt*/) {}
    // Once per frame with the real (variable) frame time.
    virtual void OnUpdate(f32 /*deltaSeconds*/) {}
    // Record 2D shapes (Begin / Draw* / End, see Renderer2D.h). Runs before the render pass
    // starts; the shapes are uploaded and then drawn after OnRender, below the ImGui overlay.
    virtual void OnRender2D(Renderer2D& /*renderer2D*/) {}
    // Record draw calls into the frame's main render pass (already cleared to ClearColor).
    // OnRender2D/OnRender are not called for skipped frames, e.g. while the window is minimized.
    virtual void OnRender(SDL_GPURenderPass* /*renderPass*/) {}
    virtual void OnImGui() {} // Only called when built with EMERALD_USE_IMGUI=ON
    // Release GPU resources here; the renderer is still alive.
    virtual void OnShutdown() {}

private:
    void InitImGui();
    void ShutdownImGui();
    void ProcessEvent(const SDL_Event& event);
    void RenderFrame();
    // The dev options' parts of the loop: start recording / replaying (after OnStart), screenshot
    // and capture requests for the coming frame, and what is left after it was drawn.
    void StartInputSession();
    void RequestCaptures();
    void FinishCaptures();

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
    std::unique_ptr<Renderer2D> m_Renderer2D;
    std::unique_ptr<CrtEffect> m_Crt = std::make_unique<CrtEffect>();
    std::unique_ptr<Assets> m_Assets; // after the renderer: its textures need the GPU device
    bool m_CrtEnabled = false;
    bool m_CrtInitialized = false;
    f32 m_FrameSeconds = 0.0f; // last frame's time, for the CRT afterglow
    Keyboard m_Keyboard;       // raw key state, fed from SDL events
    Gamepads m_Gamepads;       // raw gamepad state, fed from SDL events
    Mouse m_Mouse;             // raw mouse state, fed from SDL events
    Audio m_Audio;
    Input m_Input{m_Keyboard, m_Gamepads, &m_Mouse}; // actions on top of all three
    SceneStack m_Scenes{&m_Input};                   // empties itself before the assets go
    FixedTimestep m_FixedTimestep;
    bool m_SdlInitialized = false;
    bool m_RendererInitialized = false;
#if EMERALD_WITH_IMGUI
    bool m_ImGuiInitialized = false;
#endif
    bool m_Running = false;
    u64 m_FrameCount = 0;
    DevOptions m_Dev;
    bool m_DevFailed = false;               // e.g. the --replay file could not be read
    std::optional<InputRecording> m_Replay; // loaded early (for its seed), played from Run
    InputSession m_Session;
    u64 m_Seed = 0;
    u64 m_ShotFrame = 0;      // the frame (1 = first) --screenshot saves
    u64 m_CaptureFrameNs = 0; // --capture: the fixed frame time (0 = real time)
    World m_World;
};

} // namespace Emerald
