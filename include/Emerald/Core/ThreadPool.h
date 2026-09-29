#pragma once

#include <condition_variable>
#include <deque>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "Emerald/Core/Defines.h"

namespace Emerald {

// A fixed set of worker threads that run submitted tasks in FIFO order.
//
//   ThreadPool pool;                                    // hardware threads - 1 workers
//   std::future<i32> answer = pool.Submit([] { return 42; });
//   pool.ParallelFor(items.size(), [&](usize i) { Process(items[i]); });
//   answer.get();                                       // 42 (or rethrows the task's exception)
//
// How it works: tasks wait in a queue guarded by a mutex. Idle workers sleep on a
// std::condition_variable_any, whose wait() also takes the worker's std::stop_token, so a stop
// request wakes them up. The workers are std::jthreads: when the pool is destroyed they are asked
// to stop and are joined automatically - there is no hand-written join/shutdown flag.
//
// Shutdown policy: the destructor lets running tasks finish but drops tasks still in the queue;
// their futures then throw std::future_error (broken_promise). Call WaitIdle() first if every task
// must complete.
//
// Rule: do not call WaitIdle() or ParallelFor() from inside a task. A worker waiting for work
// that only workers can run may deadlock when all workers do the same.
class ThreadPool {
public:
    // threadCount = 0 picks DefaultThreadCount(). Threads are named "<namePrefix><index>" where the
    // OS supports it (visible in debuggers and profilers).
    explicit ThreadPool(u32 threadCount = 0, std::string namePrefix = "Emerald-W");
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // hardware_concurrency - 1 (the main thread keeps a core), at least 1.
    [[nodiscard]] static u32 DefaultThreadCount();
    [[nodiscard]] u32 GetThreadCount() const { return static_cast<u32>(m_Workers.size()); }

    // Queues `task` (any callable taking no arguments) and returns a future for its result.
    // An exception thrown by the task is stored in the future and rethrown by future.get().
    template <typename F>
    [[nodiscard]] std::future<std::invoke_result_t<std::decay_t<F>&>> Submit(F&& task);

    // Blocks until the queue is empty and no task is running.
    void WaitIdle();

    // Calls fn(i) for every i in [0, count), split into contiguous chunks that run on the workers
    // and on the calling thread. Returns when all indices are done; rethrows the first exception.
    // `fn` runs concurrently, so it must only touch data that belongs to index i (or is shared
    // read-only / synchronized).
    template <typename F> void ParallelFor(usize count, F&& fn);

private:
    void Enqueue(std::function<void()> job);
    void WorkerLoop(const std::stop_token& stop, u32 index);

    std::mutex m_Mutex;
    std::condition_variable_any m_WorkAvailable; // _any: its wait() accepts a stop_token
    std::condition_variable m_Idle;
    std::deque<std::function<void()>> m_Queue;
    u32 m_Running = 0; // tasks currently executing
    std::string m_NamePrefix;

    // Declared last so it is destroyed first: the jthreads are stopped and joined while the mutex,
    // condition variables and queue above still exist.
    std::vector<std::jthread> m_Workers;
};

template <typename F>
std::future<std::invoke_result_t<std::decay_t<F>&>> ThreadPool::Submit(F&& task)
{
    using Result = std::invoke_result_t<std::decay_t<F>&>;
    // packaged_task connects the callable to a future (value or exception). It is move-only, but
    // std::function needs a copyable callable, hence the shared_ptr.
    auto packaged = std::make_shared<std::packaged_task<Result()>>(std::forward<F>(task));
    std::future<Result> future = packaged->get_future();
    Enqueue([packaged] { (*packaged)(); });
    return future;
}

template <typename F> void ThreadPool::ParallelFor(usize count, F&& fn)
{
    if (count == 0)
        return;

    // A few chunks per thread (workers + caller) balances uneven work without much overhead.
    const usize threads = static_cast<usize>(GetThreadCount()) + 1;
    const usize chunkCount = count < threads * 4 ? count : threads * 4;
    const usize chunkSize = (count + chunkCount - 1) / chunkCount;

    const auto runChunk = [&fn](usize begin, usize end) {
        for (usize i = begin; i < end; ++i)
            fn(i);
    };

    std::vector<std::future<void>> futures;
    futures.reserve(chunkCount);
    usize begin = 0;
    for (; begin + chunkSize < count; begin += chunkSize) {
        const usize end = begin + chunkSize;
        futures.push_back(Submit([&runChunk, begin, end] { runChunk(begin, end); }));
    }

    // The caller does the last chunk instead of just waiting.
    std::exception_ptr error;
    try {
        runChunk(begin, count);
    } catch (...) {
        error = std::current_exception();
    }
    // Wait for every chunk (they reference fn) before returning or rethrowing.
    for (std::future<void>& future : futures) {
        try {
            future.get();
        } catch (...) {
            if (!error)
                error = std::current_exception();
        }
    }
    if (error)
        std::rethrow_exception(error);
}

} // namespace Emerald
