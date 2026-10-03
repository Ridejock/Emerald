#pragma once

// Entities: game objects made of components (plain structs), stored by EnTT. This is a thin layer
// over entt::registry, so game code reads like Emerald code and does not depend on EnTT's API:
//
//   World world;                                    // usually a member of a Scene
//   Entity ship = world.Spawn();
//   ship.Add<Transform>(Transform{.Position = {100.0f, 50.0f}});
//   ship.Add<Velocity>(Velocity{.Linear = {40.0f, 0.0f}});
//   world.Each<Transform, Velocity>([](Entity e, Transform& t, Velocity& v) { ... });
//   ship.Destroy();                                 // deferred: gone at the next Flush()
//   world.Flush();                                  // once per frame, at a safe point
//
// Components are any movable struct; an entity has at most one of each type. Common ones and the
// systems that use them are in Components.h and Systems.h. GetRegistry() is there for anything
// this layer does not cover (groups, sorting, signals...).
//
// During Each you may spawn entities, add components and Destroy anything (destruction waits for
// Flush). Removing components of other entities than the current one is not allowed while
// iterating them.

#include <utility>
#include <vector>

#include <entt/entity/registry.hpp>

#include "Emerald/Core/Defines.h"

namespace Emerald {

class World;

// A handle: the entity's id plus its world, cheap to copy. Ids are versioned, so a handle to a
// destroyed entity stays invalid even when its slot is reused. Default-constructed: invalid.
class Entity {
public:
    Entity() = default;
    Entity(World* world, entt::entity id) : m_World(world), m_Id(id) {}

    // Exists and is not waiting to be destroyed.
    [[nodiscard]] bool IsValid() const;
    explicit operator bool() const { return IsValid(); }

    // Adds (or replaces) a component, constructed from `args` (brace-initialized for aggregates).
    // Returns it, except for empty "tag" structs (nothing to return).
    template <typename T, typename... Args> decltype(auto) Add(Args&&... args) const;
    template <typename T> [[nodiscard]] T& Get() const;    // must exist
    template <typename T> [[nodiscard]] T* TryGet() const; // null if missing
    template <typename T> [[nodiscard]] bool Has() const;
    template <typename T> void Remove() const;
    // Deferred, see World::Destroy.
    void Destroy() const;

    [[nodiscard]] World* GetWorld() const { return m_World; }
    [[nodiscard]] entt::entity GetId() const { return m_Id; }
    // The id as a number (unique among living entities): for logs, maps and SpatialHash.
    [[nodiscard]] u32 ToU32() const { return static_cast<u32>(entt::to_integral(m_Id)); }

    bool operator==(const Entity&) const = default;

private:
    World* m_World = nullptr;
    entt::entity m_Id = entt::null;
};

class World {
public:
    World() = default;
    // Handles point at their world: it neither copies nor moves.
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    [[nodiscard]] Entity Spawn();
    // Marks the entity: from now on it is invalid and skipped by Each, but its components stay
    // in memory until Flush() destroys it. Safe at any time, also from inside Each. Twice or on
    // an invalid handle: nothing happens.
    void Destroy(Entity entity);
    // Destroys the marked entities (their components' destructors run here). Call it once per
    // frame where nothing is iterating, e.g. at the end of the scene's update.
    void Flush();
    // Destroys every entity now (not from inside Each).
    void Clear();

    // Calls fn(Entity, Ts&...) for every living entity that has all of Ts (components with data;
    // test tags with Has). The order is unspecified.
    template <typename... Ts, typename Fn> void Each(Fn&& fn);

    [[nodiscard]] Entity Wrap(entt::entity id) { return {this, id}; }
    // Living entities (the marked ones count until they are flushed).
    [[nodiscard]] usize GetCount() const { return m_Count; }
    [[nodiscard]] usize GetPendingDestroyCount() const { return m_Doomed.size(); }
    [[nodiscard]] u64 GetSpawnedTotal() const { return m_SpawnedTotal; }
    [[nodiscard]] u64 GetDestroyedTotal() const { return m_DestroyedTotal; }

    // The EnTT registry underneath, for what this layer does not wrap.
    [[nodiscard]] entt::registry& GetRegistry() { return m_Registry; }
    [[nodiscard]] const entt::registry& GetRegistry() const { return m_Registry; }

private:
    friend class Entity;
    struct Doomed {}; // the tag Destroy puts on an entity until Flush

    entt::registry m_Registry;
    std::vector<entt::entity> m_Doomed;
    usize m_Count = 0;
    u64 m_SpawnedTotal = 0;
    u64 m_DestroyedTotal = 0;
};

// --- Templates ---

inline bool Entity::IsValid() const
{
    return m_World && m_World->m_Registry.valid(m_Id) &&
           !m_World->m_Registry.all_of<World::Doomed>(m_Id);
}

template <typename T, typename... Args> decltype(auto) Entity::Add(Args&&... args) const
{
    return m_World->m_Registry.emplace_or_replace<T>(m_Id, std::forward<Args>(args)...);
}

template <typename T> T& Entity::Get() const
{
    return m_World->m_Registry.get<T>(m_Id);
}

template <typename T> T* Entity::TryGet() const
{
    return m_World ? m_World->m_Registry.try_get<T>(m_Id) : nullptr;
}

template <typename T> bool Entity::Has() const
{
    return m_World && m_World->m_Registry.valid(m_Id) && m_World->m_Registry.all_of<T>(m_Id);
}

template <typename T> void Entity::Remove() const
{
    m_World->m_Registry.remove<T>(m_Id);
}

inline void Entity::Destroy() const
{
    if (m_World)
        m_World->Destroy(*this);
}

template <typename... Ts, typename Fn> void World::Each(Fn&& fn)
{
    auto view = m_Registry.view<Ts...>(entt::exclude<Doomed>);
    for (const entt::entity id : view)
        fn(Entity(this, id), view.template get<Ts>(id)...);
}

} // namespace Emerald
