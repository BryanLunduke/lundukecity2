#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Lunduke City authors
# See COPYING and NOTICE.
"""Original sprites for City 2's own disasters.

Writes data/disaster-art/:
  lion.png, lion-svga.png          192x24: 4 directions (N, E, S, W) x 2
                                   walk frames, 24x24 each (a road tile and
                                   a half), seen from the game's top-down
                                   3/4 angle.
  barricade.png, barricade-svga.png 112x16: three barricades along a
                                   horizontal tile edge (sawhorse, concrete
                                   barrier, cones), the same three along a
                                   vertical edge, then a corner block, one
                                   16x16 cell each.

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


LION = 24  # lion cell size: a road tile and a half


def tuft(g, cx, cy, c="n"):
    # The dark tassel at the end of a lion's tail.
    g.ellipse(cx, cy, 1.6, 1.6, c)


def mane_ring(g, cx, cy, rx, ry, spikes, c="m"):
    # A full, shaggy mane: an ellipse with tufts poking out around it.
    g.ellipse(cx, cy, rx, ry, c)
    for k in range(spikes):
        a = 2 * math.pi * k / spikes
        x = cx + math.cos(a) * (rx + 0.6)
        y = cy + math.sin(a) * (ry + 0.6)
        g.put(int(x), int(y), c)


def lion_east(frame):
    g = Grid(LION, LION)
    # Tail: from the rump, a curve up and back, ending in a dark tuft.
    for x, y in ((5, 12), (4, 12), (3, 11), (2, 10), (2, 9), (2, 8)):
        g.put(x, y, "s")
    tuft(g, 2.5, 6.8)
    # Legs (2 px wide): the back pair darker, the pairs swap each step.
    reach = ((5, 17), (16, 17)) if frame == 0 else ((7, 14), (14, 14))
    tuck = ((7, 14), (14, 14)) if frame == 0 else ((5, 17), (16, 17))
    for x, xb in tuck:
        g.rect(x, 15, x + 1, 19, "s")
    for x, xb in reach:
        g.rect(x, 15, x + 1, 20, "b")
        g.put(x + 2, 20, "b")  # paw
    # Body: long and low, a deeper chest under the mane.
    g.ellipse(11.0, 13.2, 7.0, 3.6, "b")
    g.ellipse(15.0, 13.0, 3.4, 4.0, "b")
    # Mane: big and shaggy, darker than the coat.
    mane_ring(g, 17.6, 9.6, 4.6, 5.2, 14)
    # Face inside the mane, snout poking out to the right.
    g.ellipse(19.6, 10.0, 2.6, 2.6, "b")
    g.rect(21, 10, 23, 12, "b")
    g.shade("b", "h", "s")
    g.put(16, 6, "n")
    g.put(14, 9, "n")
    g.put(15, 13, "n")
    g.put(18, 14, "n")
    g.put(17, 5, "b")  # ear
    g.put(20, 9, "e")  # eye
    g.rect(22, 11, 23, 12, "w")  # muzzle
    g.put(23, 10, "e")  # nose
    g.put(21, 12, "n")  # mouth line
    g.outline()
    return g


def lion_north(frame):
    # Walking away (up the screen): the back, the mane from behind, the
    # tail and its tuft trailing below.
    g = Grid(LION, LION)
    g.rect(12, 19, 12, 20, "s")
    tuft(g, 12.0, 21.6)
    a, b = (0, 2) if frame == 0 else (2, 0)
    for x in (5, 17):  # forelegs reaching out beside the mane
        g.rect(x, 8 + (a if x == 5 else b), x + 1, 11 + (a if x == 5 else b), "b")
    for x in (6, 16):  # hind legs
        g.rect(x, 14 + (b if x == 6 else a), x + 1, 17 + (b if x == 6 else a), "s")
    g.ellipse(12.0, 13.4, 4.6, 6.2, "b")
    mane_ring(g, 12.0, 7.0, 5.2, 4.4, 16)
    g.ellipse(12.0, 3.4, 2.4, 1.8, "b")  # crown of the head
    g.put(10, 2, "n")  # ears
    g.put(14, 2, "n")
    g.shade("b", "h", "s")
    g.put(12, 12, "s")  # spine
    g.put(12, 14, "s")
    g.put(12, 16, "s")
    g.put(9, 6, "n")
    g.put(15, 8, "n")
    g.put(11, 10, "n")
    g.outline()
    return g


def lion_south(frame):
    # Walking toward the viewer: the face framed by the whole mane.
    g = Grid(LION, LION)
    g.rect(12, 3, 12, 4, "s")
    tuft(g, 12.0, 2.0)
    a, b = (0, 2) if frame == 0 else (2, 0)
    for x in (6, 16):  # hind legs
        g.rect(x, 5 + (a if x == 6 else b), x + 1, 8 + (a if x == 6 else b), "s")
    for x in (7, 15):  # forelegs below the mane
        g.rect(x, 18 + (b if x == 7 else a), x + 1, 22 + (b if x == 7 else a) - 1, "b")
    g.ellipse(12.0, 9.6, 4.6, 5.6, "b")
    mane_ring(g, 12.0, 14.6, 5.6, 5.0, 18)
    g.ellipse(12.0, 15.6, 3.2, 3.2, "b")
    g.shade("b", "h", "s")
    g.put(9, 11, "n")
    g.put(15, 13, "n")
    g.put(7, 17, "n")
    g.put(16, 19, "n")
    g.put(9, 12, "b")  # ears
    g.put(15, 12, "b")
    g.put(11, 15, "e")  # eyes
    g.put(13, 15, "e")
    g.rect(11, 17, 13, 18, "w")  # muzzle
    g.put(12, 17, "e")  # nose
    g.outline()
    return g


def stripes(g, x0, y0, x1, y1, along_x):
    # Bold orange and white hazard bands, three pixels each, leaning one
    # pixel per row (or column), so they still read at 1x.
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            k = (x + y) if along_x else (y + x)
            g.put(x, y, "o" if (k // 3) % 2 == 0 else "x")


def cone(g, cx, base_y, height=8):
    # A big traffic cone from the 3/4 angle: black foot, orange body with a
    # white band, tip on top.
    g.rect(cx - 3, base_y, cx + 3, base_y, "g")
    for i in range(1, height):
        half = max(0, 2 - (i * 3) // height)
        g.rect(cx - half, base_y - i, cx + half, base_y - i, "x" if i in (3, 4) else "o")


# Barricades fill a 16x16 cell. Along a horizontal edge the piece takes
# rows 3-12 (outline included) and runs the full width, so neighbours join
# into one solid line; along a vertical edge it takes columns 3-12 and the
# full height. The corner block takes the 10x10 square at rows and columns
# 3-12.


def barricade_sawhorse_h():
    g = Grid()
    for x in (3, 12):
        g.rect(x - 1, 9, x, 11, "g")
        g.put(x - 2, 11, "g")
        g.put(x + 1, 11, "g")
    stripes(g, 0, 4, 15, 8, True)
    g.rect(3, 3, 4, 3, "y")  # warning lamp
    g.outline()
    return g


def barricade_concrete_h():
    g = Grid()
    g.rect(0, 4, 15, 11, "c")
    g.rect(0, 4, 15, 4, "C")
    g.rect(0, 11, 15, 11, "d")
    stripes(g, 0, 6, 15, 9, True)  # painted hazard band
    g.rect(15, 4, 15, 11, "d")  # joint
    g.outline()
    return g


def barricade_cones_h():
    g = Grid()
    stripes(g, 0, 6, 15, 8, True)  # tape between the cones
    cone(g, 4, 11)
    cone(g, 12, 11)
    g.outline()
    return g


def barricade_sawhorse_v():
    g = Grid()
    for y in (2, 12):
        g.rect(4, y, 11, y + 1, "g")
    stripes(g, 5, 0, 10, 15, False)
    g.rect(11, 5, 11, 6, "y")
    g.outline()
    return g


def barricade_concrete_v():
    g = Grid()
    g.rect(4, 0, 11, 15, "c")
    g.rect(4, 0, 4, 15, "C")
    g.rect(11, 0, 11, 15, "d")
    stripes(g, 6, 0, 9, 15, False)
    g.rect(4, 15, 11, 15, "d")  # joint
    g.outline()
    return g


def barricade_cones_v():
    g = Grid()
    stripes(g, 6, 0, 9, 15, False)
    cone(g, 8, 7)
    cone(g, 8, 15)
    g.outline()
    return g


def barricade_corner():
    # A big concrete block with a hazard band for the corners.
    g = Grid()
    g.rect(4, 4, 11, 11, "c")
    g.rect(4, 4, 11, 4, "C")
    g.rect(4, 11, 11, 11, "d")
    stripes(g, 4, 6, 11, 9, True)
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
        barricade_corner(),
    ]
    for look, suffix in (("classic", ""), ("svga", "-svga")):
        palette = PALETTES[look]
        render(lion, palette).save(os.path.join(OUT, "lion" + suffix + ".png"))
        render(pieces, palette).save(os.path.join(OUT, "barricade" + suffix + ".png"))


if __name__ == "__main__":
    main()
