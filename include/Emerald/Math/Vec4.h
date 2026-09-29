#pragma once

#include <cmath>

#include "Emerald/Math/Common.h"
#include "Emerald/Math/Vec2.h"
#include "Emerald/Math/Vec3.h"

namespace Emerald {

// 4D vector of floats: homogeneous positions (w = 1) and directions (w = 0), RGBA colors, and the
// columns of a Mat4. Matches HLSL's float4.
//
// Four floats are exactly one 128-bit SSE register, so every operation below is a single SIMD
// instruction (plus a few shuffles for Dot) when EMERALD_MATH_SIMD is on. alignas(16) lets the
// SSE code use aligned loads/stores; it is set in both modes so the memory layout never changes.
struct alignas(16) Vec4 {
    f32 x = 0.0f;
    f32 y = 0.0f;
    f32 z = 0.0f;
    f32 w = 0.0f;

    constexpr Vec4() = default;
    constexpr Vec4(f32 x_, f32 y_, f32 z_, f32 w_) : x(x_), y(y_), z(z_), w(w_) {}
    constexpr explicit Vec4(f32 s) : x(s), y(s), z(s), w(s) {}
    constexpr Vec4(const Vec3& xyz, f32 w_) : x(xyz.x), y(xyz.y), z(xyz.z), w(w_) {}
    constexpr Vec4(const Vec2& xy, f32 z_, f32 w_) : x(xy.x), y(xy.y), z(z_), w(w_) {}

    [[nodiscard]] constexpr Vec2 XY() const { return {x, y}; }
    [[nodiscard]] constexpr Vec3 XYZ() const { return {x, y, z}; }

    // Component by index (0 = x ... 3 = w).
    [[nodiscard]] constexpr f32 operator[](usize i) const
    {
        return i == 0 ? x : (i == 1 ? y : (i == 2 ? z : w));
    }
    [[nodiscard]] constexpr f32& operator[](usize i)
    {
        return i == 0 ? x : (i == 1 ? y : (i == 2 ? z : w));
    }

    // Defined below, after the implementations they forward to.
    Vec4& operator+=(const Vec4& o);
    Vec4& operator-=(const Vec4& o);
    Vec4& operator*=(f32 s);
    Vec4& operator/=(f32 s);

    constexpr bool operator==(const Vec4&) const = default; // exact; see NearlyEqual
};

static_assert(sizeof(Vec4) == 16 && alignof(Vec4) == 16);

namespace Math {

// Reference implementation in plain C++. Always available; used by the public API when
// EMERALD_MATH_SIMD is off or the CPU has no SSE2.
namespace Scalar {

inline Vec4 Add(const Vec4& a, const Vec4& b)
{
    return {a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w};
}
inline Vec4 Sub(const Vec4& a, const Vec4& b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w};
}
inline Vec4 Mul(const Vec4& a, const Vec4& b)
{
    return {a.x * b.x, a.y * b.y, a.z * b.z, a.w * b.w};
}
inline Vec4 Div(const Vec4& a, const Vec4& b)
{
    return {a.x / b.x, a.y / b.y, a.z / b.z, a.w / b.w};
}
inline Vec4 Scale(const Vec4& v, f32 s)
{
    return {v.x * s, v.y * s, v.z * s, v.w * s};
}
inline Vec4 DivScalar(const Vec4& v, f32 s)
{
    return {v.x / s, v.y / s, v.z / s, v.w / s};
}

// Summed as (x + y) + (z + w), the same order as the SSE version, so both give identical results.
inline f32 Dot(const Vec4& a, const Vec4& b)
{
    return (a.x * b.x + a.y * b.y) + (a.z * b.z + a.w * b.w);
}

inline Vec4 Normalize(const Vec4& v)
{
    const f32 len = std::sqrt(Dot(v, v));
    return len > 0.0f ? DivScalar(v, len) : Vec4{};
}

inline Vec4 Lerp(const Vec4& a, const Vec4& b, f32 t)
{
    return Add(a, Scale(Sub(b, a), t));
}

} // namespace Scalar

#if EMERALD_MATH_HAS_SSE2
// SSE implementation: each function loads the Vec4(s) into __m128 registers, does the work with
// one or a few instructions, and stores the result. Compilers keep values in registers across
// inlined calls, so chains like a * s + b do not actually round-trip through memory.
namespace Sse {

inline __m128 Load(const Vec4& v)
{
    return _mm_load_ps(&v.x);
} // aligned thanks to alignas(16)
inline Vec4 Store(__m128 r)
{
    Vec4 v;
    _mm_store_ps(&v.x, r);
    return v;
}

inline Vec4 Add(const Vec4& a, const Vec4& b)
{
    return Store(_mm_add_ps(Load(a), Load(b)));
}
inline Vec4 Sub(const Vec4& a, const Vec4& b)
{
    return Store(_mm_sub_ps(Load(a), Load(b)));
}
inline Vec4 Mul(const Vec4& a, const Vec4& b)
{
    return Store(_mm_mul_ps(Load(a), Load(b)));
}
inline Vec4 Div(const Vec4& a, const Vec4& b)
{
    return Store(_mm_div_ps(Load(a), Load(b)));
}
inline Vec4 Scale(const Vec4& v, f32 s)
{
    return Store(_mm_mul_ps(Load(v), _mm_set1_ps(s)));
}
inline Vec4 DivScalar(const Vec4& v, f32 s)
{
    return Store(_mm_div_ps(Load(v), _mm_set1_ps(s)));
}

inline f32 Dot(const Vec4& a, const Vec4& b)
{
#if EMERALD_MATH_HAS_SSE41
    // 0xF1: multiply all four lanes, write the sum to lane 0.
    return _mm_cvtss_f32(_mm_dp_ps(Load(a), Load(b), 0xF1));
#else
    // Horizontal add with SSE2 shuffles: [x y z w] -> [x+y, ., z+w, .] -> (x+y) + (z+w).
    const __m128 m = _mm_mul_ps(Load(a), Load(b));
    const __m128 swapped = _mm_shuffle_ps(m, m, _MM_SHUFFLE(2, 3, 0, 1)); // [y x w z]
    const __m128 pairs = _mm_add_ps(m, swapped);                          // [x+y . z+w .]
    const __m128 high = _mm_movehl_ps(swapped, pairs);                    // [z+w . . .]
    return _mm_cvtss_f32(_mm_add_ss(pairs, high));
#endif
}

inline Vec4 Normalize(const Vec4& v)
{
    const f32 len = std::sqrt(Dot(v, v));
    return len > 0.0f ? DivScalar(v, len) : Vec4{};
}

inline Vec4 Lerp(const Vec4& a, const Vec4& b, f32 t)
{
    const __m128 va = Load(a);
    const __m128 delta = _mm_sub_ps(Load(b), va);
    return Store(_mm_add_ps(va, _mm_mul_ps(delta, _mm_set1_ps(t))));
}

} // namespace Sse
#endif

// The implementation behind the public operators and functions.
#if EMERALD_MATH_USE_SSE
namespace Impl = Sse;
#else
namespace Impl = Scalar;
#endif

} // namespace Math

inline Vec4& Vec4::operator+=(const Vec4& o)
{
    return *this = Math::Impl::Add(*this, o);
}
inline Vec4& Vec4::operator-=(const Vec4& o)
{
    return *this = Math::Impl::Sub(*this, o);
}
inline Vec4& Vec4::operator*=(f32 s)
{
    return *this = Math::Impl::Scale(*this, s);
}
inline Vec4& Vec4::operator/=(f32 s)
{
    return *this = Math::Impl::DivScalar(*this, s);
}

[[nodiscard]] inline Vec4 operator+(const Vec4& a, const Vec4& b)
{
    return Math::Impl::Add(a, b);
}
[[nodiscard]] inline Vec4 operator-(const Vec4& a, const Vec4& b)
{
    return Math::Impl::Sub(a, b);
}
[[nodiscard]] inline Vec4 operator*(const Vec4& a, const Vec4& b)
{
    return Math::Impl::Mul(a, b);
}
[[nodiscard]] inline Vec4 operator/(const Vec4& a, const Vec4& b)
{
    return Math::Impl::Div(a, b);
}
[[nodiscard]] inline Vec4 operator*(const Vec4& v, f32 s)
{
    return Math::Impl::Scale(v, s);
}
[[nodiscard]] inline Vec4 operator*(f32 s, const Vec4& v)
{
    return Math::Impl::Scale(v, s);
}
[[nodiscard]] inline Vec4 operator/(const Vec4& v, f32 s)
{
    return Math::Impl::DivScalar(v, s);
}
[[nodiscard]] inline Vec4 operator-(const Vec4& v)
{
    return Math::Impl::Scale(v, -1.0f);
}

[[nodiscard]] inline f32 Dot(const Vec4& a, const Vec4& b)
{
    return Math::Impl::Dot(a, b);
}
[[nodiscard]] inline f32 LengthSquared(const Vec4& v)
{
    return Dot(v, v);
}
[[nodiscard]] inline f32 Length(const Vec4& v)
{
    return std::sqrt(Dot(v, v));
}
// Unit-length copy of v (all four components); returns a zero vector for a zero input.
[[nodiscard]] inline Vec4 Normalize(const Vec4& v)
{
    return Math::Impl::Normalize(v);
}
[[nodiscard]] inline Vec4 Lerp(const Vec4& a, const Vec4& b, f32 t)
{
    return Math::Impl::Lerp(a, b, t);
}

[[nodiscard]] constexpr Vec4 Min(const Vec4& a, const Vec4& b)
{
    return {Min(a.x, b.x), Min(a.y, b.y), Min(a.z, b.z), Min(a.w, b.w)};
}
[[nodiscard]] constexpr Vec4 Max(const Vec4& a, const Vec4& b)
{
    return {Max(a.x, b.x), Max(a.y, b.y), Max(a.z, b.z), Max(a.w, b.w)};
}

[[nodiscard]] inline bool NearlyEqual(const Vec4& a, const Vec4& b, f32 epsilon = Epsilon)
{
    return NearlyEqual(a.x, b.x, epsilon) && NearlyEqual(a.y, b.y, epsilon) &&
           NearlyEqual(a.z, b.z, epsilon) && NearlyEqual(a.w, b.w, epsilon);
}

} // namespace Emerald
