#!/usr/bin/env python3
"""Builds the sandbox's animated character sheet (hand-made pixel art, no image tools needed).

    python3 tools/sprites/make_hero.py   # writes sandbox/assets/sprites/hero.png + hero.json

Every frame is 16 x 16 pixels, drawn below as text with a fixed 8-color palette. The hero is put
together from parts (body, legs) so the walk cycle stays consistent. The output is deterministic:
running the script again gives the same bytes. Only the Python standard library is used.
"""

import json
import struct
import zlib
from pathlib import Path

# Palette (PICO-8-ish colors); '.' is transparent.
PALETTE = {
    ".": (0, 0, 0, 0),
    "K": (26, 28, 44, 255),  # outline
    "H": (56, 183, 100, 255),  # tunic / hood
    "h": (37, 113, 121, 255),  # tunic shadow, far leg
    "S": (255, 205, 160, 255),  # skin
    "E": (26, 28, 44, 255),  # eye (same as outline)
    "Y": (255, 205, 117, 255),  # belt, coin
    "y": (239, 125, 87, 255),  # coin shade
    "B": (107, 62, 46, 255),  # boots
}

SIZE = 16

# Head and body, facing right (11 rows).
BODY = [
    "......KKKKK.....",
    ".....KHHHHHK....",
    "....KHHHHHHHK...",
    "....KHHSSSSSK...",
    "....KHSSSSESK...",
    "....KHSSSSSSK...",
    ".....KSSSSSK....",
    "....KhHHHHHhK...",
    "...KShHHHHHhSK..",
    "...KSKYYYYYKSK..",
    "....KhhhhhhhK...",
]
BLINK = {4: "....KHSSSSSSK..."}  # row 4 without the eye

LEGS_STAND = [
    ".....KhK.KhK....",
    ".....KhK.KhK....",
    "....KBBK.KBBK...",
    "....KKKK.KKKK...",
]
LEGS_STRIDE_A = [  # near leg forward
    "....KhK...KHK...",
    "...KhK.....KHK..",
    "..KBBK.....KBBK.",
    "..KKKK.....KKKK.",
]
LEGS_STRIDE_B = [  # far leg forward
    "....KHK...KhK...",
    "...KHK.....KhK..",
    "..KBBK.....KBBK.",
    "..KKKK.....KKKK.",
]
LEGS_CROUCH = [
    ".....KhK.KhK....",
    "....KBBK.KBBK...",
    "....KKKK.KKKK...",
]
LEGS_TUCKED = [
    "....KhhK.KhhK...",
    "....KBBKKKBBK...",
    ".....KKK.KKK....",
]

EMPTY = "." * SIZE


def frame(top, body, legs, bottom=0):
    rows = [EMPTY] * top + body + legs + [EMPTY] * bottom
    assert len(rows) == SIZE, len(rows)
    assert all(len(r) == SIZE for r in rows), rows
    return rows


def blink(body):
    return [BLINK.get(i, row) for i, row in enumerate(body)]


def coin(width):
    """A spinning coin: an ellipse `width` pixels wide (drawn, not hand-typed)."""
    rows = []
    for y in range(SIZE):
        row = ""
        for x in range(SIZE):
            dx = (x + 0.5 - 8) / max(width / 2, 0.5)
            dy = (y + 0.5 - 8) / 6.5
            d = dx * dx + dy * dy
            row += "K" if 0.7 < d <= 1.0 else ("Y" if d <= 0.7 and dx < 0.35 else ("y" if d <= 0.7 else "."))
        rows.append(row)
    return rows


FRAMES = {
    "hero_idle_0": frame(1, BODY, LEGS_STAND),
    "hero_idle_1": frame(1, blink(BODY), LEGS_STAND),
    # Walk: contact, passing (body up a pixel), other contact, passing.
    "hero_walk_0": frame(1, BODY, LEGS_STRIDE_A),
    "hero_walk_1": frame(0, BODY, [LEGS_STAND[0]] + LEGS_STAND),
    "hero_walk_2": frame(1, BODY, LEGS_STRIDE_B),
    "hero_walk_3": frame(0, BODY, [LEGS_STAND[0]] + LEGS_STAND),
    # Jump: crouch, in the air (legs tucked), land.
    "hero_jump_0": frame(2, BODY, LEGS_CROUCH),
    "hero_jump_1": frame(0, BODY, LEGS_TUCKED, 2),
    "hero_jump_2": frame(2, BODY, LEGS_CROUCH),
    "coin_0": coin(12),
    "coin_1": coin(8),
    "coin_2": coin(4),
    "coin_3": coin(1.5),
}

ANIMATIONS = {
    "idle": {"frames": ["hero_idle_0", "hero_idle_1"], "durations": [1.6, 0.12]},
    "walk": {"pattern": "hero_walk_{}", "duration": 0.12},
    "jump": {"pattern": "hero_jump_{}", "durations": [0.08, 0.3, 0.12], "mode": "once"},
    "coin": {"pattern": "coin_{}", "duration": 0.09, "mode": "pingpong"},
}


def write_png(path, width, height, pixels):
    raw = b"".join(b"\0" + bytes(c for px in pixels[y * width:(y + 1) * width] for c in px)
                   for y in range(height))

    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    path.write_bytes(png)


def main():
    out = Path(__file__).resolve().parents[2] / "sandbox" / "assets" / "sprites"
    out.mkdir(parents=True, exist_ok=True)
    gap = 1  # transparent pixel between frames
    width = len(FRAMES) * (SIZE + gap) - gap
    pixels = [(0, 0, 0, 0)] * (width * SIZE)
    atlas = {}
    for i, (name, rows) in enumerate(FRAMES.items()):
        x0 = i * (SIZE + gap)
        for y, row in enumerate(rows):
            for x, ch in enumerate(row):
                pixels[y * width + x0 + x] = PALETTE[ch]
        atlas[name] = {"x": x0, "y": 0, "w": SIZE, "h": SIZE}
    atlas["animations"] = ANIMATIONS
    write_png(out / "hero.png", width, SIZE, pixels)
    (out / "hero.json").write_text(json.dumps(atlas, indent=2) + "\n")
    print(f"wrote {out / 'hero.png'} ({width} x {SIZE}) and hero.json")


if __name__ == "__main__":
    main()
