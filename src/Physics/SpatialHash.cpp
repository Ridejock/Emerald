#include "Emerald/Physics/SpatialHash.h"

#include "Emerald/Core/Log.h"

#include <algorithm>
#include <cmath>

namespace Emerald {

namespace {
// Cell coordinates are kept within +-kMaxCell, so the float-to-int conversion and the cell
// loops can never overflow.
constexpr f32 kMaxCell = 1048576.0f; // 2^20

// A box covering more cells than this is not stored (it would take forever to file).
constexpr i64 kMaxCellsPerBox = 1 << 16;

bool IsFinite(const Aabb& box)
{
    return std::isfinite(box.Min.x) && std::isfinite(box.Min.y) && std::isfinite(box.Max.x) &&
           std::isfinite(box.Max.y);
}

i64 CellCount(i32 x0, i32 y0, i32 x1, i32 y1)
{
    if (x1 < x0 || y1 < y0)
        return 0;
    return (static_cast<i64>(x1) - x0 + 1) * (static_cast<i64>(y1) - y0 + 1);
}

// Two intervals [aLo, aHi] and [bLo, bHi] overlap on a circle of `length` (strictly).
bool WrappedOverlap(f32 aLo, f32 aHi, f32 bLo, f32 bHi, f32 length)
{
    const f32 half = 0.5f * ((aHi - aLo) + (bHi - bLo));
    if (half * 2.0f >= length)
        return true; // together they cover the whole circle
    f32 d = std::fmod(0.5f * ((bLo + bHi) - (aLo + aHi)), length);
    if (d < 0.0f)
        d += length;
    return std::min(d, length - d) < half;
}
} // namespace

SpatialHash::SpatialHash(f32 cellSize, std::optional<Vec2> wrapSize)
    : m_CellSize(std::max(cellSize, 1e-3f)), m_Wrap(wrapSize)
{
    if (m_Wrap) {
        // A whole number of cells across, so the grid lines up at the wrap edges.
        m_CellsX = std::max(1, static_cast<i32>(std::lround(m_Wrap->x / cellSize)));
        m_CellsY = std::max(1, static_cast<i32>(std::lround(m_Wrap->y / cellSize)));
        m_CellSize = {m_Wrap->x / static_cast<f32>(m_CellsX),
                      m_Wrap->y / static_cast<f32>(m_CellsY)};
    }
}

SpatialHash::CellRange SpatialHash::CellsFor(const Aabb& box) const
{
    // Clamped as floats first, like Tilemap::GetTileRange.
    const auto cell = [](f32 v, f32 size) {
        return static_cast<i32>(Clamp(std::floor(v / size), -kMaxCell, kMaxCell));
    };
    CellRange r{cell(box.Min.x, m_CellSize.x), cell(box.Min.y, m_CellSize.y),
                cell(box.Max.x, m_CellSize.x), cell(box.Max.y, m_CellSize.y)};
    if (m_Wrap) {
        // Never more than one lap (each cell once).
        r.X1 = std::min(r.X1, r.X0 + m_CellsX - 1);
        r.Y1 = std::min(r.Y1, r.Y0 + m_CellsY - 1);
    }
    return r;
}

u64 SpatialHash::Key(i32 x, i32 y) const
{
    if (m_Wrap) {
        x = ((x % m_CellsX) + m_CellsX) % m_CellsX;
        y = ((y % m_CellsY) + m_CellsY) % m_CellsY;
    }
    return (static_cast<u64>(static_cast<u32>(x)) << 32) | static_cast<u32>(y);
}

template <typename Fn> void SpatialHash::ForEachCell(const CellRange& range, Fn&& fn) const
{
    for (i32 y = range.Y0; y <= range.Y1; ++y)
        for (i32 x = range.X0; x <= range.X1; ++x)
            fn(Key(x, y));
}

bool SpatialHash::BoxesOverlap(const Aabb& a, const Aabb& b) const
{
    if (!m_Wrap)
        return Overlaps(a, b);
    return WrappedOverlap(a.Min.x, a.Max.x, b.Min.x, b.Max.x, m_Wrap->x) &&
           WrappedOverlap(a.Min.y, a.Max.y, b.Min.y, b.Max.y, m_Wrap->y);
}

void SpatialHash::Insert(Id id, const Aabb& box)
{
    if (!IsFinite(box)) { // e.g. a NaN position: skipped rather than filed somewhere random
        EM_CORE_WARN("SpatialHash: skipped object {} with a non-finite box", id);
        Remove(id);
        return;
    }
    const CellRange cells = CellsFor(box);
    if (CellCount(cells.X0, cells.Y0, cells.X1, cells.Y1) > kMaxCellsPerBox) {
        EM_CORE_WARN("SpatialHash: skipped object {}, its box covers too many cells", id);
        Remove(id);
        return;
    }
    if (const auto it = m_Objects.find(id); it != m_Objects.end()) {
        if (it->second.Cells == cells) { // still in the same cells: just the new box
            it->second.Box = box;
            return;
        }
        Remove(id);
    }
    m_Objects[id] = {box, cells};
    ForEachCell(cells, [&](u64 key) { m_Cells[key].push_back(id); });
}

void SpatialHash::Remove(Id id)
{
    const auto it = m_Objects.find(id);
    if (it == m_Objects.end())
        return;
    ForEachCell(it->second.Cells, [&](u64 key) {
        const auto cell = m_Cells.find(key);
        if (cell == m_Cells.end())
            return;
        std::vector<Id>& ids = cell->second;
        const auto at = std::find(ids.begin(), ids.end(), id);
        if (at != ids.end()) {
            *at = ids.back();
            ids.pop_back();
        }
        if (ids.empty())
            m_Cells.erase(cell);
    });
    m_Objects.erase(it);
}

void SpatialHash::Clear()
{
    m_Objects.clear();
    m_Cells.clear();
}

void SpatialHash::Query(const Aabb& area, std::vector<Id>& out) const
{
    out.clear();
    if (!IsFinite(area)) {
        EM_CORE_WARN("SpatialHash: ignored a query with a non-finite area");
        return;
    }
    const CellRange cells = CellsFor(area);
    if (CellCount(cells.X0, cells.Y0, cells.X1, cells.Y1) > static_cast<i64>(m_Cells.size())) {
        // A huge area: checking every object is quicker than visiting every cell.
        for (const auto& [id, object] : m_Objects)
            if (BoxesOverlap(area, object.Box))
                out.push_back(id);
        std::sort(out.begin(), out.end());
        return;
    }
    ForEachCell(cells, [&](u64 key) {
        const auto cell = m_Cells.find(key);
        if (cell == m_Cells.end())
            return;
        for (Id id : cell->second)
            if (BoxesOverlap(area, m_Objects.at(id).Box))
                out.push_back(id);
    });
    // An object spanning several cells was found once per cell.
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
}

void SpatialHash::GetPairs(std::vector<std::pair<Id, Id>>& out) const
{
    out.clear();
    for (const auto& [key, ids] : m_Cells) {
        for (usize i = 0; i < ids.size(); ++i) {
            const Object& a = m_Objects.at(ids[i]);
            for (usize j = i + 1; j < ids.size(); ++j) {
                if (BoxesOverlap(a.Box, m_Objects.at(ids[j]).Box))
                    out.emplace_back(std::min(ids[i], ids[j]), std::max(ids[i], ids[j]));
            }
        }
    }
    // Pairs sharing several cells were found once per shared cell.
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
}

} // namespace Emerald
