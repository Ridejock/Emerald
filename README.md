# Emerald

[![Windows](https://github.com/Ridejock/Emerald/actions/workflows/windows.yml/badge.svg)](https://github.com/Ridejock/Emerald/actions/workflows/windows.yml)
[![Linux](https://github.com/Ridejock/Emerald/actions/workflows/linux.yml/badge.svg)](https://github.com/Ridejock/Emerald/actions/workflows/linux.yml)
[![macOS](https://github.com/Ridejock/Emerald/actions/workflows/macos.yml/badge.svg)](https://github.com/Ridejock/Emerald/actions/workflows/macos.yml)
[![Sanitizers](https://github.com/Ridejock/Emerald/actions/workflows/sanitizers.yml/badge.svg)](https://github.com/Ridejock/Emerald/actions/workflows/sanitizers.yml)
[![clang-tidy](https://github.com/Ridejock/Emerald/actions/workflows/clang-tidy.yml/badge.svg)](https://github.com/Ridejock/Emerald/actions/workflows/clang-tidy.yml)

Emerald is a 2D game engine written in C++20. It draws through [SDL3](https://github.com/libsdl-org/SDL) and its GPU API (SDL_GPU).

SDL3 stays inside Emerald. The engine fetches it, builds it, and links it privately. Public headers do not include SDL. The GPU objects a game sees (`GpuDevice`, `GpuRenderPass`, and the rest in `Gpu.h`) are Emerald types. A game includes `<Emerald/Emerald.h>` and links `Emerald::Emerald`. It does not call SDL.

You get a window, a batched 2D renderer, sprites and animation, a camera, tilemaps, entities, platformer physics, scenes, UI, dialogue, and audio. Third-party libraries are fetched with CMake `FetchContent` and pinned.

- **`Emerald::Emerald`**: the engine library.
- **`Emerald::Math`**: a header-only math library (vectors, `Mat4`, optional SSE on x86/x64).
- **`sandbox/`**: an example app on the scene stack (title, camera demo, tilemap room, entity swarm, platformer, pause, options, lighting, dialogue, particles, pop-ups).
- **`tests/`**: unit tests, run with `ctest`.

## Features

| Area | What is there | Docs |
|---|---|---|
| Rendering | SDL GPU renderer, batched lines and textured sprites, texture atlases, 2D camera, TTF text and a built-in debug font, 2D lighting, post-effect chain (CRT and a tint grade), HLSL shaders compiled at build time | [Rendering](docs/rendering.md) |
| Animation and effects | Sprite animation, tweens with easing, timers, particles with effect files | [Animation and effects](docs/animation-and-effects.md) |
| Physics and world | Collision shapes and a spatial hash, Tiled tilemaps, platformer character physics | [Physics and world](docs/physics.md) |
| Gameplay | Scene stack with transitions, entities and components (EnTT), action input (keyboard, mouse, and gamepad), UI widgets including pop-ups, branching dialogue | [Gameplay systems](docs/gameplay.md) |
| Assets, audio, saves | Asset manager (hot reload in debug), APAK v1 packs, audio (WAV, MP3, OGG, streaming music), versioned saves | [Assets, audio and saves](docs/assets-audio-saves.md) |
| Core | Logging, thread pool, `std::pmr` memory helpers, math library, fixed-timestep update (120 Hz by default) | [Core](docs/core.md) |
| Tooling | ImGui editor tools (optional), input record/replay, asset checker, text-to-Tiled-map tool, in-game profiler, Tracy (`profile` preset) | [Tools](docs/tools.md), [Testing and profiling](docs/testing-and-profiling.md) |

## Platforms

| Platform | GPU backend |
|---|---|
| Windows | Direct3D 12 or Vulkan |
| Linux | Vulkan |
| macOS | Metal |

Shaders are HLSL, compiled at build time to SPIR-V (Vulkan), DXIL (Direct3D 12), and MSL (Metal). There is no OpenGL backend, and none is planned.

A browser build is not available. The pinned SDL 3.4.16 GPU API has no WebGPU backend. See [Emscripten](docs/emscripten.md).

Linux paths, the x64 release archive, and Steam Deck controls are in [Platforms](docs/platforms.md). Deck support has not been tried on a real Deck.

## Continuous integration

GitHub Actions runs on every push to `main` and on pull requests. Windows, Linux, macOS, and the sanitizer builds treat warnings in Emerald's own code as errors (`EMERALD_WARNINGS_AS_ERRORS`).

| Workflow | What runs |
|---|---|
| [Windows](https://github.com/Ridejock/Emerald/actions/workflows/windows.yml) | MSVC, x64: `debug`, `debug-asan`, `debug-full` (ImGui), `release`, `profile` (Tracy) |
| [Linux](https://github.com/Ridejock/Emerald/actions/workflows/linux.yml) | Ubuntu 24.04, GCC and Clang: `debug`, `release`, `debug-ubsan`, `debug-tsan`, `profile`. GCC also builds the `linux-release` archive |
| [macOS](https://github.com/Ridejock/Emerald/actions/workflows/macos.yml) | Apple Silicon (`macos-15`): `debug` and `release` |
| [Sanitizers](https://github.com/Ridejock/Emerald/actions/workflows/sanitizers.yml) | Linux Clang: `debug-asan-ubsan` (AddressSanitizer and UndefinedBehaviorSanitizer together) and `debug-tsan` |
| [clang-tidy](https://github.com/Ridejock/Emerald/actions/workflows/clang-tidy.yml) | Report only. Checks `.cpp` files in `src/` (not `src/Vendor`), `tests/`, `sandbox/src/`, and `bench/`. Downloaded dependencies are left out. The log is uploaded; findings do not fail the job |

Debug and release run on Windows, Linux, and macOS. The `profile` preset (Tracy, optimized with debug info) runs on Windows and Linux.

GPU checks:

- **SwiftShader.** Linux and Windows run `RenderSmoke`: one Vulkan frame, compared with `tests/data/render_smoke.png`. The test is not part of a ThreadSanitizer build. With no Vulkan device it exits 77 and CTest skips it. Those jobs require SwiftShader, so a missing device fails the run. The Linux GCC debug job also runs the sandbox for a few frames on SwiftShader.
- **llvmpipe.** The sanitizer jobs run the sandbox for a few frames on lavapipe (Mesa's llvmpipe Vulkan driver).
- **Metal.** The macOS debug job runs the sandbox for a few frames on Metal. If that runner cannot open a window or a Metal device, the step skips and the job stays green.

The `debug-tsan` test preset points ThreadSanitizer at [cmake/tsan.supp](cmake/tsan.supp). That file suppresses third-party reports only. Emerald frames are not listed.

Tests, the sanitizer presets, and `RenderSmoke` are described in [Testing and profiling](docs/testing-and-profiling.md).

## Quick start

Requirements: CMake 3.24 or newer, a C++20 compiler (GCC 11+, Clang 14+, MSVC 2022, Apple Clang 15+), Ninja, and Git. Linux needs the system libraries SDL builds against. On Windows, use a Visual Studio developer prompt so `cl` and `ninja` are on `PATH`. The package lists are in [Building and running](docs/building.md).

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
./build/debug/bin/Sandbox        # Sandbox.exe on Windows
```

`ctest --preset` prints failures and allows each test ten minutes. `RenderSmoke` needs a Vulkan device. Without one it exits 77 and CTest skips it.

Other presets, from [CMakePresets.json](CMakePresets.json): `release`, `debug-full` (ImGui), `profile` (Tracy), `debug-asan`, `debug-ubsan`, `debug-asan-ubsan`, `debug-tsan`, and on Linux `linux-release`. What each one sets, and how to run the sandbox (`--frames`, `--screenshot`, `--gpu`, scene switches), is in [Building and running](docs/building.md#running).

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

As a subproject, Emerald does not build the sandbox or the tests (`EMERALD_BUILD_SANDBOX` and `EMERALD_BUILD_TESTS` default to on only when Emerald is the top-level project). It keeps the parent's output directories. The shader compiler is built once per machine, in a per-user cache that presets and projects share ([The shader compiler](docs/building.md#the-shader-compiler)). To use a local checkout instead of the pinned commit, configure with `-DFETCHCONTENT_SOURCE_DIR_EMERALD=/path/to/Emerald`.

```cpp
#include <Emerald/Emerald.h>

class MyGame : public Emerald::Application {
    void OnFixedUpdate(f32 dt) override { /* game logic at 120 Hz */ }
    void OnRender2D(Emerald::Renderer2D& r) override { /* r.Begin(...); r.DrawLine(...); r.End(); */ }
};

int main() { return MyGame{}.Run(); }
```

## Roadmap

What is still to come is in [ROADMAP.md](ROADMAP.md). Planned items are marked planned. The file does not give dates.

Phase work also has a GitHub issue. The lists after the phases (core and rendering, tooling and shipping, and later) are not issues yet.

## Documentation

| File | Contents |
|---|---|
| [docs/building.md](docs/building.md) | Dependencies, build options, per-platform setup, presets, the Linux release package, sandbox command line |
| [docs/releasing.md](docs/releasing.md) | Reusable game CI: build, test, package, and publish a tag to itch.io with butler |
| [docs/platforms.md](docs/platforms.md) | Linux paths (XDG), borderless fullscreen, Steam Deck controls and Steam Input. Deck support is untested on real hardware |
| [docs/emscripten.md](docs/emscripten.md) | Emscripten spike: SDL 3.4.16 GPU has no WebGPU backend, so a browser build is not shippable |
| [docs/core.md](docs/core.md) | Logging, threads, memory, math library, fixed-timestep loop |
| [docs/rendering.md](docs/rendering.md) | Renderer, 2D shapes, camera, textures and sprites, text, CRT, lighting, shaders |
| [docs/animation-and-effects.md](docs/animation-and-effects.md) | Sprite animation, tweens and timers, particles |
| [docs/physics.md](docs/physics.md) | Collision, tilemaps, platformer physics |
| [docs/gameplay.md](docs/gameplay.md) | Scenes, entities, input, UI widgets, dialogue |
| [docs/assets-audio-saves.md](docs/assets-audio-saves.md) | Assets, audio, saves |
| [docs/testing-and-profiling.md](docs/testing-and-profiling.md) | Tests, `RenderSmoke`, sanitizers, benchmarks, the in-game profiler, Tracy |
| [docs/tools.md](docs/tools.md) | Editor tools, dev tools (command line, record/replay, asset checker, text maps) |

## Project layout

```
docs/              Documentation (one file per topic, see above)
include/Emerald/   Public headers (Assets/, Audio/, Core/, Dialogue/, Editor/, Entity/, Input/, IO/, Math/, Memory/, Particles/, Physics/, Renderer/, Save/, Scene/, Tilemap/, Tween/, UI/)
src/               Engine implementation. SDL translation headers live in src/Platform/ and are not on the public include path
shaders/           Engine HLSL (lines, sprites, lit sprites, tint, CRT), compiled at build time for every app
sandbox/           Example application (src/, shaders/, assets/)
tests/             Unit tests (ctest), including RenderSmoke and tests/data/render_smoke.png
bench/             Math, particle, collision, and entity micro-benchmarks (EMERALD_BUILD_BENCH)
cmake/             FetchContent (Dependencies.cmake), shader compilation (Shaders.cmake), sanitizer flags (Sanitizers.cmake), TSan suppressions (tsan.supp), Linux archive (LinuxPackage.cmake, cmake/linux/)
tools/ci/          CI scripts (game matrix, SwiftShader setup, GPU smoke, clang-tidy, itch publish)
tools/shadercross/ Host-tool project that builds SDL_shadercross
tools/sprites/     Script that builds the sandbox hero sheet
tools/check_assets.py  Asset checker (palette, atlases, Tiled maps) for CI
tools/tests/       Unit tests of the Python tools
tools/tilemaps/    textmap.py (text drawings to Tiled maps) and the scripts that build the sandbox maps
```

## License

Emerald is released under the [MIT License](LICENSE). Third-party code is listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
