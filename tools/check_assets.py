#!/usr/bin/env python3
"""Checks a game's assets before they reach the engine, e.g. in CI:

    python3 tools/check_assets.py assets/                          # atlases and Tiled maps
    python3 tools/check_assets.py assets/ --palette art/palette.gpl  # and the PNGs' colors
    python3 tools/check_assets.py assets/ --exclude "fonts/*"      # skip some files

Every file under the given folders (or each given file) is checked by its kind:
    *.png   with --palette: every pixel is fully transparent, or opaque in a palette color
            (a GIMP .gpl palette, as Aseprite and GIMP export it)
    *.json  texture atlases (the engine's plain {"name": {x, y, w, h}} or TexturePacker's
            {"frames": ...}): the PNG of the same name exists, every region lies inside it, and
            every animation's frames (or pattern) name existing regions. Other JSON is skipped.
    *.tmj   Tiled maps: orthogonal, finite and CSV/array data (what Emerald loads); tilesets
            and their images exist; tile sizes match the map; the image holds the tileset's
            columns and tile count; every tile id in layers and tile objects is in a tileset
    *.tsj   Tiled tilesets on their own: the image exists and holds the tiles

Prints one line per problem ("file: what is wrong") and exits with 1 if there was any, 0 if
not (2 for bad arguments). Only the Python standard library is needed; Pillow is used for PNGs
the small decoder here does not read (interlaced ones), if it is installed.
"""

import argparse
import fnmatch
import json
import struct
import sys
import zlib
from pathlib import Path

FLIP_BITS = 0xE0000000  # Tiled's flip flags on a tile id (GID)


class Report:
    """Collects problems as "file: message" lines."""

    def __init__(self, root):
        self.root = root
        self.problems = []

    def name(self, path):
        try:
            return str(Path(path).resolve().relative_to(self.root))
        except ValueError:
            return str(path)

    def problem(self, path, message):
        self.problems.append("%s: %s" % (self.name(path), message))


# --- PNG ------------------------------------------------------------------------------------


def png_size(path):
    """(width, height) from the PNG header, or None if it is not a PNG."""
    with open(path, "rb") as f:
        head = f.read(24)
    if len(head) < 24 or head[:8] != b"\x89PNG\r\n\x1a\n" or head[12:16] != b"IHDR":
        return None
    return struct.unpack(">II", head[16:24])


def read_png(path):
    """The pixels as (width, height, rows of (r, g, b, a) tuples), or None if not readable.
    Reads 1 to 16 bit gray, RGB, palette (with tRNS transparency), gray + alpha and RGBA."""
    data = Path(path).read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        return None
    pos, idat, palette, trns = 8, b"", [], b""
    width = height = depth = color = interlace = 0
    while pos + 8 <= len(data):
        length, kind = struct.unpack(">I4s", data[pos : pos + 8])
        body = data[pos + 8 : pos + 8 + length]
        pos += 12 + length
        if kind == b"IHDR":
            width, height, depth, color, _, _, interlace = struct.unpack(">IIBBBBB", body)
        elif kind == b"PLTE":
            palette = [tuple(body[i : i + 3]) for i in range(0, len(body), 3)]
        elif kind == b"tRNS":
            trns = body
        elif kind == b"IDAT":
            idat += body
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}.get(color)
    if channels is None or interlace:
        return read_png_with_pillow(path)
    raw = zlib.decompress(idat)

    # Undo the per-row filters (PNG spec, section 9).
    bpp = max(1, channels * depth // 8)  # bytes to the same channel of the previous pixel
    stride = (width * channels * depth + 7) // 8
    rows, prev, pos = [], bytearray(stride), 0
    for _ in range(height):
        kind, line = raw[pos], bytearray(raw[pos + 1 : pos + 1 + stride])
        pos += 1 + stride
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if kind == 1:
                line[i] = (line[i] + a) & 255
            elif kind == 2:
                line[i] = (line[i] + b) & 255
            elif kind == 3:
                line[i] = (line[i] + (a + b) // 2) & 255
            elif kind == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(line)
        prev = line

    # Samples to RGBA (16-bit samples keep their high byte).
    def samples(line):
        if depth == 8:
            return list(line)
        if depth == 16:
            return list(line[0::2])
        per_byte, mask = 8 // depth, (1 << depth) - 1
        out = [(byte >> (8 - depth * (k + 1))) & mask for byte in line for k in range(per_byte)]
        return out[: width * channels]

    gray_key = struct.unpack(">H", trns[:2])[0] if color == 0 and len(trns) >= 2 else None
    rgb_key = struct.unpack(">HHH", trns[:6]) if color == 2 and len(trns) >= 6 else None
    pixels = []
    for line in rows:
        s, out = samples(line), []
        for x in range(width):
            v = s[x * channels : (x + 1) * channels]
            if color == 3:
                r, g, b = palette[v[0]] if v[0] < len(palette) else (0, 0, 0)
                out.append((r, g, b, trns[v[0]] if v[0] < len(trns) else 255))
            elif color == 0:
                scale = 255 // ((1 << depth) - 1) if depth < 8 else 1
                g = v[0] * scale
                out.append((g, g, g, 0 if gray_key is not None and v[0] == gray_key else 255))
            elif color == 4:
                out.append((v[0], v[0], v[0], v[1]))
            elif color == 2:
                key = rgb_key is not None and depth == 8 and tuple(v) == rgb_key
                out.append((v[0], v[1], v[2], 0 if key else 255))
            else:
                out.append(tuple(v))
        pixels.append(out)
    return width, height, pixels


def read_png_with_pillow(path):
    try:
        from PIL import Image
    except ImportError:
        return None
    with Image.open(path) as image:
        rgba = image.convert("RGBA")
        w, h = rgba.size
        px = rgba.load()
        return w, h, [[px[x, y] for x in range(w)] for y in range(h)]


def read_gpl(path):
    """The colors of a GIMP palette as a set of (r, g, b)."""
    colors = set()
    for line in Path(path).read_text().splitlines():
        parts = line.split()
        if len(parts) >= 3 and all(p.isdigit() for p in parts[:3]):
            colors.add(tuple(int(p) for p in parts[:3]))
    return colors


def check_palette(path, palette, report):
    image = read_png(path)
    if image is None:
        report.problem(path, "cannot read this PNG (interlaced? install Pillow)")
        return
    _, _, rows = image
    off, partial = [], []
    for y, row in enumerate(rows):
        for x, (r, g, b, a) in enumerate(row):
            if a == 0:
                continue
            if a != 255:
                partial.append((x, y, a))
            elif (r, g, b) not in palette:
                off.append((x, y, "#%02x%02x%02x" % (r, g, b)))
    if off:
        x, y, hexcolor = off[0]
        report.problem(path, "%d pixel(s) not in the palette, first at (%d, %d): %s"
                       % (len(off), x, y, hexcolor))
    if partial:
        x, y, a = partial[0]
        report.problem(path, "%d semi-transparent pixel(s), first at (%d, %d): alpha %d"
                       % (len(partial), x, y, a))


# --- Texture atlases ------------------------------------------------------------------------


def atlas_regions(root):
    """The atlas's {name: (x, y, w, h)} like the engine reads it, or None if this JSON is not
    an atlas."""
    if not isinstance(root, dict):
        return None
    entries = root.get("frames") if isinstance(root.get("frames"), dict) else root
    regions = {}
    for name, entry in entries.items():
        if entries is root and name in ("animations", "meta"):
            continue
        rect = entry.get("frame", entry) if isinstance(entry, dict) else None
        if isinstance(rect, dict) and all(isinstance(rect.get(k), (int, float)) for k in "xywh"):
            regions[name] = tuple(rect[k] for k in "xywh")
    return regions or None


def check_atlas(path, root, report):
    regions = atlas_regions(root)
    if regions is None:
        return False  # some other JSON
    image = path.with_suffix(".png")
    size = png_size(image) if image.exists() else None
    if size is None:
        report.problem(path, "atlas image %s is missing or not a PNG" % image.name)
    else:
        for name, (x, y, w, h) in regions.items():
            if x < 0 or y < 0 or w <= 0 or h <= 0 or x + w > size[0] or y + h > size[1]:
                report.problem(path, "region '%s' (%g, %g, %g x %g) is not inside the %d x %d "
                               "image" % (name, x, y, w, h, *size))

    animations = root.get("animations", {})
    if not isinstance(animations, dict):
        report.problem(path, '"animations" must be an object')
        return True
    for name, entry in animations.items():
        if isinstance(entry, list):
            frames = entry
        elif isinstance(entry, dict) and isinstance(entry.get("frames"), list):
            frames = entry["frames"]
        elif isinstance(entry, dict) and "{}" in str(entry.get("pattern", "")):
            # "walk_{}" from "from" to "to", or while such regions exist (as the engine does).
            pattern, first, last = entry["pattern"], entry.get("from", 0), entry.get("to", -1)
            if last >= 0:
                frames = [pattern.replace("{}", str(i), 1) for i in range(first, last + 1)]
            else:
                frames, i = [], first
                while pattern.replace("{}", str(i), 1) in regions:
                    frames.append(pattern.replace("{}", str(i), 1))
                    i += 1
                frames = frames or [pattern.replace("{}", str(first), 1)]
        else:
            report.problem(path, "animation '%s' needs \"frames\" or a \"pattern\" with {}" % name)
            continue
        missing = [f for f in frames if f not in regions]
        if missing:
            report.problem(path, "animation '%s' uses missing frame(s): %s"
                           % (name, ", ".join(map(str, missing))))
    return True


# --- Tiled maps and tilesets ----------------------------------------------------------------


def check_tileset(ts, where, base, report, map_tile=None):
    """Checks one tileset (from a .tsj or embedded in a map); returns its tile count."""
    name = ts.get("name", "?")
    tw, th = ts.get("tilewidth", 0), ts.get("tileheight", 0)
    if tw <= 0 or th <= 0:
        report.problem(where, "tileset '%s' has no tile size" % name)
        return ts.get("tilecount", 0)
    if map_tile and (tw, th) != map_tile:
        report.problem(where, "tileset '%s' has %d x %d tiles, the map %d x %d"
                       % (name, tw, th, *map_tile))
    if "image" not in ts:
        report.problem(where, "tileset '%s' has no single image (image collections are not "
                       "supported)" % name)
        return ts.get("tilecount", 0)
    image = (base / ts["image"]).resolve()
    size = png_size(image) if image.exists() else None
    if size is None:
        report.problem(where, "tileset '%s': image %s is missing or not a PNG"
                       % (name, ts["image"]))
        return ts.get("tilecount", 0)
    if (ts.get("imagewidth", size[0]), ts.get("imageheight", size[1])) != size:
        report.problem(where, "tileset '%s': says %s x %s but %s is %d x %d"
                       % (name, ts.get("imagewidth"), ts.get("imageheight"), ts["image"], *size))
    margin, spacing = ts.get("margin", 0), ts.get("spacing", 0)
    fit_x = (size[0] - 2 * margin + spacing) // (tw + spacing)
    fit_y = (size[1] - 2 * margin + spacing) // (th + spacing)
    columns, count = ts.get("columns", fit_x), ts.get("tilecount", fit_x * fit_y)
    if columns > fit_x or count > columns * fit_y or count <= 0:
        report.problem(where, "tileset '%s': a %d x %d image holds %d x %d tiles of %d x %d, "
                       "not %d columns and %d tiles"
                       % (name, *size, fit_x, fit_y, tw, th, columns, count))
    return count


def check_map(path, root, report):
    for key, ok in (("orientation", "orthogonal"), ("infinite", False)):
        if root.get(key, ok) != ok:
            report.problem(path, "%s is %r; Emerald needs %r" % (key, root.get(key), ok))
    map_tile = (root.get("tilewidth", 0), root.get("tileheight", 0))
    width, height = root.get("width", 0), root.get("height", 0)

    # Tilesets: GID ranges [firstgid, firstgid + tilecount).
    ranges = []
    for ts in root.get("tilesets", []):
        first = ts.get("firstgid", 0)
        if "source" in ts:
            source = (path.parent / ts["source"]).resolve()
            if not source.exists():
                report.problem(path, "tileset %s is missing" % ts["source"])
                continue
            if source.suffix != ".tsj":
                report.problem(path, "tileset %s is not JSON; save it as .tsj" % ts["source"])
                continue
            try:
                ext = json.loads(source.read_text())
            except (OSError, ValueError) as error:
                report.problem(path, "tileset %s: %s" % (ts["source"], error))
                continue
            count = check_tileset(ext, path, source.parent, report, map_tile)
        else:
            count = check_tileset(ts, path, path.parent, report, map_tile)
        ranges.append((first, first + count))

    def known(gid):
        gid &= ~FLIP_BITS & 0xFFFFFFFF
        return gid == 0 or any(a <= gid < b for a, b in ranges)

    def layers(items):  # through group layers
        for layer in items:
            yield layer
            yield from layers(layer.get("layers", []))

    for layer in layers(root.get("layers", [])):
        kind, name = layer.get("type"), layer.get("name", "?")
        if kind == "tilelayer":
            data = layer.get("data")
            if not isinstance(data, list):
                report.problem(path, "layer '%s' is not CSV / array data (set Map Properties > "
                               "Tile Layer Format to CSV)" % name)
                continue
            if len(data) != width * height:
                report.problem(path, "layer '%s' has %d cells, not %d x %d"
                               % (name, len(data), width, height))
            bad = [i for i, gid in enumerate(data) if not known(gid)]
            if bad:
                i = bad[0]
                report.problem(path, "layer '%s': %d cell(s) with unknown tile ids, first %d at "
                               "(%d, %d)" % (name, len(bad), data[i], i % max(width, 1),
                                             i // max(width, 1)))
        elif kind == "objectgroup":
            for o in layer.get("objects", []):
                if "gid" in o and not known(o["gid"]):
                    report.problem(path, "object '%s' (id %s) in '%s' has unknown tile id %d"
                                   % (o.get("name", ""), o.get("id"), name, o["gid"]))
        elif kind == "imagelayer" and layer.get("image"):
            if not (path.parent / layer["image"]).exists():
                report.problem(path, "image layer '%s': %s is missing" % (name, layer["image"]))


# --- Main -----------------------------------------------------------------------------------


def check_file(path, palette, report):
    """Checks one file by its kind; returns True if it was something to check."""
    suffix = path.suffix.lower()
    if suffix == ".png":
        if palette is not None:
            check_palette(path, palette, report)
        return palette is not None
    if suffix not in (".json", ".tmj", ".tsj"):
        return False
    try:
        root = json.loads(path.read_text())
    except (OSError, ValueError) as error:
        report.problem(path, "not valid JSON: %s" % error)
        return True
    if suffix == ".tmj":
        check_map(path, root, report)
    elif suffix == ".tsj":
        check_tileset(root, path, path.parent, report)
    else:
        return check_atlas(path, root, report)
    return True


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0],
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("paths", nargs="+", type=Path, help="asset folders or files")
    parser.add_argument("--palette", type=Path, help="GIMP .gpl palette the PNGs must use")
    parser.add_argument("--exclude", action="append", default=[], metavar="GLOB",
                        help="skip files whose path (relative to the folder) matches")
    args = parser.parse_args(argv)

    palette = None
    if args.palette:
        if not args.palette.exists():
            parser.error("palette %s does not exist" % args.palette)
        palette = read_gpl(args.palette)
        if not palette:
            parser.error("palette %s has no colors" % args.palette)

    report = Report(Path.cwd().resolve())
    checked = 0
    for top in args.paths:
        if not top.exists():
            report.problem(top, "does not exist")
            continue
        files = sorted(p for p in top.rglob("*") if p.is_file()) if top.is_dir() else [top]
        for path in files:
            rel = path.relative_to(top).as_posix() if top.is_dir() else path.name
            if any(fnmatch.fnmatch(rel, pattern) for pattern in args.exclude):
                continue
            checked += check_file(path, palette, report)

    for line in report.problems:
        print(line)
    print("check_assets: %d file(s) checked, %d problem(s)" % (checked, len(report.problems)))
    return 1 if report.problems else 0


if __name__ == "__main__":
    sys.exit(main())
