#pragma once

// The entity swarm scene (Emerald/Entity): thousands of entities in a World owned by the scene.
// Walking heroes, spinning coins and gems move (UpdateMovement), animate (UpdateAnimation),
// bump into each other through the broadphase (CollisionSystem) and are drawn sorted by layer
// and y (DrawSprites). Each lives a few seconds; the scene keeps the population at its target by
// spawning new ones, so thousands are spawned and destroyed every second.
//
// Space / South: a burst of 1,000 at the mouse (or the middle). Backspace: destroy half.
// Up / Down: target population -/+ 1,000. M / Start pauses, T fades to the platformer.

#include <chrono>
#include <cmath>
#include <cstdio>
#include <optional>
#include <random>
#include <string>

#include <Emerald/Emerald.h>

#include "Shared.h"

#if EMERALD_WITH_IMGUI
#include <imgui.h>
#endif

class SwarmScene final : public Emerald::Scene {
public:
    using Vec2 = Emerald::Vec2;
    using Entity = Emerald::Entity;

    explicit SwarmScene(SandboxShared& shared) : Scene("Entity swarm"), m_Shared(shared) {}

    void OnEnter() override
    {
        const Vec2 size = m_Shared.GetViewSize();
        std::uniform_real_distribution<f32> x(0.0f, size.x), y(0.0f, size.y);
        for (u32 i = 0; i < m_Target; ++i)
            Spawn({x(m_Rng), y(m_Rng)}, RandomVelocity(30.0f, 90.0f));
    }

    void OnUpdate(f32 dt) override
    {
        const Emerald::Input& input = m_Shared.App.GetInput();
        if (input.WasActionPressed("Menu"))
            GetStack()->Push(m_Shared.Make(SceneId::Pause));
        else if (input.WasActionPressed("Scene"))
            GetStack()->Replace(m_Shared.Make(SceneId::Platformer), FadeBlack());
        if (input.WasActionPressed("Pulse"))
            Burst(GetMouse().value_or(m_Shared.GetViewSize() * 0.5f));
        if (input.WasActionPressed("SwarmCut"))
            DestroyHalf();
        if (input.WasActionPressed("MenuUp"))
            m_Target = m_Target > 1000 ? m_Target - 1000 : 0;
        if (input.WasActionPressed("MenuDown"))
            m_Target += 1000;

        Simulate(Emerald::Min(dt, 1.0f / 30.0f));
        Time(m_AnimateMs, [&] { Emerald::UpdateAnimation(m_World, dt); });
        Time(m_LifeMs, [&] {
            UpdateLives(dt);
            TopUp(dt);
            m_World.Flush(); // the safe point: nothing is iterating
        });
        UpdateRates(dt);
    }

    void OnRender2D(Emerald::Renderer2D& r) override
    {
        const Vec2 size = m_Shared.GetViewSize();
        r.Begin(Emerald::Mat4::OrthoPixelSpace(size.x, size.y));
        Time(m_DrawMs, [&] {
            m_Drawn = Emerald::DrawSprites(m_World, r,
                                           {.SortByY = m_SortByY,
                                            .Visible = Emerald::Rect2D{{0.0f, 0.0f}, size},
                                            .Scratch = m_Shared.App.GetFrameAllocator()});
        });
        DrawHud(r, size);
        r.End();
    }

    void OnImGui() override
    {
#if EMERALD_WITH_IMGUI
        ImGui::Begin("Emerald");
        if (ImGui::CollapsingHeader("Entity swarm", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Entities: %zu (target %u), %u drawn, %.0f fps", m_World.GetCount(),
                        m_Target, m_Drawn, static_cast<f64>(m_Fps));
            ImGui::Text("Spawned %.0f/s, destroyed %.0f/s (%llu / %llu in total)",
                        static_cast<f64>(m_SpawnRate), static_cast<f64>(m_DestroyRate),
                        static_cast<unsigned long long>(m_World.GetSpawnedTotal()),
                        static_cast<unsigned long long>(m_World.GetDestroyedTotal()));
            ImGui::Text("Broadphase: %zu colliders, %zu cells, %zu candidate pairs, %zu contacts",
                        m_Collisions.GetBroadphase().GetCount(),
                        m_Collisions.GetBroadphase().GetCellCount(),
                        m_Collisions.GetCandidateCount(), m_Collisions.GetEvents().size());
            ImGui::Text("ms: movement %.3f  collisions %.3f  animation %.3f  lives %.3f  draw %.3f",
                        m_MoveMs, m_CollideMs, m_AnimateMs, m_LifeMs, m_DrawMs);
            i32 target = static_cast<i32>(m_Target);
            if (ImGui::SliderInt("Target", &target, 0, 20000))
                m_Target = static_cast<u32>(target);
            ImGui::Checkbox("Collisions", &m_Collide);
            ImGui::SameLine();
            ImGui::Checkbox("Sort by y", &m_SortByY);
            if (ImGui::Button("Burst"))
                Burst(m_Shared.GetViewSize() * 0.5f);
            ImGui::SameLine();
            if (ImGui::Button("Destroy half"))
                DestroyHalf();
            ImGui::SameLine();
            if (ImGui::Button("Destroy all"))
                m_World.Each<Life>([](Entity e, Life&) { e.Destroy(); });
        }
        ImGui::End();
#endif
    }

private:
    // Move, bounce off the window edges, then resolve the contacts. Once per frame (dt capped
    // at 1/30 s), not at the fixed rate: with thousands of entities a slow frame (a debug build)
    // would run up to 8 fixed steps, take longer still, and never catch up.
    void Simulate(f32 dt)
    {
        using namespace Emerald;
        EM_PROFILE_SCOPE("Swarm::Simulate"); // in Tracy with the profile preset
        Time(m_MoveMs, [&] {
            UpdateMovement(m_World, dt);
            const Vec2 size = m_Shared.GetViewSize();
            m_World.Each<Transform, Velocity>([&](Entity, Transform& t, Velocity& v) {
                if (t.Position.x < 0.0f || t.Position.x > size.x)
                    v.Linear.x = t.Position.x < 0.0f ? std::abs(v.Linear.x) : -std::abs(v.Linear.x);
                if (t.Position.y < 0.0f || t.Position.y > size.y)
                    v.Linear.y = t.Position.y < 0.0f ? std::abs(v.Linear.y) : -std::abs(v.Linear.y);
                t.Position =
                    Min(Max(t.Position, Vec2(0.0f)), size); // contacts may push past the edge
            });
        });
        if (!m_Collide)
            return;
        Time(m_CollideMs, [&] {
            for (const CollisionEvent& hit : m_Collisions.Update(m_World))
                Bounce(hit);
        });
    }

    // The scene's own component: how long the entity lives, and a flash after a bump.
    struct Life {
        f32 Age = 0.0f;
        f32 Span = 4.0f;
        f32 Flash = 0.0f;
    };
    enum class Kind : u8 { Hero, Coin, Gem };

    void Spawn(Vec2 at, Vec2 velocity)
    {
        using namespace Emerald;
        const auto kind = static_cast<Kind>(m_Rng() % 3);
        Entity e = m_World.Spawn();
        e.Add<Transform>(Transform{.Position = at});
        e.Add<Life>(Life{.Span = std::uniform_real_distribution<f32>(3.0f, 7.0f)(m_Rng)});
        // Gems (their own texture) on layer 0, coins and heroes (the hero sheet) on layer 1: the
        // sort keeps each texture together, so the swarm is two draw calls, not thousands.
        SpriteRenderer sprite{.Options = {.PixelSnap = true}, .Layer = kind == Kind::Gem ? 0 : 1};
        if (kind == Kind::Gem && m_Shared.Atlas) {
            sprite.Image = m_Shared.Atlas->Get("gem");
            sprite.Options.PixelSnap = false; // it spins
            e.Add<Velocity>(Velocity{.Linear = velocity, .Angular = velocity.x * 0.05f});
        } else {
            e.Add<Velocity>(Velocity{.Linear = velocity});
            if (m_Shared.Hero) {
                Animator& animator = e.Add<Animator>();
                animator.Play(m_Shared.Hero->GetAnimation(kind == Kind::Hero ? "walk" : "coin"));
                animator.SetSpeed(std::uniform_real_distribution<f32>(0.7f, 1.4f)(m_Rng));
            }
        }
        e.Add<SpriteRenderer>(sprite);
        e.Add<Collider>(Collider::MakeCircle(kind == Kind::Hero ? 6.0f : 5.0f));
    }

    Vec2 RandomVelocity(f32 minSpeed, f32 maxSpeed)
    {
        const f32 angle = std::uniform_real_distribution<f32>(0.0f, Emerald::TwoPi)(m_Rng);
        const f32 speed = std::uniform_real_distribution<f32>(minSpeed, maxSpeed)(m_Rng);
        return Vec2(std::cos(angle), std::sin(angle)) * speed;
    }

    // 1,000 in a disc around `at`, flying outwards. (Not all on one point: a million
    // overlapping pairs would stall the collision system for a while.)
    void Burst(Vec2 at)
    {
        std::uniform_real_distribution<f32> unit(0.0f, 1.0f);
        for (i32 i = 0; i < 1000; ++i) {
            const Vec2 v = RandomVelocity(40.0f, 260.0f);
            const f32 r = 160.0f * std::sqrt(unit(m_Rng)); // uniform over the disc
            Spawn(at + Emerald::Normalize(v) * r, v);
        }
    }

    void DestroyHalf()
    {
        bool odd = false;
        m_World.Each<Life>([&](Entity e, Life&) {
            if ((odd = !odd))
                e.Destroy();
        });
    }

    // Equal masses: push both apart by half the overlap and swap their speeds along the normal
    // when they move towards each other. Then flash both.
    static void Bounce(const Emerald::CollisionEvent& hit)
    {
        using namespace Emerald;
        const Vec2 n = hit.Hit.Normal;
        hit.A.Get<Transform>().Position -= n * (hit.Hit.Depth * 0.5f);
        hit.B.Get<Transform>().Position += n * (hit.Hit.Depth * 0.5f);
        Velocity& va = hit.A.Get<Velocity>();
        Velocity& vb = hit.B.Get<Velocity>();
        const f32 closing = Dot(va.Linear - vb.Linear, n);
        if (closing > 0.0f) {
            va.Linear -= n * closing;
            vb.Linear += n * closing;
            hit.A.Get<Life>().Flash = 0.2f;
            hit.B.Get<Life>().Flash = 0.2f;
        }
    }

    // Ages everything (old ones go), fades the flashes, faces walkers the way they move.
    void UpdateLives(f32 dt)
    {
        using namespace Emerald;
        m_World.Each<Life, SpriteRenderer, Velocity>(
            [dt](Entity e, Life& life, SpriteRenderer& s, const Velocity& v) {
                life.Age += dt;
                life.Flash = Max(life.Flash - dt, 0.0f);
                if (life.Age > life.Span)
                    e.Destroy();
                // Fade in over 0.2 s and out over the last 0.5 s; gold while flashing.
                const f32 alpha =
                    Clamp(Min(life.Age * 5.0f, (life.Span - life.Age) * 2.0f), 0.0f, 1.0f);
                s.Options.Tint = life.Flash > 0.0f ? Vec4(1.0f, 0.85f, 0.3f, alpha)
                                                   : Vec4(1.0f, 1.0f, 1.0f, alpha);
                s.Options.FlipX = v.Linear.x < 0.0f;
            });
    }

    // New entities at random places towards the target, at most 4,000 per second (so a
    // "destroy half" stays visible for a moment).
    void TopUp(f32 dt)
    {
        const Vec2 size = m_Shared.GetViewSize();
        std::uniform_real_distribution<f32> x(0.0f, size.x), y(0.0f, size.y);
        m_SpawnBudget = Emerald::Min(m_SpawnBudget + 4000.0f * dt, 150.0f); // no saving up
        const usize alive = m_World.GetCount() - m_World.GetPendingDestroyCount();
        for (usize i = alive; i < m_Target && m_SpawnBudget >= 1.0f; ++i, m_SpawnBudget -= 1.0f)
            Spawn({x(m_Rng), y(m_Rng)}, RandomVelocity(30.0f, 90.0f));
    }

    // Spawned / destroyed per second, over half-second windows.
    void UpdateRates(f32 dt)
    {
        m_RateTime += dt;
        ++m_RateFrames;
        if (m_RateTime < 0.5f)
            return;
        m_Fps = static_cast<f32>(m_RateFrames) / m_RateTime;
        m_RateFrames = 0;
        m_SpawnRate = static_cast<f32>(m_World.GetSpawnedTotal() - m_LastSpawned) / m_RateTime;
        m_DestroyRate =
            static_cast<f32>(m_World.GetDestroyedTotal() - m_LastDestroyed) / m_RateTime;
        m_LastSpawned = m_World.GetSpawnedTotal();
        m_LastDestroyed = m_World.GetDestroyedTotal();
        m_RateTime = 0.0f;
    }

    void DrawHud(Emerald::Renderer2D& r, Vec2 size)
    {
        if (!m_Shared.SmallFont)
            return;
        const Emerald::Font& font = *m_Shared.SmallFont;
        const Emerald::Vec4 white{1.0f, 1.0f, 1.0f, 1.0f};
        const Emerald::Vec4 grey{0.65f, 0.7f, 0.75f, 1.0f};
        const auto fmt = [](f64 v) {
            char text[32];
            std::snprintf(text, sizeof(text), "%.2f", v);
            return std::string(text);
        };
        const f32 x = size.x - 330.0f;
        r.FillRect({x - 10.0f, 10.0f}, {330.0f, 92.0f}, {0.0f, 0.02f, 0.04f, 0.75f});
        r.DrawString(font,
                     "ENTITIES " + std::to_string(m_World.GetCount()) + " / target " +
                         std::to_string(m_Target) + "  " + std::to_string(static_cast<i32>(m_Fps)) +
                         " fps",
                     {x, 20.0f}, white);
        r.DrawString(font,
                     "spawned " + std::to_string(static_cast<i32>(m_SpawnRate)) + "/s  destroyed " +
                         std::to_string(static_cast<i32>(m_DestroyRate)) + "/s",
                     {x, 34.0f}, grey);
        r.DrawString(font,
                     "contacts " + std::to_string(m_Collisions.GetEvents().size()) + "  pairs " +
                         std::to_string(m_Collisions.GetCandidateCount()),
                     {x, 48.0f}, grey);
        r.DrawString(font,
                     "ms move " + fmt(m_MoveMs) + " hit " + fmt(m_CollideMs) + "\n   anim " +
                         fmt(m_AnimateMs) + " life " + fmt(m_LifeMs) + " draw " + fmt(m_DrawMs),
                     {x, 62.0f}, grey);
        r.DrawString(font,
                     "Space burst  Backspace destroy half  Up/Down target  M pause  T platformer",
                     {12.0f, size.y - 20.0f}, {1.0f, 1.0f, 1.0f, 0.9f});
    }

    // The mouse in window units, if it is over the window.
    [[nodiscard]] static std::optional<Vec2> GetMouse()
    {
        if (!SDL_GetMouseFocus())
            return std::nullopt;
        Vec2 mouse;
        SDL_GetMouseState(&mouse.x, &mouse.y);
        return mouse;
    }

    // Runs fn and folds its time into `ms` (a moving average, so the numbers are readable).
    template <typename Fn> static void Time(f64& ms, Fn&& fn)
    {
        const auto start = std::chrono::steady_clock::now();
        fn();
        const f64 now =
            std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - start)
                .count();
        ms = ms * 0.95 + now * 0.05;
    }

    SandboxShared& m_Shared;
    Emerald::World m_World;                       // the scene's entities
    Emerald::CollisionSystem m_Collisions{24.0f}; // its broadphase
    std::mt19937 m_Rng{42};
    u32 m_Target = 3000;
    bool m_Collide = true;
    bool m_SortByY = true;
    u32 m_Drawn = 0;
    f32 m_SpawnBudget = 0.0f; // TopUp's: entities it may still spawn
    f32 m_RateTime = 0.0f;
    u32 m_RateFrames = 0;
    f32 m_Fps = 0.0f;
    u64 m_LastSpawned = 0;
    u64 m_LastDestroyed = 0;
    f32 m_SpawnRate = 0.0f;
    f32 m_DestroyRate = 0.0f;
    f64 m_MoveMs = 0.0;
    f64 m_CollideMs = 0.0;
    f64 m_AnimateMs = 0.0;
    f64 m_LifeMs = 0.0;
    f64 m_DrawMs = 0.0;
};
