// Tests for Emerald::ThreadPool.

#include <atomic>
#include <chrono>
#include <future>
#include <numeric>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <Emerald/Core/ThreadPool.h>

#include "Test.h"

using Emerald::ThreadPool;
using namespace std::chrono_literals;

TEST(ThreadCount)
{
    CHECK(ThreadPool::DefaultThreadCount() >= 1);
    const ThreadPool automatic;
    CHECK(automatic.GetThreadCount() == ThreadPool::DefaultThreadCount());
    const ThreadPool three(3);
    CHECK(three.GetThreadCount() == 3);
}

TEST(SubmitReturnsValues)
{
    ThreadPool pool(4);
    std::vector<std::future<i32>> futures;
    for (i32 i = 0; i < 100; ++i)
        futures.push_back(pool.Submit([i] { return i * i; }));
    i32 sum = 0;
    for (auto& f : futures)
        sum += f.get();
    CHECK(sum == 328350); // sum of squares 0..99

    std::future<std::string> text = pool.Submit([] { return std::string("hello"); });
    CHECK(text.get() == "hello");

    // Tasks really run on other threads.
    std::future<std::thread::id> id = pool.Submit([] { return std::this_thread::get_id(); });
    CHECK(id.get() != std::this_thread::get_id());
}

TEST(ExceptionsPropagateThroughFuture)
{
    ThreadPool pool(2);
    std::future<i32> failing = pool.Submit([]() -> i32 { throw std::runtime_error("boom"); });
    bool caught = false;
    try {
        (void)failing.get();
    } catch (const std::runtime_error& e) {
        caught = std::string(e.what()) == "boom";
    }
    CHECK(caught);
    // The worker survived the exception.
    CHECK(pool.Submit([] { return 7; }).get() == 7);
}

TEST(WaitIdleWaitsForAllTasks)
{
    ThreadPool pool(4);
    std::atomic<i32> done{0};
    for (i32 i = 0; i < 64; ++i) {
        (void)pool.Submit([&done] {
            std::this_thread::sleep_for(1ms);
            done.fetch_add(1);
        });
    }
    pool.WaitIdle();
    CHECK(done.load() == 64);
    pool.WaitIdle(); // idle already: returns immediately
}

TEST(ShutdownWithPendingTasks)
{
    std::atomic<i32> ran{0};
    std::vector<std::future<void>> futures;
    {
        ThreadPool pool(2);
        for (i32 i = 0; i < 200; ++i) {
            futures.push_back(pool.Submit([&ran] {
                std::this_thread::sleep_for(2ms);
                ran.fetch_add(1);
            }));
        }
        std::this_thread::sleep_for(5ms);
    } // destructor: running tasks finish, queued ones are dropped, workers are joined

    // Running all tasks would take 200 x 2 ms / 2 threads = 200 ms; most were dropped instead.
    i32 completed = 0, dropped = 0;
    for (auto& f : futures) {
        try {
            f.get();
            ++completed;
        } catch (const std::future_error& e) {
            if (e.code() == std::future_errc::broken_promise)
                ++dropped;
        }
    }
    CHECK(completed == ran.load());
    CHECK(completed + dropped == 200);
    CHECK(dropped > 0);
}

TEST(ParallelForCoversEveryIndexOnce)
{
    ThreadPool pool(4);
    for (const usize count :
         {usize{0}, usize{1}, usize{7}, usize{64}, usize{1000}, usize{100003}}) {
        std::vector<std::atomic<i32>> hits(count);
        pool.ParallelFor(count, [&hits](usize i) { hits[i].fetch_add(1); });
        bool allOnce = true;
        for (const auto& h : hits)
            allOnce = allOnce && h.load() == 1;
        CHECK(allOnce);
    }

    // Results written per index, then combined on the caller.
    std::vector<u64> squares(10000);
    pool.ParallelFor(squares.size(), [&squares](usize i) { squares[i] = u64{i} * i; });
    CHECK(std::accumulate(squares.begin(), squares.end(), u64{0}) == 333283335000ULL);
}

TEST(ParallelForPropagatesExceptions)
{
    ThreadPool pool(3);
    std::atomic<i32> visited{0};
    bool caught = false;
    try {
        pool.ParallelFor(1000, [&visited](usize i) {
            visited.fetch_add(1);
            if (i == 500)
                throw std::logic_error("bad index");
        });
    } catch (const std::logic_error&) {
        caught = true;
    }
    CHECK(caught);
    CHECK(visited.load() >= 1);
    CHECK(pool.Submit([] { return true; }).get()); // pool still usable
}

TEST(SingleWorkerRunsInOrder)
{
    ThreadPool pool(1);
    std::vector<i32> order; // only the single worker touches it
    for (i32 i = 0; i < 10; ++i)
        (void)pool.Submit([&order, i] { order.push_back(i); });
    pool.WaitIdle();
    bool inOrder = order.size() == 10;
    for (i32 i = 0; inOrder && i < 10; ++i)
        inOrder = order[static_cast<usize>(i)] == i;
    CHECK(inOrder);
}
