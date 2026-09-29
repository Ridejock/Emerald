// Tests for include/Emerald/Math. Built twice by CMake: with the SIMD setting from
// EMERALD_MATH_SIMD, and (when that is ON) again with EMERALD_MATH_SIMD=0.

#include <array>
#include <random>
#include <vector>

#include "Test.h"

using namespace Emerald;

namespace {

// Deterministic pseudo-random values in [lo, hi).
struct Random {
    std::mt19937 Engine{1234};
    f32 Next(f32 lo = -10.0f, f32 hi = 10.0f)
    {
        return std::uniform_real_distribution<f32>(lo, hi)(Engine);
    }
    Vec4 NextVec4() { return {Next(), Next(), Next(), Next()}; }
    Mat4 NextMat4() { return {NextVec4(), NextVec4(), NextVec4(), NextVec4()}; }
};

} // namespace

// --- Scalar helpers -----------------------------------------------------------------------------

TEST(ScalarHelpers)
{
    CHECK_NEAR(ToRadians(180.0f), Pi);
    CHECK_NEAR(ToDegrees(HalfPi), 90.0f);
    CHECK(Clamp(5, 0, 3) == 3);
    CHECK(Clamp(-1.0f, 0.0f, 1.0f) == 0.0f);
    CHECK(Clamp(0.5f, 0.0f, 1.0f) == 0.5f);
    CHECK_NEAR(Lerp(2.0f, 4.0f, 0.25f), 2.5f);
    CHECK(NearlyEqual(1.0f, 1.0f + 1e-6f));
    CHECK(!NearlyEqual(1.0f, 1.001f));
    CHECK(NearlyEqualRelative(100000.0f, 100000.5f));
    CHECK(NearlyZero(1e-7f));
}

// --- Vectors ------------------------------------------------------------------------------------

TEST(Vec2Operations)
{
    const Vec2 a(1.0f, 2.0f), b(3.0f, -4.0f);
    CHECK(a + b == Vec2(4.0f, -2.0f));
    CHECK(a - b == Vec2(-2.0f, 6.0f));
    CHECK(a * 2.0f == Vec2(2.0f, 4.0f));
    CHECK(2.0f * a == a * 2.0f);
    CHECK(a * b == Vec2(3.0f, -8.0f));
    CHECK(b / 2.0f == Vec2(1.5f, -2.0f));
    CHECK(-a == Vec2(-1.0f, -2.0f));
    CHECK(Dot(a, b) == -5.0f);
    CHECK(Cross(Vec2(1.0f, 0.0f), Vec2(0.0f, 1.0f)) == 1.0f);
    CHECK_NEAR(Length(b), 5.0f);
    CHECK_NEAR(Normalize(b), Vec2(0.6f, -0.8f));
    CHECK(Normalize(Vec2()) == Vec2()); // zero stays zero, no NaN
    CHECK_NEAR(Lerp(a, b, 0.5f), Vec2(2.0f, -1.0f));
    CHECK(Min(a, b) == Vec2(1.0f, -4.0f));
    CHECK(Max(a, b) == Vec2(3.0f, 2.0f));

    Vec2 c = a;
    c += b;
    c -= Vec2(1.0f);
    c *= 2.0f;
    CHECK(c == Vec2(6.0f, -6.0f));
}

TEST(Vec2iOperations)
{
    const Vec2i a(3, 4), b(-1, 2);
    CHECK(a + b == Vec2i(2, 6));
    CHECK(a - b == Vec2i(4, 2));
    CHECK(a * 2 == Vec2i(6, 8));
    CHECK(static_cast<Vec2>(a) == Vec2(3.0f, 4.0f));
}

TEST(Vec3Operations)
{
    const Vec3 x(1.0f, 0.0f, 0.0f), y(0.0f, 1.0f, 0.0f), z(0.0f, 0.0f, 1.0f);
    CHECK(Cross(x, y) == z);
    CHECK(Cross(y, z) == x);
    CHECK(Cross(z, x) == y);
    CHECK(Dot(x, y) == 0.0f);

    const Vec3 a(1.0f, 2.0f, 3.0f), b(4.0f, 5.0f, 6.0f);
    CHECK(a + b == Vec3(5.0f, 7.0f, 9.0f));
    CHECK(b - a == Vec3(3.0f));
    CHECK(Dot(a, b) == 32.0f);
    CHECK_NEAR(Length(Vec3(2.0f, 3.0f, 6.0f)), 7.0f);
    CHECK_NEAR(Length(Normalize(b)), 1.0f);
    CHECK_NEAR(Lerp(a, b, 1.0f / 3.0f), Vec3(2.0f, 3.0f, 4.0f));
    CHECK(Vec3(Vec2(1.0f, 2.0f), 3.0f) == a);
    CHECK(a.XY() == Vec2(1.0f, 2.0f));
}

TEST(Vec4Operations)
{
    const Vec4 a(1.0f, 2.0f, 3.0f, 4.0f), b(-2.0f, 0.5f, 4.0f, 1.0f);
    CHECK(a + b == Vec4(-1.0f, 2.5f, 7.0f, 5.0f));
    CHECK(a - b == Vec4(3.0f, 1.5f, -1.0f, 3.0f));
    CHECK(a * b == Vec4(-2.0f, 1.0f, 12.0f, 4.0f));
    CHECK(a / Vec4(2.0f) == Vec4(0.5f, 1.0f, 1.5f, 2.0f));
    CHECK(a * 2.0f == Vec4(2.0f, 4.0f, 6.0f, 8.0f));
    CHECK(0.5f * a == a / 2.0f);
    CHECK(-a == Vec4(-1.0f, -2.0f, -3.0f, -4.0f));
    CHECK(Dot(a, b) == 15.0f);
    CHECK_NEAR(Length(Vec4(1.0f, 1.0f, 1.0f, 1.0f)), 2.0f);
    CHECK_NEAR(Normalize(Vec4(0.0f, 3.0f, 0.0f, 4.0f)), Vec4(0.0f, 0.6f, 0.0f, 0.8f));
    CHECK(Normalize(Vec4()) == Vec4());
    CHECK_NEAR(Lerp(a, b, 0.5f), Vec4(-0.5f, 1.25f, 3.5f, 2.5f));
    CHECK(a[0] == 1.0f && a[3] == 4.0f);
    CHECK(Vec4(Vec3(1.0f, 2.0f, 3.0f), 4.0f) == a);
    CHECK(a.XYZ() == Vec3(1.0f, 2.0f, 3.0f));

    Vec4 c = a;
    c += b;
    c -= a;
    c *= 2.0f;
    c /= 4.0f;
    CHECK(c == b * 0.5f);
}

// --- Mat4 ---------------------------------------------------------------------------------------

TEST(Mat4LayoutMatchesHlsl)
{
    // Column-major: translation lives in Elements[12..14], i.e. the fourth float4 of the cbuffer.
    const Mat4 t = Mat4::Translate(Vec3(5.0f, 6.0f, 7.0f));
    CHECK(t.Elements[12] == 5.0f && t.Elements[13] == 6.0f && t.Elements[14] == 7.0f);
    CHECK(t(0, 3) == 5.0f); // row 0, column 3
    CHECK(t.Column(3) == Vec4(5.0f, 6.0f, 7.0f, 1.0f));
    CHECK(sizeof(Mat4) == 64 && alignof(Mat4) == 16);

    const Mat4 rows = Mat4::FromRows({1, 2, 3, 4}, {5, 6, 7, 8}, {9, 10, 11, 12}, {13, 14, 15, 16});
    CHECK(rows(1, 2) == 7.0f);
    CHECK(rows.Row(2) == Vec4(9.0f, 10.0f, 11.0f, 12.0f));
    CHECK(rows.Column(0) == Vec4(1.0f, 5.0f, 9.0f, 13.0f));
    CHECK(Transpose(rows).Row(0) == rows.Column(0));
    CHECK(Transpose(Transpose(rows)) == rows);
}

TEST(Mat4Multiply)
{
    const Mat4 a = Mat4::FromRows({1, 2, 3, 4}, {5, 6, 7, 8}, {9, 10, 11, 12}, {13, 14, 15, 16});
    const Mat4 b = Mat4::FromRows({2, 0, 0, 1}, {0, 1, 0, 0}, {1, 0, 3, 0}, {0, 0, 0, 1});
    // Computed by hand: (a * b)(r, c) = sum_k a(r, k) * b(k, c).
    const Mat4 expected =
        Mat4::FromRows({5, 2, 9, 5}, {17, 6, 21, 13}, {29, 10, 33, 21}, {41, 14, 45, 29});
    CHECK(a * b == expected);
    CHECK(a * Mat4::Identity() == a);
    CHECK(Mat4::Identity() * a == a);
    CHECK(a * Vec4(1.0f, 0.0f, 0.0f, 0.0f) == a.Column(0));
    CHECK(a * Vec4(1.0f, 1.0f, 1.0f, 1.0f) == Vec4(10.0f, 26.0f, 42.0f, 58.0f));

    Random rng;
    for (i32 i = 0; i < 100; ++i) {
        const Mat4 m1 = rng.NextMat4(), m2 = rng.NextMat4(), m3 = rng.NextMat4();
        const Vec4 v = rng.NextVec4();
        CHECK_NEAR_EPS((m1 * m2) * m3, m1 * (m2 * m3), 0.05f); // associativity (values ~1e3)
        CHECK_NEAR_EPS((m1 * m2) * v, m1 * (m2 * v), 0.05f);
    }
}

TEST(Mat4Transforms)
{
    const Vec3 p(1.0f, 2.0f, 3.0f);
    CHECK(TransformPoint(Mat4::Translate(Vec3(10.0f, 0.0f, -1.0f)), p) == Vec3(11.0f, 2.0f, 2.0f));
    CHECK(TransformDirection(Mat4::Translate(Vec3(10.0f, 0.0f, 0.0f)), p) == p);
    CHECK(TransformPoint(Mat4::Scale(Vec3(2.0f, 3.0f, 4.0f)), p) == Vec3(2.0f, 6.0f, 12.0f));
    CHECK(TransformPoint(Mat4::Scale(Vec2(2.0f, 3.0f)), p) == Vec3(2.0f, 6.0f, 3.0f));

    // +90 degrees about Z turns +X into +Y (counterclockwise in a y-up space).
    CHECK_NEAR(TransformPoint(Mat4::RotateZ(HalfPi), Vec3(1.0f, 0.0f, 0.0f)),
               Vec3(0.0f, 1.0f, 0.0f));
    CHECK_NEAR(Mat4::Rotate(0.7f, Vec3(0.0f, 0.0f, 2.0f)), Mat4::RotateZ(0.7f));
    CHECK_NEAR(TransformPoint(Mat4::Rotate(HalfPi, Vec3(1.0f, 0.0f, 0.0f)), Vec3(0.0f, 1.0f, 0.0f)),
               Vec3(0.0f, 0.0f, 1.0f));

    // Right-to-left composition: scale, then rotate, then translate.
    const Mat4 model = Mat4::Translate(Vec2(100.0f, 50.0f)) * Mat4::RotateZ(HalfPi) *
                       Mat4::Scale(Vec2(10.0f, 10.0f));
    CHECK_NEAR(TransformPoint(model, Vec3(1.0f, 0.0f, 0.0f)), Vec3(100.0f, 60.0f, 0.0f));
}

TEST(Mat4Inverse)
{
    const Mat4 t = Mat4::Translate(Vec3(3.0f, -4.0f, 5.0f));
    CHECK_NEAR(Inverse(t), Mat4::Translate(Vec3(-3.0f, 4.0f, -5.0f)));
    CHECK_NEAR(Inverse(Mat4::Scale(Vec3(2.0f, 4.0f, 8.0f))),
               Mat4::Scale(Vec3(0.5f, 0.25f, 0.125f)));
    CHECK_NEAR(Inverse(Mat4::RotateZ(0.3f)), Transpose(Mat4::RotateZ(0.3f)));

    const Mat4 a = Mat4::FromRows({2, 0, 0, 1}, {0, 1, 0, 0}, {1, 0, 3, 0}, {0, 0, 0, 1});
    CHECK_NEAR(Determinant(a), 6.0f);
    CHECK_NEAR(a * Inverse(a), Mat4::Identity());

    // Singular (two equal rows): determinant 0, Inverse returns identity by contract.
    const Mat4 singular = Mat4::FromRows({1, 2, 3, 4}, {1, 2, 3, 4}, {0, 1, 0, 0}, {0, 0, 0, 1});
    CHECK(Determinant(singular) == 0.0f);
    CHECK(Inverse(singular) == Mat4::Identity());

    // General matrices: random ones are well conditioned enough for a loose tolerance.
    Random rng;
    for (i32 i = 0; i < 200; ++i) {
        const Mat4 m = rng.NextMat4();
        if (std::fabs(Determinant(m)) < 1.0f)
            continue;
        CHECK_NEAR_EPS(m * Inverse(m), Mat4::Identity(), 1e-3f);
        CHECK_NEAR_EPS(Inverse(m) * m, Mat4::Identity(), 1e-3f);
    }
}

TEST(Mat4Ortho)
{
    // Pixel space for an 800x600 window: top-left -> (-1, 1), bottom-right -> (1, -1).
    const Mat4 p = Mat4::OrthoPixelSpace(800.0f, 600.0f);
    CHECK_NEAR(p * Vec4(0.0f, 0.0f, 0.0f, 1.0f), Vec4(-1.0f, 1.0f, 0.5f, 1.0f));
    CHECK_NEAR(p * Vec4(800.0f, 600.0f, 0.0f, 1.0f), Vec4(1.0f, -1.0f, 0.5f, 1.0f));
    CHECK_NEAR(p * Vec4(400.0f, 300.0f, 0.0f, 1.0f), Vec4(0.0f, 0.0f, 0.5f, 1.0f));

    // Generic y-up ortho: depth maps zNear -> 0, zFar -> 1.
    const Mat4 o = Mat4::Ortho(-2.0f, 2.0f, -1.0f, 1.0f, 0.1f, 10.0f);
    CHECK_NEAR(o * Vec4(2.0f, 1.0f, 0.1f, 1.0f), Vec4(1.0f, 1.0f, 0.0f, 1.0f));
    CHECK_NEAR(o * Vec4(-2.0f, -1.0f, 10.0f, 1.0f), Vec4(-1.0f, -1.0f, 1.0f, 1.0f));
    CHECK_NEAR(Inverse(o) * (o * Vec4(0.3f, -0.2f, 5.0f, 1.0f)), Vec4(0.3f, -0.2f, 5.0f, 1.0f));
}

TEST(Mat4PerspectiveAndLookAt)
{
    const f32 zNear = 0.1f, zFar = 100.0f;
    const Mat4 proj = Mat4::Perspective(ToRadians(90.0f), 16.0f / 9.0f, zNear, zFar);
    const Vec4 nearPoint = proj * Vec4(0.0f, 0.0f, zNear, 1.0f);
    const Vec4 farPoint = proj * Vec4(0.0f, 0.0f, zFar, 1.0f);
    CHECK_NEAR(nearPoint.w, zNear); // w = view depth
    CHECK_NEAR(nearPoint.z / nearPoint.w, 0.0f);
    CHECK_NEAR(farPoint.z / farPoint.w, 1.0f);
    // 90 degree vertical FOV: a point at y = z is on the top edge.
    const Vec4 top = proj * Vec4(0.0f, 5.0f, 5.0f, 1.0f);
    CHECK_NEAR(top.y / top.w, 1.0f);

    // Camera at z = -5 looking at the origin (left-handed: along +Z).
    const Mat4 view = Mat4::LookAt(Vec3(0.0f, 0.0f, -5.0f), Vec3(0.0f), Vec3(0.0f, 1.0f, 0.0f));
    CHECK_NEAR(TransformPoint(view, Vec3(0.0f)), Vec3(0.0f, 0.0f, 5.0f));
    CHECK_NEAR(TransformPoint(view, Vec3(1.0f, 0.0f, 0.0f)), Vec3(1.0f, 0.0f, 5.0f));
    CHECK_NEAR(TransformPoint(view, Vec3(0.0f, 0.0f, -5.0f)), Vec3(0.0f)); // eye -> origin
}

// --- Scalar vs SIMD -----------------------------------------------------------------------------

TEST(PublicApiUsesSelectedImplementation)
{
    std::printf("    EMERALD_MATH_SIMD=%d, SSE2 available=%d, SSE4.1=%d -> public API uses %s\n",
                EMERALD_MATH_SIMD, EMERALD_MATH_HAS_SSE2, EMERALD_MATH_HAS_SSE41,
                EMERALD_MATH_USE_SSE ? "SSE" : "scalar");
    Random rng;
    const Mat4 m = rng.NextMat4();
    const Vec4 v = rng.NextVec4();
    CHECK(m * v == Math::Impl::Mul(m, v));
#if !EMERALD_MATH_USE_SSE
    CHECK(m * v == Math::Scalar::Mul(m, v));
#endif
}

TEST(ScalarAndSimdAgree)
{
#if EMERALD_MATH_HAS_SSE2
    namespace S = Math::Scalar;
    namespace V = Math::Sse;
    // Both implementations add in the same order, so on SSE2 they should agree exactly; the
    // tolerance only matters if the compiler contracts to FMA or SSE4.1's dot product is used.
    const f32 eps = 1e-5f;
    Random rng;
    usize exact = 0, total = 0;
    const auto count = [&](bool same) {
        exact += same ? 1 : 0;
        ++total;
    };
    for (i32 i = 0; i < 1000; ++i) {
        const Vec4 a = rng.NextVec4(), b = rng.NextVec4();
        const f32 s = rng.Next();
        const Mat4 m1 = rng.NextMat4(), m2 = rng.NextMat4();

        CHECK_NEAR_EPS(S::Add(a, b), V::Add(a, b), eps);
        CHECK_NEAR_EPS(S::Sub(a, b), V::Sub(a, b), eps);
        CHECK_NEAR_EPS(S::Mul(a, b), V::Mul(a, b), eps);
        CHECK_NEAR_EPS(S::Div(a, b), V::Div(a, b), eps);
        CHECK_NEAR_EPS(S::Scale(a, s), V::Scale(a, s), eps);
        CHECK_NEAR_EPS(S::Lerp(a, b, 0.3f), V::Lerp(a, b, 0.3f), eps);
        CHECK_NEAR_EPS(S::Normalize(a), V::Normalize(a), eps);
        CHECK(NearlyEqualRelative(S::Dot(a, b), V::Dot(a, b), eps));
        CHECK_NEAR_EPS(S::Mul(m1, a), V::Mul(m1, a), 1e-3f);
        CHECK(NearlyEqual(S::Mul(m1, m2), V::Mul(m1, m2), 1e-3f));

        count(S::Dot(a, b) == V::Dot(a, b));
        count(S::Mul(m1, a) == V::Mul(m1, a));
        count(S::Mul(m1, m2) == V::Mul(m1, m2));
    }

    std::vector<Vec4> in(1024), outScalar(1024), outSimd(1024);
    for (Vec4& v : in)
        v = rng.NextVec4();
    const Mat4 m = rng.NextMat4();
    S::TransformBatch(m, in, outScalar);
    V::TransformBatch(m, in, outSimd);
    for (usize i = 0; i < in.size(); ++i) {
        CHECK_NEAR_EPS(outScalar[i], outSimd[i], 1e-3f);
        count(outScalar[i] == outSimd[i]);
    }
    // Public batch API, in place (in and out may alias).
    std::vector<Vec4> inPlace = in;
    TransformBatch(m, inPlace, inPlace);
    CHECK(inPlace == (EMERALD_MATH_USE_SSE ? outSimd : outScalar));

    std::printf("    %zu of %zu scalar/SSE results bit-identical\n", exact, total);
#else
    std::printf("    no SSE2 on this target: only the scalar implementation exists\n");
#endif
}
