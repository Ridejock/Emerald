// Collision: shape overlap with normal and depth (incl. touching, containment and degenerate
// cases), raycasts against each shape, and the spatial hash (insert/update/remove, queries,
// pairs, wrapping) checked against brute force, and its guards against NaN and huge boxes.

#include <algorithm>
#include <array>
#include <cmath>
#include <random>
#include <type_traits>
#include <utility>
#include <vector>

#include <Emerald/Core/Log.h>
#include <Emerald/Physics/Collision.h>
#include <Emerald/Physics/SpatialHash.h>

#include "Test.h"

using namespace Emerald;

namespace {

// A square of half size h around c, counterclockwise in a y-up frame.
std::array<Vec2, 4> Square(const Vec2& c, f32 h)
{
    return {{c + Vec2(-h, -h), c + Vec2(h, -h), c + Vec2(h, h), c + Vec2(-h, h)}};
}

// Applying a contact separates the shapes (b moved by normal * depth, plus a hair).
template <typename A, typename B> bool Separates(const A& a, B b, const Contact& c)
{
    if constexpr (std::is_same_v<B, Circle>)
        b.Center += c.Normal * (c.Depth + 1e-3f);
    else {
        b.Min += c.Normal * (c.Depth + 1e-3f);
        b.Max += c.Normal * (c.Depth + 1e-3f);
    }
    return !Collide(a, b).has_value();
}

} // namespace

TEST(CollideCircles)
{
    const Circle a{{0.0f, 0.0f}, 10.0f};
    const auto c = Collide(a, Circle{{15.0f, 0.0f}, 10.0f});
    CHECK(c && NearlyEqual(c->Normal, Vec2(1.0f, 0.0f)) && NearlyEqual(c->Depth, 5.0f));
    CHECK(c && Separates(a, Circle{{15.0f, 0.0f}, 10.0f}, *c));
    // Normal points from a to b in any direction.
    const auto diagonal = Collide(a, Circle{{-6.0f, -8.0f}, 1.0f});
    CHECK(diagonal && NearlyEqual(diagonal->Normal, Vec2(-0.6f, -0.8f)) &&
          NearlyEqual(diagonal->Depth, 1.0f));
    // Touching is not overlapping; same center picks +X with the full depth.
    CHECK(!Collide(a, Circle{{20.0f, 0.0f}, 10.0f}));
    CHECK(!Overlaps(a, Circle{{20.0f, 0.0f}, 10.0f}));
    const auto same = Collide(a, Circle{{0.0f, 0.0f}, 4.0f});
    CHECK(same && NearlyEqual(same->Normal, Vec2(1.0f, 0.0f)) && NearlyEqual(same->Depth, 14.0f));
    // Points (radius 0): a point inside a circle overlaps it, two points never do.
    CHECK(Overlaps(Circle{{3.0f, 4.0f}, 0.0f}, a));
    CHECK(!Overlaps(Circle{{1.0f, 1.0f}, 0.0f}, Circle{{1.0f, 1.0f}, 0.0f}));
}

TEST(CollideCircleBox)
{
    const Aabb box{{0.0f, 0.0f}, {10.0f, 10.0f}};
    // Outside, near a side and near a corner.
    const auto side = Collide(Circle{{-3.0f, 5.0f}, 5.0f}, box);
    CHECK(side && NearlyEqual(side->Normal, Vec2(1.0f, 0.0f)) && NearlyEqual(side->Depth, 2.0f));
    const auto corner = Collide(Circle{{13.0f, 14.0f}, 6.0f}, box);
    CHECK(corner && NearlyEqual(corner->Normal, Vec2(-0.6f, -0.8f)) &&
          NearlyEqual(corner->Depth, 1.0f));
    CHECK(!Collide(Circle{{14.0f, 14.0f}, 5.0f}, box)); // just outside the corner
    CHECK(!Collide(Circle{{-5.0f, 5.0f}, 5.0f}, box));  // touching
    // Center inside: out through the nearest side.
    const auto inside = Collide(Circle{{8.0f, 5.0f}, 1.0f}, box);
    CHECK(inside && NearlyEqual(inside->Normal, Vec2(-1.0f, 0.0f)) &&
          NearlyEqual(inside->Depth, 3.0f));
    CHECK(inside && Separates(Circle{{8.0f, 5.0f}, 1.0f}, box, *inside));
    // The other order flips the normal.
    const auto flipped = Collide(box, Circle{{-3.0f, 5.0f}, 5.0f});
    CHECK(flipped && NearlyEqual(flipped->Normal, Vec2(-1.0f, 0.0f)));
    CHECK(Overlaps(Circle{{5.0f, 5.0f}, 0.0f}, box));  // a point inside
    CHECK(!Overlaps(Circle{{0.0f, 5.0f}, 0.0f}, box)); // a point on the edge only touches
}

TEST(CollideBoxes)
{
    const Aabb a{{0.0f, 0.0f}, {10.0f, 10.0f}};
    const Aabb b{{8.0f, 3.0f}, {20.0f, 6.0f}};
    const auto c = Collide(a, b);
    CHECK(c && NearlyEqual(c->Normal, Vec2(1.0f, 0.0f)) && NearlyEqual(c->Depth, 2.0f));
    CHECK(c && Separates(a, b, *c));
    const auto up = Collide(a, Aabb{{2.0f, -4.0f}, {8.0f, 1.0f}});
    CHECK(up && NearlyEqual(up->Normal, Vec2(0.0f, -1.0f)) && NearlyEqual(up->Depth, 1.0f));
    CHECK(!Collide(a, Aabb{{10.0f, 0.0f}, {20.0f, 10.0f}})); // sharing an edge
    CHECK(!Overlaps(a, Aabb{{10.0f, 0.0f}, {20.0f, 10.0f}}));
    // Contained: the depth is the full push out (not the overlap width), the shorter way.
    const Aabb inside{{7.0f, 4.0f}, {8.0f, 6.0f}};
    const auto inner = Collide(a, inside);
    CHECK(inner && NearlyEqual(inner->Normal, Vec2(1.0f, 0.0f)) && NearlyEqual(inner->Depth, 3.0f));
    CHECK(inner && Separates(a, inside, *inner));
    const Aabb low{{3.0f, 8.0f}, {7.0f, 9.0f}}; // nearer the bottom: pushed down
    const auto down = Collide(a, low);
    CHECK(down && NearlyEqual(down->Normal, Vec2(0.0f, 1.0f)) && NearlyEqual(down->Depth, 2.0f));
    CHECK(down && Separates(a, low, *down));
    const auto outer = Collide(inside, a); // a containing box is pushed off the inner one
    CHECK(outer && NearlyEqual(outer->Normal, Vec2(-1.0f, 0.0f)) && Separates(inside, a, *outer));
}

TEST(CollidePolygons)
{
    const auto a = Square({0.0f, 0.0f}, 5.0f);
    const auto b = Square({8.0f, 1.0f}, 5.0f);
    const auto c = Collide(a, b);
    CHECK(c && NearlyEqual(c->Normal, Vec2(1.0f, 0.0f)) && NearlyEqual(c->Depth, 2.0f));
    // Clockwise points give the same answer.
    std::array<Vec2, 4> reversed = b;
    std::reverse(reversed.begin(), reversed.end());
    const auto r = Collide(a, reversed);
    CHECK(r && NearlyEqual(r->Normal, Vec2(1.0f, 0.0f)) && NearlyEqual(r->Depth, 2.0f));
    // A triangle poking into the square's top from above (y-up).
    const std::array<Vec2, 3> tri{{{-2.0f, 4.0f}, {2.0f, 4.0f}, {0.0f, 9.0f}}};
    const auto t = Collide(a, tri);
    CHECK(t && NearlyEqual(t->Normal, Vec2(0.0f, 1.0f)) && NearlyEqual(t->Depth, 1.0f));
    // A diamond near the corner: separated along its own diagonal edge axis.
    const std::array<Vec2, 4> diamond{{{9.0f, 9.0f}, {11.0f, 7.0f}, {13.0f, 9.0f}, {11.0f, 11.0f}}};
    CHECK(!Collide(a, diamond));
    const std::array<Vec2, 4> close{{{3.5f, 6.0f}, {6.0f, 3.5f}, {8.5f, 6.0f}, {6.0f, 8.5f}}};
    const auto k = Collide(a, close); // overlaps only at the corner
    CHECK(k && k->Normal.x > 0.0f && k->Normal.y > 0.0f && NearlyEqual(k->Normal.x, k->Normal.y));
    CHECK(!Collide(a, Square({10.0f, 0.0f}, 5.0f))); // touching
    const std::array<Vec2, 2> segment{{{0.0f, 0.0f}, {1.0f, 1.0f}}};
    CHECK(!Collide(a, segment)); // fewer than 3 points
}

TEST(RaycastCircle)
{
    const Circle c{{10.0f, 0.0f}, 2.0f};
    const auto hit = Raycast(Ray{{0.0f, 0.0f}, {1.0f, 0.0f}}, c);
    CHECK(hit && NearlyEqual(hit->Distance, 8.0f) && NearlyEqual(hit->Point, Vec2(8.0f, 0.0f)) &&
          NearlyEqual(hit->Normal, Vec2(-1.0f, 0.0f)));
    // The direction is normalized, so distance is in world units.
    const auto scaled = Raycast(Ray{{0.0f, 0.0f}, {5.0f, 0.0f}}, c);
    CHECK(scaled && NearlyEqual(scaled->Distance, 8.0f));
    CHECK(!Raycast(Ray{{0.0f, 0.0f}, {-1.0f, 0.0f}}, c));             // pointing away
    CHECK(!Raycast(Ray{{0.0f, 3.0f}, {1.0f, 0.0f}}, c));              // passes above
    CHECK(!Raycast(Ray{{0.0f, 0.0f}, {1.0f, 0.0f}, 7.9f}, c));        // too short
    CHECK(Raycast(Ray{{0.0f, 0.0f}, {1.0f, 0.0f}, 8.0f}, c));         // exactly long enough
    const auto inside = Raycast(Ray{{10.0f, 0.0f}, {0.0f, 1.0f}}, c); // starts inside
    CHECK(inside && inside->Distance == 0.0f && NearlyEqual(inside->Normal, Vec2(0.0f, -1.0f)));
    const auto zero = Raycast(Ray{{0.0f, 0.0f}, {0.0f, 0.0f}}, c); // no direction: +X
    CHECK(zero && NearlyEqual(zero->Distance, 8.0f));
}

TEST(RaycastBox)
{
    const Aabb box{{5.0f, -1.0f}, {7.0f, 1.0f}};
    const auto hit = Raycast(Ray{{0.0f, 0.0f}, {1.0f, 0.0f}}, box);
    CHECK(hit && NearlyEqual(hit->Distance, 5.0f) && NearlyEqual(hit->Normal, Vec2(-1.0f, 0.0f)));
    const auto top = Raycast(Ray{{6.0f, 10.0f}, {0.0f, -1.0f}}, box);
    CHECK(top && NearlyEqual(top->Distance, 9.0f) && NearlyEqual(top->Normal, Vec2(0.0f, 1.0f)));
    const auto diagonal = Raycast(Ray{{0.0f, -5.0f}, {1.0f, 1.0f}}, box);
    CHECK(diagonal && NearlyEqual(diagonal->Point, Vec2(5.0f, 0.0f)) &&
          NearlyEqual(diagonal->Normal, Vec2(-1.0f, 0.0f)));
    CHECK(!Raycast(Ray{{0.0f, 2.0f}, {1.0f, 0.0f}}, box)); // parallel, outside the slab
    CHECK(!Raycast(Ray{{8.0f, 0.0f}, {1.0f, 0.0f}}, box)); // behind
    CHECK(!Raycast(Ray{{0.0f, 0.0f}, {1.0f, 0.0f}, 4.0f}, box));
    const auto inside = Raycast(Ray{{6.0f, 0.0f}, {1.0f, 0.0f}}, box);
    CHECK(inside && inside->Distance == 0.0f);
}

TEST(RaycastPolygon)
{
    const auto square = Square({10.0f, 0.0f}, 2.0f);
    const auto hit = Raycast(Ray{{0.0f, 0.0f}, {1.0f, 0.0f}}, square);
    CHECK(hit && NearlyEqual(hit->Distance, 8.0f) && NearlyEqual(hit->Normal, Vec2(-1.0f, 0.0f)));
    // Same answer with the points clockwise.
    std::array<Vec2, 4> reversed = square;
    std::reverse(reversed.begin(), reversed.end());
    const auto r = Raycast(Ray{{0.0f, 0.0f}, {1.0f, 0.0f}}, reversed);
    CHECK(r && NearlyEqual(r->Distance, 8.0f) && NearlyEqual(r->Normal, Vec2(-1.0f, 0.0f)));
    // A triangle's slanted side.
    const std::array<Vec2, 3> tri{{{0.0f, 5.0f}, {5.0f, 0.0f}, {5.0f, 5.0f}}};
    const auto slant = Raycast(Ray{{0.0f, 0.0f}, {1.0f, 1.0f}}, tri);
    const f32 s = 0.70710678f;
    CHECK(slant && NearlyEqual(slant->Point, Vec2(2.5f, 2.5f)) &&
          NearlyEqual(slant->Normal, Vec2(-s, -s)));
    CHECK(!Raycast(Ray{{0.0f, 3.0f}, {1.0f, 0.0f}}, square));
    CHECK(!Raycast(Ray{{20.0f, 0.0f}, {1.0f, 0.0f}}, square));
    const auto inside = Raycast(Ray{{10.0f, 0.0f}, {0.0f, 1.0f}}, square);
    CHECK(inside && inside->Distance == 0.0f);
}

namespace {

std::vector<std::pair<u32, u32>> BrutePairs(const std::vector<Aabb>& boxes)
{
    std::vector<std::pair<u32, u32>> pairs;
    for (u32 i = 0; i < boxes.size(); ++i)
        for (u32 j = i + 1; j < boxes.size(); ++j)
            if (Overlaps(boxes[i], boxes[j]))
                pairs.emplace_back(i, j);
    return pairs;
}

std::vector<Aabb> RandomBoxes(std::mt19937& rng, usize count, f32 world, f32 maxSize)
{
    std::uniform_real_distribution<f32> pos(-world, world), size(0.5f, maxSize);
    std::vector<Aabb> boxes;
    for (usize i = 0; i < count; ++i)
        boxes.push_back(Aabb::FromCenter({pos(rng), pos(rng)}, {size(rng), size(rng)}));
    return boxes;
}

} // namespace

TEST(SpatialHashBasics)
{
    SpatialHash hash(10.0f);
    hash.Insert(1, Aabb::FromCenter({0.0f, 0.0f}, {2.0f, 2.0f}));
    hash.Insert(2, Aabb::FromCenter({3.0f, 0.0f}, {2.0f, 2.0f}));
    hash.Insert(3, Aabb::FromCenter({50.0f, 50.0f}, {2.0f, 2.0f}));
    CHECK(hash.GetCount() == 3 && hash.Contains(2) && !hash.Contains(4));

    std::vector<std::pair<u32, u32>> pairs;
    hash.GetPairs(pairs);
    CHECK(pairs == (std::vector<std::pair<u32, u32>>{{1, 2}}));

    std::vector<u32> ids;
    hash.Query(Aabb{{-100.0f, -100.0f}, {100.0f, 100.0f}}, ids);
    CHECK(ids == (std::vector<u32>{1, 2, 3}));
    hash.Query(Aabb{{45.0f, 45.0f}, {49.0f, 49.0f}}, ids);
    CHECK(ids == (std::vector<u32>{3}));
    hash.Query(Aabb{{20.0f, 20.0f}, {30.0f, 30.0f}}, ids);
    CHECK(ids.empty());

    // Moving 3 next to 1 (another cell), a small move within a cell, and removing.
    hash.Update(3, Aabb::FromCenter({-3.0f, 0.0f}, {2.0f, 2.0f}));
    hash.Update(2, Aabb::FromCenter({3.5f, 0.0f}, {2.0f, 2.0f}));
    hash.GetPairs(pairs);
    CHECK(pairs == (std::vector<std::pair<u32, u32>>{{1, 2}, {1, 3}}));
    hash.Remove(1);
    hash.Remove(1); // removing twice is harmless
    hash.GetPairs(pairs);
    CHECK(pairs.empty() && hash.GetCount() == 2);
    // Touching boxes are not a pair.
    hash.Insert(4, Aabb{{5.5f, -2.0f}, {9.0f, 2.0f}});
    hash.GetPairs(pairs);
    CHECK(pairs.empty());

    u32 visited = 0;
    hash.Insert(5, Aabb{{5.0f, -2.0f}, {9.0f, 2.0f}});
    hash.ForEachPair([&](u32 a, u32 b) { visited += a * 10 + b; });
    CHECK(visited == 25 + 45); // (2, 5) and (4, 5)
    hash.Clear();
    CHECK(hash.GetCount() == 0 && hash.GetCellCount() == 0);
}

TEST(SpatialHashMatchesBruteForce)
{
    std::mt19937 rng(1234);
    for (f32 cell : {2.0f, 8.0f, 40.0f}) {
        std::vector<Aabb> boxes = RandomBoxes(rng, 400, 100.0f, 6.0f);
        SpatialHash hash(cell);
        for (u32 i = 0; i < boxes.size(); ++i)
            hash.Insert(i, boxes[i]);

        std::vector<std::pair<u32, u32>> pairs;
        hash.GetPairs(pairs);
        CHECK(pairs == BrutePairs(boxes));

        // Move everything a bit (some re-bucket, some don't) and check again.
        std::uniform_real_distribution<f32> nudge(-5.0f, 5.0f);
        for (u32 i = 0; i < boxes.size(); ++i) {
            const Vec2 d{nudge(rng), nudge(rng)};
            boxes[i] = {boxes[i].Min + d, boxes[i].Max + d};
            hash.Update(i, boxes[i]);
        }
        hash.GetPairs(pairs);
        CHECK(pairs == BrutePairs(boxes));

        // Queries.
        std::vector<u32> ids;
        for (const Aabb& area : RandomBoxes(rng, 50, 100.0f, 30.0f)) {
            hash.Query(area, ids);
            std::vector<u32> expected;
            for (u32 i = 0; i < boxes.size(); ++i)
                if (Overlaps(area, boxes[i]))
                    expected.push_back(i);
            CHECK(ids == expected);
        }
    }
}

TEST(SpatialHashWraps)
{
    // A 100 x 50 torus: objects near opposite edges are neighbours.
    SpatialHash hash(10.0f, Vec2{100.0f, 50.0f});
    hash.Insert(1, Aabb::FromCenter({1.0f, 25.0f}, {2.0f, 2.0f}));  // reaches past x = 0
    hash.Insert(2, Aabb::FromCenter({98.5f, 25.0f}, {2.0f, 2.0f})); // reaches past x = 100
    hash.Insert(3, Aabb::FromCenter({50.0f, 1.0f}, {2.0f, 2.0f}));
    hash.Insert(4, Aabb::FromCenter({50.0f, 48.5f}, {2.0f, 2.0f}));
    hash.Insert(5, Aabb::FromCenter({50.0f, 25.0f}, {2.0f, 2.0f}));
    std::vector<std::pair<u32, u32>> pairs;
    hash.GetPairs(pairs);
    CHECK(pairs == (std::vector<std::pair<u32, u32>>{{1, 2}, {3, 4}}));

    // A query hanging off the left edge finds the object at the right.
    std::vector<u32> ids;
    hash.Query(Aabb{{-5.0f, 20.0f}, {-1.0f, 30.0f}}, ids);
    CHECK(ids == (std::vector<u32>{2}));
    // A query larger than the world finds everything once.
    hash.Query(Aabb{{-500.0f, -500.0f}, {500.0f, 500.0f}}, ids);
    CHECK(ids == (std::vector<u32>{1, 2, 3, 4, 5}));

    // Random check against a brute force with wrapped center distances.
    std::mt19937 rng(99);
    std::uniform_real_distribution<f32> x(0.0f, 100.0f), y(0.0f, 50.0f), r(0.5f, 4.0f);
    SpatialHash wrapped(8.0f, Vec2{100.0f, 50.0f});
    std::vector<Vec2> centers, halves;
    for (u32 i = 0; i < 300; ++i) {
        centers.push_back({x(rng), y(rng)});
        halves.push_back({r(rng), r(rng)});
        wrapped.Insert(i, Aabb::FromCenter(centers[i], halves[i]));
    }
    std::vector<std::pair<u32, u32>> expected;
    const auto wrappedGap = [](f32 d, f32 size) {
        d = std::fmod(std::fabs(d), size);
        return std::min(d, size - d);
    };
    for (u32 i = 0; i < 300; ++i)
        for (u32 j = i + 1; j < 300; ++j) {
            const Vec2 d = centers[j] - centers[i];
            const Vec2 h = halves[i] + halves[j];
            if (wrappedGap(d.x, 100.0f) < h.x && wrappedGap(d.y, 50.0f) < h.y)
                expected.emplace_back(i, j);
        }
    wrapped.GetPairs(pairs);
    CHECK(pairs == expected);
}

TEST(SpatialHashRejectsBadBoxes)
{
    Log::Init({}); // the skipped boxes are logged
    SpatialHash hash(16.0f);
    hash.Insert(1, Aabb::FromCenter({0.0f, 0.0f}, {2.0f, 2.0f}));

    // A NaN box (e.g. from a NaN position) is skipped, and drops the object's old box.
    const f32 nan = std::nanf("");
    hash.Insert(2, Aabb{{nan, nan}, {nan, nan}});
    hash.Insert(1, Aabb{{nan, 0.0f}, {1.0f, 1.0f}});
    CHECK(!hash.Contains(1) && !hash.Contains(2) && hash.GetCount() == 0);
    hash.Insert(3, Aabb::FromCenter({0.0f, 0.0f}, {2.0f, 2.0f}));
    std::vector<u32> ids;
    hash.Query(Aabb{{nan, nan}, {nan, nan}}, ids);
    CHECK(ids.empty());

    // Huge finite boxes finish at once instead of visiting billions of cells.
    hash.Insert(4, Aabb{{-1e9f, -1e9f}, {1e9f, 1e9f}});
    CHECK(!hash.Contains(4));
    hash.Insert(5, Aabb{{-3e38f, 0.0f}, {3e38f, 1.0f}});
    CHECK(!hash.Contains(5));
    hash.Query(Aabb{{-1e30f, -1e30f}, {1e30f, 1e30f}}, ids); // still finds what is there
    CHECK(ids == (std::vector<u32>{3}));
    std::vector<std::pair<u32, u32>> pairs;
    hash.GetPairs(pairs);
    CHECK(pairs.empty());

    // Far away but small is still fine.
    hash.Insert(6, Aabb::FromCenter({1e7f, -1e7f}, {4.0f, 4.0f}));
    hash.Query(Aabb::FromCenter({1e7f, -1e7f}, {8.0f, 8.0f}), ids);
    CHECK(ids == (std::vector<u32>{6}));
}
