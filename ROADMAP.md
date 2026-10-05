# Emerald roadmap

Where the engine is going, in phases. **Principle: each phase ends with a small game that uses its
features**, so every feature is proven in a real game before the next phase starts.

Every planned item has a GitHub issue (labels `roadmap` + `phase-N`, one milestone per phase) with
the same scope and acceptance criteria; tick the criteria off there.

## Phase 1 - Essentials

The building blocks every 2D game needs, so games stop re-implementing them. ([milestone](https://github.com/Ridejock/Emerald/milestone/1))

### 2D camera ([#1](https://github.com/Ridejock/Emerald/issues/1))

A `Camera2D` with position, zoom, rotation and trauma-based shake, smooth follow of a target (dead zone, lerp, optional bounds), and screen <-> world conversion. Renderer2D batches take its view-projection.

Done when:
- Pan, zoom and shake work in the sandbox; shake decays over time and is frame-rate independent
- Follow with dead zone and world bounds (the camera never shows outside them)
- `ScreenToWorld` / `WorldToScreen` round-trip in unit tests, including zoom and letterboxing
- Rock Blaster's screen shake moves onto it

### Sprite animation ([#2](https://github.com/Ridejock/Emerald/issues/2))

Frame animations defined in the atlas JSON (frame list or name pattern, per-frame duration, loop / once / ping-pong), played by an `Animator` that yields the current frame; drawn with flip and tint.

Done when:
- Atlas JSON animation entries parsed and unit-tested (missing frames reported, not crashing)
- `Animator`: play, stop, speed, events on finish; timing tests at different dt
- Horizontal/vertical flip and tint applied when drawing an animated sprite
- Sandbox shows an animated character

### Collision ([#3](https://github.com/Ridejock/Emerald/issues/3))

Shape tests for circle, AABB and convex polygon (overlap, plus contact normal and depth where cheap), raycasts against them, and a spatial hash broadphase that returns candidate pairs.

Done when:
- Circle/circle, circle/AABB, AABB/AABB, polygon/polygon (SAT) overlap tests with normal + depth, unit-tested incl. edge cases
- Raycast against each shape type
- Spatial hash: insert/update/remove, query by area, pair iteration; a benchmark with 10k objects
- Rock Blaster's collision checks use it with identical results

### Asset manager ([#4](https://github.com/Ridejock/Emerald/issues/4))

Named, typed handles for textures, atlases, fonts and sounds; loads each file once (dedupe by path), reference-counted unloading, and in debug builds hot reload when a file changes on disk.

Done when:
- `Assets::Load<Texture>("ship.png")` style API returns a handle; loading the same path twice returns the same handle
- Missing files give a logged error and a visible placeholder, not a crash
- Debug hot reload: editing a PNG/atlas/WAV updates the running game within a second
- Unit tests for dedupe and lifetime (no GPU needed for the bookkeeping)

### Tweening, easing and timers ([#5](https://github.com/Ridejock/Emerald/issues/5))

Standard easing functions, tweens of floats/vectors/colors with delay, repeat, yoyo and completion callbacks, and one-shot / repeating timers, all driven by the game's update dt.

Done when:
- Easing set (linear, quad, cubic, back, elastic, bounce; in/out/in-out) with unit tests at 0, 0.5 and 1
- Tweens can be cancelled and chained; completion callbacks fire exactly once
- Timers: after / every / cancel, frame-rate independent
- Used by a sandbox menu animation

### Pixel art asset pipeline ([#22](https://github.com/Ridejock/Emerald/issues/22))

Deferred: not needed yet; picked up when a test game needs art beyond what we have. A `tools/` script that makes art from any source (generated, hand-built, downloaded packs) look like one set by enforcing a per-game style guide: a fixed 16-32 color palette, a tile size/grid, outline and transparency rules. It snaps and quantizes the art to the guide, then packs it into the `TextureAtlas` PNG + JSON format.

Done when:
- One command turns a folder of raw images plus a style guide into an atlas the engine loads
- Every output pixel is a palette color or transparent (checked by a test)
- Works on a downloaded tileset and on generated sprites, and the results visibly match
- Documented in the README with an example style guide

### Phase 1 test game ([#6](https://github.com/Ridejock/Emerald/issues/6))

A small game built on the phase 1 features (for example a top-down arena shooter with a following camera, animated sprites and tweened UI), proving they work together.

Done when:
- Playable start to game over, built only on engine features from phase 1 and earlier
- Uses camera, animation, collision broadphase, the asset manager and tweens
- Builds warning-free in CI with a downloadable zip

## Phase 2 - Beyond the arena

Scrolling levels, game flow and entities: what a level-based game needs. ([milestone](https://github.com/Ridejock/Emerald/milestone/2))

### Tilemaps ([#7](https://github.com/Ridejock/Emerald/issues/7))

Import Tiled (.tmx/.tmj) or LDtk levels: tile layers drawn in order with culling to the camera, object layers exposed to the game, and tile collision queries.

Done when:
- Import of at least one format (Tiled JSON or LDtk) with multiple tile layers, tilesets and object layers
- Only visible tiles are drawn; a 500x500 map stays at full frame rate
- Tile collision: solid / one-way flags queryable by area and used by the platformer physics
- Loader unit-tested on a small sample map

### Scene stack ([#8](https://github.com/Ridejock/Emerald/issues/8))

A stack of scenes (title, gameplay, pause, game over...) with push/pop/replace, each with its own update/draw/input, and fade (or custom) transitions between them.

Done when:
- Push/pop/replace with enter/exit hooks; scenes below can keep drawing (pause over gameplay)
- Fade transition with configurable duration and color; input blocked during transitions
- Unit tests for stack order and hook calls
- Rock Blaster's title/pause/options flow can be expressed with it

### Entities ([#9](https://github.com/Ridejock/Emerald/issues/9))

A way to organize game objects: build on the existing optional EnTT integration (or a light ECS) with common components (transform, sprite, collider, animation) and systems that draw and update them.

Done when:
- Decision documented (EnTT vs. light ECS) with the reasoning
- Core components and systems for transform, sprite drawing, animation and colliders
- Spawning/destroying thousands of entities per second without leaks (test)
- Used by the phase 2 test game

### Platformer physics ([#10](https://github.com/Ridejock/Emerald/issues/10))

Simple kinematic character physics for platformers: gravity, ground detection, slopes, one-way platforms, coyote time and jump buffering, moving against tilemap collision.

Done when:
- Character walks up/down slopes without bouncing or sticking; lands on and drops through one-way platforms
- Coyote time and jump buffer configurable
- Deterministic at the fixed timestep (same input -> same path, tested)
- Sandbox or test game level demonstrating each case

**Status:** done (`Physics/Platformer.h`, slope tiles in `Tilemap`, `PlatformerTests`, the sandbox's Platformer scene); verified on Windows / MSVC.

### Dev tools: input replay, headless capture, asset checker, map helpers ([#24](https://github.com/Ridejock/Emerald/issues/24))

Shared tools for developing and testing games, so each project stops growing its own one-off scripts.

Done when:
- Record a play session's actions to a file and replay it frame-exact at the fixed timestep (`--record` / `--replay`), tested
- Engine-level `--frames N`, `--screenshot` and frame-sequence capture usable by any game and the Sandbox
- Asset checker command (palette, atlas frames, map references, tile sizes) that CI can run
- Python helpers that turn text drawings into Tiled `.tmj` maps, with variant tile picking
- Used by Cavern Rover (#11)

**Status:** implemented (`Core/DevOptions.h`, `Input/InputRecording.h`, `tools/check_assets.py`, `tools/tilemaps/textmap.py`, `ReplayTests`, `ToolsTests`); Cavern Rover switched to `ParseDevOptions` (and runs the asset checker as a test) in CavernRover e5d939d, whose Windows CI (MSVC, zero warnings, tests) passed.

### Phase 2 test game: small Blaster Master-style platformer ([#11](https://github.com/Ridejock/Emerald/issues/11))

A small side-scrolling platformer in the spirit of Blaster Master (a vehicle section and an on-foot section) using tilemaps, scenes, entities and the platformer physics.

Done when:
- At least one scrolling level from a tilemap with enemies, pickups and a goal
- Title, gameplay, pause and game over scenes with transitions
- Builds warning-free in CI with a downloadable zip

**Status:** done as Cavern Rover (private repo), verified on Windows.

## Phase 3 - Polish & tooling

Game-feel features and in-engine tools. ([milestone](https://github.com/Ridejock/Emerald/milestone/3))

### UI widgets with focus navigation ([#12](https://github.com/Ridejock/Emerald/issues/12))

In-game UI widgets (button, toggle, slider, list, label, panel) with layout helpers and keyboard/gamepad focus navigation, styled through the engine's fonts and sprites.

Done when:
- Widgets usable with mouse, keyboard and gamepad; focus moves predictably (up/down/left/right) and wraps
- Options-style menu built from widgets in a few lines
- Focus/navigation logic unit-tested without a GPU
- Rock Blaster's menus could be rebuilt on it

**Status:** implemented, in review (`UI/UI.h`: immediate-mode widgets, layout, focus navigation and `UiStyle`; `UiTests`; the sandbox's options screen, `OptionsScene.h`).

### 2D lighting, normal maps and a post-effect chain ([#13](https://github.com/Ridejock/Emerald/issues/13))

Point/spot lights with normal-mapped sprites and an ambient term, plus a configurable chain of fullscreen post effects; the existing CRT effect becomes one effect in the chain.

Done when:
- Lights with color, radius and falloff; sprites with an optional normal map are lit correctly
- Post-effect chain: add/remove/reorder effects at runtime; CRT works as before inside it
- Lighting off costs nothing (no extra passes)
- Sandbox scene showing lighting + chain

**Status:** implemented, in review (`Renderer/Light.h`, lit sprite shaders, `PostEffect`/`PostChain` with CRT as one effect and a Tint grade; `LightingTests`; sandbox `LightingScene.h` / `--lighting`).

### Audio extras ([#14](https://github.com/Ridejock/Emerald/issues/14))

Music streaming from disk (no full decode up front) with crossfades, mixer groups (music/sfx/ui) with their own volumes, and positional panning/attenuation relative to the camera.

Done when:
- Streams an MP3/OGG music track with constant memory use; crossfade between two tracks
- Groups with volume and mute; per-voice group assignment
- Pan and volume follow a sound's position relative to the listener (camera)
- Unit tests for the mixer math (no audio device needed)

**Status:** implemented, in review (`Audio/Spatial.h`, `Audio/Music.h` streaming MP3/OGG with a fixed ring + crossfade via `MusicPlayer`; mixer groups Music/Sfx/Ui with volume/mute; spatial `PlayOptions::Position` vs listener; `AudioExtrasTests`; sandbox Options music controls).

### Versioned save system ([#15](https://github.com/Ridejock/Emerald/issues/15))

Save/load of game data with a format version, migrations between versions, atomic writes (no corrupt saves on crash) and save slots in the per-user folder.

Done when:
- Versioned format with registered migrations; old saves load into the current version
- Atomic write (temp file + rename); a failed or interrupted write keeps the old save
- Multiple slots; round-trip and migration unit tests
- Rock Blaster's settings/high scores could use it

### ImGui editor tools ([#16](https://github.com/Ridejock/Emerald/issues/16))

Debug-build editor panels on top of the ImGui overlay: an entity inspector, a particle emitter editor with live preview and save to file, and live tweak variables.

Done when:
- Entity inspector lists entities and edits component values live
- Particle editor edits every ParticleEmitterConfig field with a live preview and saves/loads JSON
- `Tweak` variables (float/int/bool/color) registered from code appear in a panel and can be saved
- Compiled out entirely in builds without ImGui

### Branching dialogue system ([#23](https://github.com/Ridejock/Emerald/issues/23))

A small branching dialogue module inspired by Jari Komppa's DialogTree (D3), written fresh (no D3
code): a conversation is a deck of cards, each with text and answers that jump to other cards;
answers can require flags and set or clear them. Dialogues are JSON files loaded and hot reloaded
through the asset manager, shown in a text box with a typewriter reveal (tweens) and an answer
menu (UI widgets, #12); flags and the current card are saved with the save system (#15).

Done when:
- Load a dialogue file and walk it: question, answers, choose, goto; missing card ids are logged,
  not a crash
- Flags gate answers and can be set/cleared by choices; unit tests port D3's guard and tag samples
- Text box with typewriter effect and answer selection works with mouse, keyboard and gamepad
- State round-trips through the save system
- Used for NPCs in a test game (e.g. the Blaster Master-style platformer)

### Phase 3 test game ([#17](https://github.com/Ridejock/Emerald/issues/17))

Polish pass on the phase 2 platformer (or a new small game) using the UI widgets, lighting/post chain, music crossfades, saves and the editor tools.

Done when:
- Menus built with the widgets, saves through the save system, music with crossfades
- At least one lit area and the post-effect chain in use
- Builds warning-free in CI with a downloadable zip

## Phase 4 - Shipping

More platforms, automated releases and performance visibility. ([milestone](https://github.com/Ridejock/Emerald/milestone/4))

### Linux / Steam Deck builds and optional Emscripten web ([#18](https://github.com/Ridejock/Emerald/issues/18))

Release builds for Linux (x64, tested on Steam Deck / SteamOS with gamepad) alongside Windows, and an optional Emscripten web build (WebGPU or a fallback) for browser demos.

Done when:
- Linux release package that runs on a clean SteamOS/Ubuntu install with the Deck's controls
- Documented build steps and presets for Linux packaging
- Spike or working Emscripten build of the sandbox, with the blockers documented if not shippable
- Platform differences (paths, fullscreen, input) covered

### CI that builds games and pushes to itch.io ([#19](https://github.com/Ridejock/Emerald/issues/19))

Reusable CI workflows that build and test every game on each push, package release zips per platform, and on a tag push them to itch.io with butler (channels per platform).

Done when:
- One reusable workflow template that a game repo can use with a few inputs
- Tag build publishes to itch.io via butler with the version from CMake; secrets documented
- Zero-warning policy enforced on all presets
- Asteroids/Rock Blaster migrated to it

### Profiler overlay and batch stats ([#20](https://github.com/Ridejock/Emerald/issues/20))

A lightweight in-game profiler: CPU scopes per frame (update, render, audio), GPU timing where available, frame time graph, and Renderer2D batch/draw-call/memory stats.

Done when:
- `EM_PROFILE_SCOPE` macro with negligible cost when disabled
- Overlay shows frame time graph, top scopes, draw calls, lines/sprites, frame arena and pool usage
- Optional capture export (e.g. Chrome trace JSON)
- Works with and without ImGui (basic text overlay)

### Phase 4 test: ship the games ([#21](https://github.com/Ridejock/Emerald/issues/21))

Ship the phase games (and Rock Blaster) through the new pipeline to all supported platforms.

Done when:
- Windows + Linux builds published to itch.io from CI on a tag
- Web demo of at least one game if the Emscripten build is viable
- Profiler overlay available in the debug builds of each game

## Done

Already in the engine (see the [README](README.md) for each):

- **SDL GPU renderer**: Vulkan / Direct3D 12 / Metal through SDL GPU, shaders compiled at build time with SDL_shadercross, screenshots, vsync control.
- **Line and sprite batches**: `Renderer2D`: batched lines and textured sprites, alpha and additive blending, draw order across batches.
- **Texture atlas**: `TextureAtlas` with JSON frame data.
- **TTF text**: `Font` (stb_truetype) and `Renderer2D::DrawString`: pixel and smooth fonts, alignment, measuring, Latin-1.
- **Particles**: `ParticleSystem`: fixed-capacity pool with scalar and SSE updates and emitter configs.
- **CRT post-process**: `CrtEffect`: curvature, bloom, afterglow, chromatic offset, vignette, optional scanlines/mask.
- **Action input and gamepads**: Named actions and axes bound to keys and gamepad buttons/axes, rebinding, hot-plugged pads, rumble.
- **Audio**: Mixer with voices and volumes, `Synth` for generated sounds, WAV and MP3 loading.
- **Fixed timestep**: `FixedTimestep`: fixed-rate updates with a per-frame step cap and interpolation alpha.
- **Frame arena**: Per-frame scratch allocator (`FrameArena`) plus pmr pool allocators with tracking.
- **Thread pool**: `ThreadPool` for background and parallel work.
- **Math**: Vec2/3/4, Mat4 and helpers, with SIMD and scalar implementations.
- **Paths**: Base path and per-user data folders (`Paths`).
- **ImGui debug overlay**: Optional Dear ImGui integration (SDL GPU backend) drawn over the frame.
