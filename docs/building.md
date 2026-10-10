# Building and running

Dependencies, build options, per-platform setup, presets and the sandbox command line.

[Back to the README](../README.md)

- [Dependencies](#dependencies)
- [Options](#options)
- [Requirements](#requirements)
- [Building](#building)
- [Running](#running)

## Dependencies

| Library | Version | Notes |
|---|---|---|
| [SDL3](https://github.com/libsdl-org/SDL) | `release-3.4.16` | Built statically |
| [spdlog](https://github.com/gabime/spdlog) | `v1.17.0` | Uses bundled fmt |
| [stb](https://github.com/nothings/stb) | commit `2c980bb` | Header-only (stb_image, stb_image_write, stb_truetype + stb_rect_pack for fonts), exposed as `Emerald::stb` (INTERFACE) |
| [nlohmann/json](https://github.com/nlohmann/json) | `v3.12.0` | JSON (atlases, Tiled maps, dialogue decks); release tarball (SHA-256 pinned), linked PRIVATE |
| [dr_libs](https://github.com/mackron/dr_libs) | commit `dfe8377` | Only `dr_mp3.h` (MP3 decoding); header-only, public domain / MIT-0 |
| [Dear ImGui](https://github.com/ocornut/imgui) | `v1.92.9b-docking` | Optional, SDL3 + SDLGPU3 backends |
| [EnTT](https://github.com/skypjack/entt) | `v3.16.0` | Entity storage behind `World` / `Entity` (see [Entities](gameplay.md#entities-entity)); headers included as SYSTEM |
| [SDL_shadercross](https://github.com/libsdl-org/SDL_shadercross) | commit `1ff05be` | Build-time host tool (HLSL → SPIR-V/DXIL/MSL), built from source with vendored DXC + SPIRV-Cross |

## Options

| Option | Default | Description |
|---|---|---|
| `EMERALD_BUILD_SANDBOX` | `ON` | Build the sandbox example executable |
| `EMERALD_USE_IMGUI` | `OFF` | Fetch Dear ImGui (docking) and integrate it into the app loop, with the editor tools (see [Editor tools](tools.md#editor-tools-editor-imgui-builds)) |
| `EMERALD_BUILD_SHADERCROSS` | `ON` | Build the `shadercross` tool from source if `EMERALD_SHADERCROSS_EXECUTABLE` is empty |
| `EMERALD_SHADERCROSS_EXECUTABLE` | *(empty)* | Use this prebuilt `shadercross` instead of building it |
| `EMERALD_SHADERCROSS_BUILD_DIR` | *(empty)* | Where the tool is built; empty means the per-user cache folder (see [The shader compiler](#the-shader-compiler)) |
| `EMERALD_SHADER_FORMATS` | `SPIRV;DXIL;MSL` | Shader formats generated at build time |
| `EMERALD_MATH_SIMD` | `ON` | Use the SSE code paths of the math library on x86/x64 (see [Math library](core.md#math-library)) |
| `EMERALD_BUILD_TESTS` | `ON` | Build the unit tests and register them with CTest |
| `EMERALD_BUILD_BENCH` | `OFF` | Build the micro-benchmarks (math, particles, collision, entities) |
| `EMERALD_PROFILE` | `OFF` | Fetch Tracy and turn the `EM_PROFILE_*` macros on (see [Profiling](testing-and-profiling.md#profiling-tracy)) |
| `EMERALD_ASAN` | `OFF` | Build everything with AddressSanitizer (see [AddressSanitizer](testing-and-profiling.md#addresssanitizer)) |

The engine exports `EMERALD_WITH_IMGUI` / `EMERALD_MATH_SIMD` / `EMERALD_PROFILE` (0 or 1) as public compile
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
| `profile` | Optimized with debug info (`RelWithDebInfo`) and `EMERALD_PROFILE=ON` (Tracy) |
| `debug-asan` | Debug build with `EMERALD_ASAN=ON` (AddressSanitizer) |

Options can also be passed directly, e.g. `cmake --preset release -DEMERALD_USE_IMGUI=ON`.

## The shader compiler

The engine's HLSL shaders are compiled at build time by SDL_shadercross, which Emerald builds from
source together with DirectXShaderCompiler (about a thousand files, 15 minutes or more). To do that
only once per machine, the tool lives outside your build folders, in a per-user cache folder named
after a hash of its recipe (`tools/shadercross`):

| Platform | Folder |
|---|---|
| Windows | `%USERPROFILE%\.emerald\shadercross\<hash>` (kept short: DXC's sources go about 200 characters deep, and Windows tools fail past 260) |
| macOS | `~/Library/Caches/Emerald/shadercross/<hash>` |
| Linux | `$XDG_CACHE_HOME/emerald/shadercross/<hash>` (or `~/.cache/emerald/...`) |

- The first build anywhere on the machine builds the tool there. When it has been built and
  smoke-tested, its `bin/` gets an `emerald-shadercross.ok` marker.
- Every later configure that finds the marker uses the tool as is, with no build step at all: other
  presets, fresh build folders, and other projects using Emerald (a game that fetches Emerald finds
  the same folder, as long as the recipe is the same).
- A newer Emerald with a different recipe (e.g. new pinned versions) gets a new hash, so a new
  folder and one more full build. Old `<hash>` folders can be deleted.
- **To force a rebuild**, delete the `<hash>` folder (or just its `bin/emerald-shadercross.ok`)
  and reconfigure.
- `EMERALD_CACHE_DIR` (an environment variable) moves the whole cache, e.g. to another drive; CI
  uses it. `-DEMERALD_SHADERCROSS_BUILD_DIR=<dir>` builds and reuses the tool in that exact folder
  instead. `-DEMERALD_SHADERCROSS_EXECUTABLE=<exe>` uses a shadercross you built yourself.

The tool used to be built in `<project>/build/_shadercross`. Build folders configured that way move
to the cache folder at their next configure; the old `build/_shadercross*` folders can be deleted.

## Running

```sh
./build/debug/bin/Sandbox                                  # run until the window is closed or Esc is pressed
./build/debug/bin/Sandbox --frames 120                     # quit automatically after 120 frames
./build/debug/bin/Sandbox --frames 60 --screenshot out.png # save the last frame as a PNG (GPU readback)
./build/debug/bin/Sandbox --record run.txt                 # record the input actions (see Dev tools)
./build/debug/bin/Sandbox --replay run.txt --capture shots # replay them, saving every frame
./build/debug/bin/Sandbox --gpu vulkan                     # pick the GPU backend (see below)
./build/debug/bin/Sandbox --demo                           # skip the title: start in the camera demo
./build/debug/bin/Sandbox --tilemap                        # start in the tilemap room (see Tilemaps)
./build/debug/bin/Sandbox --swarm                          # start in the entity swarm (see Entities)
./build/debug/bin/Sandbox --platformer                     # start in the platformer level (see Platformer physics)
./build/debug/bin/Sandbox --dialogue                       # start in the dialogue demo (see Dialogue)
./build/debug/bin/Sandbox --particles                      # start in the particle effects (see Editor tools)
./build/debug/bin/Sandbox --lighting                       # start in the lighting scene (see Lighting)
./build/debug/bin/Sandbox --options                        # start in the options screen over the title
./build/debug/bin/Sandbox --platformer --collision         # turn on the collision overlay
./build/debug/bin/Sandbox --tilemap --objects              # turn on the map objects overlay
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
