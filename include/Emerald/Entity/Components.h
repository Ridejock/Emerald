#pragma once

// Common components for the systems in Systems.h. All plain structs: set them with designated
// initializers, e.g. e.Add<Velocity>(Velocity{.Linear = {40.0f, 0.0f}}).

#include "Emerald/Core/Defines.h"
#include "Emerald/Math/Vec2.h"
#include "Emerald/Physics/Collision.h"
#include "Emerald/Renderer/Animation.h"
#include "Emerald/Renderer/Renderer2D.h"
#include "Emerald/Renderer/Sprite.h"

namespace Emerald {

// Where the entity is: Position (world units), Rotation (radians, clockwise on screen), Scale.
// The same struct Renderer2D uses for shapes. There is no parent / child hierarchy: an entity
// that follows another copies its position in the game's update.
using Transform = Transform2D;

// Moved by UpdateMovement: units per second and radians per second.
struct Velocity {
    Vec2 Linear{};
    f32 Angular = 0.0f;
};

// Drawn by DrawSprites at the entity's Transform (its rotation and scale combine with the
// options'). With an Animator on the same entity, the animator's current frame is drawn instead
// of Image. Lower layers are drawn first. (C4324: see SpriteOptions in Sprite.h.)
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324)
#endif
struct SpriteRenderer {
    Sprite Image{};
    SpriteOptions Options{};
    i32 Layer = 0;
};
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

// The animator (Animation.h) is a component as it is: UpdateAnimation advances it.

// A circle or an axis-aligned box around the Transform's position (plus Offset), in world
// units; the transform's rotation and scale do not change it. CollisionSystem reports two
// colliders touching when each one's Layer is in the other's Mask (bit masks: up to 32 layers).
enum class ColliderShape : u8 { Circle, Box };

struct Collider {
    ColliderShape Shape = ColliderShape::Circle;
    f32 Radius = 0.0f; // Circle
    Vec2 HalfSize{};   // Box
    Vec2 Offset{};     // from the Transform's position
    u32 Layer = 1u;    // what this collider is
    u32 Mask = ~0u;    // what it collides with

    [[nodiscard]] static Collider MakeCircle(f32 radius, u32 layer = 1u, u32 mask = ~0u)
    {
        return {.Shape = ColliderShape::Circle, .Radius = radius, .Layer = layer, .Mask = mask};
    }
    [[nodiscard]] static Collider MakeBox(Vec2 halfSize, u32 layer = 1u, u32 mask = ~0u)
    {
        return {.Shape = ColliderShape::Box, .HalfSize = halfSize, .Layer = layer, .Mask = mask};
    }

    // The bounding box at `position` (the Transform's).
    [[nodiscard]] Aabb GetBounds(Vec2 position) const
    {
        const Vec2 half = Shape == ColliderShape::Circle ? Vec2(Radius) : HalfSize;
        return Aabb::FromCenter(position + Offset, half);
    }
};

} // namespace Emerald
