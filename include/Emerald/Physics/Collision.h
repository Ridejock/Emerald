#pragma once

#include <limits>
#include <optional>
#include <span>

#include "Emerald/Core/Defines.h"
#include "Emerald/Math/Vec2.h"

namespace Emerald {

// 2D shape tests: overlap with contact normal and depth, and raycasts. Shapes are plain values in
// world units; polygons are convex point lists (any winding, at least 3 points).
//
//   if (std::optional<Contact> c = Collide(Circle{a, 10}, Circle{b, 12}))
//       b += c->Normal * c->Depth; // pushes b out of a
//
// Touching is not overlapping: shapes that only share an edge or a point give no contact.

struct Circle {
    Vec2 Center{};
    f32 Radius = 0.0f;
};

// Axis-aligned box, Min <= Max.
struct Aabb {
    Vec2 Min{};
    Vec2 Max{};

    [[nodiscard]] static Aabb FromCenter(const Vec2& center, const Vec2& halfSize)
    {
        return {center - halfSize, center + halfSize};
    }
    [[nodiscard]] Vec2 GetCenter() const { return (Min + Max) * 0.5f; }
    [[nodiscard]] Vec2 GetHalfSize() const { return (Max - Min) * 0.5f; }
};

// How two shapes overlap. Normal is a unit vector pointing from the first shape towards the
// second; moving the second by Normal * Depth (or the first by -Normal * Depth) separates them.
struct Contact {
    Vec2 Normal{};
    f32 Depth = 0.0f;
};

// A ray from Origin along Direction (normalized by the tests; zero means +X), up to MaxDistance.
struct Ray {
    Vec2 Origin{};
    Vec2 Direction{1.0f, 0.0f};
    f32 MaxDistance = std::numeric_limits<f32>::infinity();
};

// Where a ray first enters a shape. A ray starting inside hits at distance 0, with the normal
// facing back along the ray.
struct RayHit {
    f32 Distance = 0.0f; // along the ray, world units
    Vec2 Point{};
    Vec2 Normal{}; // the surface normal there (unit, facing the ray)
};

// --- Overlap ---
[[nodiscard]] bool Overlaps(const Circle& a, const Circle& b);
[[nodiscard]] bool Overlaps(const Aabb& a, const Aabb& b);
[[nodiscard]] bool Overlaps(const Circle& a, const Aabb& b);

[[nodiscard]] std::optional<Contact> Collide(const Circle& a, const Circle& b);
[[nodiscard]] std::optional<Contact> Collide(const Circle& a, const Aabb& b);
[[nodiscard]] std::optional<Contact> Collide(const Aabb& a, const Circle& b);
[[nodiscard]] std::optional<Contact> Collide(const Aabb& a, const Aabb& b);
// Separating axis test for two convex polygons.
[[nodiscard]] std::optional<Contact> Collide(std::span<const Vec2> a, std::span<const Vec2> b);

// --- Raycasts ---
[[nodiscard]] std::optional<RayHit> Raycast(const Ray& ray, const Circle& circle);
[[nodiscard]] std::optional<RayHit> Raycast(const Ray& ray, const Aabb& box);
[[nodiscard]] std::optional<RayHit> Raycast(const Ray& ray, std::span<const Vec2> polygon);

} // namespace Emerald
