r"""Text drawings to Tiled maps (.tmj): helpers shared by the map scripts of Emerald games.

Draw a level as rows of characters, say what each character becomes, and write a map that opens
in Tiled and loads with Emerald::Tilemap:

    import sys
    sys.path.insert(0, "path/to/Emerald/tools/tilemaps")
    from textmap import TextMap, FLIP_X, dump_map, make_map, object_layer, point_object, tile_layer

    level = TextMap([
        "#........#",
        "#..@.../##",
        "##########",
    ], outside="#")                       # off the map counts as '#' (for neighbour checks)

    def ground(m, x, y):                  # a function: the tile from the neighbours
        if m.is_open(x, y - 1, "#/"):
            return 1                      # grass on top
        return m.pick([2, 3, 4], x, y)    # inner ground: one of three variants, by cell hash

    spawn = level.find("@")[0]            # (x, y) of the first '@'
    legend = {"#": ground, "/": 5, "\\": 5 | FLIP_X, "=": [6, 7]}  # GIDs; a list = variants
    layers = [
        tile_layer(1, "Ground", level.cells(legend), level.width, level.height),
        object_layer(2, "Objects", [point_object(1, "spawn", *level.feet(*spawn, 16))]),
    ]
    dump_map("level.tmj", make_map(level.width, level.height, 16, layers,
                                   [{"firstgid": 1, "source": "tiles.tsj"}]))

Variants are picked by a hash of the cell (noise), so rerunning gives the same map and changing
one part of the drawing leaves the rest alone. Only the Python standard library is used.
"""

import json
from pathlib import Path

# Tiled's flip bits on a GID (the engine mirrors such tiles).
FLIP_X = 0x80000000
FLIP_Y = 0x40000000
FLIP_D = 0x20000000  # anti-diagonal (with X or Y: rotations by 90 degrees)


def noise(x, y, seed):
    """A repeatable 0..1 value per (x, y, seed): the same as the sandbox's art scripts use."""
    h = (x * 73856093) ^ (y * 19349663) ^ (seed * 83492791)
    h = (h ^ (h >> 13)) * 1274126177
    return ((h ^ (h >> 16)) & 0xFFFF) / 65535.0


def pick(variants, x, y, seed=0):
    """One of `variants` for cell (x, y), repeatably. Items can be (value, weight) pairs to make
    some variants rarer: [(1, 8), (2, 1)] gives tile 2 about one cell in nine."""
    pairs = [v if isinstance(v, tuple) else (v, 1) for v in variants]
    total = sum(w for _, w in pairs)
    n = noise(x, y, seed) * total * 0.99999  # < total, so the last variant can be picked too
    for value, weight in pairs:
        if n < weight:
            return value
        n -= weight
    return pairs[-1][0]


class TextMap:
    """A level drawn as equally long rows of characters; (x, y) = (column, row), y down."""

    def __init__(self, rows, outside="."):
        self.rows = list(rows)
        self.width = len(self.rows[0]) if self.rows else 0
        self.height = len(self.rows)
        self.outside = outside  # what at() answers off the map
        bad = [i for i, row in enumerate(self.rows) if len(row) != self.width]
        if bad:
            raise ValueError("rows %s are not %d characters long" % (bad, self.width))

    def at(self, x, y):
        """The character at (x, y), or `outside` off the map."""
        if 0 <= x < self.width and 0 <= y < self.height:
            return self.rows[y][x]
        return self.outside

    def is_open(self, x, y, solid):
        """True if (x, y) is not one of the characters in `solid`."""
        return self.at(x, y) not in solid

    def find(self, ch):
        """Every (x, y) holding `ch`, row by row."""
        return [(x, y) for y, row in enumerate(self.rows) for x, c in enumerate(row) if c == ch]

    def mask(self, x, y, chars):
        """Which of the four neighbours are in `chars`, for autotiling: 1 up, 2 right, 4 down,
        8 left (so 15 = surrounded, 0 = alone)."""
        bits = 0
        for bit, (dx, dy) in ((1, (0, -1)), (2, (1, 0)), (4, (0, 1)), (8, (-1, 0))):
            if self.at(x + dx, y + dy) in chars:
                bits |= bit
        return bits

    def pick(self, variants, x, y, seed=0):
        """pick() for a cell of this map."""
        return pick(variants, x, y, seed)

    def cells(self, legend, seed=0):
        """Tile layer data (a GID per cell, row by row) from a legend: character -> GID, a list
        of GIDs (variants, see pick), or a function (map, x, y) -> GID. Characters not in the
        legend, and None, give 0 (empty)."""
        data = []
        for y in range(self.height):
            for x in range(self.width):
                rule = legend.get(self.rows[y][x], 0)
                if callable(rule):
                    rule = rule(self, x, y)
                elif isinstance(rule, list):
                    rule = pick(rule, x, y, seed)
                data.append(rule or 0)
        return data

    @staticmethod
    def feet(x, y, tile):
        """Pixel position of the middle of the cell's bottom edge (where a character stands)."""
        return (x * tile + tile / 2, (y + 1) * tile)

    @staticmethod
    def center(x, y, tile):
        """Pixel position of the cell's center."""
        return (x * tile + tile / 2, y * tile + tile / 2)


# --- Tiled JSON pieces (keys in the order the sandbox's maps already use) ---------------------


def prop(name, value, kind=None):
    """A Tiled custom property; the type follows the Python value unless given."""
    if kind is None:
        kind = {bool: "bool", int: "int", float: "float"}.get(type(value), "string")
    return {"name": name, "type": kind, "value": value}


def tile_layer(layer_id, name, data, width, height, properties=()):
    layer = {"data": data, "height": height, "id": layer_id, "name": name, "opacity": 1,
             "type": "tilelayer", "visible": True, "width": width, "x": 0, "y": 0}
    if properties:
        layer["properties"] = list(properties)
    return layer


def point_object(object_id, name, x, y, kind=None, properties=()):
    """A point object (`kind` is its Tiled class, the name by default)."""
    o = {"height": 0, "id": object_id, "name": name, "point": True, "rotation": 0,
         "type": name if kind is None else kind, "visible": True, "width": 0, "x": x, "y": y}
    if properties:
        o["properties"] = list(properties)
    return o


def rect_object(object_id, name, x, y, width, height, kind=None, properties=()):
    """A rectangle object, e.g. a trigger area."""
    o = point_object(object_id, name, x, y, kind, properties)
    del o["point"]
    o.update(width=width, height=height)
    return o


def object_layer(layer_id, name, objects):
    return {"draworder": "topdown", "id": layer_id, "name": name, "opacity": 1,
            "type": "objectgroup", "visible": True, "x": 0, "y": 0, "objects": list(objects)}


def tileset(name, image, image_size, tile, count, columns, tiles=()):
    """An external tileset (.tsj): `tiles` are {"id": i, "properties": [...]} entries."""
    return {"columns": columns, "image": image, "imageheight": image_size[1],
            "imagewidth": image_size[0], "margin": 0, "name": name, "spacing": 0,
            "tilecount": count, "tiledversion": "1.11.2", "tileheight": tile,
            "tiles": list(tiles), "tilewidth": tile, "type": "tileset", "version": "1.10"}


def make_map(width, height, tile, layers, tilesets, properties=()):
    """A whole orthogonal map. Object ids should be unique; nextobjectid follows the largest."""
    ids = [o["id"] for layer in layers for o in layer.get("objects", [])]
    m = {"compressionlevel": -1, "height": height, "infinite": False, "layers": list(layers),
         "nextlayerid": max([layer["id"] for layer in layers], default=0) + 1,
         "nextobjectid": max(ids, default=0) + 1, "orientation": "orthogonal"}
    if properties:
        m["properties"] = list(properties)
    m.update({"renderorder": "right-down", "tiledversion": "1.11.2", "tileheight": tile,
              "tilesets": list(tilesets), "tilewidth": tile, "type": "map", "version": "1.10",
              "width": width})
    return m


def dump_map(path, m):
    """Writes the map like Tiled does, but with one row of tiles per line so diffs stay
    readable."""
    rows, saved = {}, {}
    for layer in m["layers"]:
        if layer["type"] == "tilelayer":
            saved[layer["id"]] = layer["data"]
            w, data = layer["width"], layer["data"]
            lines = (", ".join(map(str, data[r * w : (r + 1) * w])) for r in range(layer["height"]))
            rows[layer["id"]] = "[\n" + ",\n".join("    " + line for line in lines) + "]"
            layer["data"] = "@data%d@" % layer["id"]
    text = json.dumps(m, indent=1)
    for layer_id, nice in rows.items():
        text = text.replace('"@data%d@"' % layer_id, nice)
    Path(path).write_text(text + "\n")
    for layer in m["layers"]:  # leave the map as it was
        if layer["id"] in saved:
            layer["data"] = saved[layer["id"]]
