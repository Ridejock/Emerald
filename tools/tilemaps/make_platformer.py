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
mirrors. Uses the palette and PNG writer of make_tilemaps.py; only the standard library.
"""

from make_tilemaps import OUT, PALETTE, T, dump_map, noise, stone, write_sheet
import json

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

FLIP_X = 0x80000000


def make_level():
    width, height = len(LEVEL[0]), len(LEVEL)
    assert all(len(row) == width for row in LEVEL), [len(r) for r in LEVEL]
    tiles = {"X": 6, "/": 2, "a": 3, "b": 4, "=": 5}
    flipped = {"\\": 2, "A": 3, "B": 4}  # down slopes: the up tiles mirrored
    ground, decor = [0] * (width * height), [0] * (width * height)

    def at(x, y):
        return LEVEL[y][x] if 0 <= x < width and 0 <= y < height else "X"

    objects = []
    for y in range(height):
        for x in range(width):
            ch, i = at(x, y), y * width + x
            if ch == "#":
                ground[i] = 1 + (0 if at(x, y - 1) in ".@k=" else 1)
                # A bush or flower on some open grass.
                n = noise(x, y, 14)
                if at(x, y - 1) == "." and n > 0.8:
                    decor[i - width] = 1 + (7 if n > 0.9 else 8)
            elif ch in tiles:
                ground[i] = 1 + tiles[ch]
            elif ch in flipped:
                ground[i] = (1 + flipped[ch]) | FLIP_X
            elif ch in "@k":
                name = "spawn" if ch == "@" else "checkpoint"
                objects.append((name, name, x * T + T / 2, (y + 1) * T, []))
                decor[i] = 1 + 9 if ch == "k" else 0
    for x, y, text in LABELS:
        objects.append(("label", "label", x * T, y * T, [("text", "string", text)]))

    def obj(oid, name, kind, x, y, props):
        o = {"height": 0, "id": oid, "name": name, "point": True, "rotation": 0, "type": kind,
             "visible": True, "width": 0, "x": x, "y": y}
        if props:
            o["properties"] = [{"name": n, "type": t, "value": v} for n, t, v in props]
        return o

    def tilelayer(lid, name, data, props=()):
        layer = {"data": data, "height": height, "id": lid, "name": name, "opacity": 1,
                 "type": "tilelayer", "visible": True, "width": width, "x": 0, "y": 0}
        if props:
            layer["properties"] = list(props)
        return layer

    return {
        "compressionlevel": -1,
        "height": height,
        "infinite": False,
        "layers": [
            tilelayer(1, "Ground", ground),
            # Bushes and flowers: drawn, never collide.
            tilelayer(2, "Decor", decor, [{"name": "collision", "type": "bool", "value": False}]),
            {"draworder": "topdown", "id": 3, "name": "Objects", "opacity": 1,
             "type": "objectgroup", "visible": True, "x": 0, "y": 0,
             "objects": [obj(i + 1, *o) for i, o in enumerate(objects)]},
        ],
        "nextlayerid": 4,
        "nextobjectid": len(objects) + 1,
        "orientation": "orthogonal",
        "properties": [{"name": "title", "type": "string", "value": "Platformer test level"}],
        "renderorder": "right-down",
        "tiledversion": "1.11.2",
        "tileheight": T,
        "tilesets": [{"firstgid": 1, "source": "platformer.tsj"}],
        "tilewidth": T,
        "type": "map",
        "version": "1.10",
        "width": width,
    }


def tileset():
    def prop(name, value):
        kind = "bool" if isinstance(value, bool) else "float"
        return {"name": name, "type": kind, "value": value}

    size = write_sheet(OUT / "platformer.png", [paint for paint, _ in TILES], 8)
    return {
        "columns": 8,
        "image": "platformer.png",
        "imageheight": size[1],
        "imagewidth": size[0],
        "margin": 0,
        "name": "platformer",
        "spacing": 0,
        "tilecount": len(TILES),
        "tiledversion": "1.11.2",
        "tileheight": T,
        "tiles": [
            {"id": i, "properties": [prop(n, v) for n, v in props.items()]}
            for i, (_, props) in enumerate(TILES)
            if props
        ],
        "tilewidth": T,
        "type": "tileset",
        "version": "1.10",
    }


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "platformer.tsj").write_text(json.dumps(tileset(), indent=1) + "\n")
    dump_map(OUT / "platformer.tmj", make_level())
    print("wrote", OUT / "platformer.tmj")


if __name__ == "__main__":
    main()
