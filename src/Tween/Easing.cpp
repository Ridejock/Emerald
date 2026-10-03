#include "Emerald/Tween/Easing.h"

#include <cmath>

#include "Emerald/Math/Common.h"

namespace Emerald {

namespace {

// Back: how far the curve overshoots (1.70158 gives about 10%).
constexpr f32 kBack = 1.70158f;
constexpr f32 kBackInOut = kBack * 1.525f;

// Elastic: the wobble's period, as an angle per unit of t.
constexpr f32 kElastic = TwoPi / 3.0f;
constexpr f32 kElasticInOut = TwoPi / 4.5f;

f32 BounceOut(f32 t)
{
    // Four parabolas, each a smaller bounce than the one before.
    constexpr f32 n = 7.5625f;
    constexpr f32 d = 2.75f;
    if (t < 1.0f / d)
        return n * t * t;
    if (t < 2.0f / d) {
        t -= 1.5f / d;
        return n * t * t + 0.75f;
    }
    if (t < 2.5f / d) {
        t -= 2.25f / d;
        return n * t * t + 0.9375f;
    }
    t -= 2.625f / d;
    return n * t * t + 0.984375f;
}

} // namespace

f32 Ease(Easing easing, f32 t)
{
    t = Clamp(t, 0.0f, 1.0f);
    const f32 u = 1.0f - t; // the remaining progress; "Out" curves are "In" curves mirrored
    switch (easing) {
    case Easing::Linear:
        return t;

    case Easing::QuadIn:
        return t * t;
    case Easing::QuadOut:
        return 1.0f - u * u;
    case Easing::QuadInOut:
        return t < 0.5f ? 2.0f * t * t : 1.0f - 2.0f * u * u;

    case Easing::CubicIn:
        return t * t * t;
    case Easing::CubicOut:
        return 1.0f - u * u * u;
    case Easing::CubicInOut:
        return t < 0.5f ? 4.0f * t * t * t : 1.0f - 4.0f * u * u * u;

    case Easing::BackIn:
        return (kBack + 1.0f) * t * t * t - kBack * t * t;
    case Easing::BackOut:
        return 1.0f - ((kBack + 1.0f) * u * u * u - kBack * u * u);
    case Easing::BackInOut: {
        const f32 s = t < 0.5f ? 2.0f * t : 2.0f * u; // progress through this half
        const f32 half = ((kBackInOut + 1.0f) * s - kBackInOut) * s * s * 0.5f;
        return t < 0.5f ? half : 1.0f - half;
    }

    case Easing::ElasticIn:
        if (t <= 0.0f || t >= 1.0f)
            return t; // exact ends (the formula only gets close)
        return -std::pow(2.0f, 10.0f * t - 10.0f) * std::sin((10.0f * t - 10.75f) * kElastic);
    case Easing::ElasticOut:
        if (t <= 0.0f || t >= 1.0f)
            return t;
        return std::pow(2.0f, -10.0f * t) * std::sin((10.0f * t - 0.75f) * kElastic) + 1.0f;
    case Easing::ElasticInOut: {
        if (t <= 0.0f || t >= 1.0f)
            return t;
        const f32 wave = std::sin((20.0f * t - 11.125f) * kElasticInOut);
        if (t < 0.5f)
            return -0.5f * std::pow(2.0f, 20.0f * t - 10.0f) * wave;
        return 0.5f * std::pow(2.0f, -20.0f * t + 10.0f) * wave + 1.0f;
    }

    case Easing::BounceIn:
        return 1.0f - BounceOut(u);
    case Easing::BounceOut:
        return BounceOut(t);
    case Easing::BounceInOut:
        return t < 0.5f ? 0.5f * (1.0f - BounceOut(1.0f - 2.0f * t))
                        : 0.5f * (1.0f + BounceOut(2.0f * t - 1.0f));
    }
    return t;
}

const char* GetEasingName(Easing easing)
{
    switch (easing) {
    case Easing::Linear:
        return "Linear";
    case Easing::QuadIn:
        return "QuadIn";
    case Easing::QuadOut:
        return "QuadOut";
    case Easing::QuadInOut:
        return "QuadInOut";
    case Easing::CubicIn:
        return "CubicIn";
    case Easing::CubicOut:
        return "CubicOut";
    case Easing::CubicInOut:
        return "CubicInOut";
    case Easing::BackIn:
        return "BackIn";
    case Easing::BackOut:
        return "BackOut";
    case Easing::BackInOut:
        return "BackInOut";
    case Easing::ElasticIn:
        return "ElasticIn";
    case Easing::ElasticOut:
        return "ElasticOut";
    case Easing::ElasticInOut:
        return "ElasticInOut";
    case Easing::BounceIn:
        return "BounceIn";
    case Easing::BounceOut:
        return "BounceOut";
    case Easing::BounceInOut:
        return "BounceInOut";
    }
    return "?";
}

} // namespace Emerald
