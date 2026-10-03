// Easing curves at 0, 0.5 and 1 (and their symmetry), tweens (delay, repeat, yoyo, chaining,
// cancelling, callbacks firing once) and timers, with results checked to be the same whether time
// comes in one big step or many uneven ones.

#include <algorithm>
#include <iterator>
#include <string>
#include <vector>

#include <Emerald/Tween/Easing.h>
#include <Emerald/Tween/Timers.h>
#include <Emerald/Tween/Tween.h>

#include "Test.h"

using namespace Emerald;

namespace {

// Uneven frame times (deterministic): 1 to 50 ms.
class FrameTimes {
public:
    f32 Next()
    {
        m_State = m_State * 1664525u + 1013904223u;
        return 0.001f + 0.049f * static_cast<f32>(m_State >> 8) / 16777216.0f;
    }

private:
    u32 m_State = 12345u;
};

struct Expected {
    Easing Curve;
    f32 Half; // the value at t = 0.5
};

} // namespace

// ---------------------------------------------------------------------------------------------
// Easing

TEST(EasingValuesAtZeroHalfAndOne)
{
    // Worked out by hand from the formulas (see easings.net).
    const Expected expected[] = {
        {Easing::Linear, 0.5f},          {Easing::QuadIn, 0.25f},
        {Easing::QuadOut, 0.75f},        {Easing::QuadInOut, 0.5f},
        {Easing::CubicIn, 0.125f},       {Easing::CubicOut, 0.875f},
        {Easing::CubicInOut, 0.5f},      {Easing::BackIn, -0.0876975f},
        {Easing::BackOut, 1.0876975f},   {Easing::BackInOut, 0.5f},
        {Easing::ElasticIn, -0.015625f}, {Easing::ElasticOut, 1.015625f},
        {Easing::ElasticInOut, 0.5f},    {Easing::BounceIn, 0.234375f},
        {Easing::BounceOut, 0.765625f},  {Easing::BounceInOut, 0.5f},
    };
    CHECK(std::size(expected) == kEasingCount);
    for (const Expected& e : expected) {
        CHECK(Ease(e.Curve, 0.0f) == 0.0f); // exact ends
        CHECK(Ease(e.Curve, 1.0f) == 1.0f);
        CHECK_NEAR_EPS(Ease(e.Curve, 0.5f), e.Half, 1e-5f);
    }
}

TEST(EasingOutMirrorsInAndInOutIsSymmetric)
{
    for (usize family = 1; family < kEasingCount; family += 3) {
        const Easing in = kAllEasings[family];
        const Easing out = kAllEasings[family + 1];
        const Easing inOut = kAllEasings[family + 2];
        for (f32 t = 0.0f; t <= 1.0f; t += 0.05f) {
            CHECK_NEAR_EPS(Ease(out, t), 1.0f - Ease(in, 1.0f - t), 1e-5f);
            CHECK_NEAR_EPS(Ease(inOut, t), 1.0f - Ease(inOut, 1.0f - t), 1e-5f);
        }
        // The two halves of InOut meet in the middle.
        CHECK_NEAR_EPS(Ease(inOut, 0.4999f), Ease(inOut, 0.5001f), 1e-2f);
    }
}

TEST(EasingClampsAndHasNames)
{
    for (const Easing easing : kAllEasings) {
        CHECK(Ease(easing, -1.0f) == 0.0f);
        CHECK(Ease(easing, 2.0f) == 1.0f);
        CHECK(std::string(GetEasingName(easing)) != "?");
    }
    CHECK(std::string(GetEasingName(Easing::BounceInOut)) == "BounceInOut");
    // Back and Elastic overshoot; the others stay within [0, 1].
    CHECK(Ease(Easing::BackOut, 0.7f) > 1.0f);
    CHECK(Ease(Easing::QuadOut, 0.7f) < 1.0f);
}

// ---------------------------------------------------------------------------------------------
// Tweens

TEST(TweenMovesAValueAndEndsExactly)
{
    Tweens tweens;
    f32 x = 0.0f;
    const TweenId id = tweens.To(&x, 10.0f, 1.0f, {.Curve = Easing::Linear});
    CHECK(id && tweens.IsActive(id) && tweens.GetCount() == 1);
    tweens.Update(0.5f);
    CHECK_NEAR(x, 5.0f);
    tweens.Update(0.7f);
    CHECK(x == 10.0f);
    CHECK(!tweens.IsActive(id) && tweens.GetCount() == 0);
    tweens.Update(1.0f); // nothing left to touch x
    CHECK(x == 10.0f);

    f32 y = 0.0f;
    tweens.To(&y, 10.0f, 1.0f, {.Curve = Easing::QuadIn});
    tweens.Update(0.5f);
    CHECK_NEAR(y, 2.5f);
}

TEST(TweenDelayAndStartValue)
{
    Tweens tweens;
    f32 x = 1.0f;
    tweens.To(&x, 5.0f, 1.0f, {.Curve = Easing::Linear, .Delay = 0.5f});
    tweens.Update(0.25f);
    CHECK(x == 1.0f);    // still waiting
    x = 3.0f;            // moved during the delay: To starts from wherever it is when it starts
    tweens.Update(0.5f); // 0.25 s into the tween
    CHECK_NEAR(x, 3.5f);

    // FromTo jumps to `from` when it starts (and not before).
    f32 y = 9.0f;
    tweens.FromTo(&y, 0.0f, 1.0f, 1.0f, {.Curve = Easing::Linear, .Delay = 1.0f});
    tweens.Update(0.5f);
    CHECK(y == 9.0f);
    tweens.Update(0.5f);
    CHECK(y == 0.0f);
}

TEST(TweenRepeatAndYoyo)
{
    Tweens tweens;
    f32 x = 0.0f;
    i32 completed = 0;
    // Three plays: there, back, there.
    tweens.To(
        &x, 1.0f, 1.0f,
        {.Curve = Easing::Linear, .Repeat = 2, .Yoyo = true, .OnComplete = [&] { ++completed; }});
    tweens.Update(0.5f);
    CHECK_NEAR(x, 0.5f);
    tweens.Update(0.75f); // 1.25: on the way back
    CHECK_NEAR(x, 0.75f);
    tweens.Update(1.0f); // 2.25: there again
    CHECK_NEAR(x, 0.25f);
    CHECK(completed == 0);
    tweens.Update(1.0f);
    CHECK(x == 1.0f && completed == 1);

    // An even number of yoyo plays ends exactly where it started.
    f32 y = 2.0f;
    tweens.To(&y, 4.0f, 0.3f, {.Repeat = 1, .Yoyo = true});
    tweens.Update(0.65f);
    CHECK(y == 2.0f && tweens.GetCount() == 0);

    // Without yoyo each repeat starts over from the start value.
    f32 z = 0.0f;
    tweens.To(&z, 1.0f, 1.0f, {.Curve = Easing::Linear, .Repeat = 1});
    tweens.Update(1.25f);
    CHECK_NEAR(z, 0.25f);
}

TEST(TweenForeverRunsUntilCancelled)
{
    Tweens tweens;
    f32 x = 0.0f;
    bool completed = false;
    const TweenId id = tweens.To(
        &x, 1.0f, 0.2f,
        {.Repeat = Tweens::kForever, .Yoyo = true, .OnComplete = [&] { completed = true; }});
    for (i32 i = 0; i < 1000; ++i)
        tweens.Update(0.1f);
    CHECK(tweens.IsActive(id));
    CHECK(tweens.Cancel(id));
    CHECK(!tweens.IsActive(id) && !completed);
    const f32 stoppedAt = x;
    tweens.Update(0.05f);
    CHECK(x == stoppedAt);     // cancelled: left where it was
    CHECK(!tweens.Cancel(id)); // already gone
}

TEST(TweenResultDoesNotDependOnFrameTimes)
{
    // The same tween advanced by one big step and by many uneven ones ends up in the same place,
    // across the delay, the repeats and the chain into a second tween.
    const auto make = [](Tweens& tweens, Vec2& value) {
        const TweenId first =
            tweens.To(&value, {100.0f, 50.0f}, 0.7f,
                      {.Curve = Easing::BackOut, .Delay = 0.3f, .Repeat = 3, .Yoyo = true});
        tweens.To(&value, {-20.0f, 10.0f}, 0.4f, {.Curve = Easing::ElasticOut, .After = first});
    };
    for (const f32 total : {0.2f, 0.65f, 1.9f, 3.05f, 3.3f}) {
        Tweens stepped;
        Vec2 a{0.0f, 0.0f};
        make(stepped, a);
        FrameTimes frames;
        f32 time = 0.0f;
        while (time < total) {
            const f32 dt = std::min(frames.Next(), total - time);
            stepped.Update(dt);
            time += dt;
        }

        Tweens once;
        Vec2 b{0.0f, 0.0f};
        make(once, b);
        once.Update(time);
        CHECK_NEAR_EPS(a, b, 1e-2f);
    }
}

TEST(TweenChainsStartWhenThePreviousCompletes)
{
    Tweens tweens;
    f32 x = 0.0f;
    std::vector<i32> order;
    const TweenId first = tweens.To(
        &x, 1.0f, 1.0f, {.Curve = Easing::Linear, .OnComplete = [&] { order.push_back(1); }});
    const TweenId second = tweens.To(
        &x, 3.0f, 1.0f,
        {.Curve = Easing::Linear, .After = first, .OnComplete = [&] { order.push_back(2); }});
    tweens.Update(0.5f);
    CHECK_NEAR(x, 0.5f); // only the first one is moving
    CHECK(tweens.IsActive(second));
    tweens.Update(1.0f); // the first ends at 1.0, the second gets the remaining 0.5 s
    CHECK_NEAR(x, 2.0f);
    tweens.Update(1.0f);
    CHECK(x == 3.0f);
    CHECK(order == std::vector<i32>({1, 2}));

    // Chained after something already finished: starts right away.
    f32 y = 0.0f;
    tweens.To(&y, 1.0f, 1.0f, {.Curve = Easing::Linear, .After = first});
    tweens.Update(0.5f);
    CHECK_NEAR(y, 0.5f);
}

TEST(TweenCancellingCancelsTheChain)
{
    Tweens tweens;
    f32 x = 0.0f;
    i32 callbacks = 0;
    const TweenId a = tweens.To(&x, 1.0f, 1.0f, {.OnComplete = [&] { ++callbacks; }});
    const TweenId b = tweens.To(&x, 2.0f, 1.0f, {.After = a, .OnComplete = [&] { ++callbacks; }});
    const TweenId c = tweens.To(&x, 3.0f, 1.0f, {.After = b, .OnComplete = [&] { ++callbacks; }});
    f32 other = 0.0f;
    tweens.To(&other, 1.0f, 1.0f);
    tweens.Update(0.5f);
    CHECK(tweens.Cancel(a));
    CHECK(!tweens.IsActive(b) && !tweens.IsActive(c));
    CHECK(tweens.GetCount() == 1); // the unrelated one
    tweens.Update(5.0f);
    CHECK(callbacks == 0);
}

TEST(TweenCompletionFiresExactlyOnce)
{
    Tweens tweens;
    f32 x = 0.0f;
    i32 fired = 0;
    TweenId self;
    self = tweens.To(&x, 1.0f, 0.1f, {.OnComplete = [&] {
        ++fired;
        CHECK(!tweens.Cancel(self)); // already done
    }});
    for (i32 i = 0; i < 10; ++i)
        tweens.Update(0.1f);
    CHECK(fired == 1);

    // A zero-length tween completes on its first Update.
    f32 y = 0.0f;
    tweens.To(&y, 7.0f, 0.0f, {.OnComplete = [&] { ++fired; }});
    tweens.Update(0.0f);
    CHECK(y == 7.0f && fired == 2);
    tweens.Update(1.0f);
    CHECK(fired == 2);
}

TEST(TweenCallbacksCanStartAndClearTweens)
{
    Tweens tweens;
    f32 x = 0.0f;
    f32 y = 0.0f;
    tweens.To(&x, 1.0f, 0.5f, {.OnComplete = [&] {
        // Starts on the next Update, not during this one.
        tweens.To(&y, 1.0f, 1.0f, {.Curve = Easing::Linear});
    }});
    tweens.Update(1.0f);
    CHECK(x == 1.0f && y == 0.0f && tweens.GetCount() == 1);
    tweens.Update(0.5f);
    CHECK_NEAR(y, 0.5f);

    // Clear from inside a callback stops everything, including tweens later in the list.
    f32 z = 0.0f;
    tweens.Clear();
    tweens.To(&x, 0.0f, 0.1f, {.OnComplete = [&] { tweens.Clear(); }});
    tweens.To(&z, 5.0f, 1.0f, {.Delay = 0.5f});
    tweens.Update(1.0f);
    CHECK(z == 0.0f && tweens.GetCount() == 0);
}

TEST(TweenCancelTargetBeforeTheValueGoesAway)
{
    // The pattern for values that die before the Tweens: cancel in the destructor.
    struct Button {
        Tweens& Owner;
        Vec2 Position{0.0f, 0.0f};
        Vec4 Color{1.0f, 1.0f, 1.0f, 1.0f};
        ~Button()
        {
            Owner.CancelTarget(&Position);
            Owner.CancelTarget(&Color);
        }
    };
    Tweens tweens;
    f32 keep = 0.0f;
    tweens.To(&keep, 1.0f, 1.0f);
    {
        Button button{tweens};
        tweens.To(&button.Position, {10.0f, 20.0f}, 1.0f, {.Curve = Easing::Linear});
        tweens.To(&button.Color, {0.0f, 0.0f, 0.0f, 0.0f}, 1.0f, {.Curve = Easing::Linear});
        tweens.Update(0.5f);
        CHECK_NEAR(button.Position, Vec2(5.0f, 10.0f));
        CHECK_NEAR(button.Color, Vec4(0.5f, 0.5f, 0.5f, 0.5f));
        CHECK(tweens.GetCount() == 3);
    }
    CHECK(tweens.GetCount() == 1); // only `keep` is left
    tweens.Update(1.0f);           // and nothing writes to the dead button
    CHECK(keep == 1.0f);
}

TEST(TweenRunCallsBackWithEasedProgress)
{
    Tweens tweens;
    std::vector<f32> seen;
    tweens.Run(1.0f, [&](f32 eased) { seen.push_back(eased); }, {.Curve = Easing::QuadIn});
    tweens.Update(0.5f);
    tweens.Update(0.5f);
    CHECK(seen.size() == 2);
    CHECK_NEAR(seen[0], 0.25f);
    CHECK(seen[1] == 1.0f);
    // Run tweens have no target, so CancelTarget leaves them alone.
    tweens.Run(1.0f, [](f32) {});
    tweens.CancelTarget(nullptr);
    CHECK(tweens.GetCount() == 1);
}

TEST(TweenStaleIdsAreHarmless)
{
    Tweens tweens;
    CHECK(!TweenId{});
    CHECK(!tweens.Cancel(TweenId{}) && !tweens.IsActive(TweenId{}));
    f32 x = 0.0f;
    const TweenId old = tweens.To(&x, 1.0f, 0.1f);
    tweens.Update(1.0f);
    const TweenId fresh = tweens.To(&x, 2.0f, 1.0f);
    CHECK(!(old == fresh)); // ids are not reused
    CHECK(!tweens.Cancel(old));
    CHECK(tweens.IsActive(fresh));
}

// ---------------------------------------------------------------------------------------------
// Timers

TEST(TimerAfterFiresOnce)
{
    Timers timers;
    i32 fired = 0;
    const TimerId id = timers.After(1.0f, [&] { ++fired; });
    timers.Update(0.6f);
    CHECK(fired == 0 && timers.IsActive(id));
    CHECK_NEAR(timers.GetTimeLeft(id), 0.4f);
    timers.Update(0.6f);
    CHECK(fired == 1 && !timers.IsActive(id));
    timers.Update(5.0f);
    CHECK(fired == 1 && timers.GetCount() == 0);
    CHECK(timers.GetTimeLeft(id) == 0.0f);
}

TEST(TimerEveryIsFrameRateIndependent)
{
    // Ten deadlines in 1.05 s, whether the time comes in uneven frames or all at once.
    Timers stepped;
    i32 steppedCount = 0;
    const TimerId a = stepped.Every(0.1f, [&] { ++steppedCount; });
    FrameTimes frames;
    f32 time = 0.0f;
    while (time < 1.05f) {
        const f32 dt = std::min(frames.Next(), 1.05f - time);
        stepped.Update(dt);
        time += dt;
    }

    Timers once;
    i32 onceCount = 0;
    const TimerId b = once.Every(0.1f, [&] { ++onceCount; });
    once.Update(1.05f);

    CHECK(steppedCount == 10 && onceCount == 10);
    CHECK_NEAR_EPS(stepped.GetTimeLeft(a), 0.05f, 1e-4f);
    CHECK_NEAR_EPS(once.GetTimeLeft(b), 0.05f, 1e-4f);
}

TEST(TimerCancel)
{
    Timers timers;
    i32 fired = 0;
    const TimerId id = timers.Every(0.1f, [&] { ++fired; });
    timers.Update(0.35f);
    CHECK(fired == 3);
    CHECK(timers.Cancel(id));
    CHECK(!timers.Cancel(id) && !timers.IsActive(id));
    timers.Update(1.0f);
    CHECK(fired == 3);

    // Cancelling itself from its callback stops it, even in the middle of a long frame.
    i32 count = 0;
    TimerId self;
    self = timers.Every(0.1f, [&] {
        if (++count == 3)
            timers.Cancel(self);
    });
    timers.Update(10.0f);
    CHECK(count == 3 && timers.GetCount() == 0);
}

TEST(TimerCallbacksCanAddTimers)
{
    Timers timers;
    i32 inner = 0;
    timers.After(0.5f, [&] { timers.After(0.0f, [&] { ++inner; }); });
    timers.Update(1.0f);
    CHECK(inner == 0 && timers.GetCount() == 1); // added during Update: runs on the next one
    timers.Update(0.0f);
    CHECK(inner == 1);

    // Clear from a callback stops the rest.
    i32 later = 0;
    timers.After(0.1f, [&] { timers.Clear(); });
    timers.After(0.2f, [&] { ++later; });
    timers.Update(1.0f);
    CHECK(later == 0 && timers.GetCount() == 0);
}

TEST(TimerTinyIntervalsAreRaised)
{
    Timers timers;
    i32 fired = 0;
    timers.Every(0.0f, [&] { ++fired; }); // treated as Timers::kMinInterval (1 ms)
    timers.Update(0.0105f);
    CHECK(fired == 10);
}
