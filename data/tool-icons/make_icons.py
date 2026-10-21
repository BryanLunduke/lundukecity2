#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Original toolbar icons for Lunduke City 2. These are not traced from
# SimCity or any other game. Run this script to regenerate the PNGs.

import pathlib
import struct
import zlib

ROOT = pathlib.Path(__file__).resolve().parent
SIZE = 24

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
    # Blade.
    rect(im, 2, 8, 3, 11, "G")
    vline(im, 2, 8, 11, "W")
    vline(im, 4, 9, 9, "s")
    # Body.
    rect(im, 5, 11, 13, 6, "Y")
    hline(im, 5, 11, 13, "y")
    hline(im, 5, 15, 13, "d")
    hline(im, 5, 16, 13, "d")
    # Cab.
    rect(im, 11, 5, 7, 7, "Y")
    hline(im, 11, 5, 7, "y")
    rect(im, 12, 6, 5, 4, "u")
    hline(im, 12, 6, 5, "q")
    vline(im, 14, 6, 4, "U")
    # Exhaust and hydraulic arm.
    rect(im, 7, 8, 2, 3, "s")
    put(im, 7, 7, "N")
    hline(im, 6, 12, 4, "d")
    # Tracks and rollers.
    rect(im, 4, 17, 15, 5, "N")
    rect(im, 5, 18, 13, 3, "s")
    for x in (6, 9, 12, 15):
        rect(im, x, 18, 2, 3, "G")
        put(im, x, 19, "W")
    hline(im, 4, 21, 15, "B")


def road(im):
    rect(im, 2, 2, 20, 20, "E")
    rect(im, 3, 3, 18, 18, "e")
    # Shoulders.
    rect(im, 6, 2, 12, 20, "D")
    rect(im, 7, 2, 10, 20, "N")
    rect(im, 8, 2, 8, 20, "g")
    vline(im, 8, 2, 20, "s")
    vline(im, 15, 2, 20, "G")
    # Dashes.
    for y in (4, 9, 14, 19):
        rect(im, 11, y, 2, 3, "W")


def rail(im):
    rect(im, 2, 2, 20, 20, "D")
    rect(im, 3, 3, 18, 18, "H")
    for y in range(3, 21, 3):
        hline(im, 4, y, 16, "T")
        hline(im, 4, y + 1, 16, "t")
    vline(im, 7, 2, 20, "S")
    vline(im, 8, 2, 20, "W")
    vline(im, 15, 2, 20, "S")
    vline(im, 16, 2, 20, "N")
    # A small sleeper highlight so the ties stay readable.
    for y in (4, 10, 16):
        put(im, 5, y, "H")
        put(im, 18, y, "t")


def wire(im):
    # Wires sagging to either side of a lattice pylon.
    for x, y in ((2, 6), (3, 7), (4, 7), (5, 8), (6, 8), (7, 7), (8, 7)):
        put(im, x, y, "B")
        put(im, x, y + 1, "N")
    for x, y in ((15, 7), (16, 7), (17, 8), (18, 8), (19, 7), (20, 6), (21, 6)):
        put(im, x, y, "B")
        put(im, x, y + 1, "N")
    # Crossarm and insulators.
    rect(im, 5, 8, 14, 2, "T")
    hline(im, 5, 8, 14, "H")
    for x in (6, 11, 16):
        rect(im, x, 7, 2, 2, "Y")
        put(im, x, 7, "y")
    # Lattice mast.
    rect(im, 10, 9, 4, 12, "S")
    vline(im, 10, 9, 12, "W")
    vline(im, 13, 9, 12, "s")
    for y in (11, 14, 17):
        hline(im, 10, y, 4, "N")
    put(im, 11, 12, "B")
    put(im, 12, 13, "B")
    put(im, 11, 15, "B")
    put(im, 12, 16, "B")
    rect(im, 9, 20, 6, 2, "N")
    hline(im, 8, 21, 8, "s")


def park(im):
    rect(im, 1, 18, 22, 4, "E")
    hline(im, 1, 18, 22, "e")
    hline(im, 2, 21, 20, "f")
    # Trunk.
    rect(im, 11, 13, 3, 7, "T")
    vline(im, 11, 14, 6, "t")
    vline(im, 13, 14, 5, "H")
    # Canopy, layered.
    disc(im, 12, 10, 8, 7, "E")
    disc(im, 11, 9, 6, 5, "e")
    disc(im, 9, 8, 3, 2, "L")
    disc(im, 15, 12, 3, 2, "f")
    # A second, smaller tree so the icon reads as a park.
    rect(im, 5, 15, 2, 4, "t")
    disc(im, 6, 13, 3, 3, "E")
    disc(im, 5, 12, 2, 2, "e")


def _badge(im, x, y, fill, hi, letter):
    rect(im, x, y, 8, 8, fill)
    hline(im, x, y, 8, hi)
    vline(im, x, y, 8, hi)
    # 5x6 letters, white.
    glyphs = {
        "R": ("WWW..", "W..W.", "WWW..", "W.W..", "W..W.", "W..W."),
        "C": (".WWW.", "W....", "W....", "W....", "W....", ".WWW."),
        "I": ("WWWWW", "..W..", "..W..", "..W..", "..W..", "WWWWW"),
    }
    glyph = glyphs[letter]
    for row, bits in enumerate(glyph):
        for col, bit in enumerate(bits):
            if bit == "W":
                put(im, x + 1 + col, y + 1 + row, "W")


def residential(im):
    rect(im, 2, 16, 20, 6, "e")
    hline(im, 2, 20, 20, "f")
    # Walls and door.
    rect(im, 6, 11, 12, 8, "W")
    vline(im, 6, 11, 8, "w")
    rect(im, 10, 14, 4, 5, "T")
    vline(im, 10, 14, 5, "t")
    rect(im, 7, 13, 2, 2, "u")
    rect(im, 15, 13, 2, 2, "u")
    # Roof.
    for i, w in enumerate(range(4, 18, 2)):
        hline(im, 14 - w // 2, 6 + i, w, "R" if i % 2 == 0 else "r")
    hline(im, 7, 10, 10, "n")
    rect(im, 13, 4, 2, 4, "T")
    put(im, 13, 3, "t")
    _badge(im, 15, 2, "E", "e", "R")


def commercial(im):
    rect(im, 3, 9, 18, 12, "U")
    hline(im, 3, 9, 18, "u")
    hline(im, 3, 19, 18, "b")
    # Awning stripes.
    rect(im, 3, 6, 18, 4, "W")
    for x in range(3, 21, 3):
        rect(im, x, 6, 2, 4, "R")
    hline(im, 3, 6, 18, "r")
    # Windows and door.
    rect(im, 5, 12, 4, 4, "q")
    rect(im, 10, 12, 4, 4, "q")
    hline(im, 5, 14, 4, "W")
    hline(im, 10, 14, 4, "W")
    rect(im, 15, 13, 4, 8, "H")
    vline(im, 15, 13, 8, "T")
    put(im, 18, 16, "A")
    _badge(im, 15, 1, "U", "u", "C")


def industrial(im):
    rect(im, 2, 12, 14, 9, "H")
    hline(im, 2, 12, 14, "D")
    hline(im, 2, 19, 14, "t")
    rect(im, 4, 14, 3, 3, "u")
    rect(im, 8, 14, 3, 3, "N")
    rect(im, 12, 15, 3, 6, "T")
    # Sawtooth roof.
    for x in (2, 6, 10):
        put(im, x, 11, "s")
        put(im, x + 1, 10, "S")
        put(im, x + 2, 11, "N")
        put(im, x + 3, 12, "s")
    # Stack and smoke.
    rect(im, 16, 8, 4, 13, "N")
    vline(im, 16, 8, 13, "G")
    vline(im, 19, 8, 13, "B")
    rect(im, 16, 6, 4, 2, "s")
    disc(im, 18, 4, 3, 2, "M")
    disc(im, 16, 3, 2, 2, "F")
    put(im, 14, 2, "W")
    _badge(im, 1, 2, "O", "o", "I")


def police(im):
    # Shield.
    rect(im, 6, 3, 12, 4, "U")
    rect(im, 5, 6, 14, 8, "U")
    rect(im, 6, 14, 12, 3, "U")
    rect(im, 8, 17, 8, 2, "U")
    rect(im, 10, 19, 4, 2, "b")
    hline(im, 6, 3, 12, "u")
    # Gold rim and star.
    rect(im, 8, 5, 8, 12, "A")
    rect(im, 9, 6, 6, 10, "U")
    hline(im, 8, 5, 8, "a")
    for x, y in (
        (11, 7),
        (12, 7),
        (10, 8),
        (11, 8),
        (12, 8),
        (13, 8),
        (9, 9),
        (10, 9),
        (11, 9),
        (12, 9),
        (13, 9),
        (14, 9),
        (10, 10),
        (11, 10),
        (12, 10),
        (13, 10),
        (11, 11),
        (12, 11),
        (10, 12),
        (11, 12),
        (12, 12),
        (13, 12),
        (9, 13),
        (10, 13),
        (13, 13),
        (14, 13),
        (8, 14),
        (15, 14),
    ):
        put(im, x, y, "a")
    put(im, 11, 9, "W")
    put(im, 12, 9, "Y")


def fire(im):
    # Cab.
    rect(im, 3, 9, 6, 7, "R")
    hline(im, 3, 9, 6, "r")
    rect(im, 4, 10, 4, 3, "q")
    hline(im, 4, 10, 4, "W")
    # Body and hose bed.
    rect(im, 9, 8, 11, 8, "R")
    hline(im, 9, 8, 11, "r")
    hline(im, 9, 14, 11, "n")
    rect(im, 11, 10, 7, 2, "W")
    hline(im, 11, 11, 7, "S")
    # Ladder.
    rect(im, 10, 6, 9, 2, "A")
    hline(im, 10, 6, 9, "a")
    for x in (12, 14, 16):
        put(im, x, 6, "T")
        put(im, x, 7, "T")
    # Light and bumper.
    rect(im, 3, 7, 2, 2, "o")
    put(im, 3, 7, "y")
    rect(im, 2, 14, 2, 2, "S")
    # Wheels.
    disc(im, 7, 18, 3, 3, "B")
    disc(im, 7, 18, 1, 1, "G")
    disc(im, 16, 18, 3, 3, "B")
    disc(im, 16, 18, 1, 1, "G")
    hline(im, 4, 16, 14, "n")


def stadium(im):
    disc(im, 12, 13, 10, 8, "G")
    disc(im, 12, 13, 9, 7, "N")
    disc(im, 12, 13, 8, 6, "s")
    disc(im, 12, 14, 6, 4, "E")
    disc(im, 12, 14, 5, 3, "e")
    hline(im, 9, 14, 6, "W")
    vline(im, 12, 12, 5, "W")
    # Lights.
    rect(im, 3, 3, 2, 5, "s")
    rect(im, 19, 3, 2, 5, "s")
    hline(im, 2, 3, 4, "Y")
    hline(im, 18, 3, 4, "y")
    # Seats in the bowl.
    for y, color in ((8, "p"), (9, "P"), (17, "P"), (18, "p")):
        hline(im, 8, y, 8, color)


def seaport(im):
    rect(im, 1, 14, 22, 8, "Q")
    hline(im, 1, 14, 22, "q")
    hline(im, 2, 16, 18, "u")
    hline(im, 4, 19, 16, "b")
    # Hull.
    rect(im, 4, 11, 14, 5, "N")
    hline(im, 5, 11, 12, "G")
    hline(im, 4, 15, 14, "B")
    put(im, 3, 13, "s")
    put(im, 18, 13, "s")
    # Containers.
    rect(im, 6, 8, 4, 3, "R")
    rect(im, 10, 8, 4, 3, "U")
    hline(im, 6, 8, 4, "r")
    hline(im, 10, 8, 4, "u")
    # Cabin and funnel.
    rect(im, 14, 6, 4, 5, "W")
    rect(im, 15, 7, 2, 2, "u")
    rect(im, 15, 4, 2, 2, "O")
    put(im, 15, 3, "M")
    put(im, 16, 2, "F")
    # Dock piles.
    vline(im, 20, 10, 8, "T")
    vline(im, 22, 9, 9, "t")
    hline(im, 19, 10, 4, "H")


def coal(im):
    rect(im, 2, 13, 13, 8, "N")
    hline(im, 2, 13, 13, "g")
    hline(im, 2, 19, 13, "B")
    rect(im, 4, 15, 3, 3, "o")
    rect(im, 8, 15, 3, 3, "Y")
    put(im, 5, 16, "y")
    # Stack.
    rect(im, 15, 6, 4, 15, "s")
    vline(im, 15, 6, 15, "G")
    vline(im, 18, 6, 15, "B")
    rect(im, 15, 4, 4, 2, "N")
    # Smoke.
    disc(im, 17, 3, 3, 2, "M")
    put(im, 14, 2, "F")
    put(im, 19, 1, "W")
    # Coal heap.
    for i, w in enumerate((3, 5, 7, 9)):
        hline(im, 3, 20 - i, w, "B" if i % 2 == 0 else "N")
    put(im, 5, 18, "G")
    put(im, 7, 19, "s")


def nuclear(im):
    # Hyperbolic cooling tower.
    for y, half in enumerate((3, 4, 5, 5, 5, 4, 4, 4, 5, 5, 6, 6, 7, 7, 8)):
        color = "c" if y < 3 else "C" if y < 10 else "Z"
        hline(im, 12 - half, 4 + y, half * 2, color)
        if y % 3 == 0:
            put(im, 12 - half + 1, 4 + y, "W")
    hline(im, 4, 18, 16, "s")
    rect(im, 5, 19, 14, 2, "N")
    # Steam.
    disc(im, 12, 3, 3, 2, "F")
    put(im, 9, 2, "W")
    put(im, 15, 1, "W")
    # A yellow band, not a copied radiation glyph.
    hline(im, 7, 11, 10, "Y")
    hline(im, 8, 12, 8, "A")


def airport(im):
    # Runway.
    rect(im, 2, 15, 20, 6, "g")
    hline(im, 2, 15, 20, "G")
    hline(im, 2, 20, 20, "N")
    for x in (4, 8, 12, 16):
        rect(im, x, 17, 2, 2, "Y")
    vline(im, 12, 15, 6, "W")
    # Fuselage.
    rect(im, 10, 3, 4, 14, "W")
    vline(im, 10, 4, 12, "w")
    vline(im, 13, 4, 12, "G")
    disc(im, 12, 4, 2, 2, "W")
    rect(im, 11, 5, 2, 2, "u")
    # Wings and tail.
    rect(im, 3, 8, 18, 3, "W")
    hline(im, 3, 8, 18, "q")
    hline(im, 4, 10, 16, "G")
    rect(im, 6, 9, 3, 1, "U")
    rect(im, 15, 9, 3, 1, "U")
    rect(im, 8, 15, 8, 2, "W")
    hline(im, 8, 16, 8, "s")
    rect(im, 11, 2, 2, 2, "R")


def query(im):
    disc(im, 10, 10, 7, 7, "S")
    disc(im, 10, 10, 6, 6, "N")
    disc(im, 10, 10, 4, 4, "q")
    disc(im, 10, 10, 3, 3, "W")
    # Glint.
    put(im, 8, 8, "u")
    put(im, 9, 8, "W")
    put(im, 8, 9, "W")
    # Handle.
    for i in range(7):
        put(im, 15 + i, 15 + i, "T")
        put(im, 16 + i, 15 + i, "H")
        put(im, 15 + i, 16 + i, "t")
    put(im, 21, 21, "A")
    put(im, 20, 21, "Y")


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
