// Micro-benchmark: the entity layer (World over EnTT) and its systems. Build in Release
// (-DEMERALD_BUILD_BENCH=ON) and run EmeraldEntityBench.
//
// "Game-like" entities have four components: Transform, Velocity, SpriteRenderer, Collider.
//   - spawn / destroy: 100k entities created with their components, then destroyed (deferred,
//     one Flush), best of 5;
//   - churn: a population of 10k where 1k are destroyed and 1k spawned every frame, for 1000
//     frames (one million of each), with the memory the storages hold before and after;
//   - systems: UpdateMovement, CollisionSystem::Update (radius 6 circles, 10k per 2000 x 2000
//     area: the world grows with the count, so the density stays the same) and DrawSprites
//     (sorting by layer and y, into a GPU-less Renderer2D) per frame.

#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

#include <Emerald/Entity/Components.h>
#include <Emerald/Entity/Systems.h>
#include <Emerald/Entity/World.h>
#include <Emerald/Math/Mat4.h>
#include <Emerald/Renderer/Renderer2D.h>
#include <Emerald/Renderer/Texture.h>

using namespace Emerald;

namespace {

using Clock = std::chrono::steady_clock;

template <typename F> f64 BestOfMs(i32 runs, F&& body)
{
    f64 best = 1e30;
    for (i32 i = 0; i < runs; ++i) {
        const auto start = Clock::now();
        body();
        const f64 ms = std::chrono::duration<f64, std::milli>(Clock::now() - start).count();
        best = ms < best ? ms : best;
    }
    return best;
}

struct Spawner {
    std::mt19937 Rng{7};
    std::uniform_real_distribution<f32> Pos{0.0f, 2000.0f};
    std::uniform_real_distribution<f32> Speed{-60.0f, 60.0f};
    Sprite Image;

    Entity Spawn(World& world)
    {
        Entity e = world.Spawn();
        e.Add<Transform>(Transform{.Position = {Pos(Rng), Pos(Rng)}});
        e.Add<Velocity>(Velocity{.Linear = {Speed(Rng), Speed(Rng)}});
        e.Add<SpriteRenderer>(SpriteRenderer{.Image = Image, .Layer = 0});
        e.Add<Collider>(Collider::MakeCircle(6.0f));
        return e;
    }
};

// Bytes the World's main storages hold (capacity, not size): growth here would be a leak.
usize StorageBytes(World& world)
{
    entt::registry& r = world.GetRegistry();
    return r.storage<entt::entity>().capacity() * sizeof(entt::entity) +
           r.storage<Transform>().capacity() * sizeof(Transform) +
           r.storage<Velocity>().capacity() * sizeof(Velocity) +
           r.storage<SpriteRenderer>().capacity() * sizeof(SpriteRenderer) +
           r.storage<Collider>().capacity() * sizeof(Collider);
}

void SpawnDestroy(const Sprite& image)
{
    constexpr usize kCount = 100'000;
    World world;
    Spawner spawner{.Image = image};
    std::vector<Entity> entities;
    entities.reserve(kCount);
    f64 spawnMs = 1e30;
    f64 destroyMs = 1e30;
    for (i32 run = 0; run < 5; ++run) {
        entities.clear();
        spawnMs = std::min(spawnMs, BestOfMs(1, [&] {
                               for (usize i = 0; i < kCount; ++i)
                                   entities.push_back(spawner.Spawn(world));
                           }));
        destroyMs = std::min(destroyMs, BestOfMs(1, [&] {
                                 for (const Entity e : entities)
                                     e.Destroy();
                                 world.Flush();
                             }));
    }
    std::printf("spawn   %zu entities (4 components): %7.2f ms  (%5.1f M/s)\n", kCount, spawnMs,
                static_cast<f64>(kCount) / spawnMs / 1e3);
    std::printf("destroy %zu entities (deferred + Flush): %5.2f ms  (%5.1f M/s)\n", kCount,
                destroyMs, static_cast<f64>(kCount) / destroyMs / 1e3);
}

void Churn(const Sprite& image)
{
    constexpr usize kPopulation = 10'000;
    constexpr usize kPerFrame = 1'000;
    constexpr i32 kFrames = 1'000;
    World world;
    Spawner spawner{.Image = image};
    std::vector<Entity> live;
    for (usize i = 0; i < kPopulation; ++i)
        live.push_back(spawner.Spawn(world));
    usize next = 0; // the oldest entity in `live` (a ring)
    const auto frame = [&] {
        for (usize i = 0; i < kPerFrame; ++i) {
            live[next].Destroy();
            live[next] = spawner.Spawn(world);
            next = (next + 1) % kPopulation;
        }
        world.Flush();
    };
    for (i32 i = 0; i < 20; ++i) // warm up: let the storages reach their steady size
        frame();
    const usize before = StorageBytes(world);
    const auto start = Clock::now();
    for (i32 i = 0; i < kFrames; ++i)
        frame();
    const f64 ms = std::chrono::duration<f64, std::milli>(Clock::now() - start).count();
    const usize after = StorageBytes(world);
    const f64 churned = static_cast<f64>(kPerFrame) * kFrames;
    std::printf("churn   %zu alive, %zu spawned + %zu destroyed per frame, %d frames: %.2f ms per "
                "frame, %.1f M spawn+destroy/s\n",
                kPopulation, kPerFrame, kPerFrame, kFrames, ms / kFrames, churned / ms / 1e3);
    std::printf("        alive %zu, storages %zu KiB before -> %zu KiB after (%llu spawned in "
                "total)\n",
                world.GetCount(), before / 1024, after / 1024,
                static_cast<unsigned long long>(world.GetSpawnedTotal()));
}

void Systems(const Sprite& image, usize count)
{
    World world;
    const f32 side = 2000.0f * std::sqrt(static_cast<f32>(count) / 10'000.0f);
    Spawner spawner{.Pos = std::uniform_real_distribution<f32>(0.0f, side), .Image = image};
    for (usize i = 0; i < count; ++i)
        spawner.Spawn(world);
    CollisionSystem collisions(16.0f);
    Renderer2D r;

    const f64 moveMs = BestOfMs(20, [&] { UpdateMovement(world, 1.0f / 120.0f); });
    collisions.Update(world); // first insert
    usize hits = 0;
    const f64 collideMs = BestOfMs(20, [&] {
        UpdateMovement(world, 1.0f / 120.0f);
        hits = collisions.Update(world).size();
    });
    u32 drawn = 0;
    const f64 drawMs = BestOfMs(20, [&] {
        r.Clear();
        r.Begin(Mat4::OrthoPixelSpace(side, side));
        drawn = DrawSprites(world, r);
        r.End();
    });
    std::printf("systems %6zu entities: movement %6.3f ms, collisions %6.3f ms (%zu candidate "
                "pairs, %zu contacts), sprites %6.3f ms (%u drawn)\n",
                count, moveMs, collideMs, collisions.GetCandidateCount(), hits, drawMs, drawn);
}

} // namespace

int main()
{
    const Texture texture = Texture::CreateWithoutGpu(16, 16);
    const Sprite image = Sprite::FromTexture(texture);
    SpawnDestroy(image);
    Churn(image);
    for (const usize count : {1'000u, 10'000u, 50'000u})
        Systems(image, count);
    return 0;
}
