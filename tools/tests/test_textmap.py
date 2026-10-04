"""Tests for tools/tilemaps/textmap.py: the text map, variant picking, legends and the JSON.

    python3 -m unittest discover -s tools/tests
"""

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tilemaps"))
from textmap import (FLIP_X, TextMap, dump_map, make_map, noise, object_layer, pick,  # noqa: E402
                     point_object, prop, rect_object, tile_layer, tileset)


class TextMapTests(unittest.TestCase):
    def test_cells_and_neighbours(self):
        m = TextMap(["#..", "#@#"], outside="#")
        self.assertEqual((m.width, m.height), (3, 2))
        self.assertEqual(m.at(1, 1), "@")
        self.assertEqual(m.at(-1, 0), "#")  # off the map
        self.assertEqual(m.find("@"), [(1, 1)])
        self.assertTrue(m.is_open(1, 0, "#"))
        # '@' at (1, 1): up '.', right '#', down outside '#', left '#'.
        self.assertEqual(m.mask(1, 1, "#"), 2 | 4 | 8)
        self.assertEqual(m.feet(1, 1, 16), (24.0, 32))
        self.assertEqual(m.center(1, 1, 16), (24.0, 24.0))
        with self.assertRaises(ValueError):
            TextMap(["##", "#"])

    def test_variants_are_repeatable(self):
        self.assertEqual(noise(3, 4, 5), noise(3, 4, 5))
        picks = [pick([1, 2, 3], x, y) for y in range(20) for x in range(20)]
        self.assertEqual(picks, [pick([1, 2, 3], x, y) for y in range(20) for x in range(20)])
        self.assertEqual(set(picks), {1, 2, 3})  # all variants show up
        # Weights: the rare variant is rare.
        rare = sum(pick([(1, 9), (2, 1)], x, y, 7) == 2 for y in range(40) for x in range(40))
        self.assertTrue(40 < rare < 300, rare)

    def test_legend(self):
        m = TextMap(["#/=x", "##\\."])
        legend = {"#": lambda mm, x, y: 1 if mm.at(x, y - 1) != "#" else 2, "/": 3,
                  "\\": 3 | FLIP_X, "=": [4, 5], "x": None}
        data = m.cells(legend)
        self.assertEqual(data[0:2], [1, 3])
        self.assertIn(data[2], (4, 5))
        self.assertEqual(data[3], 0)  # None: empty
        self.assertEqual(data[4:8], [2, 1, 3 | FLIP_X, 0])  # '.' is not in the legend

    def test_written_map(self):
        m = TextMap(["#.", "##"])
        layers = [tile_layer(1, "Ground", m.cells({"#": 1}), m.width, m.height,
                             [prop("collision", True)]),
                  object_layer(2, "Objects", [point_object(3, "spawn", 8, 16),
                                              rect_object(5, "zone", 0, 0, 32, 16, "trigger")])]
        tmj = make_map(m.width, m.height, 16, layers, [{"firstgid": 1, "source": "t.tsj"}],
                       [prop("title", "Test")])
        self.assertEqual((tmj["nextlayerid"], tmj["nextobjectid"]), (3, 6))
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "test.tmj"
            dump_map(path, tmj)
            text = path.read_text()
            self.assertIn("[\n    1, 0,\n    1, 1]", text)  # one row per line
            back = json.loads(text)
        self.assertEqual(back, tmj)  # dump_map leaves the map as it was
        self.assertEqual(back["layers"][1]["objects"][1]["type"], "trigger")
        self.assertNotIn("point", back["layers"][1]["objects"][1])
        self.assertEqual(prop("n", 2), {"name": "n", "type": "int", "value": 2})
        ts = tileset("t", "t.png", (32, 16), 16, 2, 2)
        self.assertEqual((ts["columns"], ts["tilecount"], ts["imagewidth"]), (2, 2, 32))


if __name__ == "__main__":
    unittest.main()
