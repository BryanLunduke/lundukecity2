#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Hand-placed civic landmarks.
# The Welcome screen uses data/welcome/welcome-bg.png (editor art), not this script.
# Original drawings. Every colour this script writes is an exact VGA mode 13h
# entry (the hand-drawn Classic sheets use the Micropolis Classic palette).
# Not a recolor of one mesh: each look has its own plan.

import pathlib
import struct
import sys
import zlib

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / "art" / "svga"))
from vga_palette import vga_palette_8bit

ROOT = pathlib.Path(__file__).resolve().parent
ICON = pathlib.Path(__file__).resolve().parents[1] / "tool-icons"
PAL = set(vga_palette_8bit())

# Exact VGA triples.
K = (20, 20, 20)
N = (32, 32, 32)
S = (56, 56, 56)
G = (113, 113, 113)
L = (170, 170, 170)
P = (203, 203, 203)
W = (227, 227, 227)
F = (255, 255, 255)
R = (170, 0, 0)
D = (113, 0, 0)
O = (170, 85, 0)
U = (113, 56, 0)
T = (113, 85, 56)
Y = (255, 190, 0)
E = (0, 170, 0)
M = (0, 113, 0)
Z = (0, 65, 0)
H = (32, 65, 32)
C = (56, 113, 56)
B = (0, 0, 170)
Q = (0, 0, 113)
A = (0, 0, 65)
V = (0, 113, 113)
X = (255, 235, 182)
J = (255, 219, 182)

for _rgb in (K, N, S, G, L, P, W, F, R, D, O, U, T, Y, E, M, Z, H, C, B, Q, A, V, X, J):
    if _rgb not in PAL:
        raise SystemExit(f"off palette {_rgb}")


def new(w, h, fill=None):
    return [[fill for _ in range(w)] for _ in range(h)]


def put(im, x, y, rgb):
    if rgb is None:
        return
    h = len(im)
    w = len(im[0])
    if 0 <= x < w and 0 <= y < h:
        im[y][x] = rgb


def rect(im, x, y, w, h, rgb):
    for yy in range(y, y + h):
        for xx in range(x, x + w):
            put(im, xx, yy, rgb)


def hline(im, x, y, w, rgb):
    rect(im, x, y, w, 1, rgb)


def vline(im, x, y, h, rgb):
    rect(im, x, y, 1, h, rgb)


def grass(im, a, b, spec):
    h = len(im)
    w = len(im[0])
    rect(im, 0, 0, w, h, a)
    for y in range(h):
        for x in range(w):
            if ((x * 3 + y * 5) % 11) == 0:
                put(im, x, y, spec)
            elif ((x + y * 2) % 17) == 0:
                put(im, x, y, b)


def shadow_under(im, mask, dx, dy, rgb):
    h = len(im)
    w = len(im[0])
    for y in range(h):
        for x in range(w):
            if not mask[y][x]:
                continue
            sx, sy = x + dx, y + dy
            if 0 <= sx < w and 0 <= sy < h and not mask[sy][sx]:
                # Keep the shadow off the building itself.
                im[sy][sx] = rgb


def mark(mask, x, y):
    h = len(mask)
    w = len(mask[0])
    if 0 <= x < w and 0 <= y < h:
        mask[y][x] = True


def mark_rect(mask, x, y, w, h):
    for yy in range(y, y + h):
        for xx in range(x, x + w):
            mark(mask, xx, yy)


def window(im, x, y, w, h, glass, muntin):
    rect(im, x, y, w, h, glass)
    hline(im, x, y + h // 2, w, muntin)
    vline(im, x + w // 2, y, h, muntin)
    hline(im, x, y, w, muntin)
    hline(im, x, y + h - 1, w, muntin)
    vline(im, x, y, h, muntin)
    vline(im, x + w - 1, y, h, muntin)


def door(im, x, y, w, h, wood, trim):
    rect(im, x, y, w, h, wood)
    vline(im, x, y, h, trim)
    vline(im, x + w - 1, y, h, trim)
    hline(im, x, y, w, trim)
    put(im, x + w - 2, y + h // 2, Y)


def chimney(im, x, y, h, brick, cap):
    rect(im, x, y, 3, h, brick)
    vline(im, x, y, h, K)
    hline(im, x - 1, y, 5, cap)


def tree(im, x, y, leaf, dark, trunk):
    rect(im, x + 3, y + 8, 2, 6, trunk)
    vline(im, x + 3, y + 8, 6, U)
    # Canopy, not a square.
    rect(im, x + 1, y + 3, 6, 6, leaf)
    rect(im, x, y + 4, 8, 3, leaf)
    rect(im, x + 2, y + 1, 4, 3, leaf)
    put(im, x + 2, y + 3, dark)
    put(im, x + 5, y + 5, dark)
    put(im, x + 3, y + 2, F if leaf == E else J)


def shrub(im, x, y, leaf, dark):
    rect(im, x, y + 1, 5, 3, leaf)
    rect(im, x + 1, y, 3, 2, dark)


def path_h(im, x, y, w, a, b):
    hline(im, x, y, w, a)
    hline(im, x, y + 1, w, b)
    hline(im, x, y + 2, w, a)
    for i in range(0, w, 4):
        put(im, x + i, y + 1, K)


def path_v(im, x, y, h, a, b):
    vline(im, x, y, h, a)
    vline(im, x + 1, y, h, b)
    vline(im, x + 2, y, h, a)
    for i in range(0, h, 4):
        put(im, x + 1, y + i, K)


def roof_plane(im, x, y, w, d, rgb, shade, shingle=True):
    rect(im, x, y, w, d, rgb)
    if shingle:
        for row in range(2, d, 3):
            hline(im, x, y + row, w, shade)
    hline(im, x, y, w, F if rgb != Q else L)
    hline(im, x, y + d - 1, w, K)


def side_wall(im, x, y, span, height, rgb):
    """East wall. Width grows down the roof, then holds, so the face
    reads at about 45 degrees instead of a one-pixel edge."""
    for i in range(span):
        if i < height:
            length = i + 2
        else:
            length = height + 1
        hline(im, x, y + i, length, rgb)
        put(im, x + length - 1, y + i, K)
        if i % 3 == 0:
            put(im, x, y + i, K)


def front_wall(im, x, y, w, h, rgb, shade):
    rect(im, x, y, w, h, rgb)
    vline(im, x, y, h, shade)
    hline(im, x, y + h - 1, w, K)


def estate_block(im, mask, x, y, w, d, h, wall, side, roof, rshade):
    roof_plane(im, x, y, w, d, roof, rshade)
    front_wall(im, x, y + d, w, h, wall, G)
    side_wall(im, x + w, y, d + h, 6, side)
    mark_rect(mask, x, y, w + 8, d + h)
    hline(im, x, y + d, w, K)


def fountain(im, x, y):
    rect(im, x, y + 2, 7, 4, B)
    rect(im, x + 1, y + 3, 5, 2, V)
    hline(im, x + 1, y + 3, 4, F)
    vline(im, x + 3, y, 3, L)
    put(im, x + 3, y, W)


def lamp(im, x, y, post, glow):
    vline(im, x, y + 2, 6, post)
    put(im, x, y + 1, glow)
    put(im, x - 1, y + 2, glow)
    put(im, x + 1, y + 2, glow)


def wheel(im, mask, cx, cy, radius, rim, spoke, cabin, squash):
    import math
    pts = []
    for deg in range(0, 360, 4):
        rad = math.radians(deg)
        px = int(round(cx + math.cos(rad) * radius))
        py = int(round(cy + math.sin(rad) * radius * squash))
        put(im, px, py, rim)
        put(im, px, py + 1, rim)
        mark(mask, px, py)
        pts.append((deg, px, py))
    for deg in range(0, 360, 30):
        rad = math.radians(deg)
        for t in range(1, radius):
            px = int(round(cx + math.cos(rad) * t))
            py = int(round(cy + math.sin(rad) * t * squash))
            put(im, px, py, spoke)
            mark(mask, px, py)
    for deg in (210, 250, 290, 330, 20, 60, 110, 150):
        rad = math.radians(deg)
        px = int(round(cx + math.cos(rad) * (radius - 1)))
        py = int(round(cy + math.sin(rad) * (radius - 1) * squash))
        rect(im, px - 1, py, 3, 2, cabin)
        mark_rect(mask, px - 1, py, 3, 2)
    # Hub and A-frame legs.
    rect(im, cx - 1, cy - 1, 3, 3, G)
    vline(im, cx, cy, 14, G)
    mark_rect(mask, cx - 1, cy - 1, 3, 16)


def tent(im, mask, x, y, w, h, cloth, stripe):
    # A peaked tent, roof planes meeting at the top.
    for i in range(h):
        inset = i // 2
        span = w - inset * 2
        if span < 2:
            break
        color = stripe if (i % 3 == 0) else cloth
        hline(im, x + inset, y + i, span, color)
        mark(mask, x + inset, y + i)
        mark(mask, x + inset + span - 1, y + i)
    rect(im, x + 2, y + h - 4, w - 4, 4, cloth)
    mark_rect(mask, x + 2, y + h - 4, w - 4, 4)
    vline(im, x + w // 2, y + h - 4, 4, K)


def apply_shadow(im, mask, rgb):
    shadow_under(im, mask, 4, 3, rgb)


def mayor_classic(im):
    mask = new(48, 48, False)
    grass(im, E, M, Z)
    path_h(im, 0, 40, 22, L, G)
    path_v(im, 18, 28, 14, L, G)
    shrub(im, 2, 36, M, Z)
    shrub(im, 40, 38, M, Z)
    tree(im, 1, 4, M, Z, U)
    tree(im, 38, 2, E, M, U)
    fountain(im, 4, 22)
    # Wings sit lower than the main roof so the house reads as an estate.
    estate_block(im, mask, 8, 16, 10, 6, 6, W, L, R, D)
    estate_block(im, mask, 30, 16, 10, 6, 6, W, L, R, D)
    estate_block(im, mask, 14, 8, 20, 10, 12, P, G, R, D)
    chimney(im, 28, 4, 6, T, K)
    window(im, 18, 12, 5, 4, B, W)
    window(im, 26, 12, 5, 4, B, W)
    window(im, 17, 22, 4, 5, B, F)
    window(im, 27, 22, 4, 5, B, F)
    door(im, 22, 22, 5, 8, U, O)
    rect(im, 20, 30, 9, 2, L)
    hline(im, 8, 44, 32, M)
    for x in range(8, 40, 4):
        vline(im, x, 42, 3, U)
    apply_shadow(im, mask, N)


def mayor_svga(im):
    mask = new(48, 48, False)
    grass(im, M, Z, H)
    # A drive approaches from the lower left. The house sits to the right
    # with a civic tower, a different plan from the classic wings.
    path_h(im, 0, 38, 16, T, U)
    path_v(im, 12, 26, 14, T, U)
    rect(im, 16, 34, 28, 6, G)
    hline(im, 16, 34, 28, L)
    tree(im, 0, 8, Z, H, U)
    tree(im, 2, 28, C, Z, U)
    shrub(im, 40, 30, C, Z)
    estate_block(im, mask, 18, 14, 22, 8, 10, W, G, Q, A)
    # Tower on the left of the house.
    estate_block(im, mask, 10, 6, 10, 8, 16, P, L, B, Q)
    window(im, 12, 10, 5, 4, Y, K)
    window(im, 12, 16, 5, 4, B, W)
    chimney(im, 34, 8, 7, T, K)
    window(im, 22, 16, 5, 4, B, F)
    window(im, 30, 16, 5, 4, B, F)
    window(im, 22, 24, 4, 4, B, F)
    window(im, 32, 24, 4, 4, B, F)
    door(im, 26, 24, 5, 8, U, O)
    rect(im, 24, 32, 9, 2, L)
    hline(im, 10, 4, 8, Y)
    vline(im, 14, 2, 4, G)
    apply_shadow(im, mask, K)


def mayor_spooky(im):
    mask = new(48, 48, False)
    grass(im, S, H, U)
    rect(im, 4, 40, 40, 4, T)
    hline(im, 4, 40, 40, U)
    path_v(im, 22, 30, 12, T, U)
    # Steep twin gables, brick, warm windows. A residence, not a ruin.
    tree(im, 1, 6, U, O, K)
    tree(im, 38, 8, O, U, K)
    shrub(im, 6, 34, U, O)
    estate_block(im, mask, 10, 18, 12, 8, 8, O, U, D, K)
    estate_block(im, mask, 26, 18, 12, 8, 8, O, U, D, K)
    estate_block(im, mask, 14, 8, 20, 12, 10, T, U, R, D)
    # Gable peaks above the wings.
    for i, ox in enumerate((12, 28)):
        for row in range(6):
            hline(im, ox + row, 12 + row, 10 - row * 2, D)
            mark(mask, ox + row, 12 + row)
    chimney(im, 22, 4, 6, U, K)
    window(im, 18, 14, 4, 4, Y, K)
    window(im, 26, 14, 4, 4, Y, K)
    window(im, 16, 24, 4, 4, Y, U)
    window(im, 30, 24, 4, 4, Y, U)
    door(im, 22, 22, 5, 8, K, O)
    rect(im, 18, 30, 12, 2, L)
    # Low garden wall.
    hline(im, 8, 36, 32, T)
    hline(im, 8, 37, 32, U)
    vline(im, 8, 34, 4, T)
    vline(im, 39, 34, 4, T)
    apply_shadow(im, mask, K)


def marquee(im, x, y, n, glow, gap=3):
    for i in range(n):
        put(im, x + i * gap, y, glow if i % 2 == 0 else F)
        put(im, x + i * gap, y + 1, O if i % 2 else Y)


def casino_classic(im):
    mask = new(48, 48, False)
    grass(im, E, M, Z)
    # Paved plaza.
    rect(im, 4, 32, 40, 12, L)
    for y in range(32, 44, 3):
        for x in range(4, 44, 4):
            put(im, x, y, G)
    rect(im, 16, 30, 16, 3, P)
    # Main hall plus a lower side salon so it is not one box.
    estate_block(im, mask, 6, 18, 10, 6, 8, R, D, O, U)
    estate_block(im, mask, 14, 8, 22, 10, 12, R, D, O, U)
    # Pediment on the roof plane.
    for row in range(5):
        hline(im, 20 + row, 4 + row, 12 - row * 2, Y)
        mark(mask, 20 + row, 4 + row)
    marquee(im, 16, 12, 7, Y)
    hline(im, 15, 14, 20, O)
    # Columns and entrance.
    for cx in (16, 30):
        vline(im, cx, 20, 10, F)
        vline(im, cx + 1, 20, 10, W)
        mark(mask, cx, 22)
    window(im, 20, 18, 4, 4, Y, K)
    window(im, 26, 18, 4, 4, Y, K)
    door(im, 22, 22, 6, 8, K, Y)
    rect(im, 8, 24, 4, 4, B)
    lamp(im, 8, 34, G, Y)
    lamp(im, 40, 34, G, Y)
    shrub(im, 4, 28, M, Z)
    shrub(im, 38, 28, M, Z)
    apply_shadow(im, mask, N)


def casino_svga(im):
    mask = new(48, 48, False)
    grass(im, Z, H, M)
    rect(im, 2, 34, 44, 12, S)
    for x in range(2, 46, 3):
        vline(im, x, 34, 12, G)
    hline(im, 2, 34, 44, L)
    # Canopy projects toward the viewer. The hall sits behind it.
    estate_block(im, mask, 10, 10, 28, 8, 10, G, S, Q, A)
    estate_block(im, mask, 8, 20, 32, 4, 6, B, Q, O, U)
    marquee(im, 12, 18, 9, F, 3)
    hline(im, 10, 20, 28, Y)
    window(im, 14, 12, 5, 4, V, F)
    window(im, 22, 12, 5, 4, V, F)
    window(im, 30, 12, 5, 4, V, F)
    door(im, 21, 24, 6, 8, K, Y)
    for cx in (16, 30):
        vline(im, cx, 24, 8, W)
    lamp(im, 6, 30, L, Y)
    lamp(im, 42, 30, L, Y)
    rect(im, 4, 26, 4, 3, C)
    rect(im, 40, 26, 4, 3, C)
    chimney(im, 34, 6, 5, G, K)
    apply_shadow(im, mask, K)


def casino_spooky(im):
    mask = new(48, 48, False)
    grass(im, H, S, U)
    rect(im, 6, 36, 36, 8, T)
    hline(im, 6, 36, 36, U)
    path_v(im, 22, 32, 8, O, U)
    estate_block(im, mask, 8, 14, 32, 10, 10, U, T, D, K)
    # Arched crown on the roof, gold trim, warm lanterns.
    for row in range(6):
        hline(im, 16 + row, 8 + row, 16 - row * 2, O)
        mark(mask, 16 + row, 8 + row)
    hline(im, 10, 16, 28, Y)
    marquee(im, 12, 18, 8, Y, 3)
    window(im, 12, 20, 5, 5, Y, K)
    window(im, 31, 20, 5, 5, Y, K)
    # Arch door.
    rect(im, 20, 22, 8, 8, K)
    hline(im, 21, 22, 6, O)
    hline(im, 22, 21, 4, O)
    put(im, 26, 26, Y)
    mark_rect(mask, 20, 21, 8, 9)
    lamp(im, 10, 32, U, Y)
    lamp(im, 38, 32, U, Y)
    shrub(im, 4, 30, U, O)
    shrub(im, 40, 30, U, O)
    apply_shadow(im, mask, K)


def park_classic(im):
    mask = new(48, 48, False)
    grass(im, E, M, Z)
    path_h(im, 0, 40, 48, L, W)
    path_v(im, 8, 28, 14, L, W)
    path_h(im, 8, 28, 28, L, W)
    tree(im, 0, 2, M, Z, U)
    tree(im, 36, 30, E, M, U)
    shrub(im, 2, 34, M, Z)
    # Ticket booth.
    estate_block(im, mask, 2, 16, 12, 6, 8, W, L, B, Q)
    window(im, 5, 20, 5, 4, Y, K)
    door(im, 6, 24, 4, 5, U, O)
    # Striped tent behind the wheel.
    tent(im, mask, 16, 8, 14, 12, R, F)
    wheel(im, mask, 34, 18, 12, G, K, R, 0.62)
    # Fence along the bottom path.
    for x in range(0, 48, 4):
        vline(im, x, 38, 3, U)
        put(im, x, 37, O)
    rect(im, 20, 32, 3, 2, R)
    rect(im, 24, 33, 3, 2, Y)
    rect(im, 28, 32, 3, 2, F)
    apply_shadow(im, mask, N)


def park_svga(im):
    mask = new(48, 48, False)
    grass(im, M, Z, C)
    path_h(im, 0, 42, 48, T, U)
    path_v(im, 30, 24, 20, T, U)
    path_h(im, 8, 24, 24, T, U)
    tree(im, 0, 0, Z, H, U)
    tree(im, 38, 0, C, Z, U)
    tree(im, 0, 28, M, Z, U)
    # Wheel is the left hero. A round carousel sits on the right.
    wheel(im, mask, 16, 16, 13, F, G, R, 0.58)
    # Carousel roof.
    for row in range(8):
        hline(im, 28 + row // 2, 14 + row, 16 - row, R)
        mark(mask, 28 + row // 2, 14 + row)
    rect(im, 30, 22, 12, 6, W)
    mark_rect(mask, 30, 22, 12, 6)
    vline(im, 32, 22, 6, G)
    vline(im, 36, 22, 6, G)
    vline(im, 40, 22, 6, G)
    estate_block(im, mask, 34, 28, 10, 4, 6, O, U, Y, O)
    window(im, 36, 30, 4, 3, B, F)
    for x in range(4, 44, 6):
        put(im, x, 40, R)
        put(im, x + 2, 41, Y)
    apply_shadow(im, mask, K)


def park_spooky(im):
    mask = new(48, 48, False)
    grass(im, H, S, U)
    path_h(im, 0, 40, 48, T, O)
    path_v(im, 14, 26, 16, T, O)
    # Bare autumn trees and a dusk wheel with warm lamps.
    rect(im, 4, 10, 2, 12, U)
    rect(im, 2, 6, 6, 5, O)
    rect(im, 40, 12, 2, 10, U)
    rect(im, 38, 8, 6, 5, U)
    tent(im, mask, 4, 18, 12, 14, O, Y)
    wheel(im, mask, 32, 16, 13, Y, O, R, 0.6)
    # A short string of lamps over the path. Gold, not neon.
    for x in range(6, 40, 5):
        put(im, x, 36, Y)
        put(im, x, 37, O)
    estate_block(im, mask, 18, 28, 10, 4, 6, T, U, D, K)
    door(im, 21, 30, 4, 5, K, Y)
    apply_shadow(im, mask, K)


def icon_from(sheet):
    out = new(40, 40, None)
    for y in range(40):
        for x in range(40):
            out[y][x] = sheet[y + 4][x + 4]
    return out


def write_png(path, im):
    h = len(im)
    w = len(im[0])

    def chunk(tag, payload):
        body = tag + payload
        return struct.pack(">I", len(payload)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    raw = b""
    for y in range(h):
        row = b"\x00"
        for x in range(w):
            rgb = im[y][x] or (0, 0, 0)
            if rgb not in PAL:
                raise SystemExit(f"{path} {x},{y} {rgb} off palette")
            row += bytes((rgb[0], rgb[1], rgb[2], 255))
        raw += row
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(png)

# Classic Mayor's House, Casino and Amusement Park are hand-kitbashed in the
# Micropolis Classic palette (3/4 projection, down-right shadow) and checked
# in as PNGs, so this script only writes the SVGA and Spooky looks.
KEEP_CLASSIC_SUFFIXES = ("",)
# Prison, Marina, and Fountain sheets and icons are editor-approved in all
# three looks. This script must not rewrite them.
HAND_AUTHORED = {"prison", "marina", "fountain"}


def main():
    sheets = {
        "mayor": (mayor_classic, mayor_svga, mayor_spooky),
        "casino": (casino_classic, casino_svga, casino_spooky),
        "amusement": (park_classic, park_svga, park_spooky),
    }
    suffixes = ("", "-svga", "-spooky")
    for stem, draws in sheets.items():
        if stem in HAND_AUTHORED:
            continue
        for draw, suffix in zip(draws, suffixes):
            if suffix in KEEP_CLASSIC_SUFFIXES:
                # The Classic sheets (and their tool icons, cut 4..44 from the
                # sheet) are hand-drawn art kept in the repo; the *_classic
                # drawers below are the old placeholders and are not written.
                continue
            im = new(48, 48, E)
            draw(im)
            write_png(ROOT / f"{stem}{suffix}.png", im)
            write_png(ICON / f"{stem}{suffix}.png", icon_from(im))


if __name__ == "__main__":
    main()
