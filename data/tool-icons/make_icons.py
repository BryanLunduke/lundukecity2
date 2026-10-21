#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Original toolbar icons for Lunduke City 2. These are not traced from
# SimCity or any other game. Run this script to regenerate the PNGs.

import math
import pathlib
import struct
import zlib

ROOT = pathlib.Path(__file__).resolve().parent
SIZE = 40

# Several shades per hue, plus a dark outline. Indexed so the sprites stay
# crisp when the game scales them with nearest-neighbor.
PALETTE = {
    "K": (22, 16, 12),
    "Y": (236, 184, 32),
    "y": (255, 226, 120),
    "d": (176, 120, 16),
    "B": (28, 28, 32),
    "N": (52, 52, 58),
    "G": (176, 176, 182),
    "g": (112, 114, 122),
    "W": (248, 246, 240),
    "w": (214, 210, 200),
    "R": (196, 40, 34),
    "r": (236, 96, 64),
    "n": (120, 22, 20),
    "E": (40, 132, 48),
    "e": (92, 184, 68),
    "f": (18, 78, 28),
    "L": (168, 208, 72),
    "T": (132, 78, 36),
    "t": (84, 48, 22),
    "H": (176, 124, 64),
    "U": (36, 72, 168),
    "u": (96, 148, 220),
    "b": (18, 36, 96),
    "Q": (48, 112, 176),
    "q": (140, 196, 224),
    "S": (168, 176, 186),
    "s": (72, 78, 88),
    "O": (224, 112, 28),
    "o": (255, 176, 64),
    "A": (212, 164, 40),
    "a": (255, 220, 112),
    "C": (40, 132, 140),
    "c": (148, 214, 206),
    "Z": (20, 72, 78),
    "F": (228, 228, 232),
    "M": (156, 156, 164),
    "P": (124, 44, 68),
    "p": (184, 80, 100),
    "D": (206, 170, 96),
    "V": (64, 48, 36),
}


def blank():
    return [["."] * SIZE for _ in range(SIZE)]


def put(im, x, y, color):
    if 0 <= x < SIZE and 0 <= y < SIZE and color != ".":
        im[y][x] = color


def rect(im, x, y, w, h, color):
    for yy in range(y, y + h):
        for xx in range(x, x + w):
            put(im, xx, yy, color)


def hline(im, x, y, w, color):
    rect(im, x, y, w, 1, color)


def vline(im, x, y, h, color):
    rect(im, x, y, 1, h, color)


def disc(im, cx, cy, rx, ry, color):
    for y in range(cy - ry, cy + ry + 1):
        for x in range(cx - rx, cx + rx + 1):
            if rx > 0 and ry > 0:
                nx = (x - cx) / rx
                ny = (y - cy) / ry
                if nx * nx + ny * ny <= 1.0:
                    put(im, x, y, color)


def outlined(draw):
    im = blank()
    draw(im)
    base = [row[:] for row in im]
    for y in range(SIZE):
        for x in range(SIZE):
            if base[y][x] != ".":
                continue
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    yy, xx = y + dy, x + dx
                    if 0 <= yy < SIZE and 0 <= xx < SIZE and base[yy][xx] != ".":
                        im[y][x] = "K"
                        break
                else:
                    continue
                break
    return im


def bulldozer(im):
    # Blade, tall enough to read at the Deck's real pixel size.
    rect(im, 1, 6, 7, 24, "G")
    vline(im, 1, 6, 24, "W")
    vline(im, 7, 8, 20, "s")
    # Body.
    rect(im, 8, 14, 28, 14, "Y")
    hline(im, 8, 14, 28, "y")
    hline(im, 8, 26, 28, "d")
    hline(im, 8, 27, 28, "d")
    # Cab.
    rect(im, 22, 3, 15, 13, "Y")
    hline(im, 22, 3, 15, "y")
    rect(im, 24, 5, 11, 9, "u")
    hline(im, 24, 5, 11, "q")
    vline(im, 29, 5, 9, "U")
    # Exhaust and hydraulic arm.
    rect(im, 12, 7, 4, 8, "s")
    put(im, 13, 6, "N")
    hline(im, 9, 18, 10, "d")
    # Tracks and rollers.
    rect(im, 5, 28, 33, 10, "N")
    rect(im, 6, 29, 31, 8, "s")
    for x in (8, 16, 24, 31):
        rect(im, x, 30, 4, 5, "G")
        put(im, x + 1, 32, "W")
    hline(im, 5, 37, 33, "B")


def road(im):
    # Horizontal roadway, same band as the rails. The asphalt is 15 pixels
    # tall so a 1px centre line has the same asphalt above and below it.
    rect(im, 0, 11, 40, 17, "g")
    rect(im, 0, 12, 40, 15, "N")
    hline(im, 0, 12, 40, "G")
    hline(im, 0, 26, 40, "B")
    for x in (2, 12, 22, 32):
        rect(im, x, 19, 6, 1, "Y")


def rail(im):
    # Horizontal track, same thickness as the road. Ties, two rails, no train.
    for x in range(0, 40, 4):
        rect(im, x, 11, 2, 17, "T")
        vline(im, x, 11, 17, "H")
    rect(im, 0, 14, 40, 3, "N")
    hline(im, 0, 14, 40, "G")
    rect(im, 0, 23, 40, 3, "N")
    hline(im, 0, 25, 40, "s")


def wire(im):
    # Wooden pole, one crossarm, insulators, wires leaving both sides.
    sag = ((0, 10), (2, 12), (4, 14), (6, 15), (8, 16), (10, 16), (12, 15), (14, 13), (16, 11))
    for x, y in sag:
        put(im, x, y, "B")
        put(im, x, y + 1, "N")
        put(im, 39 - x, y, "B")
        put(im, 39 - x, y + 1, "N")
    rect(im, 4, 10, 32, 3, "T")
    hline(im, 4, 10, 32, "H")
    for x in (8, 18, 29):
        rect(im, x, 7, 3, 4, "W")
        hline(im, x, 7, 3, "Y")
        put(im, x + 1, 8, "y")
    rect(im, 18, 13, 4, 22, "T")
    vline(im, 18, 13, 22, "t")
    vline(im, 21, 13, 22, "H")
    rect(im, 14, 34, 12, 4, "t")
    hline(im, 14, 34, 12, "H")


def park(im):
    # Grass with an irregular canopy. The crown is stacked blocks, not a disc.
    rect(im, 1, 26, 38, 12, "E")
    hline(im, 1, 26, 38, "e")
    hline(im, 2, 36, 36, "f")
    rect(im, 18, 20, 4, 12, "T")
    vline(im, 18, 20, 12, "t")
    vline(im, 21, 20, 11, "H")
    rect(im, 8, 16, 20, 8, "E")
    rect(im, 5, 12, 14, 8, "e")
    rect(im, 18, 9, 14, 8, "E")
    rect(im, 11, 7, 8, 6, "L")
    rect(im, 24, 13, 8, 5, "f")
    rect(im, 14, 18, 10, 4, "e")
    rect(im, 9, 14, 4, 3, "L")
    put(im, 28, 11, "e")
    put(im, 7, 15, "E")


def _badge(im, x, y, fill, hi, letter):
    rect(im, x, y, 14, 14, fill)
    hline(im, x, y, 14, hi)
    vline(im, x, y, 14, hi)
    # 5x6 letters, doubled so they stay readable on the 40px sprite.
    glyphs = {
        "R": ("WWW..", "W..W.", "WWW..", "W.W..", "W..W.", "W..W."),
        "C": (".WWW.", "W....", "W....", "W....", "W....", ".WWW."),
        "I": ("WWWWW", "..W..", "..W..", "..W..", "..W..", "WWWWW"),
    }
    glyph = glyphs[letter]
    for row, bits in enumerate(glyph):
        for col, bit in enumerate(bits):
            if bit == "W":
                rect(im, x + 2 + col * 2, y + 1 + row * 2, 2, 2, "W")


def residential(im):
    rect(im, 2, 26, 36, 12, "e")
    hline(im, 2, 36, 36, "f")
    rect(im, 8, 18, 20, 14, "W")
    vline(im, 8, 18, 14, "w")
    rect(im, 15, 24, 6, 8, "T")
    vline(im, 15, 24, 8, "t")
    rect(im, 10, 21, 4, 4, "u")
    rect(im, 22, 21, 4, 4, "u")
    for i, w in enumerate(range(6, 28, 3)):
        hline(im, 18 - w // 2, 8 + i, w, "R" if i % 2 == 0 else "r")
    hline(im, 9, 17, 18, "n")
    rect(im, 20, 6, 3, 6, "T")
    put(im, 21, 5, "t")
    _badge(im, 24, 1, "E", "e", "R")


def commercial(im):
    rect(im, 3, 16, 30, 20, "U")
    hline(im, 3, 16, 30, "u")
    hline(im, 3, 34, 30, "b")
    rect(im, 3, 10, 30, 7, "W")
    for x in range(3, 33, 5):
        rect(im, x, 10, 3, 7, "R")
    hline(im, 3, 10, 30, "r")
    rect(im, 6, 20, 7, 7, "q")
    rect(im, 15, 20, 7, 7, "q")
    hline(im, 6, 23, 7, "W")
    hline(im, 15, 23, 7, "W")
    rect(im, 24, 22, 7, 14, "H")
    vline(im, 24, 22, 14, "T")
    put(im, 28, 28, "A")
    _badge(im, 24, 1, "U", "u", "C")


def industrial(im):
    rect(im, 2, 18, 24, 16, "H")
    hline(im, 2, 18, 24, "D")
    hline(im, 2, 32, 24, "t")
    rect(im, 5, 22, 5, 5, "u")
    rect(im, 12, 22, 5, 5, "N")
    rect(im, 19, 24, 5, 10, "T")
    for x in (2, 8, 14, 20):
        put(im, x, 17, "s")
        put(im, x + 1, 15, "S")
        put(im, x + 2, 14, "W")
        put(im, x + 3, 16, "N")
        put(im, x + 4, 18, "s")
    rect(im, 28, 12, 6, 22, "N")
    vline(im, 28, 12, 22, "G")
    vline(im, 33, 12, 22, "B")
    rect(im, 28, 9, 6, 3, "s")
    disc(im, 31, 6, 5, 3, "M")
    disc(im, 28, 4, 3, 2, "F")
    put(im, 24, 3, "W")
    _badge(im, 2, 2, "O", "o", "I")


GLYPHS = {
    "P": ("####.", "#...#", "#...#", "####.", "#....", "#....", "#...."),
    # Straight left stem, round right side. Cutting every corner made this
    # read as O at 1x.
    "D": ("####.", "#...#", "#...#", "#...#", "#...#", "#...#", "####."),
    "F": ("#####", "#....", "#....", "####.", "#....", "#....", "#...."),
}


def glyph2x(im, x, y, letter, color):
    for row, bits in enumerate(GLYPHS[letter]):
        for col, bit in enumerate(bits):
            if bit == "#":
                rect(im, x + col * 2, y + row * 2, 2, 2, color)


def _paint_shape(im, inside, fill, rim_hi, rim_mid, rim_lo):
    mask = [[inside(x, y) for x in range(SIZE)] for y in range(SIZE)]
    rim = [[0] * SIZE for _ in range(SIZE)]
    for y in range(SIZE):
        for x in range(SIZE):
            if not mask[y][x]:
                continue
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    nx, ny = x + dx, y + dy
                    if not (0 <= nx < SIZE and 0 <= ny < SIZE and mask[ny][nx]):
                        rim[y][x] = 1
                        break
    for y in range(SIZE):
        for x in range(SIZE):
            if not mask[y][x] or rim[y][x]:
                continue
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    nx, ny = x + dx, y + dy
                    if 0 <= nx < SIZE and 0 <= ny < SIZE and rim[ny][nx]:
                        rim[y][x] = 2
                        break
    for y in range(SIZE):
        for x in range(SIZE):
            if not mask[y][x]:
                continue
            if rim[y][x] == 1:
                put(im, x, y, rim_hi if (x + y) < 34 else rim_lo)
            elif rim[y][x] == 2:
                put(im, x, y, rim_mid)
            else:
                put(im, x, y, fill)


def _shield(x, y):
    # Police shield: broad shoulders, flat top, point at the bottom.
    if y < 3 or y > 36:
        return False
    cx = 19.5
    if y <= 18:
        half = 8 + (y - 3) * 2
        if half > 16:
            half = 16
        return abs(x + 0.5 - cx) <= half
    half = 16 * (36 - y) / 18.0
    return abs(x + 0.5 - cx) <= half


def _maltese(x, y):
    # Fire badge: four flared arms with a V notch in each tip.
    dx = abs(x + 0.5 - 19.5)
    dy = abs(y + 0.5 - 19.5)
    along = dx if dx >= dy else dy
    across = dy if dx >= dy else dx
    if along > 18.2:
        return False
    limit = 6.2 + along * 0.34
    if across > limit:
        return False
    if along > 13.5 and across < (along - 13.5) * 1.15:
        return False
    return True


def police(im):
    _paint_shape(im, _shield, "U", "W", "Y", "G")
    glyph2x(im, 8, 10, "P", "W")
    glyph2x(im, 22, 10, "D", "W")


def fire(im):
    _paint_shape(im, _maltese, "R", "y", "Y", "n")
    # Centre disc so FD sits on a flat field inside the cross.
    disc(im, 20, 19, 12, 9, "n")
    disc(im, 20, 19, 11, 8, "R")
    glyph2x(im, 8, 12, "F", "W")
    glyph2x(im, 22, 12, "D", "W")


def stadium(im):
    # Side profile, facemask to the right. The jaw and the cage keep it
    # from reading as a ball at 1x.
    shell = {
        4: (16, 22),
        5: (13, 25),
        6: (11, 27),
        7: (9, 28),
        8: (7, 29),
        9: (6, 29),
        10: (5, 28),
        11: (5, 26),
        12: (4, 24),
        13: (4, 23),
        14: (4, 22),
        15: (4, 22),
        16: (5, 22),
        17: (5, 22),
        18: (5, 22),
        19: (5, 23),
        20: (6, 24),
        21: (6, 25),
        22: (7, 26),
        23: (8, 27),
        24: (9, 27),
        25: (11, 26),
        26: (13, 24),
        27: (15, 22),
    }
    for y, (x0, x1) in shell.items():
        hline(im, x0, y, x1 - x0 + 1, "U")
        put(im, x0, y, "u")
        put(im, x1, y, "b")
    # Stripe over the crown, from the back of the shell to the brow.
    for y, x0, x1 in (
        (4, 17, 21),
        (5, 15, 23),
        (6, 13, 25),
        (7, 12, 26),
        (8, 14, 24),
        (9, 16, 22),
    ):
        hline(im, x0, y, x1 - x0 + 1, "W")
    # Ear hole.
    disc(im, 12, 17, 4, 4, "N")
    disc(im, 12, 17, 2, 2, "B")
    # Facemask: clips at the brow and the jaw, three bars, a vertical cage.
    rect(im, 26, 9, 4, 3, "G")
    rect(im, 24, 23, 4, 3, "G")
    for y in (11, 16, 21):
        rect(im, 28, y, 8, 2, "B")
        hline(im, 28, y, 8, "W")
    rect(im, 35, 11, 2, 12, "B")
    vline(im, 36, 11, 12, "W")


def seaport(im):
    rect(im, 1, 22, 38, 16, "Q")
    hline(im, 1, 22, 38, "q")
    hline(im, 2, 26, 34, "u")
    hline(im, 4, 32, 30, "b")
    rect(im, 4, 16, 24, 10, "N")
    hline(im, 6, 16, 20, "G")
    hline(im, 4, 25, 24, "B")
    put(im, 3, 20, "s")
    put(im, 28, 20, "s")
    rect(im, 6, 11, 7, 5, "R")
    rect(im, 14, 11, 7, 5, "U")
    hline(im, 6, 11, 7, "r")
    hline(im, 14, 11, 7, "u")
    rect(im, 22, 8, 7, 8, "W")
    rect(im, 24, 10, 3, 3, "u")
    rect(im, 24, 4, 3, 4, "O")
    disc(im, 25, 3, 3, 2, "M")
    put(im, 28, 2, "F")
    vline(im, 34, 14, 16, "T")
    vline(im, 37, 12, 18, "t")
    hline(im, 32, 14, 6, "H")


def power_hall(im):
    # Shared factory. Stacks are drawn to the right of x=21, so this hall
    # stays identical on the coal and nuclear icons.
    rect(im, 1, 20, 20, 16, "s")
    hline(im, 1, 20, 20, "G")
    hline(im, 1, 35, 20, "B")
    rect(im, 1, 16, 20, 4, "N")
    hline(im, 1, 16, 20, "G")
    hline(im, 1, 19, 20, "B")
    rect(im, 3, 23, 5, 4, "Y")
    hline(im, 3, 23, 5, "y")
    rect(im, 10, 23, 5, 4, "o")
    hline(im, 10, 23, 5, "Y")
    rect(im, 15, 27, 4, 8, "T")
    vline(im, 15, 27, 8, "t")
    put(im, 17, 30, "y")


def coal(im):
    power_hall(im)
    # Coal pile at the foot of the stack.
    for i, width in enumerate((12, 10, 8, 6)):
        hline(im, 22, 33 - i, width, "B" if i % 2 == 0 else "N")
    # Smokestack.
    rect(im, 28, 10, 6, 24, "g")
    vline(im, 28, 10, 24, "G")
    vline(im, 33, 10, 24, "B")
    rect(im, 27, 8, 8, 3, "N")
    hline(im, 27, 8, 8, "G")
    # Dark smoke, drifting up and to the right.
    rect(im, 30, 4, 6, 4, "N")
    rect(im, 33, 1, 5, 4, "B")
    rect(im, 24, 3, 5, 3, "s")
    put(im, 36, 0, "N")
    put(im, 23, 5, "B")


def nuclear(im):
    power_hall(im)
    # Cooling tower: wide lip, narrow waist, wide base. White steam on top.
    halves = (8, 7, 6, 5, 5, 4, 4, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8)
    for i, half in enumerate(halves):
        y = 10 + i
        color = "W" if i == 0 else "C" if i < 12 else "Z"
        hline(im, 31 - half, y, half * 2, color)
        put(im, 31 - half, y, "G")
        put(im, 30 + half, y, "s")
    hline(im, 23, 26, 16, "s")
    hline(im, 22, 27, 18, "N")
    rect(im, 26, 2, 9, 6, "W")
    rect(im, 32, 0, 6, 4, "F")
    rect(im, 23, 4, 5, 3, "W")
    # Radiation mark to the left of the tower base, still off the shared hall.
    for x, y in ((23, 22), (24, 21), (25, 22), (24, 23), (22, 23), (25, 24), (23, 24)):
        put(im, x, y, "Y")
    put(im, 24, 22, "B")


def airport(im):
    rect(im, 1, 1, 38, 38, "e")
    rect(im, 3, 26, 34, 10, "g")
    hline(im, 3, 26, 34, "G")
    hline(im, 3, 35, 34, "N")
    for x in (6, 13, 20, 27, 33):
        rect(im, x, 30, 3, 2, "Y")
    vline(im, 20, 26, 10, "W")
    # Airplane, seen from above.
    rect(im, 17, 4, 6, 20, "W")
    vline(im, 17, 6, 16, "w")
    vline(im, 22, 6, 16, "G")
    disc(im, 20, 5, 3, 3, "W")
    rect(im, 18, 6, 4, 3, "u")
    rect(im, 3, 10, 34, 6, "W")
    hline(im, 3, 10, 34, "q")
    hline(im, 4, 15, 32, "G")
    rect(im, 8, 12, 5, 3, "U")
    rect(im, 27, 12, 5, 3, "U")
    rect(im, 14, 20, 12, 4, "W")
    hline(im, 14, 23, 12, "s")
    rect(im, 18, 3, 4, 3, "R")


def query(im):
    disc(im, 15, 15, 12, 12, "S")
    disc(im, 15, 15, 10, 10, "N")
    disc(im, 15, 15, 7, 7, "q")
    disc(im, 15, 15, 5, 5, "W")
    put(im, 11, 11, "u")
    put(im, 12, 11, "W")
    put(im, 11, 12, "W")
    for i in range(12):
        put(im, 22 + i, 22 + i, "T")
        put(im, 23 + i, 22 + i, "H")
        put(im, 22 + i, 23 + i, "t")
    put(im, 34, 34, "A")
    put(im, 33, 34, "Y")


def _in_star(x, y):
    cx, cy = 19.5, 19.5
    outer, inner = 17.2, 8.4
    pts = []
    for i in range(5):
        outer_a = -math.pi / 2 + i * 2 * math.pi / 5
        inner_a = outer_a + math.pi / 5
        pts.append((cx + outer * math.cos(outer_a), cy + outer * math.sin(outer_a)))
        pts.append((cx + inner * math.cos(inner_a), cy + inner * math.sin(inner_a)))
    inside = False
    j = len(pts) - 1
    for i, (px, py) in enumerate(pts):
        qx, qy = pts[j]
        if ((py > y) != (qy > y)) and (x < (qx - px) * (y - py) / ((qy - py) or 1e-6) + px):
            inside = not inside
        j = i
    return inside


def special(im):
    for y in range(SIZE):
        for x in range(SIZE):
            if not _in_star(x + 0.5, y + 0.5):
                continue
            if y < 16 and x < 22:
                put(im, x, y, "y")
            elif y > 24:
                put(im, x, y, "d")
            else:
                put(im, x, y, "Y")


def zoo(im):
    rect(im, 1, 22, 38, 16, "E")
    hline(im, 1, 22, 38, "e")
    rect(im, 4, 24, 32, 10, "D")
    hline(im, 4, 28, 32, "T")
    rect(im, 6, 16, 10, 12, "H")
    hline(im, 6, 16, 10, "D")
    rect(im, 8, 18, 5, 4, "u")
    rect(im, 8, 24, 4, 4, "T")
    # Elephant: body, head, trunk, ear. Blocky, not a circle.
    rect(im, 18, 20, 12, 8, "g")
    rect(im, 28, 18, 6, 6, "G")
    rect(im, 30, 16, 3, 3, "S")
    vline(im, 33, 22, 6, "s")
    vline(im, 34, 24, 6, "N")
    rect(im, 16, 18, 4, 5, "s")
    rect(im, 20, 26, 2, 4, "N")
    rect(im, 26, 26, 2, 4, "N")
    rect(im, 22, 14, 6, 4, "e")
    rect(im, 20, 12, 4, 3, "L")


def bigpark(im):
    rect(im, 1, 24, 38, 14, "E")
    hline(im, 1, 24, 38, "e")
    rect(im, 6, 28, 28, 3, "D")
    rect(im, 8, 18, 4, 10, "T")
    rect(im, 26, 16, 4, 12, "T")
    rect(im, 2, 12, 14, 8, "E")
    rect(im, 4, 8, 10, 6, "e")
    rect(im, 10, 10, 6, 4, "L")
    rect(im, 22, 8, 14, 8, "E")
    rect(im, 24, 5, 8, 6, "e")
    rect(im, 30, 10, 6, 4, "f")
    rect(im, 18, 30, 4, 3, "T")
    rect(im, 14, 26, 3, 2, "R")
    rect(im, 32, 28, 3, 2, "Y")


def library(im):
    # Columned hall and a book. A triangular blue pediment read as the letter F.
    rect(im, 0, 32, 40, 7, "E")
    hline(im, 0, 32, 40, "e")
    for x in range(2, 38, 7):
        put(im, x, 35, "f")
        put(im, x + 3, 37, "L")
    rect(im, 5, 29, 30, 4, "G")
    hline(im, 5, 29, 30, "W")
    hline(im, 5, 32, 30, "s")
    rect(im, 7, 17, 26, 13, "w")
    hline(im, 7, 17, 26, "W")
    for x in (8, 15, 22, 29):
        rect(im, x, 18, 3, 12, "W")
        vline(im, x, 18, 12, "G")
        vline(im, x + 2, 18, 12, "s")
    # Open book between the middle columns.
    rect(im, 18, 19, 4, 6, "R")
    vline(im, 18, 19, 6, "y")
    hline(im, 19, 20, 3, "W")
    hline(im, 19, 22, 3, "w")
    hline(im, 19, 23, 3, "W")
    rect(im, 18, 26, 4, 4, "T")
    # Flat stone lintel and a flat blue roof. No tapering banner.
    rect(im, 5, 14, 30, 3, "H")
    hline(im, 5, 14, 30, "D")
    hline(im, 5, 16, 30, "t")
    rect(im, 7, 9, 26, 5, "U")
    hline(im, 7, 9, 26, "u")
    hline(im, 7, 13, 26, "b")
    # Closed book standing on the roof.
    rect(im, 16, 4, 8, 5, "n")
    vline(im, 16, 4, 5, "y")
    hline(im, 17, 5, 6, "W")
    hline(im, 17, 6, 6, "w")
    hline(im, 17, 7, 6, "W")


ICONS = [
    ("bulldozer", bulldozer),
    ("road", road),
    ("rail", rail),
    ("wire", wire),
    ("park", park),
    ("residential", residential),
    ("commercial", commercial),
    ("industrial", industrial),
    ("police", police),
    ("fire", fire),
    ("stadium", stadium),
    ("seaport", seaport),
    ("coal", coal),
    ("nuclear", nuclear),
    ("airport", airport),
    ("special", special),
    ("query", query),
    ("zoo", zoo),
    ("bigpark", bigpark),
    ("library", library),
]


def write_png(path, width, height, rgba_rows):
    def chunk(tag, payload):
        body = tag + payload
        return struct.pack(">I", len(payload)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    raw = b"".join(b"\x00" + row for row in rgba_rows)
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    path.write_bytes(png)


def to_rgba(im):
    rows = []
    for y in range(SIZE):
        row = bytearray()
        for x in range(SIZE):
            color = im[y][x]
            if color == ".":
                row += b"\x00\x00\x00\x00"
            else:
                r, g, b = PALETTE[color]
                row += bytes((r, g, b, 255))
        rows.append(bytes(row))
    return rows


def main():
    drawn = {}
    for name, draw in ICONS:
        im = outlined(draw)
        drawn[name] = im
        write_png(ROOT / f"{name}.png", SIZE, SIZE, to_rgba(im))
    coal_im = drawn["coal"]
    nuclear_im = drawn["nuclear"]
    for y in range(SIZE):
        for x in range(22):
            if coal_im[y][x] != nuclear_im[y][x]:
                raise SystemExit(f"power hall differs at {x},{y}: {coal_im[y][x]} vs {nuclear_im[y][x]}")
    check_road_and_rail(drawn["road"], drawn["rail"])


def check_road_and_rail(road, rail):
    """The yellow dashes sit on the exact middle row of the road band."""
    rows = sorted({y for y in range(SIZE) for x in range(SIZE) if road[y][x] == "Y"})
    if len(rows) not in (1, 2) or rows[-1] - rows[0] != len(rows) - 1:
        raise SystemExit(f"road centre line is not a 1-2px band: {rows}")
    column = next(x for x in range(SIZE) if road[rows[0]][x] == "Y")
    painted = [y for y in range(SIZE) if road[y][column] != "."]
    above = rows[0] - painted[0]
    below = painted[-1] - rows[-1]
    if above != below or above < 1:
        raise SystemExit(f"road centre line is off-centre: {above}px above, {below}px below")
    def band(im):
        ys = [y for y in range(SIZE) if any(im[y][x] != "." for x in range(SIZE))]
        return ys[0], ys[-1]

    road_top, road_bot = band(road)
    rail_top, rail_bot = band(rail)
    if road_bot - road_top != rail_bot - rail_top:
        raise SystemExit(
            f"road and rail bands differ: road {road_top}-{road_bot}, rail {rail_top}-{rail_bot}"
        )
    for name, im in (("road", road), ("rail", rail)):
        if not any(im[y][0] != "." for y in range(SIZE)) or not any(im[y][SIZE - 1] != "." for y in range(SIZE)):
            raise SystemExit(f"{name} does not run from the left edge to the right edge")


if __name__ == "__main__":
    main()
