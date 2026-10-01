#pragma once

#include <functional>
#include <string>
#include <vector>

#include "Emerald/Core/Defines.h"
#include "Emerald/Renderer/Sprite.h"

namespace Emerald {

// What happens at the last frame.
enum class AnimationMode : u8 {
    Loop,     // 0 1 2 0 1 2 ...
    Once,     // 0 1 2, then stays on 2 (finished)
    PingPong, // 0 1 2 1 0 1 2 ...
};

struct AnimationFrame {
    Sprite Image;
    f32 Duration = 0.1f; // seconds
};

// A named frame sequence, usually from a TextureAtlas's JSON (see TextureAtlas.h).
struct Animation {
    std::string Name;
    std::vector<AnimationFrame> Frames;
    AnimationMode Mode = AnimationMode::Loop;

    // One pass through the frames (for PingPong, there and back without repeating the ends).
    [[nodiscard]] f32 GetCycleDuration() const;
};

// Plays an Animation and says which frame to draw. Advance it with the game's dt:
//
//   m_Animator.Play(atlas.GetAnimation(moving ? "walk" : "idle")); // no restart if already on it
//   m_Animator.Update(dt);
//   r.DrawSprite(m_Animator, position, {.Tint = flash, .FlipX = facingLeft});
//
// Time carries over between frames, so the result depends only on the total time, not on how it
// was split into dt steps. The animation must outlive the animator (atlas animations live as long
// as the atlas).
class Animator {
public:
    // Starts `animation` from its first frame, unless it is already the current one and
    // `restart` is false (so calling Play every update is fine).
    void Play(const Animation& animation, bool restart = false);
    // Halts on the current frame; Resume continues from there.
    void Stop() { m_Playing = false; }
    void Resume() { m_Playing = m_Animation && !m_Finished; }
    void SetSpeed(f32 speed) { m_Speed = speed > 0.0f ? speed : 0.0f; } // 1 = as authored
    [[nodiscard]] f32 GetSpeed() const { return m_Speed; }

    void Update(f32 dt);

    [[nodiscard]] bool IsPlaying() const { return m_Playing; }
    // A Once animation reached its last frame (Loop / PingPong never finish).
    [[nodiscard]] bool IsFinished() const { return m_Finished; }
    [[nodiscard]] const Animation* GetAnimation() const { return m_Animation; }
    [[nodiscard]] u32 GetFrameIndex() const { return m_Frame; }
    [[nodiscard]] f32 GetFrameTime() const { return m_FrameTime; } // seconds into the frame
    [[nodiscard]] u32 GetLoopCount() const { return m_Loops; }     // completed cycles
    // The frame to draw; an empty Sprite (no texture) before the first Play.
    [[nodiscard]] Sprite GetSprite() const;

    // Events, called from Update: a Once animation finished / a Loop or PingPong cycle ended.
    std::function<void(const Animation&)> OnFinished;
    std::function<void(const Animation&)> OnLoop;

private:
    void Advance(); // to the next frame, by the animation's mode

    const Animation* m_Animation = nullptr;
    u32 m_Frame = 0;
    f32 m_FrameTime = 0.0f;
    f32 m_Speed = 1.0f;
    u32 m_Loops = 0;
    bool m_Backwards = false; // PingPong on its way back
    bool m_Playing = false;
    bool m_Finished = false;
};

} // namespace Emerald
