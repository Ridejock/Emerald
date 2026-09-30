#include "Emerald/Particles/ParticleSystem.h"

#include <algorithm>
#include <cmath>

#if EMERALD_MATH_HAS_SSE2
#include <emmintrin.h>
#endif

namespace Emerald {

u32 ContinuousEmitter::Advance(f32 rate, f32 dt)
{
    m_Carry += std::max(rate, 0.0f) * dt;
    const f32 whole = std::floor(m_Carry);
    m_Carry -= whole;
    return static_cast<u32>(whole);
}

ParticleSystem::ParticleSystem(u32 capacity) : m_Capacity(capacity)
{
    // The only allocations: every array gets its full size up front.
    for (std::vector<f32>* array :
         {&m_PositionX, &m_PositionY, &m_VelocityX, &m_VelocityY, &m_GravityX, &m_GravityY, &m_Drag,
          &m_Life, &m_InverseLifetime, &m_StartSize, &m_EndSize})
        array->resize(capacity);
    m_StartColor.resize(capacity);
    m_EndColor.resize(capacity);
}

f32 ParticleSystem::RandomFloat(f32 min, f32 max)
{
    // xorshift32: tiny and fast; plenty for effects.
    m_RandomState ^= m_RandomState << 13;
    m_RandomState ^= m_RandomState >> 17;
    m_RandomState ^= m_RandomState << 5;
    const f32 unit = static_cast<f32>(m_RandomState >> 8) * (1.0f / 16777216.0f); // [0, 1)
    return min + (max - min) * unit;
}

void ParticleSystem::Emit(const ParticleEmitterConfig& config, const Vec2& position, u32 count,
                          f32 direction, const Vec2& baseVelocity)
{
    for (u32 n = 0; n < count; ++n) {
        if (m_Count == m_Capacity) {
            m_Dropped += count - n; // full: the rest are dropped
            return;
        }
        Vec2 start = position;
        if (config.Shape == EmitterShape::Circle) {
            // sqrt spreads them evenly over the area instead of bunching them at the center.
            const f32 radius = config.Radius * std::sqrt(RandomFloat(0.0f, 1.0f));
            const f32 angle = RandomFloat(0.0f, TwoPi);
            start += Vec2{std::cos(angle), std::sin(angle)} * radius;
        } else if (config.Shape == EmitterShape::Line) {
            start += config.LineHalfExtent * RandomFloat(-1.0f, 1.0f);
        }
        const f32 angle = direction + RandomFloat(config.Angle.Min, config.Angle.Max);
        const f32 speed = RandomFloat(config.Speed.Min, config.Speed.Max);
        const f32 lifetime =
            std::max(RandomFloat(config.Lifetime.Min, config.Lifetime.Max), 0.001f);

        const u32 i = m_Count++;
        m_PositionX[i] = start.x;
        m_PositionY[i] = start.y;
        m_VelocityX[i] = std::cos(angle) * speed + baseVelocity.x;
        m_VelocityY[i] = std::sin(angle) * speed + baseVelocity.y;
        m_GravityX[i] = config.Gravity.x;
        m_GravityY[i] = config.Gravity.y;
        m_Drag[i] = config.Drag;
        m_Life[i] = lifetime;
        m_InverseLifetime[i] = 1.0f / lifetime;
        m_StartSize[i] = config.StartSize;
        m_EndSize[i] = config.EndSize;
        m_StartColor[i] = config.StartColor;
        m_EndColor[i] = config.EndColor;
    }
}

void ParticleSystem::EmitContinuous(const ParticleEmitterConfig& config, ContinuousEmitter& emitter,
                                    const Vec2& position, f32 dt, f32 direction,
                                    const Vec2& baseVelocity)
{
    Emit(config, position, emitter.Advance(config.Rate, dt), direction, baseVelocity);
}

void ParticleSystem::Update(f32 dt)
{
#if EMERALD_MATH_USE_SSE
    UpdateSse(dt);
#else
    UpdateScalar(dt);
#endif
}

void ParticleSystem::UpdateScalar(f32 dt)
{
    IntegrateScalar(0, m_Count, dt);
    RemoveDead();
}

// The same steps as the SSE loop below, one particle at a time:
//   velocity = velocity * max(1 - drag * dt, 0) + gravity * dt
//   position += velocity * dt
//   life -= dt
void ParticleSystem::IntegrateScalar(u32 first, u32 end, f32 dt)
{
    for (u32 i = first; i < end; ++i) {
        // (max(x, 0) rather than max(0, x) so -0 behaves exactly like _mm_max_ps below.)
        const f32 keep = std::max(1.0f - m_Drag[i] * dt, 0.0f);
        m_VelocityX[i] = m_VelocityX[i] * keep + m_GravityX[i] * dt;
        m_VelocityY[i] = m_VelocityY[i] * keep + m_GravityY[i] * dt;
        m_PositionX[i] += m_VelocityX[i] * dt;
        m_PositionY[i] += m_VelocityY[i] * dt;
        m_Life[i] -= dt;
    }
}

void ParticleSystem::UpdateSse(f32 dt)
{
#if EMERALD_MATH_HAS_SSE2
    // Four particles per step: each __m128 holds the same field of particles i..i+3, which is
    // exactly what the structure-of-arrays layout gives us with one load.
    const __m128 step = _mm_set1_ps(dt);
    const __m128 one = _mm_set1_ps(1.0f);
    const __m128 zero = _mm_setzero_ps();
    const u32 end4 = m_Count & ~3u; // the rest (0-3) go through the scalar loop
    for (u32 i = 0; i < end4; i += 4) {
        const __m128 keep =
            _mm_max_ps(_mm_sub_ps(one, _mm_mul_ps(_mm_loadu_ps(&m_Drag[i]), step)), zero);
        const __m128 vx = _mm_add_ps(_mm_mul_ps(_mm_loadu_ps(&m_VelocityX[i]), keep),
                                     _mm_mul_ps(_mm_loadu_ps(&m_GravityX[i]), step));
        const __m128 vy = _mm_add_ps(_mm_mul_ps(_mm_loadu_ps(&m_VelocityY[i]), keep),
                                     _mm_mul_ps(_mm_loadu_ps(&m_GravityY[i]), step));
        _mm_storeu_ps(&m_VelocityX[i], vx);
        _mm_storeu_ps(&m_VelocityY[i], vy);
        _mm_storeu_ps(&m_PositionX[i],
                      _mm_add_ps(_mm_loadu_ps(&m_PositionX[i]), _mm_mul_ps(vx, step)));
        _mm_storeu_ps(&m_PositionY[i],
                      _mm_add_ps(_mm_loadu_ps(&m_PositionY[i]), _mm_mul_ps(vy, step)));
        _mm_storeu_ps(&m_Life[i], _mm_sub_ps(_mm_loadu_ps(&m_Life[i]), step));
    }
    IntegrateScalar(end4, m_Count, dt);
    RemoveDead();
#else
    UpdateScalar(dt);
#endif
}

void ParticleSystem::RemoveDead()
{
    for (u32 i = 0; i < m_Count;) {
        if (m_Life[i] > 0.0f) {
            ++i;
            continue;
        }
        // Swap-remove: move the last particle into this slot, then check the slot again.
        const u32 last = --m_Count;
        if (i == last)
            break;
        m_PositionX[i] = m_PositionX[last];
        m_PositionY[i] = m_PositionY[last];
        m_VelocityX[i] = m_VelocityX[last];
        m_VelocityY[i] = m_VelocityY[last];
        m_GravityX[i] = m_GravityX[last];
        m_GravityY[i] = m_GravityY[last];
        m_Drag[i] = m_Drag[last];
        m_Life[i] = m_Life[last];
        m_InverseLifetime[i] = m_InverseLifetime[last];
        m_StartSize[i] = m_StartSize[last];
        m_EndSize[i] = m_EndSize[last];
        m_StartColor[i] = m_StartColor[last];
        m_EndColor[i] = m_EndColor[last];
    }
}

void ParticleSystem::Draw(Renderer2D& r, const ParticleDrawOptions& options) const
{
    if (m_Count == 0)
        return;
    const BlendMode previous = r.GetBlendMode();
    r.SetBlendMode(options.Blend);
    for (u32 i = 0; i < m_Count; ++i) {
        // Age from 0 (just born) to 1 (about to die) picks the color and size in between.
        const f32 t = Clamp(1.0f - m_Life[i] * m_InverseLifetime[i], 0.0f, 1.0f);
        const Vec4 color = Lerp(m_StartColor[i], m_EndColor[i], t);
        const f32 size = Lerp(m_StartSize[i], m_EndSize[i], t) * options.SizeScale;
        if (size <= 0.0f || color.w <= 0.0f)
            continue;
        const Vec2 position{m_PositionX[i], m_PositionY[i]};
        if (options.Sprite) {
            r.DrawSprite(*options.Sprite, position, {.Size = {size, size}, .Tint = color});
            continue;
        }
        // A streak trailing behind the particle, `size` long, along its motion.
        const Vec2 velocity{m_VelocityX[i], m_VelocityY[i]};
        const f32 speed = Length(velocity);
        const Vec2 along = speed > 1e-4f ? velocity * (1.0f / speed) : Vec2{1.0f, 0.0f};
        r.DrawLine(position - along * size, position, color);
    }
    r.SetBlendMode(previous);
}

} // namespace Emerald
