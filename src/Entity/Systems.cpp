#include "Emerald/Entity/Systems.h"

#include <algorithm>
#include <iterator>
#include <tuple>

#include "Emerald/Core/Profile.h"
#include "Emerald/Renderer/Renderer2D.h"

namespace Emerald {

void UpdateMovement(World& world, f32 dt)
{
    EM_PROFILE_FUNCTION();
    world.Each<Transform, Velocity>([dt](Entity, Transform& t, const Velocity& v) {
        t.Position += v.Linear * dt;
        t.Rotation += v.Angular * dt;
    });
}

void UpdateAnimation(World& world, f32 dt)
{
    EM_PROFILE_FUNCTION();
    world.Each<Animator>([dt](Entity, Animator& animator) { animator.Update(dt); });
}

// --- Collisions -------------------------------------------------------------------------------

namespace {

std::optional<Contact> Test(const Collider& a, Vec2 aAt, const Collider& b, Vec2 bAt)
{
    const bool aCircle = a.Shape == ColliderShape::Circle;
    const bool bCircle = b.Shape == ColliderShape::Circle;
    const Circle ca{aAt + a.Offset, a.Radius};
    const Circle cb{bAt + b.Offset, b.Radius};
    const Aabb ba = a.GetBounds(aAt);
    const Aabb bb = b.GetBounds(bAt);
    if (aCircle && bCircle)
        return Collide(ca, cb);
    if (aCircle)
        return Collide(ca, bb);
    if (bCircle)
        return Collide(ba, cb);
    return Collide(ba, bb);
}

} // namespace

CollisionSystem::CollisionSystem(f32 cellSize, std::optional<Vec2> wrapSize)
    : m_Hash(cellSize, wrapSize)
{
}

std::span<const CollisionEvent> CollisionSystem::Update(World& world)
{
    EM_PROFILE_SCOPE("CollisionSystem::Update");
    // Broadphase: insert or move every collider (unchanged cells cost a lookup).
    m_Seen.clear();
    world.Each<Transform, Collider>([&](Entity e, const Transform& t, const Collider& c) {
        m_Hash.Update(e.ToU32(), c.GetBounds(t.Position));
        m_Seen.push_back(e.ToU32());
    });
    // Whatever was tracked but not seen now was destroyed or lost its collider.
    std::sort(m_Seen.begin(), m_Seen.end());
    m_Gone.clear();
    std::set_difference(m_Tracked.begin(), m_Tracked.end(), m_Seen.begin(), m_Seen.end(),
                        std::back_inserter(m_Gone));
    for (const u32 id : m_Gone)
        m_Hash.Remove(id);
    m_Tracked.swap(m_Seen);

    // Narrowphase on the candidate pairs (already sorted and unique).
    m_Events.clear();
    m_Hash.GetPairs(m_Candidates);
    entt::registry& registry = world.GetRegistry();
    for (const auto& [ia, ib] : m_Candidates) {
        const auto a = static_cast<entt::entity>(ia);
        const auto b = static_cast<entt::entity>(ib);
        const auto [ta, ca] = registry.get<Transform, Collider>(a);
        const auto [tb, cb] = registry.get<Transform, Collider>(b);
        if ((ca.Layer & cb.Mask) == 0 || (cb.Layer & ca.Mask) == 0)
            continue;
        if (const std::optional<Contact> hit = Test(ca, ta.Position, cb, tb.Position))
            m_Events.push_back({world.Wrap(a), world.Wrap(b), *hit});
    }
    return m_Events;
}

// --- Sprites ----------------------------------------------------------------------------------

u32 DrawSprites(World& world, Renderer2D& r, const DrawSpritesOptions& options)
{
    EM_PROFILE_FUNCTION();
    struct Item {
        i32 Layer;
        f32 Y;
        u32 Id; // ties: the same order every frame
        const Transform* Where;
        const SpriteRenderer* What;
        const Animator* Animation; // null: What->Image
    };
    std::pmr::vector<Item> items(options.Scratch ? options.Scratch
                                                 : std::pmr::get_default_resource());
    items.reserve(world.GetCount());

    entt::registry& registry = world.GetRegistry();
    world.Each<Transform, SpriteRenderer>([&](Entity e, const Transform& t,
                                              const SpriteRenderer& s) {
        const Animator* animator = registry.try_get<Animator>(e.GetId());
        if (options.Visible) {
            // A circle around the position that surely holds the sprite, whatever its origin.
            const Vec2 region = animator ? animator->GetSprite().Region.Size : s.Image.Region.Size;
            const Vec2 size = s.Options.Size.x > 0.0f ? s.Options.Size : region * s.Options.Scale;
            const f32 reach = Length(size * t.Scale);
            const Rect2D& v = *options.Visible;
            if (t.Position.x + reach < v.Min.x || t.Position.x - reach > v.Max.x ||
                t.Position.y + reach < v.Min.y || t.Position.y - reach > v.Max.y)
                return;
        }
        items.push_back(
            {s.Layer, options.SortByY ? t.Position.y : 0.0f, e.ToU32(), &t, &s, animator});
    });

    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        return std::tie(a.Layer, a.Y, a.Id) < std::tie(b.Layer, b.Y, b.Id);
    });
    for (const Item& item : items) {
        SpriteOptions o = item.What->Options;
        o.Rotation += item.Where->Rotation;
        o.Scale = o.Scale * item.Where->Scale;
        if (o.Size.x > 0.0f)
            o.Size = o.Size * item.Where->Scale;
        const Sprite sprite = item.Animation ? item.Animation->GetSprite() : item.What->Image;
        r.DrawSprite(sprite, item.Where->Position, o);
    }
    return static_cast<u32>(items.size());
}

} // namespace Emerald
