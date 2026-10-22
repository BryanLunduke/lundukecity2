#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Lunduke City authors
# See COPYING and NOTICE.

"""Spooky look. A clone of the SVGA atlas with graveyards and haunted houses.

The committed sheet is checked in place and is not rewritten. Pass --out to
write the report under another directory (the test does this). Pass --emit
only when regenerating the committed sheet by hand.

    python3 art/spooky/make_atlas.py --out DIR
"""

from __future__ import annotations

import sys

sys.dont_write_bytecode = True

import hashlib
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "svga"))
from vga_palette import vga_palette_8bit  # noqa: E402

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
SVGA_PATH = ROOT / "third_party/micropolis-assets/images/micropolisEngine/tiles-svga.png"
SPOOKY_PATH = SVGA_PATH.with_name("tiles-spooky.png")
PARK_PATH = ROOT / "data/special-tiles/big-park.png"
SPOOKY_PARK_PATH = ROOT / "data/special-tiles/big-park-spooky.png"
TILE = 16

# Atlas cells that the Spooky sheet is allowed to change. Everything else
# stays byte-identical to tiles-svga.png.
PARK_TILES = (40, 41, 42, 43)
FOUNTAIN_TILES = (840, 841, 842, 843)
HOUSE_TILES = (252, 254, 256)
MANOR_TURRET = tuple(range(297, 306))
MANOR_FENCE = tuple(range(324, 333))
CHANGED = PARK_TILES + FOUNTAIN_TILES + HOUSE_TILES + MANOR_TURRET + MANOR_FENCE

# Locked after the sheet is committed. 16px tile bytes, then the snapped
# 8px overview, in tile order. The big-park sprite has its own hash.
ATLAS_SHA = "cc5975e29711780c0ddf285dfadc68b772364d52052ba60cb4dd079cad14e3ab"
PARK_SHA = "0e39417505ad2a3cc696353ca60d294498c8557af3b5842a37b2ae2898f2f319"

GRASS = (0, 113, 0)
LAWN = (0, 170, 0)
HEDGE = (32, 65, 32)
STONE = (203, 203, 203)
STONE_MID = (130, 130, 130)
STONE_DARK = (81, 81, 81)
STONE_EDGE = (69, 69, 69)
INK = (20, 20, 20)
BLACK = (0, 0, 0)
WALL = (32, 32, 32)
WALL_SHADE = (32, 32, 65)
ROOF = (56, 56, 56)
ROOF_LIT = (97, 97, 97)
GLOW = (255, 255, 85)
GLOW_HOT = (255, 255, 125)
GLOW_DIM = (113, 113, 0)
DOOR = (65, 0, 0)
BOARD = (65, 32, 0)
BOARD_DARK = (65, 16, 0)
PURPLE = (32, 0, 65)
PURPLE_LIT = (48, 0, 65)
SPIKE = (227, 227, 227)
SPIKE_MID = (182, 182, 182)

# Lot colours left alone when a residential block is darkened, so the
# SVGA dirt, hedge, and lawn still meet the neighbouring tiles.
GROUND = frozenset(
    {
        LAWN,
        GRASS,
        (28, 113, 0),
        (56, 113, 0),
        (56, 113, 56),
        HEDGE,
        (44, 65, 44),
        (16, 65, 0),
        (0, 65, 0),
        (0, 65, 16),
        (0, 65, 32),
        (0, 65, 48),
        (113, 56, 0),
        (113, 69, 56),
        (113, 28, 0),
        (113, 0, 28),
        (170, 85, 0),
        (65, 48, 0),
        (65, 16, 0),
        (65, 32, 0),
        (65, 0, 16),
        (65, 0, 32),
        (65, 40, 32),
        (65, 48, 32),
        (113, 85, 56),
        (113, 85, 0),
    }
)

WINDOW_SRC = frozenset(
    {
        BLACK,
        (0, 0, 170),
        (0, 0, 113),
        (0, 0, 65),
        (125, 158, 255),
        (56, 85, 113),
        (56, 56, 113),
    }
)

STONE_RAMP = (
    BLACK,
    INK,
    WALL,
    (44, 44, 44),
    ROOF,
    STONE_EDGE,
    STONE_DARK,
    ROOF_LIT,
    (113, 113, 113),
)

# Fountain pixels replaced by the mausoleum. The lawn and the hedge stay.
FOUNTAIN_BODY = frozenset(
    {
        (162, 162, 162),
        (130, 130, 130),
        (113, 113, 113),
        (146, 146, 146),
        (255, 255, 255),
        (203, 203, 203),
        (0, 32, 65),
        (0, 48, 65),
        (182, 182, 182),
        STONE_DARK,
        STONE_EDGE,
    }
)

# Big Park, classic sprite to the SVGA VGA entries. Same map as paint_special.
SPECIAL_VGA = {
    (40, 132, 48): (0, 113, 0),
    (92, 184, 68): (0, 170, 0),
    (18, 78, 28): (0, 65, 0),
    (168, 208, 72): (56, 113, 0),
    (132, 78, 36): (113, 56, 0),
    (84, 48, 22): (65, 16, 0),
    (176, 124, 64): (113, 85, 56),
    (206, 170, 96): (113, 113, 56),
    (112, 114, 122): (113, 113, 113),
    (72, 78, 88): (69, 69, 69),
    (176, 176, 182): (182, 182, 182),
    (52, 52, 58): (56, 56, 56),
    (28, 28, 32): (32, 32, 32),
    (96, 148, 220): (125, 158, 255),
    (36, 72, 168): (0, 0, 170),
    (18, 36, 96): (0, 0, 65),
    (48, 112, 176): (0, 85, 113),
    (140, 196, 224): (125, 223, 255),
    (196, 40, 34): (113, 56, 56),
    (120, 22, 20): (65, 32, 32),
    (236, 96, 64): (113, 81, 81),
    (236, 184, 32): (255, 255, 85),
    (255, 226, 120): (255, 255, 125),
    (248, 246, 240): (255, 255, 255),
    (214, 210, 200): (203, 203, 203),
    (228, 228, 232): (227, 227, 227),
}

# Small enough to sit in the gaps of the SVGA park stipple.
CROSSES = {
    "cross": (" g ", "ggg", " g ", " d "),
    "round": (" s ", "sSs", "s.s", " d "),
    "slab": ("SSS", "S#S", "SSS", " d "),
    "lean": (" sS", "s# ", "dd "),
}
STONE_INK = {
    " ": None,
    ".": STONE_DARK,
    "g": STONE,
    "G": SPIKE_MID,
    "s": STONE_MID,
    "S": STONE,
    "#": ROOF,
    "d": STONE_EDGE,
}


def _palette() -> tuple[list[tuple[int, int, int]], set[tuple[int, int, int]]]:
    order: list[tuple[int, int, int]] = []
    seen: set[tuple[int, int, int]] = set()
    for rgb in vga_palette_8bit():
        if rgb not in seen:
            seen.add(rgb)
            order.append(rgb)
    return order, seen


def _crop(atlas: Image.Image, index: int) -> Image.Image:
    columns = atlas.size[0] // TILE
    col, row = index % columns, index // columns
    return atlas.crop((col * TILE, row * TILE, (col + 1) * TILE, (row + 1) * TILE))


def _paste(atlas: Image.Image, index: int, tile: Image.Image) -> None:
    columns = atlas.size[0] // TILE
    col, row = index % columns, index // columns
    atlas.paste(tile, (col * TILE, row * TILE))


def _box_half(image: Image.Image) -> Image.Image:
    """2×2 box average. Each channel is (sum + 2) / 4, matching tile_atlas.cpp."""
    width, height = image.size
    src = image.load()
    out = Image.new("RGB", (width // 2, height // 2))
    dst = out.load()
    for y in range(height // 2):
        for x in range(width // 2):
            samples = (
                src[x * 2, y * 2],
                src[x * 2 + 1, y * 2],
                src[x * 2, y * 2 + 1],
                src[x * 2 + 1, y * 2 + 1],
            )
            dst[x, y] = tuple((sum(sample[channel] for sample in samples) + 2) // 4 for channel in range(3))
    return out


def _snap_vga(image: Image.Image, order: list[tuple[int, int, int]], palette: set[tuple[int, int, int]]) -> Image.Image:
    src = image.load()
    out = Image.new("RGB", image.size)
    dst = out.load()
    cache: dict[tuple[int, int, int], tuple[int, int, int]] = {}
    for y in range(image.size[1]):
        for x in range(image.size[0]):
            colour = src[x, y]
            snapped = cache.get(colour)
            if snapped is None:
                if colour in palette:
                    snapped = colour
                else:
                    best = order[0]
                    best_d = 1 << 30
                    for entry in order:
                        distance = (entry[0] - colour[0]) ** 2 + (entry[1] - colour[1]) ** 2 + (entry[2] - colour[2]) ** 2
                        if distance < best_d:
                            best_d = distance
                            best = entry
                    snapped = best
                cache[colour] = snapped
            dst[x, y] = snapped
    return out


def _stamp(image: Image.Image, x0: int, y0: int, rows: tuple[str, ...] | list[str], ink: dict[str, tuple[int, int, int] | None], allowed: set[tuple[int, int, int]] | None = None) -> int:
    px = image.load()
    painted = 0
    for dy, row in enumerate(rows):
        for dx, char in enumerate(row):
            colour = ink.get(char)
            if colour is None:
                continue
            x, y = x0 + dx, y0 + dy
            if not (0 <= x < image.size[0] and 0 <= y < image.size[1]):
                continue
            if allowed is not None and px[x, y] not in allowed:
                continue
            px[x, y] = colour
            painted += 1
    return painted


def _can_place(image: Image.Image, x0: int, y0: int, rows: tuple[str, ...], occupied: set[tuple[int, int]]) -> bool:
    px = image.load()
    ink = 0
    for dy, row in enumerate(rows):
        for dx, char in enumerate(row):
            if STONE_INK.get(char) is None:
                continue
            x, y = x0 + dx, y0 + dy
            if not (0 <= x < image.size[0] and 0 <= y < image.size[1]):
                return False
            if px[x, y] != GRASS or (x, y) in occupied:
                return False
            ink += 1
    return ink >= 4


def _place_stone(image: Image.Image, kind: str, x: int, y: int, occupied: set[tuple[int, int]]) -> bool:
    rows = CROSSES[kind]
    spots = [(x, y)]
    for radius in range(1, 8):
        for dy in range(-radius, radius + 1):
            for dx in range(-radius, radius + 1):
                spots.append((x + dx, y + dy))
    for sx, sy in spots:
        if _can_place(image, sx, sy, rows, occupied):
            _stamp(image, sx, sy, rows, STONE_INK, {GRASS})
            for dy, row in enumerate(rows):
                for dx, char in enumerate(row):
                    if STONE_INK.get(char) is not None:
                        occupied.add((sx + dx, sy + dy))
            return True
    return False


def _paint_parks(atlas: Image.Image) -> None:
    # Two or three stones on open deep-green grass. Shrubs and the dirt
    # contact pixels stay, so the tile still meets its neighbours.
    plans = {
        40: (("round", 1, 1), ("cross", 9, 8), ("slab", 3, 11)),
        41: (("slab", 2, 1), ("cross", 9, 7), ("lean", 4, 11)),
        42: (("cross", 1, 2), ("round", 9, 8), ("slab", 4, 11)),
        43: (("lean", 1, 2), ("round", 8, 7), ("cross", 3, 11)),
    }
    for index, stones in plans.items():
        tile = _crop(atlas, index)
        occupied: set[tuple[int, int]] = set()
        placed = 0
        for kind, x, y in stones:
            if _place_stone(tile, kind, x, y, occupied):
                placed += 1
        if placed < 2:
            raise SystemExit(f"park {index} only placed {placed} tombstones")
        _paste(atlas, index, tile)


def _mausoleum(frame: Image.Image, candle: tuple[int, int, tuple[int, int, int]]) -> Image.Image:
    out = frame.copy()
    px = out.load()
    for y in range(TILE):
        for x in range(TILE):
            if px[x, y] in FOUNTAIN_BODY:
                px[x, y] = LAWN
    # Pitched stone tomb where the basin was, plus a flanking headstone.
    tomb = (
        "  gGg  ",
        " gGGGg ",
        "gGGwGGg",
        "GGGwGGG",
        "GGGdGGG",
        "GGGdGGG",
        "GGGGGGG",
        " ddddd ",
    )
    ink = {
        " ": None,
        "g": ROOF_LIT,
        "G": (162, 162, 162),
        "w": (113, 113, 113),
        "d": BLACK,
    }
    _stamp(out, 4, 3, tomb, ink)
    _stamp(out, 1, 7, ("sS", "dd"), {"s": STONE_MID, "S": STONE, "d": STONE_EDGE}, {LAWN, GRASS})
    cx, cy, colour = candle
    if px[cx, cy] != HEDGE:
        px[cx, cy] = colour
    return out


def _paint_fountains(atlas: Image.Image) -> None:
    base = _crop(atlas, 840)
    candles = (
        (7, 6, GLOW),
        (8, 6, GLOW_HOT),
        (7, 7, (255, 255, 0)),
        (8, 5, (255, 125, 0)),
    )
    for offset, candle in enumerate(candles):
        _paste(atlas, 840 + offset, _mausoleum(base, candle))


def _bounds(image: Image.Image) -> tuple[int, int, int, int] | None:
    px = image.load()
    xs = [x for y in range(image.size[1]) for x in range(image.size[0]) if px[x, y] not in GROUND]
    ys = [y for y in range(image.size[1]) for x in range(image.size[0]) if px[x, y] not in GROUND]
    if not xs:
        return None
    return min(xs), min(ys), max(xs), max(ys)


def _lot_fill(tile: Image.Image) -> Image.Image:
    """Replace the building with the lot's own ground colours, stippled."""
    px = tile.load()
    grounds = [px[x, y] for y in range(tile.size[1]) for x in range(tile.size[0]) if px[x, y] in GROUND]
    if not grounds:
        return tile.copy()
    out = tile.copy()
    op = out.load()
    for y in range(tile.size[1]):
        for x in range(tile.size[0]):
            if op[x, y] not in GROUND:
                op[x, y] = grounds[(x * 5 + y * 11) % len(grounds)]
    return out


def _put(px, x: int, y: int, colour: tuple[int, int, int], width: int, height: int) -> None:
    if 0 <= x < width and 0 <= y < height:
        px[x, y] = colour


def _paint_victorian(tile: Image.Image) -> Image.Image:
    """A dark house on the same lot, with the roof leaned off the walls."""
    out = _lot_fill(tile)
    px = out.load()
    # Walls sit where the SVGA house sat. The roof shears left.
    for y, row in enumerate(
        (
            "      c       ",
            "     cLss     ",
            "    sLLLSs    ",
            "   sLyyySs    ",
            "   sswwwws    ",
            "    wayyaws   ",
            "    wayyaws   ",
            "    waddaws   ",
            "    wakkaws   ",
            "    sssssss   ",
        )
    ):
        for x, char in enumerate(row):
            colour = {
                "c": STONE_EDGE,
                "s": INK,
                "L": ROOF_LIT,
                "S": ROOF,
                "y": GLOW,
                "w": WALL,
                "a": WALL_SHADE,
                "d": DOOR,
                "k": GLOW_DIM,
            }.get(char)
            if colour is not None:
                _put(px, x + 2, y + 2, colour, 16, 16)
    return out


def _paint_shack(tile: Image.Image) -> Image.Image:
    """A low boarded house. The roof sags and the window is an X."""
    out = _lot_fill(tile)
    px = out.load()
    for y, row in enumerate(
        (
            "    b     ",
            "   bBb    ",
            "  bBBBBb  ",
            " bBBxBxBb ",
            " bBBxyxBb ",
            " bBBxBxBb ",
            " bBBBBBBb ",
            " bBBddBBb ",
            " bbbbbbbb ",
            "  p    p  ",
        )
    ):
        for x, char in enumerate(row):
            colour = {
                "b": BOARD_DARK,
                "B": BOARD,
                "x": INK,
                "d": BLACK,
                "y": GLOW,
                "p": STONE_EDGE,
            }.get(char)
            if colour is not None:
                _put(px, x + 3, y + 3, colour, 16, 16)
    return out


def _paint_cottage(tile: Image.Image) -> Image.Image:
    """The wide SVGA house, knocked down to purple stone, with a cross on the peak."""
    src = tile.load()
    out = tile.copy()
    px = out.load()
    for y in range(16):
        for x in range(16):
            colour = src[x, y]
            if colour in GROUND:
                continue
            red, green, blue = colour
            if red > 160 and green > 60 and blue < 140:
                px[x, y] = PURPLE if (x + y) % 2 == 0 else PURPLE_LIT
            elif colour in WINDOW_SRC or red + green + blue < 30:
                px[x, y] = GLOW if (x % 3 == 1 and y % 2 == 0) else BLACK
            else:
                luma = (red * 30 + green * 59 + blue * 11) // 100
                ramp = (BLACK, INK, WALL, (44, 44, 44), ROOF, STONE_EDGE)
                px[x, y] = ramp[min(len(ramp) - 1, luma * (len(ramp) - 1) // 180)]
    bounds = _bounds(tile)
    if bounds is None:
        return out
    left, top, right, _bottom = bounds
    cx = (left + right) // 2
    # A one-pixel cross above the roof, on the lawn.
    for dx, dy in ((0, -2), (0, -1), (-1, -1), (1, -1)):
        _put(px, cx + dx, top + dy, SPIKE, 16, 16)
    _put(px, cx, top, INK, 16, 16)
    return out


def _paint_houses(atlas: Image.Image) -> None:
    _paste(atlas, 252, _paint_victorian(_crop(atlas, 252)))
    _paste(atlas, 254, _paint_shack(_crop(atlas, 254)))
    _paste(atlas, 256, _paint_cottage(_crop(atlas, 256)))


def _assemble(atlas: Image.Image, base: int) -> Image.Image:
    image = Image.new("RGB", (TILE * 3, TILE * 3))
    for dy in range(3):
        for dx in range(3):
            image.paste(_crop(atlas, base + dy * 3 + dx), (dx * TILE, dy * TILE))
    return image


def _scatter(atlas: Image.Image, base: int, image: Image.Image) -> None:
    for dy in range(3):
        for dx in range(3):
            _paste(atlas, base + dy * 3 + dx, image.crop((dx * TILE, dy * TILE, (dx + 1) * TILE, (dy + 1) * TILE)))


def _stone_of(colour: tuple[int, int, int], x: int, y: int) -> tuple[int, int, int]:
    luma = (colour[0] * 30 + colour[1] * 59 + colour[2] * 11) // 100
    index = min(len(STONE_RAMP) - 1, luma * (len(STONE_RAMP) - 1) // 220)
    if index <= 2 and (x + y) % 2 == 0:
        return WALL_SHADE
    return STONE_RAMP[index]


def _darken_block(image: Image.Image) -> Image.Image:
    src = image.copy()
    sp = src.load()
    out = image.copy()
    dp = out.load()
    width, height = image.size
    for y in range(height):
        for x in range(width):
            colour = sp[x, y]
            if colour in GROUND:
                continue
            dp[x, y] = _stone_of(colour, x, y)
            if colour in WINDOW_SRC and x % 4 == 1 and y % 3 == 1:
                dp[x, y] = GLOW if (x // 4 + y // 3) % 2 == 0 else GLOW_DIM
    return out


def _building_bounds(original: Image.Image) -> tuple[int, int, int, int] | None:
    px = original.load()
    xs: list[int] = []
    ys: list[int] = []
    for y in range(original.size[1]):
        for x in range(original.size[0]):
            if px[x, y] not in GROUND:
                xs.append(x)
                ys.append(y)
    if not xs:
        return None
    return min(xs), min(ys), max(xs), max(ys)


def _draw_turret(image: Image.Image, bounds: tuple[int, int, int, int]) -> None:
    left, top, right, _bottom = bounds
    px = image.load()
    width = image.size[0]
    height = image.size[1]
    cx = max(4, min(left + 8, right - 8))
    peak = max(1, top - 5)
    # Conical roof, then a shaft with one lit lancet window.
    for dy in range(0, 7):
        half = dy
        y = peak + dy
        for x in range(cx - half, cx + half + 1):
            if 0 <= x < width and 0 <= y < height:
                px[x, y] = ROOF_LIT if x < cx else ROOF
    shaft_top = peak + 7
    for y in range(shaft_top, min(height - 1, shaft_top + 14)):
        for x in range(cx - 3, cx + 4):
            if not (0 <= x < width):
                continue
            if x == cx - 3 or x == cx + 3:
                px[x, y] = INK
            elif x < cx:
                px[x, y] = (44, 44, 44)
            else:
                px[x, y] = WALL
        if shaft_top + 4 <= y <= shaft_top + 8 and 0 <= cx < width:
            px[cx, y] = GLOW
            if cx + 1 < width:
                px[cx + 1, y] = BLACK
    # A small cross on the peak.
    if 0 <= cx < width and peak - 1 >= 0:
        px[cx, peak - 1] = SPIKE
        px[cx, peak] = SPIKE


def _draw_fence(image: Image.Image, original: Image.Image) -> None:
    px = image.load()
    src = original.load()
    width, height = image.size
    for x in range(1, width - 1):
        bottom = None
        for y in range(height - 1, -1, -1):
            if src[x, y] not in GROUND:
                bottom = y
                break
        if bottom is None or bottom < 4:
            continue
        if x % 3 == 1:
            tip = bottom - 4
            if tip >= 0:
                px[x, tip] = SPIKE
            px[x, bottom - 3] = SPIKE_MID
            px[x, bottom - 2] = (113, 113, 113)
            px[x, bottom - 1] = STONE_EDGE
            px[x, bottom] = INK
        else:
            px[x, bottom - 1] = STONE_DARK
            px[x, bottom] = WALL


def _paint_manors(atlas: Image.Image) -> None:
    turret_src = _assemble(atlas, 297)
    turret = _darken_block(turret_src)
    bounds = _building_bounds(turret_src)
    if bounds is None:
        raise SystemExit("manor 297 has no building pixels")
    _draw_turret(turret, bounds)
    _scatter(atlas, 297, turret)

    fence_src = _assemble(atlas, 324)
    fence = _darken_block(fence_src)
    _draw_fence(fence, fence_src)
    _scatter(atlas, 324, fence)


def _paint_special_base(image: Image.Image) -> Image.Image:
    src = image.convert("RGBA")
    out = Image.new("RGBA", src.size)
    sp = src.load()
    dp = out.load()
    for y in range(src.size[1]):
        for x in range(src.size[0]):
            red, green, blue, alpha = sp[x, y]
            if alpha == 0:
                dp[x, y] = (0, 0, 0, 0)
                continue
            rgb = SPECIAL_VGA.get((red, green, blue))
            if rgb is None:
                raise SystemExit(f"no SVGA colour for special pixel {(red, green, blue)} at {(x, y)}")
            dp[x, y] = (*rgb, alpha)
    return out


def _paint_big_park(classic: Image.Image) -> Image.Image:
    """SVGA Big Park, with the pond and the flower beds turned into graves."""
    image = _paint_special_base(classic)
    px = image.load()
    pond = {(0, 85, 113), (125, 223, 255), (255, 255, 255)}
    flowers = {(255, 255, 85), (255, 255, 125), (113, 81, 81), (113, 56, 56)}
    for y in range(image.size[1]):
        for x in range(image.size[0]):
            rgb = px[x, y][:3]
            if rgb in pond:
                px[x, y] = (*(INK if (x + y) % 3 == 0 else (44, 44, 44)), 255)
            elif rgb in flowers:
                px[x, y] = (*STONE_MID, 255)
    # Tombstones on the deep lawn. The trees, path, and hedge greens stay.
    rgb = image.convert("RGB")
    occupied: set[tuple[int, int]] = set()
    # A larger stone reads on the 48px lawn. The 16px parks keep the small set.
    big = {
        "round": ("  s  ", " sSs ", " s.s ", " sSs ", " ddd "),
        "cross": ("  g  ", " ggg ", "ggggg", "  g  ", "  d  "),
        "slab": (" SSS ", " S#S ", " S#S ", " SSS ", " ddd "),
    }
    stones = (
        ("round", 6, 20),
        ("cross", 28, 6),
        ("slab", 34, 32),
        ("round", 2, 34),
        ("slab", 20, 36),
        ("cross", 38, 20),
    )
    for kind, x, y in stones:
        rows = big[kind]
        placed = False
        for radius in range(0, 10):
            for dy in range(-radius, radius + 1):
                for dx in range(-radius, radius + 1):
                    if radius not in (0, abs(dx), abs(dy)):
                        continue
                    if _can_place(rgb, x + dx, y + dy, rows, occupied):
                        _stamp(rgb, x + dx, y + dy, rows, STONE_INK, {GRASS})
                        for py, row in enumerate(rows):
                            for px_i, char in enumerate(row):
                                if STONE_INK.get(char) is not None:
                                    occupied.add((x + dx + px_i, y + dy + py))
                        placed = True
                        break
                if placed:
                    break
            if placed:
                break
    painted = rgb.convert("RGBA")
    # _place_stone works in RGB. Copy alpha back to opaque.
    alpha = image.getchannel("A")
    painted.putalpha(alpha)
    return painted


def build() -> tuple[Image.Image, Image.Image]:
    with Image.open(SVGA_PATH) as opened:
        atlas = opened.convert("RGB")
        atlas.load()
    _paint_parks(atlas)
    _paint_fountains(atlas)
    _paint_houses(atlas)
    _paint_manors(atlas)
    with Image.open(PARK_PATH) as opened:
        park = _paint_big_park(opened)
    return atlas, park


def _digest_atlas(atlas: Image.Image, order: list[tuple[int, int, int]], palette: set[tuple[int, int, int]]) -> tuple[str, int]:
    digest = hashlib.sha256()
    moved = 0
    columns = atlas.size[0] // TILE
    for index in range(960):
        tile = _crop(atlas, index)
        digest.update(tile.tobytes())
        half = _box_half(tile)
        snapped = _snap_vga(half, order, palette)
        before = half.tobytes()
        after = snapped.tobytes()
        moved += sum(1 for a, b in zip(before, after) if a != b) // 3
        digest.update(after)
    return digest.hexdigest(), moved


def _differing_tiles(svga: Image.Image, spooky: Image.Image) -> list[int]:
    changed: list[int] = []
    columns = svga.size[0] // TILE
    sp = svga.load()
    dp = spooky.load()
    for index in range(960):
        col, row = index % columns, index // columns
        x0, y0 = col * TILE, row * TILE
        if any(sp[x0 + x, y0 + y] != dp[x0 + x, y0 + y] for y in range(TILE) for x in range(TILE)):
            changed.append(index)
    return changed


def _check(lines: list[str]):
    failed = False
    order, palette = _palette()
    atlas, park = build()
    with Image.open(SVGA_PATH) as opened:
        svga = opened.convert("RGB")
        svga.load()

    if atlas.size != svga.size or atlas.size != (256, 960):
        lines.append(f"FAIL spooky atlas size {atlas.size}, expected 256x960")
        failed = True
    columns = atlas.size[0] // TILE
    rows = atlas.size[1] // TILE
    if columns * rows != 960:
        lines.append(f"FAIL spooky atlas has {columns * rows} tiles")
        failed = True

    px = atlas.load()
    off = sum(1 for y in range(atlas.size[1]) for x in range(atlas.size[0]) if px[x, y] not in palette)
    if off:
        lines.append(f"FAIL spooky atlas has {off} off-palette pixels")
        failed = True
    else:
        lines.append("OK spooky 16px atlas is on the VGA palette")

    digest, moved = _digest_atlas(atlas, order, palette)
    if ATLAS_SHA != "PENDING" and digest != ATLAS_SHA:
        lines.append(f"FAIL spooky atlas changed: {digest}")
        failed = True
    elif ATLAS_SHA == "PENDING":
        lines.append(f"FAIL spooky atlas hash is not locked: {digest}")
        failed = True
    else:
        lines.append(f"OK spooky atlas hash {ATLAS_SHA[:12]}, 8px snap moved {moved} channels")

    differing = _differing_tiles(svga, atlas)
    if tuple(differing) != tuple(sorted(CHANGED)):
        extra = [index for index in differing if index not in CHANGED]
        missing = [index for index in CHANGED if index not in differing]
        lines.append(f"FAIL tile delta extra={extra} missing={missing}")
        failed = True
    else:
        lines.append(f"OK only {len(CHANGED)} listed tiles differ from SVGA")

    if not SPOOKY_PATH.is_file():
        lines.append(f"FAIL committed spooky sheet missing: {SPOOKY_PATH}")
        failed = True
    else:
        with Image.open(SPOOKY_PATH) as opened:
            committed = opened.convert("RGB")
            committed.load()
        if committed.size != atlas.size or committed.tobytes() != atlas.tobytes():
            lines.append("FAIL committed tiles-spooky.png does not match the generator")
            failed = True
        else:
            lines.append(
                "OK committed tiles-spooky.png: "
                f"{committed.size[0]}x{committed.size[1]}, 960 tiles, VGA palette, hash {digest[:12]}"
            )

    park_rgb = park.convert("RGB")
    pp = park_rgb.load()
    park_off = sum(1 for y in range(park_rgb.size[1]) for x in range(park_rgb.size[0]) if pp[x, y] not in palette)
    if park.size != (48, 48) or park_off:
        lines.append(f"FAIL big park {park.size} off-palette={park_off}")
        failed = True
    park_digest = hashlib.sha256(park.tobytes()).hexdigest()
    if PARK_SHA != "PENDING" and park_digest != PARK_SHA:
        lines.append(f"FAIL big park changed: {park_digest}")
        failed = True
    elif PARK_SHA == "PENDING":
        lines.append(f"FAIL big park hash is not locked: {park_digest}")
        failed = True
    if not SPOOKY_PARK_PATH.is_file():
        lines.append(f"FAIL committed big-park-spooky.png missing: {SPOOKY_PARK_PATH}")
        failed = True
    else:
        with Image.open(SPOOKY_PARK_PATH) as opened:
            committed_park = opened.convert("RGBA")
            committed_park.load()
        if committed_park.tobytes() != park.tobytes():
            lines.append("FAIL committed big-park-spooky.png does not match the generator")
            failed = True
        elif not failed:
            lines.append(f"OK big-park-spooky.png hash {park_digest[:12]}")

    # The four fountain frames have to keep moving, or the park animation dies.
    frames = [_crop(atlas, index).tobytes() for index in FOUNTAIN_TILES]
    if len(set(frames)) < 2:
        lines.append("FAIL fountain frames no longer animate")
        failed = True

    return not failed, atlas, park, digest, park_digest


def _save(image: Image.Image, path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    image.save(path)


def main(argv: list[str]) -> int:
    out: Path | None = None
    emit = False
    index = 0
    while index < len(argv):
        arg = argv[index]
        if arg == "--out":
            if index + 1 >= len(argv):
                raise SystemExit("--out needs a directory")
            out = Path(argv[index + 1])
            index += 2
            continue
        if arg == "--emit":
            emit = True
            index += 1
            continue
        raise SystemExit(f"unknown argument: {arg}")

    lines = ["Spooky atlas", "groups: parks 40-43, fountain 840-843, houses 252 254 256,", "manor 297-305, fence manor 324-332, big park sprite"]
    ok, atlas, park, digest, park_digest = _check(lines)
    if emit:
        _save(atlas, SPOOKY_PATH)
        _save(park, SPOOKY_PARK_PATH)
        lines.append(f"EMIT {SPOOKY_PATH}")
        lines.append(f"EMIT {SPOOKY_PARK_PATH}")
        lines.append(f"ATLAS_SHA {digest}")
        lines.append(f"PARK_SHA {park_digest}")
    if out is not None:
        out.mkdir(parents=True, exist_ok=True)
        report_path = out / "verify-report.txt"
    else:
        report_path = None
    result = "PASS" if ok else "FAIL"
    lines.append("")
    lines.append(f"RESULT: {result}")
    report = "\n".join(lines) + "\n"
    if report_path is not None:
        report_path.write_text(report, encoding="utf-8")
    sys.stdout.write(report)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
