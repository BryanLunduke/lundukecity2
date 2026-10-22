#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Lunduke City authors
# See COPYING and NOTICE.

"""SVGA-look sample recolour of the Micropolis police station and airport.

Classic pixels are not redrawn. Each output pixel keeps the source tile's
flat-colour region and is painted from a VGA Mode 13h ramp. Black ink stays
black, so outlines do not move. Run from the repository root or from this
directory:

    python3 art/svga-sample/make_svga_sample.py
"""

from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path

from PIL import Image, ImageDraw

from vga_palette import vga_dac_6bit, vga_palette_8bit, write_gpl

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
ATLAS_PATH = ROOT / "third_party/micropolis-assets/images/micropolisEngine/tiles.png"
TILE = 16
NEAREST = Image.Resampling.NEAREST

# Atlas indices. Police is 3x3, row-major, with no animation frames.
# Airport is 6x6, row-major. The radar cell is (2, 0) = tile 711.
# Powered radar cycles 832..839 (RADAR0..RADAR7).
POLICE_BASE = 770
POLICE_SIZE = (3, 3)
AIRPORT_BASE = 709
AIRPORT_SIZE = (6, 6)
RADAR_CELL = (2, 0)
RADAR_FRAMES = list(range(832, 840))
DIRT = 0
ROAD_H = 66  # horizontal road, dirt shoulders

BLACK = (0, 0, 0)
WHITE = (255, 255, 255)

# Source flat colours in tiles.png.
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

# 5x7 glyphs. Only the labels used on the comparison sheets.
FONT = {
    " ": ["     ", "     ", "     ", "     ", "     ", "     ", "     "],
    "A": [" ### ", "#   #", "#   #", "#####", "#   #", "#   #", "#   #"],
    "C": [" ### ", "#   #", "#    ", "#    ", "#    ", "#   #", " ### "],
    "D": ["#### ", "#   #", "#   #", "#   #", "#   #", "#   #", "#### "],
    "E": ["#####", "#    ", "#    ", "#### ", "#    ", "#    ", "#####"],
    "G": [" ### ", "#   #", "#    ", "# ###", "#   #", "#   #", " ### "],
    "I": ["#####", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", "#####"],
    "K": ["#   #", "#  # ", "# #  ", "##   ", "# #  ", "#  # ", "#   #"],
    "L": ["#    ", "#    ", "#    ", "#    ", "#    ", "#    ", "#####"],
    "M": ["#   #", "## ##", "# # #", "#   #", "#   #", "#   #", "#   #"],
    "O": [" ### ", "#   #", "#   #", "#   #", "#   #", "#   #", " ### "],
    "P": ["#### ", "#   #", "#   #", "#### ", "#    ", "#    ", "#    "],
    "R": ["#### ", "#   #", "#   #", "#### ", "# #  ", "#  # ", "#   #"],
    "S": [" ####", "#    ", "#    ", " ### ", "    #", "    #", "#### "],
    "T": ["#####", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", "  #  "],
    "V": ["#   #", "#   #", "#   #", "#   #", "#   #", " # # ", "  #  "],
    "X": ["#   #", "#   #", " # # ", "  #  ", " # # ", "#   #", "#   #"],
    "Y": ["#   #", "#   #", " # # ", "  #  ", "  #  ", "  #  ", "  #  "],
    "0": [" ### ", "#   #", "#  ##", "# # #", "##  #", "#   #", " ### "],
    "1": ["  #  ", " ##  ", "  #  ", "  #  ", "  #  ", "  #  ", " ### "],
    "2": [" ### ", "#   #", "    #", "   # ", "  #  ", " #   ", "#####"],
    "3": ["#### ", "    #", "    #", " ### ", "    #", "    #", "#### "],
    "6": [" ### ", "#    ", "#    ", "#### ", "#   #", "#   #", " ### "],
    "-": ["     ", "     ", "     ", "#####", "     ", "     ", "     "],
}


@dataclass(frozen=True)
class Material:
    """One source flat colour, repainted from a light-to-dark VGA ramp.

    role:
      flat     one entry, for marks that must stay crisp
      grass    soft 1px speckle, darker where a building or pavement casts
      dirt     warm speckle, same shadow rule
      asphalt  smooth body, 1px lit edge on the north/west, dark on the south/east
      surface  roof or wall: gradient from the upper left, darker on the south/east
    """

    ramp: tuple[tuple[int, int, int], ...]
    role: str = "flat"


@dataclass(frozen=True)
class Style:
    name: str
    materials: dict[tuple[int, int, int], Material]
    casters: frozenset[tuple[int, int, int]]
    receivers: frozenset[tuple[int, int, int]]
    # Dark-grey pixels that touch this colour (through other dark grey) stay ink.
    ink_touch: tuple[int, int, int] | None = None
    ink: tuple[int, int, int] | None = None


def _require_in_palette(colours: set[tuple[int, int, int]], palette: set[tuple[int, int, int]]) -> None:
    missing = sorted(colours - palette)
    if missing:
        raise SystemExit(f"ramp colour is not in the VGA palette: {missing}")


def _assert_disjoint(style: Style) -> None:
    owner: dict[tuple[int, int, int], str] = {}

    def claim(rgb: tuple[int, int, int], who: str) -> None:
        if rgb == BLACK:
            raise SystemExit(f"{who} uses black; black is reserved for ink")
        if rgb in owner and owner[rgb] != who:
            raise SystemExit(f"{rgb} is used by both {owner[rgb]} and {who}")
        owner[rgb] = who

    for src, mat in style.materials.items():
        who = f"{style.name}:{src}"
        for rgb in mat.ramp:
            claim(rgb, who)
    if style.ink is not None:
        claim(style.ink, f"{style.name}:ink")


def _hash(x: int, y: int) -> int:
    """Full-coordinate hash. The low bits depend on x and y, not on x%16."""
    n = (x * 374761393 + y * 668265263) & 0xFFFFFFFF
    n ^= n >> 13
    n = (n * 1274126177) & 0xFFFFFFFF
    n ^= n >> 16
    return n


def police_style() -> Style:
    # Fresh greens near the classic brightness. Less saturated than pure neon.
    # The last two are only the darker edge and the cast shadow.
    grass = Material(
        ramp=(
            (125, 255, 125),
            (85, 255, 85),
            (0, 255, 65),
            (0, 255, 0),
            (0, 170, 0),
        ),
        role="grass",
    )
    hedge = Material(
        ramp=(
            (44, 65, 44),
            (32, 65, 32),
            (16, 65, 0),
            (0, 65, 0),
        ),
        role="hedge",
    )
    # Light blue trim and roof, kept on the blue axis (red == green).
    roof = Material(
        ramp=(
            (125, 125, 255),
            (85, 85, 255),
            (56, 56, 113),
        ),
        role="surface",
    )
    wall = Material(
        ramp=(
            (0, 0, 255),
            (0, 0, 170),
            (0, 0, 113),
        ),
        role="surface",
    )
    # Forecourt, flat roof and driveway. Neutral greys around the classic 127.
    concrete = Material(
        ramp=(
            (162, 162, 162),
            (146, 146, 146),
            (130, 130, 130),
            (113, 113, 113),
        ),
        role="surface",
    )
    # Left wing. Sign letters that touch the yellow are pulled out as ink.
    slate = Material(
        ramp=(
            (81, 81, 81),
            (69, 69, 69),
            (56, 56, 56),
        ),
        role="surface",
    )
    sign = Material(
        ramp=(
            (255, 255, 125),
            (255, 255, 85),
            (255, 255, 0),
        ),
        role="surface",
    )
    return Style(
        name="police",
        materials={
            SRC_GREEN: grass,
            SRC_DGREEN: hedge,
            SRC_LBLUE: roof,
            SRC_BLUE: wall,
            SRC_GREY: concrete,
            SRC_DGREY: slate,
            SRC_YELLOW: sign,
            SRC_LGREY: Material(ramp=((203, 203, 203),)),
            WHITE: Material(ramp=(WHITE,)),
            SRC_RED: Material(ramp=((255, 0, 0),)),
        },
        # Lights and the small highlight do not cast. The wing, walls, roof,
        # sign, and hedge do.
        casters=frozenset({SRC_LBLUE, SRC_BLUE, SRC_DGREY, SRC_YELLOW, SRC_DGREEN}),
        receivers=frozenset({SRC_GREEN, SRC_DGREEN, SRC_GREY}),
        ink_touch=SRC_YELLOW,
        ink=(32, 32, 32),
    )


def airport_style() -> Style:
    # Classic tan is (204, 127, 102). These three are the close VGA salmons:
    # same red-over-green-over-blue hue, a little lighter, low contrast.
    # The last entry is only the cast shadow.
    dirt = Material(
        ramp=(
            (255, 190, 125),
            (255, 158, 125),
            (255, 125, 125),
            (255, 85, 85),
        ),
        role="dirt",
    )
    # A step darker than the lot, still a brown, not a pit beside the walls.
    soil = Material(ramp=((113, 85, 0),))
    # Smooth asphalt. Classic runway grey is 63; these three sit against it.
    tarmac = Material(
        ramp=(
            (69, 69, 69),
            (56, 56, 56),
            (44, 44, 44),
        ),
        role="asphalt",
    )
    concrete = Material(
        ramp=(
            (162, 162, 162),
            (146, 146, 146),
            (130, 130, 130),
            (113, 113, 113),
        ),
        role="surface",
    )
    # Neutral light grey. The classic roof is 191; white panes stay pure white.
    roof = Material(
        ramp=(
            (227, 227, 227),
            (203, 203, 203),
            (182, 182, 182),
            (170, 170, 170),
        ),
        role="surface",
    )
    glass = Material(
        ramp=(
            (125, 125, 255),
            (85, 85, 255),
            (56, 56, 113),
        ),
        role="surface",
    )
    deep = Material(
        ramp=(
            (0, 0, 255),
            (0, 0, 170),
            (0, 0, 113),
        ),
        role="surface",
    )
    return Style(
        name="airport",
        materials={
            SRC_TAN: dirt,
            SRC_DTAN: soil,
            SRC_DGREY: tarmac,
            SRC_GREY: concrete,
            SRC_LGREY: roof,
            SRC_LBLUE: glass,
            SRC_BLUE: deep,
            SRC_YELLOW: Material(ramp=((255, 255, 0),)),
            WHITE: Material(ramp=(WHITE,)),
            SRC_RED: Material(ramp=((255, 0, 0),)),
            SRC_ORANGE: Material(ramp=((255, 125, 0),)),
            SRC_DGREEN: Material(ramp=((0, 65, 0),)),
        },
        # Paint (yellow, white dashes, lights, orange) stays flat and does not cast.
        casters=frozenset({SRC_GREY, SRC_LGREY, SRC_LBLUE, SRC_BLUE}),
        receivers=frozenset({SRC_TAN, SRC_DTAN, SRC_DGREY}),
    )


def terrain_style() -> Style:
    """Dirt and the horizontal road, for the sample scenes only.

    Same dirt and tarmac ramps as the airport so the infield meets the
    surrounding lot without a new colour. These tiles are not part of the
    delivered building set.
    """
    air = airport_style()
    return Style(
        name="terrain",
        materials={
            SRC_TAN: air.materials[SRC_TAN],
            SRC_DTAN: air.materials[SRC_DTAN],
            SRC_DGREY: air.materials[SRC_DGREY],
            WHITE: air.materials[WHITE],
        },
        casters=frozenset(),
        receivers=frozenset({SRC_TAN, SRC_DTAN, SRC_DGREY}),
    )


def load_atlas() -> Image.Image:
    if not ATLAS_PATH.is_file():
        raise SystemExit(f"missing tile atlas: {ATLAS_PATH}")
    im = Image.open(ATLAS_PATH).convert("RGB")
    if im.size[0] % TILE or im.size[1] % TILE:
        raise SystemExit(f"atlas {im.size} is not a multiple of {TILE}")
    return im


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


# Ground texture returns 0 or 2 for a sparse cluster, 1 for the calm base.

# Bevel where the dirt shoulder meets the pavement. Internal black lines stay smooth.
_ASPHALT_EDGE = frozenset({SRC_TAN, SRC_DTAN})

# Dither dots and 1px marks are not buildings, so they do not cast a shadow.
_MIN_CAST = 8
_PAVEMENT = frozenset({SRC_GREY})

_ORTHO = ((0, -1), (0, 1), (-1, 0), (1, 0))
_NW = ((0, -1), (-1, 0))
_SE = ((0, 1), (1, 0))


def _assert_hash_aperiodic() -> None:
    """The speckle hash must not repeat on a 16px tile grid."""
    same = 0
    total = 0
    for y in range(48):
        for x in range(48):
            base = _hash(x, y) & 15
            same += base == (_hash(x + 16, y) & 15)
            same += base == (_hash(x, y + 16) & 15)
            total += 2
    rate = same / total
    if rate > 0.2:
        raise SystemExit(f"speckle hash still tiles at 16px (match rate {rate:.3f})")
    # The ground clusters must not line up with the tile grid either.
    # A period-16 pattern matches itself at +16; a calm field matches by chance.
    clustered = 0
    cluster_total = 0
    for y in range(64):
        for x in range(64):
            here = _ground_index(x, y, 0)
            clustered += here == _ground_index(x + 16, y, 0)
            clustered += here == _ground_index(x, y + 16, 0)
            cluster_total += 2
    cluster_rate = clustered / cluster_total
    if cluster_rate > 0.95:
        raise SystemExit(f"ground clusters still tile at 16px (match rate {cluster_rate:.3f})")


def _ink_mask(original: Image.Image, touch: tuple[int, int, int] | None) -> list[list[bool]]:
    """Dark-grey pixels connected to a pixel that touches `touch`."""
    w, h = original.size
    src = original.load()
    mask = [[False] * w for _ in range(h)]
    if touch is None:
        return mask
    seeds: list[tuple[int, int]] = []
    for y in range(h):
        for x in range(w):
            if src[x, y] != SRC_DGREY:
                continue
            for nx, ny in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
                if 0 <= nx < w and 0 <= ny < h and src[nx, ny] == touch:
                    seeds.append((x, y))
                    break
    seen = [[False] * w for _ in range(h)]
    stack = seeds
    for x, y in seeds:
        seen[y][x] = True
        mask[y][x] = True
    while stack:
        x, y = stack.pop()
        for nx, ny in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
            if not (0 <= nx < w and 0 <= ny < h) or seen[ny][nx]:
                continue
            if src[nx, ny] != SRC_DGREY:
                continue
            seen[ny][nx] = True
            mask[ny][nx] = True
            stack.append((nx, ny))
    return mask


def _in(x: int, y: int, w: int, h: int) -> bool:
    return 0 <= x < w and 0 <= y < h


def _caster_counts(src, w: int, h: int, casters: frozenset[tuple[int, int, int]]) -> dict[tuple[int, int], int]:
    """Pixel count of each caster region big enough to throw a shadow."""
    counts: dict[tuple[int, int], int] = {}
    seen = [[False] * w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            if seen[y][x] or src[x, y] not in casters:
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
                    if _in(nx, ny, w, h) and not seen[ny][nx] and src[nx, ny] == colour:
                        seen[ny][nx] = True
                        stack.append((nx, ny))
            if len(pixels) < _MIN_CAST:
                continue
            for px, py in pixels:
                counts[(px, py)] = len(pixels)
    return counts


def _casts(
    src,
    x: int,
    y: int,
    w: int,
    h: int,
    receiver: tuple[int, int, int],
    counts: dict[tuple[int, int], int],
) -> bool:
    """A real building pixel, or a black outline with one beside it."""
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


def _shadow_strength(
    src,
    x: int,
    y: int,
    w: int,
    h: int,
    style: Style,
    counts: dict[tuple[int, int], int],
) -> int:
    """2 on the pixel touching a building's south or east side, 1 one pixel further."""
    colour = src[x, y]
    if colour not in style.receivers:
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


def _grass_rim(src, x: int, y: int, w: int, h: int, counts: dict[tuple[int, int], int]) -> bool:
    """Darker grass where it meets a building or the pavement, including through an outline."""
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


def _touches(src, x: int, y: int, w: int, h: int, dirs: tuple[tuple[int, int], ...], colours: frozenset) -> bool:
    for dx, dy in dirs:
        nx, ny = x + dx, y + dy
        if _in(nx, ny, w, h) and src[nx, ny] in colours:
            return True
    return False


def _same_count(src, x: int, y: int, w: int, h: int) -> int:
    colour = src[x, y]
    count = 0
    for dx, dy in _ORTHO:
        nx, ny = x + dx, y + dy
        if _in(nx, ny, w, h) and src[nx, ny] == colour:
            count += 1
    return count


def _component_boxes(src, w: int, h: int, colours: set[tuple[int, int, int]]) -> dict[tuple[int, int], tuple[int, int, int, int]]:
    """Bounding box of each 4-connected region, so every roof gets its own gradient."""
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


def _ground_index(x: int, y: int, salt: int) -> int:
    """Calm base (1), with a few 2–4px clusters of the neighbour shades.

    The cluster grid is 2px and the choice comes from the full-coordinate
    hash, so the pattern does not repeat every tile.
    """
    cell = _hash((x // 2) + salt * 17, (y // 2) + salt * 29)
    kind = cell & 15
    if kind > 2:
        return 1
    shade = 0 if (cell & 16) == 0 else 2
    ox, oy = x & 1, y & 1
    if kind == 0:
        return shade
    if kind == 1:
        # Three pixels of the 2×2.
        if ((ox << 1) | oy) == ((cell >> 5) & 3):
            return 1
        return shade
    # A 2px pair, either the top row or the left column.
    if (cell & 32) == 0:
        return shade if oy == 0 else 1
    return shade if ox == 0 else 1


def _paint_surface(src, x: int, y: int, w: int, h: int, ramp: tuple, box: tuple[int, int, int, int]) -> tuple[int, int, int]:
    n = len(ramp)
    if n == 1:
        return ramp[0]
    x0, y0, x1, y1 = box
    tx = (x - x0) / max(1, x1 - x0)
    ty = (y - y0) / max(1, y1 - y0)
    t = (tx + ty) / 2
    # On a longer ramp the lightest entry is only the highlight and the
    # darkest is only the south/east edge. The face uses the steps between.
    if n >= 4:
        idx = 1 + int(t * (n - 3) + 0.5)
        idx = max(1, min(n - 2, idx))
    else:
        idx = int(t * (n - 2) + 0.5)
        idx = max(0, min(n - 2, idx))
    # A one-pixel line has different neighbours on both sides. Leave the
    # gradient there so a dither pattern does not strobe.
    if _same_count(src, x, y, w, h) >= 2:
        colour = src[x, y]
        nw = any(_in(x + dx, y + dy, w, h) and src[x + dx, y + dy] != colour for dx, dy in _NW)
        se = any(_in(x + dx, y + dy, w, h) and src[x + dx, y + dy] != colour for dx, dy in _SE)
        if nw and not se:
            idx -= 1
        elif se and not nw:
            idx += 1
    return ramp[max(0, min(n - 1, idx))]


def recolour(original: Image.Image, style: Style, cells: set[tuple[int, int]] | None = None) -> Image.Image:
    """Repaint `original`. With `cells`, only those 16px cells change.

    Coordinates are the image's own pixels, so a speckle pattern continues
    across a tile edge instead of restarting every 16px.
    """
    w, h = original.size
    src = original.load()
    out = Image.new("RGB", (w, h))
    dst = out.load()
    ink = _ink_mask(original, style.ink_touch)
    surface_colours = {colour for colour, mat in style.materials.items() if mat.role == "surface"}
    boxes = _component_boxes(src, w, h, surface_colours)
    counts = _caster_counts(src, w, h, style.casters)
    for y in range(h):
        for x in range(w):
            colour = src[x, y]
            if cells is not None and (x // TILE, y // TILE) not in cells:
                dst[x, y] = colour
                continue
            if colour == BLACK:
                dst[x, y] = BLACK
                continue
            if ink[y][x]:
                if style.ink is None:
                    raise SystemExit("ink mask set without an ink colour")
                dst[x, y] = style.ink
                continue
            mat = style.materials.get(colour)
            if mat is None:
                raise SystemExit(f"{style.name}: no ramp for source colour {colour} at {(x, y)}")
            ramp = mat.ramp
            strength = _shadow_strength(src, x, y, w, h, style, counts)
            shadowed = _apply_shadow(ramp, strength)
            if mat.role == "flat":
                dst[x, y] = ramp[0]
                continue
            if mat.role == "hedge":
                # Kept on the pass-2 speckle so the shrubs do not move.
                body = ramp[:-2]
                idx = 0 if (_hash(x, y) & 7) < 2 else min(1, len(body) - 1)
                if _grass_rim(src, x, y, w, h, counts):
                    idx = len(body) - 1
                chosen = body[idx]
                if shadowed is not None:
                    chosen = shadowed
                dst[x, y] = chosen
                continue
            if mat.role in ("grass", "dirt"):
                # Grass keeps two shadow steps. Dirt keeps one, so the three
                # salmons stay in the texture and only the rose is the shadow.
                body = ramp[:-2] if mat.role == "grass" else ramp[:-1]
                idx = _ground_index(x, y, 0 if mat.role == "dirt" else 1)
                if mat.role == "grass" and _grass_rim(src, x, y, w, h, counts):
                    idx = len(body) - 1
                chosen = body[min(idx, len(body) - 1)]
                if shadowed is not None:
                    chosen = shadowed
                dst[x, y] = chosen
                continue
            if mat.role == "asphalt":
                lit = _touches(src, x, y, w, h, _NW, _ASPHALT_EDGE)
                dark = _touches(src, x, y, w, h, _SE, _ASPHALT_EDGE)
                if lit and dark:
                    chosen = ramp[1]
                elif lit:
                    chosen = ramp[0]
                elif dark:
                    chosen = ramp[2]
                else:
                    chosen = ramp[1]
                if shadowed is not None:
                    chosen = shadowed
                dst[x, y] = chosen
                continue
            if mat.role == "surface":
                chosen = _paint_surface(src, x, y, w, h, ramp, boxes.get((x, y), (x, y, x, y)))
                if shadowed is not None:
                    chosen = shadowed
                dst[x, y] = chosen
                continue
            raise SystemExit(f"{style.name}: unknown role {mat.role}")
    return out


def slice_tiles(image: Image.Image, width: int, height: int) -> list[Image.Image]:
    tiles = []
    for y in range(height):
        for x in range(width):
            tiles.append(image.crop((x * TILE, y * TILE, x * TILE + TILE, y * TILE + TILE)))
    return tiles


def save_tile(image: Image.Image, path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    image.save(path, optimize=True)


def build_indexed_tiles(atlas: Image.Image) -> dict[int, Image.Image]:
    """SVGA tiles keyed by atlas index. Terrain tiles are for the scenes."""
    police = recolour(assemble(atlas, POLICE_BASE, *POLICE_SIZE), police_style())
    airport_classic = assemble(atlas, AIRPORT_BASE, *AIRPORT_SIZE)
    airport = recolour(airport_classic, airport_style())
    tiles: dict[int, Image.Image] = {}
    pw, ph = POLICE_SIZE
    for i, tile in enumerate(slice_tiles(police, pw, ph)):
        tiles[POLICE_BASE + i] = tile
    aw, ah = AIRPORT_SIZE
    for i, tile in enumerate(slice_tiles(airport, aw, ah)):
        tiles[AIRPORT_BASE + i] = tile
    # Radar frames are recoloured in place on the full airport. Pixels the
    # frame shares with the static cell keep the static colour, so the dish
    # meets the neighbouring tiles.
    ax, ay = RADAR_CELL
    x0, y0 = ax * TILE, ay * TILE
    classic_px = airport_classic.load()
    static_px = airport.load()
    for frame in RADAR_FRAMES:
        patched = airport_classic.copy()
        patched.paste(crop_tile(atlas, frame), (x0, y0))
        painted = recolour(patched, airport_style())
        patched_px = patched.load()
        painted_px = painted.load()
        tile = Image.new("RGB", (TILE, TILE))
        dst = tile.load()
        for y in range(TILE):
            for x in range(TILE):
                if patched_px[x0 + x, y0 + y] == classic_px[x0 + x, y0 + y]:
                    dst[x, y] = static_px[x0 + x, y0 + y]
                else:
                    dst[x, y] = painted_px[x0 + x, y0 + y]
        tiles[frame] = tile
    return tiles


def classic_tile(atlas: Image.Image, index: int) -> Image.Image:
    return crop_tile(atlas, index)


def _scene_style(grid: list[list[int]], building_cells: set[tuple[int, int]]) -> Style:
    for y, row in enumerate(grid):
        for x, index in enumerate(row):
            if (x, y) not in building_cells:
                continue
            if POLICE_BASE <= index < POLICE_BASE + POLICE_SIZE[0] * POLICE_SIZE[1]:
                return police_style()
            return airport_style()
    return airport_style()


def paint_grid(
    atlas: Image.Image,
    svga: dict[int, Image.Image],
    grid: list[list[int]],
    use_svga: bool,
    building_cells: set[tuple[int, int]],
) -> Image.Image:
    height = len(grid)
    width = len(grid[0])
    classic = Image.new("RGB", (width * TILE, height * TILE))
    for y, row in enumerate(grid):
        for x, index in enumerate(row):
            classic.paste(classic_tile(atlas, index), (x * TILE, y * TILE))
    if not use_svga:
        return classic
    # Ground is recoloured in scene coordinates so neighbouring dirt tiles
    # share one speckle field. Building cells are pasted from the assembly,
    # which was shaded the same way.
    ground = {(x, y) for y in range(height) for x in range(width) if (x, y) not in building_cells}
    painted = recolour(classic, terrain_style(), cells=ground)
    for y, row in enumerate(grid):
        for x, index in enumerate(row):
            if (x, y) in building_cells:
                painted.paste(svga[index], (x * TILE, y * TILE))
    _external_shadow(painted, classic, building_cells, _scene_style(grid, building_cells))
    return painted


def _external_shadow(
    painted: Image.Image,
    classic: Image.Image,
    building_cells: set[tuple[int, int]],
    style: Style,
) -> None:
    """1–2px south-east shadow on the lot, in a darker shade of that ground."""
    terrain = terrain_style()
    src = classic.load()
    dst = painted.load()
    w, h = classic.size
    counts = _caster_counts(src, w, h, style.casters)
    for y in range(h):
        for x in range(w):
            if (x // TILE, y // TILE) in building_cells:
                continue
            colour = src[x, y]
            mat = terrain.materials.get(colour)
            if mat is None or colour in (BLACK, WHITE):
                continue
            strength = 0
            for dx, dy in _NW:
                nx, ny = x + dx, y + dy
                if (
                    _casts(src, nx, ny, w, h, colour, counts)
                    and _in(nx, ny, w, h)
                    and (nx // TILE, ny // TILE) in building_cells
                ):
                    strength = max(strength, 2)
                    continue
                if not _in(nx, ny, w, h):
                    continue
                mid = src[nx, ny]
                if mid != colour and mid != BLACK:
                    continue
                fx, fy = x + 2 * dx, y + 2 * dy
                if (
                    _casts(src, fx, fy, w, h, colour, counts)
                    and _in(fx, fy, w, h)
                    and (fx // TILE, fy // TILE) in building_cells
                ):
                    strength = max(strength, 1)
            shadowed = _apply_shadow(mat.ramp, strength)
            if shadowed is not None:
                dst[x, y] = shadowed


def police_grid(origin_x: int, origin_y: int, width: int, height: int) -> tuple[list[list[int]], set[tuple[int, int]]]:
    grid = [[DIRT for _ in range(width)] for _ in range(height)]
    for x in range(width):
        grid[height - 1][x] = ROAD_H
    cells = set()
    pw, ph = POLICE_SIZE
    for y in range(ph):
        for x in range(pw):
            gx, gy = origin_x + x, origin_y + y
            grid[gy][gx] = POLICE_BASE + y * pw + x
            cells.add((gx, gy))
    return grid, cells


def airport_grid(
    origin_x: int,
    origin_y: int,
    width: int,
    height: int,
    radar_frame: int | None,
) -> tuple[list[list[int]], set[tuple[int, int]]]:
    grid = [[DIRT for _ in range(width)] for _ in range(height)]
    for x in range(width):
        grid[height - 1][x] = ROAD_H
    cells = set()
    aw, ah = AIRPORT_SIZE
    for y in range(ah):
        for x in range(aw):
            gx, gy = origin_x + x, origin_y + y
            index = AIRPORT_BASE + y * aw + x
            if radar_frame is not None and (x, y) == RADAR_CELL:
                index = radar_frame
            grid[gy][gx] = index
            cells.add((gx, gy))
    return grid, cells


def text_size(text: str) -> tuple[int, int]:
    width = 0
    for ch in text:
        glyph = FONT[ch]
        width += len(glyph[0]) + 1
    return width - 1, 7


def draw_text(image: Image.Image, xy: tuple[int, int], text: str, fill: tuple[int, int, int]) -> None:
    draw = ImageDraw.Draw(image)
    x, y = xy
    for ch in text:
        glyph = FONT[ch]
        for gy, row in enumerate(glyph):
            for gx, bit in enumerate(row):
                if bit == "#":
                    draw.point((x + gx, y + gy), fill=fill)
        x += len(glyph[0]) + 1


def side_by_side(
    classic: Image.Image,
    svga: Image.Image,
    scale: int,
    left_label: str,
    right_label: str,
) -> Image.Image:
    left = classic.resize((classic.size[0] * scale, classic.size[1] * scale), NEAREST)
    right = svga.resize((svga.size[0] * scale, svga.size[1] * scale), NEAREST)
    gap = max(4, scale * 2)
    header = 16
    width = left.size[0] + gap + right.size[0]
    height = header + left.size[1]
    canvas = Image.new("RGB", (width, height), (32, 32, 32))
    canvas.paste(left, (0, header))
    canvas.paste(right, (left.size[0] + gap, header))
    draw_text(canvas, (4, 4), left_label, WHITE)
    draw_text(canvas, (left.size[0] + gap + 4, 4), right_label, WHITE)
    sample = "SAMPLE"
    sample_w, _ = text_size(sample)
    # EGA yellow, so the sheet is marked as an approval sample.
    draw_text(canvas, (width - sample_w - 4, 4), sample, (255, 255, 85))
    return canvas


def radar_sheet(atlas: Image.Image, svga: dict[int, Image.Image], scale: int) -> Image.Image:
    gap = max(2, scale)
    frame_px = TILE * scale
    row_w = len(RADAR_FRAMES) * frame_px + (len(RADAR_FRAMES) - 1) * gap
    header = 16
    mid = max(6, scale * 2)
    height = header + frame_px + mid + frame_px
    # Left classic, right SVGA, as one row each stacked would not be side by side.
    # Place classic frames on the left half and SVGA frames on the right half.
    width = row_w * 2 + mid
    canvas = Image.new("RGB", (width, height), (32, 32, 32))
    draw_text(canvas, (4, 4), "CLASSIC", WHITE)
    draw_text(canvas, (row_w + mid + 4, 4), "SVGA", WHITE)
    sample = "SAMPLE"
    sample_w, _ = text_size(sample)
    draw_text(canvas, (width - sample_w - 4, 4), sample, (255, 255, 85))
    for i, frame in enumerate(RADAR_FRAMES):
        x = i * (frame_px + gap)
        classic = classic_tile(atlas, frame).resize((frame_px, frame_px), NEAREST)
        painted = svga[frame].resize((frame_px, frame_px), NEAREST)
        canvas.paste(classic, (x, header))
        canvas.paste(painted, (row_w + mid + x, header))
    return canvas


def palette_swatch(pal: list[tuple[int, int, int]]) -> Image.Image:
    cell = 8
    image = Image.new("RGB", (16 * cell, 16 * cell))
    dst = image.load()
    for index, rgb in enumerate(pal):
        col, row = index % 16, index // 16
        for y in range(cell):
            for x in range(cell):
                dst[col * cell + x, row * cell + y] = rgb
    return image


def ramp_report(palette: list[tuple[int, int, int]]) -> str:
    index = {rgb: i for i, rgb in enumerate(palette)}
    lines = ["VGA ramp assignments (palette index, 8-bit RGB), light to dark", ""]
    for style in (police_style(), airport_style()):
        lines.append(f"[{style.name}]")
        for src, mat in style.materials.items():
            described = ", ".join(f"{index[c]}:{c}" for c in mat.ramp)
            lines.append(f"  source {src} {mat.role} -> {described}")
        if style.ink is not None:
            lines.append(f"  letter ink -> {index[style.ink]}:{style.ink}")
        lines.append("")
    return "\n".join(lines)


def combined_sheet(
    panels: list[tuple[str, Image.Image, Image.Image]],
    scale: int,
) -> Image.Image:
    """Police and airport, each Classic | SVGA, on one sheet."""
    gap = max(4, scale * 2)
    group_gap = gap * 4
    header = 28
    scaled: list[tuple[str, Image.Image, Image.Image]] = []
    for title, classic, svga in panels:
        left = classic.resize((classic.size[0] * scale, classic.size[1] * scale), NEAREST)
        right = svga.resize((svga.size[0] * scale, svga.size[1] * scale), NEAREST)
        scaled.append((title, left, right))
    width = sum(left.size[0] + gap + right.size[0] for _, left, right in scaled)
    width += group_gap * (len(scaled) - 1)
    height = header + max(left.size[1] for _, left, _ in scaled)
    canvas = Image.new("RGB", (width, height), (32, 32, 32))
    x = 0
    for title, left, right in scaled:
        canvas.paste(left, (x, header))
        canvas.paste(right, (x + left.size[0] + gap, header))
        draw_text(canvas, (x + 4, 4), title, WHITE)
        draw_text(canvas, (x + 4, 16), "CLASSIC", WHITE)
        draw_text(canvas, (x + left.size[0] + gap + 4, 16), "SVGA", WHITE)
        x += left.size[0] + gap + right.size[0] + group_gap
    sample = "SAMPLE"
    sample_w, _ = text_size(sample)
    draw_text(canvas, (width - sample_w - 4, 4), sample, (255, 255, 85))
    return canvas


def main() -> None:
    _assert_hash_aperiodic()
    palette = vga_palette_8bit()
    palette_set = set(palette)
    for style in (police_style(), airport_style(), terrain_style()):
        colours: set[tuple[int, int, int]] = set()
        for mat in style.materials.values():
            colours.update(mat.ramp)
        if style.ink is not None:
            colours.add(style.ink)
        _require_in_palette(colours, palette_set)
        _assert_disjoint(style)

    pal_dir = HERE / "palette"
    pal_dir.mkdir(parents=True, exist_ok=True)
    write_gpl(pal_dir / "vga-mode13h.gpl", palette, vga_dac_6bit())
    palette_swatch(palette).save(pal_dir / "vga-mode13h-swatch.png", optimize=True)
    (HERE / "ramps-used.txt").write_text(ramp_report(palette), encoding="utf-8")

    atlas = load_atlas()
    svga = build_indexed_tiles(atlas)

    manifest_tiles = []
    groups = {
        "police": list(range(POLICE_BASE, POLICE_BASE + POLICE_SIZE[0] * POLICE_SIZE[1])),
        "airport": list(range(AIRPORT_BASE, AIRPORT_BASE + AIRPORT_SIZE[0] * AIRPORT_SIZE[1])),
        "radar": RADAR_FRAMES,
    }
    for group, indices in groups.items():
        for index in indices:
            rel = f"tiles/{group}/tile-{index:04d}.png"
            save_tile(svga[index], HERE / rel)
            manifest_tiles.append({"id": index, "file": rel, "group": group})
    manifest = {
        "note": "Sample SVGA recolour. Not wired into the game.",
        "atlas": "third_party/micropolis-assets/images/micropolisEngine/tiles.png",
        "palette": "palette/vga-mode13h.gpl",
        "assemblies": {
            "police": {"base": POLICE_BASE, "width": 3, "height": 3, "group": "police"},
            "airport": {"base": AIRPORT_BASE, "width": 6, "height": 6, "group": "airport"},
        },
        "tiles": manifest_tiles,
    }
    (HERE / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

    # Tight comparisons: one dirt margin, road along the south edge.
    police_cmp, police_cells = police_grid(1, 1, 5, 5)
    # Powered dish (frame 834, arm toward the upper left) so the animation art is in the sheet.
    airport_cmp, airport_cells = airport_grid(1, 1, 8, 8, radar_frame=834)
    police_classic = paint_grid(atlas, svga, police_cmp, False, police_cells)
    police_svga = paint_grid(atlas, svga, police_cmp, True, police_cells)
    airport_classic = paint_grid(atlas, svga, airport_cmp, False, airport_cells)
    airport_svga = paint_grid(atlas, svga, airport_cmp, True, airport_cells)

    cmp_dir = HERE / "comparisons"
    cmp_dir.mkdir(parents=True, exist_ok=True)
    for scale, name in ((1, "1x"), (3, "3x"), (6, "6x")):
        side_by_side(police_classic, police_svga, scale, "CLASSIC", "SVGA").save(
            cmp_dir / f"police-{name}.png", optimize=True
        )
        side_by_side(airport_classic, airport_svga, scale, "CLASSIC", "SVGA").save(
            cmp_dir / f"airport-{name}.png", optimize=True
        )
    for scale, name in ((3, "3x"), (6, "6x")):
        radar_sheet(atlas, svga, scale).save(cmp_dir / f"radar-{name}.png", optimize=True)
    combined_sheet(
        (
            ("POLICE", police_classic, police_svga),
            ("AIRPORT", airport_classic, airport_svga),
        ),
        3,
    ).save(cmp_dir / "police-airport-3x.png", optimize=True)

    # Wider lot, still a road along the south. Mock-up radar is frame 833 (face-on dish).
    mock_dir = HERE / "mockups"
    mock_dir.mkdir(parents=True, exist_ok=True)
    pgrid, pcells = police_grid(3, 2, 9, 7)
    agrid, acells = airport_grid(2, 1, 10, 9, radar_frame=833)
    scenes = {
        "police": (pgrid, pcells),
        "airport": (agrid, acells),
    }
    for building, (grid, cells) in scenes.items():
        classic = paint_grid(atlas, svga, grid, False, cells)
        painted = paint_grid(atlas, svga, grid, True, cells)
        side_by_side(classic, painted, 1, "CLASSIC", "SVGA").save(
            mock_dir / f"{building}-classic-vs-svga-16px.png", optimize=True
        )
        side_by_side(classic, painted, 2, "CLASSIC", "SVGA").save(
            mock_dir / f"{building}-classic-vs-svga-32px.png", optimize=True
        )

    print(f"wrote {len(manifest_tiles)} tiles under {HERE}")


if __name__ == "__main__":
    main()
