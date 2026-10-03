#pragma once

// Systems: plain functions (and one class, for the broadphase it keeps between frames) that a
// scene calls on its World, in the order it likes:
//
//   void OnFixedUpdate(f32 dt) override
//   {
//       UpdateMovement(m_World, dt);
//       for (const CollisionEvent& hit : m_Collisions.Update(m_World))
//           ...;                    // e.g. hit.A.Destroy() - deferred, so it is safe here
//   }
//   void OnUpdate(f32 dt) override
//   {
//       UpdateAnimation(m_World, dt);
//       m_World.Flush();            // the destroyed entities go
//   }
//   void OnRender2D(Renderer2D& r) override
//   {
//       r.Begin(m_Camera);
//       DrawSprites(m_World, r, {.Visible = m_Camera.GetVisibleBounds()});
//       r.End();
//   }

#include <memory_resource>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "Emerald/Core/Defines.h"
#include "Emerald/Entity/Components.h"
#include "Emerald/Entity/World.h"
#include "Emerald/Physics/SpatialHash.h"
#include "Emerald/Renderer/Camera2D.h"

namespace Emerald {

class Renderer2D;

// Transform += Velocity * dt (position and rotation).
void UpdateMovement(World& world, f32 dt);

// Advances every Animator by dt (its OnFinished / OnLoop events fire from here).
void UpdateAnimation(World& world, f32 dt);

// Two colliders touching. Contact.Normal points from A towards B: moving B by Normal * Depth
// separates them. A has the smaller id; each pair is reported once per update.
struct CollisionEvent {
    Entity A;
    Entity B;
    Contact Hit;
};

// Colliders: a SpatialHash broadphase kept up to date from every Transform + Collider, then the
// exact test (Physics/Collision.h) on the candidate pairs it finds.
class CollisionSystem {
public:
    // cellSize: about the size of a typical collider. wrapSize: space repeats (see SpatialHash).
    explicit CollisionSystem(f32 cellSize = 64.0f, std::optional<Vec2> wrapSize = std::nullopt);

    // Moves the colliders into the broadphase (dropping entities that lost theirs or were
    // destroyed) and returns the pairs that touch, sorted by (A, B). Valid until the next call.
    std::span<const CollisionEvent> Update(World& world);

    [[nodiscard]] std::span<const CollisionEvent> GetEvents() const { return m_Events; }
    [[nodiscard]] const SpatialHash& GetBroadphase() const { return m_Hash; }
    // Pairs whose bounding boxes overlapped in the last update (before the exact test).
    [[nodiscard]] usize GetCandidateCount() const { return m_Candidates.size(); }

private:
    SpatialHash m_Hash;
    std::vector<u32> m_Tracked; // ids in the hash, sorted
    std::vector<u32> m_Seen;    // this update's ids
    std::vector<u32> m_Gone;
    std::vector<std::pair<u32, u32>> m_Candidates;
    std::vector<CollisionEvent> m_Events;
};

struct DrawSpritesOptions {
    // Within a layer, lower on screen (larger y) is drawn later, i.e. in front: top-down depth.
    bool SortByY = true;
    // Skips sprites outside this area (world units), e.g. Camera2D::GetVisibleBounds().
    std::optional<Rect2D> Visible{};
    // For the per-frame sort list, e.g. Application::GetFrameAllocator(); null: the heap.
    std::pmr::memory_resource* Scratch = nullptr;
};

// Draws every Transform + SpriteRenderer, sorted by layer (then y), into the current batch: call
// it between Renderer2D::Begin and End. Returns how many sprites it drew.
u32 DrawSprites(World& world, Renderer2D& r, const DrawSpritesOptions& options = {});

} // namespace Emerald
