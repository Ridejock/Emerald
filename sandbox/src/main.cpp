#include <array>
#include <charconv>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

#include <SDL3/SDL.h>

#include <Emerald/Emerald.h>

#if EMERALD_WITH_IMGUI
#include <imgui.h>
#endif

namespace {

// Layout of one vertex in the vertex buffer; must match `Input` in Triangle.vert.hlsl.
struct Vertex {
    f32 X, Y;    // TEXCOORD0: position in normalized device coordinates (-1..1)
    f32 R, G, B; // TEXCOORD1: color
};

// Uniform data pushed per draw; must match `cbuffer Transform` in Triangle.vert.hlsl.
struct Transform {
    f32 OffsetX = 0.0f, OffsetY = 0.0f;
    f32 ScaleX = 1.0f, ScaleY = 1.0f;
};

constexpr std::array<Vertex, 3> kTriangle{{
    {0.0f, 0.6f, 1.0f, 0.2f, 0.2f},   // top: red
    {-0.6f, -0.5f, 0.2f, 1.0f, 0.3f}, // bottom left: green
    {0.6f, -0.5f, 0.2f, 0.4f, 1.0f},  // bottom right: blue
}};

#if EMERALD_WITH_ENTT
// Unit quad with its top-left corner at the origin (y points up in NDC, so it extends down).
constexpr f32 kG = 0.8f, kR = 0.18f, kB = 0.44f; // emerald green
constexpr std::array<Vertex, 6> kQuad{{
    {0.0f, 0.0f, kR, kG, kB},
    {1.0f, 0.0f, kR, kG, kB},
    {1.0f, -1.0f, kR, kG, kB},
    {0.0f, 0.0f, kR, kG, kB},
    {1.0f, -1.0f, kR, kG, kB},
    {0.0f, -1.0f, kR, kG, kB},
}};
constexpr f32 kQuadSize = 50.0f; // pixels

struct Position {
    f32 X = 0.0f, Y = 0.0f; // pixels, top-left origin
};
struct Velocity {
    f32 X = 0.0f, Y = 0.0f;
};
#endif

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
        registry.emplace<Position>(entity, 100.0f, 100.0f);
        registry.emplace<Velocity>(entity, 120.0f, 80.0f);
        EM_INFO("EnTT enabled: created entity {}", static_cast<u32>(entity));
#endif
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
        i32 w = 0, h = 0;
        SDL_GetWindowSize(GetWindow().GetNativeWindow(), &w, &h);
        GetRegistry().view<Position, Velocity>().each([&](Position& p, Velocity& v) {
            p.X += v.X * dt;
            p.Y += v.Y * dt;
            if (p.X < 0.0f || p.X > static_cast<f32>(w) - kQuadSize)
                v.X = -v.X;
            if (p.Y < 0.0f || p.Y > static_cast<f32>(h) - kQuadSize)
                v.Y = -v.Y;
        });
#endif
    }

    void OnRender(SDL_GPURenderPass* pass) override
    {
        SDL_GPUCommandBuffer* cmd = GetRenderer().GetCommandBuffer();
        SDL_BindGPUGraphicsPipeline(pass, m_Pipeline);

        // The triangle: identity transform. Uniform data is pushed into the command buffer and
        // applies to the draws that follow (slot 0 = register(b0, space1)).
        const Transform identity;
        SDL_PushGPUVertexUniformData(cmd, 0, &identity, sizeof(identity));
        const SDL_GPUBufferBinding triangle{m_TriangleBuffer, 0};
        SDL_BindGPUVertexBuffers(pass, 0, &triangle, 1);
        SDL_DrawGPUPrimitives(pass, static_cast<u32>(kTriangle.size()), 1, 0, 0);

#if EMERALD_WITH_ENTT
        // One quad per entity, positioned by converting its pixel position to NDC.
        i32 w = 0, h = 0;
        SDL_GetWindowSize(GetWindow().GetNativeWindow(), &w, &h);
        const f32 fw = static_cast<f32>(w), fh = static_cast<f32>(h);
        const SDL_GPUBufferBinding quad{m_QuadBuffer, 0};
        SDL_BindGPUVertexBuffers(pass, 0, &quad, 1);
        GetRegistry().view<Position>().each([&](const Position& p) {
            const Transform t{p.X / fw * 2.0f - 1.0f, 1.0f - p.Y / fh * 2.0f, kQuadSize / fw * 2.0f,
                              kQuadSize / fh * 2.0f};
            SDL_PushGPUVertexUniformData(cmd, 0, &t, sizeof(t));
            SDL_DrawGPUPrimitives(pass, static_cast<u32>(kQuad.size()), 1, 0, 0);
        });
#endif
    }

    void OnImGui() override
    {
#if EMERALD_WITH_IMGUI
        ImGui::SetNextWindowPos(ImVec2(20.0f, 20.0f), ImGuiCond_FirstUseEver);
        ImGui::Begin("Emerald");
        ImGui::Text("Renderer: SDL GPU (%s)", SDL_GetGPUDeviceDriver(GetRenderer().GetDevice()));
        ImGui::Text("Frame: %llu", static_cast<unsigned long long>(GetFrameCount()));
        ImGui::Text("Time:  %.2f s", static_cast<f64>(m_Time));
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
             .offset = offsetof(Vertex, X)},
            {.location = 1, // TEXCOORD1
             .buffer_slot = 0,
             .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
             .offset = offsetof(Vertex, R)},
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
