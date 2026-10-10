#pragma once

// Platformer character physics: a kinematic box (no forces, no rigid bodies) that runs, jumps
// and falls against a Tilemap's collision, with slopes, one-way platforms, coyote time and jump
// buffering. Call StepPlatformer once per fixed update with that step's input:
//
//   PlatformerBody hero{.Position = spawn, .HalfSize = {5.0f, 7.0f}};
//   PlatformerTunables tunables;                     // or your own numbers
//   // OnFixedUpdate(dt):
//   const PlatformerInput in{.Move = input.GetAxis("MoveX"),
//                            .JumpPressed = input.WasActionPressed("Jump"),
//                            .JumpHeld = input.IsActionDown("Jump"),
//                            .Down = input.GetAxis("MoveY") > 0.5f};
//   StepPlatformer(hero, in, tunables, *map, dt);
//
// Everything is plain float math on the body's own state, so the same inputs at the same fixed
// step always give the same path, bit for bit (PlatformerTests checks this).

#include "Emerald/Core/Defines.h"
#include "Emerald/Math/Vec2.h"
#include "Emerald/Physics/Collision.h"

namespace Emerald {

class Tilemap;

// The feel of the character. Units: map pixels and seconds.
struct PlatformerTunables {
    f32 Gravity = 1400.0f;      // px/s^2, pulling down
    f32 MaxFallSpeed = 420.0f;  // px/s
    f32 RunSpeed = 140.0f;      // px/s at full stick / key
    f32 GroundAccel = 1600.0f;  // px/s^2 towards the run speed on the ground...
    f32 GroundDecel = 2000.0f;  // ...and back to 0 with no input...
    f32 TurnAccel = 2400.0f;    // ...and when the input points against the motion (turning)
    f32 AirAccel = 1000.0f;     // px/s^2 in the air, both ways (less control than on the ground)
    f32 JumpVelocity = 400.0f;  // px/s up when the jump starts (height = v^2 / 2g, ~57 px)
    f32 JumpCut = 0.45f;        // releasing jump while rising multiplies the up speed by this
    f32 CoyoteTime = 0.10f;     // s after walking off a ledge in which a jump still works
    f32 JumpBuffer = 0.12f;     // s a jump pressed in the air is remembered until landing
    f32 SnapDown = 4.0f;        // px: keep to the ground walking down slopes (0: off)
    f32 DropThroughTime = 0.2f; // s one-way platforms are ignored after down + jump
};

// One step's input. Move is -1..1 (left / right); Down + a jump on a one-way platform drops
// through it instead of jumping.
struct PlatformerInput {
    f32 Move = 0.0f;
    bool JumpPressed = false; // pressed during this step
    bool JumpHeld = false;    // still down (releasing early makes a lower jump)
    bool Down = false;
};

// The character's state. Position is the center of its collision box.
struct PlatformerBody {
    Vec2 Position{};
    Vec2 HalfSize{5.0f, 7.0f};
    Vec2 Velocity{}; // px/s, +y down
    bool Grounded = false;
    bool OnSlope = false;   // grounded on a slope
    bool OnOneWay = false;  // grounded on one-way tiles only (down + jump drops through)
    bool Rising = false;    // going up from a jump that can still be cut short
    f32 CoyoteTimer = 0.0f; // > 0: a jump works although not grounded
    f32 BufferTimer = 0.0f; // > 0: a jump was pressed recently and is waiting for the ground
    f32 DropTimer = 0.0f;   // > 0: falling through one-way platforms
    // What happened in the last step (for sounds, animation and the HUD).
    bool Jumped = false;
    bool Landed = false;
    bool DroppedThrough = false;
    bool HitWall = false;
    bool HitCeiling = false;

    [[nodiscard]] Aabb GetBox() const { return {Position - HalfSize, Position + HalfSize}; }
    [[nodiscard]] Vec2 GetFeet() const { return {Position.x, Position.y + HalfSize.y}; }
};

// Advances the body by one fixed step `dt`: timers, run, jump / drop, gravity, then the move
// against the map's tiles (Tilemap::MoveAndCollide).
void StepPlatformer(PlatformerBody& body, const PlatformerInput& input,
                    const PlatformerTunables& tunables, const Tilemap& map, f32 dt);

} // namespace Emerald
