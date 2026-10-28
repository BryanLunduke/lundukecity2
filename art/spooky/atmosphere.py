#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Lunduke City authors
# See COPYING and NOTICE.

"""Night ground, violet water, dead woods, and two civic reskins.

The graveyard (40-43), the crypt (840-843), and the paused manor
(297-305) are left byte-for-byte. Grey cemetery dirt is not in the
remap, so it cannot leak into this pass even on a tile that is touched.

Dirt colours are the SVGA scheme-B field. They are the lot and the
open ground, not the rose and amber walls, so a grown city changes
with the terrain instead of only at the park.
"""

from __future__ import annotations

import sys
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "svga"))
from vga_palette import vga_palette_8bit  # noqa: E402

TILE = 16

# Approved art. Not repainted here.
PROTECTED = frozenset(tuple(range(40, 44)) + tuple(range(840, 844)) + tuple(range(297, 306)))

# Open ground and the shore band. Woods are handled on their own.
WOODS = frozenset(range(21, 38))

# Tiles that carry the water ramp, including bridges, rail-over-water,
# power-over-water, and flood. Deep blue (0, 0, 170) is also a wall
# colour, so it is remapped only on this list.
WATER_TILES = frozenset(
    list(range(2, 21))
    + list(range(48, 52))
    + [64, 65, 79, 80, 81, 95, 96, 97, 111, 112, 113, 127, 128, 129]
    + [143, 144, 145, 159, 160, 161, 175, 176, 177, 191, 192, 193, 207]
    + [208, 209, 224, 225]
    + list(range(828, 832))
    + list(range(948, 952))
)

# Night dirt. Darker and less orange than scheme B, still brown, so it
# does not melt into the cemetery greys (69, 69, 69) / (81, 81, 81).
DIRT_MAP = {
    (113, 56, 0): (65, 48, 32),
    (113, 69, 56): (65, 56, 32),
    (113, 28, 0): (65, 32, 0),
    (113, 0, 28): (65, 0, 16),
    (65, 48, 0): (65, 32, 32),
    (65, 16, 0): (65, 0, 16),
}

# The rare dirt speck. Only on open ground, where it cannot be a sign.
ACCENT = (170, 85, 0)
ACCENT_NIGHT = (113, 85, 0)
ACCENT_TILES = frozenset(list(range(0, 40)) + list(range(64, 240)) + list(range(828, 832)) + list(range(948, 952)))

# Dithered lawn. (0, 170, 0) is the zone letter, the lot border, and the
# stadium field, so it is not in this map.
LAWN_MAP = {
    (0, 113, 0): (0, 65, 16),
    (56, 113, 0): (32, 65, 0),
    (28, 113, 0): (16, 65, 0),
    (56, 113, 56): (32, 65, 32),
    (0, 113, 28): (0, 65, 16),
    (0, 113, 56): (0, 65, 32),
}

# Bare branches. Lighter than the night dirt so a tree still has a shape.
# The darkest greens become the gaps between limbs.
DEAD_MAP = {
    (0, 170, 0): (113, 85, 56),
    (0, 113, 0): (113, 85, 56),
    (56, 113, 0): (113, 113, 56),
    (28, 113, 0): (113, 85, 0),
    (56, 113, 56): (65, 56, 32),
    (0, 113, 28): (113, 85, 56),
    (0, 113, 56): (65, 56, 32),
    (0, 65, 0): (32, 32, 32),
    (16, 65, 0): (44, 44, 44),
    (0, 65, 16): (32, 32, 32),
    (0, 65, 32): (44, 44, 44),
    (0, 65, 48): (56, 56, 56),
}

# Asylum stone. The cross stays (170, 0, 0). Glass is barred separately.
# Lit stone steps down into real VGA purples. There is no light purple-grey
# in the palette, so the highlight is the dusty violet (113, 81, 113)
# rather than a neon (182, 182, 255).
ASYLUM_MAP = {
    (227, 227, 227): (182, 182, 182),
    (203, 203, 203): (113, 81, 113),
    (182, 182, 182): (113, 81, 113),
    (162, 162, 162): (97, 81, 113),
    (146, 146, 146): (81, 81, 113),
    (130, 130, 130): (65, 44, 65),
    (113, 113, 113): (65, 44, 65),
    (97, 97, 97): (52, 44, 65),
    (81, 81, 81): (48, 32, 65),
    (69, 69, 69): (32, 32, 65),
    (56, 56, 56): (32, 0, 65),
}
GLASS = {(125, 158, 255), (56, 85, 113), (56, 56, 113), (0, 0, 170), (0, 0, 113), (0, 0, 65)}
BAR = (20, 20, 20)

# Haunted stands. The pitch, the white lines, and the players stay.
ARENA_MAP = {
    (227, 227, 227): (162, 162, 162),
    (203, 203, 203): (113, 113, 113),
    (182, 182, 182): (97, 97, 97),
    (162, 162, 162): (81, 81, 81),
    (146, 146, 146): (69, 69, 69),
    (130, 130, 130): (56, 56, 56),
}
HOSPITAL = frozenset(range(405, 414))
STADIUM = frozenset(list(range(779, 811)) + list(range(932, 948)))

WATER_MAP = {
    (0, 85, 113): (85, 56, 113),
    (0, 56, 113): (56, 0, 113),
    (0, 28, 113): (32, 0, 65),
    (0, 0, 170): (16, 0, 65),
    (0, 48, 65): (44, 44, 65),
    (0, 32, 65): (32, 32, 65),
}

# Pumpkins sit in clearings, not on the limbs. Body, shade, stem.
PUMPKIN = (
    " .o. ",
    " oOo ",
    " oOo ",
    " .s. ",
)
PUMPKIN_INK = {
    ".": None,
    "o": (170, 85, 0),
    "O": (113, 85, 0),
    "s": (0, 65, 0),
}
# Tiles that read as clearings in the SVGA woods sheet.
PUMPKIN_TILES = (30, 31, 33, 35)
GROUND = frozenset(DIRT_MAP.values()) | frozenset(DIRT_MAP)


def _check_colours() -> None:
    palette = set(vga_palette_8bit())
    colours = []
    for table in (DIRT_MAP, LAWN_MAP, DEAD_MAP, ASYLUM_MAP, ARENA_MAP, WATER_MAP, PUMPKIN_INK):
        for value in table.values():
            if value is not None:
                colours.append(value)
    colours.append(ACCENT_NIGHT)
    colours.append(BAR)
    missing = sorted({c for c in colours if c not in palette})
    if missing:
        raise SystemExit(f"atmosphere colour is not in the VGA palette: {missing}")


def _crop(atlas: Image.Image, index: int) -> Image.Image:
    columns = atlas.size[0] // TILE
    col, row = index % columns, index // columns
    return atlas.crop((col * TILE, row * TILE, (col + 1) * TILE, (row + 1) * TILE))


def _paste(atlas: Image.Image, index: int, tile: Image.Image) -> None:
    columns = atlas.size[0] // TILE
    col, row = index % columns, index // columns
    atlas.paste(tile, (col * TILE, row * TILE))


def _remap(tile: Image.Image, table: dict[tuple[int, int, int], tuple[int, int, int]]) -> bool:
    px = tile.load()
    changed = False
    for y in range(tile.size[1]):
        for x in range(tile.size[0]):
            nxt = table.get(px[x, y])
            if nxt is not None and nxt != px[x, y]:
                px[x, y] = nxt
                changed = True
    return changed


def _bars(tile: Image.Image) -> None:
    """A bar on every other glass column. The red cross is not glass."""
    px = tile.load()
    for y in range(tile.size[1]):
        for x in range(tile.size[0]):
            if px[x, y] in GLASS and x % 2 == 0:
                px[x, y] = BAR


def _can_pumpkin(tile: Image.Image, x0: int, y0: int) -> bool:
    px = tile.load()
    ground = 0
    for dy, row in enumerate(PUMPKIN):
        for dx, char in enumerate(row):
            if PUMPKIN_INK.get(char) is None:
                continue
            x, y = x0 + dx, y0 + dy
            if not (0 <= x < TILE and 0 <= y < TILE):
                return False
            if px[x, y] not in GROUND and px[x, y] != ACCENT_NIGHT:
                return False
            ground += 1
    return ground >= 6


def _stamp_pumpkin(tile: Image.Image, x0: int, y0: int) -> None:
    px = tile.load()
    for dy, row in enumerate(PUMPKIN):
        for dx, char in enumerate(row):
            colour = PUMPKIN_INK.get(char)
            if colour is not None:
                px[x0 + dx, y0 + dy] = colour


def _place_pumpkin(tile: Image.Image, slot: int) -> bool:
    # Two patches, offset so neighbouring woods tiles do not repeat.
    seeds = ((2, 10), (9, 4), (8, 11), (3, 3))
    x, y = seeds[slot % len(seeds)]
    for radius in range(0, 8):
        for dy in range(-radius, radius + 1):
            for dx in range(-radius, radius + 1):
                if radius not in (0, abs(dx), abs(dy)):
                    continue
                if _can_pumpkin(tile, x + dx, y + dy):
                    _stamp_pumpkin(tile, x + dx, y + dy)
                    return True
    return False


def _pennant(tile: Image.Image, index: int) -> None:
    """Two orange pixels on the stand, never on the pitch or a yard line."""
    # Empty stadium top row, full stadium top row. Football frames are the
    # field, so they are left alone.
    spots = {
        780: (4, 3),
        782: (11, 3),
        796: (4, 3),
        798: (11, 3),
    }
    origin = spots.get(index)
    if origin is None:
        return
    px = tile.load()
    x0, y0 = origin
    blocked = {(0, 170, 0), (255, 255, 255), (56, 113, 56), (170, 0, 0), (255, 255, 85)}
    for dx, dy in ((0, 0), (1, 0), (0, 1)):
        x, y = x0 + dx, y0 + dy
        if px[x, y] in blocked:
            return
    px[x0, y0] = (255, 125, 0)
    px[x0 + 1, y0] = (170, 85, 0)
    px[x0, y0 + 1] = (113, 56, 0)


def apply(atlas: Image.Image) -> None:
    _check_colours()
    columns = atlas.size[0] // TILE
    count = columns * (atlas.size[1] // TILE)
    for index in range(min(count, 960)):
        if index in PROTECTED:
            continue
        tile = _crop(atlas, index)
        if index in WOODS:
            _remap(tile, DIRT_MAP)
            _remap(tile, DEAD_MAP)
        else:
            _remap(tile, DIRT_MAP)
            _remap(tile, LAWN_MAP)
        if index in ACCENT_TILES and index not in PROTECTED:
            px = tile.load()
            for y in range(TILE):
                for x in range(TILE):
                    if px[x, y] == ACCENT:
                        px[x, y] = ACCENT_NIGHT
        if index in PUMPKIN_TILES:
            _place_pumpkin(tile, PUMPKIN_TILES.index(index))
        if index in WATER_TILES:
            _remap(tile, WATER_MAP)
        if index in HOSPITAL:
            _remap(tile, ASYLUM_MAP)
            # The southeast shadow stays (32, 65, 32). Only the neon lawn drops.
            _remap(tile, {(0, 170, 0): (0, 65, 16)})
            _bars(tile)
        if index in STADIUM:
            _remap(tile, ARENA_MAP)
            _pennant(tile, index)
        _paste(atlas, index, tile)
