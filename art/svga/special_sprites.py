#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Lunduke City authors
# See COPYING and NOTICE.

"""SVGA and Spooky library and zoo sprites, plus the picker icons.

Original drawings. The classic files are not rewritten. The view is the
city's: a top-down 3/4 with the walls sheared up and to the right, light
from the upper left, and the hospital's shadow green on the south-east.

    python3 art/svga/special_sprites.py
"""

from __future__ import annotations

import sys
from pathlib import Path

from PIL import Image

from vga_palette import vga_palette_8bit

ROOT = Path(__file__).resolve().parents[2]
SPECIAL = ROOT / "data" / "special-tiles"
ICONS = ROOT / "data" / "tool-icons"

# Day palette. Deep greens, concrete, a slate roof. No neon.
GRASS = (0, 113, 0)
GRASS_LIT = (28, 113, 0)
GRASS_DARK = (0, 65, 0)
DIRT = (113, 56, 0)
DIRT_DARK = (65, 32, 0)
WALL = (182, 182, 182)
WALL_LIT = (203, 203, 203)
WALL_SHADE = (130, 130, 130)
WALL_DARK = (81, 81, 81)
ROOF = (56, 56, 113)
ROOF_LIT = (81, 81, 113)
ROOF_EDGE = (32, 32, 65)
GLASS = (125, 158, 255)
GLASS_DARK = (56, 56, 113)
DOOR = (65, 32, 0)
PAGE = (255, 255, 255)
BOOK = (170, 0, 0)
INK = (0, 0, 0)
SHADOW = (32, 65, 32)
FENCE = (113, 85, 56)
FENCE_DARK = (65, 32, 0)
ANIMAL = (162, 162, 162)
ANIMAL_DARK = (97, 97, 97)
ANIMAL_LIT = (203, 203, 203)
TRUNK = (113, 113, 113)

# Night. Same drawing, cemetery-adjacent ground, violet stone, candle glass.
NIGHT_GRASS = (0, 65, 16)
NIGHT_LIT = (16, 65, 0)
NIGHT_DARK = (0, 65, 0)
NIGHT_DIRT = (65, 48, 32)
NIGHT_WALL = (97, 81, 113)
NIGHT_WALL_LIT = (113, 81, 113)
NIGHT_WALL_SHADE = (65, 44, 65)
NIGHT_ROOF = (48, 32, 65)
NIGHT_ROOF_LIT = (65, 44, 65)
NIGHT_GLASS = (255, 255, 85)
NIGHT_FENCE = (69, 69, 69)
PUMPKIN = (170, 85, 0)
PUMPKIN_DARK = (113, 56, 0)


def _check(colours: list[tuple[int, int, int]]) -> None:
    palette = set(vga_palette_8bit())
    missing = sorted({c for c in colours if c not in palette})
    if missing:
        raise SystemExit(f"sprite colour left the VGA palette: {missing}")


def _poly(px, pts: list[tuple[int, int]], colour: tuple[int, int, int], w: int, h: int) -> None:
    if len(pts) < 3:
        return
    y0 = max(0, min(p[1] for p in pts))
    y1 = min(h - 1, max(p[1] for p in pts))
    for y in range(y0, y1 + 1):
        xs: list[float] = []
        for i, (x1, py1) in enumerate(pts):
            x2, py2 = pts[(i + 1) % len(pts)]
            if (py1 <= y < py2) or (py2 <= y < py1):
                if py2 == py1:
                    continue
                xs.append(x1 + (y - py1) * (x2 - x1) / (py2 - py1))
        xs.sort()
        pairs = xs[: len(xs) - len(xs) % 2]
        for i in range(0, len(pairs), 2):
            left = max(0, int(round(pairs[i])))
            right = min(w - 1, int(round(pairs[i + 1])))
            for x in range(left, right + 1):
                px[x, y] = (*colour, 255)


def _outline(image: Image.Image) -> None:
    src = image.copy()
    sp = src.load()
    px = image.load()
    w, h = image.size
    for y in range(h):
        for x in range(w):
            if sp[x, y][3] == 0:
                continue
            for dx, dy in ((0, -1), (0, 1), (-1, 0), (1, 0)):
                nx, ny = x + dx, y + dy
                if not (0 <= nx < w and 0 <= ny < h) or sp[nx, ny][3] == 0:
                    if 0 <= nx < w and 0 <= ny < h and px[nx, ny][3] == 0:
                        px[nx, ny] = (*INK, 255)


def _shadow(image: Image.Image, body: set[tuple[int, int]]) -> None:
    """One-pixel east face, then a widening south-east fall on empty ground."""
    px = image.load()
    w, h = image.size
    for x, y in body:
        nx = x + 1
        if 0 <= nx < w and (nx, y) not in body and px[nx, y][3]:
            px[nx, y] = (*SHADOW, 255)
    for x, y in list(body):
        for step, width in enumerate((1, 3, 5), start=1):
            sy = y + step
            for dx in range(width):
                sx = x + 1 + dx
                if 0 <= sx < w and 0 <= sy < h and (sx, sy) not in body and px[sx, sy][3]:
                    # Keep the outer edge from eating the whole lawn.
                    if dx == width - 1 or step == 1:
                        px[sx, sy] = (*SHADOW, 255)


def _grass(px, w: int, h: int, spooky: bool) -> None:
    if spooky:
        # Darker and less saturated than a flat (0, 65, 16) sheet, so the
        # lot sits with the night lawn instead of a daytime green.
        for y in range(h):
            for x in range(w):
                n = (x * 5 + y * 3) % 19
                if n == 0:
                    colour = (32, 65, 32)
                elif n in (1, 2):
                    colour = NIGHT_GRASS
                elif n == 3:
                    colour = NIGHT_LIT
                else:
                    colour = NIGHT_DARK
                px[x, y] = (*colour, 255)
        return
    base, lit, dark = GRASS, GRASS_LIT, GRASS_DARK
    for y in range(h):
        for x in range(w):
            if (x + y * 3) % 11 == 0:
                px[x, y] = (*lit, 255)
            elif (x * 5 + y) % 13 == 0:
                px[x, y] = (*dark, 255)
            else:
                px[x, y] = (*base, 255)


def _library(size: int, spooky: bool, icon: bool) -> Image.Image:
    image = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    px = image.load()
    if not icon:
        _grass(px, size, size, spooky)
    wall = NIGHT_WALL if spooky else WALL
    wall_lit = NIGHT_WALL_LIT if spooky else WALL_LIT
    wall_shade = NIGHT_WALL_SHADE if spooky else WALL_SHADE
    wall_dark = NIGHT_ROOF if spooky else WALL_DARK
    roof = NIGHT_ROOF if spooky else ROOF
    roof_lit = NIGHT_ROOF_LIT if spooky else ROOF_LIT
    glass = NIGHT_GLASS if spooky else GLASS
    # Sheared hall. Base sits low; the top of the wall is up and to the right.
    s = size / 48.0
    def P(x: float, y: float) -> tuple[int, int]:
        return int(round(x * s)), int(round(y * s))

    south = [P(6, 40), P(30, 40), P(42, 26), P(18, 26)]
    east = [P(30, 40), P(40, 44), P(48, 32), P(42, 26)]
    roof_pts = [P(18, 26), P(42, 26), P(36, 12), P(12, 12)]
    body: set[tuple[int, int]] = set()
    # Paint into a mask by drawing on a copy of filled pixels.
    before = {(x, y) for y in range(size) for x in range(size) if px[x, y][3]}
    _poly(px, south, wall, size, size)
    _poly(px, east, wall_shade, size, size)
    _poly(px, roof_pts, roof, size, size)
    # Lit roof cap, upper left.
    _poly(px, [P(12, 12), P(28, 12), P(24, 16), P(14, 16)], roof_lit, size, size)
    # Columns on the south face. Each is a short sheared stroke.
    for i, x0 in enumerate((10, 18, 26)):
        col = [P(x0, 39), P(x0 + 3, 39), P(x0 + 12, 28), P(x0 + 9, 28)]
        _poly(px, col, wall_lit if i != 1 else PAGE, size, size)
    # Door between the middle columns, on the south wall.
    _poly(px, [P(20, 39), P(25, 39), P(33, 30), P(29, 30)], DOOR, size, size)
    # Book standing on the roof, the thing that says library.
    _poly(px, [P(20, 14), P(28, 14), P(30, 20), P(22, 20)], PAGE, size, size)
    _poly(px, [P(23, 14), P(25, 14), P(27, 20), P(25, 20)], BOOK, size, size)
    # One window on the east face.
    _poly(px, [P(36, 36), P(39, 37), P(43, 32), P(40, 31)], glass, size, size)
    if spooky:
        # A candle on the roof peak.
        px[P(16, 10)] = (*NIGHT_GLASS, 255)
        px[P(16, 11)] = (*PUMPKIN, 255)
    for y in range(size):
        for x in range(size):
            if px[x, y][3] and (x, y) not in before:
                body.add((x, y))
    if not icon:
        _shadow(image, body)
    else:
        _outline(image)
    # Roof edge so the plane separates from the wall.
    _poly(px, [P(18, 26), P(42, 26), P(42, 27), P(18, 27)], ROOF_EDGE if not spooky else NIGHT_ROOF, size, size)
    return image


def _zoo(size: int, spooky: bool, icon: bool) -> Image.Image:
    image = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    px = image.load()
    if not icon:
        _grass(px, size, size, spooky)
    dirt = NIGHT_DIRT if spooky else DIRT
    fence = NIGHT_FENCE if spooky else FENCE
    fence_dark = INK if spooky else FENCE_DARK
    s = size / 48.0
    def P(x: float, y: float) -> tuple[int, int]:
        return int(round(x * s)), int(round(y * s))

    before = {(x, y) for y in range(size) for x in range(size) if px[x, y][3]}
    # Path.
    _poly(px, [P(0, 34), P(18, 34), P(26, 26), P(8, 26)], dirt, size, size)
    # Pen floor.
    _poly(px, [P(16, 36), P(40, 36), P(46, 24), P(22, 24)], dirt if not spooky else (65, 32, 32), size, size)
    # Fence rails, sheared with the pen.
    for y0, y1 in ((24, 26), (34, 36)):
        _poly(px, [P(16, y1), P(40, y1), P(46, y0 + 2), P(22, y0 + 2), P(22, y0), P(46, y0), P(40, y1 - 2), P(16, y1 - 2)], fence, size, size)
    # Posts.
    for x0 in (18, 28, 38):
        _poly(px, [P(x0, 38), P(x0 + 2, 38), P(x0 + 10, 26), P(x0 + 8, 26)], fence_dark, size, size)
    # Elephant in plan, sheared with the pen. The back is up-left and lit.
    # The head is the near end. Ears stick out to the sides, the trunk
    # curls on the ground, and four feet sit under the body.
    _ellipse_scaled(px, P, 30, 29, 8, 5, ANIMAL, size)
    _ellipse_scaled(px, P, 28, 27, 4, 2, ANIMAL_LIT, size)
    _ellipse_scaled(px, P, 36, 33, 4, 3, ANIMAL_LIT, size)
    _ellipse_scaled(px, P, 26, 24, 3, 2, ANIMAL_DARK, size)
    _ellipse_scaled(px, P, 40, 36, 3, 2, ANIMAL_DARK, size)
    for rx, ry in ((42, 35), (44, 37), (45, 39), (43, 40), (41, 39)):
        x, y = P(rx, ry)
        if 0 <= x < size and 0 <= y < size:
            px[x, y] = (*TRUNK, 255)
    for fx, fy in ((24, 32), (34, 35), (27, 25), (35, 27)):
        _ellipse_scaled(px, P, fx, fy, 2, 1, ANIMAL_DARK, size)
    eye = P(38, 32)
    if 0 <= eye[0] < size and 0 <= eye[1] < size:
        px[eye] = (*INK, 255)
    # Booth on the path.
    _poly(px, [P(2, 32), P(12, 32), P(16, 26), P(6, 26)], WALL if not spooky else NIGHT_WALL, size, size)
    _poly(px, [P(6, 26), P(16, 26), P(14, 22), P(4, 22)], ROOF if not spooky else NIGHT_ROOF, size, size)
    if spooky:
        px[P(8, 21)] = (*NIGHT_GLASS, 255)
        # One pumpkin by the gate.
        _poly(px, [P(14, 40), P(18, 40), P(19, 37), P(15, 37)], PUMPKIN, size, size)
        px[P(16, 36)] = (*NIGHT_DARK, 255)
    body = {(x, y) for y in range(size) for x in range(size) if px[x, y][3] and (x, y) not in before}
    if not icon:
        _shadow(image, body)
    else:
        _outline(image)
    return image


def _ellipse_scaled(px, place, cx: float, cy: float, rx: float, ry: float, colour: tuple[int, int, int], size: int) -> None:
    """Fill an ellipse in the 48-unit design space. `place` maps that space to pixels."""
    if rx <= 0 or ry <= 0:
        return
    for dy in range(int(-ry) - 1, int(ry) + 2):
        for dx in range(int(-rx) - 1, int(rx) + 2):
            if (dx / rx) ** 2 + (dy / ry) ** 2 <= 1:
                x, y = place(cx + dx, cy + dy)
                if 0 <= x < size and 0 <= y < size:
                    px[x, y] = (*colour, 255)


def _ellipse(px, cx: int, cy: int, rx: int, ry: int, colour: tuple[int, int, int], w: int, h: int) -> None:
    for y in range(max(0, cy - ry), min(h, cy + ry + 1)):
        for x in range(max(0, cx - rx), min(w, cx + rx + 1)):
            nx = (x - cx) / max(rx, 1)
            ny = (y - cy) / max(ry, 1)
            if nx * nx + ny * ny <= 1:
                px[x, y] = (*colour, 255)


def _big_park() -> Image.Image:
    """A 3/4 park in the SVGA greens. No neon (0, 170, 0).

    The ground matches the park tiles: deep (0, 113, 0), a lit fleck, and
    the dark canopy green. The path uses the smooth earth colours.
    """
    size = 48
    image = Image.new("RGBA", (size, size), (0, 0, 0, 255))
    px = image.load()
    earth = (113, 89, 81)
    earth_lit = (113, 105, 81)
    earth_dark = (65, 48, 44)
    for y in range(size):
        for x in range(size):
            n = (x * 5 + y * 7) % 23
            if n == 0:
                px[x, y] = (*GRASS_LIT, 255)
            elif n in (1, 2):
                px[x, y] = (*GRASS_DARK, 255)
            else:
                px[x, y] = (*GRASS, 255)
    # A sheared path, light on the upper-left edge.
    for i in range(40):
        x = 4 + i
        y = 40 - i // 2
        for t in range(4):
            sx, sy = x + t, y + t // 2
            if 0 <= sx < size and 0 <= sy < size:
                px[sx, sy] = (*earth, 255)
                if t == 0:
                    px[sx, sy] = (*earth_lit, 255)
    for sx, sy in ((18, 28), (30, 22), (12, 36)):
        if px[sx, sy][:3] == earth:
            px[sx, sy] = (*earth_dark, 255)
    # Pond, upper-right of the path's end. Highlight on the upper left.
    for y in range(8, 18):
        for x in range(30, 44):
            nx = (x - 37) / 7
            ny = (y - 13) / 4
            if nx * nx + ny * ny <= 1:
                if x + y < 46:
                    px[x, y] = (0, 85, 113, 255)
                else:
                    px[x, y] = (0, 56, 113, 255)
    px[32, 11] = (125, 223, 255, 255)
    # Trees. Canopy from above, trunk, hospital shadow to the south-east.
    trees = ((10, 12, 7, 5), (22, 16, 5, 4), (14, 24, 4, 3))
    for cx, cy, rx, ry in trees:
        for y in range(cy + 2, cy + ry + 4):
            for x in range(cx + 2, cx + rx + 3):
                if 0 <= x < size and 0 <= y < size and px[x, y][:3] in (GRASS, GRASS_LIT, GRASS_DARK, earth):
                    px[x, y] = (*SHADOW, 255)
        _ellipse(px, cx, cy, rx, ry, GRASS_DARK, size, size)
        _ellipse(px, cx - 1, cy - 1, max(rx - 2, 1), max(ry - 2, 1), GRASS, size, size)
        for dy in range(2):
            for dx in range(2):
                tx, ty = cx + 1 + dx, cy + ry - 1 + dy
                if 0 <= tx < size and 0 <= ty < size:
                    px[tx, ty] = (113, 85, 56, 255)
    # Bench beside the path, sheared, light on the upper-left plank.
    _poly(px, [(8, 32), (16, 30), (18, 33), (10, 35)], earth, size, size)
    _poly(px, [(8, 32), (16, 30), (16, 31), (8, 33)], earth_lit, size, size)
    _poly(px, [(10, 35), (12, 35), (13, 38), (11, 38)], (65, 32, 0), size, size)
    _poly(px, [(16, 33), (18, 33), (19, 36), (17, 36)], (65, 32, 0), size, size)
    return image


def _save(image: Image.Image, path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    image.save(path)


def _sheets() -> list[tuple[Path, Image.Image]]:
    return [
        (SPECIAL / "library-svga.png", _library(48, False, False)),
        (SPECIAL / "library-spooky.png", _library(48, True, False)),
        (SPECIAL / "zoo-svga.png", _zoo(48, False, False)),
        (SPECIAL / "zoo-spooky.png", _zoo(48, True, False)),
        (SPECIAL / "big-park-svga.png", _big_park()),
        (ICONS / "library-svga.png", _library(40, False, True)),
        (ICONS / "library-spooky.png", _library(40, True, True)),
        (ICONS / "zoo-svga.png", _zoo(40, False, True)),
        (ICONS / "zoo-spooky.png", _zoo(40, True, True)),
    ]


def main(argv: list[str]) -> int:
    colours = [
        GRASS, GRASS_LIT, GRASS_DARK, DIRT, DIRT_DARK, WALL, WALL_LIT, WALL_SHADE, WALL_DARK,
        ROOF, ROOF_LIT, ROOF_EDGE, GLASS, GLASS_DARK, DOOR, PAGE, BOOK, INK, SHADOW, FENCE,
        FENCE_DARK, ANIMAL, ANIMAL_DARK, ANIMAL_LIT, TRUNK, NIGHT_GRASS, NIGHT_LIT, NIGHT_DARK,
        NIGHT_DIRT, NIGHT_WALL, NIGHT_WALL_LIT, NIGHT_WALL_SHADE, NIGHT_ROOF, NIGHT_ROOF_LIT,
        NIGHT_GLASS, NIGHT_FENCE, PUMPKIN, PUMPKIN_DARK, (65, 32, 32), (32, 65, 32),
        (113, 89, 81), (113, 105, 81), (65, 48, 44), (0, 85, 113), (0, 56, 113),
        (125, 223, 255), (113, 85, 56), (65, 32, 0),
    ]
    _check(colours)
    sheets = _sheets()
    if "--check" in argv:
        import hashlib

        failed = False
        for path, image in sheets:
            if not path.is_file():
                print(f"FAIL missing {path}")
                failed = True
                continue
            with Image.open(path) as opened:
                committed = opened.convert("RGBA")
            if committed.tobytes() != image.tobytes():
                digest = hashlib.sha256(image.tobytes()).hexdigest()
                print(f"FAIL {path.name} does not match the generator {digest}")
                failed = True
        if failed:
            return 1
        print("OK special sprites match the generator")
        return 0
    for path, image in sheets:
        _save(image, path)
    print("wrote library, zoo, and SVGA big park")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
