// Micro-benchmark: scalar vs SSE for Mat4 * Vec4 over a large array (TransformBatch) and for
// Mat4 * Mat4. Build in Release (-DEMERALD_BUILD_BENCH=ON) and run EmeraldMathBench.
//
// Treat the numbers as rough: timings vary between runs and machines, and optimizers may
// auto-vectorize parts of the "scalar" loop anyway.

#include <chrono>
#include <cstdio>
#include <random>
#include <vector>

#include <Emerald/Math/Math.h>

using namespace Emerald;

namespace {

using Clock = std::chrono::steady_clock;

// Keeps the optimizer from deleting work whose result is otherwise unused.
volatile f32 g_Sink = 0.0f;

template <typename F> f64 BestOfMs(i32 runs, F&& body)
{
    f64 best = 1e30;
    for (i32 i = 0; i < runs; ++i) {
        const auto start = Clock::now();
        body();
        const f64 ms = std::chrono::duration<f64, std::milli>(Clock::now() - start).count();
        best = ms < best ? ms : best;
    }
    return best;
}

void BenchTransform(usize count, i32 repeats)
{
    std::mt19937 rng(42);
    std::uniform_real_distribution<f32> dist(-1.0f, 1.0f);
    std::vector<Vec4> in(count), out(count);
    for (Vec4& v : in)
        v = {dist(rng), dist(rng), dist(rng), 1.0f};
    const Mat4 m = Mat4::Translate(Vec3(1.0f, 2.0f, 3.0f)) * Mat4::RotateZ(0.5f) *
                   Mat4::Scale(Vec3(2.0f, 2.0f, 2.0f));

    const f64 scalarMs = BestOfMs(repeats, [&] {
        Math::Scalar::TransformBatch(m, in, out);
        g_Sink = g_Sink + out[count / 2].x;
    });
    std::printf("  TransformBatch %8zu x Vec4   scalar: %8.3f ms (%5.2f ns/vec)\n", count, scalarMs,
                scalarMs * 1e6 / static_cast<f64>(count));
#if EMERALD_MATH_HAS_SSE2
    const f64 simdMs = BestOfMs(repeats, [&] {
        Math::Sse::TransformBatch(m, in, out);
        g_Sink = g_Sink + out[count / 2].x;
    });
    std::printf("  TransformBatch %8zu x Vec4   SSE:    %8.3f ms (%5.2f ns/vec)  speedup %.2fx\n",
                count, simdMs, simdMs * 1e6 / static_cast<f64>(count), scalarMs / simdMs);
#endif
}

void BenchMatMul(usize count, i32 repeats)
{
    std::mt19937 rng(7);
    std::uniform_real_distribution<f32> dist(-1.0f, 1.0f);
    std::vector<Mat4> a(count), out(count);
    for (Mat4& m : a)
        for (f32& e : m.Elements)
            e = dist(rng);
    const Mat4 view = Mat4::LookAt(Vec3(0.0f, 0.0f, -5.0f), Vec3(0.0f), Vec3(0.0f, 1.0f, 0.0f));

    const f64 scalarMs = BestOfMs(repeats, [&] {
        for (usize i = 0; i < count; ++i)
            out[i] = Math::Scalar::Mul(view, a[i]);
        g_Sink = g_Sink + out[count / 2].Elements[5];
    });
    std::printf("  Mat4 * Mat4    %8zu x        scalar: %8.3f ms (%5.2f ns/mul)\n", count, scalarMs,
                scalarMs * 1e6 / static_cast<f64>(count));
#if EMERALD_MATH_HAS_SSE2
    const f64 simdMs = BestOfMs(repeats, [&] {
        for (usize i = 0; i < count; ++i)
            out[i] = Math::Sse::Mul(view, a[i]);
        g_Sink = g_Sink + out[count / 2].Elements[5];
    });
    std::printf("  Mat4 * Mat4    %8zu x        SSE:    %8.3f ms (%5.2f ns/mul)  speedup %.2fx\n",
                count, simdMs, simdMs * 1e6 / static_cast<f64>(count), scalarMs / simdMs);
#endif
}

} // namespace

int main()
{
    std::printf("Emerald math micro-benchmark (best of N runs; SSE2=%d, SSE4.1=%d)\n",
                EMERALD_MATH_HAS_SSE2, EMERALD_MATH_HAS_SSE41);
    BenchTransform(10'000, 200);   // fits in L1/L2
    BenchTransform(1'000'000, 20); // 16 MB in + 16 MB out: memory bound
    BenchMatMul(100'000, 50);
    return 0;
}
