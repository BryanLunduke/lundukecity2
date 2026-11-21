#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Bryan Lunduke
# See COPYING and NOTICE.

"""Original map art for church types 1-7 (Lunduke City 2).

The engine has eight church types. Type 0 is the classic church in the main
atlas (tiles 414-422). Types 1-7 are tiles 956-1018, past the end of the
256x960 main atlas, and had no art. This script builds each one as a small
voxel model (one voxel per sheet pixel) and draws it in the City 2 3/4
top-down view: roofs read first, walls rise up and a little to the right so
the south and west faces show, light comes from the upper left and shadows
fall down and to the right onto the lawn, like the hospital and the other
large civic buildings. It writes one extra sheet per look:

    data/special-tiles/churches.png         Classic palette
    data/special-tiles/churches-svga.png    VGA palette (SVGA look)
    data/special-tiles/churches-spooky.png  VGA palette, night ground

Each sheet is 256x64: 16 tiles a row, tile id 956 + n at index n, the same
row-major order as the main atlas. A church is nine consecutive ids, its 3x3
block in reading order. Index 63 (id 1019) stays plain lawn.

All shapes are original and procedural; nothing is traced or copied.

    python3 art/churches/make_churches.py [--preview DIR]
"""

from __future__ import annotations

import argparse
import math
import random
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "art" / "svga"))
from vga_palette import vga_palette_8bit  # noqa: E402

OUT = ROOT / "data" / "special-tiles"
S = 48
FIRST_ID = 956
CHURCH1BASE = 956
ZMAX = 48

# A unit of height rises RISE px and leans LEAN px right on the sheet.
LEAN, RISE = 0.45, 0.55
# Toward the light: up, from the north-west (upper left of the sheet).
_L = (-0.55, -0.50, 0.67)
_n = math.sqrt(sum(c * c for c in _L))
LIGHT = tuple(c / _n for c in _L)
# Face shading uses a softer key light from the west and a little south, so
# the roofs and west walls are brightest, the south fronts mid tone and the
# east sides dark, the way the hospital and the other civic buildings read.
_K = (-0.50, 0.45, 0.74)
_k = math.sqrt(sum(c * c for c in _K))
KEY = tuple(c / _k for c in _K)

# ---------------------------------------------------------------- palettes
C_BLACK, C_DGRAY, C_GRAY, C_LGRAY, C_WHITE = (0, 0, 0), (63, 63, 63), (127, 127, 127), (191, 191, 191), (255, 255, 255)
C_GRASS, C_DGREEN = (0, 230, 0), (0, 127, 0)
C_DIRT, C_BROWN = (204, 127, 102), (153, 127, 76)
C_RED, C_ORANGE, C_YELLOW = (255, 0, 0), (255, 127, 0), (255, 255, 0)
C_LBLUE, C_BLUE = (102, 102, 230), (0, 0, 230)

# Each material is a ramp: lit, mid, dark, darkest.
CLASSIC = {
    "white": (C_WHITE, C_LGRAY, C_GRAY, C_DGRAY),
    "stone": (C_LGRAY, C_GRAY, C_DGRAY, C_BLACK),
    "brick": (C_DIRT, C_BROWN, C_DGRAY, C_BLACK),
    "tan": (C_DIRT, C_BROWN, C_DGRAY, C_BLACK),
    "slate": (C_GRAY, C_DGRAY, C_DGRAY, C_BLACK),
    "shingle": (C_BROWN, C_DGRAY, C_DGRAY, C_BLACK),
    "terra": (C_ORANGE, C_DIRT, C_BROWN, C_DGRAY),
    "gold": (C_YELLOW, C_ORANGE, C_BROWN, C_DGRAY),
    "copper": (C_DGREEN, C_DGREEN, C_DGRAY, C_BLACK),
    "glass": (C_WHITE, C_LBLUE, C_BLUE, C_BLUE),
    "window": (C_DGRAY, C_BLACK, C_BLACK, C_BLACK),
    "lit_window": (C_LBLUE, C_BLUE, C_BLUE, C_BLACK),
    "door": (C_BROWN, C_DGRAY, C_BLACK, C_BLACK),
    "rose": (C_RED, C_RED, C_BROWN, C_BLACK),
    "cross": (C_YELLOW, C_YELLOW, C_ORANGE, C_BROWN),
    "bell": (C_YELLOW, C_ORANGE, C_BROWN, C_BROWN),
    "tree": (C_DGREEN, C_DGREEN, C_BLACK, C_BLACK),
    "trunk": (C_BROWN, C_BROWN, C_DGRAY, C_DGRAY),
    "concrete": (C_LGRAY, C_GRAY, C_DGRAY, C_BLACK),
    "dark": (C_DGRAY, C_BLACK, C_BLACK, C_BLACK),
}
CLASSIC_GROUND = {
    "grass": C_GRASS, "grass_dot": C_DGREEN, "grass_sh": C_DGREEN,
    "path": C_LGRAY, "path_sh": C_GRAY, "gravel": C_DIRT, "gravel_sh": C_BROWN,
    "asphalt": C_DGRAY, "asphalt_sh": C_BLACK, "line": C_WHITE, "line_sh": C_GRAY,
    "sand": C_DIRT, "sand_sh": C_BROWN, "water": C_LBLUE, "water_sh": C_BLUE,
    "grave": C_LGRAY, "grave_sh": C_GRAY, "flower": C_YELLOW, "flower_sh": C_ORANGE,
    "star": C_YELLOW, "star_sh": C_ORANGE,
    "ink": C_BLACK,
}

# SVGA colours are picked by eye, then snapped to the VGA palette.
SVGA = {
    "white": ((243, 243, 243), (203, 203, 203), (150, 150, 150), (97, 97, 97)),
    "stone": ((190, 186, 170), (146, 142, 130), (100, 97, 90), (56, 56, 52)),
    "brick": ((180, 72, 48), (140, 50, 32), (97, 32, 20), (56, 20, 12)),
    "tan": ((205, 175, 125), (165, 132, 90), (120, 95, 60), (70, 56, 36)),
    "slate": ((105, 105, 130), (75, 75, 100), (52, 52, 72), (32, 32, 45)),
    "shingle": ((125, 75, 45), (95, 52, 30), (65, 36, 20), (40, 22, 12)),
    "terra": ((220, 110, 55), (180, 80, 40), (130, 52, 28), (85, 32, 16)),
    "gold": ((255, 225, 85), (230, 175, 40), (170, 115, 20), (105, 70, 12)),
    "copper": ((90, 190, 150), (56, 150, 113), (32, 105, 81), (16, 65, 50)),
    "glass": ((215, 235, 255), (125, 170, 240), (65, 100, 190), (32, 50, 120)),
    "window": ((56, 56, 90), (32, 32, 60), (20, 20, 40), (10, 10, 20)),
    "lit_window": ((150, 190, 255), (90, 130, 220), (60, 90, 170), (30, 45, 100)),
    "door": ((113, 65, 28), (81, 45, 20), (56, 30, 12), (32, 16, 8)),
    "rose": ((230, 60, 90), (190, 40, 120), (120, 30, 90), (60, 16, 45)),
    "cross": ((255, 235, 130), (240, 200, 70), (180, 140, 40), (110, 85, 20)),
    "bell": ((240, 190, 70), (200, 145, 40), (140, 95, 24), (85, 56, 12)),
    "tree": ((60, 150, 30), (20, 113, 10), (0, 81, 0), (0, 48, 0)),
    "trunk": ((113, 72, 40), (90, 56, 28), (65, 40, 20), (40, 24, 12)),
    "concrete": ((200, 200, 195), (160, 160, 155), (113, 113, 110), (69, 69, 65)),
    "dark": ((40, 40, 48), (24, 24, 32), (16, 16, 20), (8, 8, 8)),
}
SVGA_GROUND = {
    "grass": (0, 113, 0), "grass_dot": (28, 113, 0), "grass_sh": (32, 65, 32),
    "path": (182, 182, 182), "path_sh": (113, 113, 113), "gravel": (181, 148, 115), "gravel_sh": (132, 99, 66),
    "asphalt": (69, 69, 69), "asphalt_sh": (40, 40, 40), "line": (227, 227, 227), "line_sh": (130, 130, 130),
    "sand": (255, 219, 182), "sand_sh": (255, 158, 125), "water": (60, 115, 210), "water_sh": (32, 65, 140),
    "grave": (170, 170, 170), "grave_sh": (100, 100, 100), "flower": (255, 210, 60), "flower_sh": (170, 120, 30),
    "star": (255, 210, 60), "star_sh": (170, 120, 30),
    "ink": (0, 0, 0),
}
SPOOKY = dict(SVGA)
SPOOKY.update({
    "white": ((190, 180, 205), (150, 140, 170), (110, 100, 130), (70, 62, 90)),
    "stone": ((140, 130, 150), (105, 97, 120), (75, 68, 90), (45, 40, 56)),
    "tree": ((32, 81, 16), (16, 65, 16), (0, 45, 16), (0, 28, 8)),
    "window": ((220, 160, 50), (180, 120, 30), (120, 80, 20), (60, 40, 10)),
    "glass": ((255, 220, 120), (230, 170, 60), (170, 110, 30), (100, 60, 16)),
})
SPOOKY_GROUND = dict(SVGA_GROUND)
SPOOKY_GROUND.update({
    "grass": (0, 65, 16), "grass_dot": (16, 65, 0), "grass_sh": (0, 32, 16),
    "sand": (110, 90, 70), "sand_sh": (70, 56, 45), "gravel": (101, 81, 65), "gravel_sh": (65, 50, 40),
})


def snap(colour, pal):
    r, g, b = colour
    return min(pal, key=lambda p: (p[0] - r) ** 2 * 3 + (p[1] - g) ** 2 * 4 + (p[2] - b) ** 2 * 2)


# ---------------------------------------------------------------- model
class Model:
    """Voxels (one per sheet pixel of ground) plus a painted ground layer."""

    def __init__(self, seed):
        self.vox = {}
        # Shapes with sloped or curved tops store their true surface normal,
        # so roofs shade as smooth planes rather than voxel stair steps.
        self.norm = {}
        self.ground = [["grass"] * S for _ in range(S)]
        self.rng = random.Random(seed)
        for y in range(S):
            for x in range(S):
                if self.rng.random() < 0.10:
                    self.ground[y][x] = "grass_dot"

    def set(self, x, y, z, mat):
        if 0 <= x < S and 0 <= y < S and 0 <= z < ZMAX:
            self.vox[(x, y, z)] = mat

    def setn(self, x, y, z, mat, n):
        if 0 <= x < S and 0 <= y < S and 0 <= z < ZMAX:
            self.vox[(x, y, z)] = mat
            l = math.sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2])
            self.norm[(x, y, z)] = (n[0] / l, n[1] / l, n[2] / l)

    def has(self, x, y, z):
        return (x, y, z) in self.vox

    # Ground paint.
    def paint(self, x0, y0, x1, y1, key):
        for y in range(max(0, y0), min(S, y1)):
            for x in range(max(0, x0), min(S, x1)):
                self.ground[y][x] = key

    # Solids. Ranges are half-open: x0 <= x < x1.
    def box(self, x0, y0, x1, y1, z0, z1, mat):
        for x in range(x0, x1):
            for y in range(y0, y1):
                for z in range(z0, z1):
                    self.set(x, y, z, mat)
                    self.norm.pop((x, y, z), None)

    def gable_ns(self, x0, y0, x1, y1, z0, h, rh, wall, roof):
        """Walls h high, a roof rh high, ridge north-south."""
        self.box(x0, y0, x1, y1, z0, z0 + h, wall)
        half = (x1 - x0) / 2.0
        xm = x0 + half
        for x in range(x0, x1):
            top = rh * (1.0 - abs(x + 0.5 - xm) / half)
            for z in range(z0 + h, z0 + h + int(round(top)) + 1):
                for y in range(y0, y1):
                    # The south and north gable ends are wall.
                    if y in (y0, y1 - 1) and z < z0 + h + top - 0.6:
                        self.set(x, y, z, wall)
                    else:
                        self.setn(x, y, z, roof, (-rh / half if x + 0.5 < xm else rh / half, 0.0, 1.0))

    def gable_ew(self, x0, y0, x1, y1, z0, h, rh, wall, roof):
        self.box(x0, y0, x1, y1, z0, z0 + h, wall)
        half = (y1 - y0) / 2.0
        ym = y0 + half
        for y in range(y0, y1):
            top = rh * (1.0 - abs(y + 0.5 - ym) / half)
            for z in range(z0 + h, z0 + h + int(round(top)) + 1):
                for x in range(x0, x1):
                    if x in (x0, x1 - 1) and z < z0 + h + top - 0.6:
                        self.set(x, y, z, wall)
                    else:
                        self.setn(x, y, z, roof, (0.0, -rh / half if y + 0.5 < ym else rh / half, 1.0))

    def pyramid(self, x0, y0, x1, y1, z0, hgt, mat):
        cx, cy = (x0 + x1) / 2.0, (y0 + y1) / 2.0
        hx, hy = (x1 - x0) / 2.0, (y1 - y0) / 2.0
        for x in range(x0, x1):
            for y in range(y0, y1):
                ux, uy = (x + 0.5 - cx) / hx, (y + 0.5 - cy) / hy
                d = max(abs(ux), abs(uy))
                top = hgt * (1.0 - d)
                if abs(ux) >= abs(uy):
                    n = (math.copysign(hgt / hx, ux), 0.0, 1.0)
                else:
                    n = (0.0, math.copysign(hgt / hy, uy), 1.0)
                for z in range(z0, z0 + int(round(top)) + 1):
                    self.setn(x, y, z, mat, n)

    def cone(self, cx, cy, r, z0, hgt, mat):
        for x in range(int(cx - r) - 1, int(cx + r) + 2):
            for y in range(int(cy - r) - 1, int(cy + r) + 2):
                dx, dy = x + 0.5 - cx, y + 0.5 - cy
                d = math.hypot(dx, dy) / r
                if d <= 1.0:
                    k = hgt / r / max(d * r, 0.01)
                    for z in range(z0, z0 + int(round(hgt * (1.0 - d))) + 1):
                        self.setn(x, y, z, mat, (dx * k, dy * k, 1.0))

    def cylinder(self, cx, cy, r, z0, z1, mat):
        for x in range(int(cx - r) - 1, int(cx + r) + 2):
            for y in range(int(cy - r) - 1, int(cy + r) + 2):
                if math.hypot(x + 0.5 - cx, y + 0.5 - cy) <= r:
                    for z in range(z0, z1):
                        self.setn(x, y, z, mat, (x + 0.5 - cx + 1e-3, y + 0.5 - cy, 0.0))

    def dome(self, cx, cy, z0, r, mat, squash=1.0):
        for x in range(int(cx - r) - 1, int(cx + r) + 2):
            for y in range(int(cy - r) - 1, int(cy + r) + 2):
                d2 = (x + 0.5 - cx) ** 2 + (y + 0.5 - cy) ** 2
                if d2 <= r * r:
                    top = math.sqrt(r * r - d2) * squash
                    for z in range(z0, z0 + int(round(top)) + 1):
                        self.setn(x, y, z, mat, (x + 0.5 - cx, y + 0.5 - cy, max(z - z0 + 0.5, 0.3) / squash))

    def tree(self, x, y, r=3, trunk=2):
        self.box(x, y, x + 1, y + 1, 0, trunk + 1, "trunk")
        cx, cy, cz = x + 0.5, y + 0.5, trunk + r
        for vx in range(int(cx - r) - 1, int(cx + r) + 2):
            for vy in range(int(cy - r) - 1, int(cy + r) + 2):
                for vz in range(int(cz - r) - 1, int(cz + r) + 2):
                    if (vx + 0.5 - cx) ** 2 + (vy + 0.5 - cy) ** 2 + ((vz + 0.5 - cz) * 1.2) ** 2 <= r * r:
                        self.set(vx, vy, vz, "tree")

    # Decals on existing surfaces.
    def south_face(self, x0, x1, y, z0, z1, mat):
        """Paint the south face voxels of row y (the front of a wall)."""
        for x in range(x0, x1):
            for z in range(z0, z1):
                if self.has(x, y, z):
                    self.vox[(x, y, z)] = mat

    def west_face(self, x, y0, y1, z0, z1, mat):
        for y in range(y0, y1):
            for z in range(z0, z1):
                if self.has(x, y, z):
                    self.vox[(x, y, z)] = mat

    def cross(self, x, y, z, mat="cross", tall=4):
        for k in range(tall):
            self.set(x, y, z + k, mat)
        self.set(x - 1, y, z + tall - 2, mat)
        self.set(x + 1, y, z + tall - 2, mat)

    def clear(self, x0, y0, x1, y1, z0, z1):
        for x in range(x0, x1):
            for y in range(y0, y1):
                for z in range(z0, z1):
                    self.vox.pop((x, y, z), None)
                    self.norm.pop((x, y, z), None)

    def _arch_top(self, i, i0, i1, z0, z1):
        """Highest z of an arched opening spanning i0..i1 (half-open)."""
        span = max(1, i1 - i0)
        mid = i0 + span / 2.0
        rx = span / 2.0
        nx = (i + 0.5 - mid) / rx
        if abs(nx) >= 1.0:
            return z0
        rise = min(span / 2.0, z1 - z0)
        spring = z1 - rise
        return min(z1, spring + rise * math.sqrt(max(0.0, 1.0 - nx * nx)))

    def arch_ns(self, x0, y0, x1, y1, z0, z1):
        """Clear an opening through north-south, arched in the x-z plane."""
        for x in range(x0, x1):
            top = int(math.ceil(self._arch_top(x, x0, x1, z0, z1)))
            for y in range(y0, y1):
                for z in range(z0, top):
                    self.vox.pop((x, y, z), None)
                    self.norm.pop((x, y, z), None)

    def arch_ew(self, x0, y0, x1, y1, z0, z1):
        """Clear an opening through west-east, arched in the y-z plane."""
        for y in range(y0, y1):
            top = int(math.ceil(self._arch_top(y, y0, y1, z0, z1)))
            for x in range(x0, x1):
                for z in range(z0, top):
                    self.vox.pop((x, y, z), None)
                    self.norm.pop((x, y, z), None)

    def niche_s(self, x0, y_back, x1, y_front, z0, z1):
        """South-facing arched recess; the wall at y_back stays and is darkened."""
        self.arch_ns(x0, y_back + 1, x1, y_front, z0, z1)
        self.south_face(x0, x1, y_back, z0, z1, "window")

    def niche_w(self, x_front, y0, x_back, y1, z0, z1):
        """West-facing arched recess; the wall at x_back stays and is darkened."""
        self.arch_ew(x_front, y0, x_back, y1, z0, z1)
        self.west_face(x_back, y0, y1, z0, z1, "window")

    def hanging_bell(self, cx, cy, z0, z1):
        """A small gold bell hanging in an opening, wider toward the lip."""
        mid = z0 + max(1, (z1 - z0) // 3)
        self.box(cx, cy, cx + 1, cy + 1, mid, z1, "bell")
        self.box(cx - 1, cy, cx + 2, cy + 1, z0, mid, "bell")

    def disc_south(self, cx, y, cz, r, mat, lift=1.0):
        """Circle on the south face. lift stretches height so it still reads
        round after the 3/4 projection."""
        rz = r * lift
        for x in range(int(cx - r) - 1, int(cx + r) + 2):
            for z in range(int(cz - rz) - 1, int(cz + rz) + 2):
                if ((x + 0.5 - cx) / r) ** 2 + ((z + 0.5 - cz) / rz) ** 2 <= 1.0:
                    if self.has(x, y, z):
                        self.vox[(x, y, z)] = mat

    def star_of_david(self, cx, y, cz, radius, mat, lift=1.0):
        """Upright hexagram on the south face, centred at (cx, cz).
        lift stretches it in z so it looks even after the 3/4 projection."""
        r = float(radius)
        rz = r * lift
        up = ((cx, cz + rz), (cx - r * 0.866, cz - rz * 0.5), (cx + r * 0.866, cz - rz * 0.5))
        dn = ((cx, cz - rz), (cx - r * 0.866, cz + rz * 0.5), (cx + r * 0.866, cz + rz * 0.5))

        def in_tri(px, pz, tri):
            (ax, az), (bx, bz), (cx_, cz_) = tri
            d = (bz - cz_) * (ax - cx_) + (cx_ - bx) * (az - cz_)
            if d == 0:
                return False
            u = ((bz - cz_) * (px - cx_) + (cx_ - bx) * (pz - cz_)) / d
            v = ((cz_ - az) * (px - cx_) + (ax - cx_) * (pz - cz_)) / d
            return u >= 0 and v >= 0 and u + v <= 1

        for x in range(int(cx - r) - 1, int(cx + r) + 2):
            for z in range(int(cz - rz) - 1, int(cz + rz) + 2):
                if in_tri(x + 0.5, z + 0.5, up) or in_tri(x + 0.5, z + 0.5, dn):
                    if self.has(x, y, z):
                        self.vox[(x, y, z)] = mat

    def star_ground(self, cx, cy, radius, key="star"):
        """Upright hexagram painted on the ground (no projection squash)."""
        r = float(radius)
        up = ((cx, cy + r), (cx - r * 0.866, cy - r * 0.5), (cx + r * 0.866, cy - r * 0.5))
        dn = ((cx, cy - r), (cx - r * 0.866, cy + r * 0.5), (cx + r * 0.866, cy + r * 0.5))

        def in_tri(px, py, tri):
            (ax, ay), (bx, by), (cx_, cy_) = tri
            d = (by - cy_) * (ax - cx_) + (cx_ - bx) * (ay - cy_)
            if d == 0:
                return False
            u = ((by - cy_) * (px - cx_) + (cx_ - bx) * (py - cy_)) / d
            v = ((cy_ - ay) * (px - cx_) + (ax - cx_) * (py - cy_)) / d
            return u >= 0 and v >= 0 and u + v <= 1

        for x in range(int(cx - r) - 1, int(cx + r) + 2):
            for y in range(int(cy - r) - 1, int(cy + r) + 2):
                if 0 <= x < S and 0 <= y < S and (in_tri(x + 0.5, y + 0.5, up) or in_tri(x + 0.5, y + 0.5, dn)):
                    self.ground[y][x] = key

    def arch_windows_s(self, x0, x1, y, z0, z1, step=3, mat="window"):
        for x in range(x0, x1 - 1, step):
            self.south_face(x, x + 2, y, z0, z1 - 1, mat)
            self.south_face(x, x + 2, y, z1 - 1, z1, mat)

    def arch_windows_w(self, x, y0, y1, z0, z1, step=3, mat="window"):
        for y in range(y0, y1 - 1, step):
            self.west_face(x, y, y + 2, z0, z1 - 1, mat)
            self.west_face(x, y, y + 2, z1 - 1, z1, mat)


# ---------------------------------------------------------------- render
def surface_normal(m, x, y, z):
    up = not m.has(x, y, z + 1)
    if (x, y, z) in m.norm:
        return m.norm[(x, y, z)]
    # Plain blocks: the face the viewer sees, top first, then west, south, east.
    if up:
        return (0.0, 0.0, 1.0)
    if not m.has(x - 1, y, z):
        return (-1.0, 0.0, 0.0)
    if not m.has(x, y + 1, z):
        return (0.0, 1.0, 0.0)
    if not m.has(x + 1, y, z):
        return (1.0, 0.0, 0.0)
    nx = ny = nz = 0.0
    for dx in (-1, 0, 1):
        for dy in (-1, 0, 1):
            for dz in (-1, 0, 1):
                if (dx or dy or dz) and not m.has(x + dx, y + dy, z + dz):
                    w = 1.0 / math.sqrt(dx * dx + dy * dy + dz * dz)
                    nx += dx * w
                    ny += dy * w
                    nz += dz * w
    n = math.sqrt(nx * nx + ny * ny + nz * nz)
    if n == 0:
        return (0.0, 0.0, 1.0)
    return (nx / n, ny / n, nz / n)


def lit_from(m, x, y, z):
    """False when something stands between this point and the light."""
    px, py, pz = x + 0.5, y + 0.5, z + 0.5
    # Start a voxel out along the surface normal so stair-stepped slopes
    # do not shadow themselves.
    if z >= 0 and m.has(x, y, z):
        n = surface_normal(m, x, y, z)
        px, py, pz = px + n[0] * 1.1, py + n[1] * 1.1, pz + n[2] * 1.1
    for i in range(1, 120):
        t = i * 0.5
        qx, qy, qz = px + LIGHT[0] * t, py + LIGHT[1] * t, pz + LIGHT[2] * t
        if qz >= ZMAX or qx < -2 or qy < -2:
            return True
        hz = int(math.floor(qz))
        # On a building, only something clearly higher casts a shadow; this
        # keeps grazing light from striping the stair-stepped roof slopes.
        if m.has(int(math.floor(qx)), int(math.floor(qy)), hz) and (z < 0 or hz >= z + 3):
            return False
    return True


def project(x, y, z):
    return (int(math.floor(x + 0.5 + LEAN * (z + 0.5))), int(math.floor(y + 0.5 - RISE * (z + 0.5))))


def render(model):
    """Sheet keys: ('g', ground key) or ('v', material, level)."""
    out = [[("g", model.ground[y][x]) for x in range(S)] for y in range(S)]
    solid = [[False] * S for _ in range(S)]
    depth = [[-1e9] * S for _ in range(S)]
    # Ground shadows.
    for y in range(S):
        for x in range(S):
            if not lit_from(model, x, y, -1):
                key = model.ground[y][x]
                out[y][x] = ("g", key + "_sh" if not key.endswith("_dot") else "grass_sh")
    # Cast a ray from each sheet pixel down into the model, from the top of
    # the box toward the ground, and take the first voxel it meets. (Splatting
    # voxels leaves holes in slopes because a unit of height leans LEAN px.)
    clipped = set()
    for sy in range(S):
        for sx in range(S):
            z = float(ZMAX)
            while z > 0.0:
                vx = int(math.floor(sx + 0.5 - LEAN * z))
                vy = int(math.floor(sy + 0.5 + RISE * z))
                vz = int(math.floor(z))
                mat = model.vox.get((vx, vy, vz))
                if mat is not None:
                    n = surface_normal(model, vx, vy, vz)
                    b = n[0] * KEY[0] + n[1] * KEY[1] + n[2] * KEY[2]
                    level = 0 if b > 0.68 else 1 if b > 0.36 else 2 if b > 0.05 else 3
                    if level < 3 and not lit_from(model, vx, vy, vz):
                        level += 1
                    out[sy][sx] = ("v", mat, level)
                    solid[sy][sx] = True
                    depth[sy][sx] = -LEAN * vx + RISE * vy + vz
                    break
                z -= 0.2
    # Where one surface stands well in front of another (an eave over a
    # wall, a tower against a roof), darken the one behind along the edge.
    edged = [row[:] for row in out]
    for sy in range(S):
        for sx in range(S):
            if not solid[sy][sx]:
                continue
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nx, ny = sx + dx, sy + dy
                if 0 <= nx < S and 0 <= ny < S and solid[ny][nx] and depth[ny][nx] - depth[sy][sx] > 3.5:
                    edged[sy][sx] = ("v", out[sy][sx][1], 3)
                    break
    out = edged
    # Anything whose top would project above or right of the sheet is cut.
    for (x, y, z), mat in model.vox.items():
        if mat in ("tree", "trunk"):
            continue
        if x + LEAN * (z + 1) >= S or y - RISE * (z + 1) < -0.5:
            clipped.add((x, y, z))
    if clipped:
        print(f"warning: {len(clipped)} voxels fall off the sheet, e.g. {sorted(clipped)[:3]}", file=sys.stderr)
    # Ink outline round the buildings (not the trees), on the ground side.
    final = [row[:] for row in out]
    for y in range(S):
        for x in range(S):
            if out[y][x][0] != "g":
                continue
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nx, ny = x + dx, y + dy
                if 0 <= nx < S and 0 <= ny < S and solid[ny][nx]:
                    final[y][x] = ("g", "ink")
                    break
    return final


def to_image(keys, ramps, ground, pal8):
    img = Image.new("RGB", (S, S))
    px = img.load()
    cache = {}
    for y in range(S):
        for x in range(S):
            k = keys[y][x]
            colour = ground.get(k[1], ground["grass"]) if k[0] == "g" else ramps[k[1]][k[2]]
            if pal8 is not None:
                if colour not in cache:
                    cache[colour] = snap(colour, pal8)
                colour = cache[colour]
            px[x, y] = colour
    return img


# ---------------------------------------------------------------- churches
def chapel(m):
    """Type 1, Country Chapel: a small white clapboard chapel with a brown
    shingle roof, a square belfry with open arches and a visible bell, and a
    short pointed spire over the door, a gravel walk, a churchyard of
    headstones and shade trees."""
    m.paint(14, 42, 22, 48, "gravel")
    for gx, gy in ((30, 26), (35, 26), (40, 26), (30, 32), (36, 32), (41, 32), (32, 38), (38, 38)):
        m.paint(gx, gy, gx + 2, gy + 2, "grave")
        m.box(gx, gy, gx + 1, gy + 1, 0, 3, "stone")
    m.gable_ns(8, 14, 24, 36, 0, 9, 7, "white", "shingle")
    for y in range(17, 32, 5):
        m.west_face(8, y, y + 2, 3, 7, "window")
    # Tower at the front: a door in the shaft, a slightly inset open belfry,
    # then a sharp slate spire and a gold cross.
    m.box(12, 34, 20, 42, 0, 14, "white")
    m.south_face(14, 18, 41, 0, 6, "door")
    m.box(13, 35, 19, 41, 14, 18, "white")
    m.box(12, 34, 20, 42, 18, 25, "white")
    m.niche_s(14, 34, 18, 42, 19, 25)
    m.niche_w(12, 36, 13, 40, 19, 25)
    m.hanging_bell(16, 40, 19, 24)
    m.cone(16, 38, 4.0, 25, 13, "slate")
    m.cross(16, 38, 38, tall=4)
    m.tree(3, 12, 3)
    m.tree(40, 14, 3)
    m.tree(3, 40, 3)
    m.tree(42, 44, 2)


def steeple_church(m):
    """Type 2, Steeple Church: a red-brick hall with tall windows and a slate
    roof, and a white steeple with a belfry, a clock and a tall spire at its
    front. A paved walk runs to the door."""
    m.paint(15, 44, 22, 48, "path")
    m.gable_ns(8, 12, 28, 38, 0, 11, 8, "brick", "slate")
    for y in range(15, 34, 4):
        m.west_face(8, y, y + 2, 3, 9, "lit_window")
    m.box(13, 34, 23, 44, 0, 15, "white")
    m.box(14, 35, 22, 43, 15, 22, "white")
    m.south_face(16, 20, 43, 0, 6, "door")
    m.south_face(16, 20, 42, 17, 21, "window")
    m.south_face(17, 19, 43, 10, 12, "window")
    m.pyramid(14, 35, 22, 43, 22, 16, "white")
    m.cross(18, 39, 38, tall=4)
    m.tree(40, 20, 4)
    m.tree(41, 37, 4)
    m.tree(3, 43, 3)


def cathedral(m):
    """Type 3, Cathedral: a big grey stone cathedral on a cross plan with
    green copper roofs, a spire over the crossing, twin west-front towers and a red
    rose window over the door. It fills the lot."""
    m.paint(15, 45, 23, 48, "path")
    m.gable_ew(2, 20, 36, 30, 0, 12, 7, "stone", "copper")
    m.gable_ns(10, 14, 28, 42, 0, 12, 8, "stone", "copper")
    for y in range(31, 40, 3):
        m.west_face(10, y, y + 1, 4, 10, "window")
    for x in (4, 7) + (30, 33):
        m.south_face(x, x + 1, 29, 4, 10, "window")
    for y in range(22, 29, 3):
        m.west_face(2, y, y + 1, 4, 10, "window")
    m.box(16, 22, 23, 29, 12, 23, "stone")
    m.south_face(18, 19, 28, 15, 20, "window")
    m.south_face(20, 21, 28, 15, 20, "window")
    m.pyramid(16, 22, 23, 29, 23, 10, "copper")
    m.cross(19, 25, 33, tall=3)
    for x0 in (7, 24):
        m.box(x0, 38, x0 + 7, 45, 0, 22, "stone")
        m.south_face(x0 + 1, x0 + 2, 44, 14, 20, "window")
        m.south_face(x0 + 4, x0 + 5, 44, 14, 20, "window")
        m.south_face(x0 + 2, x0 + 4, 44, 4, 9, "window")
        m.pyramid(x0, 38, x0 + 7, 45, 22, 6, "copper")
    m.box(14, 40, 24, 44, 0, 15, "stone")
    m.south_face(17, 21, 43, 0, 5, "door")
    m.south_face(16, 22, 43, 8, 13, "rose")
    m.south_face(18, 20, 43, 9, 12, "glass")
    m.tree(42, 40, 3)
    m.tree(3, 42, 2)


def synagogue(m):
    """Type 4, Synagogue: a brick hall with a dark slate roof, modest square
    twin towers with pyramidal caps, a paved entry court and steps,
    round-arched windows, and a large gold Star of David (two interlocking
    triangles on a dark round backing) on the front gable, with another
    star in the pavement. No dome and no minaret-like towers."""
    m.paint(10, 40, 36, 48, "path")
    m.star_ground(21.5, 41.5, 6, "ink")
    m.star_ground(21.5, 41.5, 4.8, "star")
    # Hall sits south enough that the roof stays on the sheet.
    m.gable_ns(10, 12, 33, 34, 0, 12, 8, "brick", "slate")
    m.arch_windows_w(10, 14, 32, 4, 10, step=4)
    for x0 in (6, 29):
        m.box(x0, 28, x0 + 8, 40, 0, 20, "brick")
        m.south_face(x0 + 2, x0 + 3, 39, 6, 12, "window")
        m.south_face(x0 + 5, x0 + 6, 39, 6, 12, "window")
        m.south_face(x0 + 3, x0 + 5, 39, 13, 16, "window")
        m.west_face(x0, 30, 38, 6, 12, "window")
        m.pyramid(x0, 28, x0 + 8, 40, 20, 6, "slate")
    # Porch and door. A tall, one-voxel-deep peaked gable above the door
    # gives a large, flat south face for a hand-placed Star of David
    # stamped in sheet pixels after the render (see stamp_star_motif).
    m.box(16, 34, 27, 40, 0, 10, "brick")
    m.south_face(19, 24, 39, 0, 6, "door")
    m.box(13, 39, 30, 40, 7, 16, "brick")
    mid, half = 21.5, 8.5
    for x in range(13, 30):
        top = 16 + int(round(24 * (1.0 - abs(x + 0.5 - mid) / half)))
        for z in range(16, top + 1):
            m.set(x, 39, z, "brick")
    m.tree(2, 16, 3)
    m.tree(42, 16, 3)
    m.tree(2, 42, 3)
    m.tree(43, 42, 2)


def mission(m):
    """Type 5, Mission Church: a whitewashed adobe hall with a red clay-tile
    roof and a tall stepped bell wall (espadaña) with three arched openings
    and hanging bells, beside an arcaded sand courtyard with a fountain and
    two trees."""
    m.paint(20, 12, 44, 44, "sand")
    m.gable_ew(20, 8, 44, 14, 0, 5, 2, "white", "terra")
    m.gable_ns(38, 14, 44, 42, 0, 5, 2, "white", "terra")
    for x in range(22, 37, 3):
        m.south_face(x, x + 2, 13, 0, 4, "window")
    for y in range(16, 41, 3):
        m.west_face(38, y, y + 2, 0, 4, "window")
    for dy in range(-2, 3):
        for dx in range(-3, 4):
            if dx * dx / 9.0 + dy * dy / 4.0 <= 1.0:
                m.ground[28 + dy][29 + dx] = "water" if abs(dx) < 3 and abs(dy) < 2 else "path"
    m.cylinder(29.5, 28.5, 0.8, 0, 3, "white")
    m.tree(24, 38, 2)
    m.tree(34, 18, 2)
    m.gable_ns(4, 8, 18, 36, 0, 9, 5, "white", "terra")
    for y in range(11, 33, 5):
        m.west_face(4, y, y + 1, 4, 7, "window")
    # Free-standing stepped bell wall, wider than the nave, with large dark
    # arched niches and a gold bell hanging in each.
    m.box(1, 38, 21, 43, 0, 11, "white")
    m.south_face(9, 13, 42, 0, 6, "door")
    m.box(4, 38, 18, 43, 11, 18, "white")
    m.niche_s(5, 38, 9, 43, 12, 18)
    m.niche_s(13, 38, 17, 43, 12, 18)
    m.hanging_bell(7, 41, 12, 17)
    m.hanging_bell(15, 41, 12, 17)
    m.box(7, 38, 15, 43, 18, 26, "white")
    m.niche_s(9, 38, 13, 43, 19, 26)
    m.hanging_bell(11, 41, 19, 25)
    m.box(9, 38, 13, 43, 26, 28, "white")
    m.cross(11, 40, 28, tall=4)
    m.paint(8, 43, 14, 48, "sand")
    m.tree(44, 8, 2)


def abbey(m):
    """Type 6, Abbey: a long brown-stone church running east to west with a
    square battlemented tower, and to the south a cloister: a covered walk
    round a garden with crossing paths and a well."""
    m.paint(9, 35, 33, 37, "path")
    m.paint(20, 30, 22, 42, "path")
    m.gable_ew(5, 26, 37, 30, 0, 4, 2, "tan", "shingle")
    m.gable_ew(5, 42, 37, 46, 0, 4, 2, "tan", "shingle")
    m.gable_ns(5, 30, 9, 42, 0, 4, 2, "tan", "shingle")
    m.gable_ns(33, 30, 37, 42, 0, 4, 2, "tan", "shingle")
    for x in range(7, 36, 3):
        m.south_face(x, x + 1, 45, 0, 3, "window")
        m.south_face(x, x + 1, 29, 0, 3, "window")
    for y in range(31, 41, 3):
        m.west_face(33, y, y + 1, 0, 3, "window")
        m.west_face(5, y, y + 1, 0, 3, "window")
    m.cylinder(21, 36, 1.6, 0, 2, "stone")
    m.ground[35][20] = "water"
    m.gable_ew(3, 10, 39, 22, 0, 10, 6, "tan", "shingle")
    for x in range(5, 38, 3):
        if not 15 <= x <= 25:
            m.south_face(x, x + 1, 21, 3, 8, "window")
    m.west_face(3, 13, 20, 3, 8, "window")
    m.box(16, 11, 25, 23, 0, 18, "tan")
    for x in range(16, 25):
        for y in range(11, 23):
            if (x in (16, 24) or y in (11, 22)) and (x + y) % 2 == 0:
                m.set(x, y, 18, "tan")
                m.set(x, y, 19, "tan")
    m.south_face(18, 19, 22, 12, 16, "window")
    m.south_face(22, 23, 22, 12, 16, "window")
    m.south_face(19, 22, 22, 0, 5, "door")
    m.tree(42, 30, 3)
    m.tree(43, 42, 2)


def modern_church(m):
    """Type 7, Modern Church: a steep A-frame whose roof runs to the ground,
    a tall glass front with white mullions, a slim concrete bell tower with
    a cross, and a striped parking lot."""
    m.paint(30, 30, 48, 48, "asphalt")
    for x in range(32, 48, 4):
        m.paint(x, 32, x + 1, 37, "line")
        m.paint(x, 42, x + 1, 47, "line")
    m.paint(12, 40, 18, 48, "path")
    m.gable_ns(5, 18, 25, 40, 0, 1, 22, "glass", "slate")
    for z in (7, 14):
        m.south_face(5, 25, 39, z, z + 1, "white")
    m.south_face(14, 16, 39, 0, 23, "white")
    m.south_face(13, 17, 39, 0, 4, "door")
    m.box(28, 22, 31, 25, 0, 30, "concrete")
    m.south_face(29, 30, 24, 22, 26, "window")
    m.cross(29, 23, 30, tall=5)
    m.tree(2, 10, 2)
    m.tree(2, 30, 2)
    m.tree(40, 14, 3)


CHURCHES = [
    (1, "Country Chapel", chapel),
    (2, "Steeple Church", steeple_church),
    (3, "Cathedral", cathedral),
    (4, "Synagogue", synagogue),
    (5, "Mission Church", mission),
    (6, "Abbey", abbey),
    (7, "Modern Church", modern_church),
]


def _in_triangle(px, py, tri):
    (ax, ay), (bx, by), (cx, cy) = tri
    d = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
    if d == 0:
        return False
    u = ((by - cy) * (px - cx) + (cx - bx) * (py - cy)) / d
    v = ((cy - ay) * (px - cx) + (ax - cx) * (py - cy)) / d
    return u >= 0 and v >= 0 and u + v <= 1


def _close_to_segment(px, py, x0, y0, x1, y1, half=0.7):
    dx, dy = x1 - x0, y1 - y0
    L2 = dx * dx + dy * dy
    if L2 == 0:
        return (px - x0) ** 2 + (py - y0) ** 2 <= half * half
    t = max(0.0, min(1.0, ((px - x0) * dx + (py - y0) * dy) / L2))
    qx, qy = x0 + t * dx, y0 + t * dy
    return (px - qx) ** 2 + (py - qy) ** 2 <= half * half


def stamp_star_motif(keys, cx, cy, radius):
    """A large six-pointed star in sheet pixels (not projected), so the two
    interlocking triangles stay clear at game zoom. Dark round backing,
    gold points, and gold outlines of both triangles. Only paints over
    already-drawn building pixels."""
    cx, cy, r = float(cx), float(cy), float(radius)
    # Apex toward the top of the sheet (smaller y).
    up = ((cx, cy - r), (cx - r * 0.866025, cy + r * 0.5), (cx + r * 0.866025, cy + r * 0.5))
    dn = ((cx, cy + r), (cx - r * 0.866025, cy - r * 0.5), (cx + r * 0.866025, cy - r * 0.5))
    back_r = r + 2.4
    x0, x1 = int(cx - back_r) - 1, int(cx + back_r) + 2
    y0, y1 = int(cy - back_r) - 1, int(cy + back_r) + 2
    for y in range(max(0, y0), min(S, y1)):
        for x in range(max(0, x0), min(S, x1)):
            if keys[y][x][0] != "v":
                continue
            px, py = x + 0.5, y + 0.5
            in_up = _in_triangle(px, py, up)
            in_dn = _in_triangle(px, py, dn)
            on_edge = False
            for tri in (up, dn):
                for i in range(3):
                    ax, ay = tri[i]
                    bx, by = tri[(i + 1) % 3]
                    if _close_to_segment(px, py, ax, ay, bx, by, 0.75):
                        on_edge = True
                        break
            in_disc = (px - cx) ** 2 + (py - cy) ** 2 <= back_r * back_r
            if on_edge or (in_up or in_dn) and not (in_up and in_dn):
                keys[y][x] = ("v", "gold", 0)
            elif in_disc:
                keys[y][x] = ("v", "dark", 3)


def build(number, draw):
    model = Model(1000 + number)
    draw(model)
    keys = render(model)
    if number == 4:
        # Screen position of the frontispiece, measured from the 3/4 projection
        # of its centre, then nudged so the star sits on the gable face.
        stamp_star_motif(keys, 30, 25.5, 8.0)
    return keys


def sheet(ramps, ground, pal8, rendered):
    lawn = ground["grass"] if pal8 is None else snap(ground["grass"], pal8)
    out = Image.new("RGB", (256, 64), lawn)
    blocks = {}
    for number, _, _ in CHURCHES:
        block = to_image(rendered[number], ramps, ground, pal8)
        blocks[number] = block
        base = CHURCH1BASE + (number - 1) * 9
        for i in range(9):
            tile = base + i - FIRST_ID
            cell = block.crop(((i % 3) * 16, (i // 3) * 16, (i % 3) * 16 + 16, (i // 3) * 16 + 16))
            out.paste(cell, ((tile % 16) * 16, (tile // 16) * 16))
    return out, blocks


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--preview", type=Path)
    args = parser.parse_args()
    pal8 = vga_palette_8bit()
    rendered = {number: build(number, draw) for number, _, draw in CHURCHES}
    looks = (("churches.png", CLASSIC, CLASSIC_GROUND, None),
             ("churches-svga.png", SVGA, SVGA_GROUND, pal8),
             ("churches-spooky.png", SPOOKY, SPOOKY_GROUND, pal8))
    for name, ramps, ground, snapper in looks:
        image, blocks = sheet(ramps, ground, snapper, rendered)
        image.save(OUT / name, optimize=True)
        if args.preview:
            args.preview.mkdir(parents=True, exist_ok=True)
            strip = Image.new("RGB", (len(blocks) * (S + 4), S), (255, 0, 255))
            for i, number in enumerate(sorted(blocks)):
                strip.paste(blocks[number], (i * (S + 4), 0))
            strip.resize((strip.width * 5, strip.height * 5), Image.NEAREST).save(args.preview / name)
    return 0


if __name__ == "__main__":
    sys.exit(main())
