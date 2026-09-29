#pragma once

#include <cmath>
#include <span>

#include "Emerald/Math/Common.h"
#include "Emerald/Math/Vec3.h"
#include "Emerald/Math/Vec4.h"

namespace Emerald {

// 4x4 float matrix for 2D/3D transforms and projections.
//
// Conventions (the same as HLSL's defaults, so a Mat4 can be copied into a cbuffer as-is):
//  * Column vectors: a point is transformed as  p' = M * p, so transforms compose right to left.
//    `Projection * Translate * Rotate * Scale` scales first, then rotates, then translates.
//  * Column-major storage: Elements[0..3] is column 0, Elements[4..7] column 1, ... The translation
//    of an affine transform is column 3 (Elements[12..14]). HLSL packs a `float4x4` in a cbuffer
//    column-major by default, and the shaders compute `mul(Transform, float4(position, 1))`
//    (matrix on the left), which is exactly M * p.
//  * Left-handed, like SDL GPU / D3D12 / Metal: +X right, +Y up, +Z into the screen, and the
//    projections map depth to [0, 1] (0 = near plane). RotateZ with a positive angle turns
//    counterclockwise in a y-up space (clockwise on screen with the y-down OrthoPixelSpace).
struct alignas(16) Mat4 {
    f32 Elements[16];

    // Identity.
    constexpr Mat4()
        : Elements{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                   0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}
    {
    }
    // From four columns.
    constexpr Mat4(const Vec4& c0, const Vec4& c1, const Vec4& c2, const Vec4& c3)
        : Elements{c0.x, c0.y, c0.z, c0.w, c1.x, c1.y, c1.z, c1.w,
                   c2.x, c2.y, c2.z, c2.w, c3.x, c3.y, c3.z, c3.w}
    {
    }
    // From four rows, i.e. written the way the matrix looks on paper.
    [[nodiscard]] static constexpr Mat4 FromRows(const Vec4& r0, const Vec4& r1, const Vec4& r2,
                                                 const Vec4& r3)
    {
        return {{r0.x, r1.x, r2.x, r3.x},
                {r0.y, r1.y, r2.y, r3.y},
                {r0.z, r1.z, r2.z, r3.z},
                {r0.w, r1.w, r2.w, r3.w}};
    }

    [[nodiscard]] static constexpr Mat4 Identity() { return {}; }

    // Element at (row, col) in math notation.
    [[nodiscard]] constexpr f32 operator()(usize row, usize col) const
    {
        return Elements[col * 4 + row];
    }
    [[nodiscard]] constexpr f32& operator()(usize row, usize col)
    {
        return Elements[col * 4 + row];
    }

    [[nodiscard]] constexpr Vec4 Column(usize col) const
    {
        const f32* c = &Elements[col * 4];
        return {c[0], c[1], c[2], c[3]};
    }
    [[nodiscard]] constexpr Vec4 Row(usize row) const
    {
        return {Elements[row], Elements[4 + row], Elements[8 + row], Elements[12 + row]};
    }
    constexpr void SetColumn(usize col, const Vec4& v)
    {
        f32* c = &Elements[col * 4];
        c[0] = v.x;
        c[1] = v.y;
        c[2] = v.z;
        c[3] = v.w;
    }

    constexpr bool operator==(const Mat4&) const = default; // exact; see NearlyEqual

    // --- Transforms ------------------------------------------------------------------------

    [[nodiscard]] static constexpr Mat4 Translate(const Vec3& t)
    {
        Mat4 m;
        m.SetColumn(3, {t, 1.0f});
        return m;
    }
    [[nodiscard]] static constexpr Mat4 Translate(const Vec2& t) { return Translate({t, 0.0f}); }

    [[nodiscard]] static constexpr Mat4 Scale(const Vec3& s)
    {
        Mat4 m;
        m(0, 0) = s.x;
        m(1, 1) = s.y;
        m(2, 2) = s.z;
        return m;
    }
    [[nodiscard]] static constexpr Mat4 Scale(const Vec2& s) { return Scale({s, 1.0f}); }
    [[nodiscard]] static constexpr Mat4 Scale(f32 s) { return Scale(Vec3{s}); }

    // Rotation about the Z axis (the only rotation 2D needs), angle in radians.
    [[nodiscard]] static Mat4 RotateZ(f32 radians)
    {
        const f32 c = std::cos(radians), s = std::sin(radians);
        Mat4 m;
        m(0, 0) = c;
        m(0, 1) = -s;
        m(1, 0) = s;
        m(1, 1) = c;
        return m;
    }

    // Rotation about an arbitrary axis (need not be normalized), angle in radians.
    [[nodiscard]] static Mat4 Rotate(f32 radians, const Vec3& axis)
    {
        const Vec3 a = Normalize(axis);
        const f32 c = std::cos(radians), s = std::sin(radians), t = 1.0f - c;
        return FromRows({t * a.x * a.x + c, t * a.x * a.y - s * a.z, t * a.x * a.z + s * a.y, 0.0f},
                        {t * a.x * a.y + s * a.z, t * a.y * a.y + c, t * a.y * a.z - s * a.x, 0.0f},
                        {t * a.x * a.z - s * a.y, t * a.y * a.z + s * a.x, t * a.z * a.z + c, 0.0f},
                        {0.0f, 0.0f, 0.0f, 1.0f});
    }

    // --- Projections ---------------------------------------------------------------------------

    // Orthographic projection: maps x in [left, right] to [-1, 1], y in [bottom, top] to [-1, 1]
    // and z in [zNear, zFar] to [0, 1]. Pass bottom > top for a y-down space.
    [[nodiscard]] static constexpr Mat4 Ortho(f32 left, f32 right, f32 bottom, f32 top,
                                              f32 zNear = -1.0f, f32 zFar = 1.0f)
    {
        Mat4 m;
        m(0, 0) = 2.0f / (right - left);
        m(1, 1) = 2.0f / (top - bottom);
        m(2, 2) = 1.0f / (zFar - zNear);
        m(0, 3) = -(right + left) / (right - left);
        m(1, 3) = -(top + bottom) / (top - bottom);
        m(2, 3) = -zNear / (zFar - zNear);
        return m;
    }

    // 2D pixel space: (0, 0) is the top-left corner, (width, height) the bottom-right, +Y down
    // (like window, mouse and texture coordinates). Note that flipping Y also flips triangle
    // winding (counterclockwise becomes clockwise), which matters once back-face culling is on.
    [[nodiscard]] static constexpr Mat4 OrthoPixelSpace(f32 width, f32 height)
    {
        return Ortho(0.0f, width, height, 0.0f);
    }

    // Perspective projection with a vertical field of view in radians. Points in front of the
    // camera have z > 0 (left-handed); depth maps to [0, 1] between zNear and zFar.
    [[nodiscard]] static Mat4 Perspective(f32 fovY, f32 aspect, f32 zNear, f32 zFar)
    {
        const f32 f = 1.0f / std::tan(fovY * 0.5f);
        Mat4 m = FromRows({}, {}, {}, {}); // all zeros
        m(0, 0) = f / aspect;
        m(1, 1) = f;
        m(2, 2) = zFar / (zFar - zNear);
        m(2, 3) = -zNear * zFar / (zFar - zNear);
        m(3, 2) = 1.0f; // w' = z, so the GPU divides by the view-space depth
        return m;
    }

    // View matrix for a camera at `eye` looking at `target` (left-handed: the camera looks along
    // +Z in view space).
    [[nodiscard]] static Mat4 LookAt(const Vec3& eye, const Vec3& target, const Vec3& up)
    {
        const Vec3 forward = Normalize(target - eye);
        const Vec3 right = Normalize(Cross(up, forward));
        const Vec3 newUp = Cross(forward, right);
        // The rows are the camera axes (rotating the world into camera space); the last column
        // moves the eye to the origin.
        return FromRows({right, -Dot(right, eye)}, {newUp, -Dot(newUp, eye)},
                        {forward, -Dot(forward, eye)}, {0.0f, 0.0f, 0.0f, 1.0f});
    }
};

static_assert(sizeof(Mat4) == 64 && alignof(Mat4) == 16);

namespace Math {

namespace Scalar {

// Each lane is computed as ((c0*x + c1*y) + c2*z) + c3*w, the same order as the SSE version.
inline Vec4 Mul(const Mat4& m, const Vec4& v)
{
    const f32* e = m.Elements;
    Vec4 r;
    for (usize i = 0; i < 4; ++i)
        r[i] = ((e[i] * v.x + e[4 + i] * v.y) + e[8 + i] * v.z) + e[12 + i] * v.w;
    return r;
}

// Column j of a * b is a * (column j of b).
inline Mat4 Mul(const Mat4& a, const Mat4& b)
{
    Mat4 r;
    for (usize j = 0; j < 4; ++j)
        r.SetColumn(j, Mul(a, b.Column(j)));
    return r;
}

inline void TransformBatch(const Mat4& m, std::span<const Vec4> in, std::span<Vec4> out)
{
    const usize count = in.size() < out.size() ? in.size() : out.size();
    for (usize i = 0; i < count; ++i)
        out[i] = Mul(m, in[i]);
}

} // namespace Scalar

#if EMERALD_MATH_HAS_SSE2
namespace Sse {

// M * v as a weighted sum of M's columns: col0 * x + col1 * y + col2 * z + col3 * w.
// Each column is one register, so this is 4 broadcasts, 4 multiplies and 3 adds.
inline __m128 MulColumns(__m128 c0, __m128 c1, __m128 c2, __m128 c3, __m128 v)
{
    __m128 r = _mm_mul_ps(c0, _mm_shuffle_ps(v, v, _MM_SHUFFLE(0, 0, 0, 0)));
    r = _mm_add_ps(r, _mm_mul_ps(c1, _mm_shuffle_ps(v, v, _MM_SHUFFLE(1, 1, 1, 1))));
    r = _mm_add_ps(r, _mm_mul_ps(c2, _mm_shuffle_ps(v, v, _MM_SHUFFLE(2, 2, 2, 2))));
    return _mm_add_ps(r, _mm_mul_ps(c3, _mm_shuffle_ps(v, v, _MM_SHUFFLE(3, 3, 3, 3))));
}

inline Vec4 Mul(const Mat4& m, const Vec4& v)
{
    const f32* e = m.Elements;
    return Store(MulColumns(_mm_load_ps(e), _mm_load_ps(e + 4), _mm_load_ps(e + 8),
                            _mm_load_ps(e + 12), Load(v)));
}

inline Mat4 Mul(const Mat4& a, const Mat4& b)
{
    const f32* e = a.Elements;
    const __m128 c0 = _mm_load_ps(e), c1 = _mm_load_ps(e + 4), c2 = _mm_load_ps(e + 8),
                 c3 = _mm_load_ps(e + 12);
    Mat4 r;
    for (usize j = 0; j < 4; ++j)
        _mm_store_ps(r.Elements + j * 4,
                     MulColumns(c0, c1, c2, c3, _mm_load_ps(b.Elements + j * 4)));
    return r;
}

// The matrix columns are loaded once and stay in registers for the whole batch; this is where
// SIMD helps most (e.g. transforming all sprite vertices on the CPU).
inline void TransformBatch(const Mat4& m, std::span<const Vec4> in, std::span<Vec4> out)
{
    const usize count = in.size() < out.size() ? in.size() : out.size();
    const f32* e = m.Elements;
    const __m128 c0 = _mm_load_ps(e), c1 = _mm_load_ps(e + 4), c2 = _mm_load_ps(e + 8),
                 c3 = _mm_load_ps(e + 12);
    for (usize i = 0; i < count; ++i)
        _mm_store_ps(&out[i].x, MulColumns(c0, c1, c2, c3, Load(in[i])));
}

} // namespace Sse
#endif

} // namespace Math

// --- Operations --------------------------------------------------------------------------------

[[nodiscard]] inline Mat4 operator*(const Mat4& a, const Mat4& b)
{
    return Math::Impl::Mul(a, b);
}
[[nodiscard]] inline Vec4 operator*(const Mat4& m, const Vec4& v)
{
    return Math::Impl::Mul(m, v);
}
inline Mat4& operator*=(Mat4& a, const Mat4& b)
{
    return a = a * b;
}

// out[i] = m * in[i] for every element (up to the shorter of the two spans). in and out may be the
// same memory.
inline void TransformBatch(const Mat4& m, std::span<const Vec4> in, std::span<Vec4> out)
{
    Math::Impl::TransformBatch(m, in, out);
}

// Transforms a point (w = 1, so translation applies). No perspective divide.
[[nodiscard]] inline Vec3 TransformPoint(const Mat4& m, const Vec3& p)
{
    return (m * Vec4{p, 1.0f}).XYZ();
}
// Transforms a direction (w = 0, so translation is ignored).
[[nodiscard]] inline Vec3 TransformDirection(const Mat4& m, const Vec3& d)
{
    return (m * Vec4{d, 0.0f}).XYZ();
}

[[nodiscard]] constexpr Mat4 Transpose(const Mat4& m)
{
    return {m.Row(0), m.Row(1), m.Row(2), m.Row(3)};
}

// Determinant and inverse via 2x2 sub-determinants (cofactor expansion), for any invertible
// matrix, not just rigid transforms.
namespace Math::Detail {
struct Minors {
    f32 s0, s1, s2, s3, s4, s5; // 2x2 determinants of rows 0-1
    f32 c0, c1, c2, c3, c4, c5; // 2x2 determinants of rows 2-3
};
[[nodiscard]] constexpr Minors ComputeMinors(const Mat4& m)
{
    const auto a = [&m](usize r, usize c) { return m(r, c); };
    return {a(0, 0) * a(1, 1) - a(1, 0) * a(0, 1), a(0, 0) * a(1, 2) - a(1, 0) * a(0, 2),
            a(0, 0) * a(1, 3) - a(1, 0) * a(0, 3), a(0, 1) * a(1, 2) - a(1, 1) * a(0, 2),
            a(0, 1) * a(1, 3) - a(1, 1) * a(0, 3), a(0, 2) * a(1, 3) - a(1, 2) * a(0, 3),
            a(2, 0) * a(3, 1) - a(3, 0) * a(2, 1), a(2, 0) * a(3, 2) - a(3, 0) * a(2, 2),
            a(2, 0) * a(3, 3) - a(3, 0) * a(2, 3), a(2, 1) * a(3, 2) - a(3, 1) * a(2, 2),
            a(2, 1) * a(3, 3) - a(3, 1) * a(2, 3), a(2, 2) * a(3, 3) - a(3, 2) * a(2, 3)};
}
[[nodiscard]] constexpr f32 Determinant(const Minors& k)
{
    return k.s0 * k.c5 - k.s1 * k.c4 + k.s2 * k.c3 + k.s3 * k.c2 - k.s4 * k.c1 + k.s5 * k.c0;
}
} // namespace Math::Detail

[[nodiscard]] constexpr f32 Determinant(const Mat4& m)
{
    return Math::Detail::Determinant(Math::Detail::ComputeMinors(m));
}

// Inverse of m, so that m * Inverse(m) == identity. If m is singular (determinant 0) there is no
// inverse and the identity is returned; check Determinant(m) first if that can happen.
[[nodiscard]] constexpr Mat4 Inverse(const Mat4& m)
{
    const Math::Detail::Minors k = Math::Detail::ComputeMinors(m);
    const f32 det = Math::Detail::Determinant(k);
    if (det == 0.0f)
        return {};

    const auto a = [&m](usize r, usize c) { return m(r, c); };
    const f32 inv = 1.0f / det;
    return Mat4::FromRows({(a(1, 1) * k.c5 - a(1, 2) * k.c4 + a(1, 3) * k.c3) * inv,
                           (-a(0, 1) * k.c5 + a(0, 2) * k.c4 - a(0, 3) * k.c3) * inv,
                           (a(3, 1) * k.s5 - a(3, 2) * k.s4 + a(3, 3) * k.s3) * inv,
                           (-a(2, 1) * k.s5 + a(2, 2) * k.s4 - a(2, 3) * k.s3) * inv},
                          {(-a(1, 0) * k.c5 + a(1, 2) * k.c2 - a(1, 3) * k.c1) * inv,
                           (a(0, 0) * k.c5 - a(0, 2) * k.c2 + a(0, 3) * k.c1) * inv,
                           (-a(3, 0) * k.s5 + a(3, 2) * k.s2 - a(3, 3) * k.s1) * inv,
                           (a(2, 0) * k.s5 - a(2, 2) * k.s2 + a(2, 3) * k.s1) * inv},
                          {(a(1, 0) * k.c4 - a(1, 1) * k.c2 + a(1, 3) * k.c0) * inv,
                           (-a(0, 0) * k.c4 + a(0, 1) * k.c2 - a(0, 3) * k.c0) * inv,
                           (a(3, 0) * k.s4 - a(3, 1) * k.s2 + a(3, 3) * k.s0) * inv,
                           (-a(2, 0) * k.s4 + a(2, 1) * k.s2 - a(2, 3) * k.s0) * inv},
                          {(-a(1, 0) * k.c3 + a(1, 1) * k.c1 - a(1, 2) * k.c0) * inv,
                           (a(0, 0) * k.c3 - a(0, 1) * k.c1 + a(0, 2) * k.c0) * inv,
                           (-a(3, 0) * k.s3 + a(3, 1) * k.s1 - a(3, 2) * k.s0) * inv,
                           (a(2, 0) * k.s3 - a(2, 1) * k.s1 + a(2, 2) * k.s0) * inv});
}

[[nodiscard]] inline bool NearlyEqual(const Mat4& a, const Mat4& b, f32 epsilon = Epsilon)
{
    for (usize i = 0; i < 16; ++i) {
        if (!NearlyEqual(a.Elements[i], b.Elements[i], epsilon))
            return false;
    }
    return true;
}

} // namespace Emerald
