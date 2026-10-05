#pragma once

// What the sandbox's scenes share: the app, the command-line options, the assets loaded once at
// startup (fonts, sprite sheets, the blip), a factory for the other scenes, the transitions and
// the hero's animation logic. The scenes: TitleScene.h, DemoScene.h, TilemapScene.h,
// SwarmScene.h, PlatformerScene.h, PauseScene.h and OptionsScene.h; main.cpp creates them
// (SandboxShared::Make) and runs the global parts.

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <Emerald/Emerald.h>

#include "TilemapRoom.h"

enum class SceneId : u8 { Title, Demo, Tilemap, Swarm, Platformer, Pause, Options };

// (--frames, --screenshot, --capture, --record and --replay are the engine's: DevOptions.h.)
struct SandboxOptions {
    // --demo / --tilemap / --swarm / --platformer start in that scene (no title, no fade); --map
    // <file.tmj> loads another map; --pan / --stats / --zoom <z> / --no-cull for the benchmark;
    // --collision /
    // --objects turn the room's overlays on.
    std::optional<SceneId> Start;
    TilemapRoom::Options Room;
    bool Collision = false;
    bool Objects = false;
    bool NoCull = false;
};

struct SandboxShared {
    SandboxShared(Emerald::Application& app, SandboxOptions options)
        : App(app), Options(std::move(options))
    {
    }

    Emerald::Application& App;
    SandboxOptions Options;
    Emerald::AssetHandle<Emerald::Font> PixelFont;                // 16 px, Nearest
    Emerald::AssetHandle<Emerald::Font> SmallFont;                // 8 px, Nearest
    Emerald::AssetHandle<Emerald::Font> SmoothFont;               // 40 px, Linear + oversampling
    Emerald::AssetHandle<Emerald::TextureAtlas> Hero;             // animated character + coins
    Emerald::AssetHandle<Emerald::Texture> Missing;               // no such file: the placeholder
    std::optional<Emerald::TextureAtlas> Atlas;                   // the generated gem / ring sheet
    std::optional<Emerald::Texture> White;                        // 1 x 1 white, for filled rects
    Emerald::Sound Blip;                                          // the jump's sound
    std::function<std::unique_ptr<Emerald::Scene>(SceneId)> Make; // set by main.cpp
    // The options screen's "Effects volume": scales the blips.
    f32 EffectsVolume = 1.0f;

    [[nodiscard]] bool HasFonts() const { return PixelFont && SmallFont && SmoothFont; }
    // Window size in window coordinates as floats: the pixel-space projections use it.
    [[nodiscard]] Emerald::Vec2 GetViewSize() const { return Emerald::Vec2(App.GetWindowSize()); }
    // The render target in pixels (differs from the view size on high-DPI displays).
    [[nodiscard]] Emerald::Vec2 GetTargetSize() const
    {
        return {static_cast<f32>(App.GetRenderer().GetFrameWidth()),
                static_cast<f32>(App.GetRenderer().GetFrameHeight())};
    }
};

// The sandbox's transitions: a plain fade, and a custom one (horizontal blinds closing from
// alternate sides) drawn by the Transition::Draw hook.
inline Emerald::Transition FadeBlack()
{
    return Emerald::Transition::Fade(0.35f);
}

inline Emerald::Transition Blinds(f32 seconds, Emerald::Vec3 color)
{
    Emerald::Transition t;
    t.Duration = seconds;
    t.Curve = Emerald::Easing::CubicInOut;
    t.Draw = [color](Emerald::Renderer2D& r, Emerald::Vec2 size, f32 amount) {
        constexpr i32 kBlinds = 8;
        const f32 height = size.y / static_cast<f32>(kBlinds);
        const f32 width = size.x * amount;
        for (i32 i = 0; i < kBlinds; ++i) {
            const f32 x = (i % 2 == 0) ? 0.0f : size.x - width; // even ones from the left
            r.FillRect({x, height * static_cast<f32>(i)}, {width, height + 1.0f},
                       {color.x, color.y, color.z, 1.0f});
        }
    };
    return t;
}

// The hero's animator: idle or walk at the run speed, and a "once" jump that flashes gold when
// it lands. Animations are looked up by name every update, so a hot-reloaded hero.json applies.
class HeroSprite {
public:
    explicit HeroSprite(const Emerald::AssetHandle<Emerald::TextureAtlas>& atlas) : m_Atlas(atlas)
    {
        if (!m_Atlas)
            return;
        m_Animator.Play(m_Atlas->GetAnimation("idle"));
        m_Animator.OnFinished = [this](const Emerald::Animation&) { // `this`: not copyable
            m_Jumping = false;
            m_Flash = 0.25f;
            ++m_JumpsLanded;
        };
    }
    HeroSprite(const HeroSprite&) = delete;
    HeroSprite& operator=(const HeroSprite&) = delete;

    void Jump()
    {
        if (!m_Atlas)
            return;
        m_Jumping = true;
        m_Animator.Play(m_Atlas->GetAnimation("jump"), true);
    }

    void Update(bool moving, f32 speed, f32 dt)
    {
        if (!m_Atlas)
            return;
        if (!m_Jumping)
            m_Animator.Play(m_Atlas->GetAnimation(moving ? "walk" : "idle"));
        m_Animator.SetSpeed(moving ? speed : 1.0f);
        m_Animator.Update(dt);
        m_Flash = Emerald::Max(m_Flash - dt, 0.0f);
    }

    [[nodiscard]] bool IsLoaded() const { return static_cast<bool>(m_Atlas); }
    [[nodiscard]] const Emerald::Animator& GetAnimator() const { return m_Animator; }
    // In the air during the jump's middle frame.
    [[nodiscard]] bool IsInAir() const { return m_Jumping && m_Animator.GetFrameIndex() == 1; }
    [[nodiscard]] Emerald::Vec4 GetTint() const
    {
        return m_Flash > 0.0f ? Emerald::Vec4(1.0f, 0.85f, 0.3f, 1.0f) : Emerald::Vec4(1.0f);
    }
    [[nodiscard]] u32 GetJumpsLanded() const { return m_JumpsLanded; }

private:
    const Emerald::AssetHandle<Emerald::TextureAtlas>& m_Atlas; // SandboxShared's, outlives this
    Emerald::Animator m_Animator;
    bool m_Jumping = false;
    f32 m_Flash = 0.0f; // seconds of gold tint left
    u32 m_JumpsLanded = 0;
};
