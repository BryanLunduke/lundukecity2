#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Original 48x48 map sprites for Lunduke City 2 special buildings.
# These are not traced from SimCity or any other game.

import pathlib
import struct
import zlib

ROOT = pathlib.Path(__file__).resolve().parent
SIZE = 48

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
    "D": (206, 170, 96),
    "M": (156, 156, 164),
    "F": (228, 228, 232),
    "P": (124, 44, 68),
    "C": (40, 132, 140),
    "c": (148, 214, 206),
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


def grass(im):
    rect(im, 0, 0, SIZE, SIZE, "E")
    for y in range(0, SIZE, 4):
        for x in range(0, SIZE, 5):
            put(im, x + (y % 3), y, "e")
            put(im, x + 2, y + 2, "f")


def tree(im, x, y):
    rect(im, x + 4, y + 10, 4, 10, "T")
    vline(im, x + 4, y + 10, 10, "t")
    rect(im, x, y + 6, 12, 6, "E")
    rect(im, x - 2, y + 2, 10, 6, "e")
    rect(im, x + 4, y, 10, 6, "E")
    rect(im, x + 2, y + 1, 4, 3, "L")
    rect(im, x + 8, y + 4, 4, 3, "f")


def zoo(im):
    grass(im)
    rect(im, 4, 30, 40, 6, "D")
    hline(im, 4, 32, 40, "T")
    # Ticket booth on the left.
    rect(im, 2, 18, 14, 16, "H")
    hline(im, 2, 18, 14, "D")
    hline(im, 2, 33, 14, "t")
    rect(im, 5, 21, 6, 5, "u")
    rect(im, 6, 28, 4, 6, "T")
    for i, width in enumerate((16, 12, 8)):
        hline(im, 1 + (16 - width) // 2, 14 + i, width, "R" if i % 2 == 0 else "n")
    # Fence and pen.
    for x in range(18, 46, 4):
        vline(im, x, 20, 12, "T")
        put(im, x, 20, "H")
    hline(im, 18, 20, 28, "H")
    hline(im, 18, 26, 28, "T")
    # Elephant.
    rect(im, 24, 28, 12, 8, "g")
    rect(im, 34, 26, 6, 6, "G")
    rect(im, 38, 30, 2, 6, "s")
    rect(im, 22, 26, 4, 5, "s")
    rect(im, 26, 34, 2, 4, "N")
    rect(im, 32, 34, 2, 4, "N")
    put(im, 36, 28, "B")
    tree(im, 4, 2)


def big_park(im):
    grass(im)
    # Path.
    rect(im, 0, 28, 20, 5, "D")
    rect(im, 16, 20, 5, 16, "D")
    rect(im, 16, 18, 22, 5, "D")
    hline(im, 0, 30, 20, "T")
    # Pond.
    rect(im, 28, 30, 16, 10, "Q")
    rect(im, 30, 32, 12, 6, "q")
    hline(im, 32, 34, 6, "W")
    tree(im, 2, 4)
    tree(im, 18, 2)
    tree(im, 34, 6)
    # Bench and flowers.
    rect(im, 8, 36, 8, 2, "T")
    vline(im, 9, 38, 3, "t")
    vline(im, 14, 38, 3, "t")
    rect(im, 6, 22, 3, 2, "R")
    rect(im, 10, 23, 3, 2, "Y")
    rect(im, 38, 24, 3, 2, "r")


def library(im):
    # Columned hall with a book. The old blue pediment read as a letter F.
    grass(im)
    tree(im, 0, 18)
    rect(im, 10, 40, 32, 4, "D")
    rect(im, 8, 37, 34, 4, "G")
    hline(im, 8, 37, 34, "W")
    rect(im, 10, 22, 28, 16, "w")
    hline(im, 10, 22, 28, "W")
    hline(im, 10, 37, 28, "s")
    for x in (12, 18, 30, 36):
        rect(im, x, 23, 3, 14, "W")
        vline(im, x, 23, 14, "G")
        vline(im, x + 2, 23, 14, "s")
    # Open book in the center bay, pages marked, spine in the middle.
    rect(im, 22, 24, 8, 7, "n")
    rect(im, 23, 25, 6, 5, "F")
    vline(im, 26, 25, 5, "U")
    hline(im, 23, 27, 6, "u")
    rect(im, 23, 32, 6, 5, "T")
    vline(im, 23, 32, 5, "t")
    # Flat lintel and a flat roof of the same width. Not a tapering banner.
    rect(im, 8, 18, 32, 4, "H")
    hline(im, 8, 18, 32, "D")
    hline(im, 8, 21, 32, "t")
    rect(im, 10, 13, 28, 5, "U")
    hline(im, 10, 13, 28, "u")
    hline(im, 10, 17, 28, "b")
    rect(im, 20, 8, 8, 5, "R")
    vline(im, 20, 8, 5, "y")
    hline(im, 21, 9, 6, "W")
    hline(im, 21, 10, 6, "w")
    hline(im, 21, 11, 6, "W")


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
                    nx, ny = x + dx, y + dy
                    if 0 <= nx < SIZE and 0 <= ny < SIZE and base[ny][nx] != ".":
                        im[y][x] = "K"
                        break
    return im


def write_png(path, rgba_rows):
    def chunk(tag, payload):
        body = tag + payload
        return struct.pack(">I", len(payload)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    raw = b"".join(b"\x00" + row for row in rgba_rows)
    ihdr = struct.pack(">IIBBBBB", SIZE, SIZE, 8, 6, 0, 0, 0)
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
    for name, draw in (("zoo", zoo), ("big-park", big_park), ("library", library)):
        write_png(ROOT / f"{name}.png", to_rgba(outlined(draw)))


if __name__ == "__main__":
    main()
