#include "Emerald/Physics/Collision.h"

#include <algorithm>
#include <cmath>

namespace Emerald {

namespace {

Vec2 Perp(const Vec2& v)
{
    return {v.y, -v.x};
}

// Twice the signed area; > 0 for counterclockwise points (in a y-up frame).
f32 SignedArea2(std::span<const Vec2> p)
{
    f32 area = 0.0f;
    for (usize i = 0; i < p.size(); ++i)
        area += Cross(p[i], p[(i + 1) % p.size()]);
    return area;
}

void Project(std::span<const Vec2> p, const Vec2& axis, f32& lo, f32& hi)
{
    lo = hi = Dot(p[0], axis);
    for (const Vec2& v : p) {
        const f32 d = Dot(v, axis);
        lo = std::min(lo, d);
        hi = std::max(hi, d);
    }
}

Vec2 SafeDirection(const Vec2& d)
{
    const f32 length = Length(d);
    return length > 0.0f ? d / length : Vec2(1.0f, 0.0f);
}

// A hit at the ray's origin (it starts inside the shape).
RayHit InsideHit(const Ray& ray, const Vec2& direction)
{
    return {.Distance = 0.0f, .Point = ray.Origin, .Normal = -direction};
}

} // namespace

bool Overlaps(const Circle& a, const Circle& b)
{
    const f32 r = a.Radius + b.Radius;
    return LengthSquared(b.Center - a.Center) < r * r;
}

bool Overlaps(const Aabb& a, const Aabb& b)
{
    return a.Min.x < b.Max.x && b.Min.x < a.Max.x && a.Min.y < b.Max.y && b.Min.y < a.Max.y;
}

bool Overlaps(const Circle& a, const Aabb& b)
{
    return Collide(a, b).has_value();
}

std::optional<Contact> Collide(const Circle& a, const Circle& b)
{
    if (!Overlaps(a, b))
        return std::nullopt;
    const Vec2 d = b.Center - a.Center;
    const f32 distance = Length(d);
    // Same center: any direction separates them; pick +X.
    const Vec2 normal = distance > 0.0f ? d / distance : Vec2(1.0f, 0.0f);
    return Contact{normal, a.Radius + b.Radius - distance};
}

std::optional<Contact> Collide(const Circle& a, const Aabb& b)
{
    const Vec2 closest = Min(Max(a.Center, b.Min), b.Max);
    const Vec2 d = closest - a.Center;
    const f32 dsq = LengthSquared(d);
    if (dsq > 0.0f) {
        if (dsq >= a.Radius * a.Radius)
            return std::nullopt;
        const f32 distance = std::sqrt(dsq);
        return Contact{d / distance, a.Radius - distance};
    }
    // The center is in the box (or on its edge): leave through the nearest side.
    const f32 left = a.Center.x - b.Min.x;
    const f32 right = b.Max.x - a.Center.x;
    const f32 top = a.Center.y - b.Min.y;
    const f32 bottom = b.Max.y - a.Center.y;
    const f32 nearest = std::min({left, right, top, bottom});
    if (nearest <= 0.0f && a.Radius <= 0.0f)
        return std::nullopt; // a point on the edge only touches
    if (nearest == left)
        return Contact{{1.0f, 0.0f}, left + a.Radius};
    if (nearest == right)
        return Contact{{-1.0f, 0.0f}, right + a.Radius};
    if (nearest == top)
        return Contact{{0.0f, 1.0f}, top + a.Radius};
    return Contact{{0.0f, -1.0f}, bottom + a.Radius};
}

std::optional<Contact> Collide(const Aabb& a, const Circle& b)
{
    std::optional<Contact> c = Collide(b, a);
    if (c)
        c->Normal = -c->Normal;
    return c;
}

std::optional<Contact> Collide(const Aabb& a, const Aabb& b)
{
    // How far b must move each way to clear a; the shortest one wins. This stays right when
    // one box contains the other (an overlap width would be too small there).
    const f32 right = a.Max.x - b.Min.x;
    const f32 left = b.Max.x - a.Min.x;
    const f32 down = a.Max.y - b.Min.y;
    const f32 up = b.Max.y - a.Min.y;
    if (right <= 0.0f || left <= 0.0f || down <= 0.0f || up <= 0.0f)
        return std::nullopt; // apart or only touching
    const f32 x = std::min(right, left);
    const f32 y = std::min(down, up);
    if (x < y)
        return Contact{{right <= left ? 1.0f : -1.0f, 0.0f}, x};
    return Contact{{0.0f, down <= up ? 1.0f : -1.0f}, y};
}

std::optional<Contact> Collide(std::span<const Vec2> a, std::span<const Vec2> b)
{
    if (a.size() < 3 || b.size() < 3)
        return std::nullopt;
    std::optional<Contact> best;
    // Every edge normal of both polygons is a candidate separating axis.
    for (const std::span<const Vec2> p : {a, b}) {
        for (usize i = 0; i < p.size(); ++i) {
            const Vec2 edge = p[(i + 1) % p.size()] - p[i];
            const f32 length = Length(edge);
            if (length <= 0.0f)
                continue; // repeated point
            const Vec2 axis = Perp(edge) / length;
            f32 aLo, aHi, bLo, bHi;
            Project(a, axis, aLo, aHi);
            Project(b, axis, bLo, bHi);
            // Push b along +axis or -axis, whichever is shorter.
            const f32 forward = aHi - bLo;
            const f32 backward = bHi - aLo;
            const f32 depth = std::min(forward, backward);
            if (depth <= 0.0f)
                return std::nullopt; // found a gap
            if (!best || depth < best->Depth)
                best = Contact{forward < backward ? axis : -axis, depth};
        }
    }
    return best;
}

std::optional<RayHit> Raycast(const Ray& ray, const Circle& circle)
{
    const Vec2 d = SafeDirection(ray.Direction);
    const Vec2 m = ray.Origin - circle.Center;
    const f32 b = Dot(m, d);
    const f32 c = LengthSquared(m) - circle.Radius * circle.Radius;
    if (c < 0.0f)
        return InsideHit(ray, d);
    if (b > 0.0f)
        return std::nullopt; // outside and pointing away
    const f32 discriminant = b * b - c;
    if (discriminant < 0.0f)
        return std::nullopt;
    const f32 t = std::max(-b - std::sqrt(discriminant), 0.0f);
    if (t > ray.MaxDistance)
        return std::nullopt;
    const Vec2 point = ray.Origin + d * t;
    return RayHit{t, point, SafeDirection(point - circle.Center)};
}

std::optional<RayHit> Raycast(const Ray& ray, const Aabb& box)
{
    // Slabs: the ray is inside the box between the latest entry and the earliest exit.
    const Vec2 d = SafeDirection(ray.Direction);
    f32 enter = 0.0f;
    f32 exit = ray.MaxDistance;
    Vec2 normal{};
    const f32 origin[2] = {ray.Origin.x, ray.Origin.y};
    const f32 dir[2] = {d.x, d.y};
    const f32 lo[2] = {box.Min.x, box.Min.y};
    const f32 hi[2] = {box.Max.x, box.Max.y};
    for (usize axis = 0; axis < 2; ++axis) {
        if (dir[axis] == 0.0f) {
            if (origin[axis] < lo[axis] || origin[axis] > hi[axis])
                return std::nullopt; // parallel and outside this slab
            continue;
        }
        f32 t0 = (lo[axis] - origin[axis]) / dir[axis];
        f32 t1 = (hi[axis] - origin[axis]) / dir[axis];
        f32 side = -1.0f; // entering through the low side: normal points to -axis
        if (t0 > t1) {
            std::swap(t0, t1);
            side = 1.0f;
        }
        if (t0 > enter) {
            enter = t0;
            normal = axis == 0 ? Vec2(side, 0.0f) : Vec2(0.0f, side);
        }
        exit = std::min(exit, t1);
        if (enter > exit)
            return std::nullopt;
    }
    if (normal == Vec2())
        return InsideHit(ray, d);
    return RayHit{enter, ray.Origin + d * enter, normal};
}

std::optional<RayHit> Raycast(const Ray& ray, std::span<const Vec2> polygon)
{
    if (polygon.size() < 3)
        return std::nullopt;
    // Clip the ray against every edge's half-plane (Cyrus-Beck).
    const Vec2 d = SafeDirection(ray.Direction);
    const f32 outward = SignedArea2(polygon) > 0.0f ? 1.0f : -1.0f;
    f32 enter = 0.0f;
    f32 exit = ray.MaxDistance;
    Vec2 normal{};
    for (usize i = 0; i < polygon.size(); ++i) {
        const Vec2 edge = polygon[(i + 1) % polygon.size()] - polygon[i];
        const f32 length = Length(edge);
        if (length <= 0.0f)
            continue;
        const Vec2 n = Perp(edge) * (outward / length);
        const f32 distance = Dot(n, polygon[i] - ray.Origin); // < 0: origin outside this edge
        const f32 speed = Dot(n, d);
        if (speed == 0.0f) {
            if (distance < 0.0f)
                return std::nullopt; // parallel and outside
            continue;
        }
        const f32 t = distance / speed;
        if (speed < 0.0f) { // entering
            if (t > enter) {
                enter = t;
                normal = n;
            }
        } else {
            exit = std::min(exit, t);
        }
        if (enter > exit)
            return std::nullopt;
    }
    if (normal == Vec2())
        return InsideHit(ray, d);
    return RayHit{enter, ray.Origin + d * enter, normal};
}

} // namespace Emerald
