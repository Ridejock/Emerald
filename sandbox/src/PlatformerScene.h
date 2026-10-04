#pragma once

// The platformer scene (Emerald/Physics/Platformer.h): the hero runs and jumps through
// sandbox/assets/tilemaps/platformer.tmj (made by tools/tilemaps/make_platformer.py): flat
// ground, 45 and 22.5 degree slopes, one-way platforms, a gap for coyote time and a step for the
// jump buffer. The physics runs in OnFixedUpdate; a HUD shows the body's state and timers, a
// trail shows the path (green: grounded, yellow: in coyote time, white: in the air).
//
// A / D, arrows, d-pad or left stick run; Space / Z / gamepad South (Cross) jumps (hold for
// higher); down + jump drops through a one-way platform; R / Back respawns; O shows the
// collision; M / Start pauses; T fades to the camera demo.

#include <array>
#include <cmath>
#include <string>

#include <Emerald/Emerald.h>

#include "Shared.h"

#if EMERALD_WITH_IMGUI
#include <imgui.h>
#endif

class PlatformerScene final : public Emerald::Scene {
public:
    using Vec2 = Emerald::Vec2;
    using Vec4 = Emerald::Vec4;

    explicit PlatformerScene(SandboxShared& shared) : Scene("Platformer"), m_Shared(shared)
    {
        m_Map = shared.App.GetAssets().Load<Emerald::Tilemap>("tilemaps/platformer.tmj");
        m_Camera.SetZoom(3.0f);
        m_Camera.GetFollowParams() = {.DeadZone = {20.0f, 12.0f}, .Damping = 6.0f};
        m_ShowCollision = shared.Options.Collision;
        if (const Emerald::MapObject* spawn = m_Map->FindObject("spawn"))
            m_Checkpoint = spawn->Position;
        Respawn();
        if (m_Shared.Hero)
            m_Animator.Play(m_Shared.Hero->GetAnimation("idle"));
    }

    // Physics at the fixed rate: the same input always gives the same path.
    void OnFixedUpdate(f32 dt) override
    {
        const Emerald::Input& input = m_Shared.App.GetInput();
        const Emerald::PlatformerInput in{.Move = input.GetAxis("MoveX"),
                                          .JumpPressed = input.WasActionPressed("Jump"),
                                          .JumpHeld = input.IsActionDown("Jump"),
                                          .Down = input.GetAxis("MoveY") > 0.5f};
        const bool wasGrounded = m_Body.Grounded;
        Emerald::StepPlatformer(m_Body, in, m_Tunables, *m_Map, dt);

        // What kind of jump it was, for the HUD: off the ground, in coyote time (already in the
        // air), or from the buffer (pressed before landing).
        if (m_Body.Jumped) {
            ++m_Jumps;
            m_Shared.App.GetAudio().Play(m_Shared.Blip, {.Volume = 0.4f});
            if (!wasGrounded)
                Popup("coyote jump!", m_CoyoteJumps);
            else if (!in.JumpPressed)
                Popup("buffered jump!", m_BufferedJumps);
        }
        if (m_Body.DroppedThrough)
            Popup("drop through!", m_Drops);
        m_PopupTime = Emerald::Max(m_PopupTime - dt, 0.0f);

        // Checkpoints: the furthest one reached on the ground; falling out of the map goes back.
        if (m_Body.Grounded)
            for (const Emerald::MapLayer& layer : m_Map->GetLayers())
                for (const Emerald::MapObject& o : layer.Objects)
                    if (o.Name == "checkpoint" && m_Body.Position.x >= o.Position.x &&
                        o.Position.x > m_Checkpoint.x)
                        m_Checkpoint = o.Position;
        if (m_Body.Position.y > m_Map->GetBounds().Max.y + 32.0f)
            Respawn();

        if (m_Body.Velocity.x != 0.0f)
            m_FacingLeft = m_Body.Velocity.x < 0.0f;
        UpdateAnimation(dt);
        AddTrail();
        m_Camera.SetViewSize(m_Shared.GetViewSize());
        m_Camera.SetBounds(m_Map->GetBounds());
        m_Camera.Follow(m_Body.GetFeet(), dt);
        m_Camera.Update(dt);
    }

    void OnUpdate(f32 /*dt*/) override
    {
        const Emerald::Input& input = m_Shared.App.GetInput();
        if (input.WasActionPressed("Menu"))
            GetStack()->Push(m_Shared.Make(SceneId::Pause));
        else if (input.WasActionPressed("Scene"))
            GetStack()->Replace(m_Shared.Make(SceneId::Demo), FadeBlack());
        if (input.WasActionPressed("CameraReset"))
            Respawn();
        if (input.WasActionPressed("Overlay"))
            m_ShowCollision = !m_ShowCollision;
    }

    void OnRender2D(Emerald::Renderer2D& r) override
    {
        const Vec2 size = m_Shared.GetViewSize();
        m_Camera.SetViewSize(size);
        m_Camera.SetTargetSize(m_Shared.GetTargetSize());
        DrawWorld(r);
        r.Begin(Emerald::Mat4::OrthoPixelSpace(size.x, size.y));
        DrawLabels(r, size);
        DrawHud(r, size);
        r.End();
    }

    void OnImGui() override
    {
#if EMERALD_WITH_IMGUI
        ImGui::Begin("Emerald"); // appends to the app's window
        if (ImGui::CollapsingHeader("Platformer", ImGuiTreeNodeFlags_DefaultOpen)) {
            Emerald::PlatformerTunables& t = m_Tunables;
            ImGui::SliderFloat("Gravity", &t.Gravity, 200.0f, 4000.0f);
            ImGui::SliderFloat("Max fall speed", &t.MaxFallSpeed, 50.0f, 1000.0f);
            ImGui::SliderFloat("Run speed", &t.RunSpeed, 20.0f, 400.0f);
            ImGui::SliderFloat("Ground accel", &t.GroundAccel, 100.0f, 8000.0f);
            ImGui::SliderFloat("Ground decel", &t.GroundDecel, 100.0f, 8000.0f);
            ImGui::SliderFloat("Air accel", &t.AirAccel, 0.0f, 8000.0f);
            ImGui::SliderFloat("Jump velocity", &t.JumpVelocity, 50.0f, 900.0f);
            ImGui::SliderFloat("Jump cut", &t.JumpCut, 0.0f, 1.0f);
            ImGui::SliderFloat("Coyote time", &t.CoyoteTime, 0.0f, 0.5f, "%.3f s");
            ImGui::SliderFloat("Jump buffer", &t.JumpBuffer, 0.0f, 0.5f, "%.3f s");
            ImGui::SliderFloat("Snap down", &t.SnapDown, 0.0f, 16.0f, "%.1f px");
            ImGui::SliderFloat("Drop-through time", &t.DropThroughTime, 0.0f, 0.5f, "%.3f s");
            if (ImGui::Button("Default tunables"))
                t = {};
            ImGui::SameLine();
            if (ImGui::Button("Respawn"))
                Respawn();
            ImGui::Checkbox("Collision overlay", &m_ShowCollision);
            ImGui::Text("Position %.2f, %.2f  velocity %.1f, %.1f",
                        static_cast<f64>(m_Body.Position.x), static_cast<f64>(m_Body.Position.y),
                        static_cast<f64>(m_Body.Velocity.x), static_cast<f64>(m_Body.Velocity.y));
        }
        ImGui::End();
#endif
    }

private:
    static constexpr usize kTrail = 360; // three seconds of fixed steps
    enum class TrailState : u8 { Ground, Coyote, Air };
    struct TrailPoint { // no Vec4 member: its 16-byte alignment would pad this (MSVC C4324)
        Vec2 Feet{};
        TrailState State = TrailState::Air;
    };

    void Respawn()
    {
        m_Body = {.Position = m_Checkpoint - Vec2(0.0f, 7.0f), .HalfSize = {5.0f, 7.0f}};
        m_Camera.SetPosition(m_Checkpoint);
        m_TrailCount = 0;
    }

    void Popup(const char* text, u32& counter)
    {
        EM_INFO("Platformer: {} at x {:.0f}", text, static_cast<f64>(m_Body.Position.x));
        m_Popup = text;
        m_PopupTime = 0.8f;
        ++counter;
    }

    // Idle or walk (faster when running faster) on the ground, the jump's middle frame in the air.
    void UpdateAnimation(f32 dt)
    {
        if (!m_Shared.Hero)
            return;
        const f32 speed = std::abs(m_Body.Velocity.x) / m_Tunables.RunSpeed;
        m_Animator.Play(m_Shared.Hero->GetAnimation(speed > 0.05f ? "walk" : "idle"));
        m_Animator.SetSpeed(Emerald::Max(speed, 0.3f));
        m_Animator.Update(dt);
    }

    void AddTrail()
    {
        const TrailState state = m_Body.Grounded             ? TrailState::Ground
                                 : m_Body.CoyoteTimer > 0.0f ? TrailState::Coyote
                                                             : TrailState::Air;
        m_Trail[m_TrailNext] = {.Feet = m_Body.GetFeet(), .State = state};
        m_TrailNext = (m_TrailNext + 1) % kTrail;
        m_TrailCount = Emerald::Min(m_TrailCount + 1, kTrail);
    }

    // The map's layers with the hero drawn where the "Objects" layer is, then the trail and
    // the collision overlay.
    void DrawWorld(Emerald::Renderer2D& r)
    {
        const Emerald::Tilemap& map = *m_Map;
        const Emerald::Rect2D view = m_Camera.GetVisibleBounds();
        r.Begin(m_Camera);
        // A sky (it also covers the app's triangle and quad drawn under every scene).
        r.FillRect(view.Min, view.Max - view.Min, {0.36f, 0.62f, 0.86f, 1.0f});
        for (usize i = 0; i < map.GetLayers().size(); ++i) {
            if (map.GetLayers()[i].Kind == Emerald::LayerKind::Objects)
                DrawHero(r);
            else if (map.GetLayers()[i].Visible)
                map.DrawLayer(r, i, view);
        }
        for (usize i = 0; i < m_TrailCount; ++i) {
            const TrailPoint& p = m_Trail[(m_TrailNext + kTrail - 1 - i) % kTrail];
            const Vec4 color = p.State == TrailState::Ground   ? Vec4(0.3f, 1.0f, 0.4f, 0.8f)
                               : p.State == TrailState::Coyote ? Vec4(1.0f, 0.9f, 0.2f, 1.0f)
                                                               : Vec4(1.0f, 1.0f, 1.0f, 0.6f);
            r.FillRect(p.Feet - Vec2(0.5f), Vec2(1.0f), color);
        }
        if (m_ShowCollision) {
            map.DrawCollision(r, view);
            const Emerald::Aabb box = m_Body.GetBox();
            r.DrawRect(box.Min, box.Max - box.Min, {0.3f, 1.0f, 0.4f, 1.0f});
        }
        r.End();
    }

    void DrawHero(Emerald::Renderer2D& r) const
    {
        if (!m_Shared.Hero)
            return;
        const Emerald::SpriteOptions options{
            .Origin = {0.5f, 1.0f}, .FlipX = m_FacingLeft, .PixelSnap = true};
        if (m_Body.Grounded)
            r.DrawSprite(m_Animator, m_Body.GetFeet(), options);
        else
            r.DrawSprite(m_Shared.Hero->Get("hero_jump_1"), m_Body.GetFeet(), options);
    }

    // The map's "label" objects as crisp screen-space text above each part of the level.
    void DrawLabels(Emerald::Renderer2D& r, Vec2 size) const
    {
        if (!m_Shared.SmallFont)
            return;
        const Vec2 scale = size / m_Shared.GetTargetSize(); // target pixels -> window units
        for (const Emerald::MapLayer& layer : m_Map->GetLayers())
            for (const Emerald::MapObject& o : layer.Objects)
                if (o.Type == "label")
                    r.DrawString(*m_Shared.SmallFont, o.Props.GetString("text"),
                                 m_Camera.WorldToScreen(o.Position) * scale,
                                 {1.0f, 1.0f, 0.85f, 0.9f}, 1.0f, Emerald::TextAlign::Center);
        if (m_PopupTime > 0.0f) {
            const Vec2 at = m_Camera.WorldToScreen(m_Body.Position - Vec2(0.0f, 20.0f)) * scale;
            r.DrawString(*m_Shared.SmallFont, m_Popup, at, {1.0f, 0.85f, 0.3f, m_PopupTime}, 1.0f,
                         Emerald::TextAlign::Center);
        }
    }

    // A small panel: state flags, the three timers as bars, counters, and a help line.
    void DrawHud(Emerald::Renderer2D& r, Vec2 size) const
    {
        if (!m_Shared.SmallFont)
            return;
        const Emerald::Font& font = *m_Shared.SmallFont;
        const Vec4 text{1.0f, 1.0f, 1.0f, 0.95f};
        const Vec4 on{0.4f, 1.0f, 0.5f, 1.0f};
        const Vec4 off{0.6f, 0.6f, 0.65f, 0.8f};
        r.FillRect({8.0f, 8.0f}, {300.0f, 112.0f}, {0.0f, 0.0f, 0.0f, 0.6f});
        f32 y = 16.0f;
        r.DrawString(font, "PLATFORMER (fixed step)", {16.0f, y}, text);
        y += 14.0f;
        const auto flag = [&](const char* name, bool value, f32 x) {
            r.DrawString(font, name, {x, y}, value ? on : off);
        };
        flag("grounded", m_Body.Grounded, 16.0f);
        flag("slope", m_Body.OnSlope, 110.0f);
        flag("one-way", m_Body.OnOneWay, 172.0f);
        flag("rising", m_Body.Rising, 246.0f);
        y += 14.0f;
        const auto bar = [&](const char* name, f32 value, f32 full, Vec4 color) {
            r.DrawString(font, name, {16.0f, y}, text);
            const f32 width =
                150.0f * (full > 0.0f ? Emerald::Clamp(value / full, 0.0f, 1.0f) : 0.0f);
            r.DrawRect({90.0f, y}, {150.0f, 8.0f}, {1.0f, 1.0f, 1.0f, 0.4f});
            r.FillRect({90.0f, y}, {width, 8.0f}, color);
            r.DrawString(font, std::to_string(static_cast<i32>(value * 1000.0f)) + " ms",
                         {248.0f, y}, text);
            y += 14.0f;
        };
        bar("coyote", m_Body.CoyoteTimer, m_Tunables.CoyoteTime, {1.0f, 0.9f, 0.2f, 1.0f});
        bar("buffer", m_Body.BufferTimer, m_Tunables.JumpBuffer, {0.3f, 0.8f, 1.0f, 1.0f});
        bar("drop", m_Body.DropTimer, m_Tunables.DropThroughTime, {1.0f, 0.5f, 0.3f, 1.0f});
        r.DrawString(font,
                     "jumps " + std::to_string(m_Jumps) + "  coyote " +
                         std::to_string(m_CoyoteJumps) + "  buffered " +
                         std::to_string(m_BufferedJumps) + "  drops " + std::to_string(m_Drops),
                     {16.0f, y}, text);
        y += 14.0f;
        r.DrawString(font,
                     "speed " + std::to_string(static_cast<i32>(m_Body.Velocity.x)) + ", " +
                         std::to_string(static_cast<i32>(m_Body.Velocity.y)) + " px/s",
                     {16.0f, y}, text);
        r.DrawString(font,
                     "Platformer: A/D run, Space/Cross jump (hold: higher), down+jump drops "
                     "through, R respawn, O collision, M pause, T camera demo",
                     {12.0f, size.y - 20.0f}, {1.0f, 1.0f, 1.0f, 0.9f});
    }

    SandboxShared& m_Shared;
    Emerald::AssetHandle<Emerald::Tilemap> m_Map;
    Emerald::Camera2D m_Camera;
    Emerald::PlatformerBody m_Body;
    Emerald::PlatformerTunables m_Tunables;
    Vec2 m_Checkpoint{64.0f, 64.0f}; // feet position to respawn at
    Emerald::Animator m_Animator;
    bool m_FacingLeft = false;
    bool m_ShowCollision = false;
    std::array<TrailPoint, kTrail> m_Trail{};
    usize m_TrailNext = 0;
    usize m_TrailCount = 0;
    std::string m_Popup;
    f32 m_PopupTime = 0.0f;
    u32 m_Jumps = 0;
    u32 m_CoyoteJumps = 0;
    u32 m_BufferedJumps = 0;
    u32 m_Drops = 0;
};
