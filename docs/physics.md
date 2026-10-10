# Physics and world

Collision, tilemaps and platformer character physics.

[Back to the README](../README.md)

- [Collision (Physics/)](#collision-physics)
- [Tilemaps (Tilemap/)](#tilemaps-tilemap)
- [Platformer physics (Physics/Platformer.h)](#platformer-physics-physicsplatformerh)

## Collision (`Physics/`)

`Emerald/Physics/Collision.h` has the shape tests a 2D game needs, and `SpatialHash.h` a
broadphase so you don't test every pair:

- **Shapes:** `Circle{Center, Radius}`, `Aabb{Min, Max}` (also `Aabb::FromCenter(c, half)`), and
  convex polygons as a `std::span<const Vec2>` of points, either winding.
- **`Overlaps(a, b)`:** yes or no, for circle/circle, AABB/AABB and circle/AABB.
- **`Collide(a, b)`:** an `std::optional<Contact>`. Its `Normal` is a unit vector from `a` towards
  `b` and `Depth` is how far they overlap, so moving `b` by `Normal * Depth` separates them. It
  covers circle/circle, circle/AABB (both orders), AABB/AABB and polygon/polygon (SAT).
- **Edge cases:**
  - Touching is not overlapping.
  - Concentric circles push apart along +X.
  - A circle whose center is inside a box exits through the nearest side.
- **`Raycast(Ray{origin, direction, maxDistance}, shape)`:** the first hit (`Distance`, `Point`,
  surface `Normal`) on a circle, AABB or polygon. The direction doesn't need to be normalized. A
  ray that starts inside hits at distance 0.

```cpp
#include <Emerald/Physics/Collision.h>
#include <Emerald/Physics/SpatialHash.h>

Emerald::SpatialHash hash(64.0f); // cell size: about the size of a typical object
// Every step: tell it where things are now (it re-buckets only what changed cells)...
for (u32 i = 0; i < balls.size(); ++i)
    hash.Update(i, Emerald::Aabb::FromCenter(balls[i].Center, Vec2(balls[i].Radius)));
// ...then run the exact test on the candidate pairs (each pair once, i < j, sorted).
hash.ForEachPair([&](u32 i, u32 j) {
    if (std::optional<Emerald::Contact> c = Emerald::Collide(balls[i], balls[j]))
        balls[j].Center += c->Normal * c->Depth; // or split it by mass, add an impulse...
});
hash.Query(area, ids); // ids whose boxes overlap `area`
```

**Wrap mode.** `SpatialHash(cell, Vec2{width, height})` makes space repeat like a torus, as in
Asteroids. A box hanging off the right edge finds objects at the left, and queries wrap the same
way. Results are sorted by id, so they don't depend on hash map order.

`CollisionTests` checks every shape pair's normal and depth, the edge cases above, raycasts (hits,
misses, parallel rays, max distance, starting inside), and the hash against brute force on random
data, with and without wrapping.

`EmeraldCollisionBench` (Release, `-DEMERALD_BUILD_BENCH=ON`) uses 10,000 moving circles (radius
2-8) in a 2000 x 2000 world. Each frame moves them, updates the hash, collects the pairs and runs
the exact test. Numbers are best of several runs on an 8-core Intel Xeon VM with GCC 14:

| 10k objects, cell 32 | Time |
|---|---:|
| Insert all | 1.2 ms |
| Move + `Update` all | 0.34 ms |
| 1000 `Query` calls (64 x 64 area, ~14 hits each) | 1.4 ms |
| `GetPairs` + exact circle test (5,358 box pairs, 4,231 contacts) | 1.36 ms |
| Brute force, all 50M pairs (same 4,231 contacts) | 66 ms (about 35x slower) |

With 100k objects the update takes 4.5 ms and finding pairs 20 ms. The best cell size is about 1-4x
the object size: smaller cells put each object in more cells, and larger cells make for more
candidate pairs.

In the sandbox, walk the hero right out of the room into the **collision yard**:

- Balls bounce off each other, using the hash and circle contacts with mass ~ area, and off the
  boxes.
- The hero's circle shoves the balls and is blocked by the boxes.
- A spinning hexagon and an orbiting triangle turn red while SAT finds an overlap, with the
  contact normal drawn.
- A ray goes from the hero to the mouse and shows the nearest hit with its normal.

The ImGui panel shows the counts (cells, candidate pairs, contacts), the SAT result and the ray
distance, and has a toggle for the hash cells.

## Tilemaps (`Tilemap/`)

`Emerald/Tilemap/Tilemap.h` loads maps made in [Tiled](https://www.mapeditor.org/) in its JSON
format (`.tmj`). It reads tile layers, tilesets and object layers, answers tile collision queries,
and draws only the tiles the camera sees.

```cpp
// OnStart: through the asset manager (hot reload in debug builds), or Tilemap::Load(device, path).
m_Map = GetAssets().Load<Emerald::Tilemap>("maps/level1.tmj");
m_Camera.SetBounds(m_Map->GetBounds()); // the camera never shows outside the map
const Emerald::MapObject* spawn = m_Map->FindObject("spawn");

// OnFixedUpdate: move a box with tile collision (x first, then y, swept: no tunneling).
const Emerald::TileMove move = m_Map->MoveAndCollide(m_Box, m_Velocity * dt);
m_Box.Min += move.Delta;
m_Box.Max += move.Delta;
if (move.HitBottom)
    m_Velocity.y = 0.0f; // landed (floor or one-way platform)

// OnRender2D: every visible tile layer in order, culled to the camera.
r.Begin(m_Camera);
m_Map->Draw(r, m_Camera.GetVisibleBounds());
r.End();
```

What the loader reads:

- **Maps:** orthogonal, fixed size (not "infinite"), with the tile layer format set to CSV (the
  default). Map, layer, tileset, tile and object custom properties are all kept in a
  `Properties` (`GetBool`, `GetInt`, `GetFloat`, `GetString`, each with a fallback).
- **Layers** come in Tiled's order (`GetLayers()`, `FindLayer(name)`). A tile layer has
  `Cells`, one per tile, row by row. Group layers are flattened into their children: offsets add
  up, opacity multiplies and a hidden group hides its children. Image layers are skipped with a
  warning.
- **Tilesets:** embedded in the map or external `.tsj` files (a `.tsx` gives an error saying to
  save it as JSON). One image per tileset, with margin, spacing and drawing offset. The images
  load as textures relative to the file that names them. A string property `normalMap` adds a
  normal map for lighting (`Tileset::NormalSheet`; see Lighting below).
- **Objects** (`MapLayer::Objects`, `FindObject(name)`): `Name`, `Type` (Tiled's "Class"),
  `Position` and `Size` in map pixels with the position at the top-left (Tiled puts tile objects
  at the bottom-left, so the loader moves them), `Rotation`, `Shape` (rectangle, point, ellipse,
  polygon, polyline, tile, text), polygon `Points`, `Gid` for tile objects, and `Props`.
- **Flip bits:** a cell is a GID plus Tiled's flip bits (`kTileFlipX`, `kTileFlipY`,
  `kTileFlipDiag`). `GetTileGid(cell)` strips them and `GetTileTransform(cell)` turns them into
  `SpriteOptions` flips plus a quarter turn, which is how `DrawLayer` draws rotated tiles.
- **Errors never crash.** A missing map, tileset or image, broken JSON, or unsupported data
  (infinite, isometric, base64, wrong cell count) logs what is wrong with the file name and
  returns `nullopt`. The asset manager then shows an empty map, or keeps the last good version on
  hot reload. Cells with unknown tile ids are logged and left empty.

**Collision.** Every tile layer's tiles go into one grid when the map loads, from properties on
the tiles in the tileset:

| In Tiled, on the tile (tileset editor) | Result |
|---|---|
| Custom property `solid` (bool, ticked), or Class `solid` | `TileCollision::Solid`: blocks from every side |
| Custom property `oneway` (bool, ticked), or Class `oneway` | `TileCollision::OneWay`: a platform top, only blocks moving down onto it from above |
| Float properties `slopeLeft` / `slopeRight`, or Class `slope` | `TileCollision::Slope`: a sloped floor (see [Platformer physics](#platformer-physics-physicsplatformerh)) |
| A bool property `collision` set to false *on a tile layer* | that layer's tiles never collide (decoration drawn over the hero, say) |

Solid wins over slope and slope over one-way when layers overlap, and outside the map counts as empty, so put walls at
the edges. The queries:

- `GetCollision(tile)`
- `WorldToTile` and `GetTileBounds`
- `GetTileRange(area)` and `ForEachCollidingTile(area, fn)` for your own tests
- `OverlapsSolid(area)`
- `MoveAndCollide(box, delta, options)`, which moves an AABB and reports `HitLeft` / `HitRight` /
  `HitTop` / `HitBottom` (on the ground), plus `OnSlope` / `OnOneWay`

The platformer physics builds on these (see [Platformer physics](#platformer-physics-physicsplatformerh)).

**Drawing.** `DrawLayer(r, layer, view)` draws only the tiles that overlap `view` (use
`Camera2D::GetVisibleBounds()`) and returns how many it drew. `Draw` does every visible tile layer
in order, and `DrawCollision` is a debug overlay: solid tiles in red, one-way tops in yellow, slopes in cyan. To
put characters between layers, call `DrawLayer` per layer (the sandbox draws the hero where the
"Objects" layer is).

**Hot reload.** A tilemap asset watches the `.tmj`, its external `.tsj` files and every tileset
image (and normal map). Saving any of them in Tiled (or an image editor) reloads the whole map
within about half a second. Read layers, objects and tilesets through the handle each time
instead of keeping pointers into them.

**The sandbox's tilemap room.** Pick it on the title screen, press T in the camera demo, or start
the sandbox with `--tilemap`:

- **The map:** `sandbox/assets/tilemaps/room.tmj` is 48 x 30 tiles of 16 px.
  - Layers: Ground, Walls and Decor, then Objects, then Overhead (pillar tops drawn over the
    hero).
  - Tilesets: `dungeon.tsj` (external) and `props` (embedded in the map).
  - Objects: a spawn point, two signs, a chest with `gold` / `contents` properties, the pond (an
    ellipse) and the stairs.
- **What you can do:** walk with WASD; the camera follows inside the map. The wooden railings are
  one-way: walk up through them, but they stop you walking down.
- **The flip row:** the arrow row near the spawn shows all eight flip combinations.
- **The ImGui panel:**
  - toggles for each layer, the collision overlay, object outlines and culling;
  - the number of visible tiles drawn;
  - a zoom slider;
  - the object the hero is standing in, with its properties.
- **Hot reload:** in the debug build, edit `room.tmj` or `dungeon.tsj` in Tiled while the sandbox
  runs and the room updates.

All of it comes from `tools/tilemaps/make_tilemaps.py`, which draws the art in code with a fixed
16-color palette and the standard library only. It writes `dungeon.png`, `dungeon.tsj`,
`props.png` and `room.tmj`. Running it again overwrites edits made in Tiled.

**Benchmark: 500 x 500 tiles.** Generate the map, then let the camera sweep it:

```sh
python3 tools/tilemaps/make_tilemaps.py --bench build/bench   # build/bench/bench.tmj
./build/release/bin/Sandbox --map build/bench/bench.tmj --pan --stats [--zoom 0.25] [--no-cull]
```

The map has three full 500 x 500 tile layers (Ground, Walls, Decor). The numbers below are from
the release build on an 8-core Intel Xeon VM with Mesa's software Vulkan driver (lavapipe) under
Xvfb, at 1280 x 720 with no vsync, averaged over 2 s windows:

| View | Tiles drawn per frame | `DrawLayer` CPU time | Frame rate |
|---|---:|---:|---:|
| Zoom 1, culled | ~4,200 | 0.15 ms | ~230 fps |
| Zoom 0.25 (16x the area), culled | ~65,800 | 2.5 ms | ~46 fps (lavapipe filling pixels) |
| Zoom 1, no culling (the whole map) | 285,269 | 10.4 ms | ~29 fps |

Culling keeps the cost at the size of the screen rather than the size of the map. The map loads
(1.5 MB of JSON) in about 50 ms.

`TilemapTests` loads the sample room, checks it in detail and breaks copies of it on purpose (see
the table under [Tests](testing-and-profiling.md#tests)).

## Platformer physics (`Physics/Platformer.h`)

A kinematic character for side-view games. A box runs, jumps and falls against a tilemap's
collision. There are no forces and no rigid bodies, only the body's own velocity, moved with
`Tilemap::MoveAndCollide` every fixed step.

```cpp
#include <Emerald/Physics/Platformer.h>

Emerald::PlatformerBody hero{.Position = spawn, .HalfSize = {5.0f, 7.0f}}; // the box's center
Emerald::PlatformerTunables tunables;                                       // the feel (below)

void OnFixedUpdate(f32 dt) override
{
    const Emerald::Input& input = GetInput();
    const Emerald::PlatformerInput in{.Move = input.GetAxis("MoveX"),
                                      .JumpPressed = input.WasActionPressed("Jump"),
                                      .JumpHeld = input.IsActionDown("Jump"),
                                      .Down = input.GetAxis("MoveY") > 0.5f};
    Emerald::StepPlatformer(hero, in, tunables, *map, dt);
    if (hero.Jumped)
        PlayJumpSound();
}
```

`PlatformerTunables` holds every number, in map pixels and seconds:

| Field | Default | What it does |
|---|---|---|
| `Gravity` | 1400 px/s² | pulls the body down |
| `MaxFallSpeed` | 420 px/s | terminal velocity |
| `RunSpeed` | 140 px/s | speed at full stick or key |
| `GroundAccel` / `GroundDecel` | 1600 / 2000 px/s² | towards the run speed on the ground, and back to 0 with no input |
| `AirAccel` | 1000 px/s² | control in the air, both ways |
| `JumpVelocity` | 400 px/s | upward speed when a jump starts (height v² / 2g, about 57 px) |
| `JumpCut` | 0.45 | releasing jump while rising multiplies the upward speed by this (variable jump height) |
| `CoyoteTime` | 0.10 s | after walking off a ledge, a jump still works for this long |
| `JumpBuffer` | 0.12 s | a jump pressed this long before landing happens on landing |
| `SnapDown` | 4 px | how far below a walking body still counts as ground (walking down slopes) |
| `DropThroughTime` | 0.2 s | how long one-way platforms are ignored after down + jump |

`PlatformerBody` holds the state:
- `Velocity`.
- The flags `Grounded`, `OnSlope`, `OnOneWay` and `Rising` (going up from a jump that can still
  be cut short).
- The timers `CoyoteTimer`, `BufferTimer` and `DropTimer`.
- What happened in the last step: `Jumped`, `Landed`, `DroppedThrough`, `HitWall` and
  `HitCeiling`, for sounds, animation and HUDs.
- `GetBox()` and `GetFeet()`.

**One step**, in order:
1. **Timers.** A jump press sets `BufferTimer` to `JumpBuffer`; otherwise it counts down.
2. **Run.** The horizontal speed accelerates towards `Move * RunSpeed`.
3. **Jump.** It happens if a jump is buffered (`BufferTimer > 0`) and the body is grounded or
   within coyote time. If the body stands on one-way tiles only and down is held, it drops
   through instead: `DropTimer` starts and no jump happens. Either way the buffer and coyote
   timers are cleared, so one press gives one jump.
4. **Jump cut.** Releasing jump while rising multiplies the upward speed by `JumpCut` once.
5. **Gravity,** capped at `MaxFallSpeed`.
6. **Move** with `MoveAndCollide`. One-way tiles are ignored while `DropTimer` runs.
   `SnapDown` applies only if the body was grounded, so jumps and drops never snap.
7. **Results.** Walls stop the horizontal speed and ceilings the upward speed. The body is
   grounded when it hit a floor while not moving up. `CoyoteTimer` is full while grounded and
   counts down in the air.

**How the cases work:**
- **Ground detection.** The move reports `HitBottom` when a floor stops the fall. Grounded
  bodies get a little gravity every step, so they stay in contact with the floor.
- **Slopes.** Slope tiles are floors under the box's **bottom center**, and they never block
  sideways or from below. The center decides, not the corners, so the box sinks into the slope
  by up to half its width. That's the usual look, and it means no jitter at tile edges.
  - **Up.** Walking up raises the floor by at most the distance walked (slopes are 45° at most),
    so the move lifts the box onto the surface.
  - **Down.** Walking down, `SnapDown` keeps the body on the surface instead of letting it fall
    in small hops.
  - **Top of a slope.** The box's front corner dips into the flat ground there. Walls that low
    don't stop a body standing on a slope, and the ground beside it can be stepped onto from a
    little below.
  - **Jumping along a slope.** The body's bottom is kept on the surface, so it lands on the
    slope rather than inside it.
- **One-way platforms.** They only stop a fall that starts above their top, so you jump up
  through them and land on them. `OnOneWay` is true only when nothing solid is under the box as
  well, and down + jump then drops through. If you don't press down, the jump is a normal one.
- **Coyote time.** The jump check uses "grounded, or `CoyoteTimer > 0`". The timer only counts
  down after the body has left the ground, so a late press just off a ledge still jumps.
- **Jump buffer.** The press is stored in `BufferTimer`, and the first step that is grounded
  with the timer still running jumps.
- **Determinism.** A step reads only the body, the input, the tunables and the map, and it is
  plain float math. Replaying the same inputs at the same fixed step gives the same path bit for
  bit. `PlatformerTests` replays a scripted 50 s run twice and compares every position's bits.
  Different builds and compilers may still round differently.

**Slopes in Tiled.** On a tile in the tileset, add float properties `slopeLeft` and `slopeRight`,
in pixels up from the tile's bottom, or set its Class to `slope`. Examples on 16 px tiles:
- `0` → `16`: 45°, rising to the right.
- `0` → `8` followed by `8` → `16`: 22.5° over two tiles.

Flip a cell horizontally (X in Tiled) to get the slope going down. The engine mirrors the
heights, so one tile serves both directions. Vertical and diagonal flips are ignored for
slopes: they are floors only, with no slopes on ceilings. `GetCollision` returns
`TileCollision::Slope` for them, `GetSlope(tile)` gives the heights after the flip, and
`GetSlopeFloorY(tile, x)` gives the surface. `DrawCollision` draws slopes as cyan lines. If
layers overlap, solid wins over slope and slope wins over one-way.

**`MoveAndCollide` options.** `TileMoveOptions{.IgnoreOneWay, .SnapDown}` are what the character
uses. The result's `OnSlope` and `OnOneWay` say what the floor is. Without options, moves behave
as before for maps without slopes.

**Entities.** `PlatformerBody` is a plain struct, so it can be a component as it is
(`entity.Add<PlatformerBody>(...)`). A system would also need each entity's input, so it is left
to the game. The phase 2 test game will add one if it needs several bodies.

**The sandbox's platformer scene.** It's in the title menu, after the entity swarm in the T
cycle, and behind `--platformer`. The level, `sandbox/assets/tilemaps/platformer.tmj` (96 x 22
tiles, from `tools/tilemaps/make_platformer.py`), has flat ground, 45° and 22.5° hills, three
one-way platforms stacked above the ground plus one more, a 4-tile gap for coyote time, and a
2-tile step for the jump buffer. Falling into the gap respawns you at the last checkpoint.

| Keyboard | PS4 / gamepad | Action |
|---|---|---|
| A / D, ← / → | d-pad, left stick | run |
| Space or Z (hold: higher) | Cross (South) | jump |
| S / ↓ + jump | d-pad down / stick down + Cross | drop through a one-way platform |
| R | Share (Back) | respawn at the checkpoint |
| O | | collision overlay |
| M | Options (Start) | pause |
| T | | next scene (camera demo) |

The scene shows:
- **HUD:** the grounded / slope / one-way / rising flags, the coyote, buffer and drop timers as
  bars, and counts of jumps, coyote jumps, buffered jumps and drops.
- **Popups** over the hero: "coyote jump!", "buffered jump!" and "drop through!", also logged.
- **Trail** of the feet over the last 3 s: green on the ground, yellow in coyote time, white in
  the air.
- **Tweaks** (debug-full, F1 shows the windows): every tunable is a `Tweak` in the Tweaks window
  ("Platformer feel": gravity, jump height, jump cut, run speed, accelerations, coyote time, jump
  buffer, snap down, drop-through time; "Platformer look": trail on/off and length, sky color).
  The jump is set by its height, so changing gravity keeps the jump as high and only changes how
  floaty it feels. Save keeps the values in `sandbox/assets/tweaks.json` (loaded at the next
  start); the scene's own ImGui section shows the resulting jump velocity and time to the top.
