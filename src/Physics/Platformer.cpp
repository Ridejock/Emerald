#include "Emerald/Physics/Platformer.h"

#include <cmath>

#include "Emerald/Core/Profile.h"
#include "Emerald/Math/Common.h"
#include "Emerald/Tilemap/Tilemap.h"

namespace Emerald {

namespace {

// Moves `value` towards `target` by at most `step`.
f32 Approach(f32 value, f32 target, f32 step)
{
    return value < target ? Min(value + step, target) : Max(value - step, target);
}

// The box's left and right edges overlap (touching is not enough).
bool OverlapsX(const Aabb& a, const Aabb& b)
{
    return a.Min.x < b.Max.x && b.Min.x < a.Max.x;
}

// Close enough to count as standing on a platform's top (its float math is not exact).
constexpr f32 kOnTop = 0.01f;

} // namespace

void StepPlatformer(PlatformerBody& body, const PlatformerInput& input,
                    const PlatformerTunables& tunables, const Tilemap& map, f32 dt)
{
    StepPlatformer(body, input, tunables, map, dt, {});
}

void StepPlatformer(PlatformerBody& body, const PlatformerInput& input,
                    const PlatformerTunables& tunables, const Tilemap& map, f32 dt,
                    std::span<const PlatformerPlatform> platforms)
{
    EM_PROFILE_FUNCTION();
    const bool wasGrounded = body.Grounded;

    // Riding: standing on a platform last step, move with it first (against the tiles).
    if (body.OnPlatform) {
        for (const PlatformerPlatform& p : platforms) {
            const Aabb before{p.Box.Min - p.Delta, p.Box.Max - p.Delta};
            if (OverlapsX(body.GetBox(), before) &&
                std::abs(body.GetFeet().y - before.Min.y) <= kOnTop) {
                body.Position += map.MoveAndCollide(body.GetBox(), p.Delta).Delta;
                break;
            }
        }
    }
    body.Jumped = body.Landed = body.DroppedThrough = body.HitWall = body.HitCeiling = false;

    // Jump buffer: a press is remembered for JumpBuffer seconds.
    body.BufferTimer = input.JumpPressed ? tunables.JumpBuffer : Max(body.BufferTimer - dt, 0.0f);
    body.DropTimer = Max(body.DropTimer - dt, 0.0f);

    // Run: accelerate towards the stick's speed (less grip in the air).
    const f32 move = Clamp(input.Move, -1.0f, 1.0f);
    const bool turning = move * body.Velocity.x < 0.0f; // input against the motion
    const f32 accel = !body.Grounded ? tunables.AirAccel
                      : move == 0.0f ? tunables.GroundDecel
                      : turning      ? tunables.TurnAccel
                                     : tunables.GroundAccel;
    body.Velocity.x = Approach(body.Velocity.x, move * tunables.RunSpeed, accel * dt);

    // Jump (or drop) while on the ground or within coyote time of leaving it.
    if (body.BufferTimer > 0.0f && (body.Grounded || body.CoyoteTimer > 0.0f)) {
        if (input.Down && body.Grounded && body.OnOneWay) {
            body.DropTimer = tunables.DropThroughTime;
            body.DroppedThrough = true;
        } else {
            body.Velocity.y = -tunables.JumpVelocity;
            body.Rising = true;
            body.Jumped = true;
        }
        body.Grounded = false;
        body.BufferTimer = body.CoyoteTimer = 0.0f; // one press, one jump
    }

    // Variable height: letting go of jump while rising cuts the rest of the climb.
    if (body.Rising && (body.Velocity.y >= 0.0f || !input.JumpHeld)) {
        if (body.Velocity.y < 0.0f)
            body.Velocity.y *= tunables.JumpCut;
        body.Rising = false;
    }

    body.Velocity.y = Min(body.Velocity.y + tunables.Gravity * dt, tunables.MaxFallSpeed);

    // Move against the tiles. Snapping down only applies while walking on the ground (not
    // after a jump or drop), so the body follows slopes down instead of hopping.
    const TileMoveOptions options{.IgnoreOneWay = body.DropTimer > 0.0f,
                                  .SnapDown = body.Grounded ? tunables.SnapDown : 0.0f};
    const f32 bottomBefore = body.GetFeet().y;
    TileMove hit = map.MoveAndCollide(body.GetBox(), body.Velocity * dt, options);
    body.Position += hit.Delta;

    // Platforms: land on the highest top the feet crossed going down (from at or above it).
    bool onPlatform = false;
    if (body.DropTimer <= 0.0f && body.Velocity.y >= 0.0f) {
        const f32 bottom = body.GetFeet().y;
        f32 top = bottom + 1.0f;
        for (const PlatformerPlatform& p : platforms) {
            const f32 y = p.Box.Min.y;
            if (OverlapsX(body.GetBox(), p.Box) && bottomBefore <= y + kOnTop && bottom >= y &&
                y < top) {
                top = y;
                onPlatform = true;
            }
        }
        if (onPlatform) {
            // A tile floor stops the move above any platform below it, so only one at the same
            // height can still be under the feet: then it's solid (no drop) unless it's one-way.
            body.Position.y = top - body.HalfSize.y;
            hit.OnOneWay = !hit.HitBottom || hit.OnOneWay;
            hit.HitBottom = true;
        }
    }

    if (hit.HitLeft || hit.HitRight) {
        body.Velocity.x = 0.0f;
        body.HitWall = true;
    }
    if (hit.HitTop && body.Velocity.y < 0.0f) {
        body.Velocity.y = 0.0f;
        body.Rising = false;
        body.HitCeiling = true;
    }
    body.Grounded = hit.HitBottom && body.Velocity.y >= 0.0f;
    if (body.Grounded)
        body.Velocity.y = 0.0f;
    body.OnSlope = body.Grounded && hit.OnSlope;
    body.OnOneWay = body.Grounded && hit.OnOneWay;
    body.OnPlatform = body.Grounded && onPlatform;
    body.Landed = body.Grounded && !wasGrounded;

    // Coyote time: full while grounded, then counting down (a jump or drop clears it above).
    body.CoyoteTimer = body.Grounded ? tunables.CoyoteTime : Max(body.CoyoteTimer - dt, 0.0f);
}

} // namespace Emerald
