// ParticleSystem: spawning, physics, swap-remove, the capacity limit and scalar vs SSE updates.

#include "Test.h"

#include <cmath>

#include <Emerald/Particles/ParticleSystem.h>
#include <Emerald/Renderer/Texture.h>

using namespace Emerald;

namespace {

// A mix of drag and gravity so every term of the update is exercised.
const ParticleEmitterConfig kSparks{.Shape = EmitterShape::Circle,
                                    .Radius = 5.0f,
                                    .Speed = {20.0f, 120.0f},
                                    .Lifetime = {0.2f, 1.5f},
                                    .Drag = 1.5f,
                                    .Gravity = {0.0f, 40.0f}};

} // namespace

TEST(ParticlesEmitAndCapacity)
{
    ParticleSystem p(10);
    CHECK(p.GetCapacity() == 10 && p.GetCount() == 0);
    p.Emit(kSparks, {0.0f, 0.0f}, 4);
    CHECK(p.GetCount() == 4);
    p.Emit(kSparks, {0.0f, 0.0f}, 9); // only 6 fit
    CHECK(p.GetCount() == 10 && p.GetDroppedCount() == 3);
    p.Clear();
    CHECK(p.GetCount() == 0);
}

TEST(ParticlesSpawnShapesAndRanges)
{
    ParticleSystem p(1000);
    p.SetSeed(123);
    const Vec2 center{100.0f, 50.0f};
    p.Emit(kSparks, center, 500);
    bool inside = true;
    bool inRange = true;
    for (u32 i = 0; i < p.GetCount(); ++i) {
        inside = inside && Distance(p.GetPosition(i), center) <= 5.0f + 1e-4f;
        const f32 speed = Length(p.GetVelocity(i));
        inRange = inRange && speed >= 20.0f - 1e-3f && speed <= 120.0f + 1e-3f;
        inRange = inRange && p.GetLife(i) >= 0.2f && p.GetLife(i) <= 1.5f;
    }
    CHECK(inside);
    CHECK(inRange);

    // A line from (-10, 0) to (10, 0) around the position, all moving straight down (+y).
    p.Clear();
    const ParticleEmitterConfig line{.Shape = EmitterShape::Line,
                                     .LineHalfExtent = {10.0f, 0.0f},
                                     .Speed = {5.0f, 5.0f},
                                     .Angle = {HalfPi, HalfPi}};
    p.Emit(line, {0.0f, 0.0f}, 100, 0.0f, {1.0f, 0.0f});
    bool onLine = true;
    for (u32 i = 0; i < p.GetCount(); ++i) {
        const Vec2 pos = p.GetPosition(i);
        const Vec2 vel = p.GetVelocity(i);
        onLine = onLine && std::fabs(pos.x) <= 10.0f && pos.y == 0.0f;
        onLine = onLine && std::fabs(vel.x - 1.0f) < 1e-4f && std::fabs(vel.y - 5.0f) < 1e-4f;
    }
    CHECK(onLine);
}

TEST(ParticlesPhysics)
{
    // One particle moving right at 10/s, drag 0.5/s, gravity 20/s^2 down: one 0.1 s step.
    ParticleSystem p(4);
    const ParticleEmitterConfig config{.Speed = {10.0f, 10.0f},
                                       .Angle = {0.0f, 0.0f},
                                       .Lifetime = {1.0f, 1.0f},
                                       .Drag = 0.5f,
                                       .Gravity = {0.0f, 20.0f}};
    p.Emit(config, {0.0f, 0.0f}, 1);
    p.UpdateScalar(0.1f);
    // velocity = (10 * 0.95, 20 * 0.1) = (9.5, 2); position = velocity * 0.1
    CHECK_NEAR(p.GetVelocity(0), Vec2(9.5f, 2.0f));
    CHECK_NEAR(p.GetPosition(0), Vec2(0.95f, 0.2f));
    CHECK_NEAR(p.GetLife(0), 0.9f);
}

TEST(ParticlesSwapRemove)
{
    // Lifetimes 1, 3, 2, 4 (seconds): after 1.5 s the first is dead and the last takes its slot.
    ParticleSystem p(8);
    for (i32 i = 0; i < 4; ++i) {
        const f32 life = i == 0 ? 1.0f : i == 1 ? 3.0f : i == 2 ? 2.0f : 4.0f;
        const ParticleEmitterConfig config{.Lifetime = {life, life}};
        p.Emit(config, {static_cast<f32>(i), 0.0f}, 1);
    }
    p.Update(1.5f);
    CHECK(p.GetCount() == 3);
    CHECK_NEAR(p.GetPosition(0), Vec2(3.0f, 0.0f)); // the last one moved into slot 0
    CHECK_NEAR(p.GetPosition(1), Vec2(1.0f, 0.0f));
    CHECK_NEAR(p.GetPosition(2), Vec2(2.0f, 0.0f));
    p.Update(1.0f); // the 2 s one dies (it is last: no swap needed)
    CHECK(p.GetCount() == 2);
    p.Update(10.0f);
    CHECK(p.GetCount() == 0);
}

TEST(ParticlesContinuousRate)
{
    // 45 per second at 60 steps per second: 0.75 per step, 45 after one second.
    ContinuousEmitter emitter;
    u32 total = 0;
    for (i32 i = 0; i < 60; ++i)
        total += emitter.Advance(45.0f, 1.0f / 60.0f);
    CHECK(total == 45 || total == 44); // floating-point rounding may leave the last one pending
    CHECK(emitter.Advance(0.0f, 1.0f) == 0);
}

TEST(ParticlesScalarMatchesSse)
{
    // Two identical pools (same seed, same emits), one updated by each path. The SSE loop does
    // the same operations in the same order, so the results should agree to the last bit; the
    // check allows a tiny tolerance in case a compiler fuses a multiply-add in one of them.
    ParticleSystem scalar(4096);
    ParticleSystem sse(4096);
    scalar.SetSeed(99);
    sse.SetSeed(99);
    const f32 dt = 1.0f / 60.0f;
    for (i32 step = 0; step < 240; ++step) {
        if (step % 20 == 0) { // odd counts leave a 1-3 particle tail for the scalar loop
            scalar.Emit(kSparks, {0.0f, 0.0f}, 301);
            sse.Emit(kSparks, {0.0f, 0.0f}, 301);
        }
        scalar.UpdateScalar(dt);
        sse.UpdateSse(dt);
    }
    CHECK(scalar.GetCount() == sse.GetCount());
    CHECK(scalar.GetCount() > 0);
    f32 worst = 0.0f;
    for (u32 i = 0; i < scalar.GetCount() && i < sse.GetCount(); ++i) {
        worst = std::fmax(worst, Distance(scalar.GetPosition(i), sse.GetPosition(i)));
        worst = std::fmax(worst, Distance(scalar.GetVelocity(i), sse.GetVelocity(i)));
        worst = std::fmax(worst, std::fabs(scalar.GetLife(i) - sse.GetLife(i)));
    }
    CHECK_NEAR_EPS(worst, 0.0f, 1e-4f);
}

TEST(ParticlesDraw)
{
    // Color and size fade from start to end; Draw restores the batch's blend mode.
    ParticleSystem p(4);
    const ParticleEmitterConfig config{.StartColor = {1.0f, 0.0f, 0.0f, 1.0f},
                                       .EndColor = {0.0f, 0.0f, 1.0f, 1.0f},
                                       .Speed = {10.0f, 10.0f},
                                       .Angle = {0.0f, 0.0f},
                                       .Lifetime = {1.0f, 1.0f},
                                       .StartSize = 4.0f,
                                       .EndSize = 2.0f};
    p.Emit(config, {0.0f, 0.0f}, 1);
    p.Update(0.5f); // halfway: size 3, purple, at x = 5

    Renderer2D r;
    r.Begin(Mat4::Identity());
    r.DrawLine({0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f});
    p.Draw(r); // additive: its own command
    CHECK(r.GetBlendMode() == BlendMode::Alpha);
    const Texture white = Texture::CreateWithoutGpu(1, 1);
    const Sprite dot = Sprite::FromTexture(white);
    p.Draw(r, {.Sprite = &dot, .Blend = BlendMode::Alpha, .SizeScale = 2.0f});
    r.End();

    const auto commands = r.GetCommands();
    CHECK(commands.size() == 3);
    CHECK(commands[0].Blend == BlendMode::Alpha && commands[1].Blend == BlendMode::Additive);
    CHECK(commands[2].Type == Renderer2D::CommandType::Sprites);
    const auto lines = r.GetVertices();
    CHECK(lines.size() == 4);
    CHECK_NEAR(lines[2].Position, Vec2(2.0f, 0.0f)); // a 3-long streak behind x = 5
    CHECK_NEAR(lines[3].Position, Vec2(5.0f, 0.0f));
    CHECK(lines[3].Color == Renderer2D::PackColor({0.5f, 0.0f, 0.5f, 1.0f}));
    const auto quad = r.GetSpriteVertices(); // 6 vertices of a 6 x 6 square around (5, 0)
    CHECK(quad.size() == 6);
    CHECK_NEAR(quad[0].Position, Vec2(2.0f, -3.0f));
}
