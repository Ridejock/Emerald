# Animation and effects

Sprite animation, tweens and timers, and particles.

[Back to the README](../README.md)

- [Sprite animation (Animator)](#sprite-animation-animator)
- [Tweens, easing and timers (Tween/)](#tweens-easing-and-timers-tween)
- [Particles](#particles)

## Sprite animation (`Animator`)

Animations are named frame sequences in the atlas JSON, next to the sprites, under
`"animations"`: a list of sprite names, or a name pattern where `{}` counts 0, 1, 2 ... while those
sprites exist (or from `"from"` to `"to"`). `"duration"` (seconds, default 0.1) applies to every
frame unless `"durations"` gives one per frame; `"mode"` is `"loop"` (default), `"once"` or
`"pingpong"`. A plain array of names is a looping animation.

```json
{
  "hero_idle_0": { "x": 0,  "y": 0, "w": 16, "h": 16 },
  "hero_idle_1": { "x": 17, "y": 0, "w": 16, "h": 16 },
  "hero_walk_0": { "x": 34, "y": 0, "w": 16, "h": 16 },
  "...": "...",
  "animations": {
    "idle": { "frames": ["hero_idle_0", "hero_idle_1"], "durations": [1.6, 0.12] },
    "walk": { "pattern": "hero_walk_{}", "duration": 0.12 },
    "jump": { "pattern": "hero_jump_{}", "durations": [0.08, 0.3, 0.12], "mode": "once" },
    "coin": { "pattern": "coin_{}", "duration": 0.09, "mode": "pingpong" }
  }
}
```

Frames that name no sprite are logged, listed by `atlas.GetMissingFrames()` (`"walk: hero_walk_9"`)
and left out; an animation with no frames left is dropped. `atlas.GetAnimation("name")` returns an
empty animation (draws nothing) for unknown names, `FindAnimation` a pointer or null.

An `Emerald::Animator` plays one and gives the frame to draw:

```cpp
// OnStart: react to a "once" animation ending.
m_Animator.OnFinished = [this](const Emerald::Animation&) { m_Jumping = false; };
// OnFixedUpdate:
if (jumpPressed)
    m_Animator.Play(atlas.GetAnimation("jump"), true); // restart
else if (!m_Jumping)
    m_Animator.Play(atlas.GetAnimation(moving ? "walk" : "idle")); // no restart if already on it
m_Animator.SetSpeed(running ? 1.8f : 1.0f);
m_Animator.Update(dt);
// OnRender2D: flip and tint like any sprite.
r.DrawSprite(m_Animator, position, {.Scale = Vec2(4.0f), .Tint = flash, .FlipX = facingLeft});
```

`Stop` / `Resume` hold and continue, `IsFinished` is true once a `"once"` animation has shown its
last frame for its duration (it stays on that frame), `OnLoop` fires at the end of every loop /
ping-pong cycle, and `GetFrameIndex`, `GetFrameTime`, `GetLoopCount` are there for debugging. Time
carries over between frames, so the result depends only on the total time, not on the dt steps.

The sandbox's hero sheet is hand-made pixel art written as text in `tools/sprites/make_hero.py`
(standard-library Python, fixed 8-color palette, deterministic output); run
`python3 tools/sprites/make_hero.py` to regenerate `sandbox/assets/sprites/hero.png` and
`hero.json`.

## Tweens, easing and timers (`Tween/`)

`Emerald/Tween/Easing.h` has the usual easing curves, `Tween.h` animates values with them, and
`Timers.h` runs code later or repeatedly. The game owns a `Tweens` and a `Timers` and advances them
with its update dt, so they pause and slow down with the game:

```cpp
#include <Emerald/Tween/Timers.h>
#include <Emerald/Tween/Tween.h>

using Emerald::Easing;

// Drop the title in with a bounce, then fade the hint in after it.
m_TitleY = -80.0f;
const Emerald::TweenId drop = m_Tweens.To(&m_TitleY, 70.0f, 0.9f, {.Curve = Easing::BounceOut});
m_Tweens.FromTo(&m_HintAlpha, 0.0f, 1.0f, 0.3f, {.After = drop});
// Blink a color three times (there and back, three times), then say so.
m_Tweens.To(&m_Color, red, 0.15f,
            {.Repeat = 5, .Yoyo = true, .OnComplete = [this] { EM_INFO("done blinking"); }});
// Timers: once, and every half second until cancelled.
m_Timers.After(2.0f, [this] { ShowHint(); });
const Emerald::TimerId blink = m_Timers.Every(0.5f, [this] { m_On = !m_On; });

// Every frame:
m_Tweens.Update(dt);
m_Timers.Update(dt);
```

- **Easing:** `Ease(Easing::QuadOut, t)` maps t in [0, 1] to the eased amount (0 at 0, exactly 1
  at 1). Linear, Quad, Cubic, Back (overshoots a little), Elastic (wobbles) and Bounce, each as In
  (slow start), Out (slow end) and InOut. `kAllEasings` and `GetEasingName` are there for menus.
- **Tweens:** `To(&value, target, seconds, options)` for `f32`, `Vec2` and `Vec4` (colors) starts
  from wherever the value is when the tween starts (after its delay); `FromTo` jumps to a start
  value first. `Run(seconds, [](f32 eased) { ... })` has no target and just calls you back.
  `TweenOptions` is `{.Curve, .Delay, .Repeat, .Yoyo, .After, .OnComplete}`: `Repeat` is extra
  plays (`Tweens::kForever` never stops), `Yoyo` plays every other one backwards, `After` chains
  this tween to start when another completes, and `OnComplete` fires exactly once, when the last
  play ends (never for a cancelled or endless tween).
- **Cancelling:** `Cancel(id)` stops a tween where it is (no `OnComplete`) along with everything
  chained after it; `CancelTarget(&value)` cancels every tween writing to `value`; `Clear()`
  everything. Ids are never reused, so cancelling a finished tween is a harmless no-op that
  returns false.
- **Timers:** `After(seconds, fn)`, `Every(interval, fn)`, `Cancel(id)`, `GetTimeLeft(id)`.
- **Frame-rate independent:** time past the end of a play, a delay, a chained tween or a timer
  deadline carries over, so the result depends only on the total time: `Every(0.1f, ...)` fires
  ten times in a second however that second is split into frames (a long frame fires it several
  times). Easing at 0, 0.5 and 1 and these timing rules are unit-tested (`TweenTests`).
- **Callbacks** may start, cancel or chain tweens and timers, including their own; new ones begin
  on the next `Update`.

**Lifetime (read this one):** a tween keeps a pointer to its target and writes through it on every
`Update` until it completes or is cancelled. So the value must stay where it is until then: cancel
its tweens (`CancelTarget(&value)` or `Cancel(id)`) before the value is destroyed or moved, e.g. in
its owner's destructor. Pointers into a `std::vector` that grows are the classic trap. The easiest
way to be safe is to keep the `Tweens` in the same object as the values it animates (the sandbox's
`MenuDemo` does that), so they go away together. When the value lives somewhere that moves (an ECS
component, a vector element), use `Run` and look the value up in the callback. The same goes for
whatever a callback captures, `this` included.

The sandbox's title screen and pause menu use a tweened menu (`sandbox/src/MenuDemo.h`): the title drops in with
`BounceOut`, the panel slides in from the right with `BackOut`, the items fade in one after another
(delays), the selection highlight glides between items, and closing plays a chain (items fade out,
then the panel slides away, then the title leaves). M / Start pauses and resumes; Up / Down and
Enter / South pick an item. The ImGui panel shows the menu's tween and timer counts and an easing
visualizer: pick a curve and a dot runs along it.

## Particles

`Emerald::ParticleSystem` (`include/Emerald/Particles/ParticleSystem.h`) is a fixed-capacity pool of
simple particles: position, velocity, drag, gravity, and a color and size that fade from a start to
an end value over each particle's life. An effect is a `ParticleEmitterConfig`:

```cpp
const ParticleEmitterConfig kSparks{.StartColor = {1.0f, 0.8f, 0.4f, 1.0f},
                                    .EndColor = {1.0f, 0.3f, 0.1f, 0.0f},
                                    .Shape = EmitterShape::Circle, .Radius = 6.0f, // or Point/Line
                                    .Speed = {80.0f, 220.0f}, .Lifetime = {0.3f, 0.7f},
                                    .Drag = 2.0f, .StartSize = 6.0f, .EndSize = 1.0f};

ParticleSystem m_Particles{4096};   // allocates once, never again
ContinuousEmitter m_Engine;         // remembers fractions of a particle between frames

m_Particles.Emit(kSparks, position, 30);                          // a burst
m_Particles.EmitContinuous(kExhaust, m_Engine, nozzle, dt,        // kExhaust.Rate per second
                           backwardsAngle, shipVelocity);         // direction, base velocity
m_Particles.Update(dt);                                           // move, age, remove dead
m_Particles.Draw(r);   // in OnRender2D: streaks along the velocity, additive by default
m_Particles.Draw(r, {.Sprite = &dot, .Blend = BlendMode::Alpha}); // or textured quads
```

The data is a **structure of arrays**: one array per field (all X positions, all Y positions, ...).
The update applies the same few operations to every element of a handful of arrays, which is the
shape SIMD wants: `UpdateSse` loads four particles' X velocities with one `_mm_loadu_ps`, and so on.
A dead particle is overwritten by the last one (**swap-remove**), so live particles always fill
`0..count-1` and removal is O(1) (the order changes, which does not matter for particles). A full pool
drops new particles and counts them (`GetDroppedCount`). `Update` uses the SSE path when the math
library does (`EMERALD_MATH_SIMD` on x86); `UpdateScalar` and `UpdateSse` are public, and
`ParticleTests` checks that they agree.

### Particle effect files (`ParticleEffect`)

An effect can also be a JSON file loaded through the asset manager (hot reloaded in debug builds,
and edited live with the particle editor, see Editor tools). A `ParticleEffect` is a config plus
how it is usually played, a burst size and a blend mode:

```cpp
Emerald::AssetHandle<Emerald::ParticleEffect> m_Sparks =
    GetAssets().Load<Emerald::ParticleEffect>("assets/particles/sparks.json");

m_Particles.Emit(m_Sparks->GetConfig(), position, m_Sparks->Burst);
m_Particles.EmitContinuous(m_Smoke->GetConfig(), m_Chimney, top, dt); // the file's "rate"
m_Particles.Draw(r, {.Blend = m_Sparks->Blend});
```

```json
{
  "startColor": [1, 0.85, 0.4, 1],
  "endColor": [1, 0.25, 0.05, 0],
  "shape": "circle",
  "radius": 3,
  "lineHalfExtent": [0, 0],
  "speed": [80, 260],
  "angle": [0, 360],
  "lifetime": [0.25, 0.7],
  "drag": 2.5,
  "gravity": [0, 220],
  "startSize": 6,
  "endSize": 0,
  "rate": 0,
  "burst": 60,
  "blend": "additive"
}
```

Every key is optional (missing ones keep `ParticleEmitterConfig`'s defaults); angles are in
degrees (the config's are radians), colors are `[r, g, b, a]` or `[r, g, b]`, `shape` is `point`,
`circle` or `line`, `blend` is `additive` or `alpha`. A wrong value (a string for a number, an
unknown shape) is logged and the file doesn't load (the last good version stays on hot reload).
`ParseParticleEffect` / `LoadParticleEffect` / `ParticleEffectToJson` / `SaveParticleEffect` work
without the asset manager; saving writes every field, one per line. Hot reload assigns the new
values in place, so `GetConfig()`'s address stays the same.

### When is SIMD worth it?

`EmeraldParticleBench` (Release, `-DEMERALD_BUILD_BENCH=ON`) times both paths. On an 8-core Intel Xeon
VM (GCC 14, `-O3`), best of many runs, including the dead-particle pass:

| Particles | Scalar | SSE | Speedup |
|---:|---:|---:|---:|
| 1,000 | 0.0028 ms (2.8 ns each) | 0.0012 ms (1.2 ns) | ~2.3x |
| 10,000 | 0.027 ms (2.7 ns) | 0.012 ms (1.2 ns) | ~2.3x |
| 100,000 | 0.33-0.36 ms (3.4 ns) | 0.15 ms (1.5 ns) | ~2.3x |
| 1,000,000 | 3.5 ms (3.5 ns) | 1.64 ms (1.6 ns) | ~2.2x |

What to take from it:

- **About 2x, not 4x.** Four lanes only speed up the arithmetic; the loads and stores, the scalar
  tail and the removal pass stay. At 1M particles the SSE loop streams about 48 bytes per particle,
  roughly 30 GB/s, so memory bandwidth starts to cap it.
- **It only matters when the work is big.** A game with a few thousand particles spends
  microseconds either way (10k scalar = 0.03 ms of a 16.7 ms frame). SIMD starts to pay off when a
  loop like this costs a noticeable part of the frame: hundreds of thousands of elements, or many
  such loops.
- **Layout first.** The structure-of-arrays layout is what makes the SSE version short; with an
  array of `Particle` structs it would need shuffles, and it would load fields it does not use.
- **Measure.** The compiler did not auto-vectorize the scalar loop here (seven separate arrays that
  might alias), not even with `__restrict` pointers; other compilers or flags may, which would
  close the gap. Your numbers will differ from these.
