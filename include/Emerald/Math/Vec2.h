#pragma once

#include <cmath>

#include "Emerald/Math/Common.h"

namespace Emerald {

// 2D vector of floats (positions, sizes, velocities in 2D).
//
// Always scalar on purpose: a Vec2 is 8 bytes, half an SSE register. Loading it into a register,
// doing one add and storing it back costs about as much as two plain float adds, so SIMD only pays
// off for 2D when many vectors are processed at once in a structure-of-arrays layout
// (all x values together, all y values together), not per Vec2.
struct Vec2 {
    f32 x = 0.0f;
    f32 y = 0.0f;

    constexpr Vec2() = default;
    constexpr Vec2(f32 x_, f32 y_) : x(x_), y(y_) {}
    constexpr explicit Vec2(f32 s) : x(s), y(s) {}

    constexpr Vec2& operator+=(const Vec2& o)
    {
        x += o.x;
        y += o.y;
        return *this;
    }
    constexpr Vec2& operator-=(const Vec2& o)
    {
        x -= o.x;
        y -= o.y;
        return *this;
    }
    constexpr Vec2& operator*=(f32 s)
    {
        x *= s;
        y *= s;
        return *this;
    }
    constexpr Vec2& operator/=(f32 s)
    {
        x /= s;
        y /= s;
        return *this;
    }

    constexpr bool operator==(const Vec2&) const = default; // exact; see NearlyEqual
};

[[nodiscard]] constexpr Vec2 operator+(Vec2 a, const Vec2& b)
{
    return a += b;
}
[[nodiscard]] constexpr Vec2 operator-(Vec2 a, const Vec2& b)
{
    return a -= b;
}
[[nodiscard]] constexpr Vec2 operator-(const Vec2& v)
{
    return {-v.x, -v.y};
}
[[nodiscard]] constexpr Vec2 operator*(const Vec2& a, const Vec2& b)
{
    return {a.x * b.x, a.y * b.y};
}
[[nodiscard]] constexpr Vec2 operator/(const Vec2& a, const Vec2& b)
{
    return {a.x / b.x, a.y / b.y};
}
[[nodiscard]] constexpr Vec2 operator*(Vec2 v, f32 s)
{
    return v *= s;
}
[[nodiscard]] constexpr Vec2 operator*(f32 s, Vec2 v)
{
    return v *= s;
}
[[nodiscard]] constexpr Vec2 operator/(Vec2 v, f32 s)
{
    return v /= s;
}

[[nodiscard]] constexpr f32 Dot(const Vec2& a, const Vec2& b)
{
    return a.x * b.x + a.y * b.y;
}
// z component of the 3D cross product: > 0 if b is counterclockwise from a (in a y-up space).
[[nodiscard]] constexpr f32 Cross(const Vec2& a, const Vec2& b)
{
    return a.x * b.y - a.y * b.x;
}
[[nodiscard]] constexpr f32 LengthSquared(const Vec2& v)
{
    return Dot(v, v);
}
[[nodiscard]] inline f32 Length(const Vec2& v)
{
    return std::sqrt(LengthSquared(v));
}
[[nodiscard]] inline f32 Distance(const Vec2& a, const Vec2& b)
{
    return Length(b - a);
}

// Unit-length copy of v; returns (0, 0) for a zero vector instead of NaNs.
[[nodiscard]] inline Vec2 Normalize(const Vec2& v)
{
    const f32 len = Length(v);
    return len > 0.0f ? v / len : Vec2{};
}

[[nodiscard]] constexpr Vec2 Lerp(const Vec2& a, const Vec2& b, f32 t)
{
    return a + (b - a) * t;
}
[[nodiscard]] constexpr Vec2 Min(const Vec2& a, const Vec2& b)
{
    return {Min(a.x, b.x), Min(a.y, b.y)};
}
[[nodiscard]] constexpr Vec2 Max(const Vec2& a, const Vec2& b)
{
    return {Max(a.x, b.x), Max(a.y, b.y)};
}

[[nodiscard]] inline bool NearlyEqual(const Vec2& a, const Vec2& b, f32 epsilon = Epsilon)
{
    return NearlyEqual(a.x, b.x, epsilon) && NearlyEqual(a.y, b.y, epsilon);
}

// 2D vector of ints (pixel coordinates, tile indices, window sizes).
struct Vec2i {
    i32 x = 0;
    i32 y = 0;

    constexpr Vec2i() = default;
    constexpr Vec2i(i32 x_, i32 y_) : x(x_), y(y_) {}

    constexpr Vec2i& operator+=(const Vec2i& o)
    {
        x += o.x;
        y += o.y;
        return *this;
    }
    constexpr Vec2i& operator-=(const Vec2i& o)
    {
        x -= o.x;
        y -= o.y;
        return *this;
    }

    constexpr bool operator==(const Vec2i&) const = default;

    // Explicit conversion so float math on integer coordinates is visible in the code.
    [[nodiscard]] constexpr explicit operator Vec2() const
    {
        return {static_cast<f32>(x), static_cast<f32>(y)};
    }
};

[[nodiscard]] constexpr Vec2i operator+(Vec2i a, const Vec2i& b)
{
    return a += b;
}
[[nodiscard]] constexpr Vec2i operator-(Vec2i a, const Vec2i& b)
{
    return a -= b;
}
[[nodiscard]] constexpr Vec2i operator-(const Vec2i& v)
{
    return {-v.x, -v.y};
}
[[nodiscard]] constexpr Vec2i operator*(const Vec2i& v, i32 s)
{
    return {v.x * s, v.y * s};
}
[[nodiscard]] constexpr Vec2i operator*(i32 s, const Vec2i& v)
{
    return {v.x * s, v.y * s};
}

} // namespace Emerald
