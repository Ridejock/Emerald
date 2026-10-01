#pragma once

// Collision demo (Emerald/Physics): a walled yard to the right of the room where balls bounce off
// each other (pairs from a SpatialHash, circle/circle contacts) and off boxes (circle/AABB), the
// hero is a circle that pushes balls and is blocked by boxes, two spinning polygons show SAT's
// normal and depth, and a ray from the hero towards the mouse (or where it faces) finds the
// nearest shape.

#include <array>
#include <cmath>
#include <optional>
#include <random>
#include <utility>
#include <vector>

#include <Emerald/Emerald.h>

class CollisionDemo {
public:
    using Vec2 = Emerald::Vec2;
    using Vec4 = Emerald::Vec4;

    static constexpr Emerald::Aabb kYard{{1960.0f, 460.0f}, {2520.0f, 1140.0f}};
    static constexpr f32 kHeroRadius = 26.0f;

    CollisionDemo()
    {
        std::mt19937 rng(5);
        std::uniform_real_distribution<f32> x(kYard.Min.x + 30.0f, kYard.Max.x - 30.0f);
        std::uniform_real_distribution<f32> y(kYard.Min.y + 30.0f, kYard.Min.y + 260.0f);
        std::uniform_real_distribution<f32> r(8.0f, 22.0f), v(-160.0f, 160.0f);
        for (u32 i = 0; i < 36; ++i)
            m_Balls.push_back({{{x(rng), y(rng)}, r(rng)}, {v(rng), v(rng)}});
    }

    // Moves the balls and resolves everything; `hero` is pushed out of the boxes.
    void Update(f32 dt, Vec2& hero)
    {
        using namespace Emerald;
        m_Time += dt;
        for (Ball& b : m_Balls)
            b.Shape.Center += b.Velocity * dt;

        // Broadphase: re-bucket the moved balls, then the exact test on each candidate pair.
        for (u32 i = 0; i < m_Balls.size(); ++i)
            m_Hash.Update(i, Bounds(m_Balls[i].Shape));
        m_Contacts = 0;
        m_Hash.GetPairs(m_Pairs);
        for (const auto& [i, j] : m_Pairs) {
            Ball& a = m_Balls[i];
            Ball& b = m_Balls[j];
            const std::optional<Contact> c = Collide(a.Shape, b.Shape);
            if (!c)
                continue;
            ++m_Contacts;
            // Mass ~ area: separate by inverse mass, then an elastic impulse if approaching.
            const f32 ia = 1.0f / (a.Shape.Radius * a.Shape.Radius);
            const f32 ib = 1.0f / (b.Shape.Radius * b.Shape.Radius);
            const Vec2 push = c->Normal * (c->Depth / (ia + ib));
            a.Shape.Center -= push * ia;
            b.Shape.Center += push * ib;
            const f32 approach = Dot(b.Velocity - a.Velocity, c->Normal);
            if (approach < 0.0f) {
                const Vec2 impulse = c->Normal * (-2.0f * approach / (ia + ib));
                a.Velocity -= impulse * ia;
                b.Velocity += impulse * ib;
            }
        }

        for (Ball& b : m_Balls) {
            for (const Aabb& box : kBoxes)
                if (const std::optional<Contact> c = Collide(b.Shape, box))
                    Bounce(b, c->Normal, c->Depth);
            // The hero shoves balls out of its way.
            if (const std::optional<Contact> c = Collide(Circle{hero, kHeroRadius}, b.Shape)) {
                b.Shape.Center += c->Normal * c->Depth;
                b.Velocity += c->Normal * Max(0.0f, 220.0f - Dot(b.Velocity, c->Normal));
            }
            KeepInYard(b);
        }

        // The hero is blocked by the boxes (only inside the yard area).
        for (const Aabb& box : kBoxes)
            if (const std::optional<Contact> c = Collide(Circle{hero, kHeroRadius}, box))
                hero -= c->Normal * c->Depth;

        // SAT: a triangle orbiting a spinning hexagon.
        const Vec2 center{2240.0f, 1010.0f};
        m_Hexagon = Regular<6>(center, 70.0f, 0.4f * m_Time);
        const Vec2 orbit = center + Vec2(std::cos(0.7f * m_Time), std::sin(0.7f * m_Time)) * 110.0f;
        m_Triangle = Regular<3>(orbit, 50.0f, -1.3f * m_Time);
        m_Sat = Collide(std::span<const Vec2>(m_Hexagon), std::span<const Vec2>(m_Triangle));
    }

    // `target`: where the ray points (the mouse), else along `facing`.
    void Draw(Emerald::Renderer2D& r, const Vec2& hero, std::optional<Vec2> target, bool facingLeft,
              bool showGrid)
    {
        using namespace Emerald;
        const Vec4 frame{0.5f, 0.6f, 0.7f, 0.8f};
        const Vec4 ball{0.4f, 0.8f, 1.0f, 1.0f};
        const Vec4 hot{1.0f, 0.35f, 0.3f, 1.0f};
        const Vec4 gold{1.0f, 0.85f, 0.3f, 1.0f};

        r.DrawRect(kYard.Min, kYard.Max - kYard.Min, frame);
        if (showGrid) { // the hash's cells
            const Vec4 grid{0.4f, 0.8f, 1.0f, 0.2f};
            for (f32 x = std::ceil(kYard.Min.x / kCell) * kCell; x < kYard.Max.x; x += kCell)
                r.DrawLine({x, kYard.Min.y}, {x, kYard.Max.y}, grid);
            for (f32 y = std::ceil(kYard.Min.y / kCell) * kCell; y < kYard.Max.y; y += kCell)
                r.DrawLine({kYard.Min.x, y}, {kYard.Max.x, y}, grid);
        }
        for (const Aabb& box : kBoxes)
            r.DrawRect(box.Min, box.Max - box.Min, frame);
        for (const Ball& b : m_Balls)
            r.DrawCircle(b.Shape.Center, b.Shape.Radius, ball, 20);
        r.DrawCircle(hero, kHeroRadius, {1.0f, 1.0f, 1.0f, 0.3f}, 24); // the hero's collider

        // SAT pair, red while overlapping, with the contact normal scaled by the depth (x4).
        const Vec4 sat = m_Sat ? hot : frame;
        r.DrawPolygon(m_Hexagon, sat);
        r.DrawPolygon(m_Triangle, sat);
        if (m_Sat) {
            const Vec2 from = Centroid(m_Triangle);
            r.DrawLine(from, from + m_Sat->Normal * (m_Sat->Depth * 4.0f + 10.0f), gold);
        }

        // The ray, cut at the nearest hit, with the surface normal there.
        const Vec2 dir = target ? *target - hero : Vec2(facingLeft ? -1.0f : 1.0f, 0.0f);
        const Ray ray{hero, dir, 900.0f};
        m_Hit = CastRay(ray);
        const Vec2 end = m_Hit ? m_Hit->Point : hero + Normalize(dir) * ray.MaxDistance;
        r.DrawLine(hero, end, {gold.x, gold.y, gold.z, 0.6f});
        if (m_Hit) {
            r.DrawCircle(m_Hit->Point, 4.0f, gold, 12);
            r.DrawLine(m_Hit->Point, m_Hit->Point + m_Hit->Normal * 24.0f, hot);
        }
    }

    [[nodiscard]] usize GetBallCount() const { return m_Balls.size(); }
    [[nodiscard]] usize GetCellCount() const { return m_Hash.GetCellCount(); }
    [[nodiscard]] usize GetPairCount() const { return m_Pairs.size(); }
    [[nodiscard]] u32 GetContactCount() const { return m_Contacts; }
    [[nodiscard]] const std::optional<Emerald::Contact>& GetSat() const { return m_Sat; }
    [[nodiscard]] const std::optional<Emerald::RayHit>& GetHit() const { return m_Hit; }

private:
    struct Ball {
        Emerald::Circle Shape;
        Vec2 Velocity;
    };

    static constexpr f32 kCell = 48.0f;
    static constexpr std::array<Emerald::Aabb, 3> kBoxes{{
        {{2020.0f, 640.0f}, {2140.0f, 680.0f}},
        {{2320.0f, 600.0f}, {2380.0f, 760.0f}},
        {{2100.0f, 800.0f}, {2260.0f, 840.0f}},
    }};

    static Emerald::Aabb Bounds(const Emerald::Circle& c)
    {
        return Emerald::Aabb::FromCenter(c.Center, {c.Radius, c.Radius});
    }

    template <usize N> static std::array<Vec2, N> Regular(const Vec2& c, f32 radius, f32 angle)
    {
        std::array<Vec2, N> points{};
        for (usize i = 0; i < N; ++i) {
            const f32 a = angle + Emerald::TwoPi * static_cast<f32>(i) / static_cast<f32>(N);
            points[i] = c + Vec2(std::cos(a), std::sin(a)) * radius;
        }
        return points;
    }

    template <usize N> static Vec2 Centroid(const std::array<Vec2, N>& points)
    {
        Vec2 sum{};
        for (const Vec2& p : points)
            sum += p;
        return sum / static_cast<f32>(N);
    }

    // Out along -normal (the contact's normal points from the ball into the obstacle).
    static void Bounce(Ball& b, const Vec2& normal, f32 depth)
    {
        b.Shape.Center -= normal * depth;
        const f32 into = Emerald::Dot(b.Velocity, normal);
        if (into > 0.0f)
            b.Velocity -= normal * (2.0f * into);
    }

    static void KeepInYard(Ball& b)
    {
        const f32 r = b.Shape.Radius;
        Vec2& p = b.Shape.Center;
        if (p.x - r < kYard.Min.x || p.x + r > kYard.Max.x) {
            p.x = Emerald::Clamp(p.x, kYard.Min.x + r, kYard.Max.x - r);
            b.Velocity.x = -b.Velocity.x;
        }
        if (p.y - r < kYard.Min.y || p.y + r > kYard.Max.y) {
            p.y = Emerald::Clamp(p.y, kYard.Min.y + r, kYard.Max.y - r);
            b.Velocity.y = -b.Velocity.y;
        }
    }

    // The nearest hit among the balls (via a hash query along the ray's box), boxes and polygons.
    [[nodiscard]] std::optional<Emerald::RayHit> CastRay(const Emerald::Ray& ray)
    {
        using namespace Emerald;
        std::optional<RayHit> best;
        const auto keep = [&](const std::optional<RayHit>& hit) {
            if (hit && (!best || hit->Distance < best->Distance))
                best = hit;
        };
        const Vec2 end = ray.Origin + Normalize(ray.Direction) * ray.MaxDistance;
        m_Hash.Query({Min(ray.Origin, end), Max(ray.Origin, end)}, m_Ids);
        for (u32 id : m_Ids)
            keep(Raycast(ray, m_Balls[id].Shape));
        for (const Aabb& box : kBoxes)
            keep(Raycast(ray, box));
        keep(Raycast(ray, std::span<const Vec2>(m_Hexagon)));
        keep(Raycast(ray, std::span<const Vec2>(m_Triangle)));
        return best;
    }

    std::vector<Ball> m_Balls;
    Emerald::SpatialHash m_Hash{kCell};
    std::vector<std::pair<u32, u32>> m_Pairs;
    std::vector<u32> m_Ids;
    u32 m_Contacts = 0;
    f32 m_Time = 0.0f;
    std::array<Vec2, 6> m_Hexagon{};
    std::array<Vec2, 3> m_Triangle{};
    std::optional<Emerald::Contact> m_Sat;
    std::optional<Emerald::RayHit> m_Hit;
};
