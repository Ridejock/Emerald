# Rendering

The SDL GPU renderer, 2D shapes, camera, sprites, text, lighting, post effects and shaders.

[Back to the README](../README.md)

- [Renderer (SDL GPU)](#renderer-sdl-gpu)
- [2D shapes (Renderer2D)](#2d-shapes-renderer2d)
- [2D camera (Camera2D)](#2d-camera-camera2d)
- [Textures and sprites](#textures-and-sprites)
- [Text (Font)](#text-font)
- [CRT post-process (CrtEffect)](#crt-post-process-crteffect)
- [Lighting and post effects (Light.h, PostEffect.h)](#lighting-and-post-effects-lighth-posteffecth)
- [Shaders](#shaders)

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

## Lighting and post effects (`Light.h`, `PostEffect.h`)

2D point and spot lights with an ambient term, optional normal maps on sprites, and a runtime
post-effect chain. Lighting is **off by default** and then costs nothing (the unlit sprite
pipeline is used); turn it on with `Renderer2D::SetLightingEnabled(true)`. It applies to the
sprites drawn while it is on, so one frame can mix lit and unlit draws: turn it off again before
the HUD, menus or glowing sprites that should stay full bright. The ambient and lights are the
frame's (the values at Render time light every lit sprite).

```cpp
r.SetLightingEnabled(true);
r.SetAmbient({0.1f, 0.1f, 0.15f});
r.ClearLights();
r.AddLight({.Position = lamp, .Z = 40.0f, .Color = {1.0f, 0.8f, 0.5f},
            .Intensity = 1.2f, .Radius = 250.0f}); // point
r.AddLight({.Kind = LightKind::Spot, .Position = torch, .Direction = {0, 1},
            .InnerAngle = 0.3f, .OuterAngle = 0.7f, .Radius = 300.0f});
r.DrawSprite(wall, at, {.NormalMap = &wallNormal}); // optional; flat stand-in otherwise
r.SetLightingEnabled(false);
r.DrawString(font, "SCORE 100", {8, 8}, white);      // the HUD, unlit

// Post chain (empty = draw straight to the swapchain):
GetPostChain().Add(std::make_unique<TintEffect>());
SetCrtEnabled(true); // CRT is one effect in the same chain; reorder with Move
```

Up to `kMaxLights` (8) lights are packed as fragment uniforms (Lambert + distance falloff; spot =
point + cone). Tilemaps take a normal map per tileset: a string property `normalMap` on the
tileset in Tiled, an image laid out like the tileset's (path relative to the tileset file); tile
drawing passes it along, and hot reload watches it. Flipped tiles keep the normal map's X, so lit
tiles that face both ways look best as mirrored copies in the tileset. The sandbox's Lighting
scene (`--lighting`, or the title menu) shows a brick floor and props with procedural normals, a
warm point light, a spot that follows the mouse, and Tint + CRT in the chain.

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
