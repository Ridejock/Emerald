#pragma once

// The options screen, built from the UI widgets (Emerald/UI): an overlay over the scene that
// pushed it (title or pause). Volume sliders, fullscreen and CRT toggles, a highlight color
// choice, the controls, and Defaults / Back. Mouse, keyboard and gamepad all work; Backspace / M /
// East (the UiBack action) or "Back" leave; leaving saves the options for the next run
// (SandboxShared::SaveSettings, through the save system).

#include <Emerald/Emerald.h>

#include "Shared.h"

class OptionsScene final : public Emerald::Scene {
public:
    explicit OptionsScene(SandboxShared& shared) : Scene("Options"), m_Shared(shared)
    {
        DrawBelow = true;
        m_Ui.GetStyle().Highlight = kHighlightColors[m_Shared.Highlight];
    }

    void OnUpdate(f32 dt) override
    {
        using Emerald::TextAlign;
        Emerald::Application& app = m_Shared.App;
        const Emerald::Input& input = app.GetInput();
        // The UI is drawn in window units, the mouse's units too.
        m_Ui.Begin(Emerald::ReadUiInput(input, input.GetMouse().GetPosition()), dt);
        m_Ui.BeginPanel(
            "OPTIONS",
            {.Position = m_Shared.GetViewSize() * 0.5f, .Width = 520.0f, .Pivot = {0.5f, 0.5f}});
        f32 master = app.GetAudio().GetMasterVolume();
        if (m_Ui.Slider("Master volume", &master, 0.0f, 1.0f))
            app.GetAudio().SetMasterVolume(master);
        if (m_Ui.Slider("Effects volume", &m_Shared.EffectsVolume, 0.0f, 1.0f))
            app.GetAudio().Play(m_Shared.Blip, {.Volume = m_Shared.EffectsVolume}); // a sample
        f32 music = app.GetAudio().GetGroupVolume(Emerald::AudioGroup::Music);
        if (m_Ui.Slider("Music group", &music, 0.0f, 1.0f))
            app.GetAudio().SetGroupVolume(Emerald::AudioGroup::Music, music);
        f32 track = app.GetAudio().GetMusicVolume();
        if (m_Ui.Slider("Music track", &track, 0.0f, 1.0f))
            app.GetAudio().SetMusicVolume(track);
        // Read back every frame, so changes made elsewhere (the C key) show up.
        bool fullscreen = app.GetWindow().IsFullscreen();
        if (m_Ui.Toggle("Fullscreen", &fullscreen))
            app.GetWindow().SetFullscreen(fullscreen);
        bool crt = app.IsCrtEnabled();
        if (m_Ui.Toggle("CRT effect", &crt))
            app.SetCrtEnabled(crt);
        if (m_Ui.Choice("Highlight", &m_Shared.Highlight, {"Emerald", "Gold", "Sky"}))
            m_Ui.GetStyle().Highlight = kHighlightColors[m_Shared.Highlight];
        m_Ui.Space(8.0f);
        m_Ui.Label("CONTROLS", TextAlign::Center);
        m_Ui.Label("Move   arrows / WASD / d-pad");
        m_Ui.Label("Pick   Enter / Space / South");
        m_Ui.Label("Back   Backspace / M / East");
        m_Ui.Space(8.0f);
        m_Ui.Label("MUSIC", TextAlign::Center);
        m_Ui.Columns(2);
        if (m_Ui.Button("Play A"))
            app.GetAudio().PlayMusic(Emerald::Paths::GetBasePath() / "assets/audio/loop_a.ogg",
                                     400.0f);
        if (m_Ui.Button("Crossfade B"))
            app.GetAudio().CrossfadeMusic(Emerald::Paths::GetBasePath() / "assets/audio/loop_b.ogg",
                                          800.0f);
        if (m_Ui.Button("Stop music"))
            app.GetAudio().StopMusic(400.0f);
        m_Ui.Space(8.0f);
        m_Ui.Columns(2);
        if (m_Ui.Button("Defaults")) {
            app.GetAudio().SetMasterVolume(1.0f);
            app.GetAudio().SetGroupVolume(Emerald::AudioGroup::Music, 1.0f);
            app.GetAudio().SetMusicVolume(1.0f);
            m_Shared.EffectsVolume = 1.0f;
        }
        if ((m_Ui.Button("Back") || m_Ui.WasBackPressed()) && !m_Leaving) {
            m_Leaving = true;
            m_Shared.SaveSettings(); // kept for the next run (Shared.h)
            GetStack()->Pop();
        }
        m_Ui.EndPanel();
        m_Ui.End();
    }

    void OnRender2D(Emerald::Renderer2D& r) override
    {
        if (!m_Shared.PixelFont)
            return;
        const Emerald::Vec2 size = m_Shared.GetViewSize();
        r.Begin(Emerald::Mat4::OrthoPixelSpace(size.x, size.y));
        r.FillRect({0.0f, 0.0f}, size, {0.0f, 0.02f, 0.04f, 0.6f}); // dims the scene below
        m_Ui.Draw(r, *m_Shared.PixelFont);
        r.End();
    }

private:
    SandboxShared& m_Shared;
    Emerald::Ui m_Ui;
    bool m_Leaving = false; // the pop happens at the end of the frame
};
