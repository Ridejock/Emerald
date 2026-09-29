// Tests for the std::pmr helpers in include/Emerald/Memory.

#include <cstdint>
#include <memory_resource>
#include <string>

#include <Emerald/Core/ThreadPool.h>
#include <Emerald/Memory/Memory.h>

#include "Test.h"

using namespace Emerald;

namespace {

bool Inside(const void* p, const void* begin, usize size)
{
    const auto* b = static_cast<const std::byte*>(begin);
    const auto* q = static_cast<const std::byte*>(p);
    return q >= b && q < b + size;
}

} // namespace

TEST(FrameArenaResets)
{
    FrameArena arena(4096);
    void* first = arena.allocate(100, 16);
    void* second = arena.allocate(200, 8);
    CHECK(first != second);
    CHECK(arena.GetStats().BytesUsed == 300);
    CHECK(arena.GetStats().Allocations == 2);
    arena.deallocate(second, 200, 8); // no-op for a monotonic arena
    CHECK(arena.GetStats().BytesUsed == 300);

    arena.Reset();
    const FrameArena::Stats stats = arena.GetStats();
    CHECK(stats.BytesUsed == 0 && stats.Allocations == 0);
    CHECK(stats.LastFrameBytes == 300);
    CHECK(stats.PeakBytes == 300);
    // After a reset the arena starts again at the beginning of its buffer.
    CHECK(arena.allocate(100, 16) == first);
}

TEST(FrameArenaAlignment)
{
    FrameArena arena(4096);
    (void)arena.allocate(1, 1);
    void* p = arena.allocate(64, 64);
    CHECK(reinterpret_cast<std::uintptr_t>(p) % 64 == 0);
    auto* m = static_cast<Mat4*>(arena.allocate(sizeof(Mat4), alignof(Mat4)));
    CHECK(reinterpret_cast<std::uintptr_t>(m) % alignof(Mat4) == 0);
}

TEST(FrameArenaOverflowFallsBackToHeap)
{
    FrameArena arena(256);
    void* inBuffer = arena.allocate(128, 8);
    void* big = arena.allocate(1024, 8); // does not fit: comes from the heap, no exception
    CHECK(big != nullptr);
    CHECK(!Inside(big, inBuffer, 256));
    FrameArena::Stats stats = arena.GetStats();
    CHECK(stats.OverflowBytes >= 1024);
    CHECK(stats.OverflowFrames == 1);

    arena.Reset(); // heap chunks are released
    stats = arena.GetStats();
    CHECK(stats.OverflowBytes == 0);
    CHECK(stats.OverflowFrames == 1);
    CHECK(arena.allocate(128, 8) == inBuffer);
}

TEST(PmrContainersUseTheResource)
{
    TrackingResource tracker;
    {
        PmrVector<i32> numbers(&tracker);
        CHECK(numbers.get_allocator().resource() == &tracker);
        for (i32 i = 0; i < 100; ++i)
            numbers.push_back(i);
        CHECK(tracker.GetStats().BytesInUse >= 100 * sizeof(i32));

        PmrString text("a string long enough to not fit the small string buffer", &tracker);
        PmrUnorderedMap<i32, PmrString> map(&tracker);
        map[1] = "one";
        // Elements of pmr containers get the container's resource too.
        CHECK(map[1].get_allocator().resource() == &tracker);
    }
    // Everything was returned.
    CHECK(tracker.GetStats().BytesInUse == 0);
    CHECK(tracker.GetStats().AllocationsInUse == 0);
    CHECK(tracker.GetStats().TotalAllocations > 3);
}

TEST(PmrVectorInFrameArena)
{
    FrameArena arena(64 * 1024);
    PmrVector<Vec4> points(&arena);
    for (i32 i = 0; i < 1000; ++i)
        points.push_back(Vec4(static_cast<f32>(i)));
    CHECK(points[999].x == 999.0f);
    CHECK(arena.GetStats().BytesUsed >= 1000 * sizeof(Vec4));
    CHECK(arena.GetStats().OverflowBytes == 0);
}

TEST(TrackingResourceCounts)
{
    TrackingResource tracker;
    void* a = tracker.allocate(100, 8);
    void* b = tracker.allocate(50, 8);
    TrackingResource::Stats s = tracker.GetStats();
    CHECK(s.BytesInUse == 150 && s.AllocationsInUse == 2 && s.TotalAllocations == 2);
    tracker.deallocate(a, 100, 8);
    s = tracker.GetStats();
    CHECK(s.BytesInUse == 50 && s.AllocationsInUse == 1 && s.PeakBytes == 150);
    tracker.deallocate(b, 50, 8);
    CHECK(tracker.GetStats().BytesInUse == 0);
    CHECK(tracker.GetStats().TotalAllocations == 2);
}

TEST(PoolResourcesOverTracker)
{
    TrackingResource heap;
    {
        std::pmr::unsynchronized_pool_resource pool(&heap);
        PmrVector<i32> v(&pool);
        v.resize(1000);
        CHECK(heap.GetStats().BytesInUse >= 1000 * sizeof(i32)); // pool took a chunk upstream
    }
    CHECK(heap.GetStats().BytesInUse == 0); // the pool returns its chunks when destroyed
}

TEST(SynchronizedPoolAcrossThreads)
{
    TrackingResource heap;
    {
        std::pmr::synchronized_pool_resource shared(&heap);
        ThreadPool workers(4);
        // Every task allocates and frees through the same pool concurrently.
        workers.ParallelFor(64, [&shared](usize i) {
            PmrVector<u64> v(&shared);
            for (u64 k = 0; k < 500; ++k)
                v.push_back(k * i);
            PmrString s(std::to_string(i) + " some text that needs heap memory", &shared);
        });
    }
    CHECK(heap.GetStats().BytesInUse == 0);
    CHECK(heap.GetStats().TotalAllocations > 0);
}
