#pragma once

#include <cmath>

#include "Emerald/Math/Common.h"
#include "Emerald/Math/Vec2.h"

namespace Emerald {

// 3D vector of floats (positions, directions, RGB colors). 12 bytes, no padding, so arrays of
// Vec3 can be uploaded as vertex data directly. Note: in an HLSL cbuffer a float3 is padded to 16
// bytes when followed by another float3/float4, so prefer Vec4 in uniform structs.
struct Vec3 {
    f32 x = 0.0f;
    f32 y = 0.0f;
    f32 z = 0.0f;

    constexpr Vec3() = default;
    constexpr Vec3(f32 x_, f32 y_, f32 z_) : x(x_), y(y_), z(z_) {}
    constexpr explicit Vec3(f32 s) : x(s), y(s), z(s) {}
    constexpr Vec3(const Vec2& xy, f32 z_) : x(xy.x), y(xy.y), z(z_) {}

    [[nodiscard]] constexpr Vec2 XY() const { return {x, y}; }

    constexpr Vec3& operator+=(const Vec3& o)
    {
        x += o.x;
        y += o.y;
        z += o.z;
        return *this;
    }
    constexpr Vec3& operator-=(const Vec3& o)
    {
        x -= o.x;
        y -= o.y;
        z -= o.z;
        return *this;
    }
    constexpr Vec3& operator*=(f32 s)
    {
        x *= s;
        y *= s;
        z *= s;
        return *this;
    }
    constexpr Vec3& operator/=(f32 s)
    {
        x /= s;
        y /= s;
        z /= s;
        return *this;
    }

    constexpr bool operator==(const Vec3&) const = default; // exact; see NearlyEqual
};

[[nodiscard]] constexpr Vec3 operator+(Vec3 a, const Vec3& b)
{
    return a += b;
}
[[nodiscard]] constexpr Vec3 operator-(Vec3 a, const Vec3& b)
{
    return a -= b;
}
[[nodiscard]] constexpr Vec3 operator-(const Vec3& v)
{
    return {-v.x, -v.y, -v.z};
}
[[nodiscard]] constexpr Vec3 operator*(const Vec3& a, const Vec3& b)
{
    return {a.x * b.x, a.y * b.y, a.z * b.z};
}
[[nodiscard]] constexpr Vec3 operator/(const Vec3& a, const Vec3& b)
{
    return {a.x / b.x, a.y / b.y, a.z / b.z};
}
[[nodiscard]] constexpr Vec3 operator*(Vec3 v, f32 s)
{
    return v *= s;
}
[[nodiscard]] constexpr Vec3 operator*(f32 s, Vec3 v)
{
    return v *= s;
}
[[nodiscard]] constexpr Vec3 operator/(Vec3 v, f32 s)
{
    return v /= s;
}

[[nodiscard]] constexpr f32 Dot(const Vec3& a, const Vec3& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

// Perpendicular to a and b. With Emerald's left-handed convention (+X right, +Y up, +Z into the
// screen): Cross(X, Y) = Z.
[[nodiscard]] constexpr Vec3 Cross(const Vec3& a, const Vec3& b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

[[nodiscard]] constexpr f32 LengthSquared(const Vec3& v)
{
    return Dot(v, v);
}
[[nodiscard]] inline f32 Length(const Vec3& v)
{
    return std::sqrt(LengthSquared(v));
}
[[nodiscard]] inline f32 Distance(const Vec3& a, const Vec3& b)
{
    return Length(b - a);
}

// Unit-length copy of v; returns (0, 0, 0) for a zero vector instead of NaNs.
[[nodiscard]] inline Vec3 Normalize(const Vec3& v)
{
    const f32 len = Length(v);
    return len > 0.0f ? v / len : Vec3{};
}

[[nodiscard]] constexpr Vec3 Lerp(const Vec3& a, const Vec3& b, f32 t)
{
    return a + (b - a) * t;
}
[[nodiscard]] constexpr Vec3 Min(const Vec3& a, const Vec3& b)
{
    return {Min(a.x, b.x), Min(a.y, b.y), Min(a.z, b.z)};
}
[[nodiscard]] constexpr Vec3 Max(const Vec3& a, const Vec3& b)
{
    return {Max(a.x, b.x), Max(a.y, b.y), Max(a.z, b.z)};
}

[[nodiscard]] inline bool NearlyEqual(const Vec3& a, const Vec3& b, f32 epsilon = Epsilon)
{
    return NearlyEqual(a.x, b.x, epsilon) && NearlyEqual(a.y, b.y, epsilon) &&
           NearlyEqual(a.z, b.z, epsilon);
}

} // namespace Emerald
