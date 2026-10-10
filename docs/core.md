# Core

Logging, threads, memory, the math library and the fixed-timestep loop.

[Back to the README](../README.md)

- [Logging](#logging)
- [Threads](#threads)
- [Memory (std::pmr)](#memory-stdpmr)
- [Math library](#math-library)
- [Fixed-timestep update](#fixed-timestep-update)

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

## Fixed-timestep update

`OnFixedUpdate(f32 dt)` runs at `ApplicationSpec::FixedUpdateRate` (default **120 Hz**) with a
constant `dt`, driven by `Emerald::FixedTimestep`: frame times accumulate (in integer nanoseconds)
and each whole step runs once, so the simulation is independent of the frame rate. A frame never
runs more than `MaxFixedStepsPerFrame` (default 8) steps; beyond that the extra time is dropped so a
slow frame or a breakpoint slows the game briefly instead of making it spiral. `OnUpdate(f32)` still
runs once per frame with the real frame time. `GetFixedDeltaSeconds()` and `GetFixedUpdateAlpha()`
(0..1, for interpolating when drawing) are available too, and `GetWindowSize()` /
`GetWindowSizeInPixels()` return the client area in window coordinates / pixels (`Vec2i`).
