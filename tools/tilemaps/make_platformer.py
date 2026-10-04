#!/usr/bin/env python3
"""Builds the sandbox's platformer level: a side-view tileset with slopes and its test level.

    python3 tools/tilemaps/make_platformer.py   # writes sandbox/assets/tilemaps/platformer.*

Outputs (all open in Tiled):
    platformer.png + platformer.tsj  the tileset; collision from the tiles' properties: bool
                                     "solid" / "oneway", float "slopeLeft" / "slopeRight"
                                     (floor heights in pixels at the tile's edges)
    platformer.tmj                   the level drawn below as text: flat ground, 45 and 22.5
                                     degree slopes up and down, one-way platforms, a gap for
                                     coyote time and a step for the jump buffer

Down slopes are the up-slope tiles flipped horizontally (Tiled's X flip bit), which the engine
mirrors. Uses the palette and PNG writer of make_tilemaps.py and the text map helpers of textmap.py;
only the standard library.
"""

import json

from make_tilemaps import OUT, T, stone, write_sheet
from textmap import (FLIP_X, TextMap, dump_map, make_map, noise, object_layer, point_object,
                     prop, tile_layer, tileset)

# --- Tiles: (x, y) -> palette key or None; y = 0 is the top row of the tile ---------------------


def dirt(x, y):
    n = noise(x, y, 11)
    return "w" if n < 0.18 else ("k" if n > 0.94 else "B")


def grass_top(x, y):
    """Ground with grass along its top edge."""
    if y == 0:
        return "l"
    if y < 4 - (noise(x, 0, 12) > 0.5):
        return "g"
    return "G" if y == 4 else dirt(x, y)


def slope(h0, h1):
    """A filled slope: the floor rises from h0 px (left edge) to h1 px (right edge)."""

    def px(x, y):
        surface = T - (h0 + (h1 - h0) * (x + 0.5) / T)  # y of the floor at this column
        if y < surface - 0.5:
            return None
        depth = y - surface
        if depth < 1:
            return "l"
        return "g" if depth < 3 else ("G" if depth < 4 else dirt(x, y))

    return px


def plank(x, y):
    """A one-way platform: a wooden board with two posts."""
    if y == 0:
        return "y"
    if y < 4:
        return "o" if not (x in (2, 13) and y == 2) else "B"
    if y == 4:
        return "B"
    return "B" if x in (2, 3, 12, 13) and y < 9 else None


def bush(x, y):
    dx, dy = x - 7.5, y - 12
    if dx * dx + dy * dy * 2 > 48 or y > 15:
        return None
    return "l" if noise(x, y, 13) > 0.8 else ("G" if dy > 1 else "g")


def flower(x, y):
    if x == 7 and 9 <= y <= 15:
        return "G"
    if (x, y) in ((6, 7), (8, 7), (7, 6), (7, 8)):
        return "r"
    return "y" if (x, y) == (7, 7) else None


def sign(x, y):
    if 7 <= x <= 8 and y >= 9:
        return "B"
    if 2 <= x <= 13 and 3 <= y <= 9:
        return "k" if y in (3, 9) or x in (2, 13) else ("y" if y == 6 and 4 <= x <= 11 else "o")
    return None


TILES = [
    # (painter, properties) in tile index order
    (grass_top, {"solid": True}),  # 0 ground, grass on top
    (dirt, {"solid": True}),  # 1 ground
    (slope(0, 16), {"slopeLeft": 0.0, "slopeRight": 16.0}),  # 2 45 degrees
    (slope(0, 8), {"slopeLeft": 0.0, "slopeRight": 8.0}),  # 3 22.5 degrees, lower half
    (slope(8, 16), {"slopeLeft": 8.0, "slopeRight": 16.0}),  # 4 22.5 degrees, upper half
    (plank, {"oneway": True}),  # 5 one-way platform
    (stone(6), {"solid": True}),  # 6 stone block
    (bush, {}),  # 7 decor
    (flower, {}),  # 8 decor
    (sign, {}),  # 9 decor
]

# --- The level -----------------------------------------------------------------------------------

# '#' ground  'X' stone  '/' 45 degree slope up  '\' down  'a' 'b' 22.5 degrees up (two tiles)
# 'B' 'A' 22.5 degrees down  '=' one-way platform  '@' spawn  'k' checkpoint
LEVEL = [
    "X..............................................................................................X",
    "X..............................................................................................X",
    "X..............................................................................................X",
    "X..............................................................................................X",
    "X..............................................................................................X",
    "X..............................................................................................X",
    "X..............................................................................................X",
    "X..............................................................................................X",
    "X..........................................====................................................X",
    "X..............................................................................................X",
    "X..............................................................................................X",
    "X..............................................====............................................X",
    "X..............................................................................................X",
    "X..............................................................................................X",
    "X.........../#####\\.......................====......====.......................................X",
    "X........../#######\\........ab####BA.....................................XXXX\\.................X",
    "X...@...../#########\\.....ab########BA...................k...............XXXX#\\................X",
    "X###########################################################....###############################X",
    "X###########################################################....###############################X",
    "X###########################################################....###############################X",
    "X###########################################################....###############################X",
    "X###########################################################....###############################X",
]

# Signs above each part of the level: (tile x, tile y, text).
LABELS = [
    (12, 10, "45 degree slopes"),
    (29, 11, "22.5 degree slopes"),
    (44, 5, "one-way: jump up, down + jump drops"),
    (58, 12, "gap: coyote time"),
    (72, 11, "step: press jump early (buffer)"),
]

def ground(m, x, y):
    """'#': grass where the cell above is open, plain ground below."""
    return 1 + (0 if m.at(x, y - 1) in ".@k=" else 1)


def decor(m, x, y):
    """Open cells: a bush or flower on some grass below, a sign at the checkpoint."""
    if m.at(x, y) == "k":
        return 1 + 9
    n = noise(x, y + 1, 14)
    if m.at(x, y + 1) == "#" and n > 0.8:
        return 1 + (7 if n > 0.9 else 8)
    return 0


# GIDs (firstgid 1 + tile index); down slopes are the up tiles mirrored.
GROUND = {"#": ground, "X": 1 + 6, "/": 1 + 2, "a": 1 + 3, "b": 1 + 4, "=": 1 + 5,
          "\\": (1 + 2) | FLIP_X, "A": (1 + 3) | FLIP_X, "B": (1 + 4) | FLIP_X}
DECOR = {".": decor, "k": decor}


def make_level():
    level = TextMap(LEVEL, outside="X")
    objects = []
    for y in range(level.height):
        for x in range(level.width):
            if level.at(x, y) in "@k":
                name = "spawn" if level.at(x, y) == "@" else "checkpoint"
                objects.append((name, *level.feet(x, y, T), []))
    for x, y, text in LABELS:
        objects.append(("label", x * T, y * T, [prop("text", text)]))

    w, h = level.width, level.height
    layers = [
        tile_layer(1, "Ground", level.cells(GROUND), w, h),
        # Bushes and flowers: drawn, never collide.
        tile_layer(2, "Decor", level.cells(DECOR), w, h, [prop("collision", False)]),
        object_layer(3, "Objects", [point_object(i + 1, name, x, y, properties=props)
                                    for i, (name, x, y, props) in enumerate(objects)]),
    ]
    return make_map(w, h, T, layers, [{"firstgid": 1, "source": "platformer.tsj"}],
                    [prop("title", "Platformer test level")])


def platformer_tileset():
    size = write_sheet(OUT / "platformer.png", [paint for paint, _ in TILES], 8)
    tiles = [{"id": i, "properties": [prop(n, v) for n, v in props.items()]}
             for i, (_, props) in enumerate(TILES) if props]
    return tileset("platformer", "platformer.png", size, T, len(TILES), 8, tiles)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "platformer.tsj").write_text(json.dumps(platformer_tileset(), indent=1) + "\n")
    dump_map(OUT / "platformer.tmj", make_level())
    print("wrote", OUT / "platformer.tmj")


if __name__ == "__main__":
    main()
