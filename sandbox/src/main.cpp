// The sandbox: every engine feature in one app, organised as scenes on the Application's
// SceneStack (Emerald/Scene):
//
//   Title (TitleScene.h) --Enter--> Camera demo (DemoScene.h) --T--> Tilemap room (TilemapScene.h)
//                                        ^                                  |
//                                        T                                  T
//                                        |                                  v
//                     Platformer (PlatformerScene.h) <--T-- Entity swarm (SwarmScene.h)
//
//   M in any of the four pushes Pause (PauseScene.h, drawn over the scene below).
//
// The app itself keeps what is global: input bindings, the shared assets (Shared.h), the
// triangle pipeline and the app-level entity's quad drawn under every scene, Escape / C,
// and the ImGui "Emerald" window with the scene stack (the scenes append their own
// sections to it).

#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <future>
#include <memory>
#include <numeric>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL.h>

#include <Emerald/Emerald.h>

#include "DemoScene.h"
#include "LightingScene.h"
#include "OptionsScene.h"
#include "PauseScene.h"
#include "PlatformerScene.h"
#include "Shared.h"
#include "SwarmScene.h"
#include "TilemapScene.h"
#include "TitleScene.h"

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

// Unit quad from (0, 0) (top-left) to (1, 1) (bottom-right) in pixel space (+Y down): the app-wide
// World's entity (see OnStart).
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

// One draw call, collected into a per-frame list before recording (see OnRender).
struct DrawItem {
    Mat4 Transform; // projection * model
    SDL_GPUBuffer* Vertices = nullptr;
    u32 VertexCount = 0;
};

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

class Sandbox final : public Emerald::Application {
public:
    Sandbox(const Emerald::ApplicationSpec& spec, SandboxOptions options)
        : Application(spec), m_Shared(*this, std::move(options))
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
        // An app-level entity, outside of any scene: a quad bouncing around under every scene
        // (Application::GetWorld; the scenes own their worlds, see SwarmScene.h).
        Emerald::Entity quad = GetWorld().Spawn();
        quad.Add<Emerald::Transform>(Emerald::Transform{.Position = {160.0f, 220.0f}});
        quad.Add<Emerald::Velocity>(Emerald::Velocity{.Linear = {120.0f, 80.0f}});
        RunThreadPoolDemo();
        BindInput();
        LoadAssets();
        m_Shared.LoadSettings(); // the options from the last run (Shared.h)

        // The scenes are made here, so they can reach each other through m_Shared.Make without
        // including each other.
        m_Shared.Make = [this](SceneId id) -> std::unique_ptr<Emerald::Scene> {
            switch (id) {
            case SceneId::Title:
                return std::make_unique<TitleScene>(m_Shared);
            case SceneId::Demo:
                return std::make_unique<DemoScene>(m_Shared);
            case SceneId::Tilemap:
                return std::make_unique<TilemapScene>(m_Shared);
            case SceneId::Swarm:
                return std::make_unique<SwarmScene>(m_Shared);
            case SceneId::Platformer:
                return std::make_unique<PlatformerScene>(m_Shared);
            case SceneId::Pause:
                return std::make_unique<PauseScene>(m_Shared);
            case SceneId::Options:
                return std::make_unique<OptionsScene>(m_Shared);
            case SceneId::Lighting:
                return std::make_unique<LightingScene>(m_Shared);
            }
            return nullptr;
        };
        // --demo / --tilemap / --swarm / --platformer (screenshots, the benchmark) start there at
        // once (--options over the title); otherwise the title fades in from black.
        if (m_Shared.Options.Start == SceneId::Options)
            GetScenes().Push(m_Shared.Make(SceneId::Title));
        if (m_Shared.Options.Start)
            GetScenes().Push(m_Shared.Make(*m_Shared.Options.Start));
        else
            GetScenes().Push(m_Shared.Make(SceneId::Title), Emerald::Transition::Fade(0.6f));

        // The ImGui easing preview: a dot running along the curve, over and over.
        m_Tweens.FromTo(&m_PreviewT, 0.0f, 1.0f, 1.5f,
                        {.Curve = Emerald::Easing::Linear, .Repeat = Emerald::Tweens::kForever});
    }

    // Global keys only; the scenes read the rest (they run after this, see SceneStack.h).
    void OnUpdate(f32 dt) override
    {
        m_Time += dt;
        if (GetInput().WasActionPressed("Quit"))
            Quit();
        if (GetInput().WasActionPressed("Crt"))
            SetCrtEnabled(!IsCrtEnabled());
        m_Tweens.Update(dt);

        // Move the app's entities and bounce them off the window edges.
        Emerald::UpdateMovement(GetWorld(), dt);
        const Vec2 limit = m_Shared.GetViewSize() - Vec2(kQuadSize);
        GetWorld().Each<Emerald::Transform, Emerald::Velocity>(
            [&](Emerald::Entity, const Emerald::Transform& t, Emerald::Velocity& v) {
                if ((t.Position.x < 0.0f && v.Linear.x < 0.0f) ||
                    (t.Position.x > limit.x && v.Linear.x > 0.0f))
                    v.Linear.x = -v.Linear.x;
                if ((t.Position.y < 0.0f && v.Linear.y < 0.0f) ||
                    (t.Position.y > limit.y && v.Linear.y > 0.0f))
                    v.Linear.y = -v.Linear.y;
            });
        GetWorld().Flush();
    }

    // Under every scene: the triangle and the app-level entity quads (raw SDL GPU draws, below
    // the 2D).
    void OnRender(SDL_GPURenderPass* pass) override
    {
        SDL_GPUCommandBuffer* cmd = GetRenderer().GetCommandBuffer();
        SDL_BindGPUGraphicsPipeline(pass, m_Pipeline);

        // Everything is drawn in pixel space: (0, 0) top-left, window size bottom-right, +Y down.
        // Window size is in the same units as mouse/window coordinates; the projection maps it to
        // the whole swapchain whatever its pixel size.
        const Vec2 size = m_Shared.GetViewSize();
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

        // One quad per app-level entity: the unit quad scaled to kQuadSize pixels and moved there.
        GetWorld().Each<Emerald::Transform>([&](Emerald::Entity, const Emerald::Transform& t) {
            draws.push_back({projection * Mat4::Translate(t.Position) * Mat4::Scale(kQuadSize),
                             m_QuadBuffer, static_cast<u32>(kQuad.size())});
        });

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

    // The global part of the "Emerald" window; each scene's OnImGui appends its own section.
    void OnImGui() override
    {
#if EMERALD_WITH_IMGUI
        ImGui::SetNextWindowPos(ImVec2(20.0f, 20.0f), ImGuiCond_FirstUseEver);
        // No keyboard navigation here: the arrows and Enter drive the game's menus, and would
        // otherwise also move ImGui's focus and press its buttons (e.g. "Push pause").
        ImGui::Begin("Emerald", nullptr, ImGuiWindowFlags_NoNavInputs);
        const std::string driver(GetRenderer().GetDriverName());
        ImGui::Text("GPU: %s", driver.c_str());
        ImGui::Text("Frame: %llu", static_cast<unsigned long long>(GetFrameCount()));
        ImGui::Text("Time:  %.2f s", static_cast<f64>(m_Time));
        ImGui::Text("Fixed update: %.0f Hz", static_cast<f64>(1.0f / GetFixedDeltaSeconds()));
        const Emerald::Vec2i pixels = GetWindowSizeInPixels();
        ImGui::Text("Window: %d x %d px", pixels.x, pixels.y);
        ImGui::Text("Renderer2D: %u lines, %u sprites, %u draw calls",
                    GetRenderer2D().GetLastFrameLineCount(),
                    GetRenderer2D().GetLastFrameSpriteCount(),
                    GetRenderer2D().GetLastFrameDrawCalls());
        ImGui::Text("Move the hero: WASD / arrows / left stick (Shift runs), jump: Space / %s",
                    GetInput().GetGamepads().GetButtonLabel(GamepadButton::South));
        ShowScenes();
        if (ImGui::CollapsingHeader("Engine")) {
            ShowAssets();
            ShowTweens();
            ShowGamepads();
            ImGui::Text("Audio: %s", GetAudio().IsAvailable() ? "on" : "no device");
            ImGui::Separator();
            ImGui::Text("Workers: %u", GetThreadPool().GetThreadCount());
            const Emerald::FrameArena::Stats arena = GetFrameArena().GetStats();
            ImGui::Text("Frame arena: %zu B last frame, peak %zu B of %zu KiB",
                        arena.LastFrameBytes, arena.PeakBytes, arena.Capacity / 1024);
            // Frames whose arena allocations did not fit and went to the heap (should stay 0).
            ImGui::Text("Frames that spilled to the heap: %llu",
                        static_cast<unsigned long long>(arena.OverflowFrames));
            const Emerald::TrackingResource::Stats pools = GetPoolStats();
            ImGui::Text("Pools: %zu B in use (peak %zu B), %zu chunks", pools.BytesInUse,
                        pools.PeakBytes, pools.AllocationsInUse);
        }
        ImGui::End();
#endif
    }

#if EMERALD_WITH_IMGUI
    // The scene stack, top first: flags (editable), whether each scene is drawn / updated / has
    // focus this frame, the transition, and buttons that queue requests like the scenes do.
    void ShowScenes()
    {
        Emerald::SceneStack& scenes = GetScenes();
        if (!ImGui::CollapsingHeader("Scene stack", ImGuiTreeNodeFlags_DefaultOpen))
            return;
        if (scenes.IsTransitioning())
            ImGui::Text("%zu scenes, transition: cover %.2f, %zu pending", scenes.GetCount(),
                        static_cast<f64>(scenes.GetCover()), scenes.GetPendingCount());
        else
            ImGui::Text("%zu scenes, no transition, %zu pending", scenes.GetCount(),
                        scenes.GetPendingCount());
        constexpr ImGuiTableFlags flags =
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit;
        if (ImGui::BeginTable("scenes", 6, flags)) {
            ImGui::TableSetupColumn("Scene");
            ImGui::TableSetupColumn("DrawBelow");
            ImGui::TableSetupColumn("UpdateBelow");
            ImGui::TableSetupColumn("Drawn");
            ImGui::TableSetupColumn("Updated");
            ImGui::TableSetupColumn("Focus");
            ImGui::TableHeadersRow();
            for (usize i = scenes.GetCount(); i-- > 0;) {
                Emerald::Scene& scene = scenes.GetScene(i);
                ImGui::PushID(static_cast<i32>(i));
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(scene.GetName().c_str());
                ImGui::TableNextColumn();
                ImGui::Checkbox("##draw", &scene.DrawBelow);
                ImGui::TableNextColumn();
                ImGui::Checkbox("##update", &scene.UpdateBelow);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(scenes.IsDrawn(i) ? "yes" : "-");
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(scenes.IsUpdated(i) ? "yes" : "-");
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(scene.HasFocus() ? "input" : "-");
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (ImGui::Button("Push pause"))
            scenes.Push(m_Shared.Make(SceneId::Pause));
        ImGui::SameLine();
        if (ImGui::Button("Pop"))
            scenes.Pop(Emerald::Transition::Fade(0.3f));
        ImGui::SameLine();
        if (ImGui::Button("Title (blinds)"))
            scenes.ReplaceAll(m_Shared.Make(SceneId::Title), Blinds(0.5f, {0.1f, 0.0f, 0.2f}));
        ImGui::SameLine();
        if (ImGui::Button("Demo (white fade)"))
            scenes.ReplaceAll(m_Shared.Make(SceneId::Demo),
                              Emerald::Transition::Fade(0.4f, {1.0f, 1.0f, 1.0f}));
        ImGui::Separator();
    }

    // Everything the asset manager has loaded, with reference counts and hot reloads.
    void ShowAssets()
    {
        const Emerald::Assets& assets = GetAssets();
        if (Emerald::Assets::kHotReload)
            ImGui::Text("Assets: %zu, hot reload on (edit a file under sandbox/assets)",
                        assets.GetCount());
        else
            ImGui::Text("Assets: %zu, hot reload off (release build)", assets.GetCount());
        constexpr ImGuiTableFlags flags =
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit;
        if (ImGui::BeginTable("assets", 5, flags)) {
            ImGui::TableSetupColumn("Type");
            ImGui::TableSetupColumn("File");
            ImGui::TableSetupColumn("Refs");
            ImGui::TableSetupColumn("Reloads");
            ImGui::TableSetupColumn("State");
            ImGui::TableHeadersRow();
            for (const Emerald::AssetInfo& info : assets.List()) {
                // The path relative to the asset folder is enough to tell them apart.
                const std::string file =
                    std::filesystem::path(info.Path).lexically_relative(assets.GetRoot()).string();
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(Emerald::GetAssetTypeName(info.Type));
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(file.c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%u", info.RefCount);
                ImGui::TableNextColumn();
                ImGui::Text("%u", info.Reloads);
                ImGui::TableNextColumn();
                if (info.Placeholder)
                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 1.0f, 1.0f), "placeholder");
                else
                    ImGui::TextUnformatted("loaded");
            }
            ImGui::EndTable();
        }
        ImGui::Separator();
    }

    // An easing curve preview with a dot running along it.
    void ShowTweens()
    {
        if (ImGui::BeginCombo("Easing", Emerald::GetEasingName(m_PreviewEasing))) {
            for (const Emerald::Easing easing : Emerald::kAllEasings)
                if (ImGui::Selectable(Emerald::GetEasingName(easing), easing == m_PreviewEasing))
                    m_PreviewEasing = easing;
            ImGui::EndCombo();
        }
        // The curve from t = 0 to 1, with room above and below for Back / Elastic overshoot.
        std::array<f32, 64> curve{};
        for (usize i = 0; i < curve.size(); ++i)
            curve[i] = Emerald::Ease(m_PreviewEasing,
                                     static_cast<f32>(i) / static_cast<f32>(curve.size() - 1));
        constexpr f32 kLow = -0.4f;
        constexpr f32 kHigh = 1.4f;
        ImGui::PlotLines("##curve", curve.data(), static_cast<i32>(curve.size()), 0, nullptr, kLow,
                         kHigh, ImVec2(260.0f, 110.0f));
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        const f32 value = Emerald::Ease(m_PreviewEasing, m_PreviewT);
        const ImVec2 dot(min.x + (max.x - min.x) * m_PreviewT,
                         max.y - (max.y - min.y) * (value - kLow) / (kHigh - kLow));
        ImGui::GetWindowDrawList()->AddCircleFilled(dot, 4.0f, IM_COL32(255, 215, 80, 255));
        ImGui::SameLine();
        ImGui::Text("t %.2f\nvalue %.2f", static_cast<f64>(m_PreviewT), static_cast<f64>(value));
        ImGui::Separator();
    }

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
    // Controls are actions bound to keys and gamepad inputs; the scenes only use the names.
    void BindInput()
    {
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
        input.BindAction("Run", {Key::LeftShift});
        input.BindAction("Run", {GamepadButton::East});
        input.BindAction("Quit", {Key::Escape});
        input.BindAction("Crt", {Key::C}); // CRT post-process on/off
        // Menus (title, pause): M / Start pauses and resumes, Up / Down and Enter / South pick.
        input.BindAction("Menu", {Key::M});
        input.BindAction("Menu", {GamepadButton::Start});
        input.BindAction("MenuUp", {Key::Up});
        input.BindAction("MenuUp", {Key::W});
        input.BindAction("MenuUp", {GamepadButton::DPadUp});
        input.BindAction("MenuDown", {Key::Down});
        input.BindAction("MenuDown", {Key::S});
        input.BindAction("MenuDown", {GamepadButton::DPadDown});
        input.BindAction("MenuSelect", {Key::Enter});
        input.BindAction("MenuSelect", {GamepadButton::South});
        // Camera: Tab / North toggles following the hero or free panning (with the move keys),
        // zoom with the wheel, Q / E or the triggers, rotate with F / G or the shoulders.
        input.BindAction("CameraMode", {Key::Tab});
        input.BindAction("CameraMode", {GamepadButton::North});
        input.BindAction("Shake", {Key::X});
        input.BindAction("Shake", {GamepadButton::West});
        input.BindAction("CameraReset", {Key::R});
        input.BindAction("CameraReset", {GamepadButton::Back});
        input.BindAxis("Zoom", Key::Q, Key::E);
        input.BindAxis("Zoom", GamepadButton::LeftTrigger, GamepadButton::RightTrigger);
        input.BindAxis("Rotate", Key::F, Key::G);
        input.BindAxis("Rotate", GamepadButton::LeftShoulder, GamepadButton::RightShoulder);
        input.BindAction("Scene", {Key::T}); // demo -> tilemap -> swarm -> platformer -> demo
        input.BindAction("SwarmCut", {Key::Backspace}); // the swarm: destroy half
        input.BindAction("Jump", {Key::Space}); // the platformer (down + jump drops through)
        input.BindAction("Jump", {Key::Z});
        input.BindAction("Jump", {GamepadButton::South});
        input.BindAction("Overlay", {Key::O}); // the platformer's collision overlay
        // The UI widgets (OptionsScene.h): the default UI actions, but Escape quits the sandbox,
        // so back is Backspace or M (rebinding replaces only the keys; East stays).
        Emerald::BindDefaultUiActions(input);
        input.RebindAction(Emerald::kUiBack, {Key::Backspace, Key::M});
    }

    // Files come from the asset manager: loaded once, placeholders for missing files, and (debug
    // builds) hot reload from the source folder. Loaded here once and shared by the scenes.
    void LoadAssets()
    {
        Emerald::Assets& assets = GetAssets();
        if constexpr (Emerald::Assets::kHotReload)
            assets.SetRoot(SANDBOX_SOURCE_ASSETS);
        else
            assets.SetRoot(Emerald::Paths::GetBasePath() / "assets");

        // A short generated "blip" for the jump, rising in pitch (files would use
        // Emerald::LoadSound("x.mp3")).
        using namespace Emerald::Synth;
        std::vector<f32> blip = Generate({.Shape = Wave::Sine,
                                          .Seconds = 0.15f,
                                          .StartHz = 520.0f,
                                          .EndHz = 880.0f,
                                          .Volume = 0.4f});
        ApplyDecay(blip, 0.03f);
        m_Shared.Blip = ToSound(blip);

        // Sprites: upload the generated sheet (nearest filtering: crisp pixel art) and name its
        // two regions, like an atlas.json would.
        SDL_GPUDevice* device = GetRenderer().GetDevice();
        if (std::optional<Emerald::Texture> sheet =
                Emerald::Texture::Create(device, MakeSpriteSheet())) {
            m_Shared.Atlas = Emerald::TextureAtlas::Create(
                std::move(*sheet), {{"gem", {{0.0f, 0.0f}, {16.0f, 16.0f}}},
                                    {"ring", {{16.0f, 0.0f}, {16.0f, 16.0f}}}});
        }
        LoadFonts();
        // The hero sheet (tools/sprites/make_hero.py): sprites and animations from hero.json,
        // the image from hero.png next to it. Edit either while the sandbox runs (debug build)
        // and the hero updates. Also a file that does not exist, to show the placeholder.
        m_Shared.Hero = assets.Load<Emerald::TextureAtlas>("sprites/hero.json");
        m_Shared.Missing = assets.Load<Emerald::Texture>("sprites/missing.png");

        // A 1 x 1 white texture: tinted and stretched, it fills the menus' rectangles.
        Emerald::Image white;
        white.Width = 1;
        white.Height = 1;
        white.Pixels = {255, 255, 255, 255};
        m_Shared.White = Emerald::Texture::Create(device, white);
    }

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

    // The same TTF at three sizes and two filters: three assets (the options are part of what
    // makes an asset), each its own atlas texture.
    void LoadFonts()
    {
        Emerald::Assets& assets = GetAssets();
        const char* path = "fonts/PressStart2P-Regular.ttf";
        const auto pixel = [&](f32 px) {
            return assets.Load<Emerald::Font>(
                path, {.Size = px,
                       .Ranges = {Emerald::kAsciiGlyphs, Emerald::kLatin1Glyphs},
                       .Oversample = 1,
                       .Filter = Emerald::TextureFilter::Nearest});
        };
        m_Shared.PixelFont = pixel(16.0f);
        m_Shared.SmallFont = pixel(8.0f);
        m_Shared.SmoothFont = assets.Load<Emerald::Font>(path, {.Size = 40.0f, .Oversample = 2});
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
        m_QuadBuffer = renderer.CreateBuffer(SDL_GPU_BUFFERUSAGE_VERTEX, kQuad.data(),
                                             static_cast<u32>(sizeof(kQuad)));
        if (!m_QuadBuffer)
            return false;
        return m_Pipeline && m_TriangleBuffer;
    }

    SandboxShared m_Shared; // options, assets and the scene factory, for the scenes
    f32 m_Time = 0.0f;
    Emerald::Tweens m_Tweens; // the sandbox's own (the easing preview)
    f32 m_PreviewT = 0.0f;    // 0 to 1, over and over
    Emerald::Easing m_PreviewEasing = Emerald::Easing::BounceOut;
    SDL_GPUGraphicsPipeline* m_Pipeline = nullptr;
    SDL_GPUBuffer* m_TriangleBuffer = nullptr;
    SDL_GPUBuffer* m_QuadBuffer = nullptr;
};

SandboxOptions ParseOptions(i32 argc, char** argv)
{
    SandboxOptions options;
    for (i32 i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        const std::string_view value(i + 1 < argc ? argv[i + 1] : "");
        if (arg == "--map") {
            options.Room.Map = std::filesystem::path(value);
            options.Start = SceneId::Tilemap;
            ++i;
        } else if (arg == "--zoom") {
            std::from_chars(value.data(), value.data() + value.size(), options.Room.Zoom);
            ++i;
        } else if (arg == "--tilemap") {
            options.Start = SceneId::Tilemap;
        } else if (arg == "--demo") {
            options.Start = SceneId::Demo;
        } else if (arg == "--swarm") {
            options.Start = SceneId::Swarm;
        } else if (arg == "--platformer") {
            options.Start = SceneId::Platformer;
        } else if (arg == "--options") { // the options screen over the title
            options.Start = SceneId::Options;
        } else if (arg == "--lighting") {
            options.Start = SceneId::Lighting;
        } else if (arg == "--pan") {
            options.Room.Pan = true;
        } else if (arg == "--stats") {
            options.Room.Stats = true;
        } else if (arg == "--collision") {
            options.Collision = true;
        } else if (arg == "--objects") {
            options.Objects = true;
        } else if (arg == "--no-cull") {
            options.NoCull = true;
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
    spec.ShaderFormats = EMERALD_SHADER_FORMATS; // formats generated by emerald_add_shaders()
    // Engine options: --gpu vulkan, --frames N, --screenshot, --capture, --record, --replay.
    spec.Args = {argv, static_cast<usize>(argc)};

    Sandbox app(spec, std::move(options));
    return app.Run();
}
