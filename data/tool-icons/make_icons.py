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
    # Horizontal roadway. Same band as the rails: no grass, no scenery.
    rect(im, 0, 12, 40, 16, "g")
    rect(im, 0, 14, 40, 12, "N")
    hline(im, 0, 14, 40, "G")
    hline(im, 0, 25, 40, "B")
    for x in (2, 12, 22, 32):
        rect(im, x, 18, 6, 3, "Y")
        hline(im, x, 18, 6, "y")


def rail(im):
    # Horizontal track, same thickness as the road. Ties, two rails, no train.
    for x in range(0, 40, 4):
        rect(im, x, 12, 2, 16, "T")
        vline(im, x, 12, 16, "H")
    rect(im, 0, 15, 40, 3, "N")
    hline(im, 0, 15, 40, "G")
    rect(im, 0, 22, 40, 3, "N")
    hline(im, 0, 24, 40, "s")


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
    "D": (".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."),
    "F": ("#####", "#....", "#....", "####.", "#....", "#....", "#...."),
}


def glyph2x(im, x, y, letter, color):
    for row, bits in enumerate(GLYPHS[letter]):
        for col, bit in enumerate(bits):
            if bit == "#":
                rect(im, x + col * 2, y + row * 2, 2, 2, color)


def badge(im, fill, hi, shade, word):
    spans = (11, 14, 16, 17, 18, 18, 18, 18, 18, 18, 18, 17, 17, 16, 15, 13, 11, 9, 7, 5, 3)
    top = 8
    for i, half in enumerate(spans):
        hline(im, 20 - half, top + i, half * 2, fill)
        put(im, 20 - half, top + i, hi)
        put(im, 19 + half, top + i, shade)
    glyph2x(im, 8, 11, word[0], "W")
    glyph2x(im, 22, 11, word[1], "W")


def police(im):
    badge(im, "U", "u", "b", "PD")


def fire(im):
    badge(im, "R", "r", "n", "FD")


def stadium(im):
    # Football helmet: shell, center stripe, ear hole, facemask.
    disc(im, 16, 20, 14, 15, "W")
    disc(im, 16, 20, 13, 14, "U")
    disc(im, 15, 19, 11, 12, "u")
    rect(im, 14, 6, 4, 20, "W")
    hline(im, 14, 6, 4, "R")
    hline(im, 14, 8, 4, "R")
    disc(im, 8, 20, 3, 4, "b")
    disc(im, 8, 20, 1, 2, "N")
    for y in (16, 20, 24):
        hline(im, 22, y, 10, "G")
        hline(im, 22, y + 1, 10, "W")
    vline(im, 31, 16, 10, "G")
    vline(im, 32, 16, 10, "W")
    hline(im, 26, 15, 6, "s")
    hline(im, 26, 26, 6, "s")


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
    # Shared plant building. Coal and nuclear add only the stack on the right.
    rect(im, 2, 20, 18, 14, "H")
    hline(im, 2, 20, 18, "D")
    hline(im, 2, 33, 18, "t")
    rect(im, 4, 23, 4, 4, "o")
    rect(im, 11, 23, 4, 4, "Y")
    rect(im, 7, 28, 5, 6, "T")
    vline(im, 7, 28, 6, "t")
    for i, width in enumerate((18, 14, 10, 6)):
        hline(im, 2 + (18 - width) // 2, 16 + i, width, "R" if i % 2 == 0 else "n")
    for x, y in ((15, 24), (14, 25), (14, 26), (13, 27), (13, 28), (14, 28)):
        put(im, x, y, "y")


def coal(im):
    power_hall(im)
    rect(im, 27, 10, 6, 24, "s")
    vline(im, 27, 10, 24, "G")
    vline(im, 32, 10, 24, "B")
    rect(im, 26, 8, 8, 3, "N")
    rect(im, 28, 4, 5, 4, "M")
    rect(im, 31, 1, 5, 4, "F")
    rect(im, 24, 3, 4, 3, "W")
    for i, width in enumerate((4, 7, 10)):
        hline(im, 26, 36 - i, width, "B" if i % 2 == 0 else "N")


def nuclear(im):
    power_hall(im)
    halves = (6, 7, 6, 5, 4, 4, 4, 5, 5, 6, 7, 8, 8)
    for i, half in enumerate(halves):
        color = "c" if i < 2 else "C" if i < 9 else "Z"
        hline(im, 32 - half, 14 + i, half * 2, color)
        if i % 3 == 0:
            put(im, 32 - half + 2, 14 + i, "W")
    rect(im, 26, 32, 14, 4, "s")
    hline(im, 26, 35, 14, "N")
    rect(im, 28, 8, 6, 4, "F")
    rect(im, 32, 4, 5, 4, "W")
    rect(im, 26, 2, 4, 3, "M")


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
    rect(im, 1, 28, 38, 10, "e")
    rect(im, 6, 26, 28, 4, "G")
    rect(im, 8, 14, 24, 14, "W")
    hline(im, 8, 14, 24, "w")
    hline(im, 8, 27, 24, "s")
    for x in (10, 16, 22, 28):
        rect(im, x, 16, 3, 12, "g")
        vline(im, x, 16, 12, "W")
    rect(im, 17, 18, 6, 5, "u")
    for i, width in enumerate(range(8, 28, 3)):
        hline(im, 20 - width // 2, 8 + i, width, "U" if i % 2 == 0 else "b")
    rect(im, 16, 4, 8, 4, "H")
    hline(im, 16, 4, 8, "D")
    glyph2x(im, 15, 18, "F", "U")


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


if __name__ == "__main__":
    main()
