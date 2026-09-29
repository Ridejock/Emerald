#include "Emerald/Memory/FrameArena.h"

#include <cassert>

#include "Emerald/Core/Log.h"

namespace Emerald {

FrameArena::FrameArena(usize capacity)
    : m_Buffer(std::make_unique<std::byte[]>(capacity)), m_Capacity(capacity),
      m_Arena(m_Buffer.get(), capacity, &m_Overflow), m_Owner(std::this_thread::get_id())
{
    m_Overflow.Capacity = capacity;
}

FrameArena::~FrameArena() = default;

void FrameArena::CheckThread() const
{
    // Catches the most likely misuse (allocating from a worker thread) in debug builds.
    assert(std::this_thread::get_id() == m_Owner &&
           "FrameArena used from a thread that doesn't own it");
}

void* FrameArena::do_allocate(usize bytes, usize alignment)
{
    CheckThread();
    void* p = m_Arena.allocate(bytes, alignment);
    m_BytesUsed += bytes;
    ++m_Allocations;
    if (m_BytesUsed > m_PeakBytes)
        m_PeakBytes = m_BytesUsed;
    return p;
}

void FrameArena::do_deallocate(void* p, usize bytes, usize alignment)
{
    // A monotonic resource ignores individual frees; memory comes back on Reset().
    m_Arena.deallocate(p, bytes, alignment);
}

void FrameArena::Reset()
{
    CheckThread();
    if (m_Overflow.Bytes > 0)
        ++m_OverflowFrames;
    // release() hands the overflow chunks back to the heap and rewinds to the initial buffer.
    m_Arena.release();
    m_Overflow.Bytes = 0;
    m_LastFrameBytes = m_BytesUsed;
    m_BytesUsed = 0;
    m_Allocations = 0;
}

FrameArena::Stats FrameArena::GetStats() const
{
    const u64 overflowFrames = m_OverflowFrames + (m_Overflow.Bytes > 0 ? 1 : 0);
    return {m_Capacity,  m_BytesUsed,      m_Allocations, m_LastFrameBytes,
            m_PeakBytes, m_Overflow.Bytes, overflowFrames};
}

void* FrameArena::OverflowResource::do_allocate(usize bytes, usize alignment)
{
    if (!Warned) {
        Warned = true;
        if (Log::Core()) // the arena also works without the logger (e.g. in tests)
            EM_CORE_WARN("FrameArena overflow: a frame needed more than {} bytes, using the heap "
                         "for the rest. Increase ApplicationSpec::FrameArenaSize.",
                         Capacity);
    }
    Bytes += bytes;
    return std::pmr::new_delete_resource()->allocate(bytes, alignment);
}

void FrameArena::OverflowResource::do_deallocate(void* p, usize bytes, usize alignment)
{
    std::pmr::new_delete_resource()->deallocate(p, bytes, alignment);
}

} // namespace Emerald
