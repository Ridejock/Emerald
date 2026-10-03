#pragma once

#include <array>

#include "Emerald/Core/Defines.h"

namespace Emerald {

// Easing curves: map progress t in [0, 1] to an eased amount, with f(0) = 0 and f(1) = 1.
//
//   In    - starts slow, ends fast (accelerates)
//   Out   - starts fast, ends slow (decelerates; good for things arriving on screen)
//   InOut - slow at both ends, fast in the middle
//
// Back overshoots (goes a little below 0 or above 1), Elastic wobbles like a spring and Bounce
// bounces off the end. The formulas are the usual ones (see https://easings.net).
enum class Easing : u8 {
    Linear,
    QuadIn,
    QuadOut,
    QuadInOut,
    CubicIn,
    CubicOut,
    CubicInOut,
    BackIn,
    BackOut,
    BackInOut,
    ElasticIn,
    ElasticOut,
    ElasticInOut,
    BounceIn,
    BounceOut,
    BounceInOut,
};

inline constexpr usize kEasingCount = 16;

// Every curve, in enum order (for menus and tests).
inline constexpr std::array<Easing, kEasingCount> kAllEasings{
    Easing::Linear,       Easing::QuadIn,    Easing::QuadOut,    Easing::QuadInOut,
    Easing::CubicIn,      Easing::CubicOut,  Easing::CubicInOut, Easing::BackIn,
    Easing::BackOut,      Easing::BackInOut, Easing::ElasticIn,  Easing::ElasticOut,
    Easing::ElasticInOut, Easing::BounceIn,  Easing::BounceOut,  Easing::BounceInOut,
};

// Evaluates `easing` at t; t is clamped to [0, 1] first.
[[nodiscard]] f32 Ease(Easing easing, f32 t);

// "QuadOut", "BounceInOut", ...
[[nodiscard]] const char* GetEasingName(Easing easing);

} // namespace Emerald
