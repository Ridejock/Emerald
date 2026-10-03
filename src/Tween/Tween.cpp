#include "Emerald/Tween/Tween.h"

#include <algorithm>
#include <cassert>
#include <type_traits>
#include <utility>

namespace Emerald {

TweenId Tweens::To(f32* target, f32 to, f32 duration, TweenOptions options)
{
    assert(target);
    return Add(Track<f32>{.Target = target, .To = to}, duration, std::move(options));
}

TweenId Tweens::To(Vec2* target, Vec2 to, f32 duration, TweenOptions options)
{
    assert(target);
    return Add(Track<Vec2>{.Target = target, .To = to}, duration, std::move(options));
}

TweenId Tweens::To(Vec4* target, Vec4 to, f32 duration, TweenOptions options)
{
    assert(target);
    return Add(Track<Vec4>{.Target = target, .To = to}, duration, std::move(options));
}

TweenId Tweens::FromTo(f32* target, f32 from, f32 to, f32 duration, TweenOptions options)
{
    assert(target);
    return Add(Track<f32>{.Target = target, .From = from, .To = to, .HasFrom = true}, duration,
               std::move(options));
}

TweenId Tweens::FromTo(Vec2* target, Vec2 from, Vec2 to, f32 duration, TweenOptions options)
{
    assert(target);
    return Add(Track<Vec2>{.Target = target, .From = from, .To = to, .HasFrom = true}, duration,
               std::move(options));
}

TweenId Tweens::FromTo(Vec4* target, Vec4 from, Vec4 to, f32 duration, TweenOptions options)
{
    assert(target);
    return Add(Track<Vec4>{.Target = target, .From = from, .To = to, .HasFrom = true}, duration,
               std::move(options));
}

TweenId Tweens::Run(f32 duration, std::function<void(f32 eased)> apply, TweenOptions options)
{
    assert(apply);
    return Add(std::move(apply), duration, std::move(options));
}

TweenId Tweens::Add(AnyTrack value, f32 duration, TweenOptions options)
{
    auto tween = std::make_unique<Tween>();
    tween->Id = TweenId{m_NextId++};
    tween->Value = std::move(value);
    tween->Duration = std::max(duration, 0.0f);
    tween->PlaysLeft = options.Repeat < 0 ? kForever : options.Repeat;
    tween->Options = std::move(options);
    // Chained after a tween that is already gone: nothing to wait for.
    if (tween->Options.After && !IsActive(tween->Options.After))
        tween->Options.After = {};
    const TweenId id = tween->Id;
    m_Tweens.push_back(std::move(tween));
    return id;
}

bool Tweens::Cancel(TweenId id)
{
    if (!IsActive(id))
        return false;
    MarkCancelled(id);
    if (!m_Updating)
        RemoveDone(); // during Update, Update removes it at the end
    return true;
}

void Tweens::MarkCancelled(TweenId id)
{
    Tween* tween = Find(id);
    if (!tween || tween->Done)
        return;
    tween->Done = true;
    // Whatever was waiting for it would wait forever, so it goes too.
    for (const std::unique_ptr<Tween>& other : m_Tweens)
        if (other->Options.After == id)
            MarkCancelled(other->Id);
}

void Tweens::CancelTarget(const void* target)
{
    for (const std::unique_ptr<Tween>& tween : m_Tweens) {
        const bool writesTarget = std::visit(
            [target](const auto& track) {
                if constexpr (std::is_same_v<std::decay_t<decltype(track)>, RunCallback>)
                    return false; // a Run callback has no target
                else
                    return static_cast<const void*>(track.Target) == target;
            },
            tween->Value);
        if (writesTarget)
            MarkCancelled(tween->Id);
    }
    if (!m_Updating)
        RemoveDone();
}

void Tweens::Clear()
{
    for (const std::unique_ptr<Tween>& tween : m_Tweens)
        tween->Done = true;
    if (!m_Updating)
        RemoveDone();
}

bool Tweens::IsActive(TweenId id) const
{
    const Tween* tween = Find(id);
    return tween && !tween->Done;
}

usize Tweens::GetCount() const
{
    return static_cast<usize>(std::count_if(m_Tweens.begin(), m_Tweens.end(),
                                            [](const auto& tween) { return !tween->Done; }));
}

void Tweens::Update(f32 dt)
{
    m_Updating = true;
    m_CompletedThisUpdate.clear();
    // Only the tweens that exist now; ones added by callbacks start on the next Update.
    const usize count = m_Tweens.size();
    for (usize i = 0; i < count; ++i) {
        Tween& tween = *m_Tweens[i];
        if (tween.Done)
            continue;

        f32 step = dt;
        if (tween.Options.After) {
            if (IsActive(tween.Options.After))
                continue; // still waiting for the tween before it
            // It completed during this Update (it comes earlier in the list): start with the
            // time left over after it ended, as if the two were one long tween.
            for (const auto& [id, leftover] : m_CompletedThisUpdate)
                if (id == tween.Options.After)
                    step = leftover;
            tween.Options.After = {};
        }

        const std::optional<f32> leftover = Advance(tween, step);
        if (leftover && !tween.Done) { // (a Run callback may have cancelled it)
            tween.Done = true;         // before the callback, so it can only ever fire once
            m_CompletedThisUpdate.emplace_back(tween.Id, *leftover);
            if (tween.Options.OnComplete)
                tween.Options.OnComplete();
        }
    }
    m_Updating = false;
    RemoveDone();
}

std::optional<f32> Tweens::Advance(Tween& tween, f32 dt)
{
    tween.Time += dt;
    if (!tween.Started) {
        if (tween.Time < tween.Options.Delay)
            return std::nullopt;
        tween.Time -= tween.Options.Delay;
        Start(tween);
    }
    // A zero-length tween jumps straight to the end (its repeats would take no time at all).
    if (tween.Duration <= 0.0f) {
        ApplyEnd(tween);
        return tween.Time;
    }
    // Whole plays that fit into the time: start the next play (repeat) or complete.
    while (tween.Time >= tween.Duration) {
        if (tween.PlaysLeft == 0) {
            ApplyEnd(tween);
            return tween.Time - tween.Duration;
        }
        tween.Time -= tween.Duration;
        if (tween.PlaysLeft > 0)
            --tween.PlaysLeft;
        if (tween.Options.Yoyo)
            tween.Backwards = !tween.Backwards;
    }
    Apply(tween, tween.Time / tween.Duration);
    return std::nullopt;
}

void Tweens::Start(Tween& tween)
{
    tween.Started = true;
    std::visit(
        [](auto& track) {
            if constexpr (!std::is_same_v<std::decay_t<decltype(track)>, RunCallback>) {
                if (track.HasFrom)
                    *track.Target = track.From;
                else
                    track.From = *track.Target; // To: from wherever the value is now
            }
        },
        tween.Value);
}

void Tweens::Apply(Tween& tween, f32 progress)
{
    // Yoyo's way back plays the same motion in reverse.
    const f32 eased = Ease(tween.Options.Curve, tween.Backwards ? 1.0f - progress : progress);
    std::visit(
        [eased](auto& track) {
            if constexpr (std::is_same_v<std::decay_t<decltype(track)>, RunCallback>)
                track(eased);
            else
                *track.Target = Lerp(track.From, track.To, eased);
        },
        tween.Value);
}

void Tweens::ApplyEnd(Tween& tween)
{
    std::visit(
        [&tween](auto& track) {
            if constexpr (std::is_same_v<std::decay_t<decltype(track)>, RunCallback>)
                track(tween.Backwards ? 0.0f : 1.0f);
            else
                *track.Target = tween.Backwards ? track.From : track.To; // exact, not via Lerp
        },
        tween.Value);
}

Tweens::Tween* Tweens::Find(TweenId id)
{
    for (const std::unique_ptr<Tween>& tween : m_Tweens)
        if (tween->Id == id)
            return tween.get();
    return nullptr;
}

const Tweens::Tween* Tweens::Find(TweenId id) const
{
    for (const std::unique_ptr<Tween>& tween : m_Tweens)
        if (tween->Id == id)
            return tween.get();
    return nullptr;
}

void Tweens::RemoveDone()
{
    std::erase_if(m_Tweens, [](const std::unique_ptr<Tween>& tween) { return tween->Done; });
}

} // namespace Emerald
