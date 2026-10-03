#pragma once

// The tilemap room scene (TilemapRoom.h): the hero walks the Tiled map with tile collision and a
// camera of its own. WASD walks, Shift runs, M / Start pauses, T fades to the entity swarm.

#include <Emerald/Emerald.h>

#include "Shared.h"
#include "TilemapRoom.h"

#if EMERALD_WITH_IMGUI
#include <imgui.h>
#endif

class TilemapScene final : public Emerald::Scene {
public:
    explicit TilemapScene(SandboxShared& shared) : Scene("Tilemap room"), m_Shared(shared)
    {
        const SandboxOptions& o = shared.Options;
        m_Room.Load(shared.App.GetAssets(), o.Room);
        m_Room.SetOverlays(o.Collision, o.Objects, !o.NoCull);
    }

    void OnFixedUpdate(f32 dt) override
    {
        const Emerald::Input& input = m_Shared.App.GetInput();
        Emerald::Vec2 direction(input.GetAxis("MoveX"), input.GetAxis("MoveY"));
        if (const f32 length = Emerald::Length(direction); length > 1.0f)
            direction = direction / length;
        const bool run = input.IsActionDown("Run");
        const bool moved = m_Room.Update(direction, run, dt, m_Shared.GetViewSize());
        m_Hero.Update(moved, run ? 1.8f : 1.0f, dt);
    }

    void OnUpdate(f32 /*dt*/) override
    {
        const Emerald::Input& input = m_Shared.App.GetInput();
        if (input.WasActionPressed("Menu"))
            GetStack()->Push(m_Shared.Make(SceneId::Pause));
        else if (input.WasActionPressed("Scene"))
            GetStack()->Replace(m_Shared.Make(SceneId::Swarm), FadeBlack());
    }

    // The room, the hero in it at the map's scale, and a line of help on top.
    void OnRender2D(Emerald::Renderer2D& r) override
    {
        const Emerald::Vec2 size = m_Shared.GetViewSize();
        m_Room.Draw(r, size, m_Shared.GetTargetSize(), [&](Emerald::Vec2 feet, bool facingLeft) {
            if (m_Hero.IsLoaded())
                r.DrawSprite(m_Hero.GetAnimator(), feet,
                             {.Origin = {0.5f, 1.0f}, .FlipX = facingLeft, .PixelSnap = true});
        });
        r.Begin(Emerald::Mat4::OrthoPixelSpace(size.x, size.y));
        if (m_Shared.SmallFont)
            r.DrawString(*m_Shared.SmallFont,
                         "Tilemap room: WASD walks, Shift runs, M pause, T entity swarm",
                         {12.0f, size.y - 20.0f}, {1.0f, 1.0f, 1.0f, 0.9f});
        r.End();
    }

    void OnImGui() override
    {
#if EMERALD_WITH_IMGUI
        ImGui::Begin("Emerald"); // appends to the app's window
        if (ImGui::CollapsingHeader("Tilemap room", ImGuiTreeNodeFlags_DefaultOpen))
            m_Room.ShowImGui();
        ImGui::End();
#endif
    }

private:
    SandboxShared& m_Shared;
    TilemapRoom m_Room;
    HeroSprite m_Hero{m_Shared.Hero};
};
