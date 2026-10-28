#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Lunduke City authors
# See COPYING and NOTICE.

"""SVGA polish on top of the painted atlas.

Small houses 249-260 get a lighter roof so they separate from dirt B.
Tiles 868-883 are the tiny-explosion cycle (animate.cpp). 860-867 were
already fire. 868-883 were still the classic black-and-white frames, so
the whole cycle is redrawn in the same fire colours. Each frame keeps
its own silhouette.

The residential R on the empty lot is embossed: a light upper-left edge,
a deep-green body, and the existing black shadow. C and I are not touched.
"""

from __future__ import annotations

from PIL import Image

from vga_palette import vga_palette_8bit

TILE = 16

# Shaded roof steps, lifted toward the concrete greys already used on the
# lit half of the same houses. Black ink and the yellow windows stay.
ROOF_LIFT = {
    (56, 56, 56): (113, 113, 113),
    (69, 69, 69): (130, 130, 130),
    (81, 81, 81): (146, 146, 146),
}

# The empty-lot R (tile 244), relative to its top-left green pixel.
# 53 pixels. Strokes are two pixels wide, so the glyph is embossed
# rather than eroded: a one-pixel shrink breaks the legs at 8px.
R_MARK = frozenset(
    {
        (0, 0), (0, 10),
        (1, 0), (1, 1), (1, 2), (1, 3), (1, 4), (1, 5), (1, 6), (1, 7), (1, 8), (1, 9), (1, 10),
        (2, 0), (2, 1), (2, 2), (2, 3), (2, 4), (2, 5), (2, 6), (2, 7), (2, 8), (2, 9), (2, 10),
        (3, 0), (3, 6), (3, 10),
        (4, 0), (4, 6),
        (5, 0), (5, 6), (5, 7),
        (6, 0), (6, 1), (6, 5), (6, 6), (6, 7), (6, 8), (6, 10),
        (7, 1), (7, 2), (7, 3), (7, 4), (7, 5), (7, 8), (7, 9), (7, 10),
        (8, 2), (8, 3), (8, 4), (8, 9), (8, 10),
        (9, 10),
    }
)

R_BODY = (0, 113, 0)
R_LIGHT = (0, 170, 0)
R_SHADE = (0, 65, 0)
R_INK = (0, 0, 0)

FLAME_YELLOW = (255, 255, 85)
FLAME_ORANGE = (255, 125, 0)
FLAME_RED = (255, 65, 0)
SMOKE = (81, 81, 81)
SMOKE_DARK = (56, 56, 56)

_ORTHO = ((0, -1), (0, 1), (-1, 0), (1, 0))


def _palette() -> set[tuple[int, int, int]]:
    return set(vga_palette_8bit())


def _dirt_at(x: int, y: int) -> tuple[int, int, int]:
    # Local import. make_atlas imports this module, so a top-level import
    # would loop. The dirt function is the same one the lots use, so a
    # blast sitting on clear ground meets the neighbouring tile.
    import make_atlas

    return make_atlas._dirt_colour(x, y, False)


def _components(pixels: list[tuple[int, int]]) -> list[list[tuple[int, int]]]:
    remaining = set(pixels)
    groups: list[list[tuple[int, int]]] = []
    while remaining:
        start = remaining.pop()
        stack = [start]
        group = [start]
        while stack:
            x, y = stack.pop()
            for dx, dy in _ORTHO:
                nxt = (x + dx, y + dy)
                if nxt in remaining:
                    remaining.remove(nxt)
                    stack.append(nxt)
                    group.append(nxt)
        groups.append(group)
    return groups


def lift_roofs(tile: Image.Image) -> Image.Image:
    """Lift the shaded roof greys on one small house. The lot stays dirt."""
    out = tile.copy()
    src = tile.load()
    dst = out.load()
    width, height = tile.size
    for y in range(height):
        for x in range(width):
            lifted = ROOF_LIFT.get(src[x, y])
            if lifted is None:
                continue
            # A lone grey speck in the dirt is not a roof.
            neighbours = set(ROOF_LIFT) | {(0, 0, 0), (255, 255, 0), (255, 255, 85), (255, 125, 0)}
            if not any(
                0 <= x + dx < width and 0 <= y + dy < height and src[x + dx, y + dy] in neighbours
                for dx, dy in _ORTHO
            ):
                continue
            dst[x, y] = lifted
    return out


def emboss_r(tile: Image.Image) -> Image.Image:
    """Two-tone the residential R. Lot borders and the C/I letters stay flat."""
    src = tile.load()
    width, height = tile.size
    greens = [(x, y) for y in range(height) for x in range(width) if src[x, y] == R_LIGHT]
    if len(greens) < len(R_MARK):
        return tile
    out = tile.copy()
    dst = out.load()
    changed = False
    for group in _components(greens):
        if len(group) != len(R_MARK):
            continue
        x0 = min(x for x, _y in group)
        y0 = min(y for _x, y in group)
        if {(x - x0, y - y0) for x, y in group} != R_MARK:
            continue
        marks = set(group)
        for x, y in group:
            light = any((x + dx, y + dy) not in marks for dx, dy in ((0, -1), (-1, 0)))
            shade = any((x + dx, y + dy) not in marks for dx, dy in ((0, 1), (1, 0)))
            if light and not shade:
                dst[x, y] = R_LIGHT
            elif shade and not light:
                dst[x, y] = R_SHADE
            else:
                dst[x, y] = R_BODY
        changed = True
    return out if changed else tile


def paint_explosion(classic: Image.Image, index: int) -> Image.Image:
    """One tiny-explosion frame. The black shape is the blast; white is dirt."""
    phase = index - 868
    src = classic.load()
    out = Image.new("RGB", (TILE, TILE))
    dst = out.load()
    ink: list[tuple[int, int]] = []
    for y in range(TILE):
        for x in range(TILE):
            red, green, blue = src[x, y]
            if red < 32 and green < 32 and blue < 32:
                ink.append((x, y))
            else:
                dst[x, y] = _dirt_at(x, y)
    ink_set = set(ink)
    cool = phase / 15.0
    for x, y in ink:
        edge = any((x + dx, y + dy) not in ink_set for dx, dy in _ORTHO)
        deep = not edge and all((x + dx, y + dy) in ink_set for dx, dy in _ORTHO)
        if cool < 0.34:
            colour = FLAME_YELLOW if deep else FLAME_ORANGE if not edge else FLAME_RED
        elif cool < 0.62:
            colour = FLAME_ORANGE if deep else FLAME_RED
        elif cool < 0.84:
            colour = FLAME_RED if not edge else SMOKE
        else:
            colour = SMOKE if (x + y + phase) % 2 == 0 else SMOKE_DARK
        dst[x, y] = colour
    return out


def polish_tile(index: int, classic: Image.Image, painted: Image.Image) -> Image.Image:
    image = painted
    if 249 <= index <= 260:
        image = lift_roofs(image)
    if 868 <= index <= 883:
        image = paint_explosion(classic, index)
    image = emboss_r(image)
    return image


def assert_palette(image: Image.Image, palette: set[tuple[int, int, int]] | None = None) -> None:
    allowed = palette if palette is not None else _palette()
    px = image.load()
    for y in range(image.size[1]):
        for x in range(image.size[0]):
            if px[x, y] not in allowed:
                raise SystemExit(f"polish left the VGA palette at {(x, y)}: {px[x, y]}")
