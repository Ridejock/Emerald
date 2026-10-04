#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <variant>
#include <vector>

#include "Emerald/Core/Defines.h"
#include "Emerald/Math/Vec2.h"
#include "Emerald/Math/Vec4.h"
#include "Emerald/Tween/Easing.h"

namespace Emerald {

// Names one tween. Ids are never reused, so an id for a tween that has finished or was cancelled
// stays harmless: Cancel and IsActive just say it is gone. A default id (0) names nothing.
struct TweenId {
    u32 Value = 0;

    explicit operator bool() const { return Value != 0; }
    bool operator==(const TweenId&) const = default;
};

struct TweenOptions {
    Easing Curve = Easing::QuadOut;
    f32 Delay = 0.0f; // seconds of waiting before the first play
    // Extra plays after the first one: 0 plays once, 2 plays three times, kForever never stops.
    i32 Repeat = 0;
    // Each repeat plays the previous one backwards (there and back), instead of starting over.
    bool Yoyo = false;
    // Chaining: wait for this tween to complete first (its delay starts after that). Cancelling
    // it also cancels this one.
    TweenId After{};
    // Called once, when the last play ends; not called when the tween is cancelled (or forever).
    std::function<void()> OnComplete{};
};

// One tween's value: what it writes and between which values (used inside Tweens). At namespace
// scope rather than nested in Tweens: Clang cannot use a nested struct's default member
// initializers in Tweens' std::variant before Tweens is complete.
template <typename T> struct TweenTrack {
    T* Target = nullptr;
    T From{};
    T To{};
    bool HasFrom = false; // FromTo; otherwise From is read from the target at the start
};

// Animates values over time: moves a value from where it is to a target with an easing curve.
// The game owns a Tweens and advances it with its update dt, so pausing the game pauses them:
//
//   m_Title.y = -40.0f;
//   m_Tweens.To(&m_Title.y, 120.0f, 0.8f, {.Curve = Easing::BounceOut});
//   const TweenId fade = m_Tweens.To(&m_Alpha, 1.0f, 0.3f, {.Delay = 0.5f});
//   m_Tweens.To(&m_Alpha, 0.0f, 0.3f, {.Delay = 2.0f, .After = fade});    // then fade out
//   m_Tweens.To(&m_Color, red, 0.2f, {.Repeat = 5, .Yoyo = true});       // blink 3 times
//   m_Tweens.Update(dt); // every frame
//
// Lifetime: a tween writes through its target pointer on every Update until it completes or is
// cancelled, so the target must stay where it is until then. Before destroying (or moving) an
// animated value, cancel its tweens: CancelTarget(&value) or Cancel(id), e.g. in the owner's
// destructor. (A pointer into a std::vector that grows is the classic trap.) When the value lives
// somewhere that can move, use Run instead and look it up in the callback.
//
// Callbacks may start, cancel or chain tweens (new ones first update on the next Update).
class Tweens {
public:
    static constexpr i32 kForever = -1;

    // From the target's current value (read when the tween starts, after its delay) to `to`.
    TweenId To(f32* target, f32 to, f32 duration, TweenOptions options = {});
    TweenId To(Vec2* target, Vec2 to, f32 duration, TweenOptions options = {});
    TweenId To(Vec4* target, Vec4 to, f32 duration, TweenOptions options = {}); // e.g. a color

    // From `from` to `to`; the target jumps to `from` when the tween starts.
    TweenId FromTo(f32* target, f32 from, f32 to, f32 duration, TweenOptions options = {});
    TweenId FromTo(Vec2* target, Vec2 from, Vec2 to, f32 duration, TweenOptions options = {});
    TweenId FromTo(Vec4* target, Vec4 from, Vec4 to, f32 duration, TweenOptions options = {});

    // No target: calls `apply` every update with the eased progress (0 to 1, or past it for
    // Back / Elastic). Use it for anything else, or to avoid holding a pointer.
    TweenId Run(f32 duration, std::function<void(f32 eased)> apply, TweenOptions options = {});

    // Stops a tween where it is (no OnComplete), and every tween chained After it. Returns false
    // if it was not running (already done, cancelled, or never existed).
    bool Cancel(TweenId id);
    // Cancels every tween writing to `target` (and what is chained after them).
    void CancelTarget(const void* target);
    void Clear(); // cancels everything

    // Waiting (delay or chain) or playing.
    [[nodiscard]] bool IsActive(TweenId id) const;
    [[nodiscard]] usize GetCount() const;

    // Advances every tween by dt seconds. Time left over at the end of a play carries into the
    // next one (and into a chained tween), so the result only depends on the total time.
    void Update(f32 dt);

private:
    template <typename T> using Track = TweenTrack<T>;
    using RunCallback = std::function<void(f32)>;
    using AnyTrack = std::variant<Track<f32>, Track<Vec2>, Track<Vec4>, RunCallback>;

    struct Tween {
        TweenId Id;
        AnyTrack Value;
        f32 Duration = 0.0f;
        TweenOptions Options;
        f32 Time = 0.0f;        // seconds into the delay, then into the current play
        bool Started = false;   // the delay is over
        bool Backwards = false; // yoyo, on the way back
        i32 PlaysLeft = 0;      // repeats still to go (kForever: endless)
        bool Done = false;      // completed or cancelled; removed at the end of Update
    };

    TweenId Add(AnyTrack value, f32 duration, TweenOptions options);
    // Advances one tween; returns the time left over if it completed.
    std::optional<f32> Advance(Tween& tween, f32 dt);
    static void Start(Tween& tween);
    static void Apply(Tween& tween, f32 progress); // progress through the current play, 0 to 1
    static void ApplyEnd(Tween& tween);            // the exact end value
    void MarkCancelled(TweenId id);                // Cancel without removing it from the list yet
    Tween* Find(TweenId id);
    [[nodiscard]] const Tween* Find(TweenId id) const;
    void RemoveDone();

    // Boxed, so a tween stays put while its callback starts new ones (the vector may reallocate).
    std::vector<std::unique_ptr<Tween>> m_Tweens;
    // Tweens that completed during the current Update, with their leftover time (for chains).
    std::vector<std::pair<TweenId, f32>> m_CompletedThisUpdate;
    u32 m_NextId = 1;
    bool m_Updating = false;
};

} // namespace Emerald
