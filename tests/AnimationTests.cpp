// Sprite animation: atlas JSON "animations" parsing and frame lookup (missing frames reported),
// Animator timing at different dt, modes, speed, stop/resume and events, and drawing a frame with
// flip and tint. No GPU: the atlas texture is CreateWithoutGpu.

#include <algorithm>
#include <utility>
#include <vector>

#include <Emerald/Core/Log.h>
#include <Emerald/Renderer/Animation.h>
#include <Emerald/Renderer/Renderer2D.h>
#include <Emerald/Renderer/Texture.h>
#include <Emerald/Renderer/TextureAtlas.h>

#include "Test.h"

using namespace Emerald;

namespace {

constexpr const char* kJson = R"({
  "walk_0": { "x": 0,  "y": 0, "w": 16, "h": 16 },
  "walk_1": { "x": 16, "y": 0, "w": 16, "h": 16 },
  "walk_2": { "x": 32, "y": 0, "w": 16, "h": 16 },
  "idle":   { "x": 48, "y": 0, "w": 16, "h": 16 },
  "animations": {
    "walk":   { "pattern": "walk_{}", "duration": 0.1 },
    "list":   { "frames": ["idle", "walk_0"], "durations": [0.5, 0.25], "mode": "once" },
    "bounce": { "pattern": "walk_{}", "mode": "pingpong", "duration": 0.2 },
    "holes":  { "frames": ["walk_0", "nope", "walk_2"] },
    "range":  { "pattern": "walk_{}", "from": 1, "to": 4 },
    "ghost":  { "frames": ["missing"] },
    "plain":  ["walk_2", "walk_1"],
    "broken": { "duration": 1.0 },
    "wrong":  { "frames": ["idle", "walk_0"], "durations": [0.5] }
  }
})";

TextureAtlas MakeAtlas()
{
    std::optional<TextureAtlas::RegionMap> regions = TextureAtlas::ParseRegions(kJson);
    return TextureAtlas::Create(Texture::CreateWithoutGpu(64, 16), std::move(*regions),
                                TextureAtlas::ParseAnimations(kJson));
}

// Three frames of 0.1, 0.15 and 0.2 s.
Animation MakeUneven(AnimationMode mode)
{
    Animation a{.Name = "uneven", .Frames = {}, .Mode = mode};
    for (f32 d : {0.1f, 0.15f, 0.2f})
        a.Frames.push_back({.Image = {}, .Duration = d});
    return a;
}

// Plays `a` for `seconds`, split into `steps` equal updates.
Animator Run(const Animation& a, f32 seconds, u32 steps, f32 speed = 1.0f)
{
    Animator animator;
    animator.SetSpeed(speed);
    animator.Play(a);
    for (u32 i = 0; i < steps; ++i)
        animator.Update(seconds / static_cast<f32>(steps));
    return animator;
}

bool Has(const std::vector<std::string>& list, const std::string& item)
{
    return std::find(list.begin(), list.end(), item) != list.end();
}

} // namespace

TEST(AnimationAtlasParse)
{
    Log::Init({}); // console only; the atlas logs the problems below
    // "animations" is not a sprite.
    const auto regions = TextureAtlas::ParseRegions(kJson);
    CHECK(regions && regions->size() == 4 && !regions->contains("animations"));
    CHECK(TextureAtlas::ParseAnimations(R"({"a": {"x": 0, "y": 0, "w": 1, "h": 1}})").empty());
    CHECK(TextureAtlas::ParseAnimations("{ not json").empty());

    const TextureAtlas atlas = MakeAtlas();
    const Animation* walk = atlas.FindAnimation("walk");
    CHECK(walk && walk->Frames.size() == 3 && walk->Mode == AnimationMode::Loop);
    CHECK(walk && walk->Frames[2].Image.Region.Position == Vec2(32.0f, 0.0f));
    CHECK(walk && walk->Frames[0].Image.Source == &atlas.GetTexture());
    CHECK(walk && NearlyEqual(walk->Frames[1].Duration, 0.1f));

    const Animation* list = atlas.FindAnimation("list");
    CHECK(list && list->Frames.size() == 2 && list->Mode == AnimationMode::Once);
    CHECK(list && NearlyEqual(list->Frames[0].Duration, 0.5f) &&
          NearlyEqual(list->Frames[1].Duration, 0.25f));
    const Animation* bounce = atlas.FindAnimation("bounce");
    CHECK(bounce && bounce->Mode == AnimationMode::PingPong);
    const Animation* plain = atlas.FindAnimation("plain"); // array form
    CHECK(plain && plain->Frames.size() == 2 && plain->Frames[0].Image.Region.Position.x == 32.0f);
    // A duration count that does not match falls back to "duration".
    const Animation* wrong = atlas.FindAnimation("wrong");
    CHECK(wrong && NearlyEqual(wrong->Frames[0].Duration, 0.1f));

    // Missing frames are reported and left out.
    const Animation* holes = atlas.FindAnimation("holes");
    CHECK(holes && holes->Frames.size() == 2);
    const Animation* range = atlas.FindAnimation("range");
    CHECK(range && range->Frames.size() == 2); // walk_3, walk_4 missing
    CHECK(!atlas.FindAnimation("ghost"));      // no frames left
    CHECK(!atlas.FindAnimation("broken"));     // neither frames nor pattern
    const std::vector<std::string>& missing = atlas.GetMissingFrames();
    CHECK(missing.size() == 4);
    CHECK(Has(missing, "holes: nope") && Has(missing, "range: walk_3") &&
          Has(missing, "range: walk_4") && Has(missing, "ghost: missing"));

    // Unknown names give an empty animation, which plays and draws nothing.
    Animator animator;
    animator.Play(atlas.GetAnimation("nope"));
    animator.Update(1.0f);
    CHECK(!animator.IsPlaying() && animator.GetSprite().Source == nullptr);
}

TEST(AnimatorLoopTimingAtAnyDt)
{
    const Animation a = MakeUneven(AnimationMode::Loop);
    CHECK_NEAR(a.GetCycleDuration(), 0.45f);
    // 1.03 s = 2 cycles + 0.13 s: frame 1, 0.03 s into it, whatever the step size.
    for (u32 steps : {1u, 7u, 31u, 103u, 1030u}) {
        const Animator animator = Run(a, 1.03f, steps);
        CHECK(animator.GetFrameIndex() == 1);
        CHECK(animator.GetLoopCount() == 2);
        CHECK_NEAR_EPS(animator.GetFrameTime(), 0.03f, 1e-4f);
        CHECK(animator.IsPlaying() && !animator.IsFinished());
    }
}

TEST(AnimatorOnceAndEvents)
{
    const Animation a = MakeUneven(AnimationMode::Once);
    for (u32 steps : {1u, 9u, 120u}) {
        Animator animator;
        u32 finished = 0;
        u32 loops = 0;
        animator.OnFinished = [&](const Animation& done) {
            CHECK(&done == &a);
            ++finished;
        };
        animator.OnLoop = [&](const Animation&) { ++loops; };
        animator.Play(a);
        for (u32 i = 0; i < steps; ++i)
            animator.Update(0.44f / static_cast<f32>(steps));
        CHECK(animator.GetFrameIndex() == 2 && finished == 0); // on the last frame, not done
        for (u32 i = 0; i < steps; ++i)
            animator.Update(1.0f / static_cast<f32>(steps));
        CHECK(animator.IsFinished() && !animator.IsPlaying());
        CHECK(animator.GetFrameIndex() == 2); // holds the last frame
        CHECK(finished == 1 && loops == 0);   // exactly once
        animator.Update(1.0f);
        animator.Play(a); // same animation, no restart: stays finished
        CHECK(animator.IsFinished() && finished == 1);
        animator.Play(a, true);
        CHECK(animator.IsPlaying() && animator.GetFrameIndex() == 0);
    }
}

TEST(AnimatorPingPong)
{
    const Animation a = MakeUneven(AnimationMode::PingPong);
    // 0 1 2 1 | 0 ...: a cycle is 0.1 + 0.15 + 0.2 + 0.15 = 0.6 s.
    CHECK_NEAR(a.GetCycleDuration(), 0.6f);
    const u32 expected[] = {0, 1, 2, 1, 0, 1};
    const f32 times[] = {0.05f, 0.2f, 0.4f, 0.55f, 0.65f, 0.75f};
    for (usize i = 0; i < 6; ++i) {
        for (u32 steps : {1u, 13u, 200u}) {
            const Animator animator = Run(a, times[i], steps);
            CHECK(animator.GetFrameIndex() == expected[i]);
            CHECK(animator.GetLoopCount() == (times[i] > 0.6f ? 1u : 0u));
        }
    }
    // One frame: a cycle per frame duration.
    Animation single{.Name = "one", .Frames = {{.Image = {}, .Duration = 0.1f}}};
    single.Mode = AnimationMode::PingPong;
    CHECK(Run(single, 0.35f, 7).GetLoopCount() == 3);
}

TEST(AnimatorSpeedStopResume)
{
    const Animation a = MakeUneven(AnimationMode::Loop);
    // Speed 2 for 0.515 s = 1.03 s of animation.
    const Animator fast = Run(a, 0.515f, 50, 2.0f);
    CHECK(fast.GetFrameIndex() == 1 && fast.GetLoopCount() == 2);
    CHECK(Run(a, 5.0f, 10, 0.0f).GetFrameIndex() == 0); // speed 0 holds
    Animator slow;
    slow.SetSpeed(-1.0f); // clamped to 0
    CHECK(slow.GetSpeed() == 0.0f);

    Animator animator;
    animator.Play(a);
    animator.Update(0.12f); // frame 1
    animator.Stop();
    animator.Update(10.0f);
    CHECK(!animator.IsPlaying() && animator.GetFrameIndex() == 1);
    animator.Resume();
    animator.Update(0.14f); // 0.02 + 0.14 > 0.15: frame 2
    CHECK(animator.IsPlaying() && animator.GetFrameIndex() == 2);
    animator.Play(a); // the current animation: no restart
    CHECK(animator.GetFrameIndex() == 2);

    // A zero duration cannot stall Update.
    const Animation zero{.Name = "zero", .Frames = {{.Image = {}, .Duration = 0.0f}}};
    CHECK(Run(zero, 0.01f, 1).GetLoopCount() > 0);
}

TEST(AnimatorDrawFlipTint)
{
    const TextureAtlas atlas = MakeAtlas();
    Animator animator;
    Renderer2D r;
    r.Begin(Mat4::Identity());
    r.DrawSprite(animator, {0.0f, 0.0f}); // before Play: nothing
    CHECK(r.GetSpriteCount() == 0);

    animator.Play(atlas.GetAnimation("walk"));
    animator.Update(0.15f); // frame 1: x 16..32 of 64
    const Vec4 tint{1.0f, 0.0f, 0.0f, 0.5f};
    r.DrawSprite(animator, {8.0f, 8.0f}, {.Tint = tint, .FlipX = true, .FlipY = true});
    r.End();
    const auto v = r.GetSpriteVertices();
    CHECK(v.size() == 6);
    // The top-left corner samples the region's bottom-right (both flips).
    CHECK_NEAR(v[0].Position, Vec2(0.0f, 0.0f));
    CHECK_NEAR(v[0].TexCoord, Vec2(0.5f, 1.0f));
    CHECK(v[0].Color == Renderer2D::PackColor(tint));
}
