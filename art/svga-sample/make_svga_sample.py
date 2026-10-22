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

    body_last is the darkest ramp index used for the ordinary surface.
    Indexes past it are only reached by the shadow rim and the contact shadow.
    """

    ramp: tuple[tuple[int, int, int], ...]
    texture: str = "none"  # none | grass | ground
    fleck: tuple[int, tuple[int, int, int]] | None = None
    flat: tuple[int, int, int] | None = None
    body_last: int = 0


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
        if mat.flat is not None:
            claim(mat.flat, who)
        for rgb in mat.ramp:
            claim(rgb, who)
        if mat.fleck is not None:
            claim(mat.fleck[1], who)
    if style.ink is not None:
        claim(style.ink, f"{style.name}:ink")


def _grain(x: int, y: int) -> int:
    """0..15 texture value. Scrambled so it does not form a visible grid."""
    return ((x * 73) ^ (y * 151) ^ (x * 13 + y * 7)) & 15


def police_style() -> Style:
    # Bright lawn in the EGA / high-value greens. The dark end is shadow only,
    # so the lawn does not sink into olive across the tile.
    grass = Material(
        ramp=(
            (85, 255, 85),
            (65, 255, 0),
            (0, 255, 0),
            (0, 170, 0),
        ),
        texture="grass",
        body_last=2,
    )
    hedge = Material(
        ramp=(
            (44, 65, 44),
            (32, 65, 32),
            (16, 65, 0),
            (0, 65, 0),
        ),
        texture="grass",
        body_last=1,
    )
    # Pale blue roof and the light-blue trim. Equal red/green so it stays blue,
    # not the pink cast of the low-saturation magenta side of the wheel.
    roof = Material(
        ramp=(
            (182, 182, 255),
            (125, 125, 255),
        ),
        body_last=1,
    )
    # Saturated police blue. Kept well below the pale roof so the eave still reads.
    wall = Material(
        ramp=(
            (0, 0, 170),
            (0, 0, 113),
        ),
        body_last=0,
    )
    # Flat roof, forecourt and the driveway. Same source grey, one concrete ramp.
    concrete = Material(
        ramp=(
            (182, 182, 182),
            (162, 162, 162),
            (146, 146, 146),
            (130, 130, 130),
            (113, 113, 113),
        ),
        body_last=2,
    )
    # Left wing and other dark structure. Sign letters are pulled out as ink.
    slate = Material(
        ramp=(
            (97, 97, 97),
            (81, 81, 81),
            (69, 69, 69),
            (56, 56, 56),
        ),
        body_last=1,
    )
    sign = Material(
        ramp=(
            (255, 255, 125),
            (255, 255, 0),
        ),
        body_last=1,
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
            SRC_LGREY: Material(ramp=((227, 227, 227),), flat=(227, 227, 227)),
            WHITE: Material(ramp=(WHITE,), flat=WHITE),
            SRC_RED: Material(ramp=((255, 0, 0),), flat=(255, 0, 0)),
        },
        casters=frozenset({SRC_LBLUE, SRC_BLUE, SRC_DGREY, SRC_YELLOW, SRC_LGREY, WHITE, SRC_RED, SRC_DGREEN}),
        receivers=frozenset({SRC_GREEN, SRC_DGREEN, SRC_GREY}),
        ink_touch=SRC_YELLOW,
        ink=(32, 32, 32),
    )


def airport_style() -> Style:
    # Clear-land tan, lifted into the light earth ramp so it stays lighter than
    # the asphalt. The VGA wheel has no brown between luma ~110 and ~180.
    dirt = Material(
        ramp=(
            (255, 219, 182),
            (255, 190, 125),
            (255, 158, 125),
        ),
        texture="ground",
        # Sparse darker specks, about one per 64 dirt pixels.
        fleck=(7, (113, 69, 56)),
        body_last=1,
    )
    soil = Material(
        ramp=((113, 85, 56),),
        flat=(113, 85, 56),
    )
    tarmac = Material(
        ramp=(
            (97, 97, 97),
            (81, 81, 81),
            (69, 69, 69),
            (56, 56, 56),
            (44, 44, 44),
        ),
        texture="ground",
        body_last=2,
    )
    concrete = Material(
        ramp=(
            (203, 203, 203),
            (182, 182, 182),
            (162, 162, 162),
            (146, 146, 146),
            (130, 130, 130),
        ),
        body_last=2,
    )
    # Cool highlight on hangar roofs. Stays off the neutral concrete ramp.
    roof = Material(
        ramp=(
            (182, 199, 255),
            (199, 182, 255),
            (182, 182, 255),
            (125, 158, 255),
        ),
        body_last=2,
    )
    # Terminal glass. Kept off the pale roof ramp above.
    glass = Material(
        ramp=(
            (158, 125, 255),
            (125, 125, 255),
            (81, 81, 113),
            (56, 56, 113),
        ),
        body_last=1,
    )
    deep = Material(
        ramp=(
            (69, 56, 113),
            (32, 32, 65),
            (0, 0, 113),
        ),
        body_last=1,
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
            SRC_YELLOW: Material(ramp=((255, 255, 0),), flat=(255, 255, 0)),
            WHITE: Material(ramp=(WHITE,), flat=WHITE),
            SRC_RED: Material(ramp=((255, 0, 0),), flat=(255, 0, 0)),
            SRC_ORANGE: Material(ramp=((255, 125, 0),), flat=(255, 125, 0)),
            SRC_DGREEN: Material(ramp=((0, 65, 0),), flat=(0, 65, 0)),
        },
        casters=frozenset({SRC_GREY, SRC_LGREY, SRC_LBLUE, SRC_BLUE, WHITE, SRC_RED, SRC_ORANGE}),
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


def _is_frame(x: int, y: int, w: int, h: int) -> bool:
    return x < 2 or y < 2 or x >= w - 2 or y >= h - 2


def _contact(src, x: int, y: int, w: int, h: int, style: Style) -> bool:
    colour = src[x, y]
    if colour not in style.receivers:
        return False
    for dx, dy in ((0, -1), (-1, 0)):
        nx, ny = x + dx, y + dy
        if not (0 <= nx < w and 0 <= ny < h) or _is_frame(nx, ny, w, h):
            continue
        neighbour = src[nx, ny]
        if neighbour in style.casters and neighbour != colour:
            return True
        if neighbour == BLACK:
            fx, fy = nx + dx, ny + dy
            if not (0 <= fx < w and 0 <= fy < h) or _is_frame(fx, fy, w, h):
                continue
            beyond = src[fx, fy]
            if beyond in style.casters and beyond != colour:
                return True
    return False


def _differs(src, x: int, y: int, w: int, h: int, dx: int, dy: int) -> bool:
    nx, ny = x + dx, y + dy
    if not (0 <= nx < w and 0 <= ny < h):
        return False
    return src[nx, ny] != src[x, y]


def recolour(original: Image.Image, style: Style) -> Image.Image:
    w, h = original.size
    src = original.load()
    out = Image.new("RGB", (w, h))
    dst = out.load()
    ink = _ink_mask(original, style.ink_touch)
    for y in range(h):
        for x in range(w):
            colour = src[x, y]
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
            if mat.flat is not None:
                dst[x, y] = mat.flat
                continue
            ramp = mat.ramp
            n = len(ramp)
            ty = y / max(1, h - 1)
            tx = x / max(1, w - 1)
            # Upper-left is lighter. Body stops at body_last; darker steps are
            # the shadow rim and the 1px contact shadow.
            last = mat.body_last
            pos = ty * 0.65 + tx * 0.35
            idx = int(round(pos * last))
            grain = _grain(x, y)
            if mat.texture == "grass":
                if grain >= 13:
                    idx -= 1
                elif grain <= 1:
                    idx += 1
            elif mat.texture == "ground":
                if grain >= 14:
                    idx -= 1
                elif grain == 0:
                    idx += 1
            idx = max(0, min(last, idx))
            light = _differs(src, x, y, w, h, 0, -1) or _differs(src, x, y, w, h, -1, 0)
            shadow = _differs(src, x, y, w, h, 0, 1) or _differs(src, x, y, w, h, 1, 0)
            contact = _contact(src, x, y, w, h, style)
            if contact:
                light = False
                idx += 1
            if light:
                idx -= 1
            if shadow:
                idx += 1
            idx = max(0, min(n - 1, idx))
            chosen = ramp[idx]
            if (
                mat.fleck is not None
                and grain == mat.fleck[0]
                and ((x ^ y) & 3) == 0
                and not contact
                and not shadow
            ):
                chosen = mat.fleck[1]
            dst[x, y] = chosen
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
    # Radar frames are recoloured in place on the full airport so the dish
    # pixels see the same neighbours as the static cell.
    ax, ay = RADAR_CELL
    for frame in RADAR_FRAMES:
        patched = airport_classic.copy()
        patched.paste(crop_tile(atlas, frame), (ax * TILE, ay * TILE))
        painted = recolour(patched, airport_style())
        tiles[frame] = painted.crop((ax * TILE, ay * TILE, ax * TILE + TILE, ay * TILE + TILE))
    for index in (DIRT, ROAD_H):
        tiles[index] = recolour(crop_tile(atlas, index), terrain_style())
    return tiles


def classic_tile(atlas: Image.Image, index: int) -> Image.Image:
    return crop_tile(atlas, index)


def paint_grid(
    atlas: Image.Image,
    svga: dict[int, Image.Image],
    grid: list[list[int]],
    use_svga: bool,
    building_cells: set[tuple[int, int]],
) -> Image.Image:
    height = len(grid)
    width = len(grid[0])
    image = Image.new("RGB", (width * TILE, height * TILE))
    for y, row in enumerate(grid):
        for x, index in enumerate(row):
            if use_svga:
                tile = svga[index]
            else:
                tile = classic_tile(atlas, index)
            image.paste(tile, (x * TILE, y * TILE))
    if use_svga:
        _external_shadow(image, grid, building_cells)
    return image


def _step_darker(rgb: tuple[int, int, int], ramps: list[tuple[tuple[int, int, int], ...]]) -> tuple[int, int, int]:
    for ramp in ramps:
        if rgb in ramp:
            idx = ramp.index(rgb)
            return ramp[min(len(ramp) - 1, idx + 1)]
    return rgb


def _external_shadow(image: Image.Image, grid: list[list[int]], building_cells: set[tuple[int, int]]) -> None:
    """1px contact shadow on lot/road pixels that touch the building footprint."""
    terrain = terrain_style()
    ramps = [mat.ramp for mat in terrain.materials.values()]
    for mat in terrain.materials.values():
        if mat.fleck is not None:
            ramps.append((mat.fleck[1], mat.ramp[-1]))
    src = image.load()
    w = len(grid[0])
    h = len(grid)
    darker: dict[tuple[int, int], tuple[int, int, int]] = {}
    for y in range(h):
        for x in range(w):
            if (x, y) in building_cells:
                continue
            for dx, dy in ((0, -1), (-1, 0)):
                nx, ny = x + dx, y + dy
                if (nx, ny) not in building_cells:
                    continue
                # Only the pixels of this cell that actually touch the footprint.
                if dy == -1:
                    span = [(px, y * TILE) for px in range(x * TILE, x * TILE + TILE)]
                else:
                    span = [(x * TILE, py) for py in range(y * TILE, y * TILE + TILE)]
                for px, py in span:
                    darker[(px, py)] = _step_darker(src[px, py], ramps)
                break
    for (px, py), rgb in darker.items():
        src[px, py] = rgb


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
    lines = ["VGA ramp assignments (palette index, 8-bit RGB)", ""]
    for style in (police_style(), airport_style()):
        lines.append(f"[{style.name}]")
        for src, mat in style.materials.items():
            colours = [mat.flat] if mat.flat is not None else list(mat.ramp)
            if mat.fleck is not None:
                colours.append(mat.fleck[1])
            described = ", ".join(f"{index[c]}:{c}" for c in colours if c is not None)
            lines.append(f"  source {src} -> {described}")
        if style.ink is not None:
            lines.append(f"  letter ink -> {index[style.ink]}:{style.ink}")
        lines.append("")
    return "\n".join(lines)


def main() -> None:
    palette = vga_palette_8bit()
    palette_set = set(palette)
    for style in (police_style(), airport_style(), terrain_style()):
        colours: set[tuple[int, int, int]] = set()
        for mat in style.materials.values():
            colours.update(mat.ramp)
            if mat.flat is not None:
                colours.add(mat.flat)
            if mat.fleck is not None:
                colours.add(mat.fleck[1])
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
