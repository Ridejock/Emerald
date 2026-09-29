# Emerald

Emerald is a small, modern C++20 game engine built on [SDL3](https://github.com/libsdl-org/SDL).
It is split into:

- **`Emerald::Emerald`** – the engine library (logging, window, SDL GPU renderer, application loop, image loading,
  thread pool, `std::pmr` memory helpers).
- **`Emerald::Math`** – a header-only math library (vectors, `Mat4`, optional SSE), included by the engine.
- **`sandbox/`** – a minimal example app that links the engine and draws a rotating vertex-colored triangle
  in pixel space with HLSL shaders through SDL GPU.
- **`tests/`** – small unit-test executables run with `ctest`.

All dependencies are fetched automatically with CMake `FetchContent` and pinned to specific versions.

## Dependencies

| Library | Version | Notes |
|---|---|---|
| [SDL3](https://github.com/libsdl-org/SDL) | `release-3.4.16` | Built statically |
| [spdlog](https://github.com/gabime/spdlog) | `v1.17.0` | Uses bundled fmt |
| [stb](https://github.com/nothings/stb) | commit `2c980bb` | Header-only, exposed as `Emerald::stb` (INTERFACE) |
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
  found with `SDL_GetBasePath()` so it does not depend on the working directory. The `logs/` folder is
  created if needed.
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
  `mul(Transform, float4(position, 1))` (see `shaders/Triangle.vert.hlsl`).
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
| `MemoryTests` | frame arena reset/alignment/overflow, pmr containers use their resource, tracking counts, pools (incl. the synchronized pool from many threads) |

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

### Unsupported GPUs

If no backend is available (no Vulkan / D3D12 / Metal capable driver), the engine logs the
`SDL_GetError()` reason, shows a message box explaining that a Vulkan, Direct3D 12 or Metal capable
GPU driver is required, and `Run()` returns exit code `1`. You can try this path with
`SDL_GPU_DRIVER=invalid ./build/debug/bin/Sandbox`.

## Shaders

Shaders are written in HLSL in `shaders/` (`*.vert.hlsl`, `*.frag.hlsl`, `*.comp.hlsl`; entry point
`main`) and compiled **at build time** by the [SDL_shadercross](https://github.com/libsdl-org/SDL_shadercross)
command line tool:

```
shaders/Triangle.vert.hlsl ──shadercross──► build/<preset>/bin/shaders/Triangle.vert.spv   (Vulkan)
                                                                       Triangle.vert.dxil  (D3D12)
                                                                       Triangle.vert.msl   (Metal)
                                                                       Triangle.vert.json  (reflection)
```

Register them for a target with `emerald_add_shaders(<target> <files...>)` (see `cmake/Shaders.cmake`
and `sandbox/CMakeLists.txt`). Editing an `.hlsl` (or an `.hlsli` next to it) recompiles just that shader
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
include/Emerald/   Public engine headers (Core/, Renderer/, Assets/, Math/, Memory/)
src/               Engine implementation
shaders/           HLSL shader sources (compiled at build time)
sandbox/           Example application
tests/             Unit tests (ctest)
bench/             Math micro-benchmark (EMERALD_BUILD_BENCH)
cmake/             Dependency setup (FetchContent) and shader compilation (Shaders.cmake)
tools/shadercross/ Host-tool project that builds SDL_shadercross
```

## Using Emerald in your own project

```cmake
add_subdirectory(Emerald)          # or FetchContent
target_link_libraries(MyGame PRIVATE Emerald::Emerald)
```

```cpp
#include <Emerald/Emerald.h>

class MyGame : public Emerald::Application {
    void OnUpdate(f32 dt) override { /* ... */ }
    void OnRender(SDL_GPURenderPass* pass) override { /* bind pipeline, draw ... */ }
};

int main() { return MyGame{}.Run(); }
```

## License

Emerald is released under the [MIT License](LICENSE).
