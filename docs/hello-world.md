# Hello, world

A window, a clear color, and one line of text. The program is
[`examples/hello/main.cpp`](../examples/hello/main.cpp). The listing under
[The program](#the-program) is that file. CI's `HelloDocMatches` test checks that the two still match.

[Back to the README](../README.md)

- [Prerequisites](#prerequisites)
- [Add Emerald to a CMake project](#add-emerald-to-a-cmake-project)
- [The program](#the-program)
- [What each part does](#what-each-part-does)
- [Build and run on Linux](#build-and-run-on-linux)
- [Build and run on Windows](#build-and-run-on-windows)

## Prerequisites

- CMake 3.24 or newer
- A C++20 compiler (GCC 11+, Clang 14+, MSVC 2022, Apple Clang 15+)
- [Ninja](https://ninja-build.org/) (the presets use it; any other generator works)
- Git, so CMake can fetch Emerald and its dependencies

On Linux, install SDL3's development packages before configuring. The package list, the Windows
Developer PowerShell note, and the other presets are in
[Building and running](building.md#requirements).

The first build compiles the HLSL compiler (SDL_shadercross and DirectXShaderCompiler) and
caches it for later builds. That step is the long one.
[The shader compiler](building.md#the-shader-compiler) explains the cache.

Opening the window needs a Vulkan, Direct3D 12, or Metal device. A machine with no GPU can use
Mesa's software Vulkan driver under Xvfb; that path is in [Running](building.md#running).

## Add Emerald to a CMake project

A game fetches Emerald and links its own executable. Emerald is not the top-level project then, so
`EMERALD_BUILD_SANDBOX`, `EMERALD_BUILD_EXAMPLES` and `EMERALD_BUILD_TESTS` stay off.
`emerald_add_shaders` compiles the engine shaders the 2D renderer loads, and it defines
`EMERALD_SHADER_FORMATS` for that target. `emerald_set_warnings` is the hook CI's
warnings-as-errors flag uses.

Save this as `CMakeLists.txt`, with [`main.cpp`](#the-program) beside it:

```cmake
cmake_minimum_required(VERSION 3.24)
project(Hello LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

include(FetchContent)
FetchContent_Declare(Emerald
    GIT_REPOSITORY https://github.com/Ridejock/Emerald.git
    GIT_TAG        <commit hash>) # pin a commit
FetchContent_MakeAvailable(Emerald)

add_executable(Hello main.cpp)
target_link_libraries(Hello PRIVATE Emerald::Emerald)
emerald_add_shaders(Hello)
emerald_set_warnings(Hello)
```

[`examples/hello/CMakeLists.txt`](../examples/hello/CMakeLists.txt) in this repository is those
four commands. Emerald is already the project being built, so that file has no `FetchContent`.

## The program

```cpp
// Hello, Emerald.
//
// A window, a clear color, and one line of text, through the public headers only.
// Escape quits. Closing the window quits too: Application ends the loop when the
// close request arrives. The built-in debug font draws letters as capitals, so
// the greeting below is shown as "HELLO, EMERALD!".

#include <string>
#include <string_view>

#include <Emerald/Emerald.h>

namespace {

constexpr std::string_view kHello = "Hello, Emerald!";
constexpr u32 kWindowWidth = 960;
constexpr u32 kWindowHeight = 540;

class Hello final : public Emerald::Application {
public:
    using Application::Application;

protected:
    void OnStart() override { GetInput().BindAction("Quit", {Emerald::Key::Escape}); }

    void OnUpdate(f32 /*deltaSeconds*/) override
    {
        if (GetInput().WasActionPressed("Quit")) {
            Quit();
        }
    }

    void OnRender2D(Emerald::Renderer2D& renderer) override
    {
        const Emerald::Vec2i window = GetWindowSize();
        const Emerald::Vec2 view(window);
        // About an eighth of the window, in whole pixels of the 5x7 debug font.
        const i32 scale = Emerald::Max(1, (window.y / 8) / Emerald::kDebugTextLineHeight);
        const Emerald::Vec2 text = Emerald::MeasureDebugText(kHello, scale);
        const Emerald::Vec2 topLeft = (view - text) * 0.5f;

        renderer.Begin(Emerald::Mat4::OrthoPixelSpace(view.x, view.y));
        Emerald::DrawDebugText(renderer, topLeft, kHello, {1.0f, 1.0f, 1.0f, 1.0f}, scale);
        renderer.End();
    }
};

} // namespace

int main(int argc, char** argv)
{
    Emerald::ApplicationSpec spec;
    spec.Window.Title = std::string(kHello);
    spec.Window.Width = kWindowWidth;
    spec.Window.Height = kWindowHeight;
    // The swapchain is cleared to this at the start of every frame.
    spec.ClearColor = {0.07f, 0.094f, 0.086f, 1.0f};
    spec.ShaderFormats = Emerald::ShaderFormatFromMask(EMERALD_SHADER_FORMATS);
    spec.Args = {argv, static_cast<usize>(argc)};
    return Hello{spec}.Run();
}
```

## What each part does

`main` fills an `ApplicationSpec` and runs the loop. `Run` returns a process exit code, nonzero
when the window or the GPU device could not be created. `using Application::Application` keeps
`Application`'s constructor, so `Hello{spec}` is the whole object.

The spec is read once, before the loop:

| Field | Role |
|---|---|
| `Window.Title`, `Window.Width`, `Window.Height` | The window. The title is the greeting. The size is 960 by 540. |
| `ClearColor` | Straight RGBA, 0 to 1. The swapchain is cleared to it at the start of every frame, and `OnRender2D` then draws on top. These numbers are `ApplicationSpec`'s default dark green, written out so the clear is visible in the program. |
| `ShaderFormats` | The shader binaries `emerald_add_shaders` produced. `ShaderFormatFromMask(EMERALD_SHADER_FORMATS)` is that set. A SPIR-V-only build would otherwise still advertise DXIL and MSL. |
| `Args` | `main`'s argv. Emerald reads its dev options from it (`--frames`, `--screenshot`, `--gpu`, and the rest in [DevOptions.h](../include/Emerald/Core/DevOptions.h)) and leaves unknown arguments alone. |

`OnStart` runs once the window and the GPU exist. `BindAction("Quit", {Key::Escape})` names an
action and attaches Escape to it. The program then asks about the action `"Quit"`. That is the
[actions](gameplay.md#input-actions) model: another key or a gamepad button can be bound to the
same name later, and `OnUpdate` stays as it is.

`OnUpdate` runs once per frame. `WasActionPressed("Quit")` is true for the frame the action goes
down, and `Quit()` ends the loop after that frame.

The window's close button is handled by `Application`, which ends the loop when the close request
arrives. `OnEvent` still receives `EventType::WindowCloseRequested` and `EventType::Quit`, which
is where a game would save on the way out. This program leaves `OnEvent` to the base class.

`OnRender2D` only records draws. The engine uploads them and draws them later in the frame, after
the clear:

- `GetWindowSize()` is the client area in window coordinates, the same units as the projection.
  It follows a resize, so the line stays in the middle. The program stores no pixel position.
- The scale is an eighth of that height, in whole pixels of the debug font
  (`kDebugTextLineHeight` is 9, the glyph plus the gap under it). `Max(1, ...)` keeps a very
  short window at one font pixel.
- `MeasureDebugText` is the width and height `DrawDebugText` will use, including the gap after
  the last glyph. Half of (window minus that size) is the top-left of the centered line.
- `Begin(Mat4::OrthoPixelSpace(width, height))` maps pixels onto the window: (0, 0) is the
  top-left, and +Y points down. `End()` closes the batch.
- `DrawDebugText` draws the greeting with the built-in 5×7 face. There is no font file. The
  glyphs are rectangles in the sprite batch. Letters are uppercase only, so `Hello, Emerald!`
  is drawn as `HELLO, EMERALD!`. The comma, the space and `!` are real glyphs. The last
  argument `{1, 1, 1, 1}` is the glyph color.

Mixed-case text uses `Font` and `DrawString` ([Text](rendering.md#text-font)). That needs a
TrueType file. The engine does not ship one, and this example adds no assets. The sandbox's
demo font is Press Start 2P, documented in [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md).

The file includes Emerald's public headers only. The window, the device, the clear and the loop
stay inside `Application`.

## Build and run on Linux

### This repository

`EMERALD_BUILD_EXAMPLES` defaults to on when Emerald is the top-level project. CI passes it on
as well. From the repository root, after the [Linux packages](building.md#linux) are installed:

```sh
cmake --preset debug -DEMERALD_BUILD_EXAMPLES=ON
cmake --build --preset debug --target Hello
./build/debug/bin/Hello
```

Escape, or closing the window, quits. A fixed-length run that saves the last frame:

```sh
./build/debug/bin/Hello --frames 60 --screenshot hello.png
```

Other presets build the same target. The binary is `build/<preset>/bin/Hello`.

### Your own project

From the directory that holds the `CMakeLists.txt` and `main.cpp` above:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/bin/Hello
```

## Build and run on Windows

Use a Developer PowerShell, or an x64 Native Tools prompt for VS 2022, so `cl` and `ninja` are on
`PATH`. [Windows](building.md#windows) has the details.

### This repository

```powershell
cmake --preset debug -DEMERALD_BUILD_EXAMPLES=ON
cmake --build --preset debug --target Hello
.\build\debug\bin\Hello.exe
```

```powershell
.\build\debug\bin\Hello.exe --frames 60 --screenshot hello.png
```

### Your own project

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
.\build\bin\Hello.exe
```

Without Ninja, the Visual Studio generator in [Windows](building.md#windows) works too. Build the
`Hello` target. The executable is `Hello.exe` next to the shaders, under the directory set as
`CMAKE_RUNTIME_OUTPUT_DIRECTORY` (in the sample above, `build/bin`).
