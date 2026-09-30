# Emerald

Emerald is a small, modern C++20 game engine built on [SDL3](https://github.com/libsdl-org/SDL).
It is split into:

- **`Emerald::Emerald`** – the engine library (logging, window, SDL GPU renderer, batched 2D renderer for lines and
  textured sprites, textures and texture atlases, keyboard input, application loop with a fixed-timestep update, image loading, thread pool, `std::pmr`
  memory helpers).
- **`Emerald::Math`** – a header-only math library (vectors, `Mat4`, optional SSE), included by the engine.
- **`sandbox/`** – a minimal example app that links the engine and draws a rotating vertex-colored triangle
  in pixel space with its own HLSL shaders through SDL GPU, plus 2D line shapes, sprites from a
  procedurally generated atlas (scaled, rotated, flipped, tinted, and layered with lines), and an arrow driven by
  keyboard or gamepad (with ImGui on, the panel lists connected pads and their live stick values).
- **`tests/`** – small unit-test executables run with `ctest`.

All dependencies are fetched automatically with CMake `FetchContent` and pinned to specific versions.

## Dependencies

| Library | Version | Notes |
|---|---|---|
| [SDL3](https://github.com/libsdl-org/SDL) | `release-3.4.16` | Built statically |
| [spdlog](https://github.com/gabime/spdlog) | `v1.17.0` | Uses bundled fmt |
| [stb](https://github.com/nothings/stb) | commit `2c980bb` | Header-only (stb_image, stb_image_write, stb_truetype + stb_rect_pack for fonts), exposed as `Emerald::stb` (INTERFACE) |
| [nlohmann/json](https://github.com/nlohmann/json) | `v3.12.0` | Texture atlas JSON; release tarball (SHA-256 pinned), linked PRIVATE |
| [dr_libs](https://github.com/mackron/dr_libs) | commit `dfe8377` | Only `dr_mp3.h` (MP3 decoding); header-only, public domain / MIT-0 |
| [Dear ImGui](https://github.com/ocornut/imgui) | `v1.92.9b-docking` | Optional, SDL3 + SDLGPU3 backends |
| [EnTT](https://github.com/skypjack/entt) | `v3.16.0` | Optional |
| [SDL_shadercross](https://github.com/libsdl-org/SDL_shadercross) | commit `1ff05be` | Build-time host tool (HLSL → SPIR-V/DXIL/MSL), built from source with vendored DXC + SPIRV-Cross |

## Options

| Option | Default | Description |
|---|---|---|
| `EMERALD_BUILD_SANDBOX` | `ON` | Build the sandbox example executable |
| `EMERALD_USE_IMGUI` | `OFF` | Fetch Dear ImGui (docking) and integrate it into the app loop |
| `EMERALD_USE_ENTT` | `OFF` | Fetch EnTT and give each `Application` an `entt::registry` |
| `EMERALD_BUILD_SHADERCROSS` | `ON` | Build the `shadercross` tool from source if `EMERALD_SHADERCROSS_EXECUTABLE` is empty |
| `EMERALD_SHADERCROSS_EXECUTABLE` | *(empty)* | Use this prebuilt `shadercross` instead of building it |
| `EMERALD_SHADERCROSS_BUILD_DIR` | `build/_shadercross` | Where the tool is built; shared by all presets |
| `EMERALD_SHADER_FORMATS` | `SPIRV;DXIL;MSL` | Shader formats generated at build time |
| `EMERALD_MATH_SIMD` | `ON` | Use the SSE code paths of the math library on x86/x64 (see [Math library](#math-library)) |
| `EMERALD_BUILD_TESTS` | `ON` | Build the unit tests and register them with CTest |
| `EMERALD_BUILD_BENCH` | `OFF` | Build `EmeraldMathBench`, a scalar-vs-SSE micro-benchmark |

The engine exports `EMERALD_WITH_IMGUI` / `EMERALD_WITH_ENTT` / `EMERALD_MATH_SIMD` (0 or 1) as public
compile definitions.

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
| `debug-full` | Debug build with `EMERALD_USE_IMGUI=ON` and `EMERALD_USE_ENTT=ON` |

Options can also be passed directly, e.g. `cmake --preset release -DEMERALD_USE_IMGUI=ON`.

## Running

```sh
./build/debug/bin/Sandbox                                  # run until the window is closed or Esc is pressed
./build/debug/bin/Sandbox --frames 120                     # quit automatically after 120 frames
./build/debug/bin/Sandbox --frames 60 --screenshot out.png # save the last frame as a PNG (GPU readback)
SDL_GPU_DRIVER=vulkan ./build/debug/bin/Sandbox            # force a backend (vulkan, direct3d12, metal)
```

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
| `Renderer2DTests` | `Renderer2D` batching and shape generation on the CPU (no GPU), `Transform2D`, color packing; sprite quads (UVs, rotation, origin, flips, pixel snap), draw order across lines/sprites/texture switches and blend modes, atlas JSON parsing |
| `ParticleTests` | particle spawning (shapes, ranges, base velocity), capacity limit, drag/gravity step, swap-remove, continuous rate, color/size fade when drawing, scalar and SSE updates agreeing over 240 steps |

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
EndFrame                 submit + present
```

### Unsupported GPUs

If no backend is available (no Vulkan / D3D12 / Metal capable driver), the engine logs the
`SDL_GetError()` reason, shows a message box explaining that a Vulkan, Direct3D 12 or Metal capable
GPU driver is required, and `Run()` returns exit code `1`. You can try this path with
`SDL_GPU_DRIVER=invalid ./build/debug/bin/Sandbox`.

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
    r.DrawRect({20, 20}, {100, 50}, color);
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

r.DrawText(*ui, "Score: 1200\nLives: 3", {20, 20}, {1, 1, 1, 1});             // top-left at (20, 20)
r.DrawText(*pixel, "GAME OVER", {640, 300}, {1, 0.3f, 0.3f, 1}, 3.0f,        // 3x, centered on x
           Emerald::TextAlign::Center);
const Vec2 size = ui->MeasureText("Score: 1200");                           // width, height in px
```

- `DrawText(font, text, position, color, scale = 1, align = Left)`: UTF-8 text, `'\n'` starts a
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
bindings and vice versa.

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
include/Emerald/   Public engine headers (Core/, Input/, Audio/, Renderer/, Particles/, Assets/, Math/, Memory/)
src/               Engine implementation
shaders/           The engine's HLSL shaders (Renderer2D lines, Sprite; compiled at build time for every app)
sandbox/           Example application (src/, its own shaders/, assets/ for the demo font)
tests/             Unit tests (ctest)
bench/             Math and particle micro-benchmarks (EMERALD_BUILD_BENCH)
cmake/             Dependency setup (FetchContent) and shader compilation (Shaders.cmake)
tools/shadercross/ Host-tool project that builds SDL_shadercross
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
