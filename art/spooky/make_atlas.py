#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Lunduke City authors
# See COPYING and NOTICE.

"""Spooky look. The SVGA atlas, then a night ground, violet water, dead woods, and the graveyard.

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

import atmosphere  # noqa: E402

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
SVGA_PATH = ROOT / "third_party/micropolis-assets/images/micropolisEngine/tiles-svga.png"
SPOOKY_PATH = SVGA_PATH.with_name("tiles-spooky.png")
PARK_PATH = ROOT / "data/special-tiles/big-park.png"
SPOOKY_PARK_PATH = ROOT / "data/special-tiles/big-park-spooky.png"
TILE = 16

# Atlas cells that the Spooky sheet is allowed to change. Everything else
# stays byte-identical to tiles-svga.png. The night pass recolors ground,
# water, and lawns on most tiles. 39, 293, and the other gaps have no
# remapped colour, so they stay. 297-305 and 40-43 / 840-843 are in the
# list because the graveyard and the paused manor already differ; the
# atmosphere pass does not repaint those cells.
PARK_TILES = (40, 41, 42, 43)
FOUNTAIN_TILES = (840, 841, 842, 843)
HOUSE_TILES = (252, 254, 256)
MANOR_TURRET = tuple(range(297, 306))
MANOR_FENCE = tuple(range(324, 333))
CHANGED = (
    tuple(range(0, 39))
    + tuple(range(40, 293))
    + tuple(range(294, 364))
    + tuple(range(365, 372))
    + tuple(range(373, 379))
    + tuple(range(380, 382))
    + tuple(range(383, 385))
    + tuple(range(386, 399))
    + tuple(range(401, 403))
    + tuple(range(404, 418))
    + tuple(range(420, 460))
    + tuple(range(468, 472))
    + tuple(range(473, 482))
    + tuple(range(483, 506))
    + (509,)
    + tuple(range(512, 515))
    + tuple(range(516, 520))
    + tuple(range(521, 535))
    + tuple(range(537, 539))
    + tuple(range(540, 542))
    + tuple(range(549, 551))
    + tuple(range(558, 560))
    + (563,)
    + tuple(range(566, 577))
    + tuple(range(578, 588))
    + tuple(range(589, 591))
    + (593,)
    + (597,)
    + tuple(range(603, 606))
    + (608,)
    + tuple(range(611, 649))
    + (650,)
    + tuple(range(653, 670))
    + tuple(range(671, 688))
    + tuple(range(689, 698))
    + tuple(range(700, 729))
    + tuple(range(731, 736))
    + tuple(range(737, 746))
    + tuple(range(747, 750))
    + tuple(range(751, 754))
    + tuple(range(755, 765))
    + tuple(range(766, 770))
    + (771,)
    + (776,)
    + tuple(range(778, 816))
    + tuple(range(818, 820))
    + tuple(range(822, 827))
    + tuple(range(828, 952))
)

# Locked after the sheet is committed. 16px tile bytes, then the snapped
# 8px overview, in tile order. The big-park sprite has its own hash.
ATLAS_SHA = "7fdb45c832034e3631dd3a5d706fbdcc2c651459ac360bc952e2f36f0ee3a32b"
PARK_SHA = "3f0e20d7313a54d8268a938d9c25efc6532611ff7c89c6877e575f442412cc42"

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
# Dull grey field. Same roles as dirt B: a base, a lighter neighbour speck
# that never touches its own kind, and a rarer lighter accent.
GREY_BASE = (69, 69, 69)
GREY_NEAR = (81, 81, 81)
GREY_ACCENT = (97, 97, 97)
GREY_SET = frozenset({GREY_BASE, GREY_NEAR, GREY_ACCENT})
TUFT = (0, 65, 0)
TUFT_LIT = (16, 65, 0)
FACE = (203, 203, 203)
FACE_LIT = (227, 227, 227)
FACE_MID = (182, 182, 182)
FACE_SHADE = (162, 162, 162)
BASE_STEP = (130, 130, 130)
WIN_YELLOW = (255, 255, 85)
WIN_HOT = (255, 255, 125)
WIN_ORANGE = (255, 125, 0)
WIN_DIM = (113, 113, 0)
STEP = (130, 130, 130)
COLUMN_LIT = (162, 162, 162)
# 16x16 wrap of the dirt-B stipple. Near specks do not touch, even across
# the tile edge, and accents stay at least one pixel apart. Park tiles share
# it, so a cemetery of them has no seam.
GREY_MASK = (
    "......B....B....",
    ".B.B.....B...CB.",
    "....CB.B....B...",
    "B.........B...B.",
    "..B..B.B....B...",
    "B........B....BC",
    "..B.C.B.C..B....",
    "C............B..",
    "..B.B.B..B.B....",
    "B...........C...",
    "..B.C...B......B",
    "B.....B...B..B..",
    "..B.....B......B",
    "B....B.....B.B..",
    "...B...C.......C",
    ".........B..B...",
)

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


def _grey_at(x: int, y: int) -> tuple[int, int, int]:
    mark = GREY_MASK[y % 16][x % 16]
    if mark == "C":
        return GREY_ACCENT
    if mark == "B":
        return GREY_NEAR
    return GREY_BASE


def _grey_tile() -> Image.Image:
    tile = Image.new("RGB", (TILE, TILE))
    px = tile.load()
    for y in range(TILE):
        for x in range(TILE):
            px[x, y] = _grey_at(x, y)
    return tile


def _blit_rows(image: Image.Image, x0: int, y0: int, rows: tuple[str, ...], ink: dict[str, tuple[int, int, int] | None]) -> None:
    px = image.load()
    for dy, row in enumerate(rows):
        for dx, char in enumerate(row):
            colour = ink.get(char)
            if colour is None:
                continue
            x, y = x0 + dx, y0 + dy
            if 0 <= x < image.size[0] and 0 <= y < image.size[1]:
                px[x, y] = colour


# Light slab and dark outline. `e` is the step end, lighter than the grey
# field so the base reads wider than the slab. `k` is a crack.
_STONE_INK = {
    " ": None,
    "o": INK,
    "h": FACE_LIT,
    "f": FACE,
    "m": FACE_MID,
    "s": FACE_SHADE,
    "#": BLACK,
    ".": FACE,
    "d": BASE_STEP,
    "e": (113, 113, 113),
    "k": INK,
    "c": BLACK,
    # Engraved into the face. Darker than the slab, lighter than the outline.
    # Neither colour is in GREY_SET: the drop shadow paints any grey-dirt
    # pixel, and it used to wipe the cross back out.
    "+": (130, 130, 130),
    "-": (113, 113, 113),
}
# Park slabs use a pure-black outline.
_SLAB_INK = dict(_STONE_INK)
_SLAB_INK["o"] = BLACK


def _rect_rows(rows: tuple[str, ...], name: str) -> tuple[str, ...]:
    width = len(rows[0])
    if any(len(row) != width for row in rows):
        raise SystemExit(f"{name} rows are not rectangular")
    return rows


def _stone_rows(kind: str, crack: bool = False) -> tuple[str, ...]:
    """One upright slab. The base is wider than the arch.

    Three faces, all the same silhouette: a blank arch, a small engraved
    cross, or two short engraved strokes. None of them is a letter.
    """
    faces = {
        "plain": (
            " ofmmmmmfo ",
            " ofmmmmmfo ",
            " ofmmmmmfo ",
        ),
        "cross": (
            " ofmm+mmfo ",
            " ofm+++mfo ",
            " ofmm+mmfo ",
        ),
        "lines": (
            " of---mmfo ",
            " ofmmmmmfo ",
            " ofmm---fo ",
        ),
    }
    if kind not in faces:
        raise SystemExit(f"unknown tombstone {kind}")
    top, mid, low = faces[kind]
    rows = _rect_rows(
        (
            "    ooo    ",
            "   ohhho   ",
            "  ofmmmfo  ",
            top,
            mid,
            low,
            "  ooooooo  ",
            "eddddddddde",
        ),
        kind,
    )
    if not crack:
        return rows
    cracked = [list(row) for row in rows]
    for x, y in ((7, 2), (7, 3), (8, 3)):
        if cracked[y][x] in "fms":
            cracked[y][x] = "k"
    return tuple("".join(row) for row in cracked)


def _stamp_sprite(image: Image.Image, x0: int, y0: int, rows: tuple[str, ...], ink: dict[str, tuple[int, int, int] | None]) -> None:
    _blit_rows(image, x0, y0, rows, ink)


def _shadow_sprite(image: Image.Image, x0: int, y0: int, rows: tuple[str, ...], ink: dict, limit: int | None) -> None:
    """One-pixel drop shadow, down and right, only on the grey field."""
    px = image.load()
    width, height = image.size
    for dy, row in enumerate(rows):
        for dx, char in enumerate(row):
            if ink.get(char) is None:
                continue
            sx, sy = x0 + dx + 1, y0 + dy + 1
            if not (0 <= sx < width and 0 <= sy < height):
                continue
            if limit is not None and not (1 <= sx < limit and 1 <= sy < limit):
                continue
            colour = px[sx, sy]
            rgb = colour[:3] if len(colour) == 4 else colour
            if rgb not in GREY_SET:
                continue
            shaded = (32, 32, 32)
            px[sx, sy] = (*shaded, 255) if len(colour) == 4 else shaded


def _paint_parks(atlas: Image.Image) -> None:
    """One slab per park. The four tiles are the three faces, plus a crack."""
    # (kind, x, y, crack). The sprite is 11x8, so these all sit inside the rim.
    plans = {
        40: ("plain", 2, 4, False),
        41: ("cross", 1, 4, False),
        42: ("lines", 3, 5, False),
        43: ("plain", 2, 3, True),
    }
    borders = []
    sprites = []
    for index, (kind, sx, sy, crack) in plans.items():
        tile = _grey_tile()
        rows = _stone_rows(kind, crack)
        sprites.append(rows)
        if sx <= 0 or sy <= 0 or sx + len(rows[0]) >= TILE or sy + len(rows) >= TILE:
            raise SystemExit(f"park {index} touches the rim")
        px = tile.load()
        for dy, row in enumerate(rows):
            for dx, char in enumerate(row):
                if _SLAB_INK.get(char) is None:
                    continue
                if px[sx + dx, sy + dy] not in GREY_SET:
                    raise SystemExit(f"park {index} overlaps another stone")
        _stamp_sprite(tile, sx, sy, rows, _SLAB_INK)
        _shadow_sprite(tile, sx, sy, rows, _SLAB_INK, TILE - 1)
        borders.append(_rim(tile))
        _paste(atlas, index, tile)
    if len(set(borders)) != 1:
        raise SystemExit("park tiles do not share a rim")
    if len(set(sprites)) < 3:
        raise SystemExit("park tiles do not show three tombstones")


def _rim(tile: Image.Image) -> tuple:
    px = tile.load()
    width, height = tile.size
    bits = []
    for x in range(width):
        bits.append(px[x, 0])
        bits.append(px[x, height - 1])
    for y in range(1, height - 1):
        bits.append(px[0, y])
        bits.append(px[width - 1, y])
    return tuple(bits)


def _crypt_rows(glow: tuple[int, int, int]) -> tuple[tuple[str, ...], dict[str, tuple[int, int, int] | None]]:
    """Crypt in the same 3/4 as the houses: roof from above, door on the front.

    The left roof slope is lit. The right slope, the right buttress, and the
    right wall are in shade. Only the candle pixels change between frames.
    The sprite fills the tile and leaves a one-pixel grey-dirt margin.
    """
    rows = (
        "      ss      ",
        "     ssss     ",
        "      rr      ",
        "     rLSr     ",
        "    rLLSSr    ",
        "   rLLLSSSr   ",
        "  rLLLLSSSSr  ",
        " rLLLLLSSSSSr ",
        "oBWWWWWWWWWWDo",
        "oBW##dddd##WDo",
        "oBW#ddggdd#WDo",
        "oBW##dddd##WDo",
        "oBWWWWWWWWWWDo",
        "TTTTTTTTTTTTTT",
    )
    if any(len(row) != 14 for row in rows):
        raise SystemExit("crypt rows are not 14 wide")
    ink = {
        " ": None,
        "s": SPIKE,
        "r": INK,
        "L": (130, 130, 130),
        "S": (56, 56, 56),
        "o": INK,
        "W": (113, 113, 113),
        "B": COLUMN_LIT,
        "D": (81, 81, 81),
        "#": BLACK,
        "d": (32, 32, 32),
        "g": glow,
        "T": STEP,
    }
    return rows, ink


def _paint_fountains(atlas: Image.Image) -> None:
    """One mausoleum on grey dirt. No hedge rim, no corner markers."""
    glows = (WIN_ORANGE, WIN_YELLOW, WIN_HOT, WIN_DIM)
    frames = []
    for glow in glows:
        tile = _grey_tile()
        rows, ink = _crypt_rows(glow)
        # Inset by one pixel so the shared rim stays grey dirt.
        _stamp_sprite(tile, 1, 1, rows, ink)
        _shadow_sprite(tile, 1, 1, rows, ink, TILE - 1)
        frames.append(tile)
    if len({_rim(frame) for frame in frames}) != 1:
        raise SystemExit("crypt frames do not share a rim")
    if any(colour == HEDGE for colour in _rim(frames[0])):
        raise SystemExit("crypt rim is green")
    if any(colour not in GREY_SET for colour in _rim(frames[0])):
        raise SystemExit("crypt rim is not grey dirt")
    if len({frame.tobytes() for frame in frames}) < 2:
        raise SystemExit("crypt frames do not flicker")
    for offset, frame in enumerate(frames):
        _paste(atlas, 840 + offset, frame)


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


# SVGA polish lifts these roof greys. The cottage is approved art, so the
# lift is undone before the luma ramp and the house stays the one that shipped.
_ROOF_LIFT_BACK = {
    (113, 113, 113): (56, 56, 56),
    (130, 130, 130): (69, 69, 69),
    (146, 146, 146): (81, 81, 81),
}


def _paint_cottage(tile: Image.Image) -> Image.Image:
    """The wide SVGA house, knocked down to purple stone, with a cross on the peak."""
    src = tile.load()
    out = tile.copy()
    px = out.load()
    for y in range(16):
        for x in range(16):
            colour = _ROOF_LIFT_BACK.get(src[x, y], src[x, y])
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


_TREE_SRC = {(92, 184, 68), (18, 78, 28), (168, 208, 72)}
_PATH_SRC = {
    (84, 48, 22): (56, 56, 56),
    (132, 78, 36): (97, 97, 97),
    (206, 170, 96): (113, 113, 113),
}
_DUSK = {
    (0, 170, 0): (0, 113, 0),
    (56, 113, 0): (32, 65, 0),
    (0, 65, 0): (0, 65, 0),
}


def _tree_mask(src: Image.Image) -> list[list[bool]]:
    px = src.load()
    width, height = src.size
    seen = [[False] * width for _ in range(height)]
    mask = [[False] * width for _ in range(height)]
    for y in range(height):
        for x in range(width):
            if seen[y][x] or px[x, y][:3] not in _TREE_SRC:
                continue
            stack = [(x, y)]
            blob: list[tuple[int, int]] = []
            seen[y][x] = True
            while stack:
                cx, cy = stack.pop()
                blob.append((cx, cy))
                for dx, dy in ((0, 1), (0, -1), (1, 0), (-1, 0)):
                    nx, ny = cx + dx, cy + dy
                    if 0 <= nx < width and 0 <= ny < height and not seen[ny][nx] and px[nx, ny][:3] in _TREE_SRC:
                        seen[ny][nx] = True
                        stack.append((nx, ny))
            if len(blob) >= 6:
                for cx, cy in blob:
                    mask[cy][cx] = True
    return mask


def _paint_big_park(classic: Image.Image) -> Image.Image:
    """Grey cemetery lawn, dusk trees, a stone path, and the three slabs."""
    image = _paint_special_base(classic)
    src = classic.convert("RGBA")
    trees = _tree_mask(src)
    sp = src.load()
    px = image.load()
    for y in range(image.size[1]):
        for x in range(image.size[0]):
            if sp[x, y][3] == 0:
                px[x, y] = (0, 0, 0, 0)
                continue
            if trees[y][x]:
                rgb = _DUSK.get(px[x, y][:3], px[x, y][:3])
                px[x, y] = (*rgb, 255)
            elif sp[x, y][:3] in _PATH_SRC:
                px[x, y] = (*_PATH_SRC[sp[x, y][:3]], 255)
            else:
                px[x, y] = (*_grey_at(x, y), 255)
    # A few dead tufts on open grey, off the path and the trees.
    for tx, ty in ((4, 44), (15, 43), (22, 15), (8, 16)):
        if px[tx, ty][:3] in GREY_SET:
            px[tx, ty] = (*(TUFT if (tx + ty) % 2 == 0 else TUFT_LIT), 255)
            if ty + 1 < 48 and px[tx, ty + 1][:3] in GREY_SET:
                px[tx, ty + 1] = (*TUFT_LIT, 255)
    rgb = image.convert("RGB")
    # The same slab as the park tiles, set apart in the open meadow.
    for kind, crack, ox, oy in (
        ("plain", False, 24, 23),
        ("cross", False, 37, 23),
        ("lines", False, 22, 36),
        ("plain", True, 35, 36),
    ):
        rows = _stone_rows(kind, crack)
        op = rgb.load()
        for dy, row in enumerate(rows):
            for dx, char in enumerate(row):
                if _SLAB_INK.get(char) is None:
                    continue
                x, y = ox + dx, oy + dy
                if trees[y][x] or op[x, y] not in GREY_SET:
                    raise SystemExit(f"tombstone at {(ox, oy)} hits {(x, y)}")
        _stamp_sprite(rgb, ox, oy, rows, _SLAB_INK)
        _shadow_sprite(rgb, ox, oy, rows, _SLAB_INK, None)
    painted = rgb.convert("RGBA")
    painted.putalpha(image.getchannel("A"))
    return painted


def build() -> tuple[Image.Image, Image.Image]:
    with Image.open(SVGA_PATH) as opened:
        atlas = opened.convert("RGB")
        atlas.load()
    _paint_parks(atlas)
    _paint_fountains(atlas)
    _paint_houses(atlas)
    _paint_manors(atlas)
    atmosphere.apply(atlas)
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

    lines = [
        "Spooky atlas",
        "groups: night dirt and lawns, violet water 2-20 and the bridge tiles,",
        "dead woods 21-37, pumpkins 30 31 33 35, parks 40-43, fountain 840-843,",
        "houses 252 254 256, manor 297-305 (untouched), fence manor 324-332,",
        "asylum 405-413, haunted arena 779-810 and 932-947, big park sprite",
    ]
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
