#pragma once

#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Emerald/Core/Defines.h"
#include "Emerald/Physics/Collision.h"

namespace Emerald {

// Broadphase: objects (an id and a bounding box) sorted into a grid of square cells, so finding
// what is near something only looks at a few cells instead of every object.
//
//   SpatialHash hash(64.0f);                     // cell size: about the typical object size
//   hash.Insert(id, Aabb::FromCenter(p, {r, r}));
//   hash.Update(id, newBox);                     // after it moved
//   hash.Query(area, ids);                       // ids whose boxes overlap `area`
//   hash.ForEachPair([](u32 a, u32 b) { ... });  // every overlapping pair once (a < b)
//
// Results are sorted by id, so they do not depend on hash map order. Boxes are only candidates:
// run the exact shape test (Collide) on them.
//
// With a wrap size, space repeats like a torus (an object leaving the right edge is next to one
// on the left): boxes and queries crossing an edge find objects on the other side. Positions
// should then be inside 0..wrapSize.
class SpatialHash {
public:
    using Id = u32;

    explicit SpatialHash(f32 cellSize = 64.0f, std::optional<Vec2> wrapSize = std::nullopt);

    // Insert of an existing id updates it.
    void Insert(Id id, const Aabb& box);
    void Update(Id id, const Aabb& box) { Insert(id, box); }
    void Remove(Id id);
    void Clear();
    [[nodiscard]] bool Contains(Id id) const { return m_Objects.contains(id); }
    [[nodiscard]] usize GetCount() const { return m_Objects.size(); }
    [[nodiscard]] usize GetCellCount() const { return m_Cells.size(); }

    // Ids whose boxes overlap `area` (cleared first, sorted).
    void Query(const Aabb& area, std::vector<Id>& out) const;
    // Every pair of objects whose boxes overlap, once each, as (smaller id, larger id), sorted.
    void GetPairs(std::vector<std::pair<Id, Id>>& out) const;
    template <typename Fn> void ForEachPair(Fn&& fn) const
    {
        GetPairs(m_PairScratch);
        for (const auto& [a, b] : m_PairScratch)
            fn(a, b);
    }

private:
    struct CellRange {
        i32 X0, Y0, X1, Y1; // inclusive, before wrapping
        bool operator==(const CellRange&) const = default;
    };
    struct Object {
        Aabb Box;
        CellRange Cells;
    };

    [[nodiscard]] CellRange CellsFor(const Aabb& box) const;
    [[nodiscard]] u64 Key(i32 x, i32 y) const; // wraps x and y when wrapping
    template <typename Fn> void ForEachCell(const CellRange& range, Fn&& fn) const;
    [[nodiscard]] bool BoxesOverlap(const Aabb& a, const Aabb& b) const; // wrap-aware

    Vec2 m_CellSize;
    std::optional<Vec2> m_Wrap;
    i32 m_CellsX = 0; // cells across the wrap size (wrapping only)
    i32 m_CellsY = 0;
    std::unordered_map<Id, Object> m_Objects;
    std::unordered_map<u64, std::vector<Id>> m_Cells;
    mutable std::vector<std::pair<Id, Id>> m_PairScratch;
};

} // namespace Emerald
