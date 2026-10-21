#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Original toolbar icons for Lunduke City 2. These are not traced from
# SimCity or any other game. Run this script to regenerate the PNGs.

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
    rect(im, 2, 2, 36, 36, "E")
    rect(im, 4, 2, 32, 36, "e")
    rect(im, 10, 2, 20, 36, "D")
    rect(im, 12, 2, 16, 36, "N")
    rect(im, 14, 2, 12, 36, "g")
    vline(im, 14, 2, 36, "s")
    vline(im, 25, 2, 36, "G")
    for y in (4, 12, 20, 28):
        rect(im, 18, y, 4, 5, "W")


def rail(im):
    # Ballast and ties that run to the edges, with a gap of sky so the
    # ties read as a track rather than a crate.
    rect(im, 8, 1, 24, 38, "g")
    rect(im, 10, 1, 20, 38, "s")
    for y in range(1, 38, 4):
        rect(im, 2, y, 36, 2, "T")
        hline(im, 2, y, 36, "H")
    # Two rails.
    rect(im, 12, 0, 3, 40, "N")
    vline(im, 12, 0, 40, "W")
    rect(im, 25, 0, 3, 40, "N")
    vline(im, 27, 0, 40, "G")
    # A train car sitting on the rails. Rails stay visible past both ends.
    rect(im, 6, 12, 28, 16, "R")
    hline(im, 6, 12, 28, "r")
    hline(im, 6, 27, 28, "n")
    rect(im, 8, 14, 8, 4, "q")
    rect(im, 24, 14, 8, 4, "u")
    rect(im, 8, 22, 8, 4, "q")
    rect(im, 24, 22, 8, 4, "u")
    hline(im, 6, 19, 28, "Y")
    hline(im, 6, 20, 28, "A")
    # Wheels on the rails.
    for x in (13, 25):
        rect(im, x, 11, 3, 2, "B")
        rect(im, x, 27, 3, 2, "B")


def wire(im):
    # Cables sag from the crossarm out to both edges.
    left = ((1, 2), (3, 4), (5, 6), (7, 7), (9, 8), (11, 8), (13, 7), (15, 5))
    for x, y in left:
        put(im, x, y, "B")
        put(im, x, y + 1, "N")
        put(im, 39 - x, y, "B")
        put(im, 39 - x, y + 1, "N")
    # Crossarm with three insulators. The tower below is an open lattice,
    # so it reads as a pylon rather than a T, a lamp, or a solid wedge.
    rect(im, 2, 6, 36, 3, "T")
    hline(im, 2, 6, 36, "H")
    for x in (6, 18, 30):
        rect(im, x, 4, 4, 4, "Y")
        hline(im, x, 4, 4, "y")
    for i in range(24):
        y = 10 + i
        half = 4 + (i * 12) // 23
        left_x = 19 - half
        right_x = 20 + half
        put(im, left_x, y, "W")
        put(im, left_x + 1, y, "S")
        put(im, right_x, y, "s")
        put(im, right_x - 1, y, "N")
        span = max(1, right_x - left_x)
        put(im, left_x + (i * span) // 23, y, "N")
        put(im, right_x - (i * span) // 23, y, "B")
    for y, half in ((16, 7), (24, 11), (32, 14)):
        hline(im, 19 - half, y, half * 2 + 2, "s")
    rect(im, 3, 34, 34, 4, "N")
    hline(im, 2, 37, 36, "s")


def park(im):
    rect(im, 1, 28, 38, 10, "E")
    hline(im, 1, 28, 38, "e")
    hline(im, 2, 36, 36, "f")
    rect(im, 17, 20, 5, 12, "T")
    vline(im, 17, 21, 10, "t")
    vline(im, 21, 21, 9, "H")
    disc(im, 20, 16, 14, 12, "E")
    disc(im, 18, 14, 10, 8, "e")
    disc(im, 14, 12, 5, 4, "L")
    disc(im, 26, 18, 5, 4, "f")
    rect(im, 6, 24, 3, 6, "t")
    disc(im, 7, 21, 5, 5, "E")
    disc(im, 6, 20, 3, 3, "e")


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


def police(im):
    rect(im, 6, 2, 28, 7, "U")
    rect(im, 3, 8, 34, 16, "U")
    rect(im, 6, 24, 28, 7, "U")
    rect(im, 10, 31, 20, 4, "U")
    rect(im, 15, 35, 10, 4, "b")
    hline(im, 6, 2, 28, "u")
    rect(im, 10, 6, 20, 22, "A")
    rect(im, 13, 9, 14, 16, "U")
    hline(im, 10, 6, 20, "a")
    # Six-point star, built as a diamond plus a bar so it reads as a badge.
    for y, half in ((13, 2), (14, 4), (15, 6), (16, 9), (17, 6), (18, 4), (19, 2)):
        hline(im, 20 - half, y, half * 2, "a")
    for x, half in ((15, 2), (16, 3), (17, 2)):
        vline(im, x, 16 - half, half * 2, "a")
        vline(im, 40 - x, 16 - half, half * 2, "a")
    hline(im, 13, 16, 14, "Y")
    put(im, 19, 16, "W")
    put(im, 20, 16, "W")


def fire(im):
    rect(im, 2, 10, 12, 16, "R")
    hline(im, 2, 10, 12, "r")
    rect(im, 4, 13, 8, 6, "q")
    hline(im, 4, 13, 8, "W")
    rect(im, 14, 8, 22, 18, "R")
    hline(im, 14, 8, 22, "r")
    hline(im, 14, 24, 22, "n")
    rect(im, 17, 13, 16, 5, "W")
    hline(im, 17, 15, 16, "S")
    rect(im, 16, 3, 18, 5, "A")
    hline(im, 16, 3, 18, "a")
    for x in (19, 24, 29):
        vline(im, x, 3, 5, "T")
    rect(im, 2, 6, 5, 4, "o")
    put(im, 4, 7, "y")
    rect(im, 1, 24, 4, 4, "S")
    disc(im, 10, 33, 6, 6, "B")
    disc(im, 10, 33, 3, 3, "G")
    disc(im, 29, 33, 6, 6, "B")
    disc(im, 29, 33, 3, 3, "G")
    hline(im, 5, 26, 28, "n")


def stadium(im):
    # Bowl and stands. The field has yard lines, not a cross.
    disc(im, 20, 22, 16, 14, "N")
    disc(im, 20, 22, 15, 13, "P")
    disc(im, 20, 22, 13, 11, "p")
    disc(im, 20, 22, 11, 9, "s")
    disc(im, 20, 23, 8, 6, "E")
    disc(im, 20, 23, 7, 5, "e")
    for y in (20, 23, 26):
        hline(im, 15, y, 10, "W")
    disc(im, 20, 23, 3, 2, "L")
    hline(im, 18, 23, 4, "W")
    # Floodlights.
    rect(im, 3, 4, 3, 10, "s")
    rect(im, 34, 4, 3, 10, "s")
    hline(im, 2, 4, 5, "Y")
    hline(im, 2, 5, 5, "y")
    hline(im, 33, 4, 5, "Y")
    hline(im, 33, 5, 5, "y")


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


def coal(im):
    # Power hall, stack on the left, coal heap on the right, and a bolt.
    # The factory icon is a sawtooth shed with the stack on the right.
    rect(im, 14, 18, 22, 14, "N")
    hline(im, 14, 18, 22, "g")
    hline(im, 14, 30, 22, "B")
    rect(im, 17, 22, 4, 5, "o")
    rect(im, 23, 22, 4, 5, "Y")
    rect(im, 29, 22, 4, 5, "o")
    rect(im, 4, 10, 8, 24, "s")
    vline(im, 4, 10, 24, "G")
    vline(im, 11, 10, 24, "B")
    rect(im, 3, 7, 10, 3, "N")
    disc(im, 8, 5, 6, 3, "M")
    disc(im, 5, 3, 3, 2, "F")
    put(im, 12, 2, "W")
    for i, w in enumerate((5, 9, 13, 16)):
        hline(im, 20, 35 - i, w, "B" if i % 2 == 0 else "N")
    put(im, 24, 33, "G")
    # Lightning bolt on the hall.
    for x, y in ((24, 19), (23, 21), (24, 21), (22, 23), (23, 23), (21, 25), (22, 25), (23, 25)):
        put(im, x, y, "Y")
    put(im, 22, 21, "y")


def nuclear(im):
    # Hyperbolic cooling tower: wide lip, narrow waist, wide base.
    halves = (8, 10, 9, 7, 6, 5, 5, 5, 6, 6, 7, 8, 9, 10, 12, 13, 14)
    for i, half in enumerate(halves):
        color = "c" if i < 3 else "C" if i < 12 else "Z"
        hline(im, 24 - half, 11 + i, half * 2, color)
        if i % 4 == 0:
            put(im, 24 - half + 2, 11 + i, "W")
    hline(im, 6, 28, 32, "s")
    rect(im, 4, 29, 34, 8, "N")
    hline(im, 4, 36, 34, "s")
    disc(im, 24, 4, 7, 3, "F")
    disc(im, 19, 2, 4, 2, "W")
    put(im, 32, 1, "W")
    # Trefoil on a dark badge, beside the tower rather than a band.
    rect(im, 2, 16, 12, 14, "N")
    disc(im, 8, 19, 3, 3, "Y")
    disc(im, 5, 25, 3, 3, "Y")
    disc(im, 11, 25, 3, 3, "Y")
    disc(im, 8, 23, 2, 2, "N")


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
    ("query", query),
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
    for name, draw in ICONS:
        im = outlined(draw)
        write_png(ROOT / f"{name}.png", SIZE, SIZE, to_rgba(im))


if __name__ == "__main__":
    main()
