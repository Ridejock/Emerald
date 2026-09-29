#include "Emerald/Memory/TrackingResource.h"

namespace Emerald {

void* TrackingResource::do_allocate(usize bytes, usize alignment)
{
    void* p = m_Upstream->allocate(bytes, alignment); // may throw; count only on success
    const usize inUse = m_BytesInUse.fetch_add(bytes, std::memory_order_relaxed) + bytes;
    m_AllocationsInUse.fetch_add(1, std::memory_order_relaxed);
    m_TotalAllocations.fetch_add(1, std::memory_order_relaxed);

    // Raise the peak if we are above it. compare_exchange retries if another thread changed it.
    usize peak = m_PeakBytes.load(std::memory_order_relaxed);
    while (inUse > peak &&
           !m_PeakBytes.compare_exchange_weak(peak, inUse, std::memory_order_relaxed)) {
    }
    return p;
}

void TrackingResource::do_deallocate(void* p, usize bytes, usize alignment)
{
    m_Upstream->deallocate(p, bytes, alignment);
    m_BytesInUse.fetch_sub(bytes, std::memory_order_relaxed);
    m_AllocationsInUse.fetch_sub(1, std::memory_order_relaxed);
}

TrackingResource::Stats TrackingResource::GetStats() const
{
    // Relaxed loads: each counter is exact, but a snapshot taken while other threads allocate may
    // mix values from slightly different moments. Fine for statistics.
    return {m_BytesInUse.load(std::memory_order_relaxed),
            m_PeakBytes.load(std::memory_order_relaxed),
            m_AllocationsInUse.load(std::memory_order_relaxed),
            m_TotalAllocations.load(std::memory_order_relaxed)};
}

} // namespace Emerald
