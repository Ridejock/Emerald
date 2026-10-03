#!/usr/bin/env python3
"""Builds the sandbox's sample tilemap: tileset art, the tilesets and a hand-built room.

    python3 tools/tilemaps/make_tilemaps.py              # writes sandbox/assets/tilemaps/
    python3 tools/tilemaps/make_tilemaps.py --bench DIR  # also DIR/bench.tmj (500 x 500 tiles)

Outputs (all open in Tiled):
    dungeon.png + dungeon.tsj  the main tileset, an external tileset file; collision comes from
                               the tiles' custom bool properties "solid" and "oneway"
    props.png                  the props image, used by a tileset embedded in room.tmj
    room.tmj                   a 48 x 30 room drawn below as text (rerunning overwrites edits
                               made in Tiled)

The art is drawn in code with a fixed 16-color palette, so running the script again gives the
same bytes. Only the Python standard library is used.
"""

import json
import os
import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "sandbox" / "assets" / "tilemaps"
T = 16  # tile size in pixels

# Palette (PICO-8-ish, like tools/sprites/make_hero.py).
PALETTE = {
    "k": (26, 28, 44),  # outline, darkest
    "n": (41, 54, 111),  # deep water
    "b": (59, 93, 201),  # water
    "c": (65, 166, 246),  # water highlight
    "G": (37, 113, 121),  # dark grass, moss
    "g": (56, 183, 100),  # grass
    "l": (167, 240, 112),  # light grass, leaves
    "1": (51, 60, 87),  # stone dark
    "2": (86, 108, 134),  # stone
    "3": (148, 176, 194),  # stone light
    "w": (93, 39, 93),  # brick shadow
    "B": (107, 62, 46),  # wood dark
    "o": (239, 125, 87),  # wood light, flame
    "y": (255, 205, 117),  # gold, light
    "r": (177, 62, 83),  # red
    "W": (244, 244, 244),  # white
}


def noise(x, y, seed):
    """A repeatable 0..1 value per pixel."""
    h = (x * 73856093) ^ (y * 19349663) ^ (seed * 83492791)
    h = (h ^ (h >> 13)) * 1274126177
    return ((h ^ (h >> 16)) & 0xFFFF) / 65535.0


# --- Tiles: each a function (x, y) -> palette key, or None for transparent ---------------------


def stone(seed, moss=False, crack=False):
    def px(x, y):
        # 8 x 8 flagstones with dark joints, lighter top-left edges.
        if x % 8 == 7 or y % 8 == 7:
            return "1"
        if crack and (x - y in (1, 2) and 2 < x < 13):
            return "1"
        if moss and noise(x // 2, y // 2, seed) > 0.6:
            return "G"
        if x % 8 == 0 or y % 8 == 0:
            return "3"
        return "2" if noise(x, y, seed) > 0.15 else "3"

    return px


def grass(seed, flowers=False):
    def px(x, y):
        if flowers and (x, y) in ((3, 4), (11, 2), (7, 11), (13, 12)):
            return "y" if (x + y) % 2 else "r"
        n = noise(x, y, seed)
        return "l" if n > 0.85 else ("G" if n < 0.2 else "g")

    return px


def dirt(x, y):
    n = noise(x, y, 7)
    return "o" if n > 0.9 else ("w" if n < 0.15 else "B")


def plank(x, y):
    if y % 4 == 3:
        return "B"
    return "o" if not (x in (3, 12) and y % 4 == 1) else "B"  # nail heads


def stairs(x, y):
    step = y // 4
    return "3" if y % 4 == 0 else ("2" if step % 2 == 0 else "1")


def wall_top(x, y):
    if y < 2:
        return "3"
    return "1" if (x + y) % 5 == 0 else "w"


def wall_face(x, y):
    # Bricks: rows of 4, offset every other row.
    row = y // 4
    if y % 4 == 3 or (x + (4 if row % 2 else 0)) % 8 == 7:
        return "k"
    return "r" if noise(x // 8, row, 3) > 0.5 else "w"


def water(x, y):
    wave = (x + 2 * (y // 4)) % 8
    if y % 4 == 1 and wave < 3:
        return "c"
    return "b" if (y // 4) % 2 == 0 else "n"


def rail(x, y):
    # A wooden railing over stone: posts and a top bar, the "platform top" of a one-way tile.
    base = stone(5)(x, y)
    if y in (1, 2):
        return "o" if y == 1 else "B"
    if x % 8 in (2, 3) and y < 12:
        return "B" if x % 8 == 3 else "o"
    return base


def pillar_base(x, y):
    if 3 <= x <= 12:
        if x == 3 or x == 12 or y >= 14:
            return "k"
        return "3" if x < 6 else "2"
    return stone(9)(x, y)


def pillar_top(x, y):
    if 3 <= x <= 12 and y >= 4:
        if x == 3 or x == 12:
            return "k"
        return "3" if x < 6 else "2"
    if 1 <= x <= 14 and y < 4:
        return "k" if y == 0 or x in (1, 14) else "3"
    return None


def arrow(x, y):
    # Points right, with a notch at the top-left: every flip and turn looks different.
    if (x, y) in ((2, 2), (3, 2), (2, 3)):
        return "y"
    if 3 <= y <= 12 and x <= 9 and 6 <= y <= 9 and x >= 2:
        return "W"
    if x >= 9 and abs(y - 7.5) <= (14 - x) and x <= 14:
        return "W"
    return None


def banner(x, y):
    if 4 <= x <= 11 and y <= 12:
        if y == 12 or x in (4, 11):
            return "k"
        return "y" if 6 <= x <= 9 and 3 <= y <= 8 else "n"
    return None


DUNGEON = [
    # (painter, collision) in tile index order; 8 columns.
    (stone(1), None),  # 0 floor
    (stone(2, crack=True), None),  # 1 cracked floor
    (stone(3, moss=True), None),  # 2 mossy floor
    (grass(4), None),  # 3 grass
    (grass(5, flowers=True), None),  # 4 flowers
    (dirt, None),  # 5 dirt path
    (plank, None),  # 6 bridge planks
    (stairs, None),  # 7 stairs
    (wall_top, "solid"),  # 8 wall top
    (wall_face, "solid"),  # 9 wall face
    (water, "solid"),  # 10 water
    (rail, "oneway"),  # 11 railing: one-way
    (pillar_base, "solid"),  # 12 pillar base
    (pillar_top, None),  # 13 pillar top (overhead)
    (arrow, None),  # 14 arrow (decor; shows flips)
    (banner, None),  # 15 banner (decor)
]


def crate(x, y):
    if x in (1, 14) or y in (1, 14):
        return "k"
    if x in (2, 13) or y in (2, 13) or x == y or x == 15 - y:
        return "B"
    return "o" if 0 < x < 15 and 0 < y < 15 else None


def barrel(x, y):
    if not 2 <= x <= 13 or not 1 <= y <= 14:
        return None
    if x in (2, 13) or y in (1, 14):
        return "k"
    if y in (4, 11):
        return "1"
    return "o" if x < 6 else "B"


def torch(x, y):
    if 7 <= x <= 8 and 7 <= y <= 14:
        return "B"
    if 5 <= x <= 10 and 1 <= y <= 6 and abs(x - 7.5) <= (y + 1) / 2:
        return "y" if 6 <= x <= 9 and y >= 3 else "o"
    return None


def plant(x, y):
    if 4 <= x <= 11 and 10 <= y <= 14:
        return "k" if x in (4, 11) or y == 14 else "r"
    if y < 10 and abs(x - 7.5) + abs(y - 5) < 6:
        return "l" if noise(x, y, 11) > 0.6 else "g"
    return None


def chest(x, y):
    if not 1 <= x <= 14 or not 3 <= y <= 14:
        return None
    if x in (1, 14) or y in (3, 14) or y == 7:
        return "k"
    if 7 <= x <= 8 and 6 <= y <= 9:
        return "y"
    return "o" if y < 7 else "B"


def sign(x, y):
    if 7 <= x <= 8 and y >= 9:
        return "B"
    if 2 <= x <= 13 and 2 <= y <= 9:
        if x in (2, 13) or y in (2, 9):
            return "k"
        return "1" if y in (4, 6) and 4 <= x <= 11 else "o"
    return None


def bones(x, y):
    if (x - 4) ** 2 + (y - 9) ** 2 < 8:
        return "W" if (x, y) not in ((3, 9), (5, 9)) else "k"
    if 7 <= x <= 13 and y in (11, 12):
        return "3"
    return None


def rug(x, y):
    if x in (0, 15) or y in (0, 15):
        return "y"
    return "r" if (x // 4 + y // 4) % 2 else "w"


PROPS = [
    (crate, "solid", None),
    (barrel, None, "solid"),  # solid through its class, to show both ways
    (torch, None, None),
    (plant, "solid", None),
    (chest, "solid", None),
    (sign, None, None),
    (bones, None, None),
    (rug, None, None),
]

# --- PNG ---------------------------------------------------------------------------------------


def write_png(path, width, height, rows):
    """rows: lists of (r, g, b, a) per pixel."""
    raw = b"".join(b"\x00" + bytes(c for px in row for c in px) for row in rows)

    def chunk(kind, data):
        return (
            struct.pack(">I", len(data))
            + kind
            + data
            + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
        )

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9))
    png += chunk(b"IEND", b"")
    path.write_bytes(png)


def write_sheet(path, painters, columns):
    rows_of_tiles = (len(painters) + columns - 1) // columns
    w, h = columns * T, rows_of_tiles * T
    rows = [[(0, 0, 0, 0)] * w for _ in range(h)]
    for i, paint in enumerate(painters):
        ox, oy = (i % columns) * T, (i // columns) * T
        for y in range(T):
            for x in range(T):
                key = paint(x, y)
                if key is not None:
                    rows[oy + y][ox + x] = PALETTE[key] + (255,)
    write_png(path, w, h, rows)
    return w, h


# --- Tiled JSON --------------------------------------------------------------------------------


def bool_prop(name):
    return {"name": name, "type": "bool", "value": True}


def tileset_json(name, image, size, count, tiles):
    return {
        "columns": 8 if count >= 8 else count,
        "image": image,
        "imageheight": size[1],
        "imagewidth": size[0],
        "margin": 0,
        "name": name,
        "spacing": 0,
        "tilecount": count,
        "tileheight": T,
        "tiles": tiles,
        "tilewidth": T,
    }


def dungeon_tiles():
    return [
        {"id": i, "properties": [bool_prop(kind)]}
        for i, (_, kind) in enumerate(DUNGEON)
        if kind
    ]


def props_tiles():
    tiles = []
    for i, (_, prop, cls) in enumerate(PROPS):
        tile = {"id": i}
        if prop:
            tile["properties"] = [bool_prop(prop)]
        if cls:
            tile["type"] = cls
        if len(tile) > 1:
            tiles.append(tile)
    return tiles


def dump_map(path, m):
    """Like Tiled's output, but with one row of tiles per line so diffs stay readable."""
    rows = {}
    for layer in m["layers"]:
        if layer["type"] == "tilelayer":
            w, data = m["width"], layer["data"]
            lines = (", ".join(map(str, data[r * w : (r + 1) * w])) for r in range(m["height"]))
            rows[layer["id"]] = "[\n" + ",\n".join("    " + line for line in lines) + "]"
            layer["data"] = "@data%d@" % layer["id"]
    text = json.dumps(m, indent=1)
    for lid, nice in rows.items():
        text = text.replace('"@data%d@"' % lid, nice)
    path.write_text(text + "\n")


# --- The room ----------------------------------------------------------------------------------

# '#' wall  '.' floor  'g' grass  'f' flowers  ':' dirt  '~' water  '=' bridge  '>' stairs
# '-' railing (one-way: only stops moving down)  'o' pillar  '@' spawn
# props: c crate, b barrel, t torch, p plant, C chest, s sign, x bones, R rug
# 0..7: the arrow tile with those flip bits (X = 1, Y = 2, diagonal = 4)
ROOM = [
    "################################################",
    "#t.....#.......o.......o.....#t.........#......#",
    "#......#.....................#.............C...#",
    "#..cc..#..gggg.....ggfggggg..#....RR....#......#",
    "#..cb..#..ggfggg..gggggggg...#....RR....#......#",
    "#......#......................------------..x..#",
    "#..p...--.....:::::::::::::::..........:.......#",
    "#......#......:..............:...o.....:...o...#",
    "####-###......:..~~~~~~~~~~..:.........:.......#",
    "#.............:..~~~~==~~~~..:.........:.......#",
    "#..o.....o....:..~~~~==~~~~..:...o.....:...o...#",
    "#.............:..~~~~==~~~~..:.........:.......#",
    "#.............:..~~~~==~~~~..::::::::::::......#",
    "#.............:......::......:.................#",
    "#..01234567...::::::::@:::::::........####-#####",
    "#.............:..............s........#........#",
    "#.............:.......................#..b..b..#",
    "#####..########...gggggggg......p.....#........#",
    "#.................ggfgggfg............#..C.....#",
    "#.................gggggggg............#........#",
    "#..x.....o......................o.....#....>...#",
    "#.............................................t#",
    "#......####################-------##############",
    "#......#.......................................#",
    "#......#....b......c...............x...........#",
    "#..p...#.......................................#",
    "#......#..........o.........o.........o........#",
    "#......#.......................................#",
    "#t.....#......................................t#",
    "################################################",
]

def make_room():
    width, height = len(ROOM[0]), len(ROOM)
    assert all(len(row) == width for row in ROOM), [len(r) for r in ROOM]
    props_first = 1 + len(DUNGEON)  # firstgid of the embedded props tileset
    prop_index = {"c": 0, "b": 1, "t": 2, "p": 3, "C": 4, "s": 5, "x": 6, "R": 7}
    ground, walls, decor, overhead = ([0] * (width * height) for _ in range(4))

    def at(x, y):
        return ROOM[y][x] if 0 <= x < width and 0 <= y < height else "#"

    for y in range(height):
        for x in range(width):
            ch, i = at(x, y), y * width + x
            n = noise(x, y, 42)
            floor = 0 if n < 0.75 else (1 if n < 0.87 else 2)
            ground[i] = floor + 1
            if ch == "#":
                walls[i] = 1 + (9 if at(x, y + 1) != "#" else 8)
            elif ch == "g":
                ground[i] = 1 + 3
            elif ch == "f":
                ground[i] = 1 + 4
            elif ch in ":@":
                ground[i] = 1 + 5
            elif ch == "~":
                ground[i] = 1 + 10
            elif ch == "=":
                ground[i] = 1 + 6
            elif ch == ">":
                ground[i] = 1 + 7
            elif ch == "-":
                walls[i] = 1 + 11
            elif ch == "o":
                walls[i] = 1 + 12
                overhead[i - width] = 1 + 13  # its top, drawn over the hero
            elif ch in "01234567":
                bits = int(ch)
                decor[i] = (1 + 14) | (
                    (0x80000000 if bits & 1 else 0)
                    | (0x40000000 if bits & 2 else 0)
                    | (0x20000000 if bits & 4 else 0)
                )
            elif ch in prop_index:
                decor[i] = props_first + prop_index[ch]
    # Banners on a few faces of the top wall.
    for x in (11, 19, 35):
        overhead[x] = 1 + 15

    # Objects: what the game reads from the map.
    oid = iter(range(1, 100))

    def obj(name, kind, x, y, w=0, h=0, props=(), **extra):
        o = {
            "height": h,
            "id": next(oid),
            "name": name,
            "rotation": 0,
            "type": kind,
            "visible": True,
            "width": w,
            "x": x,
            "y": y,
        }
        if props:
            o["properties"] = list(props)
        o.update(extra)
        return o

    def p(name, kind, value):
        return {"name": name, "type": kind, "value": value}

    def find(ch):
        for y, row in enumerate(ROOM):
            if ch in row:
                return row.index(ch), y
        raise KeyError(ch)

    sx, sy = find("@")
    signx, signy = find("s")
    objects = [
        obj("spawn", "spawn", sx * T + 8, sy * T + 8, point=True),
        obj(
            "welcome",
            "sign",
            signx * T - 8,
            signy * T - 8,
            32,
            32,
            [p("text", "string", "Welcome! Walk with WASD. Railings only block moving down.")],
        ),
        obj(
            "arrows",
            "sign",
            3 * T,
            13 * T,
            8 * T,
            2 * T,
            [p("text", "string", "Tiled flip bits 0..7: X = 1, Y = 2, diagonal = 4")],
        ),
        obj(
            "chest",
            "chest",
            43 * T,
            1 * T,
            3 * T,
            3 * T,
            [p("gold", "int", 25), p("contents", "string", "a rusty key")],
        ),
        obj("pond", "area", 17 * T, 8 * T, 10 * T, 4 * T, [p("depth", "float", 1.5)], ellipse=True),
        obj(
            "stairs",
            "exit",
            43 * T,
            19 * T,
            T,
            2 * T,
            [p("target", "string", "the 500 x 500 benchmark map (--map bench)")],
        ),
    ]
    def tilelayer(lid, name, data, props=()):
        layer = {
            "data": data,
            "height": height,
            "id": lid,
            "name": name,
            "opacity": 1,
            "type": "tilelayer",
            "visible": True,
            "width": width,
            "x": 0,
            "y": 0,
        }
        if props:
            layer["properties"] = list(props)
        return layer

    return {
        "compressionlevel": -1,
        "height": height,
        "infinite": False,
        "layers": [
            tilelayer(1, "Ground", ground),
            tilelayer(2, "Walls", walls),
            tilelayer(3, "Decor", decor),
            {
                "draworder": "topdown",
                "id": 4,
                "name": "Objects",
                "objects": objects,
                "opacity": 1,
                "type": "objectgroup",
                "visible": True,
                "x": 0,
                "y": 0,
            },
            # Drawn after the hero; never collides.
            tilelayer(5, "Overhead", overhead, [p("collision", "bool", False)]),
        ],
        "nextlayerid": 6,
        "nextobjectid": len(objects) + 1,
        "orientation": "orthogonal",
        "properties": [p("title", "string", "Sample room")],
        "renderorder": "right-down",
        "tiledversion": "1.11.2",
        "tileheight": T,
        "tilesets": [
            {"firstgid": 1, "source": "dungeon.tsj"},
            dict(
                {"firstgid": props_first},
                **tileset_json("props", "props.png", (8 * T, T), len(PROPS), props_tiles()),
            ),
        ],
        "tilewidth": T,
        "type": "map",
        "version": "1.10",
        "width": width,
    }


def make_bench(out_dir, size=500):
    """A big generated map for the renderer benchmark: three full-size tile layers."""
    props_first = 1 + len(DUNGEON)
    ground, walls, decor = [], [], []
    for y in range(size):
        for x in range(size):
            # Patches of water and grass, wall lines every 25 tiles, props here and there.
            n = noise(x // 6, y // 6, 1)
            floor = 10 if n < 0.12 else (3 if n < 0.4 else int(noise(x, y, 2) * 3))
            ground.append(1 + floor)
            edge = x in (0, size - 1) or y in (0, size - 1)
            walls.append(1 + 8 if edge or (x % 25 == 0 and y % 7 != 0) else 0)
            prop = props_first + int(noise(x, y, 3) * 8)
            decor.append(prop if noise(x, y, 4) > 0.9 else 0)

    def relative(path):  # the bench map points back at the sandbox's tileset files
        return os.path.relpath(path, out_dir).replace("\\", "/")

    def layer(lid, name, data):
        return {
            "data": data,
            "height": size,
            "id": lid,
            "name": name,
            "opacity": 1,
            "type": "tilelayer",
            "visible": True,
            "width": size,
            "x": 0,
            "y": 0,
        }

    spawn = {"id": 1, "name": "spawn", "type": "spawn", "point": True}
    spawn.update(x=size * T / 2, y=size * T / 2, width=0, height=0)
    props = tileset_json("props", relative(OUT / "props.png"), (8 * T, T), len(PROPS), props_tiles())
    m = {
        "height": size,
        "infinite": False,
        "layers": [
            layer(1, "Ground", ground),
            layer(2, "Walls", walls),
            layer(3, "Decor", decor),
            {"id": 4, "name": "Objects", "type": "objectgroup", "objects": [spawn]},
        ],
        "orientation": "orthogonal",
        "renderorder": "right-down",
        "tileheight": T,
        "tilesets": [
            {"firstgid": 1, "source": relative(OUT / "dungeon.tsj")},
            dict({"firstgid": props_first}, **props),
        ],
        "tilewidth": T,
        "type": "map",
        "width": size,
    }
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "bench.tmj").write_text(json.dumps(m, separators=(",", ":")))
    print("wrote", out_dir / "bench.tmj")


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    size = write_sheet(OUT / "dungeon.png", [paint for paint, _ in DUNGEON], 8)
    (OUT / "dungeon.tsj").write_text(
        json.dumps(
            dict(
                tileset_json("dungeon", "dungeon.png", size, len(DUNGEON), dungeon_tiles()),
                type="tileset",
                version="1.10",
                tiledversion="1.11.2",
            ),
            indent=1,
        )
        + "\n"
    )
    write_sheet(OUT / "props.png", [paint for paint, _, _ in PROPS], 8)
    dump_map(OUT / "room.tmj", make_room())
    print("wrote", OUT)
    if "--bench" in sys.argv:
        make_bench(Path(sys.argv[sys.argv.index("--bench") + 1]).resolve())


if __name__ == "__main__":
    main()
