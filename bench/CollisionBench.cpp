// Micro-benchmark: SpatialHash with 10k moving circles vs a brute-force O(n^2) pair check. Build in
// Release (-DEMERALD_BUILD_BENCH=ON) and run EmeraldCollisionBench.
//
// The circles (radius 2..8) are spread over a 2000 x 2000 world, roughly the density of a busy
// game screen scaled up, so most objects have a few neighbours. Each "frame" moves every object a
// little, updates the hash, finds the candidate pairs and runs the exact circle test on them.

#include <chrono>
#include <cstdio>
#include <random>
#include <vector>

#include <Emerald/Physics/Collision.h>
#include <Emerald/Physics/SpatialHash.h>

using namespace Emerald;

namespace {

using Clock = std::chrono::steady_clock;

volatile usize g_Sink = 0; // keeps the optimizer from deleting unused work

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

Aabb Bounds(const Circle& c)
{
    return Aabb::FromCenter(c.Center, {c.Radius, c.Radius});
}

void Bench(usize count, f32 world, f32 cellSize, bool bruteForce)
{
    std::mt19937 rng(7);
    std::uniform_real_distribution<f32> pos(0.0f, world), radius(2.0f, 8.0f), step(-1.0f, 1.0f);
    std::vector<Circle> circles(count);
    for (Circle& c : circles)
        c = {{pos(rng), pos(rng)}, radius(rng)};

    SpatialHash hash(cellSize);
    const f64 insertMs = BestOfMs(5, [&] {
        hash.Clear();
        for (u32 i = 0; i < count; ++i)
            hash.Insert(i, Bounds(circles[i]));
    });

    const f64 updateMs = BestOfMs(5, [&] {
        for (u32 i = 0; i < count; ++i) {
            circles[i].Center += Vec2(step(rng), step(rng));
            hash.Update(i, Bounds(circles[i]));
        }
    });

    std::vector<u32> ids;
    usize found = 0;
    const f64 queryMs = BestOfMs(5, [&] {
        found = 0;
        for (u32 i = 0; i < 1000; ++i) {
            hash.Query(Aabb::FromCenter(circles[i].Center, {32.0f, 32.0f}), ids);
            found += ids.size();
        }
    });
    g_Sink = g_Sink + found;

    std::vector<std::pair<u32, u32>> pairs;
    usize contacts = 0;
    const f64 pairsMs = BestOfMs(5, [&] {
        hash.GetPairs(pairs);
        contacts = 0;
        for (const auto& [a, b] : pairs)
            contacts += Overlaps(circles[a], circles[b]) ? 1 : 0;
    });

    std::printf("  %zu objects, cell %.0f: %zu cells, %zu box pairs, %zu circle contacts\n", count,
                static_cast<f64>(cellSize), hash.GetCellCount(), pairs.size(), contacts);
    std::printf("    insert all       %8.3f ms\n", insertMs);
    std::printf("    move + update    %8.3f ms\n", updateMs);
    std::printf("    1000 queries     %8.3f ms (%.1f hits each)\n", queryMs,
                static_cast<f64>(found) / 1000.0);
    std::printf("    pairs + narrow   %8.3f ms\n", pairsMs);

    if (bruteForce) {
        usize bruteContacts = 0;
        const f64 bruteMs = BestOfMs(3, [&] {
            bruteContacts = 0;
            for (usize i = 0; i < count; ++i)
                for (usize j = i + 1; j < count; ++j)
                    bruteContacts += Overlaps(circles[i], circles[j]) ? 1 : 0;
        });
        std::printf(
            "    brute force      %8.3f ms (%zu contacts, %s), %.0fx slower than the hash\n",
            bruteMs, bruteContacts, bruteContacts == contacts ? "same" : "DIFFERENT",
            bruteMs / (updateMs + pairsMs));
    }
}

} // namespace

int main()
{
    std::printf("SpatialHash vs brute force (best of several runs)\n");
    Bench(10000, 2000.0f, 16.0f, true);
    Bench(10000, 2000.0f, 32.0f, false);
    Bench(10000, 2000.0f, 64.0f, false);
    Bench(100000, 6300.0f, 32.0f, false);
    return 0;
}
