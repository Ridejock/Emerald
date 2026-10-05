#pragma once

#include <cmath>

#include "Emerald/Core/Defines.h"
#include "Emerald/Math/Common.h"
#include "Emerald/Math/Vec2.h"

namespace Emerald {

// How a sound in the world falls off and pans relative to a listener (usually the camera).
// Volume is 1 inside MinDistance, 0 at and beyond MaxDistance, and linear in between. Pan is the
// relative X clamped to [-1, 1] over MaxDistance (same units as the positions).
struct SpatialParams {
    Vec2 Position{};
    f32 MinDistance = 64.0f;
    f32 MaxDistance = 512.0f;
};

[[nodiscard]] inline f32 SpatialAttenuation(const Vec2& listener, const SpatialParams& sound)
{
    const f32 maxD = Max(sound.MaxDistance, sound.MinDistance + 1e-3f);
    const f32 minD = Clamp(sound.MinDistance, 0.0f, maxD);
    const f32 d = Distance(listener, sound.Position);
    if (d <= minD)
        return 1.0f;
    if (d >= maxD)
        return 0.0f;
    return 1.0f - (d - minD) / (maxD - minD);
}

// -1 = fully left, +1 = fully right. At the listener, 0.
[[nodiscard]] inline f32 SpatialPan(const Vec2& listener, const SpatialParams& sound)
{
    const f32 range = Max(sound.MaxDistance, 1e-3f);
    return Clamp((sound.Position.x - listener.x) / range, -1.0f, 1.0f);
}

// Balance pan (matches Mixer): far side quieter, center full on both.
inline void SpatialGains(f32 pan, f32& left, f32& right)
{
    const f32 p = Clamp(pan, -1.0f, 1.0f);
    left = Min(1.0f, 1.0f - p);
    right = Min(1.0f, 1.0f + p);
}

} // namespace Emerald
