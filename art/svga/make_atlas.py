#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Lunduke City authors
# See COPYING and NOTICE.

"""SVGA look, batch 1: dirt, water, coast, trees, and residential zones.

Classic pixels are not redrawn. Each output pixel keeps the source tile's
flat-colour region and is painted from a VGA Mode 13h ramp. Black ink stays
black. Later batches add the other tile groups to this file.

    python3 art/svga/make_atlas.py
"""

from __future__ import annotations

import sys
from dataclasses import dataclass
from pathlib import Path

from PIL import Image

from vga_palette import vga_palette_8bit

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
ATLAS_PATH = ROOT / "third_party/micropolis-assets/images/micropolisEngine/tiles.png"
PREVIEW_DIR = ROOT / "previews"
REPORT_PATH = HERE / "verify-report.txt"
TILE = 16
NEAREST = Image.Resampling.NEAREST

BLACK = (0, 0, 0)
WHITE = (255, 255, 255)
BG = (32, 32, 32)
FG = (255, 255, 85)
INK_YELLOW = FG

SRC_GREY = (127, 127, 127)
SRC_GREEN = (0, 230, 0)
SRC_LBLUE = (102, 102, 230)
SRC_DGREY = (63, 63, 63)
SRC_BLUE = (0, 0, 230)
SRC_YELLOW = (255, 255, 0)
SRC_DGREEN = (0, 127, 0)
SRC_LGREY = (191, 191, 191)
SRC_RED = (255, 0, 0)
SRC_TAN = (204, 127, 102)
SRC_DTAN = (153, 127, 76)
SRC_ORANGE = (255, 127, 0)

# SNES-style sand. VGA has no desaturated khaki near (195, 170, 135):
# the nearest entry is roof grey (170, 170, 170), distance 43, which would
# disappear into buildings. (255, 190, 125) is the nearest warm non-pink,
# distance 64.0. The dot is one step darker in that same hue so the stipple
# stays low-contrast. The last two steps are the building shadow.
DIRT_LIGHT = (255, 223, 125)
DIRT_BASE = (255, 190, 125)
DIRT_DOT = (255, 158, 125)
DIRT_RAMP = (
    DIRT_LIGHT,
    DIRT_BASE,
    DIRT_DOT,
    (113, 69, 56),
    (113, 56, 0),
)
GRASS_RAMP = (
    (0, 170, 0),
    (56, 113, 0),
    (28, 113, 0),
    (0, 113, 0),
    (0, 65, 32),
    (0, 65, 16),
)
HEDGE_RAMP = (
    (44, 65, 44),
    (32, 65, 32),
    (16, 65, 0),
    (0, 65, 0),
)
CONCRETE_RAMP = (
    (162, 162, 162),
    (146, 146, 146),
    (130, 130, 130),
    (113, 113, 113),
)
ROOF_RAMP = (
    (227, 227, 227),
    (203, 203, 203),
    (182, 182, 182),
    (170, 170, 170),
)
GLASS_RAMP = (
    (125, 125, 255),
    (85, 85, 255),
    (56, 56, 113),
)
WALL_RAMP = (
    (0, 0, 255),
    (0, 0, 170),
    (0, 0, 113),
)
SLATE_RAMP = (
    (81, 81, 81),
    (69, 69, 69),
    (56, 56, 56),
)
SIGN_RAMP = (
    (255, 255, 125),
    (255, 255, 85),
    (255, 255, 0),
)
# Light-blue water. The last two are the coastline band: a darker teal,
# outside the body dither, with no red so the shore does not turn purple.
WATER_RAMP = (
    (0, 56, 113),
    (0, 85, 113),
    (0, 28, 113),
    (0, 48, 65),
    (0, 32, 65),
)
# Soil is the other classic ground colour. Its open field uses the sand
# stipple (those RGB values belong to tan; a soil pixel only keeps one where
# it does not touch tan). The rim and the shadow are soil's own darker steps.
SOIL_RIM = (113, 85, 56)
SOIL_SAND = (255, 235, 182)
SOIL_CONTACT = (113, 85, 0)
SOIL_RAMP = (SOIL_RIM, SOIL_SAND, SOIL_CONTACT)
DEEP = (0, 0, 170)
SPECK = (56, 56, 56)
STONE = (130, 130, 130)
ROCK = (44, 44, 44)
LEAF = (162, 162, 162)
ORANGE = (255, 125, 0)

_DIRT_WEIGHTS = (0, 0, 0, 0, 0, 0, 1, 2)
# Same deep-green hues. More of the light step, so lawns are not a dark hole
# in the new sand. 2/9 (0, 170, 0), 3/9 (56, 113, 0), 3/9 (28, 113, 0), 1/9 (0, 113, 0).
_GRASS_WEIGHTS = (0, 0, 1, 1, 1, 2, 2, 2, 3)
_WATER_WEIGHTS = _DIRT_WEIGHTS
_MIN_CAST = 8
_ORTHO = ((0, -1), (0, 1), (-1, 0), (1, 0))
_NW = ((0, -1), (-1, 0))
_SE = ((0, 1), (1, 0))
_PAVEMENT = frozenset({SRC_GREY})
RECEIVERS = frozenset({SRC_TAN, SRC_DTAN, SRC_GREEN, SRC_DGREEN})
TREE_CAST = frozenset({SRC_GREEN, SRC_DGREEN})
BUILD_CAST = frozenset({SRC_GREY, SRC_LGREY, SRC_LBLUE, SRC_BLUE, SRC_DGREY, SRC_DGREEN, SRC_YELLOW})
WATER_FAMILY = frozenset({SRC_LBLUE, SRC_BLUE, BLACK})

STRONG_LUMA = 80

FONT = {
    " ": ["     ", "     ", "     ", "     ", "     ", "     ", "     "],
    "A": [" ### ", "#   #", "#   #", "#####", "#   #", "#   #", "#   #"],
    "B": ["#### ", "#   #", "#   #", "#### ", "#   #", "#   #", "#### "],
    "C": [" ### ", "#   #", "#    ", "#    ", "#    ", "#   #", " ### "],
    "D": ["#### ", "#   #", "#   #", "#   #", "#   #", "#   #", "#### "],
    "E": ["#####", "#    ", "#    ", "#### ", "#    ", "#    ", "#####"],
    "G": [" ### ", "#   #", "#    ", "# ###", "#   #", "#   #", " ### "],
    "H": ["#   #", "#   #", "#   #", "#####", "#   #", "#   #", "#   #"],
    "I": ["#####", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", "#####"],
    "K": ["#   #", "#  # ", "# #  ", "##   ", "# #  ", "#  # ", "#   #"],
    "L": ["#    ", "#    ", "#    ", "#    ", "#    ", "#    ", "#####"],
    "M": ["#   #", "## ##", "# # #", "#   #", "#   #", "#   #", "#   #"],
    "N": ["#   #", "##  #", "# # #", "#  ##", "#   #", "#   #", "#   #"],
    "O": [" ### ", "#   #", "#   #", "#   #", "#   #", "#   #", " ### "],
    "P": ["#### ", "#   #", "#   #", "#### ", "#    ", "#    ", "#    "],
    "R": ["#### ", "#   #", "#   #", "#### ", "# #  ", "#  # ", "#   #"],
    "S": [" ####", "#    ", "#    ", " ### ", "    #", "    #", "#### "],
    "T": ["#####", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", "  #  "],
    "U": ["#   #", "#   #", "#   #", "#   #", "#   #", "#   #", " ### "],
    "V": ["#   #", "#   #", "#   #", "#   #", "#   #", " # # ", "  #  "],
    "W": ["#   #", "#   #", "#   #", "# # #", "# # #", "## ##", "#   #"],
    "X": ["#   #", "#   #", " # # ", "  #  ", " # # ", "#   #", "#   #"],
    "Y": ["#   #", "#   #", " # # ", "  #  ", "  #  ", "  #  ", "  #  "],
    "0": [" ### ", "#   #", "#  ##", "# # #", "##  #", "#   #", " ### "],
    "1": ["  #  ", " ##  ", "  #  ", "  #  ", "  #  ", "  #  ", " ### "],
    "2": [" ### ", "#   #", "    #", "   # ", "  #  ", " #   ", "#####"],
    "3": ["#### ", "    #", "    #", " ### ", "    #", "    #", "#### "],
    "4": ["   # ", "  ## ", " # # ", "#  # ", "#####", "   # ", "   # "],
    "5": ["#####", "#    ", "#    ", "#### ", "    #", "    #", "#### "],
    "6": [" ### ", "#    ", "#    ", "#### ", "#   #", "#   #", " ### "],
    "7": ["#####", "    #", "   # ", "  #  ", "  #  ", "  #  ", "  #  "],
    "8": [" ### ", "#   #", "#   #", " ### ", "#   #", "#   #", " ### "],
    "9": [" ### ", "#   #", "#   #", " ####", "    #", "    #", " ### "],
    "-": ["     ", "     ", "     ", "#####", "     ", "     ", "     "],
}


@dataclass(frozen=True)
class Material:
    ramp: tuple[tuple[int, int, int], ...]
    role: str


def _mat(ramp: tuple[tuple[int, int, int], ...], role: str) -> Material:
    return Material(ramp=ramp, role=role)


def _flat(rgb: tuple[int, int, int]) -> Material:
    return Material(ramp=(rgb,), role="flat")


DIRT = _mat(DIRT_RAMP, "dirt")
GRASS = _mat(GRASS_RAMP, "grass")
HEDGE = _mat(HEDGE_RAMP, "hedge")
CONCRETE = _mat(CONCRETE_RAMP, "surface")
ROOF = _mat(ROOF_RAMP, "surface")
GLASS = _mat(GLASS_RAMP, "surface")
WALL = _mat(WALL_RAMP, "surface")
SLATE = _mat(SLATE_RAMP, "surface")
SIGN = _mat(SIGN_RAMP, "surface")
WATER = _mat(WATER_RAMP, "water")

# Outputs reserved for one classic source stay disjoint from every other source.
MATS: dict[str, dict[tuple[int, int, int], Material]] = {
    "ground": {
        SRC_TAN: DIRT,
        SRC_DGREY: _flat(SPECK),
    },
    "water": {
        SRC_LBLUE: WATER,
        SRC_BLUE: _flat(DEEP),
        SRC_TAN: DIRT,
        SRC_DTAN: _mat(SOIL_RAMP, "soil"),
        SRC_GREY: _flat(STONE),
        SRC_DGREY: _flat(ROCK),
    },
    "tree": {
        SRC_TAN: DIRT,
        SRC_DTAN: _mat(SOIL_RAMP, "soil"),
        SRC_GREEN: GRASS,
        SRC_DGREEN: HEDGE,
        SRC_GREY: _flat(LEAF),
    },
    "building": {
        SRC_TAN: DIRT,
        SRC_DTAN: _mat(SOIL_RAMP, "soil"),
        SRC_GREEN: GRASS,
        SRC_DGREEN: HEDGE,
        SRC_GREY: CONCRETE,
        SRC_DGREY: SLATE,
        SRC_LGREY: ROOF,
        SRC_LBLUE: GLASS,
        SRC_BLUE: WALL,
        SRC_YELLOW: SIGN,
        WHITE: _flat(WHITE),
        SRC_RED: _flat((255, 0, 0)),
        SRC_ORANGE: _flat(ORANGE),
    },
}


def _require_palette(palette: set[tuple[int, int, int]]) -> None:
    owner: dict[tuple[int, int, int], tuple[int, int, int]] = {}
    for kind, mats in MATS.items():
        for src, mat in mats.items():
            for rgb in mat.ramp:
                if rgb == BLACK:
                    raise SystemExit(f"{kind} {src} uses black; black is reserved for ink")
                if rgb not in palette:
                    raise SystemExit(f"{rgb} is not in the VGA palette ({kind} {src})")
                if rgb in owner and owner[rgb] != src:
                    raise SystemExit(f"{rgb} is used by both source {owner[rgb]} and {src}")
                owner[rgb] = src


def _hash(x: int, y: int) -> int:
    """Full-coordinate hash. The low bits depend on x and y, not on x % 16."""
    n = (x * 374761393 + y * 668265263) & 0xFFFFFFFF
    n ^= n >> 13
    n = (n * 1274126177) & 0xFFFFFFFF
    n ^= n >> 16
    return n


def _dither_index(x: int, y: int, salt: int, weights: tuple[int, ...]) -> int:
    if (_hash(x + salt * 3, y + 19) & 7) == 0:
        h = _hash(x + salt, y + salt * 5)
    else:
        h = _hash((x // 2) + salt * 17, (y // 2) + salt * 29)
    return weights[h % len(weights)]


def _assert_hash_aperiodic() -> None:
    same = 0
    total = 0
    for y in range(48):
        for x in range(48):
            base = _hash(x, y) & 15
            same += base == (_hash(x + 16, y) & 15)
            same += base == (_hash(x, y + 16) & 15)
            total += 2
    if same / total > 0.2:
        raise SystemExit(f"speckle hash still tiles at 16px (match rate {same / total:.3f})")
    clustered = 0
    cluster_total = 0
    for y in range(64):
        for x in range(64):
            here = _dither_index(x, y, 0, _DIRT_WEIGHTS)
            clustered += here == _dither_index(x + 16, y, 0, _DIRT_WEIGHTS)
            clustered += here == _dither_index(x, y + 16, 0, _DIRT_WEIGHTS)
            cluster_total += 2
    if clustered / cluster_total > 0.95:
        raise SystemExit(
            f"ground clusters still tile at 16px (match rate {clustered / cluster_total:.3f})"
        )
    dots = 0
    repeated = 0
    for y in range(64):
        for x in range(64):
            if _dirt_tone(x, y) != 2:
                continue
            dots += 1
            repeated += _dirt_tone(x + 16, y) == 2
            repeated += _dirt_tone(x, y + 16) == 2
    if dots == 0 or repeated / (dots * 2) > 0.2:
        raise SystemExit(f"sand dots still repeat every 16px ({repeated} of {dots * 2})")
    touching = 0
    for y in range(40):
        for x in range(40):
            if _dirt_tone(x, y) != 2:
                continue
            for dx, dy in _ORTHO:
                if _dirt_tone(x + dx, y + dy) == 2:
                    touching += 1
    if touching:
        raise SystemExit(f"sand dots touch their neighbours ({touching})")


def _in(x: int, y: int, w: int, h: int) -> bool:
    return 0 <= x < w and 0 <= y < h


def tile_kind(index: int) -> str:
    if index in (0, 1):
        return "ground"
    if 2 <= index <= 20:
        return "water"
    if 21 <= index <= 37 or 40 <= index <= 43:
        return "tree"
    if 240 <= index <= 404:
        return "building"
    raise SystemExit(f"tile {index} is outside batch 1")


def batch1_indices() -> list[int]:
    ids = [0, 1]
    ids += list(range(2, 21))
    ids += list(range(21, 38))
    ids += list(range(40, 44))
    ids += list(range(240, 405))
    return ids


def _caster_mask(kinds: list[list[str]], src, w: int, h: int) -> list[list[bool]]:
    mask = [[False] * w for _ in range(h)]
    for y in range(h):
        row = y // TILE
        for x in range(w):
            kind = kinds[row][x // TILE]
            colour = src[x, y]
            if kind == "tree" and colour in TREE_CAST:
                mask[y][x] = True
            elif kind == "building" and colour in BUILD_CAST:
                mask[y][x] = True
    return mask


def _caster_counts(src, w: int, h: int, mask: list[list[bool]]) -> dict[tuple[int, int], int]:
    counts: dict[tuple[int, int], int] = {}
    seen = [[False] * w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            if seen[y][x] or not mask[y][x]:
                continue
            colour = src[x, y]
            stack = [(x, y)]
            seen[y][x] = True
            pixels: list[tuple[int, int]] = []
            while stack:
                cx, cy = stack.pop()
                pixels.append((cx, cy))
                for dx, dy in _ORTHO:
                    nx, ny = cx + dx, cy + dy
                    if _in(nx, ny, w, h) and not seen[ny][nx] and mask[ny][nx] and src[nx, ny] == colour:
                        seen[ny][nx] = True
                        stack.append((nx, ny))
            if len(pixels) < _MIN_CAST:
                continue
            for px, py in pixels:
                counts[(px, py)] = len(pixels)
    return counts


def _casts(src, x: int, y: int, w: int, h: int, receiver: tuple[int, int, int], counts) -> bool:
    if not _in(x, y, w, h):
        return False
    if (x, y) in counts and src[x, y] != receiver:
        return True
    if src[x, y] == BLACK:
        for dx, dy in _ORTHO:
            nx, ny = x + dx, y + dy
            if (nx, ny) in counts and src[nx, ny] != receiver:
                return True
    return False


def _shadow_strength(src, x: int, y: int, w: int, h: int, counts) -> int:
    colour = src[x, y]
    if colour not in RECEIVERS:
        return 0
    strength = 0
    for dx, dy in _NW:
        nx, ny = x + dx, y + dy
        if _casts(src, nx, ny, w, h, colour, counts):
            strength = max(strength, 2)
            continue
        if not _in(nx, ny, w, h):
            continue
        mid = src[nx, ny]
        if mid != colour and mid != BLACK:
            continue
        if _casts(src, x + 2 * dx, y + 2 * dy, w, h, colour, counts):
            strength = max(strength, 1)
    return strength


def _apply_shadow(ramp: tuple[tuple[int, int, int], ...], strength: int) -> tuple[int, int, int] | None:
    if strength >= 2:
        return ramp[-1]
    if strength == 1 and len(ramp) >= 2:
        return ramp[-2]
    if strength == 1:
        return ramp[-1]
    return None


def _grass_rim(src, x: int, y: int, w: int, h: int, counts) -> bool:
    for dx, dy in _ORTHO:
        nx, ny = x + dx, y + dy
        if not _in(nx, ny, w, h):
            continue
        neighbour = src[nx, ny]
        if (nx, ny) in counts or neighbour in _PAVEMENT:
            return True
        if neighbour == BLACK:
            fx, fy = nx + dx, ny + dy
            if _in(fx, fy, w, h) and ((fx, fy) in counts or src[fx, fy] in _PAVEMENT):
                return True
    return False


def _touches_water(src, x: int, y: int, w: int, h: int) -> bool:
    """Ground pixel against open water, including through one black outline."""
    for dx, dy in _ORTHO:
        nx, ny = x + dx, y + dy
        if not _in(nx, ny, w, h):
            continue
        neighbour = src[nx, ny]
        if neighbour in (SRC_LBLUE, SRC_BLUE):
            return True
        if neighbour == BLACK:
            fx, fy = nx + dx, ny + dy
            if _in(fx, fy, w, h) and src[fx, fy] in (SRC_LBLUE, SRC_BLUE):
                return True
    return False


def _dirt_tone(x: int, y: int) -> int:
    """0 rare light, 1 flat base, 2 isolated darker dot.

    The lattices are in world pixels. Their periods (3 and 7) are not 16, so
    a tile edge does not restart the pattern.
    """
    if (x % 3) == 1 and (y % 3) == 1 and (_hash(x // 3, y // 3) % 5) != 0:
        return 2
    if (x % 7) == 3 and (y % 7) == 5:
        return 0
    return 1


def _touches_ground(src, x: int, y: int, w: int, h: int, colours: frozenset) -> bool:
    for dx, dy in _ORTHO:
        nx, ny = x + dx, y + dy
        if not _in(nx, ny, w, h):
            continue
        neighbour = src[nx, ny]
        if neighbour in colours:
            return True
        if neighbour == BLACK:
            fx, fy = nx + dx, ny + dy
            if _in(fx, fy, w, h) and src[fx, fy] in colours:
                return True
    return False


def _dirt_rim(src, x: int, y: int, w: int, h: int) -> bool:
    """1px darker tan where sand meets concrete. Soil carries its own rim."""
    return _touches_ground(src, x, y, w, h, _PAVEMENT)


def _is_land(colour: tuple[int, int, int]) -> bool:
    return colour not in WATER_FAMILY


def _coast_strength(src, x: int, y: int, w: int, h: int) -> int:
    """2 on water touching land, 1 one pixel further out. Black outlines count."""
    strength = 0
    for dx, dy in _ORTHO:
        nx, ny = x + dx, y + dy
        if not _in(nx, ny, w, h):
            continue
        neighbour = src[nx, ny]
        if _is_land(neighbour):
            strength = 2
            continue
        if neighbour == BLACK:
            fx, fy = nx + dx, ny + dy
            if _in(fx, fy, w, h) and _is_land(src[fx, fy]):
                strength = max(strength, 2)
            continue
        fx, fy = nx + dx, ny + dy
        if not _in(fx, fy, w, h):
            continue
        beyond = src[fx, fy]
        if _is_land(beyond):
            strength = max(strength, 1)
        elif beyond == BLACK:
            gx, gy = fx + dx, fy + dy
            if _in(gx, gy, w, h) and _is_land(src[gx, gy]):
                strength = max(strength, 1)
    return strength


def _same_count(src, x: int, y: int, w: int, h: int) -> int:
    colour = src[x, y]
    count = 0
    for dx, dy in _ORTHO:
        nx, ny = x + dx, y + dy
        if _in(nx, ny, w, h) and src[nx, ny] == colour:
            count += 1
    return count


def _component_boxes(src, w: int, h: int, colours: set[tuple[int, int, int]]):
    boxes: dict[tuple[int, int], tuple[int, int, int, int]] = {}
    seen = [[False] * w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            if seen[y][x] or src[x, y] not in colours:
                continue
            colour = src[x, y]
            stack = [(x, y)]
            seen[y][x] = True
            pixels: list[tuple[int, int]] = []
            x0 = x1 = x
            y0 = y1 = y
            while stack:
                cx, cy = stack.pop()
                pixels.append((cx, cy))
                x0, y0 = min(x0, cx), min(y0, cy)
                x1, y1 = max(x1, cx), max(y1, cy)
                for dx, dy in _ORTHO:
                    nx, ny = cx + dx, cy + dy
                    if _in(nx, ny, w, h) and not seen[ny][nx] and src[nx, ny] == colour:
                        seen[ny][nx] = True
                        stack.append((nx, ny))
            box = (x0, y0, x1, y1)
            for px, py in pixels:
                boxes[(px, py)] = box
    return boxes


def _paint_surface(src, x: int, y: int, w: int, h: int, ramp: tuple, box: tuple[int, int, int, int]):
    n = len(ramp)
    if n == 1:
        return ramp[0]
    x0, y0, x1, y1 = box
    tx = (x - x0) / max(1, x1 - x0)
    ty = (y - y0) / max(1, y1 - y0)
    t = (tx + ty) / 2
    if n >= 4:
        idx = 1 + int(t * (n - 3) + 0.5)
        idx = max(1, min(n - 2, idx))
    else:
        idx = int(t * (n - 2) + 0.5)
        idx = max(0, min(n - 2, idx))
    if _same_count(src, x, y, w, h) >= 2:
        colour = src[x, y]
        nw = any(_in(x + dx, y + dy, w, h) and src[x + dx, y + dy] != colour for dx, dy in _NW)
        se = any(_in(x + dx, y + dy, w, h) and src[x + dx, y + dy] != colour for dx, dy in _SE)
        if nw and not se:
            idx -= 1
        elif se and not nw:
            idx += 1
    return ramp[max(0, min(n - 1, idx))]


def paint(original: Image.Image, kinds: list[list[str]]) -> Image.Image:
    """Recolour `original`. `kinds` is one label per 16px cell, row-major."""
    w, h = original.size
    if w % TILE or h % TILE:
        raise SystemExit(f"image {w}x{h} is not a multiple of {TILE}")
    src = original.load()
    out = Image.new("RGB", (w, h))
    dst = out.load()
    surface_colours = {SRC_GREY, SRC_DGREY, SRC_LGREY, SRC_LBLUE, SRC_BLUE, SRC_YELLOW}
    boxes = _component_boxes(src, w, h, surface_colours)
    mask = _caster_mask(kinds, src, w, h)
    counts = _caster_counts(src, w, h, mask)
    for y in range(h):
        kind_row = kinds[y // TILE]
        for x in range(w):
            colour = src[x, y]
            if colour == BLACK:
                dst[x, y] = BLACK
                continue
            mat = MATS[kind_row[x // TILE]].get(colour)
            if mat is None:
                raise SystemExit(
                    f"no ramp for {colour} at {(x, y)} kind {kind_row[x // TILE]}"
                )
            ramp = mat.ramp
            if mat.role == "flat":
                dst[x, y] = ramp[0]
                continue
            if mat.role == "soil":
                # Open soil is the same sand as tan. Only the pixels that touch
                # tan, concrete, or water change, so a soil fill cannot stay
                # the old dark brown and cannot share an edge colour with tan.
                strength = _shadow_strength(src, x, y, w, h, counts)
                if _touches_water(src, x, y, w, h):
                    chosen = SOIL_SAND
                elif strength >= 2:
                    chosen = SOIL_CONTACT
                elif strength == 1 or _touches_ground(src, x, y, w, h, frozenset({SRC_TAN}) | _PAVEMENT):
                    chosen = SOIL_RIM
                else:
                    chosen = DIRT_RAMP[_dirt_tone(x, y)]
                dst[x, y] = chosen
                continue
            strength = _shadow_strength(src, x, y, w, h, counts)
            shadowed = _apply_shadow(ramp, strength) if mat.role in ("grass", "dirt", "hedge") else None
            if mat.role == "hedge":
                body = ramp[:-2]
                idx = 0 if (_hash(x, y) & 7) < 2 else min(1, len(body) - 1)
                if _grass_rim(src, x, y, w, h, counts):
                    idx = len(body) - 1
                chosen = body[idx]
                if shadowed is not None:
                    chosen = shadowed
                dst[x, y] = chosen
                continue
            if mat.role == "dirt":
                chosen = ramp[_dirt_tone(x, y)]
                if _touches_water(src, x, y, w, h):
                    chosen = ramp[0]
                elif _dirt_rim(src, x, y, w, h):
                    chosen = ramp[-2]
                if shadowed is not None:
                    chosen = shadowed
                dst[x, y] = chosen
                continue
            if mat.role == "grass":
                body = ramp[:-2]
                idx = _dither_index(x, y, 1, _GRASS_WEIGHTS)
                if _grass_rim(src, x, y, w, h, counts):
                    idx = len(body) - 1
                chosen = body[min(idx, len(body) - 1)]
                if shadowed is not None:
                    chosen = shadowed
                dst[x, y] = chosen
                continue
            if mat.role == "water":
                body = ramp[:3]
                idx = _dither_index(x, y, 2, _WATER_WEIGHTS)
                chosen = body[min(idx, len(body) - 1)]
                coast = _coast_strength(src, x, y, w, h)
                if coast >= 2:
                    chosen = ramp[4]
                elif coast == 1:
                    chosen = ramp[3]
                dst[x, y] = chosen
                continue
            if mat.role == "surface":
                dst[x, y] = _paint_surface(src, x, y, w, h, ramp, boxes.get((x, y), (x, y, x, y)))
                continue
            raise SystemExit(f"unknown role {mat.role}")
    return out


def load_atlas() -> Image.Image:
    if not ATLAS_PATH.is_file():
        raise SystemExit(f"missing tile atlas: {ATLAS_PATH}")
    image = Image.open(ATLAS_PATH).convert("RGB")
    if image.size[0] % TILE or image.size[1] % TILE:
        raise SystemExit(f"atlas {image.size} is not a multiple of {TILE}")
    return image


def crop_tile(atlas: Image.Image, index: int) -> Image.Image:
    columns = atlas.size[0] // TILE
    col, row = index % columns, index // columns
    box = (col * TILE, row * TILE, col * TILE + TILE, row * TILE + TILE)
    return atlas.crop(box)


def assemble(atlas: Image.Image, base: int, width: int, height: int) -> Image.Image:
    image = Image.new("RGB", (width * TILE, height * TILE))
    for y in range(height):
        for x in range(width):
            image.paste(crop_tile(atlas, base + y * width + x), (x * TILE, y * TILE))
    return image


def kinds_for(indexes: list[list[int]]) -> list[list[str]]:
    return [[tile_kind(index) for index in row] for row in indexes]


def paint_grid(atlas: Image.Image, grid: list[list[int]]) -> tuple[Image.Image, Image.Image]:
    height = len(grid)
    width = len(grid[0])
    classic = Image.new("RGB", (width * TILE, height * TILE))
    for y, row in enumerate(grid):
        for x, index in enumerate(row):
            classic.paste(crop_tile(atlas, index), (x * TILE, y * TILE))
    return classic, paint(classic, kinds_for(grid))


def luma(rgb: tuple[int, int, int]) -> float:
    red, green, blue = rgb
    return 0.299 * red + 0.587 * green + 0.114 * blue


def compare(classic: Image.Image, svga: Image.Image, palette: set[tuple[int, int, int]]) -> dict:
    cw, ch = classic.size
    if svga.size != classic.size:
        raise RuntimeError(f"size {svga.size} != classic {classic.size}")
    cpx = classic.load()
    spx = svga.load()
    stats = {
        "palette_bad": 0,
        "ink_bad": 0,
        "collapsed": 0,
        "boundaries": 0,
        "strong": 0,
        "strong_kept": 0,
        "new_strong": 0,
        "pixels": cw * ch,
    }
    samples: list[str] = []
    for y in range(ch):
        for x in range(cw):
            src = cpx[x, y]
            dst = spx[x, y]
            if dst not in palette:
                stats["palette_bad"] += 1
                if len(samples) < 6:
                    samples.append(f"palette {dst} at {(x, y)} from {src}")
            if (src == BLACK) != (dst == BLACK):
                stats["ink_bad"] += 1
                if len(samples) < 6:
                    samples.append(f"ink {src} -> {dst} at {(x, y)}")
            for nx, ny in ((x + 1, y), (x, y + 1)):
                if nx >= cw or ny >= ch:
                    continue
                src2 = cpx[nx, ny]
                dst2 = spx[nx, ny]
                if src != src2:
                    stats["boundaries"] += 1
                    if dst == dst2:
                        stats["collapsed"] += 1
                        if len(samples) < 8:
                            samples.append(
                                f"collapsed {src}/{src2} -> {dst} at {(x, y)}-{(nx, ny)}"
                            )
                classic_delta = abs(luma(src) - luma(src2))
                svga_delta = abs(luma(dst) - luma(dst2))
                if classic_delta >= STRONG_LUMA:
                    stats["strong"] += 1
                    if svga_delta >= STRONG_LUMA * 0.5:
                        stats["strong_kept"] += 1
                elif classic_delta < 12 and svga_delta >= STRONG_LUMA:
                    stats["new_strong"] += 1
    stats["samples"] = samples
    return stats


def _ok(stats: dict) -> bool:
    return stats["palette_bad"] == 0 and stats["ink_bad"] == 0 and stats["collapsed"] == 0


def _stamp(grid: list[list[int]], index: int, x: int, y: int, width: int = 1, height: int = 1) -> None:
    for dy in range(height):
        for dx in range(width):
            if grid[y + dy][x + dx] != 0:
                raise SystemExit(f"scene overlap at {(x + dx, y + dy)}")
            grid[y + dy][x + dx] = index + dy * width + dx


def neighbourhood() -> list[list[int]]:
    """A riverbank with woods, an empty lot, and houses. Roads are a later batch."""
    width, height = 22, 15
    grid = [[0 for _ in range(width)] for _ in range(height)]
    for x in range(width):
        grid[0][x] = 2
        grid[1][x] = 13 if x % 2 == 0 else 14
    for x, index in ((2, 21), (5, 22), (8, 28), (10, 1), (15, 36), (19, 21)):
        _stamp(grid, index, x, 3)
    woods = (
        (37, 40, 41, 42),
        (43, 37, 40, 41),
        (42, 43, 40, 37),
    )
    for dy, row in enumerate(woods):
        for dx, index in enumerate(row):
            _stamp(grid, index, 1 + dx, 4 + dy)
    _stamp(grid, 240, 16, 4, 3, 3)
    for x, y, index in (
        (7, 5, 22),
        (8, 6, 21),
        (12, 4, 36),
        (13, 5, 28),
        (14, 6, 22),
        (6, 8, 30),
        (20, 10, 28),
        (5, 12, 21),
        (8, 13, 21),
        (20, 13, 28),
        (3, 14, 22),
        (10, 14, 36),
    ):
        _stamp(grid, index, x, y)
    _stamp(grid, 261, 1, 9, 3, 3)
    _stamp(grid, 315, 8, 9, 3, 3)
    _stamp(grid, 378, 15, 9, 3, 3)
    _stamp(grid, 252, 20, 7)
    for x, index in ((2, 249), (4, 250), (6, 255), (11, 258), (13, 260), (18, 252)):
        _stamp(grid, index, x, 13)
    return grid


def _blank(width: int, height: int, colour: tuple[int, int, int] = BG) -> Image.Image:
    return Image.new("RGB", (width, height), colour)


def _draw_text(image: Image.Image, text: str, x: int, y: int, scale: int, colour: tuple[int, int, int]) -> None:
    px = image.load()
    width, height = image.size
    for char in text:
        glyph = FONT.get(char, FONT[" "])
        for gy, row in enumerate(glyph):
            for gx, bit in enumerate(row):
                if bit != "#":
                    continue
                for sy in range(scale):
                    for sx in range(scale):
                        px_x = x + gx * scale + sx
                        px_y = y + gy * scale + sy
                        if 0 <= px_x < width and 0 <= px_y < height:
                            px[px_x, px_y] = colour
        x += 6 * scale


def _text(text: str, scale: int = 2, colour: tuple[int, int, int] = FG) -> Image.Image:
    width = max(1, (len(text) * 6 - 1) * scale)
    image = _blank(width, 7 * scale)
    _draw_text(image, text, 0, 0, scale, colour)
    return image


def _hstack(images: list[Image.Image], gap: int) -> Image.Image:
    height = max(image.height for image in images)
    width = sum(image.width for image in images) + gap * (len(images) - 1)
    out = _blank(width, height)
    x = 0
    for image in images:
        out.paste(image, (x, (height - image.height) // 2))
        x += image.width + gap
    return out


def _vstack(images: list[Image.Image], gap: int) -> Image.Image:
    width = max(image.width for image in images)
    height = sum(image.height for image in images) + gap * (len(images) - 1)
    out = _blank(width, height)
    y = 0
    for image in images:
        out.paste(image, (0, y))
        y += image.height + gap
    return out


def _fit_width(image: Image.Image, width: int) -> Image.Image:
    if image.width == width:
        return image
    out = _blank(width, image.height)
    out.paste(image, (0, 0))
    return out


def _banner(text: str, width: int) -> Image.Image:
    label = _text(text, 2)
    bar = _blank(width, label.height + 14)
    bar.paste(label, (8, 7))
    return bar


def _scaled(image: Image.Image, scale: int) -> Image.Image:
    return image.resize((image.width * scale, image.height * scale), NEAREST)


def _pair(classic: Image.Image, svga: Image.Image, caption: str, scale: int) -> Image.Image:
    left = _scaled(classic, scale)
    right = _scaled(svga, scale)
    pair = _hstack([left, right], 4)
    label = _text(caption, 2)
    width = max(pair.width, label.width)
    cell = _blank(width, label.height + 4 + pair.height)
    cell.paste(label, ((width - label.width) // 2, 0))
    cell.paste(pair, ((width - pair.width) // 2, label.height + 4))
    return cell


def _grid(cells: list[Image.Image], columns: int) -> Image.Image:
    rows = []
    for start in range(0, len(cells), columns):
        rows.append(_hstack(cells[start : start + columns], 12))
    return _vstack(rows, 12)


def _save(image: Image.Image, path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    image.save(path, optimize=True)


def _histogram(image: Image.Image) -> dict[tuple[int, int, int], int]:
    counts: dict[tuple[int, int, int], int] = {}
    px = image.load()
    for y in range(image.height):
        for x in range(image.width):
            rgb = px[x, y]
            counts[rgb] = counts.get(rgb, 0) + 1
    return counts


def main() -> int:
    _assert_hash_aperiodic()
    palette = set(vga_palette_8bit())
    _require_palette(palette)
    atlas = load_atlas()
    lines = [
        "SVGA batch 1 verification",
        "groups: dirt, marked dirt, water and coast, trees and woods, residential 240-404",
        "held back: tiles 38-39 (unused), rubble, flood, fire, roads, rails, power,",
        "commercial, industrial, hospital, church, civic buildings, animations, the menu toggle",
        f"palette unique RGB triples: {len(palette)}",
        "sand base (255, 190, 125): distance 64.0 from (195, 170, 135).",
        "nearest palette colour is roof grey (170, 170, 170) at 43.0, not used.",
        "nearest warm colour (255, 158, 125) is 62.0 but reads salmon, so it is only the dot.",
        "dot target (150, 120, 90): nearest is olive (113, 113, 81) at 38.7.",
        "that olive is far darker than the sand, so the dot is (255, 158, 125), distance 117.0,",
        "one VGA step below the base. Rare light is (255, 223, 125).",
        "shadows (113, 69, 56) and (113, 56, 0). Soil rim (113, 85, 56),",
        "soil shore (255, 235, 182), soil contact (113, 85, 0).",
        "grass hues unchanged; weights now 2/9 of (0, 170, 0) so lawns sit nearer the light sand.",
        "",
    ]
    failed = False

    def check(name: str, classic: Image.Image, svga: Image.Image) -> None:
        nonlocal failed
        stats = compare(classic, svga, palette)
        recall = (stats["strong_kept"] / stats["strong"]) if stats["strong"] else 1.0
        status = "OK" if _ok(stats) else "FAIL"
        if status == "FAIL":
            failed = True
        lines.append(
            f"{status} {name}: palette={stats['palette_bad']} ink={stats['ink_bad']} "
            f"collapsed={stats['collapsed']}/{stats['boundaries']} "
            f"strong={stats['strong_kept']}/{stats['strong']} ({recall:.3f})"
        )
        for sample in stats["samples"]:
            lines.append(f"    {sample}")

    singles = [0, 1, *range(2, 21), *range(21, 38), *range(40, 44), *range(249, 261)]
    for index in singles:
        classic = crop_tile(atlas, index)
        svga = paint(classic, [[tile_kind(index)]])
        check(f"tile {index}", classic, svga)

    blocks = [(240, 3, 3)] + [(261 + i * 9, 3, 3) for i in range(16)]
    painted_blocks: list[tuple[int, Image.Image, Image.Image]] = []
    for base, width, height in blocks:
        classic = assemble(atlas, base, width, height)
        kinds = [[tile_kind(base)] * width for _ in range(height)]
        svga = paint(classic, kinds)
        painted_blocks.append((base, classic, svga))
        check(f"block {base} {width}x{height}", classic, svga)

    scene = neighbourhood()
    classic_scene, svga_scene = paint_grid(atlas, scene)
    check(f"neighbourhood {len(scene[0])}x{len(scene)}", classic_scene, svga_scene)

    shore_rows = [row[:] for row in scene[:4]]
    classic_shore, svga_shore = paint_grid(atlas, shore_rows)
    check("shore strip", classic_shore, svga_shore)

    open_water = paint(crop_tile(atlas, 2), [["water"]])
    shore = paint(crop_tile(atlas, 13), [["water"]])
    if _histogram(open_water).get(WATER_RAMP[4], 0) or _histogram(open_water).get(WATER_RAMP[3], 0):
        lines.append("FAIL open water picked up a coastline colour")
        failed = True
    if _histogram(shore).get(WATER_RAMP[4], 0) == 0:
        lines.append("FAIL south shore has no coastline band")
        failed = True

    dirt = paint(crop_tile(atlas, 0), [["ground"]])
    dirt_counts = _histogram(dirt)
    base_n = dirt_counts.get(DIRT_BASE, 0)
    if base_n < 180:
        lines.append(f"FAIL sand base is not the flat field: {dirt_counts}")
        failed = True
    dot_n = dirt_counts.get(DIRT_DOT, 0)
    if dot_n < 8 or dot_n > 40:
        lines.append(f"FAIL sand dots are not a sparse stipple ({dot_n})")
        failed = True
    forbidden = {
        (85, 255, 85),
        (113, 113, 56),
        (113, 97, 56),
        (113, 56, 56),
        (113, 28, 0),
        (170, 85, 0),
        (255, 127, 0),
        SRC_TAN,
        SRC_DTAN,
    }
    for image, name in ((svga_scene, "neighbourhood"), (dirt, "dirt")):
        present = forbidden & set(_histogram(image))
        if present:
            lines.append(f"FAIL {name} contains forbidden colours {sorted(present)}")
            failed = True
    for base in (261, 279, 315, 351):
        block = next(image for origin, _classic, image in painted_blocks if origin == base)
        present = forbidden & set(_histogram(block))
        if present:
            lines.append(f"FAIL block {base} still has classic or reddish ground {sorted(present)}")
            failed = True
        classic = next(image for origin, image, _svga in painted_blocks if origin == base)
        cpx = classic.load()
        spx = block.load()
        missed = 0
        for y in range(classic.height):
            for x in range(classic.width):
                if cpx[x, y] in (SRC_TAN, SRC_DTAN) and spx[x, y] in (SRC_TAN, SRC_DTAN):
                    missed += 1
        if missed:
            lines.append(f"FAIL block {base} left {missed} classic ground pixels")
            failed = True

    # Previews. Each pair is classic on the left and SVGA on the right.
    water_cells = []
    for index in [0, 1, *range(2, 21)]:
        classic = crop_tile(atlas, index)
        svga = paint(classic, [[tile_kind(index)]])
        water_cells.append(_pair(classic, svga, str(index), 3))
    body_w = max(_scaled(classic_shore, 3).width, _grid(water_cells, 7).width)
    terrain = _vstack(
        [
            _banner("TERRAIN WATER AND COAST  3X", body_w),
            _banner("SHORE SAMPLE  CLASSIC ABOVE  SVGA BELOW", body_w),
            _vstack([_text("CLASSIC", 2), _scaled(classic_shore, 3)], 6),
            _vstack([_text("SVGA", 2), _scaled(svga_shore, 3)], 6),
            _banner("EACH TILE  CLASSIC LEFT  SVGA RIGHT", body_w),
            _fit_width(_grid(water_cells, 7), body_w),
        ],
        14,
    )

    tree_cells = []
    for index in [*range(21, 38), *range(40, 44)]:
        classic = crop_tile(atlas, index)
        svga = paint(classic, [[tile_kind(index)]])
        tree_cells.append(_pair(classic, svga, str(index), 3))
    tree_grid = _grid(tree_cells, 7)
    trees = _vstack(
        [
            _banner("TREES AND WOODS  3X  CLASSIC LEFT  SVGA RIGHT", tree_grid.width),
            tree_grid,
        ],
        14,
    )

    house_cells = []
    for index in range(249, 261):
        classic = crop_tile(atlas, index)
        svga = paint(classic, [["building"]])
        house_cells.append(_pair(classic, svga, str(index), 3))
    house_grid = _grid(house_cells, 6)
    block_cells = [_pair(classic, svga, str(base), 3) for base, classic, svga in painted_blocks]
    block_grid = _grid(block_cells, 4)
    res_w = max(house_grid.width, block_grid.width)
    residential = _vstack(
        [
            _banner("RESIDENTIAL  3X  CLASSIC LEFT  SVGA RIGHT", res_w),
            _banner("HOUSES 249-260", res_w),
            _fit_width(house_grid, res_w),
            _banner("EMPTY LOT 240 AND 3X3 BLOCKS", res_w),
            _fit_width(block_grid, res_w),
        ],
        14,
    )

    classic_panel = _vstack([_text("CLASSIC", 2), _scaled(classic_scene, 2)], 8)
    svga_panel = _vstack([_text("SVGA", 2), _scaled(svga_scene, 2)], 8)
    panels = _hstack([classic_panel, svga_panel], 24)
    neighbourhood_sheet = _vstack(
        [
            _banner("NEIGHBOURHOOD  32PX TILES  COMPOSED RIVERBANK", panels.width),
            panels,
        ],
        12,
    )

    _save(terrain, PREVIEW_DIR / "terrain-water-3x.png")
    _save(trees, PREVIEW_DIR / "trees-3x.png")
    _save(residential, PREVIEW_DIR / "residential-3x.png")
    _save(neighbourhood_sheet, PREVIEW_DIR / "neighbourhood-32.png")

    lines.append("")
    lines.append(
        f"dirt tile 0: { {str(k): v for k, v in sorted(dirt_counts.items(), key=lambda kv: -kv[1])} }"
    )
    lines.append(f"south shore tile 13: { {str(k): v for k, v in sorted(_histogram(shore).items(), key=lambda kv: -kv[1])} }")
    lines.append("")
    lines.append("previews:")
    for name in (
        "terrain-water-3x.png",
        "trees-3x.png",
        "residential-3x.png",
        "neighbourhood-32.png",
    ):
        lines.append(f"  previews/{name}")
    result = "PASS" if not failed else "FAIL"
    lines.append("")
    lines.append(f"RESULT: {result}")
    report = "\n".join(lines) + "\n"
    REPORT_PATH.write_text(report, encoding="utf-8")
    sys.stdout.write(report)
    return 0 if not failed else 1


if __name__ == "__main__":
    sys.exit(main())
