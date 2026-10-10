# Gameplay systems

Scenes, entities, input, UI widgets and dialogue.

[Back to the README](../README.md)

- [Scenes (Scene/)](#scenes-scene)
- [Entities (Entity/)](#entities-entity)
- [Input (actions)](#input-actions)
- [UI widgets (UI/UI.h)](#ui-widgets-uiuih)
- [Dialogue (Dialogue/)](#dialogue-dialogue)

## Scenes (`Scene/`)

`Emerald/Scene/SceneStack.h` keeps the screens of a game on a stack: title, gameplay, pause, game
over... Each one is a `Scene` with its own hooks, which mirror `Application`'s. The `Application`
owns a stack (`GetScenes()`) and runs it after its own hooks every frame. An app that never pushes
a scene works exactly as before.

```cpp
#include <Emerald/Scene/SceneStack.h>

class PauseScene final : public Emerald::Scene {
public:
    explicit PauseScene(Emerald::Application& app) : Scene("Pause"), m_App(app)
    {
        DrawBelow = true; // the game stays visible (frozen) under the menu
    }
    void OnEnter() override { /* open the menu */ }
    void OnUpdate(f32) override
    {
        if (m_App.GetInput().WasActionPressed("Pause"))
            GetStack()->Pop(Emerald::Transition::Fade(0.2f));
    }
    void OnRender2D(Emerald::Renderer2D& r) override { /* the menu */ }

private:
    Emerald::Application& m_App;
};

// From the game scene:
GetStack()->Push(std::make_unique<PauseScene>(m_App));
```

- **Hooks:** `OnEnter` / `OnExit` when a scene joins or leaves the stack, `OnPause` / `OnResume`
  when another scene is pushed on top of it or popped off it. Then `OnEvent`, `OnFixedUpdate`,
  `OnUpdate`, `OnRender` (raw SDL GPU draws), `OnRender2D` and `OnImGui`, as in `Application`.
- **Requests:** `Push`, `Pop`, `Replace` (pop + push), `Clear` (everything leaves, top first) and
  `ReplaceAll` (everything leaves, then a new scene enters: back to the title). Each one takes an
  optional `Transition`.
- **Deferred changes:** requests are queued and applied at the end of the stack's `Update`, after
  every scene has updated. A scene can pop itself or push the next one from inside its own hooks;
  nothing is destroyed while one of its hooks runs. Requests made during a transition wait for it.
- **Below the top:** with `DrawBelow` the scene under this one is drawn first (a pause menu over
  the game); with `UpdateBelow` it keeps updating too (a HUD over a running game). The flags chain:
  the stack draws downwards for as long as each scene above has `DrawBelow`. `IsDrawn(i)` and
  `IsUpdated(i)` say which scenes run this frame.
- **Input focus:** only the top scene gets input, and only while no transition runs (`HasFocus()`).
  For the others the stack sets `Input::SetBlocked(true)`: every action reads as up and every axis
  as 0. `OnEvent` goes to the focused scene only. The app's own hooks run first and see input
  unblocked (the sandbox's Escape and C keys work in every scene). The raw `Keyboard` and
  `Gamepads` are not blocked.
- **Transitions:** `Transition::Fade(seconds, color)` covers the screen in `seconds`, makes the
  change while it is fully covered, then uncovers it in `seconds` again. The amount is a tween
  (`Curve`, `QuadInOut` by default). Set `Draw` for a custom look: it gets the `Renderer2D`, the
  view size and the cover amount (0 to 1), in a pixel-space batch drawn over every scene. Pushing
  onto an empty stack with a transition starts covered, so the first scene fades in.
- **Shutdown:** the application exits every scene after the main loop, before `OnShutdown` and
  before the asset manager goes. Scenes can hold asset handles and GPU resources and release them
  in their destructor.

**Rock Blaster with scenes** (a sketch; the Asteroids project itself is unchanged). Its title
screen, game, pause menu and options menu map onto the stack like this:

```cpp
// Title: a menu; starting a game replaces it with a fade.
void TitleScene::OnUpdate(f32)
{
    if (input.WasActionPressed("Start"))
        GetStack()->Replace(std::make_unique<GameScene>(app), Transition::Fade(0.4f));
    else if (input.WasActionPressed("Options"))
        GetStack()->Push(std::make_unique<OptionsScene>(app)); // over the title (DrawBelow)
}

// Game: pausing pushes an overlay; losing the last ship goes to the game over screen.
void GameScene::OnUpdate(f32)
{
    if (input.WasActionPressed("Pause"))
        GetStack()->Push(std::make_unique<PauseScene>(app)); // DrawBelow: the frozen game shows
    if (m_Lives == 0)
        GetStack()->Replace(std::make_unique<GameOverScene>(app, m_Score), Transition::Fade(1.0f));
}
void GameScene::OnPause() { app.GetAudio().SetMuted(true); } // the thrust sound stops, etc.
void GameScene::OnResume() { app.GetAudio().SetMuted(false); }

// Pause: resume, options, or quit to the title.
void PauseScene::OnUpdate(f32)
{
    if (input.WasActionPressed("Pause"))
        GetStack()->Pop();
    else if (selected == Options)
        GetStack()->Push(std::make_unique<OptionsScene>(app)); // Pop returns to the pause menu
    else if (selected == QuitToTitle)
        GetStack()->ReplaceAll(std::make_unique<TitleScene>(app), Transition::Fade(0.4f));
}
```

The stack then reads, bottom to top: `Title`; or `Game`; or `Game, Pause`; or
`Game, Pause, Options`. The options scene is the same class wherever it is pushed from, and
popping it always returns to whatever opened it.

**The sandbox's scenes** (`sandbox/src`):

- **Title** (`TitleScene.h`): the tweened menu over drifting gems. "Camera demo" fades to black.
  "Tilemap room" uses a custom transition, horizontal blinds closing from alternate sides
  (`Blinds` in `Shared.h`). "Entity swarm" fades to white, "Platformer" to black. "Quit" quits.
- **Camera demo** (`DemoScene.h`): the shapes, sprites, text, camera, hero and collision yard.
- **Tilemap room** (`TilemapScene.h`): the Tiled map (see Tilemaps).
- **Entity swarm** (`SwarmScene.h`): thousands of entities in the scene's own `World` (see Entities).
- **Platformer** (`PlatformerScene.h`): the platformer level (see Platformer physics).
- **Pause** (`PauseScene.h`): M / Start in any game scene. It has `DrawBelow` and plays its
  menu intro over the frozen scene. Choices: Resume (the outro plays, then the scene pops), CRT
  effect, Replay intro, Title screen (`ReplaceAll` with a fade), Quit.
- T cycles camera demo → tilemap room → entity swarm → platformer → camera demo (`Replace` with a fade).
- `main.cpp` keeps what is global: input bindings, the shared assets (`Shared.h`), the triangle
  and the bouncing quads (entities in the app's `GetWorld()`) under every scene, Escape / C, and screenshots. It also builds the scenes
  (`SandboxShared::Make`), so they never include each other.
- **ImGui:** the panel's "Scene stack" section lists the scenes top first. For each one it shows
  DrawBelow and UpdateBelow (both editable), and whether it is drawn, updated and has focus. It
  also shows the transition's cover and the pending requests. Its buttons push a pause, pop
  (with a fade), go to the title (blinds) or go to the demo (white fade). Each scene appends its
  own section below.

`SceneTests` covers the stack without a GPU (see the table under [Tests](testing-and-profiling.md#tests)).

## Entities (`Entity/`)

Game objects are **entities** made of **components** (plain structs), updated by **systems**
(plain functions). The storage is [EnTT](https://github.com/skypjack/entt), behind a thin Emerald
layer: `World`, `Entity` and `Each`.

**Why EnTT behind a thin layer** (rather than our own small ECS):

- **For EnTT:** it is fast (packed component arrays, cache-friendly views), battle-tested in shipped
  games, and scales to hundreds of thousands of entities. Writing and debugging our own would take
  time that games should get.
- **For the layer:** game code reads like the rest of Emerald (`Spawn`, `Add`, `Each`, `Destroy`)
  instead of EnTT's API, destruction is always deferred (safe inside loops and callbacks), and
  the backend could be swapped later without touching games.
- **Against EnTT:** it is template-heavy, so mistakes give long error messages and the headers cost
  compile time. The layer keeps most calls on a few simple functions, which keeps the errors
  shorter. EnTT's headers are included as SYSTEM headers, so their warnings don't show up in our
  builds. `GetRegistry()` is the escape hatch for what the layer does not cover (groups, sorting,
  signals...).

```cpp
#include <Emerald/Emerald.h>

class GameScene : public Emerald::Scene {
    Emerald::World m_World;                       // a scene owns its world
    Emerald::CollisionSystem m_Collisions{32.0f}; // broadphase cell size
    Emerald::Camera2D m_Camera;
    Emerald::Sprite m_Coin; // e.g. atlas->Get("coin")

    void OnEnter() override {
        for (int i = 0; i < 1000; ++i) {
            Emerald::Entity e = m_World.Spawn();
            e.Add<Emerald::Transform>(Emerald::Transform{.Position = {f32(i), 100.0f}});
            e.Add<Emerald::Velocity>(Emerald::Velocity{.Linear = {0.0f, 50.0f}});
            e.Add<Emerald::Collider>(Emerald::Collider::MakeCircle(8.0f));
            e.Add<Emerald::SpriteRenderer>(Emerald::SpriteRenderer{.Image = m_Coin, .Layer = 1});
        }
    }
    void OnUpdate(f32 dt) override {
        Emerald::UpdateMovement(m_World, dt);
        Emerald::UpdateAnimation(m_World, dt);
        for (const Emerald::CollisionEvent& hit : m_Collisions.Update(m_World))
            hit.B.Destroy(); // deferred: safe while looping
        m_World.Each<Emerald::Transform>([](Emerald::Entity e, Emerald::Transform& t) {
            if (t.Position.y > 720.0f)
                e.Destroy();
        });
        m_World.Flush(); // destroy what was marked, once per frame
    }
    void OnRender2D(Emerald::Renderer2D& r) override
    {
        r.Begin(m_Camera);
        Emerald::DrawSprites(m_World, r);
        r.End();
    }
};
```

**`World`** (`Entity/World.h`) owns the `entt::registry`. It is not copyable.
- `Spawn()` returns an `Entity`.
- `Each<Ts...>(fn)` calls `fn(Entity, Ts&...)` for every entity that has all of `Ts`. Pass only
  components that hold data (a tag struct can't be passed by reference).
- `Destroy(e)` / `e.Destroy()` only *marks* the entity. Marked entities are skipped by `Each` and
  report `IsValid() == false`. `Flush()` destroys them (their components' destructors run there),
  so call it once per frame at a safe point, e.g. the end of `OnUpdate`. `Clear()` destroys
  everything now.
- Stats: `GetCount()`, `GetPendingDestroyCount()`, `GetSpawnedTotal()`, `GetDestroyedTotal()`.
- Inside `Each` you may spawn, add components and destroy anything. Don't remove components
  from *other* entities of the same view while iterating.

**`Entity`** is a small handle: the world plus EnTT's versioned id, so a handle to a destroyed
entity stays invalid even when its slot is reused. It has `IsValid()` / `explicit operator bool`,
`Add<T>(args...)` (adds or replaces; aggregates are brace-initialized), `Get<T>()`, `TryGet<T>()`
(nullptr if missing), `Has<T>()`, `Remove<T>()`, `Destroy()`, `GetWorld()` and `ToU32()` (for
logs and ImGui).

**Components** (`Entity/Components.h`):

| Component | Fields | Used by |
|---|---|---|
| `Transform` | the same `Transform2D` as `Renderer2D` (position, rotation, scale) | everything |
| `Velocity` | `Linear` (px/s), `Angular` (rad/s) | `UpdateMovement` |
| `SpriteRenderer` | `Image` (a `Sprite`, e.g. an atlas region), `Options` (tint, flip, origin...), `Layer` | `DrawSprites` |
| `Animator` | the [sprite animation](animation-and-effects.md#sprite-animation-animator) player itself | `UpdateAnimation`, `DrawSprites` |
| `Collider` | `MakeCircle(radius)` or `MakeBox(halfSize)`, `Offset`, `Layer` / `Mask` bits | `CollisionSystem` |

There is **no parent / child transform**: an entity's transform is in world space. A hierarchy
costs an ordering pass every frame and is not needed yet. Store a parent `Entity` in your own
component if something has to follow another entity.

**Systems** (`Entity/Systems.h`): the scene calls them, in the order it wants.
- `UpdateMovement(world, dt)`: moves `Transform` by `Velocity`.
- `UpdateAnimation(world, dt)`: advances every `Animator`. Its `OnFinished` / loop callbacks may
  destroy the entity.
- `CollisionSystem(cellSize, wrapSize)`: `Update(world)` keeps a [`SpatialHash`](physics.md#collision-physics)
  of every `Transform` + `Collider` (updated in place each frame; destroyed entities and removed
  colliders leave it). It tests each candidate pair with the exact shape test from
  `Physics/Collision.h`, skipping pairs whose layer/mask bits don't match. It returns
  `CollisionEvent{A, B, Hit}` (normal from A to B, depth) sorted by id, so results are
  deterministic. `GetCandidateCount()` and `GetBroadphase()` are there for debugging. Colliders
  ignore the transform's rotation and scale.
- `DrawSprites(world, r, {.SortByY, .Visible})`: draws every `Transform` + `SpriteRenderer`, sorted by
  `Layer`, then by y (lower on screen is in front), then by id so equal keys don't flicker. If
  an `Animator` is present its current frame is drawn. `Visible` (e.g. the camera's world rect)
  culls off-screen entities. It returns the number of sprites drawn. Keep each layer on one
  texture to keep the draw calls low.

**Per app:** `Application::GetWorld()` is a world for apps that don't use scenes (the old
`GetRegistry()` returns its registry). It is cleared before the asset manager goes away.

**Numbers** (`EmeraldEntityBench`, release, 8-core Intel Xeon VM):

| Test | Time |
|---|---|
| spawn 100,000 entities with 4 components | 7.1 ms (14 M/s) |
| destroy 100,000 (deferred + `Flush`) | 4.8 ms (21 M/s) |
| churn: 10,000 alive, 1,000 spawned + 1,000 destroyed per frame, 1,000 frames | 0.13 ms per frame (7.6 M spawn+destroy/s); storages 2000 KiB before and after |
| systems, 10,000 moving circles (Transform, Velocity, Collider, SpriteRenderer) | movement 0.05 ms, collisions 2.5 ms (7,181 pairs → 5,670 contacts), sprites 1.06 ms |
| systems, 50,000 at the same density | movement 0.29 ms, collisions 15.3 ms, sprites 6.6 ms |

`EntityTests` checks that churn doesn't leak: 400,000 spawns over 200 frames, with every
component destructor counted and the storages not growing past the peak.

**Sandbox: "Entity swarm"** (`SwarmScene.h`; title menu, T from the tilemap room, or `--swarm`).
Thousands of walking heroes, spinning gems and coins bounce off the walls and off each other
through the broadphase, flash gold on contact, and fade in and out at the end of their lives.
The scene refills to the target count at up to 4000 entities/s. Under lavapipe (software
Vulkan), a release build runs 6,000 entities at about 55 fps.

| Key | Action |
|---|---|
| Space / gamepad South | burst of 1000 at the mouse |
| Backspace | destroy half |
| Up / Down | target -1000 / +1000 (default 3000) |
| Left click | select the nearest entity in the inspector (ImGui builds) |
| M / Start | pause |
| T | platformer |

The HUD shows entities, target, fps, spawned and destroyed per second, contacts and pairs, and
each system's time. With ImGui, the "Entity swarm" section shows the same, plus the broadphase
cells, a target slider, collision and y-sort toggles and burst / destroy buttons. The "Entities"
window (see Editor tools) lists the swarm and edits the selected entity, including the scene's own
`Life` component; the selection is outlined and stops aging so it stays to be inspected. The bump
flash and spawn speed are tweaks ("Swarm").

## Input (actions)

Games use **actions**: name what the player can do, bind keys and gamepad inputs to it, and query
the name.
`Application::GetInput()` returns the `Emerald::Input` (`include/Emerald/Input/Input.h`):

```cpp
// Once, e.g. in OnStart:
Emerald::Input& input = GetInput();
input.BindAction("Fire", {Key::Space});
input.BindAction("Thrust", {Key::W, Key::Up});     // any number of keys per action
input.BindAxis("Rotate", Key::A, Key::D);           // negative key, positive key
input.BindAxis("Rotate", Key::Left, Key::Right);    // more pairs for the same axis

// Gamepads (SDL's standard layout, so one set works for Xbox, PlayStation and Switch pads):
input.BindAction("Fire", {GamepadButton::South, GamepadButton::RightShoulder});
input.BindAction("Thrust", {GamepadButton::RightTrigger});  // trigger as a button
input.BindAxis("Rotate", GamepadAxis::LeftX);                // analog, deadzoned
input.BindAxis("Rotate", GamepadButton::DPadLeft, GamepadButton::DPadRight);

// Mouse buttons work like keys; the cursor position is in window coordinates.
input.BindAction("Fire", {MouseButton::Left});
const Vec2 cursor = input.GetMouse().GetPosition();

// In OnFixedUpdate / OnUpdate:
if (input.IsActionDown("Thrust"))    Accelerate(dt); // held
if (input.WasActionPressed("Fire"))  Shoot();        // once per press
if (input.WasActionReleased("Fire")) StopCharging();
angle += input.GetAxis("Rotate") * turnSpeed * dt;   // -1..+1 (keys: -1, 0 or +1)
input.Rumble(0.6f, 0.3f, 200);                        // low/high motor 0..1, milliseconds

// Remapping at runtime, e.g. from an options menu:
input.RebindAction("Fire", {Key::LeftCtrl});
input.RebindAxis("Rotate", Key::J, Key::L);
```

A second key held for an action does not count as a new press, and the action is only released
when its last key is. Opposite axis keys cancel out. Underneath, `Emerald::Keyboard`
(`Keyboard.h`, `input.GetKeyboard()`) tracks raw key state; `Key` values are physical keys (SDL
scancodes, so `Key::W` is the key left of `E` on any layout). `GetAxis` returns the binding with
the largest magnitude, so a full key press beats a half-tilted stick. Rebinding keys keeps the pad
bindings and vice versa (and the mouse buttons).

**Rebinding screens and saving the bindings** (`InputBindings.h`): `keyboard.GetPressedKey()` and
`gamepads.GetPressedButton()` give whatever went down this frame ("press a key..."), and
`GetKeyLabel(key)` what is printed on that key with the player's layout. `SaveBindings` /
`LoadBindings` store the listed actions in a `SaveData` with readable names, so they persist with
the save system:

```cpp
constexpr std::array<std::string_view, 2> kRebindable = {"Jump", "Fire"};
SaveBindings(input, settings, kRebindable); // input.Jump.keys = Space, Z / .buttons = South / .mouse =
saves.Save("settings", settings);
// At startup: bind the defaults, then load what the player changed (missing values keep them).
LoadBindings(input, saves.Load("settings").Data, kRebindable);
```

Keys are saved by SDL's scancode names (`GetKeyName` / `FindKey`: "Space", "Left Shift"; `#<n>`
for keys without a unique name), buttons by their enum names ("South", "LeftTrigger"). Unknown
names are skipped and logged; axes are not saved.

`Emerald::Mouse` (`Mouse.h`, `input.GetMouse()`) has the buttons (`Left`, `Middle`, `Right`, `X1`,
`X2`), `GetPosition()`, `HasMoved()` (this frame, e.g. to switch aiming from a stick back to the
mouse) and `IsInWindow()`. Clicks on the ImGui overlay don't reach the game, and losing focus
releases the buttons like keys.

### Gamepads

`Emerald::Gamepads` (`Gamepads.h`, `input.GetGamepads()`) uses SDL3's `SDL_Gamepad` API. Every
connected pad is opened (also when plugged in later), connects and disconnects are logged with
the pad's name and type, and all pads drive the same actions (single player: pick up any pad).

- **Buttons** are named by position (`South`, `East`, `West`, `North`, shoulders, d-pad, `Start`,
  ...). South is A on Xbox, Cross on PlayStation and **B** on Switch. For prompts use
  `gamepads.GetButtonLabel(GamepadButton::South)` (labels of the pad used last, e.g. "Cross") or
  `GetGamepadButtonLabel(button, type)`; `GetGamepadTypeName(type)` gives "PS4", "Switch Pro", ...
- **Virtual buttons** make analog inputs bindable to actions: `LeftTrigger`/`RightTrigger` and
  `LeftStickUp/Down/Left/Right` (same for the right stick) press at `ButtonThreshold` (0.5) and
  release a bit below it, with the same pressed/released edges as real buttons.
- **Axes** (`GamepadAxis`): sticks -1..+1 with **+Y down** (bind with scale -1 to make up
  positive), triggers 0..1. Values inside the deadzone read 0, the rest is rescaled so it still runs
  smoothly from 0 to 1. Settings via `gamepads.SetSettings(...)`: `StickDeadzone` (0.2),
  `RadialDeadzone` (true: on the stick's length, smooth diagonals; false: per axis),
  `TriggerDeadzone` (0.1), `ButtonThreshold` (0.5).
- **Rumble**: `input.Rumble(low, high, ms)` on all pads (ignored by pads without motors).
- The static SDL build forces `SDL_JOYSTICK`/`SDL_HIDAPI` on, and the HIDAPI hints for PS4, PS5
  and Switch are set explicitly, so those pads use SDL's own drivers (correct layout, labels and
  rumble, also over Bluetooth). If Steam is running, Steam Input may take over PlayStation/Switch
  pads and present them as Xbox controllers; that is a Steam setting, not an engine bug.

"Pressed/released" means *since the last frame* in `OnUpdate`/`OnRender*` and *since the last fixed
step* in `OnFixedUpdate`, so a tap in a frame that runs no fixed step (which happens at 144 fps with
120 Hz) still reaches the next step, exactly once; gamepad buttons follow the same rules. Key
repeats are ignored, held keys and pad buttons are released when the window loses focus, and with ImGui on, key presses are withheld from the game while an ImGui
text field is active.

## UI widgets (`UI/UI.h`)

Immediate-mode game UI for menus and options screens, in the style of Dear ImGui: every frame you
describe the widgets, and each one returns whether it was used. Mouse, keyboard and gamepad all
drive it (through named actions, so they can be rebound), and the focus moves between widgets by
their rectangles.

```cpp
// Once: the UI actions (UiUp/Down/Left/Right, UiAccept, UiBack, and the UiMoveX/Y stick axes).
Emerald::BindDefaultUiActions(GetInput());
input.RebindAction(Emerald::kUiBack, {Key::Backspace}); // like any other action

// Every frame (OnUpdate): the mouse in the UI's units (here window units, as drawn below).
ui.Begin(Emerald::ReadUiInput(input, input.GetMouse().GetPosition()), dt);
ui.BeginPanel("OPTIONS", {.Position = size * 0.5f, .Width = 400.0f, .Pivot = {0.5f, 0.5f}});
if (ui.Slider("Music", &settings.Music, 0.0f, 1.0f)) ApplyVolumes();
ui.Toggle("Fullscreen", &settings.Fullscreen);
ui.Choice("Scale", &settings.Scale, {"x1", "x2", "x3"});
ui.Columns(2); // the next two share a row
if (ui.Button("Defaults")) settings = {};
if (ui.Button("Back") || ui.WasBackPressed()) Close();
ui.EndPanel();
ui.End();

// OnRender2D, in a pixel-space projection of the same units:
ui.Draw(r, font);
```

- **Widgets**: `Label` (left / center / right), `Button`, `Toggle`, `Slider` (Left / Right step,
  snapped to whole steps; the mouse drags it), `Choice` (a list of options, Left / Right / Accept
  / click step through it, wrapping). `Space` adds a gap.
- **Layout**: a panel stacks its widgets downwards in rows of `UiStyle::RowHeight`, with padding,
  spacing and an optional title row; `Columns(n)` puts the next n widgets side by side. The panel
  is placed by a pivot (centered, top-left...) using its last frame's height. Every rectangle is
  rounded to whole units, so pixel fonts and pixel-art frames stay crisp at a virtual
  resolution.
- **Focus**: one widget always has it (the first, unless `SetFocus`). Up / Down / Left / Right go
  to the nearest widget entirely in that direction, preferring ones in the same column / row;
  with nothing there it wraps around to the other side. Sliders, toggles and choices use Left /
  Right for their value. Held directions repeat after `RepeatDelay`, every `RepeatRate`. The
  mouse focuses what it moves over; a click is a press and release over the same widget.
- **Ids**: a widget's id is its label hashed; `"Volume##music"` shows "Volume" but hashes the
  whole string, and `PushId` / `PopId` scope labels (e.g. per save slot). The small per-widget
  state (the focus highlight's fade, a panel's height) is kept by id and dropped when a widget is
  no longer described.
- **Look**: `UiStyle` (colors, sizes, text scale, timing; `ui.GetStyle()` changes it live) and an
  optional 9-slice panel sprite (`PanelSprite`; `DrawNineSlice` is public for other frames).
  Drawing uses `Renderer2D` and a `Font`, and is separate from the logic: the description makes a
  list of items with their rectangles (`GetItems`), which `Draw` renders and `UiTests` checks
  without a GPU.

The sandbox's options screen (`sandbox/src/OptionsScene.h`, from the title or pause menu, or
`--options`) uses all of them: volume sliders, fullscreen and CRT toggles, a highlight color choice,
the controls, Defaults / Back. Rock Blaster's options menu would be the same few lines: one
`Slider` per volume (step 0.1), `Toggle`s for fullscreen, vsync, shake, particles, CRT and rock
bounce, and `Back`; its auto-repeating Left / Right is `UiStyle::RepeatDelay` / `RepeatRate`.

## Dialogue (`Dialogue/`)

Branching conversations, modeled on Jari Komppa's
[DialogTree (D3)](https://github.com/jarikomppa/d3) and written fresh (no D3 code; D3 is released
under the Unlicense, i.e. public domain). A conversation is a **deck** of **cards**; a card has text
and answers that jump to other cards. Conditions read flags and numbers, effects change them. The
logic (`Dialogue.h`) needs no GPU; `DialogueBox.h` draws it.

```cpp
// Members: one flag store for the whole game, the deck, a walker and a text box.
Emerald::DialogueFlags m_Flags;
Emerald::AssetHandle<Emerald::DialogueDeck> m_Deck =
    GetAssets().Load<Emerald::DialogueDeck>("assets/dialogue/keeper.json"); // hot reloaded
Emerald::Dialogue m_Talk{*m_Deck, m_Flags};
Emerald::DialogueBox m_Box;

m_Talk.Start(); // when the player talks to the keeper

// OnUpdate: typewriter, answers, continue (mouse, keyboard and gamepad through the UI actions).
m_Box.Update(m_Talk, Emerald::ReadUiInput(input, mouse), *m_Font, dt,
             {.Position = {width * 0.5f, height - 16.0f}, .Width = 600.0f});
// OnRender2D, in a pixel-space projection (the portrait is optional):
m_Box.Draw(r, *m_Font, portraitSprite, 4.0f);

// Saving: the flags and where the conversation is.
m_Flags.Save(data, "flags"); // flags.flags = "has_key keeper.hello", flags.values = "coins=1"
m_Talk.Save(data, "talk");   // talk.deck = "keeper", talk.card = "menu"
```

Without the box, the walker is a few calls: `GetText()`, `GetAnswerCount()` / `GetAnswer(i)`
(the answers you may pick now), `Choose(i)`, `Advance()` (continue / close), `IsActive()`.

**The file format** (JSON; ids are letters, digits and `_`):

```json
{
  "id": "keeper",
  "speaker": "Keeper",
  "portrait": "hero_idle_0",
  "start": "start",
  "cards": [
    { "id": "start", "answers": [
        { "goto": "again", "if": "keeper.hello" },
        { "goto": "hello" } ] },
    { "id": "hello", "text": "Hello. This is a test conversation.", "next": "menu" },
    { "id": "again", "text": "Hello again.", "next": "menu" },
    { "id": "menu",
      "text": ["What would you like to ask?", { "text": "The gate is open now.", "if": "gate_open" }],
      "answers": [
        { "text": "What is behind the gate?", "goto": "gate" },
        { "text": "Can I have the key?", "goto": "key", "if": "knows_gate !has_key" },
        { "text": "Anything else?", "goto": "coin", "once": true },
        { "text": "Goodbye.", "goto": "bye" } ] },
    { "id": "gate", "text": "Another room. The gate is locked.", "do": "knows_gate", "next": "menu" },
    { "id": "key", "text": "Here is the key.", "do": "has_key", "next": "menu" },
    { "id": "coin", "text": "Take this coin.", "do": "coins+=1", "next": "menu" },
    { "id": "bye", "text": "Goodbye." }
  ]
}
```

- Deck: `id` (required), `start` (default: the first card), `speaker` / `portrait` (defaults for
  every card), `data` (a free string for the game), `cards`.
- Card: `id`, `text` (a string, or a list of strings and `{ "text", "if", "do" }` lines that show
  only when their condition holds), `answers`, `next` (shorthand for one "continue" answer),
  `speaker`, `portrait` (a name the game resolves, e.g. a sprite in its atlas), `if` + `do` (run
  when the card is entered, if `if` holds; the card shows either way, as in D3), `data`.
- Answer: `text`, `goto` (no goto ends the conversation), `if`, `do`, `once` (offered until picked
  once), `data` (e.g. `"open_shop"` for the game to act on).

**Flags and numbers.** One `DialogueFlags` store holds flags (on/off) and integer values (0 until
set), shared by every deck and the game, so it can carry quest state ("quest.wolves.started",
"wolves_killed"). Names are letters, digits, `_` and `.`.

- Conditions (`if`, all terms must hold): `flag`, `!flag`, `name==3`, `name!=3`, `<`, `<=`, `>`,
  `>=` (D3's `has:flag` / `not:flag` work too).
- Effects (`do`, in order): `flag` or `set:flag`, `clear:flag`, `toggle:flag`, `name=3`,
  `name+=3`, `name-=3`.
- Entering a card sets `<deck>.<card>` ("was I there?", as D3 does); picking a `once` answer sets
  `<deck>.<card>.a<N>`. Effects run when they happen; restoring a save doesn't run them again.

**How a card plays:** answers whose `if` fails (or `once` answers already picked) are hidden. A
card with text and answers with text asks; a card whose only answers have no text shows
Continue; a card with text and no answers ends (Close). A card with neither text nor text answers
jumps on at once through its first visible answer (pick "first time" vs "hello again" by flags); a
card without text but with answers is a menu.

**The text box** types the text out with a tween (`CharsPerSecond`, default 45). Accept (Enter /
Space / South) or a click shows the whole text at once; then the answers are UI buttons (Up / Down
/ d-pad, Accept or a click) with the first one focused, or Continue / Close. The actions are the
UI's (`BindDefaultUiActions`), so they rebind like any other. `WrapText` breaks the lines.

**Never crashes on bad data.** Broken JSON, duplicate or bad ids and terms it doesn't understand
are logged and the file doesn't load (a placeholder deck with no cards, or the last good version
on hot reload). An answer to a missing card is a warning at load and ends the conversation when
picked. On hot reload, the open card shows its new text and answers; if it was removed, the
conversation restarts at the start card (logged). Automatic cards that jump in a circle stop after
`kMaxAutoSteps`.

The sandbox's Dialogue scene (`--dialogue`, or Dialogue in the title menu) talks to a keeper
(`sandbox/assets/dialogue/keeper.json`): branching answers, "Can I have the key?" only after asking
about the gate, text lines and answers that change once you have the key or open the gate, a
once-only answer, and F5 / F9 (or the panel's buttons) to save and load the flags
and the current card, also mid-conversation (`dialogue.sav` next to `settings.sav`). The top right
shows the flag store. Edit `keeper.json` while it runs (debug build) to see hot reload.
