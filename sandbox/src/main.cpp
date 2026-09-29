#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <future>
#include <numeric>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL.h>

#include <Emerald/Emerald.h>

#if EMERALD_WITH_IMGUI
#include <imgui.h>
#endif

namespace {

using Emerald::Mat4;
using Emerald::Vec2;
using Emerald::Vec3;

// Layout of one vertex in the vertex buffer; must match `Input` in Triangle.vert.hlsl.
struct Vertex {
    Vec2 Position; // TEXCOORD0: model-space position
    Vec3 Color;    // TEXCOORD1
};

// Uniform data pushed per draw; must match `cbuffer Uniforms` in Triangle.vert.hlsl.
// Mat4 is column-major like HLSL's float4x4, so it can be copied as-is.
struct Uniforms {
    Mat4 Transform; // projection * model
};

// Equilateral triangle around the origin with radius 1, in a y-down space (top vertex at y = -1)
// to match the pixel-space projection used for drawing.
constexpr std::array<Vertex, 3> kTriangle{{
    {{0.0f, -1.0f}, {1.0f, 0.2f, 0.2f}},   // top: red
    {{-0.866f, 0.5f}, {0.2f, 1.0f, 0.3f}}, // bottom left: green
    {{0.866f, 0.5f}, {0.2f, 0.4f, 1.0f}},  // bottom right: blue
}};

#if EMERALD_WITH_ENTT
// Unit quad from (0, 0) (top-left) to (1, 1) (bottom-right) in pixel space (+Y down).
constexpr Vec3 kEmerald{0.18f, 0.8f, 0.44f};
constexpr std::array<Vertex, 6> kQuad{{
    {{0.0f, 0.0f}, kEmerald},
    {{1.0f, 0.0f}, kEmerald},
    {{1.0f, 1.0f}, kEmerald},
    {{0.0f, 0.0f}, kEmerald},
    {{1.0f, 1.0f}, kEmerald},
    {{0.0f, 1.0f}, kEmerald},
}};
constexpr f32 kQuadSize = 50.0f; // pixels

struct Position {
    Vec2 Value; // pixels, top-left origin
};
struct Velocity {
    Vec2 Value; // pixels per second
};
#endif

// One draw call, collected into a per-frame list before recording (see OnRender).
struct DrawItem {
    Mat4 Transform; // projection * model
    SDL_GPUBuffer* Vertices = nullptr;
    u32 VertexCount = 0;
};

struct SandboxOptions {
    u64 Frames = 0;
    std::string ScreenshotPath;
};

class Sandbox final : public Emerald::Application {
public:
    Sandbox(const Emerald::ApplicationSpec& spec, SandboxOptions options)
        : Application(spec), m_Options(std::move(options))
    {
    }

protected:
    void OnStart() override
    {
        EM_INFO("Sandbox started - press Escape or close the window to quit");
        if (!CreateGpuResources()) {
            EM_ERROR("Failed to create GPU resources, quitting");
            Quit();
            return;
        }
#if EMERALD_WITH_ENTT
        auto& registry = GetRegistry();
        const auto entity = registry.create();
        registry.emplace<Position>(entity, Vec2(160.0f, 220.0f)); // below the ImGui panel
        registry.emplace<Velocity>(entity, Vec2(120.0f, 80.0f));
        EM_INFO("EnTT enabled: created entity {}", static_cast<u32>(entity));
#endif
        RunThreadPoolDemo();
    }

    void OnEvent(const SDL_Event& event) override
    {
        if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)
            Quit();
    }

    void OnUpdate(f32 dt) override
    {
        m_Time += dt;

        // Capture the last frame of a --frames run (or frame 60 otherwise).
        const u64 shotFrame = m_Options.Frames != 0 ? m_Options.Frames : 60;
        if (!m_Options.ScreenshotPath.empty() && GetFrameCount() + 1 == shotFrame)
            GetRenderer().RequestScreenshot(m_Options.ScreenshotPath);

#if EMERALD_WITH_ENTT
        // Bounce the quads off the window edges.
        const Vec2 limit = GetWindowSize() - Vec2(kQuadSize);
        GetRegistry().view<Position, Velocity>().each([&](Position& p, Velocity& v) {
            p.Value += v.Value * dt;
            if (p.Value.x < 0.0f || p.Value.x > limit.x)
                v.Value.x = -v.Value.x;
            if (p.Value.y < 0.0f || p.Value.y > limit.y)
                v.Value.y = -v.Value.y;
        });
#endif
    }

    void OnRender(SDL_GPURenderPass* pass) override
    {
        SDL_GPUCommandBuffer* cmd = GetRenderer().GetCommandBuffer();
        SDL_BindGPUGraphicsPipeline(pass, m_Pipeline);

        // Everything is drawn in pixel space: (0, 0) top-left, window size bottom-right, +Y down.
        // Window size is in the same units as mouse/window coordinates; the projection maps it to
        // the whole swapchain whatever its pixel size.
        const Vec2 size = GetWindowSize();
        const Mat4 projection = Mat4::OrthoPixelSpace(size.x, size.y);

        // First collect this frame's draws into a list, then record them. The list lives in the
        // frame arena: allocating is a pointer bump and there is nothing to free - the arena is
        // reset when the next frame starts.
        Emerald::PmrVector<DrawItem> draws(GetFrameAllocator());

        // The triangle: scaled to 40% of the window, slowly rotating, centered. Read right to
        // left: scale the unit triangle, rotate it, move it to the center, then project.
        const f32 radius = 0.4f * Emerald::Min(size.x, size.y);
        const Mat4 model =
            Mat4::Translate(size * 0.5f) * Mat4::RotateZ(0.5f * m_Time) * Mat4::Scale(Vec2(radius));
        draws.push_back({projection * model, m_TriangleBuffer, static_cast<u32>(kTriangle.size())});

#if EMERALD_WITH_ENTT
        // One quad per entity: the unit quad scaled to kQuadSize pixels and moved to its position.
        GetRegistry().view<Position>().each([&](const Position& p) {
            draws.push_back({projection * Mat4::Translate(p.Value) * Mat4::Scale(kQuadSize),
                             m_QuadBuffer, static_cast<u32>(kQuad.size())});
        });
#endif

        for (const DrawItem& draw : draws) {
            // Uniform data is pushed into the command buffer and applies to the draws that follow
            // (slot 0 = register(b0, space1)).
            const Uniforms uniforms{draw.Transform};
            SDL_PushGPUVertexUniformData(cmd, 0, &uniforms, sizeof(uniforms));
            const SDL_GPUBufferBinding binding{draw.Vertices, 0};
            SDL_BindGPUVertexBuffers(pass, 0, &binding, 1);
            SDL_DrawGPUPrimitives(pass, draw.VertexCount, 1, 0, 0);
        }
    }

    void OnImGui() override
    {
#if EMERALD_WITH_IMGUI
        ImGui::SetNextWindowPos(ImVec2(20.0f, 20.0f), ImGuiCond_FirstUseEver);
        ImGui::Begin("Emerald");
        ImGui::Text("Renderer: SDL GPU (%s)", SDL_GetGPUDeviceDriver(GetRenderer().GetDevice()));
        ImGui::Text("Frame: %llu", static_cast<unsigned long long>(GetFrameCount()));
        ImGui::Text("Time:  %.2f s", static_cast<f64>(m_Time));
        ImGui::Separator();
        ImGui::Text("Workers: %u", GetThreadPool().GetThreadCount());
        const Emerald::FrameArena::Stats arena = GetFrameArena().GetStats();
        ImGui::Text("Frame arena: %zu B last frame, peak %zu / %zu B", arena.LastFrameBytes,
                    arena.PeakBytes, arena.Capacity);
        ImGui::Text("Frames over capacity: %llu",
                    static_cast<unsigned long long>(arena.OverflowFrames));
        const Emerald::TrackingResource::Stats pools = GetPoolStats();
        ImGui::Text("Pools: %zu B in use (peak %zu B), %zu chunks", pools.BytesInUse,
                    pools.PeakBytes, pools.AllocationsInUse);
        ImGui::End();
#endif
    }

    void OnShutdown() override
    {
        SDL_GPUDevice* device = GetRenderer().GetDevice();
        if (m_Pipeline)
            SDL_ReleaseGPUGraphicsPipeline(device, m_Pipeline);
        if (m_TriangleBuffer)
            SDL_ReleaseGPUBuffer(device, m_TriangleBuffer);
        if (m_QuadBuffer)
            SDL_ReleaseGPUBuffer(device, m_QuadBuffer);
        EM_INFO("Sandbox shutting down after {:.2f}s", m_Time);
    }

private:
    // Shows the thread pool once at startup: fills an array in parallel with ParallelFor, then
    // sums it in a background task and waits for the result through its future.
    void RunThreadPoolDemo()
    {
        Emerald::ThreadPool& pool = GetThreadPool();
        constexpr usize kCount = 1'000'000;
        std::vector<f32> values(kCount);

        const u64 start = SDL_GetTicksNS();
        // Each index is written by exactly one thread, so no locking is needed.
        pool.ParallelFor(kCount,
                         [&values](usize i) { values[i] = std::sqrt(static_cast<f32>(i)); });
        std::future<f64> sum =
            pool.Submit([&values] { return std::accumulate(values.begin(), values.end(), 0.0); });
        const f64 total = sum.get(); // blocks until the task has run
        const f64 ms = static_cast<f64>(SDL_GetTicksNS() - start) / 1e6;

        EM_INFO("Thread pool demo: {} square roots on {} workers + main thread, summed by a task: "
                "{:.0f} ({:.2f} ms)",
                kCount, pool.GetThreadCount(), total, ms);
    }

    [[nodiscard]] Vec2 GetWindowSize()
    {
        i32 w = 0, h = 0;
        SDL_GetWindowSize(GetWindow().GetNativeWindow(), &w, &h);
        return {static_cast<f32>(w), static_cast<f32>(h)};
    }

    bool CreateGpuResources()
    {
        Emerald::Renderer& renderer = GetRenderer();
        SDL_GPUDevice* device = renderer.GetDevice();

        // Resource counts (1 uniform buffer in the vertex shader) come from the .json reflection.
        SDL_GPUShader* vertex = Emerald::LoadShader(device, {.Name = "Triangle.vert"});
        SDL_GPUShader* fragment = Emerald::LoadShader(device, {.Name = "Triangle.frag"});

        // One vertex buffer (slot 0) with interleaved position + color.
        const SDL_GPUVertexBufferDescription buffers[] = {{
            .slot = 0,
            .pitch = sizeof(Vertex),
            .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
            .instance_step_rate = 0,
        }};
        const SDL_GPUVertexAttribute attributes[] = {
            {.location = 0, // TEXCOORD0
             .buffer_slot = 0,
             .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
             .offset = offsetof(Vertex, Position)},
            {.location = 1, // TEXCOORD1
             .buffer_slot = 0,
             .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
             .offset = offsetof(Vertex, Color)},
        };

        if (vertex && fragment) {
            m_Pipeline = Emerald::CreateGraphicsPipeline(
                device, {.VertexShader = vertex,
                         .FragmentShader = fragment,
                         .VertexBuffers = buffers,
                         .VertexAttributes = attributes,
                         .ColorFormat = renderer.GetSwapchainFormat()});
        }
        // The pipeline keeps what it needs; the shader objects can go.
        if (vertex)
            SDL_ReleaseGPUShader(device, vertex);
        if (fragment)
            SDL_ReleaseGPUShader(device, fragment);

        m_TriangleBuffer = renderer.CreateBuffer(SDL_GPU_BUFFERUSAGE_VERTEX, kTriangle.data(),
                                                 static_cast<u32>(sizeof(kTriangle)));
#if EMERALD_WITH_ENTT
        m_QuadBuffer = renderer.CreateBuffer(SDL_GPU_BUFFERUSAGE_VERTEX, kQuad.data(),
                                             static_cast<u32>(sizeof(kQuad)));
        if (!m_QuadBuffer)
            return false;
#endif
        return m_Pipeline && m_TriangleBuffer;
    }

    SandboxOptions m_Options;
    f32 m_Time = 0.0f;
    SDL_GPUGraphicsPipeline* m_Pipeline = nullptr;
    SDL_GPUBuffer* m_TriangleBuffer = nullptr;
    SDL_GPUBuffer* m_QuadBuffer = nullptr;
};

SandboxOptions ParseOptions(i32 argc, char** argv)
{
    SandboxOptions options;
    for (i32 i = 1; i + 1 < argc; ++i) {
        const std::string_view arg(argv[i]);
        const std::string_view value(argv[i + 1]);
        if (arg == "--frames") {
            std::from_chars(value.data(), value.data() + value.size(), options.Frames);
            ++i;
        } else if (arg == "--screenshot") {
            options.ScreenshotPath = value;
            ++i;
        }
    }
    return options;
}

} // namespace

int main(int argc, char** argv)
{
    SandboxOptions options = ParseOptions(argc, argv);

    Emerald::ApplicationSpec spec;
    spec.Window.Title = "Emerald Sandbox";
    spec.MaxFrames = options.Frames;
    spec.ShaderFormats = EMERALD_SHADER_FORMATS; // formats generated by emerald_add_shaders()

    Sandbox app(spec, std::move(options));
    return app.Run();
}
