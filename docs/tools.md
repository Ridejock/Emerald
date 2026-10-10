# Tools

ImGui editor tools, command-line options, input record/replay and the Python asset tools.

[Back to the README](../README.md)

- [Editor tools (Editor/, ImGui builds)](#editor-tools-editor-imgui-builds)
- [Dev tools](#dev-tools)

## Editor tools (`Editor/`, ImGui builds)

Debug panels on top of the ImGui overlay: live `Tweak` variables, an entity inspector and a particle
editor. They exist in builds with ImGui (`EMERALD_USE_IMGUI=ON`, e.g. the `debug-full` preset);
without it the headers turn into stand-ins that compile to nothing (a `Tweak` is just its value,
the inspector and editor calls are empty inline functions), so games call them unconditionally.
**F1** hides and shows every ImGui window (`Application::SetImGuiVisible`).

**Tweaks** (`Editor/Tweak.h`): values registered from code and tuned in the Tweaks window while the
game runs. `f32` and `i32` (a slider with a range, a drag field without), `bool` and `Vec4` (a color):

```cpp
// At namespace scope, or as members of the scene that uses them (shown while it exists):
Emerald::Tweak<f32> g_JumpHeight{"Player", "Jump height (px)", 57.0f, {8.0f, 160.0f}};
Emerald::Tweak<i32> g_Lives{"Player", "Lives", 3, {1, 9}};
Emerald::Tweak<bool> g_ShowTrail{"Effects", "Show trail", true};
Emerald::Tweak<Emerald::Vec4> g_Sky{"Effects", "Sky", {0.36f, 0.62f, 0.86f, 1.0f}};

const f32 height = g_JumpHeight; // reads like the value (or .Get())

// Once at startup: the saved values, if the file exists. In OnImGui: the window.
Emerald::GetTweaks().SetFile(GetAssets().GetRoot() / "tweaks.json");
Emerald::GetTweaks().ShowPanel();
```

The window groups them by category, with a filter, Save / Load / Reset all, and a "reset" button
next to each value that differs from the code's default. The file is readable JSON, grouped the
same way:

```json
{
  "Platformer feel": {
    "Coyote time (s)": 0.14,
    "Gravity (px/s2)": 1650,
    "Jump height (px)": 64
  },
  "Platformer look": {
    "Show trail": false,
    "Sky": [0.2, 0.3, 0.5, 1]
  }
}
```

Values are applied when a tweak registers, so a file loaded at startup reaches tweaks created later
(a scene's members), and a tweak that goes away (the scene is left) leaves its value for the next
one. Entries for tweaks that aren't registered are kept and written back. Editing the file while
it runs reloads it (the window checks it twice a second). Tuned values only exist in ImGui builds:
copy the good ones back into the code. The sandbox keeps its file (`sandbox/assets/tweaks.json`)
out of git and skips it for automated runs (`--frames`, `--replay`), so replays use the code's
values.

**Entity inspector** (`Editor/EntityInspector.h`): the "Entities" window lists a `World`'s entities
(filtered by component, thousands are fine: only visible rows are built), and shows the selected
one's components with their values editable live, a Remove button per component and Destroy. The
engine's components are registered (`Transform`, `Velocity`, `SpriteRenderer`, `Animator`,
`Collider`); a game adds its own with a function made of `Edit::` widgets, so it doesn't need
`imgui.h`:

```cpp
struct Health { i32 Points = 10; f32 Regen = 0.5f; };
struct Frozen {};

Emerald::EntityInspector m_Inspector;
m_Inspector.Add<Health>("Health", [](Health& h) {
    Emerald::Edit::Int("Points", h.Points, 0, 100);
    Emerald::Edit::Float("Regen", h.Regen, 0.0f, 5.0f); // 0, 0: a drag field
});
m_Inspector.AddTag<Frozen>("Frozen"); // an empty struct: listed, nothing to edit

m_Inspector.Show(m_World);                         // OnImGui
m_Inspector.DrawSelection(r);                      // OnRender2D: outline the selection
m_Inspector.SelectAt(m_World, mouse.GetPosition()); // on a click: the nearest entity
```

`Edit::` has `Float`, `Int`, `Bool`, `Vector` (a `Vec2`), `Color`, `Angle` (radians shown in
degrees), `Choice` and `Text`.

**Particle editor** (`Editor/ParticleEditor.h`): a window that edits every field of a
`ParticleEffect` (colors, sizes, shape and its size, speed, angle and lifetime ranges, drag,
gravity, rate, burst, blend), with Save, Revert (to the file), Defaults, Burst and Clear, and a `*`
while there are unsaved changes. The caller owns the effect and the preview:

```cpp
// m_Working is a copy of the asset, so edits show at once; Save writes the file, and the asset
// manager's hot reload passes it on to every handle of that effect.
if (m_Effect->Revision != m_Working.Revision) // (re)loaded from disk
    m_Working = *m_Effect;
const Emerald::ParticleEditorResult edit =
    m_Editor.Show(m_Working, GetAssets().Resolve("particles/sparks.json"));
if (edit.Burst)
    m_Particles.Emit(m_Working.GetConfig(), m_Emitter, m_Working.Burst);
```

**The sandbox:** the platformer's game feel and the swarm's flash are tweaks, the swarm has the
inspector (click an entity), and the Particles scene (`--particles`, or Particles in the title
menu) plays `sandbox/assets/particles/` (sparks, fire, fountain, smoke) with the particle editor:

| Key | Action |
|---|---|
| Up / Down | previous / next effect |
| Space / gamepad South | a burst (the effect's burst count) |
| Hold left mouse | move the emitter |
| Tab | quads / lines |
| R | clear and recenter |
| M / Start | pause |
| F1 | hide / show the ImGui windows |

Without ImGui the scene still plays the effects, and editing a file updates them (debug builds).

## Dev tools

Tools for developing and testing games, shared so each project doesn't grow its own one-offs.

### Command line options (`Core/DevOptions.h`)

`Application` reads these from `ApplicationSpec::Args`, so every game that passes its command line
(`spec.Args = {argv, static_cast<usize>(argc)};`) and the sandbox has them without code of its own.
Values can also be written as `--frames=120`; unknown arguments are left to the game, which can
read the parsed options with `GetDevOptions()` (or call `Emerald::ParseDevOptions(args)` itself,
e.g. to pick a windowed mode before the `Application` exists).

| Option | Does |
|---|---|
| `--frames N` | quit after N frames (wins over `ApplicationSpec::MaxFrames`) |
| `--screenshot out.png` | save one frame as a PNG: the last one of a `--frames` run or a replay, otherwise frame 60 |
| `--capture dir` | save every frame as `dir/frame_000001.png`, `frame_000002.png`, ... |
| `--capture-fps N` | the capture's frame rate (default 60) |
| `--record run.txt` | record the input actions, saved when the app quits |
| `--replay run.txt` | play a recording instead of the devices, frame-exact; quits at its end |

While capturing, every frame gets a fixed frame time of 1 / `--capture-fps` seconds whatever the
real time was (saving PNGs is slow), so the sequence plays at that rate; turn it into a video with
e.g. `ffmpeg -framerate 60 -i shots/frame_%06d.png -pix_fmt yuv420p run.mp4`. A replay keeps its
recorded frame times instead. With `--capture` and `--screenshot` together, the screenshot is a
copy of its frame from the capture.

### Input recording and replay (`Input/InputRecording.h`)

`--record` saves what the game asked `Input` for in every frame's `OnUpdate` and in every fixed
step: each bound action's down / pressed / released state and each axis's value, plus each frame's
elapsed time. `--replay` feeds that back instead of the keyboard, gamepads and mouse buttons, so
the fixed timestep runs the same steps with the same input and the game takes exactly the same
path. Because it is recorded at the action level, a recording still plays after rebinding keys or
on a machine with other devices.

The file is plain text, one line per sample:

```
emerald-input 1
engine 0.1.0                 <- the engine version that recorded it (a mismatch is only a warning)
seed 3492330105              <- GetSeed() of the session
fixed-rate 120               <- a replay needs the same FixedUpdateRate
actions Jump Quit
axes MoveX MoveY
frame 16666667 00 1 0        <- elapsed ns, a flags digit per action (1 down, 2 pressed, 4 released), the axes
step 30 1 0                  <- one fixed step of that frame
step 10 1 0
```

To make a whole game replayable, seed its random numbers from `GetSeed()` (random per run; the
recorded seed when replaying). Not recorded: mouse position and wheel, keys read straight from
`Keyboard` instead of actions, text input and ImGui, and actions bound after `OnStart` (the
recording covers what is bound when the session starts). If a replayed frame runs a different
number of fixed steps than recorded (only if the fixed rate or step cap changed), a warning is
logged once. `ReplayTests` records a scripted run through the sandbox's platformer level with
uneven frame times and replays it twice: the positions after every step are bit-identical to each
other and to the live run.

```sh
./build/debug/bin/Sandbox --platformer --record run.txt       # play, then quit
./build/debug/bin/Sandbox --platformer --replay run.txt --screenshot end.png
```

### Asset checker (`tools/check_assets.py`)

Checks a game's assets before the engine sees them; prints one line per problem and exits with 1
if there was any, so CI can run it (standard library only; Python 3.8+):

```sh
python3 tools/check_assets.py assets/                            # atlases and Tiled maps
python3 tools/check_assets.py assets/ --palette art/palette.gpl  # and the colors of every PNG
python3 tools/check_assets.py assets/ --exclude "fonts/*"        # skip some files
```

- **Palette** (with `--palette file.gpl`, a GIMP palette as Aseprite / GIMP / Lospec export it):
  every pixel of every PNG is fully transparent or opaque in a palette color.
- **Atlases** (`*.json` in the engine's plain or TexturePacker's format; other JSON is skipped):
  the PNG of the same name exists, every region lies inside it, and every animation's frames or
  pattern name existing regions.
- **Tiled maps** (`*.tmj`): orthogonal, finite, CSV / array tile data; external tilesets exist and
  are `.tsj`; tileset images exist, match their stated size and hold the stated columns and tile
  count; tile sizes match the map; every tile id in tile layers and tile objects (group layers
  included) belongs to a tileset. Tilesets on their own (`*.tsj`) get the image checks.

### Text maps (`tools/tilemaps/textmap.py`)

Helpers for drawing levels as text and writing Tiled maps (standard library only), generalized
from the sandbox's map scripts (`make_platformer.py` uses them):

- `TextMap(rows, outside="#")`: `at(x, y)` (with `outside` off the map), `find("@")`,
  `is_open`, `mask(x, y, "#")` (which neighbours are solid: 1 up, 2 right, 4 down, 8 left, for
  autotiling), `feet` / `center` pixel positions of a cell.
- `cells(legend)`: tile layer data from a legend of character → GID, a list of GIDs (variants,
  picked by a repeatable hash of the cell; `(gid, weight)` pairs make some rarer) or a function
  `(map, x, y) -> gid` (edges from the neighbours, say). `FLIP_X` / `FLIP_Y` / `FLIP_D` mirror.
- `tile_layer`, `point_object`, `rect_object`, `object_layer`, `tileset`, `make_map` and `prop`
  build the JSON; `dump_map` writes it with one row of tiles per line so diffs stay readable.

The module's docstring has a complete small example. From another project, add Emerald's
`tools/tilemaps` to `sys.path` and import it. `ToolsTests` (CTest, when Python 3 is found) runs
the tools' unit tests (`python3 -m unittest discover -s tools/tests`), and `SandboxAssets` runs the
checker on the sandbox's assets.
