#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <future>
#include <numeric>
#include <optional>
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

using Emerald::GamepadAxis;
using Emerald::GamepadButton;
using Emerald::Key;
using Emerald::Mat4;
using Emerald::Vec2;
using Emerald::Vec3;
using Emerald::Vec4;

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

// A small arrow-shaped marker for the input demo, pointing along +X (model space, pixels).
constexpr std::array<Vec2, 4> kArrow{
    {{14.0f, 0.0f}, {-10.0f, -9.0f}, {-5.0f, 0.0f}, {-10.0f, 9.0f}}};

// A five-pointed star (alternating outer/inner radius), built once at startup.
std::array<Vec2, 10> MakeStar()
{
    std::array<Vec2, 10> points{};
    for (usize i = 0; i < points.size(); ++i) {
        const f32 angle = Emerald::TwoPi * static_cast<f32>(i) / 10.0f - Emerald::HalfPi;
        const f32 radius = (i % 2 == 0) ? 1.0f : 0.45f;
        points[i] = Vec2(std::cos(angle), std::sin(angle)) * radius;
    }
    return points;
}
const std::array<Vec2, 10> kStar = MakeStar();

// A tiny procedural sprite sheet (no image files needed): a 16 x 16 pixel-art gem at (0, 0) and
// a ring at (16, 0). Real games load one with TextureAtlas::Load(device, "atlas.png", ...).
Emerald::Image MakeSpriteSheet()
{
    Emerald::Image image;
    image.Width = 32;
    image.Height = 16;
    image.Pixels.assign(static_cast<usize>(image.Width * image.Height) * 4, 0); // transparent
    const auto put = [&](i32 x, i32 y, u8 r, u8 g, u8 b) {
        u8* p = &image.Pixels[static_cast<usize>(y * image.Width + x) * 4];
        p[0] = r;
        p[1] = g;
        p[2] = b;
        p[3] = 255;
    };
    for (i32 y = 0; y < 16; ++y) {
        for (i32 x = 0; x < 16; ++x) {
            // Gem: a diamond, lighter on the top-left facets, with a dark outline.
            const i32 dx = x < 8 ? 7 - x : x - 8;
            const i32 dy = y < 8 ? 7 - y : y - 8;
            if (dx + dy <= 7) {
                const bool edge = dx + dy == 7;
                const bool light = (x < 8) == (y < 8);
                if (edge)
                    put(x, y, 10, 70, 40);
                else if (x == 5 && y == 4)
                    put(x, y, 230, 255, 240); // highlight
                else
                    put(x, y, light ? 70 : 30, light ? 220 : 160, light ? 120 : 90);
            }
            // Ring: pixels between two radii.
            const f32 rx = static_cast<f32>(x) - 7.5f;
            const f32 ry = static_cast<f32>(y) - 7.5f;
            const f32 d = std::sqrt(rx * rx + ry * ry);
            if (d > 4.5f && d < 7.5f)
                put(16 + x, y, 255, d < 6.0f ? 220 : 170, 60);
        }
    }
    return image;
}

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

        // Controls are actions bound to keys and gamepad inputs; the code below only uses the
        // action names.
        Emerald::Input& input = GetInput();
        input.BindAxis("MoveX", Key::A, Key::D);
        input.BindAxis("MoveX", Key::Left, Key::Right);
        input.BindAxis("MoveX", GamepadButton::DPadLeft, GamepadButton::DPadRight);
        input.BindAxis("MoveX", GamepadAxis::LeftX);
        input.BindAxis("MoveY", Key::W, Key::S);
        input.BindAxis("MoveY", Key::Up, Key::Down);
        input.BindAxis("MoveY", GamepadButton::DPadUp, GamepadButton::DPadDown);
        input.BindAxis("MoveY", GamepadAxis::LeftY); // +Y is down on screen and on the stick
        input.BindAction("Pulse", {Key::Space});
        input.BindAction("Pulse", {GamepadButton::South});
        input.BindAction("Quit", {Key::Escape});

        // A short generated "blip" for the pulse, rising in pitch (files would use
        // Emerald::LoadSound("x.mp3")).
        using namespace Emerald::Synth;
        std::vector<f32> blip = Generate({.Shape = Wave::Sine,
                                          .Seconds = 0.15f,
                                          .StartHz = 520.0f,
                                          .EndHz = 880.0f,
                                          .Volume = 0.4f});
        ApplyDecay(blip, 0.03f);
        m_Blip = ToSound(blip);

        // Sprites: upload the generated sheet (nearest filtering: crisp pixel art) and name its
        // two regions, like an atlas.json would.
        if (std::optional<Emerald::Texture> sheet =
                Emerald::Texture::Create(GetRenderer().GetDevice(), MakeSpriteSheet())) {
            m_Atlas = Emerald::TextureAtlas::Create(std::move(*sheet),
                                                    {{"gem", {{0.0f, 0.0f}, {16.0f, 16.0f}}},
                                                     {"ring", {{16.0f, 0.0f}, {16.0f, 16.0f}}}});
        }
    }

    // Input + fixed-step demo: move the arrow with WASD / arrow keys / left stick / d-pad (at
    // 120 Hz, independent of the frame rate), Space or South (A / Cross) starts a ring pulse and
    // a short rumble.
    void OnFixedUpdate(f32 dt) override
    {
        Emerald::Input& input = GetInput();
        Vec2 direction(input.GetAxis("MoveX"), input.GetAxis("MoveY"));
        const f32 length = Emerald::Length(direction);
        if (length > 0.0f) {
            // Keys give length 1 or 1.41 (diagonal), a stick anything up to 1: cap it at 1 so
            // diagonals are not faster and a half-tilted stick moves at half speed.
            if (length > 1.0f)
                direction = direction / length;
            m_ArrowPosition += direction * (300.0f * dt);
            m_ArrowAngle = std::atan2(direction.y, direction.x);
        }
        // Keep it on screen.
        m_ArrowPosition = Emerald::Min(Emerald::Max(m_ArrowPosition, Vec2(0.0f)), GetViewSize());

        if (input.WasActionPressed("Pulse")) {
            m_PulseAge = 0.0f;
            input.Rumble(0.3f, 0.6f, 120);
            // Panned towards the side of the window the arrow is on.
            const f32 pan = m_ArrowPosition.x / GetViewSize().x * 2.0f - 1.0f;
            GetAudio().Play(m_Blip, {.Volume = 0.7f, .Pan = pan * 0.8f});
        }
        m_PulseAge += dt;
    }

    void OnUpdate(f32 dt) override
    {
        m_Time += dt;
        if (GetInput().WasActionPressed("Quit"))
            Quit();

        // Capture the last frame of a --frames run (or frame 60 otherwise).
        const u64 shotFrame = m_Options.Frames != 0 ? m_Options.Frames : 60;
        if (!m_Options.ScreenshotPath.empty() && GetFrameCount() + 1 == shotFrame)
            GetRenderer().RequestScreenshot(m_Options.ScreenshotPath);

#if EMERALD_WITH_ENTT
        // Bounce the quads off the window edges.
        const Vec2 limit = GetViewSize() - Vec2(kQuadSize);
        GetRegistry().view<Position, Velocity>().each([&](Position& p, Velocity& v) {
            p.Value += v.Value * dt;
            if (p.Value.x < 0.0f || p.Value.x > limit.x)
                v.Value.x = -v.Value.x;
            if (p.Value.y < 0.0f || p.Value.y > limit.y)
                v.Value.y = -v.Value.y;
        });
#endif
    }

    // 2D shapes: recorded here, drawn by the engine on top of OnRender's triangle.
    void OnRender2D(Emerald::Renderer2D& r) override
    {
        const Vec2 size = GetViewSize();
        const Vec4 green{0.18f, 0.8f, 0.44f, 1.0f};
        const Vec4 white{1.0f, 1.0f, 1.0f, 1.0f};
        const Vec4 dim{1.0f, 1.0f, 1.0f, 0.25f}; // the pipeline alpha-blends

        r.Begin(Mat4::OrthoPixelSpace(size.x, size.y));

        // Frame around the window and a faint grid.
        r.DrawRect({10.0f, 10.0f}, size - Vec2(20.0f), green);
        for (f32 x = 80.0f; x < size.x - 10.0f; x += 80.0f)
            r.DrawLine({x, 10.0f}, {x, size.y - 10.0f}, Vec4(1.0f, 1.0f, 1.0f, 0.06f));

        // Spinning stars in the bottom corners, one of them pulsing in size.
        const f32 pulse = 1.0f + 0.2f * std::sin(3.0f * m_Time);
        r.DrawPolygon(
            kStar, {1.0f, 0.85f, 0.2f, 1.0f},
            {.Position = {90.0f, size.y - 90.0f}, .Rotation = m_Time, .Scale = Vec2(60.0f)});
        r.DrawPolygon(kStar, {0.3f, 0.7f, 1.0f, 1.0f},
                      {.Position = size - Vec2(90.0f),
                       .Rotation = -0.7f * m_Time,
                       .Scale = Vec2(60.0f * pulse)});

        // Concentric circles around the center with increasing segment counts.
        for (u32 i = 0; i < 4; ++i)
            r.DrawCircle(size * 0.5f, 60.0f + 40.0f * static_cast<f32>(i), dim, 8u << i);

        // The input demo arrow and its Space pulse (a ring growing for half a second).
        DrawSprites(r, size);

        r.DrawPolygon(kArrow, white, {.Position = m_ArrowPosition, .Rotation = m_ArrowAngle});
        if (m_PulseAge < 0.5f)
            r.DrawCircle(m_ArrowPosition, 20.0f + 200.0f * m_PulseAge,
                         {1.0f, 1.0f, 1.0f, 1.0f - 2.0f * m_PulseAge});

        r.End();
    }

    // Sprite demo: scaling, rotation, flipping, tint and alpha, pixel snapping, and layering in
    // call order with lines.
    void DrawSprites(Emerald::Renderer2D& r, const Vec2& size)
    {
        if (!m_Atlas)
            return;
        const Emerald::Sprite gem = m_Atlas->Get("gem");
        const Emerald::Sprite ring = m_Atlas->Get("ring");

        // A row along the top: plain, flipped, tinted, half transparent (4x, snapped to pixels).
        const f32 y = 70.0f;
        const Emerald::SpriteOptions big{.Scale = Vec2(4.0f), .PixelSnap = true};
        r.DrawSprite(gem, {size.x * 0.5f - 200.0f, y}, big);
        r.DrawSprite(gem, {size.x * 0.5f - 120.0f, y}, {.Scale = Vec2(4.0f), .FlipX = true});
        r.DrawSprite(gem, {size.x * 0.5f - 40.0f, y},
                     {.Scale = Vec2(4.0f), .Tint = {1.0f, 0.4f, 0.4f, 1.0f}});
        r.DrawSprite(gem, {size.x * 0.5f + 40.0f, y},
                     {.Scale = Vec2(4.0f), .Tint = {0.5f, 0.7f, 1.0f, 1.0f}});
        r.DrawSprite(gem, {size.x * 0.5f + 120.0f, y},
                     {.Scale = Vec2(4.0f), .Tint = {1.0f, 1.0f, 1.0f, 0.4f}});
        r.DrawSprite(ring, {size.x * 0.5f + 200.0f, y}, big);

        // Layering: a spinning gem, a line on top of it, then a ring on top of the line.
        const Vec2 center = size * 0.5f;
        r.DrawSprite(gem, center, {.Scale = Vec2(8.0f), .Rotation = 0.6f * m_Time});
        r.DrawLine(center - Vec2(90.0f, 0.0f), center + Vec2(90.0f, 0.0f),
                   {1.0f, 1.0f, 1.0f, 1.0f});
        r.DrawSprite(ring, center + Vec2(40.0f, 0.0f), {.Scale = Vec2(3.0f), .Rotation = -m_Time});
    }

    void OnRender(SDL_GPURenderPass* pass) override
    {
        SDL_GPUCommandBuffer* cmd = GetRenderer().GetCommandBuffer();
        SDL_BindGPUGraphicsPipeline(pass, m_Pipeline);

        // Everything is drawn in pixel space: (0, 0) top-left, window size bottom-right, +Y down.
        // Window size is in the same units as mouse/window coordinates; the projection maps it to
        // the whole swapchain whatever its pixel size.
        const Vec2 size = GetViewSize();
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
        ImGui::Text("Fixed update: %.0f Hz", static_cast<f64>(1.0f / GetFixedDeltaSeconds()));
        const Emerald::Vec2i pixels = GetWindowSizeInPixels();
        ImGui::Text("Window: %d x %d px", pixels.x, pixels.y);
        ImGui::Text("Renderer2D: %u lines, %u sprites, %u draw calls",
                    GetRenderer2D().GetLastFrameLineCount(),
                    GetRenderer2D().GetLastFrameSpriteCount(),
                    GetRenderer2D().GetLastFrameDrawCalls());
        ImGui::Text("Move the arrow: WASD / arrows / left stick, pulse: Space / %s",
                    GetInput().GetGamepads().GetButtonLabel(GamepadButton::South));
        ShowGamepads();
        ImGui::Text("Audio: %s", GetAudio().IsAvailable() ? "on" : "no device");
        ImGui::Separator();
        ImGui::Text("Workers: %u", GetThreadPool().GetThreadCount());
        const Emerald::FrameArena::Stats arena = GetFrameArena().GetStats();
        ImGui::Text("Frame arena: %zu B last frame, peak %zu B of %zu KiB", arena.LastFrameBytes,
                    arena.PeakBytes, arena.Capacity / 1024);
        // Frames whose arena allocations did not fit and went to the heap (should stay 0).
        ImGui::Text("Frames that spilled to the heap: %llu",
                    static_cast<unsigned long long>(arena.OverflowFrames));
        const Emerald::TrackingResource::Stats pools = GetPoolStats();
        ImGui::Text("Pools: %zu B in use (peak %zu B), %zu chunks", pools.BytesInUse,
                    pools.PeakBytes, pools.AllocationsInUse);
        ImGui::End();
#endif
    }

#if EMERALD_WITH_IMGUI
    // Connected gamepads with their live values: raw from SDL, and after the deadzone.
    void ShowGamepads()
    {
        const Emerald::Input& input = GetInput();
        const Emerald::Gamepads& pads = input.GetGamepads();
        ImGui::Text("Axes: MoveX %+.2f  MoveY %+.2f", static_cast<f64>(input.GetAxis("MoveX")),
                    static_cast<f64>(input.GetAxis("MoveY")));
        if (pads.GetCount() == 0) {
            ImGui::TextDisabled("No gamepad connected");
            return;
        }
        const auto value = [&](usize pad, GamepadAxis axis) {
            return static_cast<f64>(pads.GetRawAxis(pad, axis));
        };
        const auto deadzoned = [&](usize pad, GamepadAxis axis) {
            return static_cast<f64>(pads.GetAxis(pad, axis));
        };
        for (usize i = 0; i < pads.GetCount(); ++i) {
            const Emerald::Gamepads::PadInfo& info = pads.GetInfo(i);
            ImGui::Text("Pad %zu: %s (%s)", i, info.Name.c_str(),
                        Emerald::GetGamepadTypeName(info.Type));
            ImGui::Text("  Left  %+.2f %+.2f -> %+.2f %+.2f", value(i, GamepadAxis::LeftX),
                        value(i, GamepadAxis::LeftY), deadzoned(i, GamepadAxis::LeftX),
                        deadzoned(i, GamepadAxis::LeftY));
            ImGui::Text("  Right %+.2f %+.2f -> %+.2f %+.2f", value(i, GamepadAxis::RightX),
                        value(i, GamepadAxis::RightY), deadzoned(i, GamepadAxis::RightX),
                        deadzoned(i, GamepadAxis::RightY));
            ImGui::Text("  Triggers L %.2f  R %.2f", value(i, GamepadAxis::LeftTrigger),
                        value(i, GamepadAxis::RightTrigger));
        }
        // Buttons held on any pad, with the labels of the pad used last.
        std::string held;
        for (u32 b = 0; b < static_cast<u32>(GamepadButton::Count); ++b) {
            const auto button = static_cast<GamepadButton>(b);
            if (pads.IsButtonDown(button)) {
                held += pads.GetButtonLabel(button);
                held += "  ";
            }
        }
        ImGui::Text("Held: %s", held.empty() ? "-" : held.c_str());
    }
#endif

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

    // Window size in window coordinates as floats: the pixel-space projections use it.
    [[nodiscard]] Vec2 GetViewSize() const { return Vec2(GetWindowSize()); }

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
    Vec2 m_ArrowPosition{200.0f, 400.0f};
    Emerald::Sound m_Blip;
    f32 m_ArrowAngle = 0.0f;
    f32 m_PulseAge = 1.0f;                        // seconds since Space was pressed
    std::optional<Emerald::TextureAtlas> m_Atlas; // the sprite demo's sheet
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
