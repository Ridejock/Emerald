// Entities without a GPU: the World / Entity layer (handles, components, deferred destruction,
// Each, stale ids), churning many entities without leaks, and the systems (movement, animation,
// collisions through the broadphase, sorted and culled sprite drawing).

#include <string>
#include <vector>

#include <Emerald/Entity/Components.h>
#include <Emerald/Entity/Systems.h>
#include <Emerald/Entity/World.h>
#include <Emerald/Math/Mat4.h>
#include <Emerald/Renderer/Renderer2D.h>
#include <Emerald/Renderer/Texture.h>

#include "Test.h"

using namespace Emerald;

namespace {

// Counts its living instances, to see that destroyed entities release their components.
struct Tracked {
    static inline i32 Alive = 0;
    i32 Value = 0;
    Tracked(i32 value = 0) : Value(value) { ++Alive; }
    Tracked(const Tracked& o) : Value(o.Value) { ++Alive; }
    Tracked(Tracked&& o) noexcept : Value(o.Value) { ++Alive; }
    Tracked& operator=(const Tracked&) = default;
    Tracked& operator=(Tracked&&) noexcept = default;
    ~Tracked() { --Alive; }
};

struct Enemy {}; // a tag

} // namespace

TEST(EntitiesAddGetHasRemove)
{
    World world;
    Entity e = world.Spawn();
    CHECK(e.IsValid() && world.GetCount() == 1 && !Entity().IsValid());
    e.Add<Transform>(Transform{.Position = {1.0f, 2.0f}});
    e.Add<Velocity>(Velocity{.Linear = {3.0f, 0.0f}});
    e.Add<Enemy>();
    CHECK(e.Has<Transform>() && e.Has<Velocity>() && e.Has<Enemy>() && !e.Has<Collider>());
    CHECK(e.Get<Transform>().Position == Vec2(1.0f, 2.0f));
    CHECK(e.TryGet<Collider>() == nullptr && e.TryGet<Velocity>() != nullptr);
    e.Add<Transform>(Transform{.Position = {5.0f, 5.0f}}); // replaces
    CHECK(e.Get<Transform>().Position == Vec2(5.0f, 5.0f));
    e.Remove<Velocity>();
    CHECK(!e.Has<Velocity>());
    // Handles compare by world and id; a copy is the same entity.
    const Entity copy = e;
    CHECK(copy == e && copy.ToU32() == e.ToU32() && copy != world.Spawn());
}

TEST(EntitiesDestroyIsDeferred)
{
    World world;
    Entity a = world.Spawn();
    a.Add<Tracked>(7);
    Entity b = world.Spawn();
    b.Add<Tracked>(8);
    a.Destroy();
    a.Destroy(); // twice: once
    CHECK(!a.IsValid() && b.IsValid());
    CHECK(world.GetPendingDestroyCount() == 1 && world.GetCount() == 2);
    CHECK(a.Get<Tracked>().Value == 7); // still in memory until Flush
    CHECK(Tracked::Alive == 2);
    usize seen = 0;
    world.Each<Tracked>([&](Entity, Tracked&) { ++seen; });
    CHECK(seen == 1); // already skipped
    world.Flush();
    CHECK(world.GetCount() == 1 && world.GetPendingDestroyCount() == 0 && Tracked::Alive == 1);
    CHECK(!a.Has<Tracked>() && !a.IsValid());
    // Its slot gets reused, but the old handle stays invalid (versioned ids).
    Entity c = world.Spawn();
    CHECK(c.IsValid() && !a.IsValid() && c != a);
    world.Clear();
    CHECK(world.GetCount() == 0 && Tracked::Alive == 0 && !b.IsValid() && !c.IsValid());
    CHECK(world.GetSpawnedTotal() == 3 && world.GetDestroyedTotal() == 3);
}

TEST(EntitiesChangeInsideEach)
{
    World world;
    for (i32 i = 0; i < 100; ++i)
        world.Spawn().Add<Tracked>(i);
    // Destroy every odd one and spawn a new one per even one, all from inside the loop.
    usize visited = 0;
    world.Each<Tracked>([&](Entity e, Tracked& t) {
        ++visited;
        if (t.Value % 2 != 0)
            e.Destroy();
        else if (t.Value < 100)
            world.Spawn().Add<Tracked>(1000 + t.Value);
    });
    CHECK(visited >= 100);
    world.Flush();
    usize odd = 0;
    usize count = 0;
    world.Each<Tracked>([&](Entity, Tracked& t) {
        ++count;
        odd += (t.Value < 100 && t.Value % 2 != 0) ? 1 : 0;
    });
    CHECK(count == 100 && odd == 0 && world.GetCount() == 100);
}

TEST(EntitiesChurnWithoutLeaks)
{
    // 200 frames of spawning 2,000 entities (4 components each) and destroying 2,000: 400k
    // spawns. The population, the storages and the component instances must not grow.
    World world;
    std::vector<Entity> live;
    usize peakStorage = 0;
    for (i32 frame = 0; frame < 200; ++frame) {
        for (i32 i = 0; i < 2000; ++i) {
            Entity e = world.Spawn();
            e.Add<Transform>(Transform{.Position = {static_cast<f32>(i), 0.0f}});
            e.Add<Velocity>(Velocity{.Linear = {1.0f, 1.0f}});
            e.Add<Collider>(Collider::MakeCircle(4.0f));
            e.Add<Tracked>(i);
            live.push_back(e);
        }
        // Keep the newest 2,000: destroy the older ones.
        if (live.size() > 2000) {
            for (usize i = 0; i < live.size() - 2000; ++i)
                live[i].Destroy();
            live.erase(live.begin(), live.end() - 2000);
        }
        UpdateMovement(world, 0.016f);
        world.Flush();
        const usize storage = world.GetRegistry().storage<Transform>().size();
        peakStorage = frame == 1 ? storage : std::max(peakStorage, storage);
    }
    CHECK(world.GetCount() == 2000 && Tracked::Alive == 2000);
    CHECK(world.GetRegistry().storage<Transform>().size() == 2000);
    CHECK(world.GetRegistry().storage<Collider>().size() == 2000);
    CHECK(peakStorage <= 4000); // two frames' worth at most, never growing
    // Entity ids are recycled: the id storage holds at most the peak population.
    CHECK(world.GetRegistry().storage<entt::entity>().size() <= 4000);
    CHECK(world.GetSpawnedTotal() == 400000 && world.GetDestroyedTotal() == 398000);
    world.Clear();
    CHECK(Tracked::Alive == 0);
}

TEST(EntitiesMovementAndAnimation)
{
    World world;
    Entity e = world.Spawn();
    e.Add<Transform>(Transform{.Position = {10.0f, 0.0f}});
    e.Add<Velocity>(Velocity{.Linear = {100.0f, -50.0f}, .Angular = 2.0f});
    for (i32 i = 0; i < 10; ++i)
        UpdateMovement(world, 0.05f);
    CHECK_NEAR(e.Get<Transform>().Position, Vec2(60.0f, -25.0f));
    CHECK_NEAR(e.Get<Transform>().Rotation, 1.0f);

    // A two-frame Once animation: finishes after 0.2 s, firing its event from UpdateAnimation.
    const Texture sheet = Texture::CreateWithoutGpu(32, 16);
    Animation blink{
        .Name = "blink",
        .Frames = {{.Image = {&sheet, {{0.0f, 0.0f}, {16.0f, 16.0f}}}, .Duration = 0.1f},
                   {.Image = {&sheet, {{16.0f, 0.0f}, {16.0f, 16.0f}}}, .Duration = 0.1f}},
        .Mode = AnimationMode::Once};
    Animator& animator = e.Add<Animator>();
    i32 finished = 0;
    animator.OnFinished = [&](const Animation&) {
        ++finished;
        e.Destroy(); // from inside the system: deferred
    };
    animator.Play(blink);
    UpdateAnimation(world, 0.15f);
    CHECK(e.Get<Animator>().GetFrameIndex() == 1 && finished == 0);
    UpdateAnimation(world, 0.1f);
    CHECK(finished == 1 && !e.IsValid());
    world.Flush();
    CHECK(world.GetCount() == 0);
}

TEST(EntitiesCollisions)
{
    World world;
    CollisionSystem collisions(32.0f);
    const auto spawn = [&](Vec2 at, const Collider& c) {
        Entity e = world.Spawn();
        e.Add<Transform>(Transform{.Position = at});
        e.Add<Collider>(c);
        return e;
    };
    Entity a = spawn({0.0f, 0.0f}, Collider::MakeCircle(10.0f));
    Entity b = spawn({15.0f, 0.0f}, Collider::MakeCircle(10.0f)); // overlaps a by 5
    Entity box = spawn({100.0f, 0.0f}, Collider::MakeBox({10.0f, 10.0f}));
    Entity far = spawn({500.0f, 500.0f}, Collider::MakeCircle(10.0f)); // alone
    // A ghost: on layer 2, colliding with nothing on layer 1.
    Entity ghost = spawn({5.0f, 0.0f}, Collider::MakeCircle(10.0f, 2u, 2u));

    std::span<const CollisionEvent> hits = collisions.Update(world);
    CHECK(hits.size() == 1 && collisions.GetCandidateCount() >= 3); // the ghost's pairs dropped
    if (hits.size() != 1) {
        CHECK(false);
        return;
    }
    CHECK(hits[0].A == a && hits[0].B == b);
    CHECK_NEAR(hits[0].Hit.Normal, Vec2(1.0f, 0.0f));
    CHECK_NEAR(hits[0].Hit.Depth, 5.0f);

    // Circle against box, after moving b next to the box.
    b.Get<Transform>().Position = {85.0f, 0.0f};
    hits = collisions.Update(world);
    CHECK(hits.size() == 1 && hits[0].A == b && hits[0].B == box);

    // Destroyed and collider-less entities leave the broadphase.
    CHECK(collisions.GetBroadphase().GetCount() == 5);
    box.Destroy();
    ghost.Remove<Collider>();
    hits = collisions.Update(world);
    CHECK(hits.empty() && collisions.GetBroadphase().GetCount() == 3);
    world.Flush();
    hits = collisions.Update(world);
    CHECK(hits.empty() && collisions.GetBroadphase().GetCount() == 3);
    CHECK(far.IsValid());
}

TEST(EntitiesDrawSpritesSortedAndCulled)
{
    World world;
    const Texture texture = Texture::CreateWithoutGpu(16, 16);
    const Sprite sprite = Sprite::FromTexture(texture);
    // The tint's red channel (0..255) identifies each sprite in the vertex output.
    const auto spawn = [&](Vec2 at, i32 layer, u8 id) {
        Entity e = world.Spawn();
        e.Add<Transform>(Transform{.Position = at});
        e.Add<SpriteRenderer>(
            SpriteRenderer{.Image = sprite,
                           .Options = {.Tint = {static_cast<f32>(id) / 255.0f, 0.0f, 0.0f, 1.0f}},
                           .Layer = layer});
    };
    spawn({0.0f, 50.0f}, 0, 1);
    spawn({0.0f, 10.0f}, 0, 2);   // higher on screen: drawn before 1
    spawn({0.0f, 0.0f}, 1, 3);    // layer 1: after all of layer 0
    spawn({0.0f, -20.0f}, -1, 4); // layer -1: first
    spawn({9000.0f, 0.0f}, 0, 5); // off screen

    Renderer2D r;
    r.Begin(Mat4::OrthoPixelSpace(640.0f, 360.0f));
    const u32 drawn =
        DrawSprites(world, r, {.Visible = Rect2D{{-100.0f, -100.0f}, {640.0f, 360.0f}}});
    r.End();
    CHECK(drawn == 4 && r.GetSpriteCount() == 4);
    std::vector<u32> order;
    for (u32 i = 0; i < r.GetSpriteCount(); ++i)
        order.push_back(r.GetSpriteVertices()[i * 6].Color & 0xffu);
    CHECK((order == std::vector<u32>{4, 2, 1, 3}));

    // Without culling all five; with an Animator its frame is drawn instead of Image.
    r.Clear();
    r.Begin(Mat4::OrthoPixelSpace(640.0f, 360.0f));
    CHECK(DrawSprites(world, r) == 5);
    r.End();
}
