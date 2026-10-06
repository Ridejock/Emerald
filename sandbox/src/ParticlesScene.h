#pragma once

// The particle effects scene (#16): the effects in assets/particles/*.json, played at an emitter
// in the middle of the window. Effects are assets, so editing a file (debug builds) changes the
// effect while it plays. In ImGui builds the particle editor window (F1 shows the panels) edits
// every field of the current effect live and saves it back to its file.
//
// Up / Down picks the effect, Space / South bursts (the effect's burst count), hold the left
// mouse button to move the emitter, Tab switches between quads and lines, R clears and
// recenters, M / Start pauses. From the title menu or --particles.

#include <array>
#include <string>

#include <Emerald/Emerald.h>

#include "Shared.h"

class ParticlesScene final : public Emerald::Scene {
public:
    using Vec2 = Emerald::Vec2;

    explicit ParticlesScene(SandboxShared& shared) : Scene("Particles"), m_Shared(shared)
    {
        for (usize i = 0; i < kFiles.size(); ++i)
            m_Effects[i] = shared.App.GetAssets().Load<Emerald::ParticleEffect>(kFiles[i]);
        m_Emitter = GetHome();
    }

    void OnUpdate(f32 dt) override
    {
        const Emerald::Input& input = m_Shared.App.GetInput();
        if (input.WasActionPressed("Menu"))
            GetStack()->Push(m_Shared.Make(SceneId::Pause));
        if (input.WasActionPressed("MenuUp"))
            Pick(m_Current + kFiles.size() - 1);
        if (input.WasActionPressed("MenuDown"))
            Pick(m_Current + 1);
        if (input.WasActionPressed("CameraMode"))
            m_Lines = !m_Lines;
        if (input.WasActionPressed("CameraReset")) {
            m_Particles.Clear();
            m_Emitter = GetHome();
        }
        // Clicks on an ImGui window never reach the game (the Application filters them).
        if (input.GetMouse().IsButtonDown(Emerald::MouseButton::Left))
            m_Emitter = input.GetMouse().GetPosition();

        // The asset was (re)loaded: start over from the file (unsaved edits give way to it).
        if (m_Effects[m_Current]->Revision != m_Working.Revision)
            m_Working = *m_Effects[m_Current];
        if (input.WasActionPressed("Pulse"))
            Burst();
        if (m_Working.GetConfig().Rate > 0.0f)
            m_Particles.EmitContinuous(m_Working.GetConfig(), m_Continuous, m_Emitter, dt);
        m_Particles.Update(dt);
    }

    void OnRender2D(Emerald::Renderer2D& r) override
    {
        const Vec2 size = m_Shared.GetViewSize();
        r.Begin(Emerald::Mat4::OrthoPixelSpace(size.x, size.y));
        r.FillRect({0.0f, 0.0f}, size, {0.04f, 0.05f, 0.08f, 1.0f});
        const Emerald::Sprite dot =
            m_Shared.White ? Emerald::Sprite::FromTexture(*m_Shared.White) : Emerald::Sprite{};
        m_Particles.Draw(
            r, {.Sprite = m_Lines || !m_Shared.White ? nullptr : &dot, .Blend = m_Working.Blend});
        r.DrawRect(m_Emitter - Vec2(3.0f), Vec2(6.0f), {1.0f, 1.0f, 1.0f, 0.5f});
        DrawHud(r, size);
        r.End();
    }

    void OnImGui() override
    {
        const Emerald::ParticleEditorResult edit = m_Editor.Show(
            m_Working, m_Shared.App.GetAssets().Resolve(kFiles[m_Current]), "Particle editor");
        if (edit.Burst)
            Burst();
        if (edit.Clear)
            m_Particles.Clear();
    }

private:
    static constexpr std::array<const char*, 4> kFiles = {
        "particles/sparks.json", "particles/fire.json", "particles/fountain.json",
        "particles/smoke.json"};

    void Pick(usize index)
    {
        m_Current = index % kFiles.size();
        m_Working = *m_Effects[m_Current];
        m_Continuous.Reset();
    }

    // Left of the middle and low: clear of the ImGui windows (the editor sits at the right).
    [[nodiscard]] Vec2 GetHome() const { return m_Shared.GetViewSize() * Vec2(0.35f, 0.7f); }

    void Burst() { m_Particles.Emit(m_Working.GetConfig(), m_Emitter, m_Working.Burst); }

    void DrawHud(Emerald::Renderer2D& r, Vec2 size) const
    {
        if (!m_Shared.SmallFont)
            return;
        const Emerald::Font& font = *m_Shared.SmallFont;
        const Emerald::Vec4 white{1.0f, 1.0f, 1.0f, 0.95f};
        const Emerald::Vec4 grey{0.65f, 0.7f, 0.75f, 1.0f};
        const Emerald::ParticleEmitterConfig& c = m_Working.GetConfig();
        r.DrawString(font, std::string("PARTICLES  ") + kFiles[m_Current], {16.0f, 16.0f}, white);
        r.DrawString(font,
                     std::to_string(m_Particles.GetCount()) + " particles, rate " +
                         std::to_string(static_cast<i32>(c.Rate)) + "/s, burst " +
                         std::to_string(m_Working.Burst) + ", " +
                         (m_Working.Blend == Emerald::BlendMode::Additive ? "additive" : "alpha") +
                         (m_Lines ? ", lines" : ", quads"),
                     {16.0f, 30.0f}, grey);
        r.DrawString(font,
                     "Up/Down effect  Space burst  hold left mouse: move  Tab quads/lines  "
                     "R reset  M pause  F1 editor (ImGui builds)",
                     {12.0f, size.y - 20.0f}, {1.0f, 1.0f, 1.0f, 0.9f});
    }

    SandboxShared& m_Shared;
    std::array<Emerald::AssetHandle<Emerald::ParticleEffect>, kFiles.size()> m_Effects;
    // The effect being played and edited: a copy of the asset, so the editor's changes show at
    // once, and Save writes them to the file (whose hot reload brings them back here).
    Emerald::ParticleEffect m_Working;
    usize m_Current = 0;
    Emerald::ParticleSystem m_Particles{8192};
    Emerald::ContinuousEmitter m_Continuous;
    Emerald::ParticleEditor m_Editor; // ImGui builds
    Vec2 m_Emitter{};
    bool m_Lines = false;
};
