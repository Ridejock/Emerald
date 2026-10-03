#pragma once

// The camera demo scene: shapes, sprites, text and the collision yard in a world larger than the
// window. Move the hero with WASD / arrows / left stick / d-pad (at 120 Hz, Shift / East runs),
// Space / South jumps with a ring pulse, a blip and a rumble. The camera follows the hero (Tab /
// North: free panning), zooms with the wheel / Q / E / triggers, rotates with F / G / shoulders,
// shakes on X / West, resets on R / Back. M / Start pauses, T fades to the tilemap room.

#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>

#include <Emerald/Emerald.h>

#include "CollisionDemo.h"
#include "Shared.h"

#if EMERALD_WITH_IMGUI
#include <imgui.h>
#endif

class DemoScene final : public Emerald::Scene {
public:
    using Vec2 = Emerald::Vec2;
    using Vec4 = Emerald::Vec4;

    explicit DemoScene(SandboxShared& shared) : Scene("Camera demo"), m_Shared(shared)
    {
        m_Camera.SetBounds({.Min = {0.0f, 0.0f}, .Max = kWorldSize});
        m_Camera.GetFollowParams() = {.DeadZone = {160.0f, 100.0f}, .Damping = 5.0f};
        m_Camera.SetViewSize(shared.GetViewSize());
        m_Camera.SetPosition(kRoomOrigin + kRoomSize * 0.5f); // the room fills the window
        if (shared.Hero)
            m_Coin.Play(shared.Hero->GetAnimation("coin"));
    }

    void OnFixedUpdate(f32 dt) override
    {
        Emerald::Input& input = m_Shared.App.GetInput(); // blocked unless this scene has focus
        Vec2 direction(input.GetAxis("MoveX"), input.GetAxis("MoveY"));
        const f32 length = Emerald::Length(direction);
        // Keys give length 1 or 1.41 (diagonal), a stick anything up to 1: cap it at 1 so
        // diagonals are not faster and a half-tilted stick moves at half speed.
        if (length > 1.0f)
            direction = direction / length;
        if (input.WasActionPressed("CameraMode"))
            m_FreeCamera = !m_FreeCamera;
        m_Camera.SetViewSize(m_Shared.GetViewSize());
        if (m_FreeCamera) {
            // Pan at the same on-screen speed at any zoom.
            m_Camera.SetPosition(m_Camera.GetPosition() +
                                 direction * (600.0f * dt / m_Camera.GetZoom()));
            m_Hero.Update(false, 1.0f, dt);
        } else {
            const f32 speed = input.IsActionDown("Run") ? 1.8f : 1.0f;
            m_HeroPosition += direction * (220.0f * speed * dt);
            if (std::abs(direction.x) > 0.1f)
                m_FacingLeft = direction.x < 0.0f;
            m_Hero.Update(length > 0.1f, speed, dt);
            // Keep it in the world.
            m_HeroPosition = Emerald::Min(Emerald::Max(m_HeroPosition, Vec2(0.0f)), kWorldSize);
            m_Camera.Follow(m_HeroPosition, dt);
        }
        m_Camera.SetZoom(m_Camera.GetZoom() * std::exp(1.5f * input.GetAxis("Zoom") * dt));
        m_Camera.SetRotation(m_Camera.GetRotation() + 1.2f * input.GetAxis("Rotate") * dt);
        if (input.WasActionPressed("CameraReset")) {
            m_Camera.SetZoom(1.0f);
            m_Camera.SetRotation(0.0f);
        }
        if (input.WasActionPressed("Shake"))
            m_Camera.AddTrauma(0.6f);
        if (input.WasActionPressed("Pulse"))
            Jump(input);
        m_PulseAge += dt;
        m_Collision.Update(dt, m_HeroPosition);
        m_Camera.Update(dt);
        m_Coin.Update(dt);
    }

    void OnUpdate(f32 dt) override
    {
        m_Time += dt;
        const Emerald::Input& input = m_Shared.App.GetInput();
        // Pausing is instant (the pause menu animates itself in); the room fades.
        if (input.WasActionPressed("Menu"))
            GetStack()->Push(m_Shared.Make(SceneId::Pause));
        else if (input.WasActionPressed("Scene"))
            GetStack()->Replace(m_Shared.Make(SceneId::Tilemap), FadeBlack());
    }

    // Mouse wheel zoom (unless the mouse is over an ImGui window). Only the focused scene gets
    // events, so the wheel does nothing while paused.
    void OnEvent(const SDL_Event& event) override
    {
        if (event.type != SDL_EVENT_MOUSE_WHEEL)
            return;
#if EMERALD_WITH_IMGUI
        if (ImGui::GetIO().WantCaptureMouse)
            return;
#endif
        m_Camera.SetZoom(m_Camera.GetZoom() * std::pow(1.15f, event.wheel.y));
    }

    // The world goes through the camera; text and stats are a screen-space batch on top.
    void OnRender2D(Emerald::Renderer2D& r) override
    {
        const Vec2 size = m_Shared.GetViewSize();
        m_Camera.SetViewSize(size); // 1 world unit = 1 window unit at zoom 1
        m_Camera.SetTargetSize(m_Shared.GetTargetSize());
        r.Begin(m_Camera);
        DrawWorld(r);
        r.End();
        r.Begin(Emerald::Mat4::OrthoPixelSpace(size.x, size.y));
        DrawTextDemo(r, size);
        r.End();
    }

    void OnImGui() override
    {
#if EMERALD_WITH_IMGUI
        ImGui::Begin("Emerald"); // appends to the app's window
        if (ImGui::CollapsingHeader("Camera demo", ImGuiTreeNodeFlags_DefaultOpen)) {
            ShowAnimation();
            ShowCamera();
            ShowCollision();
        }
        ImGui::End();
#endif
    }

private:
    // The world is larger than the window; the original demo layout is a 1280 x 720 "room" in
    // its middle.
    static constexpr Vec2 kWorldSize{2560.0f, 1600.0f};
    static constexpr Vec2 kRoomSize{1280.0f, 720.0f};
    static constexpr Vec2 kRoomOrigin = (kWorldSize - kRoomSize) * 0.5f; // the room's top-left

    // A five-pointed star (alternating outer/inner radius).
    static std::array<Vec2, 10> MakeStar()
    {
        std::array<Vec2, 10> points{};
        for (usize i = 0; i < points.size(); ++i) {
            const f32 angle = Emerald::TwoPi * static_cast<f32>(i) / 10.0f - Emerald::HalfPi;
            const f32 radius = (i % 2 == 0) ? 1.0f : 0.45f;
            points[i] = Vec2(std::cos(angle), std::sin(angle)) * radius;
        }
        return points;
    }

    void Jump(Emerald::Input& input)
    {
        m_PulseAge = 0.0f;
        m_Hero.Jump();
        input.Rumble(0.3f, 0.6f, 120);
        m_Camera.AddTrauma(0.25f);
        // Panned towards the side of the screen the hero is on.
        const f32 x = m_Camera.WorldToScreen(m_HeroPosition).x / m_Camera.GetTargetSize().x;
        const f32 pan = Emerald::Clamp(x * 2.0f - 1.0f, -1.0f, 1.0f);
        m_Shared.App.GetAudio().Play(m_Shared.Blip, {.Volume = 0.7f, .Pan = pan * 0.8f});
    }

    void DrawWorld(Emerald::Renderer2D& r)
    {
        const Vec4 green{0.18f, 0.8f, 0.44f, 1.0f};
        const Vec4 dim{1.0f, 1.0f, 1.0f, 0.25f}; // the pipeline alpha-blends
        const Vec2 room = kRoomOrigin;
        const Vec2 size = kRoomSize;

        // Frame around the world (its bounds), a faint grid and the room's outline.
        r.DrawRect({2.0f, 2.0f}, kWorldSize - Vec2(4.0f), green);
        for (f32 x = 80.0f; x < kWorldSize.x; x += 80.0f)
            r.DrawLine({x, 0.0f}, {x, kWorldSize.y}, Vec4(1.0f, 1.0f, 1.0f, 0.06f));
        for (f32 y = 80.0f; y < kWorldSize.y; y += 80.0f)
            r.DrawLine({0.0f, y}, {kWorldSize.x, y}, Vec4(1.0f, 1.0f, 1.0f, 0.06f));
        r.DrawRect(room, size, Vec4(0.18f, 0.8f, 0.44f, 0.35f));

        // Spinning stars in the room's bottom corners, one of them pulsing in size, and smaller
        // ones in a ring around the world.
        const f32 pulse = 1.0f + 0.2f * std::sin(3.0f * m_Time);
        r.DrawPolygon(m_Star, {1.0f, 0.85f, 0.2f, 1.0f},
                      {.Position = room + Vec2(90.0f, size.y - 90.0f),
                       .Rotation = m_Time,
                       .Scale = Vec2(60.0f)});
        r.DrawPolygon(m_Star, {0.3f, 0.7f, 1.0f, 1.0f},
                      {.Position = room + size - Vec2(90.0f),
                       .Rotation = -0.7f * m_Time,
                       .Scale = Vec2(60.0f * pulse)});
        for (u32 i = 0; i < 24; ++i) {
            const f32 a = Emerald::TwoPi * static_cast<f32>(i) / 24.0f;
            const Vec2 at = kWorldSize * 0.5f + Vec2(std::cos(a) * 1100.0f, std::sin(a) * 680.0f);
            r.DrawPolygon(m_Star, Vec4(0.9f, 0.5f + 0.02f * static_cast<f32>(i), 0.3f, 0.8f),
                          {.Position = at, .Rotation = a + 0.3f * m_Time, .Scale = Vec2(24.0f)});
        }

        // Concentric circles around the center with increasing segment counts.
        for (u32 i = 0; i < 4; ++i)
            r.DrawCircle(room + size * 0.5f, 60.0f + 40.0f * static_cast<f32>(i), dim, 8u << i);

        DrawSprites(r, room, size);
        m_Collision.Draw(r, m_HeroPosition, GetMouseWorld(), m_FacingLeft, m_ShowHashGrid);

        DrawHero(r);
        // The Space pulse (a ring growing for half a second).
        if (m_PulseAge < 0.5f)
            r.DrawCircle(m_HeroPosition, 20.0f + 200.0f * m_PulseAge,
                         {1.0f, 1.0f, 1.0f, 1.0f - 2.0f * m_PulseAge});

        // A cross under the mouse, placed via ScreenToWorld (it should sit right on the cursor).
        if (const std::optional<Vec2> mouse = GetMouseWorld()) {
            const f32 arm = 8.0f / m_Camera.GetZoom();
            r.DrawLine(*mouse - Vec2(arm, 0.0f), *mouse + Vec2(arm, 0.0f), green);
            r.DrawLine(*mouse - Vec2(0.0f, arm), *mouse + Vec2(0.0f, arm), green);
        }
    }

    // The animated hero (flipped to face where it walks, gold flash when a jump lands) and a row
    // of ping-pong coins with tints and a vertical flip.
    void DrawHero(Emerald::Renderer2D& r)
    {
        if (!m_Hero.IsLoaded())
            return;
        const Vec4 white{1.0f, 1.0f, 1.0f, 1.0f};
        const Vec2 coins = kRoomOrigin + Vec2(440.0f, 520.0f);
        const Vec4 tints[] = {white,
                              {0.6f, 0.8f, 1.0f, 1.0f},
                              {1.0f, 0.5f, 0.5f, 1.0f},
                              white,
                              {1.0f, 1.0f, 1.0f, 0.4f}};
        for (usize i = 0; i < 5; ++i)
            r.DrawSprite(m_Coin, coins + Vec2(100.0f * static_cast<f32>(i), 0.0f),
                         {.Scale = Vec2(3.0f), .Tint = tints[i], .FlipY = i == 3});
        // Lift the hero while the jump is in the air.
        const Vec2 at = m_HeroPosition - Vec2(0.0f, m_Hero.IsInAir() ? 28.0f : 0.0f);
        r.DrawSprite(m_Hero.GetAnimator(), at,
                     {.Scale = Vec2(4.0f),
                      .Tint = m_Hero.GetTint(),
                      .FlipX = m_FacingLeft,
                      .PixelSnap = true});
    }

    // The mouse in world units, if it is over the window.
    [[nodiscard]] std::optional<Vec2> GetMouseWorld() const
    {
        if (!SDL_GetMouseFocus())
            return std::nullopt;
        Vec2 mouse;
        SDL_GetMouseState(&mouse.x, &mouse.y);
        // Window units -> render target pixels (they differ on high-DPI displays).
        const Vec2 pixels = mouse * (m_Camera.GetTargetSize().x / m_Shared.GetViewSize().x);
        return m_Camera.ScreenToWorld(pixels);
    }

    // Text demo: one pixel font baked at three sizes (Nearest: crisp), and smooth (Linear,
    // oversampled) at a large size, scaling, measuring, alignment and Latin-1 characters.
    void DrawTextDemo(Emerald::Renderer2D& r, const Vec2& size)
    {
        if (!m_Shared.HasFonts())
            return;
        using Emerald::TextAlign;
        const Emerald::Font& pixel = *m_Shared.PixelFont;
        const Emerald::Font& small = *m_Shared.SmallFont;
        const Vec4 white{1.0f, 1.0f, 1.0f, 1.0f};
        const Vec4 gold{1.0f, 0.85f, 0.3f, 1.0f};
        const Vec4 grey{0.65f, 0.7f, 0.75f, 1.0f};

        // Header, top-left: the pixel font at 16 px and 8 px.
        r.DrawString(pixel, "EMERALD TEXT", {30.0f, 30.0f}, {0.35f, 0.95f, 0.55f, 1.0f});
        r.DrawString(small, "stb_truetype -> one atlas per font\nsprite batch, 1 draw call each",
                     {30.0f, 56.0f}, grey);

        // Smooth text in the middle, gently breathing: Linear filtering keeps it soft when scaled.
        const f32 breathe = 1.0f + 0.08f * std::sin(1.5f * m_Time);
        r.DrawString(*m_Shared.SmoothFont, "Hello, world!", {size.x * 0.5f, 140.0f}, white, breathe,
                     TextAlign::Center);

        // MeasureText: a box drawn exactly around a string.
        const std::string_view measured = "MeasureText";
        const Vec2 box = pixel.MeasureText(measured, 2.0f);
        const Vec2 boxAt{size.x - 30.0f - box.x, 30.0f};
        r.DrawRect(boxAt - Vec2(4.0f), box + Vec2(8.0f), gold);
        r.DrawString(pixel, measured, boxAt, gold, 2.0f);
        r.DrawString(small,
                     std::to_string(static_cast<i32>(box.x)) + " x " +
                         std::to_string(static_cast<i32>(box.y)) + " px",
                     {size.x - 30.0f, boxAt.y + box.y + 12.0f}, grey, 1.0f, TextAlign::Right);

        // Alignment: three blocks, each line aligned on the marker line through its anchor.
        const f32 top = size.y - 190.0f;
        const TextAlign aligns[] = {TextAlign::Left, TextAlign::Center, TextAlign::Right};
        const std::string_view texts[] = {"Left\naligned\ntext", "Center\naligned\ntext",
                                          "Right\naligned\ntext"};
        for (usize i = 0; i < 3; ++i) {
            const f32 x = size.x * (0.3f + 0.2f * static_cast<f32>(i));
            r.DrawLine({x, top - 10.0f}, {x, top + 60.0f}, {0.3f, 0.7f, 1.0f, 0.6f});
            r.DrawString(pixel, texts[i], {x, top}, white, 1.0f, aligns[i]);
        }

        // Latin-1 from the extra glyph range, and the pixel font scaled 3x (still crisp).
        r.DrawString(pixel,
                     "Caf\xC3\xA9 \xC2\xBFQu\xC3\xA9 tal? Gr\xC3\xBC\xC3\x9F"
                     "e \xC2\xA9 2026",
                     {size.x * 0.5f, size.y - 100.0f}, gold, 1.0f, TextAlign::Center);
        r.DrawString(pixel, "x3", {size.x * 0.5f - 20.0f, size.y - 76.0f}, white, 3.0f);

        // Live stats, bottom-left (last frame's numbers), and the controls.
        const Emerald::Renderer2D& stats = m_Shared.App.GetRenderer2D();
        r.DrawString(small,
                     std::string("camera: ") + (m_FreeCamera ? "free pan" : "follow") +
                         "  Tab mode, wheel/Q/E zoom, F/G rotate, X shake, R reset",
                     {30.0f, size.y - 58.0f}, grey);
        r.DrawString(small, "M pause, T tilemap room, C CRT, Esc quit", {30.0f, size.y - 44.0f},
                     grey);
        r.DrawString(small,
                     "frame " + std::to_string(m_Shared.App.GetFrameCount()) + "  sprites " +
                         std::to_string(stats.GetLastFrameSpriteCount()) + "  draw calls " +
                         std::to_string(stats.GetLastFrameDrawCalls()),
                     {30.0f, size.y - 30.0f}, grey);
    }

    // Sprite demo: scaling, rotation, flipping, tint and alpha, pixel snapping, and layering in
    // call order with lines.
    void DrawSprites(Emerald::Renderer2D& r, const Vec2& room, const Vec2& size)
    {
        if (!m_Shared.Atlas)
            return;
        const Emerald::Sprite gem = m_Shared.Atlas->Get("gem");
        const Emerald::Sprite ring = m_Shared.Atlas->Get("ring");

        // A row along the top: plain, flipped, tinted, half transparent (4x, snapped to pixels).
        const f32 y = room.y + 70.0f;
        const Emerald::SpriteOptions big{.Scale = Vec2(4.0f), .PixelSnap = true};
        r.DrawSprite(gem, {room.x + size.x * 0.5f - 200.0f, y}, big);
        r.DrawSprite(gem, {room.x + size.x * 0.5f - 120.0f, y},
                     {.Scale = Vec2(4.0f), .FlipX = true});
        r.DrawSprite(gem, {room.x + size.x * 0.5f - 40.0f, y},
                     {.Scale = Vec2(4.0f), .Tint = {1.0f, 0.4f, 0.4f, 1.0f}});
        r.DrawSprite(gem, {room.x + size.x * 0.5f + 40.0f, y},
                     {.Scale = Vec2(4.0f), .Tint = {0.5f, 0.7f, 1.0f, 1.0f}});
        r.DrawSprite(gem, {room.x + size.x * 0.5f + 120.0f, y},
                     {.Scale = Vec2(4.0f), .Tint = {1.0f, 1.0f, 1.0f, 0.4f}});
        r.DrawSprite(ring, {room.x + size.x * 0.5f + 200.0f, y}, big);
        // A texture whose file does not exist: the asset manager's checkerboard placeholder.
        const Vec2 missingAt = room + Vec2(size.x - 110.0f, size.y * 0.42f);
        if (m_Shared.Missing)
            r.DrawSprite(*m_Shared.Missing, missingAt);
        if (m_Shared.SmallFont)
            r.DrawString(*m_Shared.SmallFont, "missing.png", missingAt + Vec2(0.0f, 44.0f),
                         {1.0f, 0.4f, 1.0f, 1.0f}, 1.0f, Emerald::TextAlign::Center);

        // Layering: a spinning gem, a line on top of it, then a ring on top of the line.
        const Vec2 center = room + size * 0.5f;
        r.DrawSprite(gem, center, {.Scale = Vec2(8.0f), .Rotation = 0.6f * m_Time});
        r.DrawLine(center - Vec2(90.0f, 0.0f), center + Vec2(90.0f, 0.0f),
                   {1.0f, 1.0f, 1.0f, 1.0f});
        r.DrawSprite(ring, center + Vec2(40.0f, 0.0f), {.Scale = Vec2(3.0f), .Rotation = -m_Time});
    }

#if EMERALD_WITH_IMGUI
    void ShowAnimation()
    {
        const Emerald::Animator& hero = m_Hero.GetAnimator();
        const Emerald::Animation* animation = hero.GetAnimation();
        ImGui::Text("Hero: %s, frame %u (%.2f s), speed %.1f, %u loops",
                    animation ? animation->Name.c_str() : "-", hero.GetFrameIndex(),
                    static_cast<f64>(hero.GetFrameTime()), static_cast<f64>(hero.GetSpeed()),
                    hero.GetLoopCount());
        ImGui::Text("Jumps landed (finish events): %u  coin frame %u", m_Hero.GetJumpsLanded(),
                    m_Coin.GetFrameIndex());
        if (m_Shared.Hero)
            ImGui::Text("Atlas: %zu sprites, %zu animations, %zu missing frames",
                        m_Shared.Hero->GetRegions().size(), m_Shared.Hero->GetAnimations().size(),
                        m_Shared.Hero->GetMissingFrames().size());
    }

    // Camera stats and a few live controls.
    void ShowCamera()
    {
        ImGui::Separator();
        const Vec2 pos = m_Camera.GetPosition();
        const Vec2 eye = m_Camera.GetEyePosition();
        ImGui::Text("Camera (%s): %.1f, %.1f  eye %.1f, %.1f", m_FreeCamera ? "free" : "follow",
                    static_cast<f64>(pos.x), static_cast<f64>(pos.y), static_cast<f64>(eye.x),
                    static_cast<f64>(eye.y));
        ImGui::Text("Zoom %.2f  rotation %.1f deg  trauma %.2f",
                    static_cast<f64>(m_Camera.GetZoom()),
                    static_cast<f64>(Emerald::ToDegrees(m_Camera.GetEyeRotation())),
                    static_cast<f64>(m_Camera.GetTrauma()));
        const Emerald::Rect2D visible = m_Camera.GetVisibleBounds();
        ImGui::Text("Visible %.0f, %.0f - %.0f, %.0f  (%.2f px/unit)",
                    static_cast<f64>(visible.Min.x), static_cast<f64>(visible.Min.y),
                    static_cast<f64>(visible.Max.x), static_cast<f64>(visible.Max.y),
                    static_cast<f64>(m_Camera.GetPixelsPerUnit()));
        if (const std::optional<Vec2> mouse = GetMouseWorld())
            ImGui::Text("Mouse in world: %.1f, %.1f", static_cast<f64>(mouse->x),
                        static_cast<f64>(mouse->y));
        else
            ImGui::TextDisabled("Mouse in world: -");
        if (ImGui::Button("Shake"))
            m_Camera.AddTrauma(0.6f);
        ImGui::SameLine();
        ImGui::SliderFloat("Max offset", &m_Camera.GetShakeParams().MaxOffset, 0.0f, 40.0f);
        ImGui::Separator();
    }

    // The collision yard (right of the room): broadphase and narrowphase counts, SAT and ray.
    void ShowCollision()
    {
        const CollisionDemo& c = m_Collision;
        ImGui::Text("Collision: %zu balls in %zu hash cells, %zu candidate pairs, %u contacts",
                    c.GetBallCount(), c.GetCellCount(), c.GetPairCount(), c.GetContactCount());
        if (c.GetSat())
            ImGui::Text("SAT: overlap, normal %+.2f %+.2f, depth %.1f",
                        static_cast<f64>(c.GetSat()->Normal.x),
                        static_cast<f64>(c.GetSat()->Normal.y),
                        static_cast<f64>(c.GetSat()->Depth));
        else
            ImGui::TextDisabled("SAT: separated");
        if (c.GetHit())
            ImGui::Text("Ray (hero -> mouse): hit at %.0f", static_cast<f64>(c.GetHit()->Distance));
        else
            ImGui::TextDisabled("Ray (hero -> mouse): no hit");
        ImGui::Checkbox("Show hash cells", &m_ShowHashGrid);
    }
#endif

    SandboxShared& m_Shared;
    const std::array<Vec2, 10> m_Star = MakeStar();
    f32 m_Time = 0.0f;
    Vec2 m_HeroPosition = kRoomOrigin + Vec2(640.0f, 450.0f); // world units
    Emerald::Camera2D m_Camera;
    bool m_FreeCamera = false; // false: follow the hero
    HeroSprite m_Hero{m_Shared.Hero};
    Emerald::Animator m_Coin;
    bool m_FacingLeft = false;
    CollisionDemo m_Collision; // the yard right of the room
    bool m_ShowHashGrid = true;
    f32 m_PulseAge = 1.0f; // seconds since the last jump
};
