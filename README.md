# Emerald

Emerald is a small, modern C++20 game engine built on [SDL3](https://github.com/libsdl-org/SDL).
It is split into:

- **`Emerald::Emerald`** – the engine library (logging, window, SDL GPU renderer, batched 2D renderer for lines and
  textured sprites, sprite animation, a 2D camera, textures and texture atlases, keyboard input, application loop with a fixed-timestep update, a scene stack with transitions, entities and components (EnTT), platformer character physics, image loading, thread pool, `std::pmr`
  memory helpers).
- **`Emerald::Math`** – a header-only math library (vectors, `Mat4`, optional SSE), included by the engine.
- **`sandbox/`** – a minimal example app that links the engine and draws a rotating vertex-colored triangle
  in pixel space with its own HLSL shaders through SDL GPU, plus 2D line shapes, sprites from a
  procedurally generated atlas (scaled, rotated, flipped, tinted, and layered with lines), and an arrow driven by
  an animated pixel-art hero (walk / idle / jump, from `tools/sprites/make_hero.py`) driven by
  keyboard or gamepad that a `Camera2D` follows around a larger world (pan, zoom, rotate, shake;
  with ImGui on, the panel shows the camera and lists connected pads and their live stick values).
  It is organised as scenes (title, camera demo, tilemap room, entity swarm, platformer, pause) on the scene stack.
- **`tests/`** – small unit-test executables run with `ctest`.

All dependencies are fetched automatically with CMake `FetchContent` and pinned to specific versions.

What is planned next (camera, animation, collision, tilemaps, scenes, UI, shipping...) is in
[ROADMAP.md](ROADMAP.md), with one GitHub issue per feature.

## Dependencies

| Library | Version | Notes |
|---|---|---|
| [SDL3](https://github.com/libsdl-org/SDL) | `release-3.4.16` | Built statically |
| [spdlog](https://github.com/gabime/spdlog) | `v1.17.0` | Uses bundled fmt |
| [stb](https://github.com/nothings/stb) | commit `2c980bb` | Header-only (stb_image, stb_image_write, stb_truetype + stb_rect_pack for fonts), exposed as `Emerald::stb` (INTERFACE) |
| [nlohmann/json](https://github.com/nlohmann/json) | `v3.12.0` | Texture atlas JSON; release tarball (SHA-256 pinned), linked PRIVATE |
| [dr_libs](https://github.com/mackron/dr_libs) | commit `dfe8377` | Only `dr_mp3.h` (MP3 decoding); header-only, public domain / MIT-0 |
| [Dear ImGui](https://github.com/ocornut/imgui) | `v1.92.9b-docking` | Optional, SDL3 + SDLGPU3 backends |
| [EnTT](https://github.com/skypjack/entt) | `v3.16.0` | Entity storage behind `World` / `Entity` (see [Entities](#entities-entity)); headers included as SYSTEM |
| [SDL_shadercross](https://github.com/libsdl-org/SDL_shadercross) | commit `1ff05be` | Build-time host tool (HLSL → SPIR-V/DXIL/MSL), built from source with vendored DXC + SPIRV-Cross |

## Options

| Option | Default | Description |
|---|---|---|
| `EMERALD_BUILD_SANDBOX` | `ON` | Build the sandbox example executable |
| `EMERALD_USE_IMGUI` | `OFF` | Fetch Dear ImGui (docking) and integrate it into the app loop |
| `EMERALD_BUILD_SHADERCROSS` | `ON` | Build the `shadercross` tool from source if `EMERALD_SHADERCROSS_EXECUTABLE` is empty |
| `EMERALD_SHADERCROSS_EXECUTABLE` | *(empty)* | Use this prebuilt `shadercross` instead of building it |
| `EMERALD_SHADERCROSS_BUILD_DIR` | `build/_shadercross` | Where the tool is built; shared by all presets |
| `EMERALD_SHADER_FORMATS` | `SPIRV;DXIL;MSL` | Shader formats generated at build time |
| `EMERALD_MATH_SIMD` | `ON` | Use the SSE code paths of the math library on x86/x64 (see [Math library](#math-library)) |
| `EMERALD_BUILD_TESTS` | `ON` | Build the unit tests and register them with CTest |
| `EMERALD_BUILD_BENCH` | `OFF` | Build the micro-benchmarks (math, particles, collision, entities) |

The engine exports `EMERALD_WITH_IMGUI` / `EMERALD_MATH_SIMD` (0 or 1) as public compile
definitions. `EMERALD_WITH_ENTT` is still exported and is always 1: EnTT is now a regular dependency.

## Requirements

- CMake **3.24+**
- A C++20 compiler (GCC 11+, Clang 14+, MSVC 2022, Apple Clang 15+)
- [Ninja](https://ninja-build.org/) (used by the presets; any generator works without presets)
- Git (for FetchContent)

## Building

### Linux

Install a toolchain and SDL3's system development headers (Debian/Ubuntu shown):

```sh
sudo apt install build-essential cmake ninja-build git pkg-config \
    libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev \
    libxss-dev libxtst-dev libxkbcommon-dev libwayland-dev wayland-protocols \
    libegl1-mesa-dev libgl1-mesa-dev libdrm-dev libgbm-dev \
    libasound2-dev libpulse-dev libudev-dev libdbus-1-dev

cmake --preset debug
cmake --build --preset debug
./build/debug/bin/Sandbox
```

### Windows

From a *Developer PowerShell / x64 Native Tools prompt for VS 2022* (so `cl` and `ninja` are on `PATH`):

```powershell
cmake --preset debug
cmake --build --preset debug
.\build\debug\bin\Sandbox.exe
```

Or open the folder directly in Visual Studio / CLion / VS Code, which pick up `CMakePresets.json`.
Without Ninja: `cmake -S . -B build -G "Visual Studio 17 2022"` then `cmake --build build --config Debug`.

### macOS

```sh
xcode-select --install
brew install cmake ninja
cmake --preset debug
cmake --build --preset debug
./build/debug/bin/Sandbox
```

### Presets

| Preset | Description |
|---|---|
| `debug` | Debug build, default options |
| `release` | Release build, default options |
| `debug-full` | Debug build with `EMERALD_USE_IMGUI=ON` |

Options can also be passed directly, e.g. `cmake --preset release -DEMERALD_USE_IMGUI=ON`.

## Running

```sh
./build/debug/bin/Sandbox                                  # run until the window is closed or Esc is pressed
./build/debug/bin/Sandbox --frames 120                     # quit automatically after 120 frames
./build/debug/bin/Sandbox --frames 60 --screenshot out.png # save the last frame as a PNG (GPU readback)
./build/debug/bin/Sandbox --gpu vulkan                     # pick the GPU backend (see below)
./build/debug/bin/Sandbox --demo                           # skip the title: start in the camera demo
./build/debug/bin/Sandbox --tilemap                        # start in the tilemap room (see Tilemaps)
./build/debug/bin/Sandbox --swarm                          # start in the entity swarm (see Entities)
./build/debug/bin/Sandbox --platformer                     # start in the platformer level (see Platformer physics)
SDL_GPU_DRIVER=vulkan ./build/debug/bin/Sandbox            # the same through SDL's environment variable
```

**Choosing the GPU backend** without recompiling: `--gpu vulkan|d3d12|direct3d12|metal|auto`
(also `--gpu=vulkan`; `d3d12` means `direct3d12`, case doesn't matter). Apps pass their command line
with `spec.Args = {argv, static_cast<usize>(argc)};` (`ApplicationSpec::Args`). Without the flag,
SDL's `SDL_GPU_DRIVER` environment variable still works; the flag wins over it (`--gpu auto` ignores
it too). An unknown value logs a warning and uses auto; if the requested backend cannot create a
device (e.g. `--gpu metal` on Windows), the error is logged and the engine retries with auto, so
the app still starts. The backend in use is logged at startup and available as
`GetRenderer().GetDriverName()` (`"vulkan"`, `"direct3d12"` or `"metal"`); the sandbox's ImGui
panel shows it as `GPU: vulkan`.

The renderer needs a real GPU backend, so a display is required (the `dummy` video driver has no
GPU swapchain). On a machine/VM/CI runner without a GPU, install Mesa's software Vulkan driver
(lavapipe) – e.g. `sudo apt install mesa-vulkan-drivers` – and run under X11/Xvfb.

## Logging

`Emerald::Log` wraps [spdlog](https://github.com/gabime/spdlog) with two loggers: **Core** (engine,
`EM_CORE_TRACE/INFO/WARN/ERROR`) and **Client** (your app, `EM_TRACE/INFO/WARN/ERROR`). Both write to
the console (colored) **and to a log file**:

- Default: `logs/Emerald.log` **next to the executable** (e.g. `build/debug/bin/logs/Emerald.log`),
  found with `Paths::GetBasePath()` so it does not depend on the working directory. The `logs/` folder is
  created if needed.
- `Emerald/Core/Paths.h` has the two folders a game needs: `Paths::GetBasePath()` (the
  executable's folder, where assets live) and `Paths::GetPrefPath(org, app)` (a per-user writable
  folder for saves and settings, e.g. `%APPDATA%\<org>\<app>\` on Windows,
  `~/.local/share/<org>/<app>/` on Linux; created if missing, empty path on failure). Both return
  UTF-8-correct `std::filesystem::path`s.
- The file is **truncated on every run** and uses the console's pattern without color codes:
  `[12:16:06.185] [EMERALD] [info] Emerald starting (SDL 3.4.16)`.
- Warnings and errors are flushed immediately; everything else is flushed on shutdown (or when
  spdlog's buffer fills), so after a crash the last info/trace lines may be missing.
- Configure it with `ApplicationSpec::LogFile`: a relative path is resolved against the executable's
  directory, an absolute path is used as is, and an **empty path disables** the file.
- If the file cannot be opened (read-only folder, invalid path, …) Emerald prints a warning and keeps
  logging to the console only.

```cpp
Emerald::ApplicationSpec spec;
spec.LogFile = "logs/MyGame.log"; // or "" for console only
```

## Threads

`Emerald::ThreadPool` (`include/Emerald/Core/ThreadPool.h`) is a small worker pool built on
`std::jthread`. `Application` owns one: `GetThreadPool()`, sized by `ApplicationSpec::WorkerThreads`
(0 = hardware threads − 1, at least 1).

```cpp
Emerald::ThreadPool& pool = GetThreadPool();
std::future<i32> answer = pool.Submit([] { return 6 * 7; }); // any callable, returns a future
pool.ParallelFor(items.size(), [&](usize i) { Update(items[i]); }); // chunks on workers + caller
pool.WaitIdle();                                                    // until the queue is empty
i32 value = answer.get(); // 42, or rethrows the exception the task threw
```

- Tasks wait in a mutex-protected queue; idle workers sleep on a `std::condition_variable_any` whose
  `wait` takes the worker's `std::stop_token`. The destructor calls `request_stop()` and the jthreads
  join automatically – no hand-written shutdown flag or join loop.
- **Shutdown policy**: running tasks finish, tasks still queued are dropped and their futures throw
  `std::future_error` (`broken_promise`). Call `WaitIdle()` first if everything must run.
  `Application` calls `WaitIdle()` before `OnShutdown()`.
- `ParallelFor(count, fn)` splits `[0, count)` into ~4 chunks per thread, runs the last chunk on the
  calling thread, waits for the rest and rethrows the first exception.
- Don't call `WaitIdle()`/`ParallelFor()` from inside a task (a worker waiting on other workers can
  deadlock).
- Workers are named `Emerald-W0`, `Emerald-W1`, … (`SetThreadDescription` on Windows,
  `pthread_setname_np` on Linux/macOS) so they are recognizable in debuggers and profilers.

## Memory (`std::pmr`)

`include/Emerald/Memory/` (umbrella `Memory.h`, included by `Emerald.h`) builds on the standard
polymorphic memory resources: a `std::pmr` container takes a `std::pmr::memory_resource*` and
allocates all its memory from it.

| What | Where | Use it for | Threads |
|---|---|---|---|
| Frame arena | `GetFrameAllocator()` (`FrameArena`) | temporary data that lives for one frame | main thread only |
| Pool | `GetPoolAllocator()` (`std::pmr::unsynchronized_pool_resource`) | long-lived objects created/destroyed often | main thread only |
| Shared pool | `GetSharedPoolAllocator()` (`std::pmr::synchronized_pool_resource`) | the same, across threads | any thread |

```cpp
Emerald::PmrVector<DrawItem> draws(GetFrameAllocator()); // freed automatically next frame
Emerald::PmrUnorderedMap<u32, Emerald::PmrString> names(GetPoolAllocator());
```

- **Aliases** (`PmrTypes.h`): `PmrVector`, `PmrDeque`, `PmrString`, `PmrUnorderedMap`,
  `PmrUnorderedSet`, `PmrMap`.
- **`FrameArena`** wraps `std::pmr::monotonic_buffer_resource` over a preallocated buffer
  (`ApplicationSpec::FrameArenaSize`, default 1 MiB). Allocating is a pointer bump, freeing does
  nothing, and `Application` calls `Reset()` at the start of every frame, so never keep frame memory
  across frames. `GetFrameArena().GetStats()` reports bytes used, the peak and overflows.
- **Overflow policy**: when a frame needs more than the buffer, the rest comes from the heap and a
  warning is logged once ("increase `ApplicationSpec::FrameArenaSize`"); the heap chunks are released
  at the next `Reset()`. This was chosen over `std::pmr::null_memory_resource()`, which would throw
  `std::bad_alloc` mid-frame and turn a one-off spike into a crash.
- **MSVC note**: `Reset()` rebuilds the monotonic resource instead of calling its `release()`. MSVC's
  `release()` does not go back to the initial buffer ([LWG 3120](https://cplusplus.github.io/LWG/issue3120),
  [microsoft/STL#1468](https://github.com/microsoft/STL/issues/1468)), so with MSVC every frame
  after the first went to the heap and counted as an overflow. `MemoryTests` checks this.
- **Thread-safety rule**: the frame arena and the unsynchronized pool belong to the main thread
  (debug builds assert this for the arena). Worker tasks should use the shared pool, their own
  `FrameArena`, or normal allocations.
- **`TrackingResource`** forwards to another resource and counts bytes/allocations in use, the peak
  and the total (atomics, so it is thread-safe). Both application pools sit on one
  (`GetPoolStats()`); with ImGui on, the sandbox panel shows the arena and pool numbers.

## Math library

Header-only, in `include/Emerald/Math/` (`#include <Emerald/Math/Math.h>`, also included by
`Emerald.h`), namespace `Emerald`:

| Header | Contents |
|---|---|
| `Common.h` | `Pi`, `TwoPi`, `HalfPi`, `ToRadians`/`ToDegrees`, `Clamp`, `Min`/`Max`, `Lerp`, `NearlyEqual`/`NearlyEqualRelative`/`NearlyZero`, SIMD configuration |
| `Vec2.h` | `Vec2` (float) and `Vec2i` (int): arithmetic, `Dot`, 2D `Cross`, `Length`, `Normalize`, `Lerp`, `Min`/`Max` |
| `Vec3.h` | `Vec3`: the same plus `Cross` |
| `Vec4.h` | `Vec4` (`alignas(16)`): the same operations, SSE-accelerated |
| `Mat4.h` | `Mat4`: `*` with `Mat4`/`Vec4`, `Translate`, `Scale`, `RotateZ`, `Rotate(angle, axis)`, `Ortho`, `OrthoPixelSpace`, `Perspective`, `LookAt`, `Transpose`, `Determinant`, `Inverse`, `TransformPoint`/`TransformDirection`, `TransformBatch` |

Conventions (documented at the top of `Mat4.h`):

- **Column vectors, column-major storage** – `p' = M * p`, transforms compose right to left
  (`Projection * Translate * RotateZ * Scale`), and the translation is in the last column. This is
  HLSL's default `float4x4` cbuffer layout, so a `Mat4` is pushed as-is and the shader does
  `mul(Transform, float4(position, 1))` (see `sandbox/shaders/Triangle.vert.hlsl`).
- **Left-handed, depth 0..1** like SDL GPU / D3D12 / Metal: +X right, +Y up, +Z into the screen;
  `Perspective`/`Ortho` map depth to [0, 1].
- **2D pixel space**: `Mat4::OrthoPixelSpace(width, height)` puts (0, 0) at the top-left with +Y down
  (like window and mouse coordinates). The sandbox draws everything with it.
- Angles are in radians; vector components are lowercase `x y z w` like HLSL.

### SIMD (`EMERALD_MATH_SIMD`)

With the option on (default) and an x86/x64 target, `Vec4` operations, `Mat4 * Vec4`, `Mat4 * Mat4`
and `TransformBatch` use SSE2 intrinsics (`<emmintrin.h>`; `Dot` uses SSE4.1's `_mm_dp_ps` only if the
compiler targets it, e.g. `-msse4.1` or MSVC `/arch:AVX`). The public API and memory layout are the same
in both modes; only the implementation behind the operators changes. Both implementations are always
compiled on x86 (`Emerald::Math::Scalar` and `Emerald::Math::Sse`), so tests and the benchmark can compare
them. On other CPUs (e.g. ARM) the scalar code is used; a NEON path could be added the same way.

Why only `Vec4`/`Mat4`: four floats fill one 128-bit SSE register, so a `Vec4` add is one instruction
and `Mat4 * Vec4` is four broadcasts, four multiplies and three adds on whole columns. The win grows
with batches (`TransformBatch` keeps the matrix in registers for the whole array). A `Vec2` would use
half a register and the load/shuffle/store overhead eats the gain; fast 2D batches need a
structure-of-arrays layout instead (all x together, all y together).

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
| `InputTests` | key down/pressed/released edges, taps within one frame, fixed-step edges, `ReleaseAll`; actions with several keys, action taps across fixed steps, axes, rebinding; gamepads (synthetic pads, no hardware): deadzone math (per-axis, radial), trigger/stick virtual buttons with hysteresis, button edges across fixed steps, several pads, labels, gamepad bindings and largest-magnitude axes; `FixedTimestep` accumulation, average rate at 144 fps / 120 Hz, slow-frame clamp |
| `AudioTests` | MP3 decoding from an embedded 809-byte file (length, level, channels, pitch after resampling), garbage rejected, WAV loading, `LoadSound` by extension incl. unknown/missing files, `MakeSound` conversion; mixer handles (stale handles, reuse, releasing samples), fade-in/out and volume ramps without clicks, looping, pitch, pan, master volume/mute, voice stealing, soft limiter; synth waveforms (length, no NaN, peak), envelopes, lowpass |
| `Renderer2DTests` | `Renderer2D` batching and shape generation on the CPU (no GPU), `Transform2D`, color packing; sprite quads (UVs, rotation, origin, flips, pixel snap), draw order across lines/sprites/texture switches and blend modes, atlas JSON parsing; `Camera2D` (pixel-space default, letterboxing, zoom/rotation, `ScreenToWorld` round trips against the GPU matrix, bounds clamp, follow dead zone and step-size independent damping, shake decay); atlas `"animations"` parsing (patterns, lists, durations, modes, missing frames reported), `Animator` loop / once / ping-pong timing at several dt, speed, stop/resume, finish and loop events, drawing a frame with flip and tint; `CrtEffect` afterglow decay, uniforms and bloom spread; `--gpu` parsing and driver names |
| `ParticleTests` | particle spawning (shapes, ranges, base velocity), capacity limit, drag/gravity step, swap-remove, continuous rate, color/size fade when drawing, scalar and SSE updates agreeing over 240 steps |
| `CollisionTests` | circle/circle, circle/AABB, AABB/AABB and SAT polygon contacts (normals, depths, touching = none, concentric circles, center inside a box, containment, winding, degenerate input); raycasts against circles, boxes and polygons (hits, misses, parallel, max distance, starting inside); `SpatialHash` insert/update/remove/query/pairs, wrap-around, brute-force equivalence on random data |
| `AssetTests` | asset manager bookkeeping with a GPU-less loader: dedupe (same path, `..` paths, absolute paths; other options or types are other assets), handle copy/move/reset reference counts, unloading on `Update` and reviving before it, placeholders for missing and broken files (texture, atlas, font, sound), hot reload in place (textures, atlas image + JSON with sprites and animators keeping their pointers, fonts, real WAVs), broken reloads keeping the old version, placeholders replaced when the file appears, reloads through `Update` on the thread pool within a second |
| `TilemapTests` | the sample room from Tiled JSON: layer order and kinds, external `.tsj` and embedded tilesets (GID lookup, sprite regions), objects (shapes, class, position, size, properties), the collision grid from tile properties and classes (non-colliding layer), tile ranges with touching edges, `OverlapsSolid`, `MoveAndCollide` (flush stops, no tunneling, sliding along walls, one-way from above / below / inside); all 8 flip-bit combinations against Tiled's transform; missing and broken files (JSON, sizes, infinite, isometric, missing/XML tileset, missing image, base64, bad cells) logged and failing cleanly, unknown tile ids left empty; through the asset manager: placeholder for a missing map, hot reload of the map and of its external tileset, broken edits keeping the last version |
| `SceneTests` | the scene stack without a GPU: requests applied only at the end of `Update`, hook order for push / pop / replace / clear / `ReplaceAll` (pause, resume, exit, destruction), requests from inside a scene's own hooks, `DrawBelow` / `UpdateBelow` chains (which scenes draw and update, in which order), fade timing (change at full cover, then uncover, requests queued meanwhile), fading into an empty stack, custom transition `Draw`, input blocked below the top and during transitions (and the app's own block kept), empty-stack pops, exiting every scene on destruction |
| `EntityTests` | add / get / has / remove and replacing components, deferred destroy (skipped by `Each`, invalid at once, destructors at `Flush`, stale handles after slot reuse), `Clear`; spawning, adding and destroying inside `Each`; churn of 400,000 spawns without leaks (destructor counts, bounded storage); movement, animation (an `OnFinished` that destroys); collisions (circle / circle normal and depth, circle / box, layer masks, destroyed and collider-less entities leaving the broadphase); `DrawSprites` order by layer and y, culling |
| `PlatformerTests` | slope tiles from the tileset (heights, flipped cells mirrored, floor heights, never blocking sideways); walking right and left over 45 and 22.5 degree hills grounded every step with the feet on the floor and always moving (no bounce, no sticking); landing on a slope and jumping along it; jumping up through a one-way platform and landing on it, down + jump dropping through to the ground, down + jump on solid ground being a jump; coyote time (in time, too late, off, longer) and the jump buffer (pressed early enough, too early, off, longer); variable jump height; walls and ceilings; a scripted 50 s run on the sandbox level replayed twice with the same positions bit for bit |
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

## Renderer (SDL GPU)

Emerald renders exclusively through [SDL GPU](https://wiki.libsdl.org/SDL3/CategoryGPU), SDL3's modern
explicit graphics API. SDL picks the backend at runtime:

| Platform | Backend | Shader format loaded |
|---|---|---|
| Linux, Windows, Android | Vulkan | SPIR-V (`.spv`) |
| Windows, Xbox | Direct3D 12 | DXIL (`.dxil`) |
| macOS, iOS | Metal | MSL (`.msl`) |

The code lives in `include/Emerald/Renderer/` and `src/Renderer/` and is intentionally small:

- **`Renderer`** – creates the `SDL_GPUDevice` (debug/validation mode in debug builds), claims the window
  (swapchain), and runs the frame: `BeginFrame()` acquires a command buffer and the swapchain texture,
  `BeginRenderPass(clearColor)` / `EndRenderPass()`, `EndFrame()` submits and presents. VSync is applied
  through the swapchain present mode (`WindowSpec::VSync`; off = mailbox/immediate when available).
  `Renderer::SetVSync(bool)` changes it at runtime (e.g. from an options menu).
- `Window::SetFullscreen(bool)` switches to borderless fullscreen on the current display and back
  (`WindowSpec::Fullscreen` starts that way); `Window::SetIcon(image)` sets the title bar /
  taskbar icon from an RGBA `Image`.
  When the window is minimized the swapchain texture is null and the frame is skipped.
  `CreateBuffer()` shows the upload path (transfer buffer → copy pass → GPU buffer) and
  `RequestScreenshot()` reads the next frame back into a PNG.
- **`LoadShader()`** – loads `bin/shaders/<Name>.<spv|dxil|msl>` for whichever format the device
  supports (`SDL_GetGPUShaderFormats`) and reads the resource counts from the reflection `.json`.
- **`CreateGraphicsPipeline()`** – a graphics pipeline from shaders, vertex layout and color format.

`Application` drives it every frame; override `OnStart()` to create GPU resources,
`OnRender(SDL_GPURenderPass*)` to record draws into the (already cleared) main pass, and
`OnShutdown()` to release them. With ImGui enabled, the `imgui_impl_sdlgpu3` backend draws into the same
render pass (`ImGui_ImplSDLGPU3_PrepareDrawData` before the pass, `RenderDrawData` inside).

One frame in `Application::Run()`:

```
events -> Input          OnEvent
fixed steps (0..N)       OnFixedUpdate(dt)       game logic at ApplicationSpec::FixedUpdateRate
                         OnUpdate(frameTime)
BeginFrame               (acquire command buffer + swapchain; vsync waits here)
                         OnRender2D(renderer2D)  record 2D shapes and sprites (CPU only)
copy passes              Renderer2D upload (lines + sprites), ImGui upload
render pass              OnRender(pass), then the 2D shapes/sprites, then ImGui on top
                         (with the CRT effect: into its scene texture, then the CRT passes,
                         then ImGui in a pass that keeps the image)
EndFrame                 submit + present
```

### Unsupported GPUs

If no backend is available (no Vulkan / D3D12 / Metal capable driver), the engine logs the
`SDL_GetError()` reason, shows a message box explaining that a Vulkan, Direct3D 12 or Metal capable
GPU driver is required, and `Run()` returns exit code `1`. (A bad `--gpu` or `SDL_GPU_DRIVER` alone
does not get here: the engine falls back to auto.) On Linux you can try this path by hiding the
Vulkan drivers: `VK_DRIVER_FILES=/nonexistent ./build/debug/bin/Sandbox`.

## 2D shapes (`Renderer2D`)

`Emerald::Renderer2D` (`include/Emerald/Renderer/Renderer2D.h`) batches 1 px lines: every shape
becomes line-list vertices (position + packed RGBA color) on the CPU, the whole frame is uploaded
with **one** transfer buffer + copy pass into a vertex buffer that grows as needed, and each
`Begin`/`End` batch is **one** draw call with its view-projection matrix pushed as a uniform
(`shaders/Renderer2D.*.hlsl`, alpha blending on). The Application owns one; record shapes in
`OnRender2D`:

```cpp
void OnRender2D(Emerald::Renderer2D& r) override
{
    const Vec2 size(GetWindowSize());
    r.Begin(Mat4::OrthoPixelSpace(size.x, size.y)); // (0, 0) top-left, +Y down
    r.DrawLine({10, 10}, {200, 60}, {1, 1, 1, 1});
    r.DrawPolygon(shipPoints, color, {.Position = pos, .Rotation = angle, .Scale = Vec2(2)});
    r.DrawPolyline(points, color);                  // open; pass closed = true to close it
    r.DrawCircle({400, 300}, 50, color, 24);        // 24 segments
    r.DrawRect({20, 20}, {100, 50}, color);         // outline
    r.FillRect({20, 80}, {100, 50}, color);         // filled (a tinted white sprite)
    r.End();
}
```

Why a separate hook instead of drawing in `OnRender`: uploads need a copy pass, and SDL GPU does not
allow copy passes inside a render pass. So the shapes are recorded first, the Application uploads
them before `BeginRenderPass`, and draws them inside it (after `OnRender`, below ImGui). Several
`Begin`/`End` batches per frame are fine (e.g. world and HUD with different projections), and
`Begin(viewProjection, clip)` takes an optional `SDL_Rect` clip rectangle in render-target pixels
(e.g. to keep a letterboxed playfield out of the black bars; `GetRenderer().GetFrameWidth()/Height()`
give the target size inside `OnRender2D`).
`Transform2D` applies scale, then rotation (radians, clockwise on screen in y-down space), then
position. The recording side needs no GPU, which is what `Renderer2DTests` checks. Textured
sprites go through the same batches; see [Textures and sprites](#textures-and-sprites).

`r.SetBlendMode(BlendMode::Additive)` switches the following draws of the batch to additive
blending (`out = src * alpha + dst`): overlapping lines or sprites add up and glow, which suits
light such as sparks and fire. `Begin` resets it to `Alpha`; each change starts a new draw call.

## 2D camera (`Camera2D`)

`Emerald::Camera2D` (`include/Emerald/Renderer/Camera2D.h`) decides which part of the world shows
where. It looks at a position (the center of the view) and shows `ViewSize` world units at zoom 1,
rotated by `Rotation`; the view is fitted into the render target with a uniform scale and centered,
with bars when the aspect ratios differ (letterboxing), and `Renderer2D::Begin(camera)` clips the
batch to that viewport. A default camera over its own view size is exactly
`Mat4::OrthoPixelSpace`, so nothing changes until you move it. Draw the HUD and menus in a second,
screen-space batch so they are not affected:

```cpp
Emerald::Camera2D m_Camera{{1280.0f, 720.0f}}; // world units visible at zoom 1

void OnStart() override
{
    m_Camera.SetBounds({.Min = {0.0f, 0.0f}, .Max = {4000.0f, 2000.0f}}); // never shows outside
    m_Camera.GetFollowParams() = {.DeadZone = {120.0f, 80.0f}, .Damping = 6.0f};
}
void OnFixedUpdate(f32 dt) override
{
    m_Camera.Follow(m_Player.Position, dt); // dead zone, then exponential damping
    if (hit)
        m_Camera.AddTrauma(0.5f); // shake: trauma 0..1, decays linearly
    m_Camera.Update(dt);
}
void OnRender2D(Emerald::Renderer2D& r) override
{
    const Emerald::Renderer& gpu = GetRenderer();
    m_Camera.SetTargetSize({f32(gpu.GetFrameWidth()), f32(gpu.GetFrameHeight())});
    r.Begin(m_Camera); // world
    ...
    r.End();
    r.Begin(Mat4::OrthoPixelSpace(width, height)); // HUD, text
    ...
    r.End();
}
```

- `SetPosition`, `SetZoom` (> 1 magnifies), `SetRotation` (radians), `SetViewSize`,
  `SetTargetSize` (render target pixels), `SetBounds` / `ClearBounds`.
- `ScreenToWorld` / `WorldToScreen` convert between render target pixels (top-left origin) and
  world units, including zoom, rotation, letterboxing and the current shake (mouse picking hits
  what is on screen). Mouse coordinates are in window units: multiply by the pixel density first.
- `GetViewProjection()`, `GetView()`, `GetViewport()`, `GetClip()`, `GetPixelsPerUnit()` and
  `GetVisibleBounds()` (for culling) expose the rest.
- Follow: the target moves freely inside the dead zone; outside it the camera closes the gap by
  `1 - exp(-Damping * dt)` per step, so it moves the same at any step size (0 snaps).
- Shake (`GetShakeParams()`): the offset is `MaxOffset * trauma^2` (in view units, so the same on
  screen at any zoom) plus up to `MaxAngle` of rotation, driven by smooth noise over time, so it is
  frame-rate independent. `Enabled = false` keeps it still (e.g. a "screen shake" option). The
  shaken view is clamped to the bounds too.

The camera has no interpolation between fixed steps: update it where the things it follows move.

## Assets (`Assets`)

The asset manager loads textures, atlases, fonts, sounds and tilemaps by path and hands out
`AssetHandle<T>`s. Application owns one (`GetAssets()`):

```cpp
// In OnStart (relative paths start at Paths::GetBasePath(), or at SetRoot's folder):
m_Ship = GetAssets().Load<Emerald::Texture>("assets/ship.png");
m_Hero = GetAssets().Load<Emerald::TextureAtlas>("assets/hero.json"); // + hero.png next to it
m_Font = GetAssets().Load<Emerald::Font>("assets/ui.ttf", {.Size = 16.0f});
m_Boom = GetAssets().Load<Emerald::Sound>("assets/boom.wav");
m_Level = GetAssets().Load<Emerald::Tilemap>("assets/level1.tmj"); // see Tilemaps

// Later: use the handle like a pointer.
r.DrawSprite(*m_Ship, position);
r.DrawString(*m_Font, "SCORE", {20, 20}, white);
GetAudio().Play(*m_Boom);
```

- **Loaded once.** Loading the same file again returns the same handle, and the file is not read
  again. The file counts as the same however its path is written (`"a/../ship.png"` =
  `"ship.png"`, and on Windows any letter case). The type and the options count too: the same
  TTF at two sizes is two fonts.
- **Reference counted.** Handles copy like `std::shared_ptr`. When the last handle to an asset
  is gone, the asset is unloaded at the start of the next frame, so a texture dropped mid-frame is
  still there when that frame is drawn. Handles must be released before the manager is destroyed;
  members of your Application are.
- **Missing or broken files never crash.** The error is logged and you get a placeholder that is
  easy to spot on screen:
  - textures: a magenta/black checkerboard;
  - atlases: every sprite and animation shows that checkerboard;
  - fonts: every character is a hollow box;
  - sounds: a tenth of a second of silence;
  - tilemaps: an empty map (no layers, nothing collides).
- **Hot reload (debug builds).** Edit a PNG, an atlas JSON, a WAV/MP3, a font or a Tiled map
  (or one of its tilesets) while the game runs, and it updates within about half a second. Release builds compile this out.

How hot reload works:

1. Every 0.25 s, a thread pool task reads the modification time of every loaded file. Only the
   time is read, not the file.
2. Back on the main thread, `Assets::Update` (called by Application before each frame) compares
   the times. A file counts as changed once its time differs from when it was loaded and has stayed
   the same for one more check. That way a file the editor is still writing is not read half
   done.
3. The asset is loaded again on the main thread, since GPU uploads happen there. The new version
   then replaces the old one *in place*, inside the same object, so nothing pointing at it
   breaks:
   - handles keep working;
   - a `Sprite` keeps pointing at the atlas texture;
   - an `Animator` keeps pointing at its animation (if the reloaded animation has fewer frames,
     the animator wraps around).
4. If the new version does not load (say, a half-saved PNG), the old one stays and the next
   change tries again. A placeholder whose file shows up later is loaded the same way.

The manager only does the bookkeeping. An `AssetLoader` does the actual loading:
`GpuAssetLoader` for the real thing, or a GPU-less fake in tests (see `tests/AssetTests.cpp`).

In the debug sandbox, the assets come straight from `sandbox/assets/`:

- Recolor `sprites/hero.png` while it runs and the hero changes.
- `sprites/missing.png` doesn't exist, so the sandbox shows the checkerboard placeholder until
  you put a PNG there.
- The ImGui panel lists every asset with its reference count, reloads and whether it is a
  placeholder.

## Textures and sprites

`Emerald::Texture` (`Renderer/Texture.h`) is an RGBA8 GPU texture plus its sampler. It is
move-only and frees both when destroyed, so keep it alive as long as sprites use it (e.g. a member of
your Application, created in `OnStart`, which runs after the GPU is set up):

```cpp
SDL_GPUDevice* device = GetRenderer().GetDevice();
const std::filesystem::path assets = Emerald::Paths::GetBasePath() / "assets";

// Any file stb_image reads (PNG, JPG, BMP, TGA, ...). Nearest filtering + clamp by default (pixel art).
std::optional<Emerald::Texture> logo = Emerald::Texture::Load(device, assets / "logo.png");
auto photo = Emerald::Texture::Load(device, assets / "photo.jpg",
                                    {.Filter = Emerald::TextureFilter::Linear});

// Several sprites in one image + a JSON file naming their rectangles (pixels, top-left origin):
//   { "ship": { "x": 1, "y": 1, "w": 38, "h": 80 }, "rock": { ... } }
// (TexturePacker's "JSON (Hash)" export, {"frames": {name: {"frame": {x, y, w, h}}}}, works too.)
std::optional<Emerald::TextureAtlas> atlas =
    Emerald::TextureAtlas::Load(device, assets / "atlas.png", assets / "atlas.json");
Emerald::Sprite ship = atlas->Get("ship");   // Find() returns std::optional instead
Emerald::Sprite hull = ship.Crop({0, 0}, {38, 66}); // part of a region, e.g. an animation frame
```

`Texture::Create(device, image)` uploads an `Emerald::Image` you built yourself (the sandbox makes
its sprites procedurally). The upload uses its own command buffer and copy pass, so it works at any
time outside a render pass. Relative paths are resolved against the working directory, which is
why the example uses `Paths::GetBasePath()` (the executable's folder, where the build copies assets).

Draw sprites with `Renderer2D`, between the same `Begin`/`End` as the shapes:

```cpp
r.DrawSprite(ship, pos, {.Scale = Vec2(0.5f), .Rotation = angle});      // centered on pos
r.DrawSprite(ship, pos, {.Size = {32, 32}, .Origin = {0.5f, 1.0f}});    // feet at pos
r.DrawSprite(*logo, {20, 20}, {.Origin = {0, 0}, .Tint = {1, 1, 1, 0.5f}, .FlipX = true,
                               .PixelSnap = true});
```

`SpriteOptions` (all optional, in this order for designated initializers): `Size` (world units;
zero = region size × `Scale`), `Scale`, `Rotation` (radians, clockwise on screen), `Origin` (pivot
as a fraction of the size, default center), `Tint` (multiplies color and alpha), `FlipX`/`FlipY`,
`PixelSnap` (moves the unrotated top-left corner onto a whole unit, which is a whole pixel with a
pixel-space projection, so pixel art stays crisp).

How it is drawn:

- Every sprite is a quad of 6 vertices (position, UV, packed tint) in one sprite vertex stream,
  uploaded in the same copy pass as the lines. The sprite pipeline (`shaders/Sprite.*.hlsl`)
  samples the texture and multiplies by the tint.
- **Draw order is call order**, lines and sprites mixed. Consecutive sprites with the same texture
  share one draw call, and so do consecutive lines; switching between lines and sprites, or to
  another texture, starts a new one. Put sprites that share an atlas next to each other for fewer
  draw calls (`GetLastFrameDrawCalls()` shows the count).
- Blending uses **straight (non-premultiplied) alpha**, like PNG files: `src × a + dst × (1 − a)`.
  With `Linear` filtering, fully transparent pixels still get blended with their neighbours, so give
  them the edge color (most tools do) or you get dark fringes; `Nearest` has no such problem.

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

## Text (`Font`)

`Emerald::Font` (`Renderer/Font.h`) loads a TrueType/OpenType file with stb_truetype and bakes its
glyphs at one size into an atlas texture (`stbtt_PackFontRanges`, packed with stb_rect_pack). Text
is drawn through the sprite batch, so a run of text in one font is one draw call:

```cpp
const std::filesystem::path path = Emerald::Paths::GetBasePath() / "assets/fonts/MyFont.ttf";
// Smooth text: Linear filtering, 2x oversampling (the defaults), 24 px em size, ASCII 32..126.
std::optional<Emerald::Font> ui = Emerald::Font::Load(device, path, {.Size = 24.0f});
// A pixel font: bake at a multiple of its grid, no oversampling, Nearest (snapped to whole pixels).
auto pixel = Emerald::Font::Load(device, path, {.Size = 16.0f,
                                                .Ranges = {Emerald::kAsciiGlyphs, Emerald::kLatin1Glyphs},
                                                .Oversample = 1,
                                                .Filter = Emerald::TextureFilter::Nearest});

r.DrawString(*ui, "Score: 1200\nLives: 3", {20, 20}, {1, 1, 1, 1});             // top-left at (20, 20)
r.DrawString(*pixel, "GAME OVER", {640, 300}, {1, 0.3f, 0.3f, 1}, 3.0f,        // 3x, centered on x
           Emerald::TextAlign::Center);
const Vec2 size = ui->MeasureText("Score: 1200");                           // width, height in px
```

- `DrawString(font, text, position, color, scale = 1, align = Left)`: UTF-8 text, `'\n'` starts a
  new line, pairs are kerned (the font's GPOS or `kern` table). `position` is the top-left of the
  first line (its ascent line), or its top center / top right for `TextAlign::Center` / `Right`;
  every line is aligned on its own.
- `MeasureText(text, scale)` returns the widest line's advance and the height of all lines (the
  last one ascent to descent, the others a full line height). `GetAscent()`, `GetDescent()`
  (negative, below the baseline) and `GetLineHeight()` give the metrics at the baked size;
  `LayoutText` returns the placed glyphs for custom effects.
- `FontOptions` (in this order): `Size` (em size in pixels, like CSS `font-size`), `Ranges`
  (codepoint blocks; `kAsciiGlyphs` by default, add `kLatin1Glyphs` or your own `{first, count}`),
  `Oversample` (1..8: the glyphs are rendered that much larger for smoother, sub-pixel placed
  edges), `Filter`. Characters that were not baked are drawn as `?`.
- Load one `Font` per size you need; each has its own atlas texture. Scaling a baked font works too
  (pixel fonts at whole multiples, smooth fonts a little up or down), but a bake at the target size
  looks best. `Font::Load(nullptr, ...)` works without a GPU (measuring and layout, e.g. in tests).
- `Font::LoadFromMemory(device, bytes, options)` takes a font embedded in the executable.

The sandbox's text demo uses Press Start 2P (SIL Open Font License, in `sandbox/assets/fonts/`,
see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)); the engine itself ships no fonts.

## CRT post-process (`CrtEffect`)

An optional fullscreen post-process that makes the frame look like a CRT monitor. It is off by
default; switch it on with `Application::SetCrtEnabled(true)` (returns false, logged, if its
pipelines could not be created) and tune it through `GetCrtParams()`:

```cpp
SetCrtEnabled(true);
Emerald::CrtParams& crt = GetCrtParams();
crt.Scanlines = 0.25f; // a raster look; the defaults are for a vector monitor
```

While it is on, the scene (`OnRender` and the 2D shapes) is rendered into an offscreen texture and
`CrtEffect` (`include/Emerald/Renderer/CrtEffect.h`, `shaders/Fullscreen.vert.hlsl` +
`Crt*.frag.hlsl`) draws it into the frame in a few fullscreen passes; ImGui is drawn on top,
unaffected, and screenshots include the effect:

1. **phosphor** (`CrtPhosphor.frag`): the new frame over the previous one faded by
   `0.5^(dt / Afterglow)`, into a 16-bit float history texture (ping-pong), so moving lines leave
   short trails that fade the same at any frame rate;
2. **bloom** (`CrtBlur.frag`): separable 9-tap Gaussian blurs at 1/2 size (near glow) and 1/4 size
   (wide glow, 3x the spread);
3. **composite** (`Crt.frag`): barrel curvature (rounded corners, black outside the glass), the
   image plus both glows, red/blue chromatic offset growing towards the edges, vignette, and the
   optional scanlines and RGB aperture mask.

| `CrtParams` | Default | |
|---|---|---|
| `Curvature` | 0.07 | barrel distortion (0 = flat) |
| `Bloom` | 2.0 | glow strength added to the image |
| `BloomRadius` | 1.0 | glow spread; 1 = near glow sigma of 0.8% of the screen height |
| `Vignette` | 0.35 | corner darkening |
| `ChromaticOffset` | 1.0 | red/blue fringe at the edges, in pixels at 1080p |
| `Afterglow` | 0.035 | phosphor half-life in seconds (0 = off) |
| `Scanlines` | 0 | scanline darkness 0..1 (off: vector monitors have none) |
| `ScanlineCount` | 360 | scanlines over the screen height |
| `Mask` | 0 | RGB aperture grille 0..1 (off) |
| `Brightness` | 1.0 | final gain |

The textures follow the window size (recreated on resize). The sandbox toggles the effect with
**C**.

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

## Collision (`Physics/`)

`Emerald/Physics/Collision.h` has the shape tests a 2D game needs, and `SpatialHash.h` a
broadphase so you don't test every pair:

- **Shapes:** `Circle{Center, Radius}`, `Aabb{Min, Max}` (also `Aabb::FromCenter(c, half)`), and
  convex polygons as a `std::span<const Vec2>` of points, either winding.
- **`Overlaps(a, b)`:** yes or no, for circle/circle, AABB/AABB and circle/AABB.
- **`Collide(a, b)`:** an `std::optional<Contact>`. Its `Normal` is a unit vector from `a` towards
  `b` and `Depth` is how far they overlap, so moving `b` by `Normal * Depth` separates them. It
  covers circle/circle, circle/AABB (both orders), AABB/AABB and polygon/polygon (SAT).
- **Edge cases:**
  - Touching is not overlapping.
  - Concentric circles push apart along +X.
  - A circle whose center is inside a box exits through the nearest side.
- **`Raycast(Ray{origin, direction, maxDistance}, shape)`:** the first hit (`Distance`, `Point`,
  surface `Normal`) on a circle, AABB or polygon. The direction doesn't need to be normalized. A
  ray that starts inside hits at distance 0.

```cpp
#include <Emerald/Physics/Collision.h>
#include <Emerald/Physics/SpatialHash.h>

Emerald::SpatialHash hash(64.0f); // cell size: about the size of a typical object
// Every step: tell it where things are now (it re-buckets only what changed cells)...
for (u32 i = 0; i < balls.size(); ++i)
    hash.Update(i, Emerald::Aabb::FromCenter(balls[i].Center, Vec2(balls[i].Radius)));
// ...then run the exact test on the candidate pairs (each pair once, i < j, sorted).
hash.ForEachPair([&](u32 i, u32 j) {
    if (std::optional<Emerald::Contact> c = Emerald::Collide(balls[i], balls[j]))
        balls[j].Center += c->Normal * c->Depth; // or split it by mass, add an impulse...
});
hash.Query(area, ids); // ids whose boxes overlap `area`
```

**Wrap mode.** `SpatialHash(cell, Vec2{width, height})` makes space repeat like a torus, as in
Asteroids. A box hanging off the right edge finds objects at the left, and queries wrap the same
way. Results are sorted by id, so they don't depend on hash map order.

`CollisionTests` checks every shape pair's normal and depth, the edge cases above, raycasts (hits,
misses, parallel rays, max distance, starting inside), and the hash against brute force on random
data, with and without wrapping.

`EmeraldCollisionBench` (Release, `-DEMERALD_BUILD_BENCH=ON`) uses 10,000 moving circles (radius
2-8) in a 2000 x 2000 world. Each frame moves them, updates the hash, collects the pairs and runs
the exact test. Numbers are best of several runs on an 8-core Intel Xeon VM with GCC 14:

| 10k objects, cell 32 | Time |
|---|---:|
| Insert all | 1.2 ms |
| Move + `Update` all | 0.34 ms |
| 1000 `Query` calls (64 x 64 area, ~14 hits each) | 1.4 ms |
| `GetPairs` + exact circle test (5,358 box pairs, 4,231 contacts) | 1.36 ms |
| Brute force, all 50M pairs (same 4,231 contacts) | 66 ms (about 35x slower) |

With 100k objects the update takes 4.5 ms and finding pairs 20 ms. The best cell size is about 1-4x
the object size: smaller cells put each object in more cells, and larger cells make for more
candidate pairs.

In the sandbox, walk the hero right out of the room into the **collision yard**:

- Balls bounce off each other, using the hash and circle contacts with mass ~ area, and off the
  boxes.
- The hero's circle shoves the balls and is blocked by the boxes.
- A spinning hexagon and an orbiting triangle turn red while SAT finds an overlap, with the
  contact normal drawn.
- A ray goes from the hero to the mouse and shows the nearest hit with its normal.

The ImGui panel shows the counts (cells, candidate pairs, contacts), the SAT result and the ray
distance, and has a toggle for the hash cells.

## Tilemaps (`Tilemap/`)

`Emerald/Tilemap/Tilemap.h` loads maps made in [Tiled](https://www.mapeditor.org/) in its JSON
format (`.tmj`). It reads tile layers, tilesets and object layers, answers tile collision queries,
and draws only the tiles the camera sees.

```cpp
// OnStart: through the asset manager (hot reload in debug builds), or Tilemap::Load(device, path).
m_Map = GetAssets().Load<Emerald::Tilemap>("maps/level1.tmj");
m_Camera.SetBounds(m_Map->GetBounds()); // the camera never shows outside the map
const Emerald::MapObject* spawn = m_Map->FindObject("spawn");

// OnFixedUpdate: move a box with tile collision (x first, then y, swept: no tunneling).
const Emerald::TileMove move = m_Map->MoveAndCollide(m_Box, m_Velocity * dt);
m_Box.Min += move.Delta;
m_Box.Max += move.Delta;
if (move.HitBottom)
    m_Velocity.y = 0.0f; // landed (floor or one-way platform)

// OnRender2D: every visible tile layer in order, culled to the camera.
r.Begin(m_Camera);
m_Map->Draw(r, m_Camera.GetVisibleBounds());
r.End();
```

What the loader reads:

- **Maps:** orthogonal, fixed size (not "infinite"), with the tile layer format set to CSV (the
  default). Map, layer, tileset, tile and object custom properties are all kept in a
  `Properties` (`GetBool`, `GetInt`, `GetFloat`, `GetString`, each with a fallback).
- **Layers** come in Tiled's order (`GetLayers()`, `FindLayer(name)`). A tile layer has
  `Cells`, one per tile, row by row. Group layers are flattened into their children: offsets add
  up, opacity multiplies and a hidden group hides its children. Image layers are skipped with a
  warning.
- **Tilesets:** embedded in the map or external `.tsj` files (a `.tsx` gives an error saying to
  save it as JSON). One image per tileset, with margin, spacing and drawing offset. The images
  load as textures relative to the file that names them.
- **Objects** (`MapLayer::Objects`, `FindObject(name)`): `Name`, `Type` (Tiled's "Class"),
  `Position` and `Size` in map pixels with the position at the top-left (Tiled puts tile objects
  at the bottom-left, so the loader moves them), `Rotation`, `Shape` (rectangle, point, ellipse,
  polygon, polyline, tile, text), polygon `Points`, `Gid` for tile objects, and `Props`.
- **Flip bits:** a cell is a GID plus Tiled's flip bits (`kTileFlipX`, `kTileFlipY`,
  `kTileFlipDiag`). `GetTileGid(cell)` strips them and `GetTileTransform(cell)` turns them into
  `SpriteOptions` flips plus a quarter turn, which is how `DrawLayer` draws rotated tiles.
- **Errors never crash.** A missing map, tileset or image, broken JSON, or unsupported data
  (infinite, isometric, base64, wrong cell count) logs what is wrong with the file name and
  returns `nullopt`. The asset manager then shows an empty map, or keeps the last good version on
  hot reload. Cells with unknown tile ids are logged and left empty.

**Collision.** Every tile layer's tiles go into one grid when the map loads, from properties on
the tiles in the tileset:

| In Tiled, on the tile (tileset editor) | Result |
|---|---|
| Custom property `solid` (bool, ticked), or Class `solid` | `TileCollision::Solid`: blocks from every side |
| Custom property `oneway` (bool, ticked), or Class `oneway` | `TileCollision::OneWay`: a platform top, only blocks moving down onto it from above |
| Float properties `slopeLeft` / `slopeRight`, or Class `slope` | `TileCollision::Slope`: a sloped floor (see [Platformer physics](#platformer-physics-physicsplatformerh)) |
| A bool property `collision` set to false *on a tile layer* | that layer's tiles never collide (decoration drawn over the hero, say) |

Solid wins over slope and slope over one-way when layers overlap, and outside the map counts as empty, so put walls at
the edges. The queries:

- `GetCollision(tile)`
- `WorldToTile` and `GetTileBounds`
- `GetTileRange(area)` and `ForEachCollidingTile(area, fn)` for your own tests
- `OverlapsSolid(area)`
- `MoveAndCollide(box, delta, options)`, which moves an AABB and reports `HitLeft` / `HitRight` /
  `HitTop` / `HitBottom` (on the ground), plus `OnSlope` / `OnOneWay`

The platformer physics builds on these (see [Platformer physics](#platformer-physics-physicsplatformerh)).

**Drawing.** `DrawLayer(r, layer, view)` draws only the tiles that overlap `view` (use
`Camera2D::GetVisibleBounds()`) and returns how many it drew. `Draw` does every visible tile layer
in order, and `DrawCollision` is a debug overlay: solid tiles in red, one-way tops in yellow, slopes in cyan. To
put characters between layers, call `DrawLayer` per layer (the sandbox draws the hero where the
"Objects" layer is).

**Hot reload.** A tilemap asset watches the `.tmj`, its external `.tsj` files and every tileset
image. Saving any of them in Tiled (or an image editor) reloads the whole map within about half a
second. Read layers, objects and tilesets through the handle each time instead of keeping
pointers into them.

**The sandbox's tilemap room.** Pick it on the title screen, press T in the camera demo, or start
the sandbox with `--tilemap`:

- **The map:** `sandbox/assets/tilemaps/room.tmj` is 48 x 30 tiles of 16 px.
  - Layers: Ground, Walls and Decor, then Objects, then Overhead (pillar tops drawn over the
    hero).
  - Tilesets: `dungeon.tsj` (external) and `props` (embedded in the map).
  - Objects: a spawn point, two signs, a chest with `gold` / `contents` properties, the pond (an
    ellipse) and the stairs.
- **What you can do:** walk with WASD; the camera follows inside the map. The wooden railings are
  one-way: walk up through them, but they stop you walking down.
- **The flip row:** the arrow row near the spawn shows all eight flip combinations.
- **The ImGui panel:**
  - toggles for each layer, the collision overlay, object outlines and culling;
  - the number of visible tiles drawn;
  - a zoom slider;
  - the object the hero is standing in, with its properties.
- **Hot reload:** in the debug build, edit `room.tmj` or `dungeon.tsj` in Tiled while the sandbox
  runs and the room updates.

All of it comes from `tools/tilemaps/make_tilemaps.py`, which draws the art in code with a fixed
16-color palette and the standard library only. It writes `dungeon.png`, `dungeon.tsj`,
`props.png` and `room.tmj`. Running it again overwrites edits made in Tiled.

**Benchmark: 500 x 500 tiles.** Generate the map, then let the camera sweep it:

```sh
python3 tools/tilemaps/make_tilemaps.py --bench build/bench   # build/bench/bench.tmj
./build/release/bin/Sandbox --map build/bench/bench.tmj --pan --stats [--zoom 0.25] [--no-cull]
```

The map has three full 500 x 500 tile layers (Ground, Walls, Decor). The numbers below are from
the release build on an 8-core Intel Xeon VM with Mesa's software Vulkan driver (lavapipe) under
Xvfb, at 1280 x 720 with no vsync, averaged over 2 s windows:

| View | Tiles drawn per frame | `DrawLayer` CPU time | Frame rate |
|---|---:|---:|---:|
| Zoom 1, culled | ~4,200 | 0.15 ms | ~230 fps |
| Zoom 0.25 (16x the area), culled | ~65,800 | 2.5 ms | ~46 fps (lavapipe filling pixels) |
| Zoom 1, no culling (the whole map) | 285,269 | 10.4 ms | ~29 fps |

Culling keeps the cost at the size of the screen rather than the size of the map. The map loads
(1.5 MB of JSON) in about 50 ms.

`TilemapTests` loads the sample room, checks it in detail and breaks copies of it on purpose (see
the table under [Tests](#tests)).

## Platformer physics (`Physics/Platformer.h`)

A kinematic character for side-view games. A box runs, jumps and falls against a tilemap's
collision. There are no forces and no rigid bodies, only the body's own velocity, moved with
`Tilemap::MoveAndCollide` every fixed step.

```cpp
#include <Emerald/Physics/Platformer.h>

Emerald::PlatformerBody hero{.Position = spawn, .HalfSize = {5.0f, 7.0f}}; // the box's center
Emerald::PlatformerTunables tunables;                                       // the feel (below)

void OnFixedUpdate(f32 dt) override
{
    const Emerald::Input& input = GetInput();
    const Emerald::PlatformerInput in{.Move = input.GetAxis("MoveX"),
                                      .JumpPressed = input.WasActionPressed("Jump"),
                                      .JumpHeld = input.IsActionDown("Jump"),
                                      .Down = input.GetAxis("MoveY") > 0.5f};
    Emerald::StepPlatformer(hero, in, tunables, *map, dt);
    if (hero.Jumped)
        PlayJumpSound();
}
```

`PlatformerTunables` holds every number, in map pixels and seconds:

| Field | Default | What it does |
|---|---|---|
| `Gravity` | 1400 px/s² | pulls the body down |
| `MaxFallSpeed` | 420 px/s | terminal velocity |
| `RunSpeed` | 140 px/s | speed at full stick or key |
| `GroundAccel` / `GroundDecel` | 1600 / 2000 px/s² | towards the run speed on the ground, and back to 0 with no input |
| `AirAccel` | 1000 px/s² | control in the air, both ways |
| `JumpVelocity` | 400 px/s | upward speed when a jump starts (height v² / 2g, about 57 px) |
| `JumpCut` | 0.45 | releasing jump while rising multiplies the upward speed by this (variable jump height) |
| `CoyoteTime` | 0.10 s | after walking off a ledge, a jump still works for this long |
| `JumpBuffer` | 0.12 s | a jump pressed this long before landing happens on landing |
| `SnapDown` | 4 px | how far below a walking body still counts as ground (walking down slopes) |
| `DropThroughTime` | 0.2 s | how long one-way platforms are ignored after down + jump |

`PlatformerBody` holds the state:
- `Velocity`.
- The flags `Grounded`, `OnSlope`, `OnOneWay` and `Rising` (going up from a jump that can still
  be cut short).
- The timers `CoyoteTimer`, `BufferTimer` and `DropTimer`.
- What happened in the last step: `Jumped`, `Landed`, `DroppedThrough`, `HitWall` and
  `HitCeiling`, for sounds, animation and HUDs.
- `GetBox()` and `GetFeet()`.

**One step**, in order:
1. **Timers.** A jump press sets `BufferTimer` to `JumpBuffer`; otherwise it counts down.
2. **Run.** The horizontal speed accelerates towards `Move * RunSpeed`.
3. **Jump.** It happens if a jump is buffered (`BufferTimer > 0`) and the body is grounded or
   within coyote time. If the body stands on one-way tiles only and down is held, it drops
   through instead: `DropTimer` starts and no jump happens. Either way the buffer and coyote
   timers are cleared, so one press gives one jump.
4. **Jump cut.** Releasing jump while rising multiplies the upward speed by `JumpCut` once.
5. **Gravity,** capped at `MaxFallSpeed`.
6. **Move** with `MoveAndCollide`. One-way tiles are ignored while `DropTimer` runs.
   `SnapDown` applies only if the body was grounded, so jumps and drops never snap.
7. **Results.** Walls stop the horizontal speed and ceilings the upward speed. The body is
   grounded when it hit a floor while not moving up. `CoyoteTimer` is full while grounded and
   counts down in the air.

**How the cases work:**
- **Ground detection.** The move reports `HitBottom` when a floor stops the fall. Grounded
  bodies get a little gravity every step, so they stay in contact with the floor.
- **Slopes.** Slope tiles are floors under the box's **bottom center**, and they never block
  sideways or from below. The center decides, not the corners, so the box sinks into the slope
  by up to half its width. That's the usual look, and it means no jitter at tile edges.
  - **Up.** Walking up raises the floor by at most the distance walked (slopes are 45° at most),
    so the move lifts the box onto the surface.
  - **Down.** Walking down, `SnapDown` keeps the body on the surface instead of letting it fall
    in small hops.
  - **Top of a slope.** The box's front corner dips into the flat ground there. Walls that low
    don't stop a body standing on a slope, and the ground beside it can be stepped onto from a
    little below.
  - **Jumping along a slope.** The body's bottom is kept on the surface, so it lands on the
    slope rather than inside it.
- **One-way platforms.** They only stop a fall that starts above their top, so you jump up
  through them and land on them. `OnOneWay` is true only when nothing solid is under the box as
  well, and down + jump then drops through. If you don't press down, the jump is a normal one.
- **Coyote time.** The jump check uses "grounded, or `CoyoteTimer > 0`". The timer only counts
  down after the body has left the ground, so a late press just off a ledge still jumps.
- **Jump buffer.** The press is stored in `BufferTimer`, and the first step that is grounded
  with the timer still running jumps.
- **Determinism.** A step reads only the body, the input, the tunables and the map, and it is
  plain float math. Replaying the same inputs at the same fixed step gives the same path bit for
  bit. `PlatformerTests` replays a scripted 50 s run twice and compares every position's bits.
  Different builds and compilers may still round differently.

**Slopes in Tiled.** On a tile in the tileset, add float properties `slopeLeft` and `slopeRight`,
in pixels up from the tile's bottom, or set its Class to `slope`. Examples on 16 px tiles:
- `0` → `16`: 45°, rising to the right.
- `0` → `8` followed by `8` → `16`: 22.5° over two tiles.

Flip a cell horizontally (X in Tiled) to get the slope going down. The engine mirrors the
heights, so one tile serves both directions. Vertical and diagonal flips are ignored for
slopes: they are floors only, with no slopes on ceilings. `GetCollision` returns
`TileCollision::Slope` for them, `GetSlope(tile)` gives the heights after the flip, and
`GetSlopeFloorY(tile, x)` gives the surface. `DrawCollision` draws slopes as cyan lines. If
layers overlap, solid wins over slope and slope wins over one-way.

**`MoveAndCollide` options.** `TileMoveOptions{.IgnoreOneWay, .SnapDown}` are what the character
uses. The result's `OnSlope` and `OnOneWay` say what the floor is. Without options, moves behave
as before for maps without slopes.

**Entities.** `PlatformerBody` is a plain struct, so it can be a component as it is
(`entity.Add<PlatformerBody>(...)`). A system would also need each entity's input, so it is left
to the game. The phase 2 test game will add one if it needs several bodies.

**The sandbox's platformer scene.** It's in the title menu, after the entity swarm in the T
cycle, and behind `--platformer`. The level, `sandbox/assets/tilemaps/platformer.tmj` (96 x 22
tiles, from `tools/tilemaps/make_platformer.py`), has flat ground, 45° and 22.5° hills, three
one-way platforms stacked above the ground plus one more, a 4-tile gap for coyote time, and a
2-tile step for the jump buffer. Falling into the gap respawns you at the last checkpoint.

| Keyboard | PS4 / gamepad | Action |
|---|---|---|
| A / D, ← / → | d-pad, left stick | run |
| Space or Z (hold: higher) | Cross (South) | jump |
| S / ↓ + jump | d-pad down / stick down + Cross | drop through a one-way platform |
| R | Share (Back) | respawn at the checkpoint |
| O | | collision overlay |
| M | Options (Start) | pause |
| T | | next scene (camera demo) |

The scene shows:
- **HUD:** the grounded / slope / one-way / rising flags, the coyote, buffer and drop timers as
  bars, and counts of jumps, coyote jumps, buffered jumps and drops.
- **Popups** over the hero: "coyote jump!", "buffered jump!" and "drop through!", also logged.
- **Trail** of the feet over the last 3 s: green on the ground, yellow in coyote time, white in
  the air.
- **ImGui** (debug-full): sliders for every tunable, a reset, and the position and velocity.

## Scenes (`Scene/`)

`Emerald/Scene/SceneStack.h` keeps the screens of a game on a stack: title, gameplay, pause, game
over... Each one is a `Scene` with its own hooks, which mirror `Application`'s. The `Application`
owns a stack (`GetScenes()`) and runs it after its own hooks every frame. An app that never pushes
a scene works exactly as before.

```cpp
#include <Emerald/Scene/SceneStack.h>

class PauseScene final : public Emerald::Scene {
public:
    explicit PauseScene(Emerald::Application& app) : Scene("Pause"), m_App(app)
    {
        DrawBelow = true; // the game stays visible (frozen) under the menu
    }
    void OnEnter() override { /* open the menu */ }
    void OnUpdate(f32) override
    {
        if (m_App.GetInput().WasActionPressed("Pause"))
            GetStack()->Pop(Emerald::Transition::Fade(0.2f));
    }
    void OnRender2D(Emerald::Renderer2D& r) override { /* the menu */ }

private:
    Emerald::Application& m_App;
};

// From the game scene:
GetStack()->Push(std::make_unique<PauseScene>(m_App));
```

- **Hooks:** `OnEnter` / `OnExit` when a scene joins or leaves the stack, `OnPause` / `OnResume`
  when another scene is pushed on top of it or popped off it. Then `OnEvent`, `OnFixedUpdate`,
  `OnUpdate`, `OnRender` (raw SDL GPU draws), `OnRender2D` and `OnImGui`, as in `Application`.
- **Requests:** `Push`, `Pop`, `Replace` (pop + push), `Clear` (everything leaves, top first) and
  `ReplaceAll` (everything leaves, then a new scene enters: back to the title). Each one takes an
  optional `Transition`.
- **Deferred changes:** requests are queued and applied at the end of the stack's `Update`, after
  every scene has updated. A scene can pop itself or push the next one from inside its own hooks;
  nothing is destroyed while one of its hooks runs. Requests made during a transition wait for it.
- **Below the top:** with `DrawBelow` the scene under this one is drawn first (a pause menu over
  the game); with `UpdateBelow` it keeps updating too (a HUD over a running game). The flags chain:
  the stack draws downwards for as long as each scene above has `DrawBelow`. `IsDrawn(i)` and
  `IsUpdated(i)` say which scenes run this frame.
- **Input focus:** only the top scene gets input, and only while no transition runs (`HasFocus()`).
  For the others the stack sets `Input::SetBlocked(true)`: every action reads as up and every axis
  as 0. `OnEvent` goes to the focused scene only. The app's own hooks run first and see input
  unblocked (the sandbox's Escape and C keys work in every scene). The raw `Keyboard` and
  `Gamepads` are not blocked.
- **Transitions:** `Transition::Fade(seconds, color)` covers the screen in `seconds`, makes the
  change while it is fully covered, then uncovers it in `seconds` again. The amount is a tween
  (`Curve`, `QuadInOut` by default). Set `Draw` for a custom look: it gets the `Renderer2D`, the
  view size and the cover amount (0 to 1), in a pixel-space batch drawn over every scene. Pushing
  onto an empty stack with a transition starts covered, so the first scene fades in.
- **Shutdown:** the application exits every scene after the main loop, before `OnShutdown` and
  before the asset manager goes. Scenes can hold asset handles and GPU resources and release them
  in their destructor.

**Rock Blaster with scenes** (a sketch; the Asteroids project itself is unchanged). Its title
screen, game, pause menu and options menu map onto the stack like this:

```cpp
// Title: a menu; starting a game replaces it with a fade.
void TitleScene::OnUpdate(f32)
{
    if (input.WasActionPressed("Start"))
        GetStack()->Replace(std::make_unique<GameScene>(app), Transition::Fade(0.4f));
    else if (input.WasActionPressed("Options"))
        GetStack()->Push(std::make_unique<OptionsScene>(app)); // over the title (DrawBelow)
}

// Game: pausing pushes an overlay; losing the last ship goes to the game over screen.
void GameScene::OnUpdate(f32)
{
    if (input.WasActionPressed("Pause"))
        GetStack()->Push(std::make_unique<PauseScene>(app)); // DrawBelow: the frozen game shows
    if (m_Lives == 0)
        GetStack()->Replace(std::make_unique<GameOverScene>(app, m_Score), Transition::Fade(1.0f));
}
void GameScene::OnPause() { app.GetAudio().SetMuted(true); } // the thrust sound stops, etc.
void GameScene::OnResume() { app.GetAudio().SetMuted(false); }

// Pause: resume, options, or quit to the title.
void PauseScene::OnUpdate(f32)
{
    if (input.WasActionPressed("Pause"))
        GetStack()->Pop();
    else if (selected == Options)
        GetStack()->Push(std::make_unique<OptionsScene>(app)); // Pop returns to the pause menu
    else if (selected == QuitToTitle)
        GetStack()->ReplaceAll(std::make_unique<TitleScene>(app), Transition::Fade(0.4f));
}
```

The stack then reads, bottom to top: `Title`; or `Game`; or `Game, Pause`; or
`Game, Pause, Options`. The options scene is the same class wherever it is pushed from, and
popping it always returns to whatever opened it.

**The sandbox's scenes** (`sandbox/src`):

- **Title** (`TitleScene.h`): the tweened menu over drifting gems. "Camera demo" fades to black.
  "Tilemap room" uses a custom transition, horizontal blinds closing from alternate sides
  (`Blinds` in `Shared.h`). "Entity swarm" fades to white, "Platformer" to black. "Quit" quits.
- **Camera demo** (`DemoScene.h`): the shapes, sprites, text, camera, hero and collision yard.
- **Tilemap room** (`TilemapScene.h`): the Tiled map (see Tilemaps).
- **Entity swarm** (`SwarmScene.h`): thousands of entities in the scene's own `World` (see Entities).
- **Platformer** (`PlatformerScene.h`): the platformer level (see Platformer physics).
- **Pause** (`PauseScene.h`): M / Start in any game scene. It has `DrawBelow` and plays its
  menu intro over the frozen scene. Choices: Resume (the outro plays, then the scene pops), CRT
  effect, Replay intro, Title screen (`ReplaceAll` with a fade), Quit.
- T cycles camera demo → tilemap room → entity swarm → platformer → camera demo (`Replace` with a fade).
- `main.cpp` keeps what is global: input bindings, the shared assets (`Shared.h`), the triangle
  and the bouncing quads (entities in the app's `GetWorld()`) under every scene, Escape / C, and screenshots. It also builds the scenes
  (`SandboxShared::Make`), so they never include each other.
- **ImGui:** the panel's "Scene stack" section lists the scenes top first. For each one it shows
  DrawBelow and UpdateBelow (both editable), and whether it is drawn, updated and has focus. It
  also shows the transition's cover and the pending requests. Its buttons push a pause, pop
  (with a fade), go to the title (blinds) or go to the demo (white fade). Each scene appends its
  own section below.

`SceneTests` covers the stack without a GPU (see the table under [Tests](#tests)).

## Entities (`Entity/`)

Game objects are **entities** made of **components** (plain structs), updated by **systems**
(plain functions). The storage is [EnTT](https://github.com/skypjack/entt), behind a thin Emerald
layer: `World`, `Entity` and `Each`.

**Why EnTT behind a thin layer** (rather than our own small ECS):

- **For EnTT:** it is fast (packed component arrays, cache-friendly views), battle-tested in shipped
  games, and scales to hundreds of thousands of entities. Writing and debugging our own would take
  time that games should get.
- **For the layer:** game code reads like the rest of Emerald (`Spawn`, `Add`, `Each`, `Destroy`)
  instead of EnTT's API, destruction is always deferred (safe inside loops and callbacks), and
  the backend could be swapped later without touching games.
- **Against EnTT:** it is template-heavy, so mistakes give long error messages and the headers cost
  compile time. The layer keeps most calls on a few simple functions, which keeps the errors
  shorter. EnTT's headers are included as SYSTEM headers, so their warnings don't show up in our
  builds. `GetRegistry()` is the escape hatch for what the layer does not cover (groups, sorting,
  signals...).

```cpp
#include <Emerald/Emerald.h>

class GameScene : public Emerald::Scene {
    Emerald::World m_World;                       // a scene owns its world
    Emerald::CollisionSystem m_Collisions{32.0f}; // broadphase cell size
    Emerald::Camera2D m_Camera;
    Emerald::Sprite m_Coin; // e.g. atlas->Get("coin")

    void OnEnter() override {
        for (int i = 0; i < 1000; ++i) {
            Emerald::Entity e = m_World.Spawn();
            e.Add<Emerald::Transform>(Emerald::Transform{.Position = {f32(i), 100.0f}});
            e.Add<Emerald::Velocity>(Emerald::Velocity{.Linear = {0.0f, 50.0f}});
            e.Add<Emerald::Collider>(Emerald::Collider::MakeCircle(8.0f));
            e.Add<Emerald::SpriteRenderer>(Emerald::SpriteRenderer{.Image = m_Coin, .Layer = 1});
        }
    }
    void OnUpdate(f32 dt) override {
        Emerald::UpdateMovement(m_World, dt);
        Emerald::UpdateAnimation(m_World, dt);
        for (const Emerald::CollisionEvent& hit : m_Collisions.Update(m_World))
            hit.B.Destroy(); // deferred: safe while looping
        m_World.Each<Emerald::Transform>([](Emerald::Entity e, Emerald::Transform& t) {
            if (t.Position.y > 720.0f)
                e.Destroy();
        });
        m_World.Flush(); // destroy what was marked, once per frame
    }
    void OnRender2D(Emerald::Renderer2D& r) override
    {
        r.Begin(m_Camera);
        Emerald::DrawSprites(m_World, r);
        r.End();
    }
};
```

**`World`** (`Entity/World.h`) owns the `entt::registry`. It is not copyable.
- `Spawn()` returns an `Entity`.
- `Each<Ts...>(fn)` calls `fn(Entity, Ts&...)` for every entity that has all of `Ts`. Pass only
  components that hold data (a tag struct can't be passed by reference).
- `Destroy(e)` / `e.Destroy()` only *marks* the entity. Marked entities are skipped by `Each` and
  report `IsValid() == false`. `Flush()` destroys them (their components' destructors run there),
  so call it once per frame at a safe point, e.g. the end of `OnUpdate`. `Clear()` destroys
  everything now.
- Stats: `GetCount()`, `GetPendingDestroyCount()`, `GetSpawnedTotal()`, `GetDestroyedTotal()`.
- Inside `Each` you may spawn, add components and destroy anything. Don't remove components
  from *other* entities of the same view while iterating.

**`Entity`** is a small handle: the world plus EnTT's versioned id, so a handle to a destroyed
entity stays invalid even when its slot is reused. It has `IsValid()` / `explicit operator bool`,
`Add<T>(args...)` (adds or replaces; aggregates are brace-initialized), `Get<T>()`, `TryGet<T>()`
(nullptr if missing), `Has<T>()`, `Remove<T>()`, `Destroy()`, `GetWorld()` and `ToU32()` (for
logs and ImGui).

**Components** (`Entity/Components.h`):

| Component | Fields | Used by |
|---|---|---|
| `Transform` | the same `Transform2D` as `Renderer2D` (position, rotation, scale) | everything |
| `Velocity` | `Linear` (px/s), `Angular` (rad/s) | `UpdateMovement` |
| `SpriteRenderer` | `Image` (a `Sprite`, e.g. an atlas region), `Options` (tint, flip, origin...), `Layer` | `DrawSprites` |
| `Animator` | the [sprite animation](#sprite-animation-animator) player itself | `UpdateAnimation`, `DrawSprites` |
| `Collider` | `MakeCircle(radius)` or `MakeBox(halfSize)`, `Offset`, `Layer` / `Mask` bits | `CollisionSystem` |

There is **no parent / child transform**: an entity's transform is in world space. A hierarchy
costs an ordering pass every frame and is not needed yet. Store a parent `Entity` in your own
component if something has to follow another entity.

**Systems** (`Entity/Systems.h`): the scene calls them, in the order it wants.
- `UpdateMovement(world, dt)`: moves `Transform` by `Velocity`.
- `UpdateAnimation(world, dt)`: advances every `Animator`. Its `OnFinished` / loop callbacks may
  destroy the entity.
- `CollisionSystem(cellSize, wrapSize)`: `Update(world)` keeps a [`SpatialHash`](#collision-physics)
  of every `Transform` + `Collider` (updated in place each frame; destroyed entities and removed
  colliders leave it). It tests each candidate pair with the exact shape test from
  `Physics/Collision.h`, skipping pairs whose layer/mask bits don't match. It returns
  `CollisionEvent{A, B, Hit}` (normal from A to B, depth) sorted by id, so results are
  deterministic. `GetCandidateCount()` and `GetBroadphase()` are there for debugging. Colliders
  ignore the transform's rotation and scale.
- `DrawSprites(world, r, {.SortByY, .Visible})`: draws every `Transform` + `SpriteRenderer`, sorted by
  `Layer`, then by y (lower on screen is in front), then by id so equal keys don't flicker. If
  an `Animator` is present its current frame is drawn. `Visible` (e.g. the camera's world rect)
  culls off-screen entities. It returns the number of sprites drawn. Keep each layer on one
  texture to keep the draw calls low.

**Per app:** `Application::GetWorld()` is a world for apps that don't use scenes (the old
`GetRegistry()` returns its registry). It is cleared before the asset manager goes away.

**Numbers** (`EmeraldEntityBench`, release, 8-core Intel Xeon VM):

| Test | Time |
|---|---|
| spawn 100,000 entities with 4 components | 7.1 ms (14 M/s) |
| destroy 100,000 (deferred + `Flush`) | 4.8 ms (21 M/s) |
| churn: 10,000 alive, 1,000 spawned + 1,000 destroyed per frame, 1,000 frames | 0.13 ms per frame (7.6 M spawn+destroy/s); storages 2000 KiB before and after |
| systems, 10,000 moving circles (Transform, Velocity, Collider, SpriteRenderer) | movement 0.05 ms, collisions 2.5 ms (7,181 pairs → 5,670 contacts), sprites 1.06 ms |
| systems, 50,000 at the same density | movement 0.29 ms, collisions 15.3 ms, sprites 6.6 ms |

`EntityTests` checks that churn doesn't leak: 400,000 spawns over 200 frames, with every
component destructor counted and the storages not growing past the peak.

**Sandbox: "Entity swarm"** (`SwarmScene.h`; title menu, T from the tilemap room, or `--swarm`).
Thousands of walking heroes, spinning gems and coins bounce off the walls and off each other
through the broadphase, flash gold on contact, and fade in and out at the end of their lives.
The scene refills to the target count at up to 4000 entities/s. Under lavapipe (software
Vulkan), a release build runs 6,000 entities at about 55 fps.

| Key | Action |
|---|---|
| Space / gamepad South | burst of 1000 at the mouse |
| Backspace | destroy half |
| Up / Down | target -1000 / +1000 (default 3000) |
| M / Start | pause |
| T | camera demo |

The HUD shows entities, target, fps, spawned and destroyed per second, contacts and pairs, and
each system's time. With ImGui, the "Entity swarm" section shows the same, plus the broadphase
cells, a target slider, collision and y-sort toggles and burst / destroy buttons.

## Input (actions)

Games use **actions**: name what the player can do, bind keys and gamepad inputs to it, and query
the name.
`Application::GetInput()` returns the `Emerald::Input` (`include/Emerald/Input/Input.h`):

```cpp
// Once, e.g. in OnStart:
Emerald::Input& input = GetInput();
input.BindAction("Fire", {Key::Space});
input.BindAction("Thrust", {Key::W, Key::Up});     // any number of keys per action
input.BindAxis("Rotate", Key::A, Key::D);           // negative key, positive key
input.BindAxis("Rotate", Key::Left, Key::Right);    // more pairs for the same axis

// Gamepads (SDL's standard layout, so one set works for Xbox, PlayStation and Switch pads):
input.BindAction("Fire", {GamepadButton::South, GamepadButton::RightShoulder});
input.BindAction("Thrust", {GamepadButton::RightTrigger});  // trigger as a button
input.BindAxis("Rotate", GamepadAxis::LeftX);                // analog, deadzoned
input.BindAxis("Rotate", GamepadButton::DPadLeft, GamepadButton::DPadRight);

// Mouse buttons work like keys; the cursor position is in window coordinates.
input.BindAction("Fire", {MouseButton::Left});
const Vec2 cursor = input.GetMouse().GetPosition();

// In OnFixedUpdate / OnUpdate:
if (input.IsActionDown("Thrust"))    Accelerate(dt); // held
if (input.WasActionPressed("Fire"))  Shoot();        // once per press
if (input.WasActionReleased("Fire")) StopCharging();
angle += input.GetAxis("Rotate") * turnSpeed * dt;   // -1..+1 (keys: -1, 0 or +1)
input.Rumble(0.6f, 0.3f, 200);                        // low/high motor 0..1, milliseconds

// Remapping at runtime, e.g. from an options menu:
input.RebindAction("Fire", {Key::LeftCtrl});
input.RebindAxis("Rotate", Key::J, Key::L);
```

A second key held for an action does not count as a new press, and the action is only released
when its last key is. Opposite axis keys cancel out. Underneath, `Emerald::Keyboard`
(`Keyboard.h`, `input.GetKeyboard()`) tracks raw key state; `Key` values are physical keys (SDL
scancodes, so `Key::W` is the key left of `E` on any layout). `GetAxis` returns the binding with
the largest magnitude, so a full key press beats a half-tilted stick. Rebinding keys keeps the pad
bindings and vice versa (and the mouse buttons).

`Emerald::Mouse` (`Mouse.h`, `input.GetMouse()`) has the buttons (`Left`, `Middle`, `Right`, `X1`,
`X2`), `GetPosition()`, `HasMoved()` (this frame, e.g. to switch aiming from a stick back to the
mouse) and `IsInWindow()`. Clicks on the ImGui overlay don't reach the game, and losing focus
releases the buttons like keys.

### Gamepads

`Emerald::Gamepads` (`Gamepads.h`, `input.GetGamepads()`) uses SDL3's `SDL_Gamepad` API. Every
connected pad is opened (also when plugged in later), connects and disconnects are logged with
the pad's name and type, and all pads drive the same actions (single player: pick up any pad).

- **Buttons** are named by position (`South`, `East`, `West`, `North`, shoulders, d-pad, `Start`,
  ...). South is A on Xbox, Cross on PlayStation and **B** on Switch. For prompts use
  `gamepads.GetButtonLabel(GamepadButton::South)` (labels of the pad used last, e.g. "Cross") or
  `GetGamepadButtonLabel(button, type)`; `GetGamepadTypeName(type)` gives "PS4", "Switch Pro", ...
- **Virtual buttons** make analog inputs bindable to actions: `LeftTrigger`/`RightTrigger` and
  `LeftStickUp/Down/Left/Right` (same for the right stick) press at `ButtonThreshold` (0.5) and
  release a bit below it, with the same pressed/released edges as real buttons.
- **Axes** (`GamepadAxis`): sticks -1..+1 with **+Y down** (bind with scale -1 to make up
  positive), triggers 0..1. Values inside the deadzone read 0, the rest is rescaled so it still runs
  smoothly from 0 to 1. Settings via `gamepads.SetSettings(...)`: `StickDeadzone` (0.2),
  `RadialDeadzone` (true: on the stick's length, smooth diagonals; false: per axis),
  `TriggerDeadzone` (0.1), `ButtonThreshold` (0.5).
- **Rumble**: `input.Rumble(low, high, ms)` on all pads (ignored by pads without motors).
- The static SDL build forces `SDL_JOYSTICK`/`SDL_HIDAPI` on, and the HIDAPI hints for PS4, PS5
  and Switch are set explicitly, so those pads use SDL's own drivers (correct layout, labels and
  rumble, also over Bluetooth). If Steam is running, Steam Input may take over PlayStation/Switch
  pads and present them as Xbox controllers; that is a Steam setting, not an engine bug.

"Pressed/released" means *since the last frame* in `OnUpdate`/`OnRender*` and *since the last fixed
step* in `OnFixedUpdate`, so a tap in a frame that runs no fixed step (which happens at 144 fps with
120 Hz) still reaches the next step, exactly once; gamepad buttons follow the same rules. Key
repeats are ignored, held keys and pad buttons are released when the window loses focus, and with ImGui on, key presses are withheld from the game while an ImGui
text field is active.

## Audio

`include/Emerald/Audio/`: load or generate sounds, then play them through `Application::GetAudio()`.

```cpp
std::optional<Emerald::Sound> boom = Emerald::LoadSound("assets/boom.mp3"); // .wav or .mp3
Emerald::Audio& audio = GetAudio();
if (boom)
    audio.Play(*boom, {.Volume = 0.8f, .Pan = -0.3f});          // fire and forget

Emerald::VoiceHandle engine = audio.Play(hum, {.Volume = 0.5f, .Loop = true});
audio.SetVolume(engine, 0.2f);   // ramped, no click
audio.Stop(engine, 80.0f);       // fade out over 80 ms (default 10 ms)
audio.IsPlaying(engine);         // false once it has faded out
audio.SetMasterVolume(0.7f);
audio.SetMuted(!audio.IsMuted());
```

- **Loading**: `LoadSound(path)` picks the loader by extension (case-insensitive): `.wav` →
  `LoadWav` (`SDL_LoadWAV`), `.mp3` → `LoadMp3` ([dr_mp3](https://github.com/mackron/dr_libs),
  compiled once in `src/Vendor/dr_mp3.cpp`). MP3s are decoded to f32 at the file's own rate and
  channel count, then everything is converted with `SDL_ConvertAudioSamples` to the **mix format**
  `kMixSpec` (f32, stereo, 48 kHz). Failures return `std::nullopt` and are logged.
  `LoadMp3FromMemory` decodes embedded data, `MakeSound(samples, channels, rate)` converts
  generated samples. A `Sound` shares its (immutable) samples, so copies are cheap and a playing
  sound stays valid even if the game drops its `Sound`.
- **Synth** (`Synth.h`): `Generate(Tone)` makes sine, square/pulse (`Duty`), triangle, saw or
  noise with an optional exponential pitch sweep (`StartHz` → `EndHz`; for noise the frequency sets
  how bright it is) and vibrato (`VibratoHz`, `VibratoDepth`: e.g. 5 Hz, ±20% for a UFO warble); shape it with `ApplyDecay`, `ApplyAdsr`, `LowPass` (one-pole) and `MixInto`,
  then `ToSound`. Asteroids generates all its sounds this way.
- **Mixer** (`Mixer.h`, owned by `Audio`): 32 voices. `Play` returns a `VoiceHandle` that remembers
  the voice's generation, so a handle to a sound that has ended (and whose voice was reused) is a
  safe no-op. `PlayOptions`: `Volume`, `Pan` (-1..1), `Pitch` (speed, linear interpolation),
  `Loop`, `FadeInMs` (2 ms). Every gain change is ramped per sample (start, `Stop` fades,
  `SetVolume`, master volume, mute), so nothing clicks. The master output goes through a soft
  limiter (unchanged up to 0.8, then eases towards 1.0), so ten explosions at once get louder
  without harsh clipping. When all voices are busy, the oldest non-looping sound is cut off.
- **Threads**: SDL calls the mixer from its audio thread with the audio stream locked; every
  `Audio` method locks the same stream (for microseconds), so the game thread and the audio thread
  never touch mixer state at the same time. The audio thread never allocates or frees: finished
  sounds are released by `Audio::Update()` on the main thread (the Application calls it every
  frame). Call `Audio` from the main thread only.
- Without an audio device the app runs normally and `Play` returns an invalid handle (a warning is
  logged). `SDL_AUDIO_DRIVER=disk` writes the output to a raw file instead (handy for testing).

## Fixed-timestep update

`OnFixedUpdate(f32 dt)` runs at `ApplicationSpec::FixedUpdateRate` (default **120 Hz**) with a
constant `dt`, driven by `Emerald::FixedTimestep`: frame times accumulate (in integer nanoseconds)
and each whole step runs once, so the simulation is independent of the frame rate. A frame never
runs more than `MaxFixedStepsPerFrame` (default 8) steps; beyond that the extra time is dropped so a
slow frame or a breakpoint slows the game briefly instead of making it spiral. `OnUpdate(f32)` still
runs once per frame with the real frame time. `GetFixedDeltaSeconds()` and `GetFixedUpdateAlpha()`
(0..1, for interpolating when drawing) are available too, and `GetWindowSize()` /
`GetWindowSizeInPixels()` return the client area in window coordinates / pixels (`Vec2i`).

## Shaders

Shaders are written in HLSL (`*.vert.hlsl`, `*.frag.hlsl`, `*.comp.hlsl`; entry point `main`) and
compiled **at build time** by the [SDL_shadercross](https://github.com/libsdl-org/SDL_shadercross)
command line tool. The engine's own shaders live in `shaders/` (e.g. `Renderer2D`), the sandbox's in
`sandbox/shaders/`:

```
sandbox/shaders/Triangle.vert.hlsl ──shadercross──► build/<preset>/bin/shaders/Triangle.vert.spv   (Vulkan)
                                                               Triangle.vert.dxil  (D3D12)
                                                               Triangle.vert.msl   (Metal)
                                                               Triangle.vert.json  (reflection)
```

Register them for a target with `emerald_add_shaders(<target> [files...])` (see `cmake/Shaders.cmake`
and `sandbox/CMakeLists.txt`). **Every executable using Emerald must call it once, even with no files**:
it also compiles the engine's shaders (`Renderer2D`) and puts everything in a `shaders/` folder next to
that target's executable. Editing an `.hlsl` (or an `.hlsli` next to it) recompiles just that shader
on the next build. SDL GPU's HLSL binding rules: vertex-stage uniform buffers use `register(bN, space1)`,
fragment-stage uniform buffers `space3`, textures/samplers `space0` (vertex) / `space2` (fragment);
vertex inputs use `TEXCOORD<location>` semantics.

### Getting shadercross

SDL_shadercross has no releases yet, so Emerald pins commit
[`1ff05be`](https://github.com/libsdl-org/SDL_shadercross/commit/1ff05bec573988a98ef9e0260b4da44f512b8367).
By default it is **built from source** as a separate host-tool project (`tools/shadercross/`, driven by
`ExternalProject`) with `SDLSHADERCROSS_VENDORED=ON`, i.e. its own SPIRV-Cross, SPIRV-Tools and
libsdl-org's DirectXShaderCompiler fork come from git submodules and no system packages are needed.
It is always built in Release and lives in `build/_shadercross`, which every preset shares.

- **The first build is slow**: the submodule clone is large and DXC is an LLVM fork (≈1700 compile
  steps; about 10 minutes on 8 cores). Later builds and other presets reuse it.
- To skip that, point CMake at a prebuilt tool:
  `cmake --preset debug -DEMERALD_SHADERCROSS_EXECUTABLE=/path/to/shadercross`, or set
  `-DEMERALD_BUILD_SHADERCROSS=OFF` to use a `shadercross` found on `PATH`.
- A prebuilt tool may lack DXC. Emerald probes it at configure time and, if it cannot emit DXIL, drops
  DXIL with a warning; the build still succeeds and, because `emerald_add_shaders()` passes the
  generated formats to the code (`EMERALD_SHADER_FORMATS` → `ApplicationSpec::ShaderFormats`), SDL
  simply won't pick Direct3D 12 (on Windows it uses Vulkan instead, if available). You can also restrict formats yourself with
  `-DEMERALD_SHADER_FORMATS="SPIRV;MSL"`.
- With the vendored build **all three formats (SPIR-V, DXIL, MSL) are produced on Linux**; DXC and its
  `libdxil` validator are built from source, so the DXIL is signed.
- **GCC 14 miscompiles DXC**: the tool then builds and handles simple shaders, but every texture
  sample fails DXIL validation (*"sample_\* instructions require resource to be declared to return
  UNORM, SNORM or FLOAT"*). So when the project compiler is GCC 14 or newer, Emerald builds the tool
  with Clang if `clang`/`clang++` are installed (and warns otherwise). The smoke test samples a
  texture, so a broken tool is caught right after it is built. On an older Linux checkout with a
  GCC-built tool: install clang, delete `build/_shadercross` and rebuild. (Only seen with GCC; the
  Clang build and the official DXC releases are fine.)
- The built `dxcompiler`/`dxil` libraries are copied next to the executable (`build/_shadercross/bin/`)
  and the tool is smoke-tested (HLSL → SPIR-V and DXIL) right after it is built. This matters on
  Windows, which has no RPATH: without the copies `shadercross.exe` picks up another
  `dxcompiler.dll` (Windows SDK / System32) that lacks SPIR-V codegen, and since HLSL → DXIL
  round-trips through SPIR-V every DXIL compile fails with *"SPIR-V CodeGen not available"*.
  If you hit that with an older checkout: pull, delete `build/_shadercross`, re-run `cmake --preset …`
  and build.
- Prebuilt Windows binaries: SDL_shadercross's GitHub Actions "Build" workflow uploads a
  `SDL3_shadercross-*-windows-VC-x64` artifact (shadercross.exe with SDL3.dll, dxcompiler.dll and
  dxil.dll beside it; downloading needs a GitHub login, artifacts expire after ~90 days). Unzip it and
  pass `-DEMERALD_SHADERCROSS_EXECUTABLE=<dir>/bin/shadercross.exe`.

## Project layout

```
include/Emerald/   Public engine headers (Core/, Input/, Audio/, Renderer/, Particles/, Physics/, Tilemap/, Scene/, Entity/, Assets/, Tween/, Math/, Memory/)
src/               Engine implementation
shaders/           The engine's HLSL shaders (Renderer2D lines, Sprite, the CRT post-process; compiled at build time for every app)
sandbox/           Example application (src/, its own shaders/, assets/ for the demo font, hero sheet and tilemap room)
tests/             Unit tests (ctest)
bench/             Math, particle, collision and entity micro-benchmarks (EMERALD_BUILD_BENCH)
cmake/             Dependency setup (FetchContent) and shader compilation (Shaders.cmake)
tools/shadercross/ Host-tool project that builds SDL_shadercross
tools/sprites/     Script that generates the sandbox's pixel-art hero sheet
tools/tilemaps/    Scripts that generate the sandbox's tileset art, Tiled tilesets, sample room (+ the 500 x 500 benchmark map) and platformer level
```

## Using Emerald in your own project

```cmake
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin) # optional: exe + shaders in bin/

include(FetchContent)
FetchContent_Declare(Emerald
    GIT_REPOSITORY https://github.com/Ridejock/Emerald.git
    GIT_TAG        <commit hash>)                          # pin a commit
FetchContent_MakeAvailable(Emerald)                        # or add_subdirectory(Emerald)

add_executable(MyGame src/Main.cpp)
target_link_libraries(MyGame PRIVATE Emerald::Emerald)
emerald_add_shaders(MyGame)                                # engine shaders (+ your own .hlsl files)
```

As a subproject Emerald does not build its sandbox and tests (`EMERALD_BUILD_SANDBOX`/`_TESTS`
default to on only when Emerald is the top-level project), keeps the parent's output directories,
and builds shadercross in `<your project>/build/_shadercross` (shared by your presets: since each
build dir has its own fetched copy of Emerald, the tool's two CMake files are copied to
`build/_shadercross-src` so they always have the same path). Override the location with
`EMERALD_SHADERCROSS_BUILD_DIR`. To work on a local Emerald checkout instead of the pinned commit, configure with
`-DFETCHCONTENT_SOURCE_DIR_EMERALD=/path/to/Emerald`.

```cpp
#include <Emerald/Emerald.h>

class MyGame : public Emerald::Application {
    void OnFixedUpdate(f32 dt) override { /* game logic at 120 Hz */ }
    void OnRender2D(Emerald::Renderer2D& r) override { /* r.Begin(...); r.DrawLine(...); r.End(); */ }
};

int main() { return MyGame{}.Run(); }
```

## License

Emerald is released under the [MIT License](LICENSE). Third-party code and the sandbox's demo font
are listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
