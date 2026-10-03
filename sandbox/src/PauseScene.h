#pragma once

// The pause menu: an overlay (DrawBelow) over the scene that pushed it, which stays drawn but
// frozen (no UpdateBelow) and gets no input. M / Start or "Resume" plays the menu's outro, then
// pops; "Title screen" fades everything out to a new title (ReplaceAll).

#include <Emerald/Emerald.h>

#include "MenuDemo.h"
#include "Shared.h"

#if EMERALD_WITH_IMGUI
#include <imgui.h>
#endif

class PauseScene final : public Emerald::Scene {
public:
    explicit PauseScene(SandboxShared& shared) : Scene("Pause"), m_Shared(shared)
    {
        DrawBelow = true;
    }

    void OnEnter() override { m_Menu.Open(); }

    void OnUpdate(f32 dt) override
    {
        const Emerald::Input& input = m_Shared.App.GetInput();
        if (m_Menu.IsOpen()) {
            if (input.WasActionPressed("Menu"))
                m_Menu.Close();
            if (input.WasActionPressed("MenuUp"))
                m_Menu.MoveSelection(-1);
            if (input.WasActionPressed("MenuDown"))
                m_Menu.MoveSelection(1);
            if (input.WasActionPressed("MenuSelect"))
                Select();
        }
        m_Menu.Update(dt);
        // The outro has played: leave (once; the pop happens at the end of the frame).
        if (!m_Menu.IsVisible() && !m_Leaving) {
            m_Leaving = true;
            GetStack()->Pop();
        }
    }

    void OnRender2D(Emerald::Renderer2D& r) override
    {
        if (!m_Shared.HasFonts() || !m_Shared.White)
            return;
        const Emerald::Vec2 size = m_Shared.GetViewSize();
        r.Begin(Emerald::Mat4::OrthoPixelSpace(size.x, size.y));
        m_Menu.Draw(r, size, *m_Shared.SmoothFont, *m_Shared.PixelFont, *m_Shared.SmallFont,
                    *m_Shared.White);
        r.End();
    }

    void OnImGui() override
    {
#if EMERALD_WITH_IMGUI
        ImGui::Begin("Emerald");
        if (ImGui::CollapsingHeader("Pause", ImGuiTreeNodeFlags_DefaultOpen))
            ImGui::Text("Menu: %s, %zu tweens, %zu timers running",
                        m_Menu.IsClosing() ? "closing" : (m_Menu.IsVisible() ? "open" : "closed"),
                        m_Menu.GetTweenCount(), m_Menu.GetTimerCount());
        ImGui::End();
#endif
    }

private:
    void Select()
    {
        Emerald::Application& app = m_Shared.App;
        switch (m_Menu.GetSelected()) {
        case 0: // Resume
            m_Menu.Close();
            break;
        case 1:
            app.SetCrtEnabled(!app.IsCrtEnabled());
            break;
        case 2: // Replay intro
            m_Menu.Open();
            break;
        case 3:
            m_Leaving = true;
            GetStack()->ReplaceAll(m_Shared.Make(SceneId::Title), FadeBlack());
            break;
        default:
            app.Quit();
            break;
        }
    }

    SandboxShared& m_Shared;
    MenuDemo m_Menu{"PAUSED",
                    {"Resume", "CRT effect", "Replay intro", "Title screen", "Quit"},
                    "Up/Down + Enter, M resumes"};
    bool m_Leaving = false; // a pop or the way to the title is on its way
};
