#pragma once

#include <atomic>
#include <memory_resource>

#include "Emerald/Core/Defines.h"

namespace Emerald {

// A memory_resource that forwards to another one (`upstream`) and counts what goes through it:
// bytes and allocations currently alive, the peak, and the total number of allocations. Put it
// under a pool or container to see how much memory that part of the engine really uses.
// The counters are atomics, so it is safe to use from several threads if the upstream is.
class TrackingResource final : public std::pmr::memory_resource {
public:
    struct Stats {
        usize BytesInUse = 0;
        usize PeakBytes = 0;
        usize AllocationsInUse = 0;
        usize TotalAllocations = 0;
    };

    explicit TrackingResource(std::pmr::memory_resource* upstream = std::pmr::new_delete_resource())
        : m_Upstream(upstream)
    {
    }

    [[nodiscard]] Stats GetStats() const;
    [[nodiscard]] std::pmr::memory_resource* GetUpstream() const { return m_Upstream; }

private:
    void* do_allocate(usize bytes, usize alignment) override;
    void do_deallocate(void* p, usize bytes, usize alignment) override;
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other; // memory from one tracker must be returned to the same tracker
    }

    std::pmr::memory_resource* m_Upstream;
    std::atomic<usize> m_BytesInUse{0};
    std::atomic<usize> m_PeakBytes{0};
    std::atomic<usize> m_AllocationsInUse{0};
    std::atomic<usize> m_TotalAllocations{0};
};

} // namespace Emerald
