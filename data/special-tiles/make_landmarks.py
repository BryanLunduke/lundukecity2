#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Original civic landmarks for Lunduke City 2. Not traced from any game.
# Every colour is an exact VGA mode 13h entry (art/svga/vga_palette.py).

import math
import pathlib
import struct
import sys
import zlib

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / "art" / "svga"))
from vga_palette import vga_palette_8bit

ROOT = pathlib.Path(__file__).resolve().parent
ICON = pathlib.Path(__file__).resolve().parents[1] / "tool-icons"
PAL = set(vga_palette_8bit())

# Named VGA entries. Checked against the palette before anything is written.
C = {
    "black": (0, 0, 0),
    "ink": (20, 20, 20),
    "shadow": (32, 32, 32),
    "grey": (113, 113, 113),
    "stone": (182, 182, 182),
    "paper": (227, 227, 227),
    "white": (255, 255, 255),
    "red": (170, 0, 0),
    "brown": (170, 85, 0),
    "umber": (113, 56, 0),
    "tan": (113, 85, 56),
    "gold": (255, 190, 0),
    "grass": (0, 170, 0),
    "lawn": (0, 113, 0),
    "deep": (0, 65, 0),
    "moss": (32, 65, 32),
    "navy": (0, 0, 113),
    "blue": (0, 0, 170),
    "slate": (69, 69, 69),
    "ground": (56, 56, 56),
    "path": (170, 170, 170),
}


def check_palette():
    missing = [name for name, rgb in C.items() if rgb not in PAL]
    if missing:
        raise SystemExit("not in the VGA palette: " + ", ".join(missing))


def blank(size):
    return [[None] * size for _ in range(size)]


def put(im, x, y, rgb):
    size = len(im)
    if 0 <= x < size and 0 <= y < size and rgb is not None:
        im[y][x] = rgb


def rect(im, x, y, w, h, rgb):
    for yy in range(y, y + h):
        for xx in range(x, x + w):
            put(im, xx, yy, rgb)


def ground(im, look):
    size = len(im)
    if look == "spooky":
        base, spec = C["ground"], C["moss"]
    elif look == "svga":
        base, spec = C["lawn"], C["deep"]
    else:
        base, spec = C["grass"], C["lawn"]
    rect(im, 0, 0, size, size, base)
    for y in range(0, size, 4):
        for x in range(0, size, 5):
            put(im, (x + y) % size, y, spec)


def shadow_of(mask, dx, dy, color):
    size = len(mask)
    out = blank(size)
    for y in range(size):
        for x in range(size):
            if mask[y][x] is None:
                continue
            sx, sy = x + dx, y + dy
            if 0 <= sx < size and 0 <= sy < size:
                out[sy][sx] = color
    return out


def stamp(dest, src):
    size = len(dest)
    for y in range(size):
        for x in range(size):
            if src[y][x] is not None:
                dest[y][x] = src[y][x]


def box3q(im, x, y, w, depth, height, wall, side, roof):
    """Top-down 3/4 box. The roof is the large face. The east wall steps
    down-right at 45 degrees. Shadow is applied by the caller."""
    rect(im, x, y, w, depth, roof)
    rect(im, x, y + depth, w, height, wall)
    # East wall: each row is one pixel shorter, a 45-degree outer edge.
    span = depth + height
    for i in range(span):
        length = max(2, height - (i * height) // max(1, span))
        tone = side if i < depth else wall
        rect(im, x + w, y + i, length, 1, tone)
    # A one-pixel dark eave so the roof reads as a plane, not a front.
    rect(im, x, y + depth - 1, w, 1, C["ink"])


def wheel(im, cx, cy, radius, rim, spoke, cabin):
    for deg in range(0, 360, 6):
        rad = math.radians(deg)
        # Squash the circle so it sits on the ground plane.
        px = int(round(cx + math.cos(rad) * radius))
        py = int(round(cy + math.sin(rad) * radius * 0.62))
        put(im, px, py, rim)
        put(im, px, py + 1, rim)
    for deg in range(0, 360, 45):
        rad = math.radians(deg)
        for t in range(1, radius):
            px = int(round(cx + math.cos(rad) * t))
            py = int(round(cy + math.sin(rad) * t * 0.62))
            put(im, px, py, spoke)
    for deg in (200, 250, 300, 20, 70):
        rad = math.radians(deg)
        px = int(round(cx + math.cos(rad) * (radius - 1)))
        py = int(round(cy + math.sin(rad) * (radius - 1) * 0.62))
        rect(im, px - 1, py, 3, 2, cabin)


def mayor(im, look):
    if look == "spooky":
        wall, side, roof, door, trim = C["tan"], C["umber"], C["brown"], C["ink"], C["gold"]
    elif look == "svga":
        wall, side, roof, door, trim = C["stone"], C["grey"], C["deep"], C["ink"], C["paper"]
    else:
        wall, side, roof, door, trim = C["paper"], C["stone"], C["red"], C["ink"], C["white"]
    box3q(im, 6, 6, 28, 16, 8, wall, side, roof)
    rect(im, 16, 24, 6, 6, door)
    rect(im, 10, 10, 5, 4, trim)
    rect(im, 24, 10, 5, 4, trim)
    rect(im, 18, 4, 3, 4, C["grey"])
    rect(im, 14, 30, 12, 2, C["path"])


def casino(im, look):
    if look == "spooky":
        wall, side, roof, lamp = C["umber"], C["tan"], C["brown"], C["gold"]
    elif look == "svga":
        wall, side, roof, lamp = C["slate"], C["grey"], C["navy"], C["gold"]
    else:
        wall, side, roof, lamp = C["red"], C["grey"], C["brown"], C["gold"]
    box3q(im, 4, 6, 32, 16, 8, wall, side, roof)
    # Marquee sits on the roof plane. Diamonds, not a neon strip.
    for i, px in enumerate(range(8, 32, 4)):
        put(im, px, 10, lamp if i % 2 == 0 else C["paper"])
        put(im, px + 1, 11, lamp if i % 2 else C["paper"])
    rect(im, 16, 24, 6, 6, C["ink"])
    rect(im, 8, 12, 4, 3, C["paper"])
    rect(im, 26, 12, 4, 3, C["paper"])


def amusement(im, look):
    if look == "spooky":
        wall, side, roof, rim = C["tan"], C["umber"], C["brown"], C["gold"]
    elif look == "svga":
        wall, side, roof, rim = C["stone"], C["grey"], C["red"], C["paper"]
    else:
        wall, side, roof, rim = C["paper"], C["path"], C["blue"], C["ink"]
    box3q(im, 2, 18, 16, 10, 6, wall, side, roof)
    rect(im, 6, 24, 4, 5, C["ink"])
    wheel(im, 32, 16, 12, rim, C["grey"], C["red"])
    rect(im, 31, 22, 2, 12, C["grey"])


def compose(draw, look, size):
    im = blank(size)
    ground(im, look)
    building = blank(size)
    draw(building, look)
    shade = shadow_of(building, 4, 3, C["ink"])
    # Shadow only on open ground, same down-right direction as the hospital.
    for y in range(size):
        for x in range(size):
            if shade[y][x] is not None and building[y][x] is None:
                im[y][x] = shade[y][x]
    stamp(im, building)
    return im


def icon_crop(src):
    # 40px toolbar icon: drop the outer grass margin of the 48px sheet.
    out = blank(40)
    for y in range(40):
        for x in range(40):
            out[y][x] = src[y + 4][x + 4]
    return out


def write_png(path, im):
    size = len(im)
    def chunk(tag, payload):
        body = tag + payload
        return struct.pack(">I", len(payload)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    raw = b""
    for y in range(size):
        row = b"\x00"
        for x in range(size):
            rgb = im[y][x] or (0, 0, 0)
            if rgb not in PAL:
                raise SystemExit(f"{path} pixel {x},{y} {rgb} is off-palette")
            row += bytes((rgb[0], rgb[1], rgb[2], 255))
        raw += row
    ihdr = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    path.write_bytes(png)


def main():
    check_palette()
    sheets = (
        ("mayor", mayor),
        ("casino", casino),
        ("amusement", amusement),
    )
    looks = (("classic", ""), ("svga", "-svga"), ("spooky", "-spooky"))
    for stem, draw in sheets:
        for look, suffix in looks:
            image = compose(draw, look, 48)
            write_png(ROOT / f"{stem}{suffix}.png", image)
            write_png(ICON / f"{stem}{suffix}.png", icon_crop(image))


if __name__ == "__main__":
    main()
