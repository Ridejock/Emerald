#pragma once

// The title screen: the tweened menu (MenuDemo.h) over drifting gems and the idle hero. Up / Down
// + Enter (d-pad + South) picks: the camera demo (a fade to black), the tilemap room (a custom
// "blinds" transition, see Shared.h) or quit. Each choice replaces the title with the new scene.

#include <cmath>

#include <Emerald/Emerald.h>

#include "MenuDemo.h"
#include "Shared.h"

#if EMERALD_WITH_IMGUI
#include <imgui.h>
#endif

class TitleScene final : public Emerald::Scene {
public:
    explicit TitleScene(SandboxShared& shared) : Scene("Title"), m_Shared(shared) {}

    void OnEnter() override { m_Menu.Open(); }

    void OnUpdate(f32 dt) override
    {
        m_Time += dt;
        const Emerald::Input& input = m_Shared.App.GetInput(); // blocked during transitions
        if (input.WasActionPressed("MenuUp"))
            m_Menu.MoveSelection(-1);
        if (input.WasActionPressed("MenuDown"))
            m_Menu.MoveSelection(1);
        if (input.WasActionPressed("MenuSelect")) {
            switch (m_Menu.GetSelected()) {
            case 0:
                GetStack()->Replace(m_Shared.Make(SceneId::Demo), FadeBlack());
                break;
            case 1:
                GetStack()->Replace(m_Shared.Make(SceneId::Tilemap),
                                    Blinds(0.45f, {0.02f, 0.08f, 0.06f}));
                break;
            default:
                m_Shared.App.Quit();
                break;
            }
        }
        m_Menu.Update(dt);
        m_Hero.Update(false, 1.0f, dt);
    }

    void OnRender2D(Emerald::Renderer2D& r) override
    {
        using Emerald::Vec2;
        const Vec2 size = m_Shared.GetViewSize();
        r.Begin(Emerald::Mat4::OrthoPixelSpace(size.x, size.y));
        // Gems drifting up on slow sine paths, wrapping around.
        if (m_Shared.Atlas) {
            const Emerald::Sprite gem = m_Shared.Atlas->Get("gem");
            for (i32 i = 0; i < 24; ++i) {
                const f32 k = static_cast<f32>(i);
                const f32 y = std::fmod(k * 97.0f - m_Time * (30.0f + 3.0f * k), size.y + 80.0f);
                const Vec2 at{std::fmod(k * 173.0f, size.x) + 20.0f * std::sin(m_Time + k),
                              y < -40.0f ? y + size.y + 80.0f : y};
                r.DrawSprite(gem, at,
                             {.Scale = Vec2(2.0f + static_cast<f32>(i % 3)),
                              .Rotation = 0.3f * m_Time + k,
                              .Tint = {1.0f, 1.0f, 1.0f, 0.35f}});
            }
        }
        if (m_Hero.IsLoaded())
            r.DrawSprite(m_Hero.GetAnimator(), {size.x * 0.25f, size.y * 0.55f},
                         {.Scale = Vec2(8.0f), .PixelSnap = true});
        if (m_Shared.HasFonts() && m_Shared.White) {
            m_Menu.Draw(r, size, *m_Shared.SmoothFont, *m_Shared.PixelFont, *m_Shared.SmallFont,
                        *m_Shared.White);
            r.DrawString(*m_Shared.SmallFont, "a scene stack demo: title > game > pause",
                         {size.x * 0.5f, 170.0f}, {0.65f, 0.7f, 0.75f, 1.0f}, 1.0f,
                         Emerald::TextAlign::Center);
        }
        r.End();
    }

    void OnImGui() override
    {
#if EMERALD_WITH_IMGUI
        ImGui::Begin("Emerald");
        if (ImGui::CollapsingHeader("Title", ImGuiTreeNodeFlags_DefaultOpen))
            ImGui::Text("Menu: %s, %zu tweens, %zu timers running",
                        m_Menu.IsOpen() ? "open" : "closed", m_Menu.GetTweenCount(),
                        m_Menu.GetTimerCount());
        ImGui::End();
#endif
    }

private:
    SandboxShared& m_Shared;
    MenuDemo m_Menu{"EMERALD", {"Camera demo", "Tilemap room", "Quit"}, "Up/Down + Enter"};
    HeroSprite m_Hero{m_Shared.Hero};
    f32 m_Time = 0.0f;
};
