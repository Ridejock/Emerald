#pragma once

#include <memory>
#include <memory_resource>
#include <optional>
#include <thread>

#include "Emerald/Core/Defines.h"

namespace Emerald {

// Per-frame scratch memory: allocation is a pointer bump inside a preallocated buffer, freeing
// individual blocks does nothing, and Reset() makes the whole buffer available again at once.
// Ideal for temporary data that lives for one frame (draw lists, visible-entity lists, strings).
//
//   PmrVector<Mat4> transforms(app.GetFrameAllocator()); // valid until the next frame starts
//
// Built on std::pmr::monotonic_buffer_resource. Application owns one and calls Reset() at the
// start of every frame, so memory from it must not be kept across frames. Reset() rebuilds the
// monotonic resource instead of calling its release(): MSVC's release() does not go back to the
// initial buffer (LWG 3120, microsoft/STL#1468), which sent every frame after the first to the
// heap there.
//
// Overflow policy: if a frame needs more than `capacity` bytes, the extra comes from the heap
// (new/delete) instead of failing, and a warning is logged the first time it happens. Using
// std::pmr::null_memory_resource() as the upstream would throw std::bad_alloc in the middle of a
// frame instead - a crash for what is usually just a spike. The heap memory is returned at the
// next Reset(). GetStats() shows how close you are to the limit; if the warning appears, raise
// ApplicationSpec::FrameArenaSize.
//
// Thread safety: NOT thread-safe. Only the thread that created it (the main thread for
// Application's arena) may allocate from it or reset it; debug builds assert this. Workers that
// need scratch memory should get their own arena (or use a synchronized pool).
class FrameArena final : public std::pmr::memory_resource {
public:
    struct Stats {
        usize Capacity = 0;       // size of the preallocated buffer
        usize BytesUsed = 0;      // bytes requested since the last Reset (without padding)
        usize Allocations = 0;    // allocations since the last Reset
        usize LastFrameBytes = 0; // BytesUsed at the last Reset, i.e. of the previous frame
        usize PeakBytes = 0;      // highest BytesUsed of any frame so far
        usize OverflowBytes = 0;  // heap memory taken since the last Reset (0 = fits)
        u64 OverflowFrames = 0;   // frames whose allocations did not fit and used the heap
    };

    explicit FrameArena(usize capacity);
    ~FrameArena() override;

    FrameArena(const FrameArena&) = delete;
    FrameArena& operator=(const FrameArena&) = delete;

    // Frees everything allocated since the last Reset and rewinds to the start of the buffer.
    void Reset();

    [[nodiscard]] Stats GetStats() const;

private:
    // Upstream of the monotonic resource: only called when the buffer is full. Counts the bytes,
    // warns once, and gets the memory from new/delete.
    class OverflowResource final : public std::pmr::memory_resource {
    public:
        usize Bytes = 0;
        bool Warned = false;
        usize Capacity = 0;

    private:
        void* do_allocate(usize bytes, usize alignment) override;
        void do_deallocate(void* p, usize bytes, usize alignment) override;
        bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
        {
            return this == &other;
        }
    };

    void* do_allocate(usize bytes, usize alignment) override;
    void do_deallocate(void* p, usize bytes, usize alignment) override;
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }
    void CheckThread() const;

    std::unique_ptr<std::byte[]> m_Buffer;
    usize m_Capacity;
    OverflowResource m_Overflow;
    // Declared after its buffer and upstream; optional so Reset() can rebuild it in place.
    std::optional<std::pmr::monotonic_buffer_resource> m_Arena;
    usize m_BytesUsed = 0;
    usize m_Allocations = 0;
    usize m_LastFrameBytes = 0;
    usize m_PeakBytes = 0;
    u64 m_OverflowFrames = 0;
    std::thread::id m_Owner;
};

} // namespace Emerald
