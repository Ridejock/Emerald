# Emerald

Emerald is a small, modern C++20 game engine built on [SDL3](https://github.com/libsdl-org/SDL).
It is split into:

- **`Emerald::Emerald`** – the engine library (logging, window/renderer, application loop, image loading).
- **`sandbox/`** – a minimal example app that links the engine, opens an SDL3 window, runs the event loop and logs via spdlog.

All dependencies are fetched automatically with CMake `FetchContent` and pinned to specific versions.

## Dependencies

| Library | Version | Notes |
|---|---|---|
| [SDL3](https://github.com/libsdl-org/SDL) | `release-3.4.16` | Built statically |
| [spdlog](https://github.com/gabime/spdlog) | `v1.17.0` | Uses bundled fmt |
| [stb](https://github.com/nothings/stb) | commit `2c980bb` | Header-only, exposed as `Emerald::stb` (INTERFACE) |
| [Dear ImGui](https://github.com/ocornut/imgui) | `v1.92.9b-docking` | Optional, SDL3 + SDLRenderer3 backends |
| [EnTT](https://github.com/skypjack/entt) | `v3.16.0` | Optional |

## Options

| Option | Default | Description |
|---|---|---|
| `EMERALD_BUILD_SANDBOX` | `ON` | Build the sandbox example executable |
| `EMERALD_USE_IMGUI` | `OFF` | Fetch Dear ImGui (docking) and integrate it into the app loop |
| `EMERALD_USE_ENTT` | `OFF` | Fetch EnTT and give each `Application` an `entt::registry` |

The engine exports `EMERALD_WITH_IMGUI` / `EMERALD_WITH_ENTT` (0 or 1) as public compile definitions.

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
./build/debug/bin/Sandbox              # run until the window is closed or Esc is pressed
./build/debug/bin/Sandbox --frames 120 # quit automatically after 120 frames
```

Headless (CI / no display):

```sh
SDL_VIDEO_DRIVER=dummy ./build/debug/bin/Sandbox --frames 120
```

## Project layout

```
include/Emerald/   Public engine headers
src/               Engine implementation
sandbox/           Example application
cmake/             Dependency setup (FetchContent)
```

## Using Emerald in your own project

```cmake
add_subdirectory(Emerald)          # or FetchContent
target_link_libraries(MyGame PRIVATE Emerald::Emerald)
```

```cpp
#include <Emerald/Emerald.h>

class MyGame : public Emerald::Application {
    void OnUpdate(float dt) override { /* ... */ }
};

int main() { return MyGame{}.Run(); }
```

## License

Emerald is released under the [MIT License](LICENSE).
