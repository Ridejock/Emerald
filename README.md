# Emerald

[![Windows](https://github.com/Ridejock/Emerald/actions/workflows/windows.yml/badge.svg)](https://github.com/Ridejock/Emerald/actions/workflows/windows.yml)

Emerald is a small, modern C++20 game engine built on [SDL3](https://github.com/libsdl-org/SDL) and its GPU API.
It gives you a window, a batched 2D renderer, sprites and animation, a camera, tilemaps, entities,
platformer physics, scenes, UI, dialogue and audio. All dependencies are fetched with CMake
`FetchContent` and pinned to specific versions.

- **`Emerald::Emerald`**: the engine library.
- **`Emerald::Math`**: a header-only math library (vectors, `Mat4`, optional SSE), included by the engine.
- **`sandbox/`**: an example app, organised as scenes on the scene stack (title, camera demo, tilemap room,
  entity swarm, platformer, pause, options, lighting, dialogue, particles). It is also the quickest way to see the engine run.
- **`tests/`**: unit-test executables run with `ctest`.

What is planned next is in [ROADMAP.md](ROADMAP.md), with one GitHub issue per feature.

## Features

| Area | What is there | Docs |
|---|---|---|
| Rendering | SDL GPU renderer, batched lines and textured sprites, texture atlases, 2D camera, text, 2D lighting, post-effect chain (CRT), HLSL shaders compiled at build time | [Rendering](docs/rendering.md) |
| Animation and effects | Sprite animation, tweens with easing, timers, particles with effect files | [Animation and effects](docs/animation-and-effects.md) |
| Physics and world | Collision shapes and a spatial hash, Tiled tilemaps, platformer character physics | [Physics and world](docs/physics.md) |
| Gameplay | Scene stack with transitions, entities and components (EnTT), action-based input (keyboard and gamepad), UI widgets, branching dialogue | [Gameplay systems](docs/gameplay.md) |
| Assets, audio, saves | Asset manager, audio, save files | [Assets, audio and saves](docs/assets-audio-saves.md) |
| Core | Logging, thread pool, `std::pmr` memory helpers, math library, application loop with a fixed-timestep update | [Core](docs/core.md) |
| Tooling | ImGui editor tools, input record/replay, asset checker, text-to-Tiled-map tool, Tracy profiling, AddressSanitizer | [Tools](docs/tools.md), [Testing and profiling](docs/testing-and-profiling.md) |

## Quick start

Requirements: CMake 3.24+, a C++20 compiler (GCC 11+, Clang 14+, MSVC 2022, Apple Clang 15+), Ninja and Git.

```sh
cmake --preset debug
cmake --build --preset debug
./build/debug/bin/Sandbox        # Sandbox.exe on Windows
```

Per-platform notes (Linux needs SDL3's development packages; on Windows use a Developer PowerShell),
the other presets (`release`, `debug-full` with ImGui, `profile`, `debug-asan`), build options and the
dependency list are in [Building and running](docs/building.md).

A few sandbox flags:

```sh
./build/debug/bin/Sandbox --frames 120                     # quit automatically after 120 frames
./build/debug/bin/Sandbox --frames 60 --screenshot out.png # save the last frame as a PNG
./build/debug/bin/Sandbox --demo                           # skip the title: start in the camera demo
```

The renderer needs a real GPU backend (Vulkan, Direct3D 12 or Metal), so a display is required. The full flag
list, how to choose the backend, and how to run on a machine without a GPU are in
[Building and running](docs/building.md#running).

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
and builds shadercross once per machine in a per-user cache folder that every preset and project
shares (see [The shader compiler](docs/building.md#the-shader-compiler)). To work on a local Emerald checkout instead of the pinned commit, configure with
`-DFETCHCONTENT_SOURCE_DIR_EMERALD=/path/to/Emerald`.

```cpp
#include <Emerald/Emerald.h>

class MyGame : public Emerald::Application {
    void OnFixedUpdate(f32 dt) override { /* game logic at 120 Hz */ }
    void OnRender2D(Emerald::Renderer2D& r) override { /* r.Begin(...); r.DrawLine(...); r.End(); */ }
};

int main() { return MyGame{}.Run(); }
```

## Documentation

| File | Contents |
|---|---|
| [docs/building.md](docs/building.md) | Dependencies, build options, per-platform setup, presets, sandbox command line |
| [docs/core.md](docs/core.md) | Logging, threads, memory, math library, fixed-timestep loop |
| [docs/rendering.md](docs/rendering.md) | Renderer, 2D shapes, camera, textures and sprites, text, CRT, lighting, shaders |
| [docs/animation-and-effects.md](docs/animation-and-effects.md) | Sprite animation, tweens and timers, particles |
| [docs/physics.md](docs/physics.md) | Collision, tilemaps, platformer physics |
| [docs/gameplay.md](docs/gameplay.md) | Scenes, entities, input, UI widgets, dialogue |
| [docs/assets-audio-saves.md](docs/assets-audio-saves.md) | Assets, audio, saves |
| [docs/testing-and-profiling.md](docs/testing-and-profiling.md) | Tests, ThreadSanitizer, benchmarks, Tracy, AddressSanitizer |
| [docs/tools.md](docs/tools.md) | Editor tools, dev tools (command line, record/replay, asset checker, text maps) |

## Project layout

```
docs/              Documentation (one file per topic, see above)
include/Emerald/   Public engine headers (Core/, Input/, Audio/, Renderer/, Particles/, Physics/, Tilemap/, Scene/, Entity/, Assets/, Tween/, UI/, Dialogue/, Save/, Editor/, Math/, Memory/)
src/               Engine implementation
shaders/           The engine's HLSL shaders (Renderer2D lines, Sprite, lit sprites, tint, the CRT post-process; compiled at build time for every app)
sandbox/           Example application (src/, its own shaders/, assets/ for the demo font, hero sheet, tilemap room, dialogue deck, particle effects and audio)
tests/             Unit tests (ctest)
bench/             Math, particle, collision and entity micro-benchmarks (EMERALD_BUILD_BENCH)
cmake/             Dependency setup (FetchContent), shader compilation (Shaders.cmake) and AddressSanitizer flags (Sanitizers.cmake)
tools/shadercross/ Host-tool project that builds SDL_shadercross
tools/sprites/     Script that generates the sandbox's pixel-art hero sheet
tools/check_assets.py  Asset checker (palette, atlases, Tiled maps) for CI
tools/tests/       Unit tests of the Python tools
tools/tilemaps/    textmap.py (text drawings to Tiled maps), and scripts that generate the sandbox's tileset art, Tiled tilesets, sample room (+ the 500 x 500 benchmark map) and platformer level
```

## License

Emerald is released under the [MIT License](LICENSE). Third-party code and the sandbox's demo font
are listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
