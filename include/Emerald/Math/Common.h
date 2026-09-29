#pragma once

// Scalar math helpers shared by the vector/matrix headers, and the SIMD configuration.

#include <cmath>
#include <numbers>

#include "Emerald/Core/Defines.h"

// ---------------------------------------------------------------------------
// SIMD configuration
// ---------------------------------------------------------------------------
// EMERALD_MATH_SIMD (1/0) comes from the CMake option of the same name. It only chooses which
// implementation the public API (operators, Dot, Mat4 * Vec4, ...) uses; types, layout and
// signatures are identical in both modes.
//
// EMERALD_MATH_HAS_SSE2 says whether the compiler targets SSE2 at all (always true on x86-64).
// When it is, the SSE code in Math::Sse is compiled even with EMERALD_MATH_SIMD=0, so tests and
// benchmarks can compare it against Math::Scalar. On other CPUs (e.g. ARM) only the scalar path
// exists; a NEON path could be added the same way later.
#ifndef EMERALD_MATH_SIMD
#define EMERALD_MATH_SIMD 1
#endif

// (ARM64EC defines _M_X64 too but only emulates SSE, so it takes the scalar path.)
#if (defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64) ||                                  \
     (defined(_M_IX86_FP) && _M_IX86_FP >= 2)) &&                                                  \
    !defined(_M_ARM64EC)
#define EMERALD_MATH_HAS_SSE2 1
#else
#define EMERALD_MATH_HAS_SSE2 0
#endif

// SSE4.1 is not part of the x86-64 baseline, so it is only used when the compiler was told the
// target has it (GCC/Clang: -msse4.1 or -march=...; MSVC: /arch:AVX or higher defines __AVX__).
#if EMERALD_MATH_HAS_SSE2 && (defined(__SSE4_1__) || defined(__AVX__))
#define EMERALD_MATH_HAS_SSE41 1
#else
#define EMERALD_MATH_HAS_SSE41 0
#endif

#define EMERALD_MATH_USE_SSE (EMERALD_MATH_SIMD && EMERALD_MATH_HAS_SSE2)

#if EMERALD_MATH_HAS_SSE2
#if EMERALD_MATH_HAS_SSE41
#include <smmintrin.h> // SSE4.1
#else
#include <emmintrin.h> // SSE2
#endif
#endif

namespace Emerald {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
inline constexpr f32 Pi = std::numbers::pi_v<f32>;
inline constexpr f32 TwoPi = 2.0f * Pi;
inline constexpr f32 HalfPi = 0.5f * Pi;
// Default tolerance for the approximate comparisons below.
inline constexpr f32 Epsilon = 1e-5f;

// ---------------------------------------------------------------------------
// Scalar helpers
// ---------------------------------------------------------------------------
[[nodiscard]] constexpr f32 ToRadians(f32 degrees)
{
    return degrees * (Pi / 180.0f);
}
[[nodiscard]] constexpr f32 ToDegrees(f32 radians)
{
    return radians * (180.0f / Pi);
}

template <typename T> [[nodiscard]] constexpr T Clamp(T value, T lo, T hi)
{
    return value < lo ? lo : (hi < value ? hi : value);
}

template <typename T> [[nodiscard]] constexpr T Min(T a, T b)
{
    return b < a ? b : a;
}

template <typename T> [[nodiscard]] constexpr T Max(T a, T b)
{
    return a < b ? b : a;
}

// Linear interpolation: t = 0 gives a, t = 1 gives b.
[[nodiscard]] constexpr f32 Lerp(f32 a, f32 b, f32 t)
{
    return a + (b - a) * t;
}

// |a - b| <= epsilon. Good for values near 1; use NearlyEqualRelative for large magnitudes.
[[nodiscard]] inline bool NearlyEqual(f32 a, f32 b, f32 epsilon = Epsilon)
{
    return std::fabs(a - b) <= epsilon;
}

// Tolerance scaled by the larger magnitude (with `epsilon` as the absolute floor near zero).
[[nodiscard]] inline bool NearlyEqualRelative(f32 a, f32 b, f32 epsilon = Epsilon)
{
    const f32 scale = Max(1.0f, Max(std::fabs(a), std::fabs(b)));
    return std::fabs(a - b) <= epsilon * scale;
}

[[nodiscard]] inline bool NearlyZero(f32 value, f32 epsilon = Epsilon)
{
    return std::fabs(value) <= epsilon;
}

} // namespace Emerald
