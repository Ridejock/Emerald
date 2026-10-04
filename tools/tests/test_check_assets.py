"""Tests for tools/check_assets.py on small asset folders written to a temporary directory.

    python3 -m unittest discover -s tools/tests
"""

import contextlib
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))
sys.path.insert(0, str(TOOLS / "tilemaps"))
import check_assets  # noqa: E402
from make_tilemaps import write_png  # noqa: E402

RED, GREEN, CLEAR = (255, 0, 0, 255), (0, 255, 0, 255), (0, 0, 0, 0)


def image(path, width, height, color=RED):
    write_png(path, width, height, [[color] * width for _ in range(height)])


class CheckAssetsTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.dir = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def run_checker(self, *args):
        """(exit code, problem lines) of check_assets on the temp folder."""
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = check_assets.main([str(self.dir), *args])
        lines = out.getvalue().splitlines()
        return code, [line for line in lines if not line.startswith("check_assets:")]

    def write_json(self, name, value):
        (self.dir / name).write_text(json.dumps(value))

    # A good 2 x 2 tileset of 16 px tiles and a 3 x 2 map using it.
    def good_map(self, **changes):
        image(self.dir / "tiles.png", 32, 32)
        self.write_json("tiles.tsj", {"name": "tiles", "image": "tiles.png", "imagewidth": 32,
                                      "imageheight": 32, "tilewidth": 16, "tileheight": 16,
                                      "columns": 2, "tilecount": 4})
        m = {"orientation": "orthogonal", "infinite": False, "width": 3, "height": 2,
             "tilewidth": 16, "tileheight": 16,
             "tilesets": [{"firstgid": 1, "source": "tiles.tsj"}],
             "layers": [{"type": "tilelayer", "name": "Ground",
                         "data": [0, 1, 2, 3, 4 | 0x80000000, 0]},  # a flipped tile is fine
                        {"type": "objectgroup", "name": "Objects",
                         "objects": [{"id": 1, "gid": 2}]}]}
        m.update(changes)
        self.write_json("level.tmj", m)

    def test_good_assets_pass(self):
        self.good_map()
        image(self.dir / "hero.png", 32, 16)
        self.write_json("hero.json", {"hero_0": {"x": 0, "y": 0, "w": 16, "h": 16},
                                      "hero_1": {"x": 16, "y": 0, "w": 16, "h": 16},
                                      "animations": {"walk": {"pattern": "hero_{}"},
                                                     "idle": ["hero_0"]}})
        self.write_json("settings.json", {"volume": 3})  # not an atlas: skipped
        self.assertEqual(self.run_checker(), (0, []))

    def test_palette(self):
        (self.dir / "colors.gpl").write_text("GIMP Palette\nName: test\n#\n255   0   0\tRed\n")
        write_png(self.dir / "ok.png", 2, 1, [[RED, CLEAR]])
        write_png(self.dir / "bad.png", 3, 1, [[RED, GREEN, (255, 0, 0, 100)]])
        code, lines = self.run_checker("--palette", str(self.dir / "colors.gpl"))
        self.assertEqual(code, 1)
        self.assertEqual(len(lines), 2)
        self.assertIn("1 pixel(s) not in the palette, first at (1, 0): #00ff00", lines[0])
        self.assertIn("1 semi-transparent pixel(s)", lines[1])
        # --exclude skips it.
        code, lines = self.run_checker("--palette", str(self.dir / "colors.gpl"),
                                       "--exclude", "bad.*")
        self.assertEqual((code, lines), (0, []))

    def test_atlas_problems(self):
        image(self.dir / "hero.png", 32, 16)
        self.write_json("hero.json", {"frames": {
            "a": {"frame": {"x": 0, "y": 0, "w": 16, "h": 16}},
            "b": {"frame": {"x": 24, "y": 0, "w": 16, "h": 16}}},  # sticks out on the right
            "animations": {"run": {"frames": ["a", "c"]}, "jump": {"pattern": "j{}", "to": 1},
                           "broken": {"duration": 1}}})
        self.write_json("lost.json", {"a": {"x": 0, "y": 0, "w": 1, "h": 1}})  # no lost.png
        code, lines = self.run_checker()
        self.assertEqual(code, 1)
        text = "\n".join(lines)
        self.assertIn("region 'b'", text)
        self.assertIn("animation 'run' uses missing frame(s): c", text)
        self.assertIn("animation 'jump' uses missing frame(s): j0, j1", text)
        self.assertIn("animation 'broken' needs", text)
        self.assertIn("lost.json: atlas image lost.png is missing", text)

    def test_map_problems(self):
        self.good_map(tilewidth=8, layers=[
            {"type": "tilelayer", "name": "Ground", "data": [0, 1, 2, 3, 9, 0]},
            {"type": "tilelayer", "name": "Short", "data": [0]},
            {"type": "tilelayer", "name": "Packed", "data": "AAAA", "encoding": "base64"},
            {"type": "group", "name": "Group", "layers": [
                {"type": "objectgroup", "name": "Things", "objects": [{"id": 7, "gid": 5}]}]}])
        code, lines = self.run_checker()
        self.assertEqual(code, 1)
        text = "\n".join(lines)
        self.assertIn("has 16 x 16 tiles, the map 8 x 16", text)
        self.assertIn("layer 'Ground': 1 cell(s) with unknown tile ids, first 9 at (1, 1)", text)
        self.assertIn("layer 'Short' has 1 cells", text)
        self.assertIn("layer 'Packed' is not CSV", text)
        self.assertIn("object '' (id 7) in 'Things' has unknown tile id 5", text)

    def test_missing_tileset_and_image(self):
        self.good_map(tilesets=[{"firstgid": 1, "source": "nowhere.tsj"},
                                {"firstgid": 5, "source": "old.tsx"}], infinite=True)
        (self.dir / "old.tsx").write_text("<tileset/>")
        self.write_json("other.tsj", {"name": "other", "image": "gone.png", "tilewidth": 16,
                                      "tileheight": 16})
        self.write_json("small.tsj", {"name": "small", "image": "tiles.png", "imagewidth": 32,
                                      "imageheight": 32, "tilewidth": 16, "tileheight": 16,
                                      "columns": 3, "tilecount": 9})
        code, lines = self.run_checker()
        self.assertEqual(code, 1)
        text = "\n".join(lines)
        self.assertIn("infinite is True", text)
        self.assertIn("tileset nowhere.tsj is missing", text)
        self.assertIn("tileset old.tsx is not JSON", text)
        self.assertIn("other.tsj: tileset 'other': image gone.png is missing", text)
        self.assertIn("small.tsj: tileset 'small': a 32 x 32 image holds 2 x 2 tiles", text)

    def test_png_decoder_filters(self):
        # The engine's own art scripts write filter 0 only; check the other filters on an image
        # with every pixel different (a hand-filtered "Sub" row, then "Up" and "Paeth").
        import struct
        import zlib
        rows = [[(x * 40, y * 60, x * y * 10, 255) for x in range(5)] for y in range(3)]
        raw = b""
        prev = bytes(20)
        for y, kind in enumerate((1, 2, 4)):
            line = bytes(c for px in rows[y] for c in px)
            out = bytearray()
            for i, v in enumerate(line):
                a = line[i - 4] if i >= 4 else 0
                b, c = prev[i], prev[i - 4] if i >= 4 else 0
                if kind == 1:
                    pred = a
                elif kind == 2:
                    pred = b
                else:
                    p = a + b - c
                    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                    pred = a if pa <= pb and pa <= pc else b if pb <= pc else c
                out.append((v - pred) & 255)
            raw += bytes([kind]) + out
            prev = line

        def chunk(kind, data):
            crc = zlib.crc32(kind + data) & 0xFFFFFFFF
            return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", crc)

        png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 5, 3, 8, 6, 0, 0, 0))
               + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))
        (self.dir / "filtered.png").write_bytes(png)
        width, height, pixels = check_assets.read_png(self.dir / "filtered.png")
        self.assertEqual((width, height), (5, 3))
        self.assertEqual(pixels, [[tuple(px) for px in row] for row in rows])


if __name__ == "__main__":
    unittest.main()
