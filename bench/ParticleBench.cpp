// Micro-benchmark: ParticleSystem::UpdateScalar vs UpdateSse. Build in Release
// (-DEMERALD_BUILD_BENCH=ON) and run EmeraldParticleBench.
//
// The particles live far longer than the benchmark, so every run updates exactly `count` of them
// and none are removed. Per particle the update reads 7 floats and writes 5 (48 bytes), for about
// 10 arithmetic operations: little math per byte, so large pools end up limited by memory
// bandwidth rather than by how fast the CPU can multiply. Watch how the speedup shrinks from
// 10k (fits in cache) to 1M (does not).
//
// Also note: an optimizing compiler may auto-vectorize the "scalar" loop by itself (GCC and Clang
// do at -O3; GCC 12+ and MSVC at -O2 in some cases), which makes the two paths look alike.

#include <chrono>
#include <cstdio>

#include <Emerald/Particles/ParticleSystem.h>

using namespace Emerald;

namespace {

using Clock = std::chrono::steady_clock;

volatile f32 g_Sink = 0.0f; // keeps the optimizer from deleting unused work

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

void BenchUpdate(u32 count, i32 runs)
{
    const ParticleEmitterConfig config{.Shape = EmitterShape::Circle,
                                       .Radius = 100.0f,
                                       .Speed = {10.0f, 200.0f},
                                       .Lifetime = {1e6f, 1e6f},
                                       .Drag = 0.5f,
                                       .Gravity = {0.0f, 30.0f}};
    ParticleSystem particles(count);
    particles.Emit(config, {0.0f, 0.0f}, count);
    const f32 dt = 1.0f / 60.0f;

    const f64 scalarMs = BestOfMs(runs, [&] {
        particles.UpdateScalar(dt);
        g_Sink = g_Sink + particles.GetPosition(count / 2).x;
    });
    const f64 sseMs = BestOfMs(runs, [&] {
        particles.UpdateSse(dt);
        g_Sink = g_Sink + particles.GetPosition(count / 2).x;
    });
    std::printf("  %8u particles  scalar: %8.4f ms (%5.2f ns/particle)   SSE: %8.4f ms (%5.2f "
                "ns/particle)   speedup %.2fx\n",
                count, scalarMs, scalarMs * 1e6 / count, sseMs, sseMs * 1e6 / count,
                scalarMs / sseMs);
}

} // namespace

int main()
{
    std::printf("Emerald particle update benchmark (best of N runs; SSE2=%d)\n",
                EMERALD_MATH_HAS_SSE2);
    BenchUpdate(1'000, 2000);
    BenchUpdate(10'000, 1000);  // 480 KB of hot data: fits in L2
    BenchUpdate(100'000, 200);  // 4.8 MB: around the size of L3
    BenchUpdate(1'000'000, 40); // 48 MB: main memory
    return 0;
}
