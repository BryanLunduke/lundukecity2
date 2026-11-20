#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Lunduke City authors
# See COPYING and NOTICE.
"""Original sprites for City 2's own disasters.

Writes data/disaster-art/:
  lion.png, lion-svga.png          128x16: 4 directions (N, E, S, W) x 2
                                   walk frames, 16x16 each, seen from the
                                   game's top-down 3/4 angle.
  barricade.png, barricade-svga.png 96x16: three barricades along a
                                   horizontal tile edge (sawhorse, concrete
                                   barrier, cones), then the same three
                                   along a vertical edge, each centred in
                                   its 16x16 cell.

The plain files are the Classic look (flat colours, black outline); the
-svga files are the SVGA look (shaded, softer outline). Everything is drawn
here from simple shapes, pixel by pixel; nothing is traced or copied.

Run from the repository root: python3 art/disasters/make_disaster_art.py
"""

import math
import os

from PIL import Image

OUT = os.path.join("data", "disaster-art")

PALETTES = {
    "classic": {
        "k": (0, 0, 0, 255),
        "b": (232, 168, 56, 255),
        "h": (232, 168, 56, 255),
        "s": (196, 124, 32, 255),
        "m": (168, 76, 0, 255),
        "n": (120, 48, 0, 255),
        "e": (0, 0, 0, 255),
        "w": (252, 232, 168, 255),
        "o": (252, 116, 0, 255),
        "x": (252, 252, 252, 255),
        "g": (96, 96, 96, 255),
        "y": (252, 220, 0, 255),
        "c": (168, 168, 168, 255),
        "C": (212, 212, 212, 255),
        "d": (120, 120, 120, 255),
        "r": (216, 0, 0, 255),
        "shadow": (0, 0, 0, 72),
    },
    "svga": {
        "k": (52, 30, 14, 255),
        "b": (220, 164, 78, 255),
        "h": (242, 200, 120, 255),
        "s": (176, 120, 52, 255),
        "m": (150, 78, 28, 255),
        "n": (104, 50, 18, 255),
        "e": (24, 14, 8, 255),
        "w": (248, 230, 186, 255),
        "o": (236, 112, 28, 255),
        "x": (244, 240, 230, 255),
        "g": (84, 80, 76, 255),
        "y": (255, 214, 64, 255),
        "c": (170, 166, 158, 255),
        "C": (206, 202, 192, 255),
        "d": (124, 120, 112, 255),
        "r": (200, 40, 32, 255),
        "shadow": (0, 0, 0, 64),
    },
}


class Grid:
    def __init__(self, w=16, h=16):
        self.w = w
        self.h = h
        self.px = [["." for _ in range(w)] for _ in range(h)]

    def put(self, x, y, c):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.px[y][x] = c

    def get(self, x, y):
        if 0 <= x < self.w and 0 <= y < self.h:
            return self.px[y][x]
        return "."

    def ellipse(self, cx, cy, rx, ry, c):
        for y in range(self.h):
            for x in range(self.w):
                dx = (x + 0.5 - cx) / rx
                dy = (y + 0.5 - cy) / ry
                if dx * dx + dy * dy <= 1.0:
                    self.put(x, y, c)

    def rect(self, x0, y0, x1, y1, c):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                self.put(x, y, c)

    def outline(self):
        # Any empty pixel touching a filled one, edge or corner on the
        # sides, edge only on the diagonals, becomes outline.
        marks = []
        for y in range(self.h):
            for x in range(self.w):
                if self.px[y][x] != ".":
                    continue
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    if self.get(x + dx, y + dy) not in (".", "k"):
                        marks.append((x, y))
                        break
        for x, y in marks:
            self.px[y][x] = "k"

    def shade(self, base, top, bottom):
        # Light from the top-left: the top pixel of each run of `base` in a
        # column goes `top`, the bottom pixel goes `bottom`.
        for x in range(self.w):
            for y in range(self.h):
                if self.px[y][x] != base:
                    continue
                if self.get(x, y - 1) not in (base, top):
                    self.px[y][x] = top
                elif self.get(x, y + 1) not in (base, bottom, top):
                    self.px[y][x] = bottom

    def mirror(self):
        g = Grid(self.w, self.h)
        for y in range(self.h):
            g.px[y] = list(reversed(self.px[y]))
        return g


def lion_east(frame):
    g = Grid()
    # Tail first so the body covers its root.
    for i, (x, y) in enumerate(((3, 8), (2, 8), (1, 7), (1, 6))):
        g.put(x, y, "b")
    g.put(1, 5, "n")
    g.put(2, 5, "m")
    # Legs: the front pair and back pair swap each step.
    legs = ((4, 11), (10, 11)) if frame == 0 else ((5, 11), (9, 11))
    back_legs = ((5, 11), (9, 11)) if frame == 0 else ((4, 11), (10, 11))
    for x, y in back_legs:
        g.rect(x, y, x, y + 1, "s")
    for x, y in legs:
        g.rect(x, y, x, y + 1, "b")
    g.ellipse(7.2, 9.0, 4.6, 2.4, "b")
    # Mane, then the face inside it.
    g.ellipse(11.6, 6.8, 2.9, 3.0, "m")
    g.ellipse(12.6, 7.2, 1.6, 1.6, "b")
    g.shade("b", "h", "s")
    g.put(10, 4, "n")
    g.put(9, 7, "n")
    g.put(10, 9, "n")
    g.put(13, 6, "e")  # eye
    g.put(14, 7, "w")  # muzzle
    g.put(14, 8, "w")
    g.put(15, 7, "e")  # nose
    g.outline()
    return g


def lion_north(frame):
    g = Grid()
    g.put(8, 14, "b")
    g.put(8, 15, "n")
    g.put(7, 15, "m")
    if frame == 0:
        g.rect(5, 6, 5, 7, "b")
        g.rect(10, 11, 10, 12, "b")
        g.rect(10, 7, 10, 8, "s")
        g.rect(5, 10, 5, 11, "s")
    else:
        g.rect(10, 6, 10, 7, "b")
        g.rect(5, 11, 5, 12, "b")
        g.rect(5, 7, 5, 8, "s")
        g.rect(10, 10, 10, 11, "s")
    g.ellipse(8.0, 9.6, 2.2, 4.0, "b")
    g.ellipse(8.0, 5.0, 3.0, 2.8, "m")
    g.shade("b", "h", "s")
    g.put(7, 3, "n")
    g.put(9, 4, "n")
    g.put(6, 6, "n")
    g.put(8, 9, "s")  # spine
    g.put(8, 11, "s")
    g.outline()
    return g


def lion_south(frame):
    g = Grid()
    g.put(8, 2, "b")
    g.put(8, 1, "n")
    g.put(9, 1, "m")
    if frame == 0:
        g.rect(5, 4, 5, 5, "s")
        g.rect(10, 8, 10, 9, "b")
        g.rect(10, 3, 10, 4, "s")
        g.rect(5, 8, 5, 9, "b")
    else:
        g.rect(10, 4, 10, 5, "s")
        g.rect(5, 8, 5, 9, "b")
        g.rect(5, 3, 5, 4, "s")
        g.rect(10, 9, 10, 10, "b")
    g.ellipse(8.0, 6.0, 2.2, 3.6, "b")
    g.ellipse(8.0, 10.6, 3.1, 3.0, "m")
    g.ellipse(8.0, 11.2, 1.8, 1.9, "b")
    g.shade("b", "h", "s")
    g.put(6, 9, "n")
    g.put(10, 12, "n")
    g.put(7, 10, "e")  # eyes
    g.put(9, 10, "e")
    g.put(7, 12, "w")  # muzzle
    g.put(9, 12, "w")
    g.put(8, 12, "e")  # nose
    g.outline()
    return g


def stripes(g, x0, y0, x1, y1, along_x):
    # Orange and white bands that lean one pixel per row or column.
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            k = (x + y) if along_x else (y + x)
            g.put(x, y, "o" if (k // 2) % 2 == 0 else "x")


def cone(g, cx, base_y):
    # A traffic cone seen from above and a little in front: black foot,
    # orange body with a white band, tip on top.
    g.rect(cx - 2, base_y, cx + 2, base_y, "g")
    g.rect(cx - 1, base_y - 1, cx + 1, base_y - 1, "o")
    g.rect(cx - 1, base_y - 2, cx + 1, base_y - 2, "x")
    g.put(cx, base_y - 3, "o")
    g.put(cx, base_y - 4, "o")


def barricade_sawhorse_h():
    g = Grid()
    for x in (3, 12):
        g.rect(x, 9, x, 11, "g")
        g.put(x - 1, 11, "g")
        g.put(x + 1, 11, "g")
    stripes(g, 1, 5, 14, 8, True)
    g.put(3, 4, "y")  # warning lamp
    g.outline()
    return g


def barricade_concrete_h():
    g = Grid()
    g.rect(0, 5, 15, 9, "c")
    g.rect(0, 5, 15, 5, "C")
    g.rect(0, 9, 15, 9, "d")
    # Joints between the precast sections, and a reflector on each.
    g.rect(7, 5, 7, 9, "d")
    g.put(3, 7, "r")
    g.put(11, 7, "r")
    g.outline()
    return g


def barricade_cones_h():
    g = Grid()
    cone(g, 4, 10)
    cone(g, 11, 10)
    # A tape strung between them.
    g.rect(6, 7, 9, 7, "o")
    g.outline()
    return g


def barricade_sawhorse_v():
    g = Grid()
    for y in (3, 12):
        g.rect(5, y, 10, y, "g")
    stripes(g, 7, 1, 8, 14, False)
    g.put(9, 1, "y")
    g.outline()
    return g


def barricade_concrete_v():
    g = Grid()
    g.rect(6, 0, 9, 15, "c")
    g.rect(6, 0, 6, 15, "C")
    g.rect(9, 0, 9, 15, "d")
    g.rect(6, 7, 9, 7, "d")
    g.put(8, 3, "r")
    g.put(8, 11, "r")
    g.outline()
    return g


def barricade_cones_v():
    g = Grid()
    cone(g, 8, 6)
    cone(g, 8, 14)
    g.rect(8, 8, 8, 9, "o")
    g.outline()
    return g


def render(grids, palette, shadow=True):
    w = sum(grid.w for grid in grids)
    h = max(grid.h for grid in grids)
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    ox = 0
    for grid in grids:
        if shadow:
            # A soft ground shadow one pixel down and right of the outline.
            for y in range(grid.h):
                for x in range(grid.w):
                    if grid.get(x - 1, y - 1) != "." and grid.get(x, y) == ".":
                        img.putpixel((ox + x, y), palette["shadow"])
        for y in range(grid.h):
            for x in range(grid.w):
                c = grid.px[y][x]
                if c != ".":
                    img.putpixel((ox + x, y), palette[c])
        ox += grid.w
    return img


def main():
    os.makedirs(OUT, exist_ok=True)
    lion = []
    for direction in range(4):
        for frame in range(2):
            if direction == 0:
                lion.append(lion_north(frame))
            elif direction == 1:
                lion.append(lion_east(frame))
            elif direction == 2:
                lion.append(lion_south(frame))
            else:
                lion.append(lion_east(frame).mirror())
    pieces = [
        barricade_sawhorse_h(),
        barricade_concrete_h(),
        barricade_cones_h(),
        barricade_sawhorse_v(),
        barricade_concrete_v(),
        barricade_cones_v(),
    ]
    for look, suffix in (("classic", ""), ("svga", "-svga")):
        palette = PALETTES[look]
        render(lion, palette).save(os.path.join(OUT, "lion" + suffix + ".png"))
        render(pieces, palette).save(os.path.join(OUT, "barricade" + suffix + ".png"))


if __name__ == "__main__":
    main()
