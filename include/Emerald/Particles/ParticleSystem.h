#pragma once

#include <vector>

#include "Emerald/Core/Defines.h"
#include "Emerald/Math/Common.h"
#include "Emerald/Math/Vec2.h"
#include "Emerald/Math/Vec4.h"
#include "Emerald/Renderer/Renderer2D.h"
#include "Emerald/Renderer/Sprite.h"

namespace Emerald {

// A random value between Min and Max (both included).
struct FloatRange {
    f32 Min = 0.0f;
    f32 Max = 0.0f;
};

// Where new particles appear, around the position passed to Emit.
enum class EmitterShape : u8 {
    Point,  // exactly at the position
    Circle, // anywhere inside a circle of Radius
    Line,   // anywhere on the line from position - LineHalfExtent to position + LineHalfExtent
};

// How one kind of particle starts and changes over its life, e.g. "rock dust" or "engine
// exhaust". Every field has a default, so a config only names what it changes:
//
//   const ParticleEmitterConfig kSparks{.StartColor = {1, 0.8f, 0.4f, 1},
//                                       .Speed = {80, 200}, .Lifetime = {0.3f, 0.6f}};
//
// Colors and sizes are interpolated linearly from Start to End over each particle's life.
// (The two colors come first: Vec4 is 16-byte aligned, and this order keeps the struct free of
// the padding that would trigger MSVC warning C4324.)
struct ParticleEmitterConfig {
    Vec4 StartColor{1.0f, 1.0f, 1.0f, 1.0f};
    Vec4 EndColor{1.0f, 1.0f, 1.0f, 0.0f};

    EmitterShape Shape = EmitterShape::Point;
    f32 Radius = 0.0f;     // Circle
    Vec2 LineHalfExtent{}; // Line

    FloatRange Speed{0.0f, 0.0f};  // units per second
    FloatRange Angle{0.0f, TwoPi}; // radians, added to Emit's direction
    FloatRange Lifetime{1.0f, 1.0f};
    f32 Drag = 0.0f; // fraction of the velocity lost per second (0 = none, 2 = gone in 0.5 s)
    Vec2 Gravity{};  // units per second squared

    f32 StartSize = 2.0f; // line length or sprite size, in units
    f32 EndSize = 0.0f;

    f32 Rate = 0.0f; // particles per second, for EmitContinuous
};

// Keeps the fraction of a particle that EmitContinuous could not emit yet, so a rate of, say,
// 45 per second comes out right at any frame rate. One per continuous source (e.g. an engine).
class ContinuousEmitter {
public:
    // How many particles to emit now for `rate` particles per second over `dt` seconds.
    [[nodiscard]] u32 Advance(f32 rate, f32 dt);
    void Reset() { m_Carry = 0.0f; }

private:
    f32 m_Carry = 0.0f;
};

// How Draw shows the particles.
struct ParticleDrawOptions {
    // nullptr: each particle is a short line along its velocity (vector look); otherwise a quad
    // of this sprite (e.g. a soft dot or a white pixel), tinted with the particle's color.
    const Emerald::Sprite* Sprite = nullptr;
    // Additive: overlapping particles add up and glow, which suits light (sparks, fire).
    BlendMode Blend = BlendMode::Additive;
    // Multiplies every particle's size, e.g. to reuse one effect at another resolution.
    f32 SizeScale = 1.0f;
};

// A fixed-capacity pool of simple particles: position, velocity, drag and gravity, with color and
// size fading over their lifetime.
//
//   ParticleSystem particles(4096);          // allocates once; never again
//   particles.Emit(kSparks, rockPosition, 20); // a burst of 20
//   particles.EmitContinuous(kExhaust, m_Engine, nozzle, dt, backwards, shipVelocity);
//   particles.Update(dt);                     // move, age and remove the dead ones
//   particles.Draw(r);                        // inside Renderer2D::Begin/End
//
// Memory layout: structure of arrays (one array per field: all X positions, then all Y
// positions, ...), so Update streams through a few arrays with the same operation for every
// element - the shape SIMD needs. A dead particle is replaced by the last one (swap-remove), so
// the live ones always fill indices 0..count-1 and removal is O(1). When the pool is full, new
// particles are dropped (see GetDroppedCount).
class ParticleSystem {
public:
    explicit ParticleSystem(u32 capacity);

    // `count` new particles around `position`. `direction` (radians) turns the config's Angle
    // range, e.g. to point exhaust backwards; `baseVelocity` is added to every particle's
    // velocity, e.g. the ship's, so the trail moves with it.
    void Emit(const ParticleEmitterConfig& config, const Vec2& position, u32 count,
              f32 direction = 0.0f, const Vec2& baseVelocity = {});
    // config.Rate particles per second on average.
    void EmitContinuous(const ParticleEmitterConfig& config, ContinuousEmitter& emitter,
                        const Vec2& position, f32 dt, f32 direction = 0.0f,
                        const Vec2& baseVelocity = {});

    // Moves and ages every particle by `dt` seconds and removes the dead ones. Uses the SSE path
    // when the math library does (EMERALD_MATH_SIMD on an x86 CPU), else the scalar one. Both
    // give the same results; the two are public for tests and benchmarks.
    void Update(f32 dt);
    void UpdateScalar(f32 dt);
    void UpdateSse(f32 dt); // falls back to UpdateScalar on CPUs without SSE2

    // Records the particles into the current Renderer2D batch (between Begin and End). Restores
    // the batch's previous blend mode afterwards.
    void Draw(Renderer2D& r, const ParticleDrawOptions& options = {}) const;

    void Clear() { m_Count = 0; }
    // The random numbers for spawning; set a seed for repeatable effects (tests).
    void SetSeed(u32 seed) { m_RandomState = seed != 0 ? seed : 1u; }

    [[nodiscard]] u32 GetCount() const { return m_Count; }
    [[nodiscard]] u32 GetCapacity() const { return m_Capacity; }
    [[nodiscard]] u64 GetDroppedCount() const { return m_Dropped; }

    // One particle's state (i < GetCount()), for tests and debugging.
    [[nodiscard]] Vec2 GetPosition(u32 i) const { return {m_PositionX[i], m_PositionY[i]}; }
    [[nodiscard]] Vec2 GetVelocity(u32 i) const { return {m_VelocityX[i], m_VelocityY[i]}; }
    [[nodiscard]] f32 GetLife(u32 i) const { return m_Life[i]; } // seconds left

private:
    // Physics for particles [first, end): the part the SSE path replaces.
    void IntegrateScalar(u32 first, u32 end, f32 dt);
    void RemoveDead();
    [[nodiscard]] f32 RandomFloat(f32 min, f32 max);

    u32 m_Capacity = 0;
    u32 m_Count = 0;
    u64 m_Dropped = 0;
    u32 m_RandomState = 0x9E3779B9u;

    // Updated every step (the "hot" data).
    std::vector<f32> m_PositionX, m_PositionY;
    std::vector<f32> m_VelocityX, m_VelocityY;
    std::vector<f32> m_GravityX, m_GravityY;
    std::vector<f32> m_Drag;
    std::vector<f32> m_Life; // seconds left
    // Only read when drawing.
    std::vector<f32> m_InverseLifetime; // 1 / total lifetime, to get the 0..1 age
    std::vector<f32> m_StartSize, m_EndSize;
    std::vector<Vec4> m_StartColor, m_EndColor;
};

} // namespace Emerald
