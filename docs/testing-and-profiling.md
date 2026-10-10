# Testing and profiling

Unit tests, ThreadSanitizer and benchmarks, Tracy and AddressSanitizer.

[Back to the README](../README.md)

- [Tests](#tests)
- [Profiling (Tracy)](#profiling-tracy)
- [AddressSanitizer](#addresssanitizer)

## Tests

`EMERALD_BUILD_TESTS=ON` (default) builds small test executables using a minimal harness
(`tests/Test.h`) and registers them with CTest:

| Test | Covers |
|---|---|
| `MathTests` | vectors, scalar helpers, `Mat4` multiply/transforms/inverse/ortho/perspective/look-at, HLSL layout, scalar-vs-SSE agreement |
| `MathTestsScalar` | the same tests compiled with `EMERALD_MATH_SIMD=0` (only added when the option is on) |
| `LogTests` | log file creation, truncation, relative paths, empty path, failure fallback |
| `ThreadPoolTests` | futures return values, exceptions through futures, `WaitIdle`, shutdown with pending tasks, `ParallelFor` covers every index once |
| `MemoryTests` | frame arena reset/alignment/overflow, many resets stay in the buffer, pmr containers use their resource, tracking counts, pools (incl. the synchronized pool from many threads) |
| `InputTests` | key down/pressed/released edges, taps within one frame, fixed-step edges, `ReleaseAll`; actions with several keys, action taps across fixed steps, axes, rebinding; saving bindings (key / button names round trip, save + load through a save file, missing values keep defaults, unknown names skipped), the key / button pressed this frame; gamepads (synthetic pads, no hardware): deadzone math (per-axis, radial), trigger/stick virtual buttons with hysteresis, button edges across fixed steps, several pads, labels, gamepad bindings and largest-magnitude axes; `FixedTimestep` accumulation, average rate at 144 fps / 120 Hz, slow-frame clamp |
| `SaveTests` | save values (types, fallbacks, escaping, key checks, rename); the text format round trip and hand-written files (no checksum, CRLF); save/load on disk, replacing a save and the `.bak`; a v1 -> v3 migration chain (order, partial chains, a missing step); newer data versions refused; every truncated prefix and a flipped byte rejected; falling back to the backup; a failed write (temp path blocked) and a leftover partial temp file keeping the old save; independent slots with listing, metadata, delete and bad names; a high-score table |
| `DialogueTests` | condition/effect terms (D3's `has:` / `not:` / `hp-1` spellings too); ports of D3's samples as behaviour (the simple walk, the flag sample with visited-card flags and guarded text, the tag sample: an automatic start card, a once-only answer, a tag set by another deck, a menu card); numbers gating answers; missing cards, loops of automatic cards and broken files logged, never a crash; flags and the current card through `SaveSystem` (and refused restores: other deck, removed card); a hot-reloaded deck (changed text, removed current card); `WrapText`; `DialogueBox` typewriter (finish at once, then advance), keyboard and mouse answer picking |
| `EditorTests` | particle effect files: a JSON round trip with every field changed (and the defaults), saving again gives the same text, missing keys keep defaults, 3-number colors, broken values rejected, revisions, files, deep copies that keep the config's address; with ImGui (`debug-full`): `Tweak` registration, the tweaks file round trip, wrong kinds skipped, values remembered across unregister / register and loaded before registration, unknown entries kept, `Save` / `Load` / `SetFile`, `ResetAll`; the entity inspector's registration (engine, custom and tag components), `Inspect`, `SelectAt` and a destroyed selection; without ImGui: tweaks are plain values and the stand-ins do nothing |
| `AudioExtrasTests` | spatial attenuation/pan math; mixer groups mute and volume ramps; spatial voices following the listener; MP3/OGG `MusicStream` constant ring memory; crossfade between two tracks; file open for sandbox OGG loops |
| `AudioTests` | MP3 decoding from an embedded 809-byte file (length, level, channels, pitch after resampling), garbage rejected, WAV loading, `LoadSound` by extension incl. unknown/missing files, `MakeSound` conversion; mixer handles (stale handles, reuse, releasing samples), fade-in/out and volume ramps without clicks, looping, pitch, pan, master volume/mute, voice stealing, soft limiter; synth waveforms (length, no NaN, peak), envelopes, lowpass |
| `Renderer2DTests` | `Renderer2D` batching and shape generation on the CPU (no GPU), `Transform2D`, color packing; sprite quads (UVs, rotation, origin, flips, pixel snap), draw order across lines/sprites/texture switches and blend modes, lighting captured per draw (lit world and unlit HUD in one frame), atlas JSON parsing; `Camera2D` (pixel-space default, letterboxing, zoom/rotation, `ScreenToWorld` round trips against the GPU matrix, bounds clamp, follow dead zone and step-size independent damping, shake decay); atlas `"animations"` parsing (patterns, lists, durations, modes, missing frames reported), `Animator` loop / once / ping-pong timing at several dt, speed, stop/resume, finish and loop events, drawing a frame with flip and tint; `CrtEffect` afterglow decay, uniforms and bloom spread; `--gpu` parsing and driver names |
| `FontTests` | stb_truetype fonts with the sandbox's demo font: metrics, measuring, layout and `DrawString` |
| `ParticleTests` | particle spawning (shapes, ranges, base velocity), capacity limit, drag/gravity step, swap-remove, continuous rate, color/size fade when drawing, scalar and SSE updates agreeing over 240 steps |
| `CollisionTests` | circle/circle, circle/AABB, AABB/AABB and SAT polygon contacts (normals, depths, touching = none, concentric circles, center inside a box, containment, winding, degenerate input); raycasts against circles, boxes and polygons (hits, misses, parallel, max distance, starting inside); `SpatialHash` insert/update/remove/query/pairs, wrap-around, brute-force equivalence on random data |
| `AssetTests` | asset manager bookkeeping with a GPU-less loader: dedupe (same path, `..` paths, absolute paths; other options or types are other assets), handle copy/move/reset reference counts, unloading on `Update` and reviving before it, placeholders for missing and broken files (texture, atlas, font, sound), hot reload in place (textures, atlas image + JSON with sprites and animators keeping their pointers, fonts, real WAVs, dialogue decks with a conversation in progress, particle effects keeping their config's address), broken reloads keeping the old version, placeholders replaced when the file appears, reloads through `Update` on the thread pool within a second |
| `TilemapTests` | the sample room from Tiled JSON: layer order and kinds, external `.tsj` and embedded tilesets (GID lookup, sprite regions), objects (shapes, class, position, size, properties), the collision grid from tile properties and classes (non-colliding layer), tile ranges with touching edges, `OverlapsSolid`, `MoveAndCollide` (flush stops, no tunneling, sliding along walls, one-way from above / below / inside); all 8 flip-bit combinations against Tiled's transform; missing and broken files (JSON, sizes, infinite, isometric, missing/XML tileset, missing image, base64, bad cells) logged and failing cleanly, unknown tile ids left empty; a tileset's `normalMap` (missing or the wrong size: ignored; watched; drawn with the tiles only while lighting is on); through the asset manager: placeholder for a missing map, hot reload of the map and of its external tileset, broken edits keeping the last version |
| `SceneTests` | the scene stack without a GPU: requests applied only at the end of `Update`, hook order for push / pop / replace / clear / `ReplaceAll` (pause, resume, exit, destruction), requests from inside a scene's own hooks, `DrawBelow` / `UpdateBelow` chains (which scenes draw and update, in which order), fade timing (change at full cover, then uncover, requests queued meanwhile), fading into an empty stack, custom transition `Draw`, input blocked below the top and during transitions (and the app's own block kept), empty-stack pops, exiting every scene on destruction |
| `EntityTests` | add / get / has / remove and replacing components, deferred destroy (skipped by `Each`, invalid at once, destructors at `Flush`, stale handles after slot reuse), `Clear`; spawning, adding and destroying inside `Each`; churn of 400,000 spawns without leaks (destructor counts, bounded storage); movement, animation (an `OnFinished` that destroys); collisions (circle / circle normal and depth, circle / box, layer masks, destroyed and collider-less entities leaving the broadphase); `DrawSprites` order by layer and y, culling |
| `PlatformerTests` | slope tiles from the tileset (heights, flipped cells mirrored, floor heights, never blocking sideways); walking right and left over 45 and 22.5 degree hills grounded every step with the feet on the floor and always moving (no bounce, no sticking); landing on a slope and jumping along it; jumping up through a one-way platform and landing on it, down + jump dropping through to the ground, down + jump on solid ground being a jump; coyote time (in time, too late, off, longer) and the jump buffer (pressed early enough, too early, off, longer); variable jump height; walls and ceilings; a scripted 50 s run on the sandbox level replayed twice with the same positions bit for bit |
| `ReplayTests` | `--frames` / `--screenshot` / `--capture` / `--record` / `--replay` parsing (both spellings, bad numbers); recording text round trip (floats exact) and broken files; `Input` answering from a replayed sample (other bindings, blocking still wins); a scripted platformer run on the sandbox level with uneven frame times, recorded and replayed twice with bit-identical positions, no divergence; a replay at another fixed rate refused |
| `ToolsTests` | the Python tools: `check_assets.py` (good assets pass; palette, atlas and map problems found; PNG filters) and `textmap.py` (neighbours, variants, legends, the written JSON); needs Python 3 |
| `SandboxAssets` | `check_assets.py` on `sandbox/assets` |
| `LightingTests` | light attenuation and spot-cone factor, point/spot shade with ambient, `LightingUniforms` packing (extras dropped), post-effect chain Add/Move/Remove/Find order (fake effects, no GPU) |
| `UiTests` | UI widgets without a GPU: ids (`##` suffixes, `PushId` scopes), panel layout in whole units (rows, columns tiling exactly, title row, pivot with last frame's height), focus starting on the first widget, Up / Down wrapping, Left / Right between columns and wrapping in a row, Down from a wide button into columns and back, buttons activating once per press, toggle / slider / choice values from Left / Right / Accept (steps snapped, clamped, choices wrapping), held-direction repeat and stick holds, mouse hover focus, click = press + release over the widget, slider dragging, focus moving off a removed widget, the focus highlight easing, `ReadUiInput` from actions (rebinding, blocked input), drawing and 9-slice sprite counts |
| `TweenTests` | every easing curve at 0, 0.5 and 1, Out mirroring In and InOut symmetry, clamping; tweens of f32 / Vec2 / Vec4 with delay, start values, repeat and yoyo, endless tweens, cancelling (chains included, no callbacks), chaining with leftover time, completion firing exactly once, callbacks starting and clearing tweens, `CancelTarget` from a destructor, `Run`, stale ids; one big step vs many uneven ones giving the same result; timers `After` / `Every` / cancel (also from their own callback), callbacks adding timers, frame-rate independence with uneven dt |

```sh
cmake --build --preset debug
ctest --test-dir build/debug --output-on-failure
```

ThreadSanitizer run (GCC/Clang; a separate build dir without the sandbox, so no shaders and no
shadercross are built):

```sh
cmake -S . -B build/tsan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DEMERALD_BUILD_SANDBOX=OFF \
      -DCMAKE_C_FLAGS=-fsanitize=thread -DCMAKE_CXX_FLAGS=-fsanitize=thread \
      -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=thread
cmake --build build/tsan --target EmeraldThreadPoolTests EmeraldMemoryTests
ctest --test-dir build/tsan -R "ThreadPool|Memory" --output-on-failure
```

Benchmark (release build recommended; numbers vary a lot between machines):

```sh
cmake --preset release -DEMERALD_BUILD_BENCH=ON
cmake --build --preset release
./build/release/bin/EmeraldMathBench
./build/release/bin/EmeraldParticleBench
./build/release/bin/EmeraldCollisionBench
./build/release/bin/EmeraldEntityBench
```

## Profiling (Tracy)

The `profile` preset builds with the [Tracy](https://github.com/wolfpld/tracy) profiler client
(v0.14.1, fetched by CMake like the other dependencies; nothing to install):

```sh
cmake --preset profile
cmake --build --preset profile
./build/profile/bin/Sandbox --swarm          # then connect the Tracy viewer
```

Get the **viewer** prebuilt from the same release,
[Tracy v0.14.1](https://github.com/wolfpld/tracy/releases/tag/v0.14.1) (`windows-0.14.1.zip`:
`tracy-profiler.exe`; there are Linux and macOS zips too). The viewer and the client must be the
same version, so when the pin in `cmake/Dependencies.cmake` moves, download the matching viewer.
Start the game, open the viewer and connect to `127.0.0.1` (it lists running games on the local
network). The client is built on demand (`TRACY_ON_DEMAND`): nothing is collected until a viewer
connects, so a profile build can run without one.

Every frame is marked, and the main loop shows as zones: `Events`, `Assets`, `FixedUpdate` (each
step), `Update` and `Render` (which includes waiting for the swapchain), plus the entity systems
(`UpdateMovement`, `UpdateAnimation`, `CollisionSystem::Update`, `DrawSprites`), `StepPlatformer`
and the sandbox swarm's `Swarm::Simulate`. Thread pool workers carry their names. Add zones in a
game with `Emerald/Core/Profile.h`:

```cpp
#include <Emerald/Core/Profile.h>

void UpdateEnemies(World& world)
{
    EM_PROFILE_FUNCTION();              // a zone named UpdateEnemies, or
    EM_PROFILE_SCOPE("Enemies: think"); // a named zone until the end of the block
}
```

Without `EMERALD_PROFILE` the macros compile to nothing and Tracy is not even downloaded.
Tracy's headers are SYSTEM includes and its one source file is compiled with its own flags, so
its warnings never count against ours.

## AddressSanitizer

The `debug-asan` preset builds the engine, its dependencies, the tests and the sandbox with
AddressSanitizer, which stops a program at the first out-of-bounds access, use after free or
double free with both call stacks (and on Linux reports leaks at exit). It runs about 2x slower.
`EMERALD_ASAN` also puts the flags on the `Emerald` target's interface, so a game that links
Emerald and turns the option on is instrumented too.

```sh
cmake --preset debug-asan
cmake --build --preset debug-asan
ctest --test-dir build/debug-asan --output-on-failure
```

- **GCC / Clang:** `-fsanitize=address -fno-omit-frame-pointer`. On Linux with Mesa's drivers the
  sandbox reports about 50 KB of leaks from inside the unloaded GPU driver ("unknown module") at
  exit; they are not ours, so run it with `ASAN_OPTIONS=detect_leaks=0` (the tests don't need it).
- **MSVC:** `/fsanitize=address`, with incremental linking (`/INCREMENTAL:NO`) and the `/RTC`
  run-time checks taken out of the Debug flags (ASan does not work with either). Needs the
  **C++ AddressSanitizer** component: Visual Studio Installer > Modify > Individual components.
  The programs need its runtime DLL (`clang_rt.asan_dynamic-x86_64.dll`, or the `_dbg_` one for
  the debug runtime); configuring copies those from next to `cl.exe` into `build/debug-asan/bin`,
  so the sandbox and `ctest` run without changing `PATH`. If configure warns that it found no DLL,
  the component is missing. When debugging in Visual Studio, an ASan report breaks into the
  debugger at the faulty line.
