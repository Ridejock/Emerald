#include "Emerald/Core/ThreadPool.h"

#if defined(_MSC_VER)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__linux__) || defined(__APPLE__)
#include <pthread.h>
#endif

namespace Emerald {

namespace {

// Names the calling thread for debuggers/profilers; silently does nothing where unsupported.
void SetCurrentThreadName(const std::string& name)
{
#if defined(_MSC_VER)
    // Windows 10 1607+. The name is UTF-16; ours is ASCII so widening each char is enough.
    const std::wstring wide(name.begin(), name.end());
    SetThreadDescription(GetCurrentThread(), wide.c_str());
#elif defined(__APPLE__)
    pthread_setname_np(name.c_str()); // macOS can only name the calling thread
#elif defined(__linux__)
    // Linux limits names to 15 characters (+ terminator); longer names make the call fail.
    pthread_setname_np(pthread_self(), name.substr(0, 15).c_str());
#else
    (void)name;
#endif
}

} // namespace

u32 ThreadPool::DefaultThreadCount()
{
    const u32 hardware = std::thread::hardware_concurrency(); // 0 if unknown
    return hardware > 1 ? hardware - 1 : 1;
}

ThreadPool::ThreadPool(u32 threadCount, std::string namePrefix)
    : m_NamePrefix(std::move(namePrefix))
{
    if (threadCount == 0)
        threadCount = DefaultThreadCount();
    m_Workers.reserve(threadCount);
    for (u32 i = 0; i < threadCount; ++i) {
        // A jthread passes its own stop_token as the first argument when the callable accepts one.
        m_Workers.emplace_back([this, i](std::stop_token stop) { WorkerLoop(stop, i); });
    }
}

ThreadPool::~ThreadPool()
{
    // Ask every worker to stop first so they all wind down in parallel; the jthread destructors
    // (m_Workers is destroyed first, see the header) then join them.
    for (std::jthread& worker : m_Workers)
        worker.request_stop();
}

void ThreadPool::Enqueue(std::function<void()> job)
{
    {
        const std::lock_guard lock(m_Mutex);
        m_Queue.push_back(std::move(job));
    }
    m_WorkAvailable.notify_one(); // notify after unlocking so the woken worker can take the lock
}

void ThreadPool::WaitIdle()
{
    std::unique_lock lock(m_Mutex);
    m_Idle.wait(lock, [this] { return m_Queue.empty() && m_Running == 0; });
}

void ThreadPool::WorkerLoop(const std::stop_token& stop, u32 index)
{
    SetCurrentThreadName(m_NamePrefix + std::to_string(index));

    while (true) {
        std::function<void()> job;
        {
            std::unique_lock lock(m_Mutex);
            // Sleeps until there is work or a stop is requested (the stop_token wakes it).
            m_WorkAvailable.wait(lock, stop, [this] { return !m_Queue.empty(); });
            if (stop.stop_requested())
                return; // queued tasks are dropped; their futures report broken_promise
            job = std::move(m_Queue.front());
            m_Queue.pop_front();
            ++m_Running;
        }

        job(); // packaged_task: exceptions end up in the future, never escape here

        {
            const std::lock_guard lock(m_Mutex);
            --m_Running;
            if (m_Running == 0 && m_Queue.empty())
                m_Idle.notify_all();
        }
    }
}

} // namespace Emerald
