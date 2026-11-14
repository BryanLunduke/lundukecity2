#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Lunduke City authors
# See COPYING and NOTICE.
"""Original toolbar and landmark icons for Lunduke City 2 ("engraved stone").

These are original drawings, not traced from SimCity or any other game.
Each object is drawn on an 18x18 grid with a 1px dark-brown outline (20x20),
then doubled with nearest-neighbour to the 40x40 PNGs the game loads. One
icon set is used in every tile look. The button plate, hover and selected
states are drawn by the toolbar code (src/tool_palette.cpp), not baked in.

Style: a four-step sepia ramp, light from the top left, and one accent hue
per icon. Zone icons carry an 8x8 sign with the zone letter and colour
(R green, C blue, I yellow).

    python3 data/tool-icons/make_icons.py            # rewrite the PNGs here
    python3 data/tool-icons/make_icons.py --out DIR  # write elsewhere
    python3 data/tool-icons/make_icons.py --check    # compare, do not write
"""

import argparse
import math
import pathlib
import sys

from PIL import Image, ImageDraw

ROOT = pathlib.Path(__file__).resolve().parent
N = 18
ICON = 20
SCALE = 2


def hx(s):
    s = s.lstrip('#')
    return tuple(int(s[i:i + 2], 16) for i in (0, 2, 4))


def mix(a, b, t):
    return tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(3))


class Canvas:
    def __init__(self, n=N):
        self.n = n
        self.px = [[None] * n for _ in range(n)]
        self.hard = set()  # pixels that must stay (no auto outline over)

    def put(self, x, y, mat, s):
        if 0 <= x < self.n and 0 <= y < self.n:
            self.px[y][x] = (mat, s)

    def rect(self, x0, y0, x1, y1, mat, s):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                self.put(x, y, mat, s)

    def hline(self, x0, x1, y, mat, s):
        self.rect(x0, y, x1, y, mat, s)

    def vline(self, x, y0, y1, mat, s):
        self.rect(x, y0, x, y1, mat, s)

    def dither(self, x0, y0, x1, y1, mat, s, phase=0):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                if (x + y + phase) % 2 == 0:
                    self.put(x, y, mat, s)

    def rows(self, ox, oy, art, legend):
        """Hand-placed pixels: art is a list of strings, legend maps char."""
        for j, line in enumerate(art):
            for i, ch in enumerate(line):
                if ch in legend:
                    v = legend[ch]
                    if v is None:
                        self.px[oy + j][ox + i] = None
                    else:
                        self.put(ox + i, oy + j, *v)



RAMPS = {
    'yellow': ['#F8E088', '#E8C048', '#C09028', '#86601A'],
    'metal': ['#E0DED6', '#A8A6A0', '#7A7876', '#4E4C50'],
    'rubber': ['#6A6670', '#4A4650', '#34303A', '#221E26'],
    'glass': ['#B8DCF4', '#6C9CDC', '#3A64AC', '#22386E'],
    'asphalt': ['#8E8C90', '#727076', '#58565E', '#3C3A42'],
    'paint': ['#FCF8EC', '#E4DCC8', '#C0B6A0', '#8C8270'],
    'line': ['#F8E088', '#E8C048', '#C09028', '#86601A'],
    'cone': ['#F8B870', '#E07C34', '#B0501E', '#703012'],
    'roof': ['#E88868', '#C4523A', '#943428', '#5E1E1C'],
    'wall': ['#FCF2DC', '#EADCB8', '#C9B892', '#9C8664'],
    'door': ['#B07C54', '#8C634A', '#6B4228', '#4A2A18'],
    'brick': ['#D89068', '#B06446', '#844430', '#5A2C20'],
    'grass': ['#A8CC6C', '#6CA448', '#3E7A30', '#245020'],
    'smoke': ['#F4F4F2', '#D0D0D2', '#A8A8B0', '#7C7C88'],
    'navy': ['#5874CC', '#2E4698', '#1C2C6C', '#0C1440'],
    'gold': ['#FCEEB0', '#E8C775', '#B88C2E', '#7A5A18'],
    'wood': ['#C08858', '#8C634A', '#6B4228', '#4A2A18'],
}
RAMPS = {k: [hx(c) for c in v] for k, v in RAMPS.items()}



def bulldozer():
    c = Canvas()
    Y, G, B, K = 'yellow', 'metal', 'glass', 'rubber'
    # cab: lit roof, front window, shaded right side
    c.hline(8, 13, 2, Y, 0)
    c.rect(8, 3, 12, 8, Y, 1)
    c.rect(13, 3, 14, 8, Y, 2)
    c.vline(14, 3, 8, Y, 3)
    c.rect(9, 4, 11, 6, B, 1)
    c.put(9, 4, B, 0)
    c.put(10, 4, B, 0)
    c.hline(9, 11, 6, B, 2)
    c.rect(13, 4, 13, 6, B, 2)
    # hood / engine block
    c.hline(4, 8, 7, Y, 0)
    c.rect(4, 8, 13, 10, Y, 1)
    c.rect(13, 9, 14, 10, Y, 2)
    c.hline(4, 14, 11, Y, 3)
    c.dither(5, 9, 7, 9, Y, 2)  # grille hint
    # exhaust stack
    c.vline(5, 3, 6, G, 2)
    c.put(5, 3, G, 0)
    c.put(6, 4, G, 3)
    # lift arm + blade (left)
    c.hline(2, 4, 10, G, 2)
    c.rect(0, 8, 1, 14, G, 1)
    c.vline(0, 8, 14, G, 0)
    c.vline(2, 9, 14, G, 3)
    c.put(1, 15, G, 3)
    c.hline(0, 1, 15, G, 2)
    # tracks
    c.rect(4, 12, 16, 16, K, 2)
    c.hline(5, 15, 12, K, 1)
    c.hline(5, 15, 16, K, 3)
    c.put(4, 12, K, 3); c.put(16, 12, K, 3); c.put(4, 16, K, 3); c.put(16, 16, K, 3)
    for x in (6, 9, 12, 15):
        c.put(x, 14, G, 1)
        c.put(x - 1, 14, K, 3)
    for x in range(5, 16, 2):
        c.put(x, 13, K, 0)
    return c


def road():
    c = Canvas()
    A, W, L, O, V = 'asphalt', 'paint', 'line', 'cone', 'grass'
    # road running away from the viewer: wide at the bottom, narrow at top
    rows = {}
    for y in range(2, 17):
        half = 2 + (y - 2) * 6 // 14       # 2 .. 8
        x0, x1 = 8 - half, 9 + half
        rows[y] = (x0, x1)
        c.hline(x0, x1, y, A, 0 if y < 5 else (1 if y < 11 else 2))
        c.put(x1, y, A, 3)
        c.put(x0 + 1, y, W, 0 if y < 9 else 1)
        c.put(x1 - 1, y, W, 1 if y < 9 else 2)
    c.hline(0, 17, 17, A, 3)
    # centre dashes grow toward the viewer
    for (y0, y1) in ((3, 3), (6, 7), (10, 12), (15, 17)):
        for y in range(y0, min(y1, 17) + 1):
            c.put(8, y, L, 0 if y < 9 else 1)
            c.put(9, y, L, 1 if y < 9 else 2)
    # traffic cone on the right shoulder
    c.rows(13, 9, [
        "..o..",
        ".oO..",
        ".wW..",
        ".oOO.",
        "owwW.",
        "oOOOO",
        "ppppp",
    ], {'o': (O, 0), 'O': (O, 2), 'w': (W, 0), 'W': (W, 2), 'p': (O, 3)})
    return c


def coal():
    c = Canvas()
    P, M, S, Y, G = 'brick', 'metal', 'smoke', 'gold', 'rubber'
    # smoke puffs drifting up-left from the stack
    c.rows(8, 0, [
        "..ss..ss.",
        ".sSSssSSs",
        "sSSSSSSss",
        ".ssss.ss.",
    ], {'s': (S, 2), 'S': (S, 0)})
    # smokestack (round: lit left, shaded right) with metal bands
    c.rect(11, 4, 14, 16, P, 1)
    c.vline(11, 4, 16, P, 0)
    c.vline(14, 4, 16, P, 2)
    c.hline(11, 14, 4, M, 2)
    for y in (7, 11):
        c.hline(11, 14, y, M, 1)
        c.put(11, y, M, 0)
        c.put(14, y, M, 2)
    # plant hall: lit roof, brick front, shaded right side
    c.hline(1, 9, 7, M, 0)
    c.hline(1, 9, 8, M, 1)
    c.rect(1, 9, 9, 16, P, 1)
    c.vline(1, 9, 16, P, 0)
    c.dither(2, 9, 9, 16, P, 2, phase=1)
    c.rect(10, 8, 10, 16, P, 3)
    # gold bolt on a dark panel: "this makes power"
    c.rect(3, 10, 8, 15, G, 2)
    c.rows(3, 10, [
        "....Y.",
        "...YY.",
        "..YYYY",
        ".YYYy.",
        "...Yy.",
        "..Y...",
    ], {'Y': (Y, 1), 'y': (Y, 2)})
    c.hline(0, 16, 17, G, 3)
    return c


def police():
    c = Canvas()
    N_, W, B, K, R, Y, M = 'navy', 'paint', 'glass', 'rubber', 'roof', 'gold', 'metal'
    # light bar
    c.rect(6, 3, 8, 4, R, 1)
    c.put(6, 3, R, 0)
    c.rect(9, 3, 11, 4, B, 1)
    c.put(11, 3, B, 0)
    c.hline(6, 11, 5, M, 3)
    # cabin roof (white) and windows
    c.hline(5, 12, 6, W, 0)
    c.rect(4, 7, 13, 9, W, 1)
    c.rect(5, 7, 7, 9, B, 1)
    c.put(5, 7, B, 0)
    c.rect(9, 7, 12, 9, B, 1)
    c.put(9, 7, B, 0)
    c.vline(8, 7, 9, W, 1)
    c.vline(13, 7, 9, W, 2)
    # body: white doors over navy skirt
    c.rect(1, 10, 16, 11, W, 1)
    c.hline(1, 16, 10, W, 0)
    c.rect(1, 12, 16, 13, N_, 1)
    c.hline(1, 16, 14, N_, 3)
    c.vline(16, 10, 13, N_, 2)
    c.put(0, 11, M, 1); c.put(0, 12, M, 2)  # bumper / headlight
    c.put(17, 11, R, 2)  # tail light
    c.put(17, 12, M, 2)
    c.put(1, 11, Y, 0)   # headlight
    # door star
    c.put(9, 11, Y, 1); c.put(8, 12, Y, 2); c.put(10, 12, Y, 2); c.put(9, 12, Y, 0)
    c.vline(7, 10, 13, N_, 3)
    # wheels
    for wx in (3, 12):
        c.rect(wx, 13, wx + 2, 16, K, 2)
        c.hline(wx, wx + 2, 13, K, 1)
        c.put(wx + 1, 14, M, 1)
        c.put(wx + 1, 15, M, 2)
        c.hline(wx, wx + 2, 16, K, 3)
    return c


def query():
    c = Canvas()
    Y, B, T = 'gold', 'glass', 'wood'
    ring = [
        ".....yyyy.....",
        "...yYYYYYYy...",
        "..yYgggggYYd..",
        ".yYgGGggggYYd.",
        ".yYgGgggggg Yd",
        "yYgGgggggggYYd",
        "yYggggggggbYYd",
        "yYggggggggbYYd",
        "yYgggggggbbYYd",
        ".yYgggggbbYYd.",
        ".dYYgggbbYYdd.",
        "..dYYYYYYYdd..",
        "...ddYYYYdd...",
        ".....dddd.....",
    ]
    leg = {'y': (Y, 0), 'Y': (Y, 1), 'd': (Y, 3), 'g': (B, 1), 'G': (B, 0),
           'b': (B, 2), ' ': (Y, 1), '.': None}
    c.rows(0, 0, ring, leg)
    # handle from lower right, 45 degrees, chunky
    for k in range(5):
        x, y = 12 + k, 12 + k
        c.put(x, y, T, 1)
        c.put(x + 1, y, T, 2) if x + 1 < 18 else None
        c.put(x, y + 1, T, 3) if y + 1 < 18 else None
        c.put(x - 1, y, T, 0) if k else None
    c.put(11, 12, Y, 3); c.put(12, 11, Y, 3)  # collar
    c.put(12, 12, Y, 2)
    # a little ? glint inside the lens (reads as Query)
    c.rows(4, 3, [
        ".WW.",
        "W..W",
        "..W.",
        ".W..",
        "....",
        ".W..",
    ], {'W': ('paint', 0)})
    return c


GLYPH = {
    'R': ["###.", "#..#", "#..#", "###.", "#.#.", "#..#"],
    'C': [".###", "#...", "#...", "#...", "#...", ".###"],
    'I': ["####", ".##.", ".##.", ".##.", ".##.", "####"],
}


def badge(c, x, y, letter, mat, ink=('paint', 0)):
    """8x8 zone sign: zone colour, dark rim, 4x6 letter with side padding."""
    c.rect(x, y, x + 7, y + 7, mat, 3)
    c.rect(x + 1, y + 1, x + 6, y + 6, mat, 1)
    for j, row in enumerate(GLYPH[letter]):
        for i, ch in enumerate(row):
            if ch == '#':
                c.put(x + 2 + i, y + 1 + j, *ink)


def poly(c, pts, mat, s):
    m = Image.new('L', (c.n, c.n), 0)
    ImageDraw.Draw(m).polygon(pts, fill=255)
    for y in range(c.n):
        for x in range(c.n):
            if m.getpixel((x, y)):
                c.put(x, y, mat, s)


def ball(c, cx, cy, r, mat, dither=True, bias=0):
    """Round blob lit from the top left (tree crowns, puffs)."""
    for y in range(c.n):
        for x in range(c.n):
            dx, dy = x + 0.5 - cx, y + 0.5 - cy
            d = math.hypot(dx, dy)
            if d <= r:
                lit = (-dx - dy) / (r * 1.41)
                s = 0 if lit > 0.45 else 1 if lit > -0.1 else 2 if d < r - 1 or lit > -0.5 else 3
                if dither and s in (1, 2) and (x + y) % 2 == 0 and abs(lit - (0.45 if s == 1 else -0.1)) < 0.18:
                    s -= 1
                c.put(x, y, mat, min(3, s + bias))


def ellipse(c, cx, cy, rx, ry, mat, s):
    for y in range(c.n):
        for x in range(c.n):
            if ((x + 0.5 - cx) / rx) ** 2 + ((y + 0.5 - cy) / ry) ** 2 <= 1:
                c.put(x, y, mat, s)


# ---------------------------------------------------------------- transport
def rail():
    """A commuter rail car on its track, seen from the side at 3/4."""
    c = Canvas()
    B = 'wall'
    # roof (lit) and a sliver of the back side
    c.hline(3, 15, 3, B, 0)
    c.hline(2, 16, 4, B, 0)
    c.rect(1, 5, 16, 12, B, 1)
    c.vline(1, 5, 12, B, 0)
    c.vline(16, 5, 12, B, 2)
    c.rect(17, 6, 17, 12, B, 3)           # end face
    # windows band
    for x0 in (3, 7, 11):
        c.rect(x0, 6, x0 + 2, 8, 'glass', 1)
        c.put(x0, 6, 'glass', 0)
    c.rect(15, 6, 15, 10, 'glass', 2)     # door window
    # accent stripe
    c.hline(1, 16, 10, 'cone', 1)
    c.hline(1, 16, 11, 'cone', 2)
    c.hline(1, 17, 12, B, 3)
    # bogies + wheels
    for wx in (3, 12):
        c.rect(wx, 13, wx + 3, 14, 'rubber', 2)
        c.put(wx, 14, 'metal', 1); c.put(wx + 3, 14, 'metal', 1)
    # track: rail + sleepers
    c.hline(0, 17, 15, 'metal', 0)
    for x in range(0, 18, 3):
        c.rect(x, 16, x + 1, 16, 'wood', 2)
    c.hline(0, 17, 17, 'stone', 3)
    return c


# ---------------------------------------------------------------- utilities
def wire():
    c = Canvas()
    W = 'wood'
    # wires first, sagging off both sides
    for (y, xs) in ((4, range(0, 18)), (9, range(0, 18))):
        for x in xs:
            sag = 1 if 3 < x < 14 and x not in (7, 8, 9, 10) else 0
            c.put(x, y + sag, 'stone', 3)
            c.hard.add((x, y + sag))
    # pole
    c.rect(8, 2, 9, 17, W, 1)
    c.vline(8, 2, 17, W, 0)
    c.vline(9, 2, 17, W, 2)
    # cross arms
    c.rect(2, 5, 15, 5, W, 1); c.hline(2, 15, 6, W, 3)
    c.rect(4, 10, 13, 10, W, 1); c.hline(4, 13, 11, W, 3)
    # insulators (gold accent)
    for x in (3, 6, 11, 14):
        c.rect(x, 3, x, 4, 'gold', 1); c.put(x, 3, 'gold', 0)
    for x in (5, 12):
        c.rect(x, 8, x, 9, 'gold', 1); c.put(x, 8, 'gold', 0)
    c.hline(7, 10, 17, W, 3)
    return c


def nuclear():
    c = Canvas()
    T = 'stone'
    for y in range(4, 17):
        if y < 10:
            half = 5 - (y - 4) * 1.4 / 6
        else:
            half = 3.6 + (y - 10) * 3.4 / 6
        x0, x1 = round(8.5 - half), round(8.5 + half) - 1
        for x in range(x0, x1 + 1):
            t = (x - x0) / max(1, x1 - x0)
            s = 0 if t < 0.2 else 1 if t < 0.55 else 2 if t < 0.85 else 3
            c.put(x, y, T, s)
    c.hline(4, 12, 4, T, 0)
    c.hline(5, 11, 5, T, 3)  # inner rim shadow
    # steam
    for (cx, cy, r) in ((7, 2.2, 2.3), (10.5, 1.8, 2.0), (12.8, 3.0, 1.4)):
        ball(c, cx, cy, r, 'smoke', dither=False)
    # trefoil sign (gold accent)
    c.rows(5, 9, [
        ".YYYYY.",
        "YooYooY",
        "YooYooY",
        "YYYoYYY",
        "YYoooYY",
        "YYoooYY",
        ".YYYYY.",
    ], {'Y': ('gold', 1), 'o': ('ink', 0)})
    c.hline(1, 16, 17, T, 3)
    return c


# ---------------------------------------------------------------- zones
def residential():
    c = Canvas()
    W, R = 'wall', 'roofs'
    ax = 10
    for y in range(3, 9):                       # front gable
        k = y - 3
        c.hline(ax - k, ax + k, y, W, 1)
        c.put(ax - k - 1, y, R, 0)              # bargeboard trim
        x0, x1 = ax + k + 1, min(17, ax + k + 4)
        c.hline(x0, x1, y, R, 1 if y % 2 else 2)  # roof plane, shingle rows
    c.hline(ax, ax + 4, 2, R, 0)                # ridge
    c.hline(ax - 6, ax - 5, 9, R, 0)
    c.rect(ax - 4, 9, ax + 4, 16, W, 1)         # front wall
    c.vline(ax - 4, 9, 16, W, 0)
    c.rect(ax + 5, 9, 17, 16, W, 3)             # side wall
    c.dither(ax + 5, 9, 17, 9, W, 2)
    c.hline(ax - 4, 17, 9, W, 2)                # eave shadow
    c.rect(14, 0, 15, 2, 'stone', 1); c.vline(15, 0, 2, 'stone', 2)  # chimney
    c.rect(ax - 1, 12, ax, 16, 'door', 1); c.vline(ax, 12, 16, 'door', 2)
    for wx in (ax - 3, ax + 2):
        c.rect(wx, 11, wx + 1, 12, 'glass', 1); c.put(wx, 11, 'glass', 0)
    c.rect(ax + 6, 11, ax + 6, 12, 'glass', 3)
    c.vline(ax, 5, 6, 'glass', 2)
    c.hline(0, 17, 17, 'zr', 1)
    c.dither(0, 17, 17, 17, 'zr', 2)
    badge(c, 0, 0, 'R', 'zr')
    return c


def commercial():
    c = Canvas()
    W = 'wall'
    # box: front face, lit roof, shaded right side
    c.rect(3, 8, 13, 16, W, 1)
    c.vline(3, 8, 16, W, 0)
    poly(c, [(3, 7), (13, 7), (16, 4), (6, 4)], W, 0)
    c.hline(7, 16, 4, W, 0)
    c.hline(4, 13, 7, W, 2)
    poly(c, [(14, 7), (17, 4), (17, 13), (14, 16)], W, 3)
    c.dither(14, 6, 17, 12, W, 2)
    # striped awning in the zone blue, scalloped edge
    for x in range(3, 14):
        m = 'zc' if ((x - 3) // 2) % 2 == 0 else 'paint'
        c.put(x, 9, m, 0)
        c.put(x, 10, m, 1)
        if x % 2 == 1:
            c.put(x, 11, m, 2)
    c.hline(2, 14, 8, 'zc', 3)
    # shop window + door
    c.rect(4, 12, 9, 15, 'glass', 1)
    c.hline(4, 9, 12, 'glass', 0)
    c.put(8, 13, 'glass', 0)
    c.rect(11, 12, 12, 16, 'door', 2); c.put(11, 14, 'door', 0)
    c.hline(3, 13, 16, W, 2)
    badge(c, 0, 0, 'C', 'zc')
    return c


def industrial():
    c = Canvas()
    M = 'stone'
    # short stack with a puff
    c.rect(13, 3, 15, 16, M, 1); c.vline(13, 3, 16, M, 0); c.vline(15, 3, 16, M, 2)
    c.hline(13, 15, 3, M, 3)
    c.hline(13, 15, 6, 'zi', 1)
    ball(c, 12.5, 1.5, 1.6, 'smoke', dither=False)
    ball(c, 15.0, 1.2, 1.3, 'smoke', dither=False)
    # sawtooth hall
    c.rect(1, 9, 12, 16, M, 1)
    c.vline(1, 9, 16, M, 0)
    c.hline(1, 12, 9, M, 3)
    c.hline(1, 6, 8, M, 0)
    for x0 in (6, 9):
        c.vline(x0, 5, 8, 'glass', 1)           # north-light glazing
        c.put(x0, 5, 'glass', 0)
        for k in range(1, 4):
            c.put(x0 + k, 5 + k - 1, M, 0)     # sloped roof falls to the right
            c.vline(x0 + k, 5 + k, 8, M, 2)
    # loading door with yellow hazard stripe
    c.rect(6, 12, 10, 16, 'ink', 0)
    for x in range(6, 11):
        c.put(x, 12, 'zi', 0 if x % 2 else 3)
    c.rect(7, 13, 9, 16, M, 3)
    c.rect(2, 11, 4, 12, 'glass', 2)
    c.hline(0, 16, 17, M, 3)
    badge(c, 0, 0, 'I', 'zi', ink=('ink', 0))
    return c


# ---------------------------------------------------------------- services
def span_shade(c, y, x0, x1, mat, lift=0):
    """Fill a row and shade it as a lit-from-the-left cylinder."""
    w = max(1, x1 - x0)
    for x in range(x0, x1 + 1):
        t = (x - x0) / w
        sh = 0 if t < 0.2 else 1 if t < 0.6 else 2 if t < 0.88 else 3
        c.put(x, y, mat, max(0, min(3, sh + lift)))


def fire():
    """Hydrant: small square nut, low rounded dome, flange, body, side nozzles."""
    c = Canvas()
    R = 'roof'
    c.hline(7, 10, 1, R, 1)                  # operating nut
    c.put(7, 1, R, 0)
    span_shade(c, 2, 5, 12, R, lift=1)       # low dome, flat on top
    span_shade(c, 3, 4, 13, R, lift=1)
    span_shade(c, 4, 4, 13, R)
    span_shade(c, 5, 3, 14, R, lift=-1)      # bonnet flange (lit top)
    c.hline(3, 14, 6, R, 3)                  # flange underside
    for y in range(7, 15):
        span_shade(c, y, 5, 12, R)
    for y in (9, 10, 11):                    # side nozzles, symmetric
        span_shade(c, y, 2, 15, R)
    c.put(1, 10, 'metal', 1); c.put(16, 10, 'metal', 2)
    c.put(8, 10, R, 3); c.put(9, 10, R, 3)   # centre cap boss
    span_shade(c, 15, 4, 13, R)              # base collar
    span_shade(c, 16, 3, 14, R, lift=-1)
    c.hline(3, 14, 17, R, 3)
    return c


def stadium():
    """American football seen from the side, laces on top."""
    c = Canvas()
    B = 'wood'
    cx, cy, rx, ry = 9.0, 9.0, 8.9, 5.8
    for y in range(18):
        for x in range(18):
            u = (x + 0.5 - cx) / rx
            if abs(u) >= 1:
                continue
            h = ry * (1 - u * u) ** 1.0
            dy = y + 0.5 - cy
            if abs(dy) <= h:
                lit = -(u * 0.45) - dy / h * 0.8
                sh = 0 if lit > 0.45 else 1 if lit > -0.1 else 2 if lit > -0.6 else 3
                c.put(x, y, B, sh)
    for x in (4, 13):                          # end stripes
        for y in range(18):
            if c.px[y][x] is not None:
                c.put(x, y, 'paint', 1 if x < 9 else 2)
    c.hline(6, 11, 6, 'paint', 1)              # seam
    for x in (6, 8, 9, 11):                    # laces cross the seam
        c.vline(x, 5, 7, 'paint', 0)
    return c


# ---------------------------------------------------------------- big builds
def seaport():
    c = Canvas()
    N = 'navy'
    # bridge tower at the stern
    c.rect(12, 3, 15, 9, 'paint', 1); c.vline(12, 3, 9, 'paint', 0); c.vline(15, 3, 9, 'paint', 2)
    c.hline(12, 15, 5, 'glass', 1)
    c.rect(13, 1, 13, 2, 'stone', 2)
    # containers
    for (x0, y0, s) in ((2, 7, 1), (6, 7, 2), (4, 5, 0), (8, 5, 1), (2, 9, 2), (6, 9, 1)):
        c.rect(x0, y0, x0 + 3, y0 + 1, 'stone', s)
        c.put(x0 + 3, y0, 'stone', 3)
    c.rect(10, 7, 11, 10, 'stone', 2)
    # hull (accent navy) with raised bow on the left
    poly(c, [(0, 9), (17, 10), (16, 14), (3, 14)], N, 1)
    c.hline(1, 17, 11, N, 0)
    c.hline(3, 16, 14, N, 3)
    c.hline(2, 16, 12, 'roof', 2)
    # water
    for x in range(0, 18):
        c.put(x, 15 + (x % 4 == 0), 'glass', 1 if x % 3 else 0)
        if x % 2:
            c.put(x, 17, 'glass', 2)
    return c


def airport():
    """Airliner in side view, nose right: thick fuselage, broad swept wing
    toward the viewer, big engine, window row, tail fin. Accent: navy."""
    c = Canvas()
    B = 'paint'
    # tail fin, swept back, navy tip
    fin = {1: (1, 2), 2: (1, 3), 3: (1, 4), 4: (2, 5), 5: (2, 6), 6: (2, 3)}
    for y, (a, b) in fin.items():
        c.hline(a, b, y, B, 1)
        c.put(a, y, B, 0)
    c.hline(1, 2, 1, 'navy', 1); c.hline(1, 3, 2, 'navy', 1); c.hline(1, 4, 3, 'navy', 2)
    # fuselage rows 6..11, rounded nose at the right
    c.hline(4, 14, 6, B, 0)
    c.hline(1, 16, 7, B, 1)
    c.hline(1, 17, 8, B, 1)
    c.hline(1, 17, 9, 'navy', 1)             # cheatline
    c.hline(1, 16, 10, B, 2)
    c.hline(3, 14, 11, B, 3)
    c.put(1, 7, B, 0)
    c.put(15, 7, 'glass', 2); c.put(16, 7, 'glass', 3)   # cockpit
    c.put(16, 8, 'glass', 2)
    for x in range(4, 14, 2):                            # cabin windows
        c.put(x, 7, 'glass', 1)
    c.hline(0, 3, 10, B, 2)                  # tailplane
    c.put(0, 10, B, 3)
    # broad wing, swept back toward the viewer
    poly(c, [(6, 11), (11, 11), (7, 16), (4, 16)], B, 1)
    c.hline(4, 7, 16, B, 3)
    for y in range(12, 16):                  # lit leading edge
        c.put(max(4, 6 - (y - 11) // 2), y, B, 0)
    # engine under the wing
    c.rect(10, 12, 14, 14, 'stone', 2)
    c.hline(10, 13, 12, 'stone', 0)
    c.vline(14, 12, 14, 'stone', 3)
    c.hline(10, 14, 14, 'stone', 3)
    c.put(14, 13, 'ink', 0)                  # intake
    return c


def special():
    """Clean symmetric 5-point star, two-tone bevel and a lit edge."""
    c = Canvas()
    G = 'gold'
    cx, cy = 9.0, 9.6
    # Hand-placed silhouette, symmetric about x=8.5. Two-pixel tip, calm
    # stair steps on each diagonal, broad bar and two clean lower points.
    spans = [
        [(8, 9)], [(8, 9)], [(8, 9)], [(7, 10)], [(7, 10)], [(6, 11)],
        [(0, 17)], [(1, 16)], [(2, 15)], [(4, 13)], [(5, 12)], [(5, 12)],
        [(4, 13)], [(4, 7), (10, 13)], [(3, 6), (11, 14)],
        [(3, 5), (12, 14)], [(2, 4), (13, 15)], [(2, 3), (14, 15)],
    ]
    for y, rr in enumerate(spans):
        for x0, x1 in rr:
            c.hline(x0, x1, y, G, 1)
    # bevel: each arm has a lit side and a shaded side, split on its ridge.
    for y in range(18):
        for x in range(18):
            if c.px[y][x] is None:
                continue
            dx, dy = x + 0.5 - cx, y + 0.5 - cy
            a = math.atan2(dy, dx)
            k = round((a + math.pi / 2) / (2 * math.pi / 5)) % 5
            arm = -math.pi / 2 + k * 2 * math.pi / 5
            side = math.sin(a - arm)            # >0 clockwise of the ridge
            lit_arm = k in (0, 4)                # top and upper-left arms face the light
            if lit_arm:
                sh = 0 if side < 0 else 1
            elif k == 1:
                sh = 1 if side < 0 else 2
            else:
                sh = 1 if side > 0 else 2
            if dx * dx + dy * dy < 2.2:
                sh = 1
            c.put(x, y, G, sh)
    return c


# ---------------------------------------------------------------- landmarks
def amusement():
    c = Canvas()
    M = 'stone'
    cx, cy, r = 9, 7.5, 6.5
    for t in range(0, 360, 4):
        a = math.radians(t)
        x, y = cx + r * math.cos(a), cy + r * math.sin(a)
        c.put(int(x), int(y), M, 1 if t > 180 else 2)
    for t in range(0, 360, 45):
        a = math.radians(t + 22)
        for k in range(1, 6):
            c.put(int(cx + k * math.cos(a)), int(cy + k * math.sin(a)), M, 2)
    # legs
    for k in range(10):
        c.put(int(cx - 0.5 - k * 0.55), int(cy + 1 + k), M, 0)
        c.put(int(cx + k * 0.55), int(cy + 1 + k), M, 2)
    c.hline(2, 15, 17, M, 3)
    # cabins (red accent)
    for t in range(0, 360, 45):
        a = math.radians(t)
        x, y = int(cx + r * math.cos(a)), int(cy + r * math.sin(a))
        c.rect(x - 1, y, x, y + 1, 'roof', 1)
        c.put(x - 1, y, 'roof', 0)
    c.rect(8, 7, 9, 8, M, 0)  # hub
    return c


def park():
    c = Canvas()
    ellipse(c, 9, 15.8, 8.4, 2.4, 'grass', 2)
    ellipse(c, 9, 15.2, 8.0, 1.8, 'grass', 1)
    c.rect(8, 10, 9, 15, 'wood', 1); c.vline(9, 10, 15, 'wood', 2)
    ball(c, 9, 6.5, 5.8, 'grass')
    return c


def bigpark():
    c = Canvas()
    ellipse(c, 9, 15.6, 8.8, 2.6, 'grass', 2)
    ellipse(c, 9, 15.0, 8.4, 2.0, 'grass', 1)
    for x in (4, 12):
        c.rect(x, 9, x, 14, 'wood', 2)
    ball(c, 12.5, 5.2, 4.6, 'grass', bias=1)
    ball(c, 4.6, 6.2, 4.2, 'grass', bias=1)
    c.rect(8, 12, 9, 15, 'wood', 1)
    ball(c, 8.8, 9.6, 4.0, 'grass')
    # little fence in front
    for x in range(1, 17, 3):
        c.vline(x, 15, 16, 'wood', 0)
    c.hline(1, 16, 16, 'wood', 1)
    return c


def casino():
    c = Canvas()
    D = 'roof'
    c.rect(2, 7, 11, 16, D, 1)
    c.vline(2, 7, 16, D, 0)
    poly(c, [(2, 6), (11, 6), (15, 2), (6, 2)], D, 0)
    poly(c, [(12, 6), (15, 3), (15, 12), (12, 15)], D, 2)
    c.vline(15, 3, 12, D, 3)
    c.hline(3, 11, 16, D, 3)
    P = ('paint', 0)
    for (x, y) in ((3, 8), (9, 8), (6, 11), (3, 14), (9, 14)):
        c.rect(x, y, x + 1, y + 1, *P)
    c.rect(8, 3, 9, 4, 'paint', 1)      # top face pip
    c.put(13, 6, 'paint', 1); c.put(14, 5, 'paint', 1)   # side pips
    c.put(13, 11, 'paint', 1); c.put(14, 10, 'paint', 1)
    return c


def fountain():
    c = Canvas()
    S, Wt = 'stone', 'glass'
    ellipse(c, 9, 14.4, 8.6, 3.2, S, 2)
    ellipse(c, 9, 13.6, 8.6, 3.0, S, 1)
    ellipse(c, 9, 13.4, 7.0, 2.0, Wt, 1)
    c.dither(3, 13, 15, 14, Wt, 0)
    c.rect(8, 8, 9, 13, S, 1); c.vline(9, 8, 13, S, 2)
    ellipse(c, 9, 7.6, 4.6, 1.6, S, 1)
    c.hline(5, 12, 8, S, 2)
    c.hline(6, 11, 7, Wt, 1)
    # spray
    for (x, y) in ((8, 1), (9, 1), (8, 2), (9, 2), (8, 3), (9, 4), (7, 2), (10, 2), (6, 3), (11, 3),
                   (5, 4), (12, 4), (4, 6), (13, 6), (4, 5), (13, 5), (3, 8), (14, 8), (3, 10), (14, 10)):
        c.put(x, y, Wt, 0 if y < 4 else 1)
    return c


def library():
    c = Canvas()
    Cv = 'roof'
    # cover (accent) under the pages, then the open pages
    poly(c, [(0, 6), (8, 8), (9, 8), (17, 6), (17, 14), (9, 16), (8, 16), (0, 14)], Cv, 2)
    c.hline(0, 1, 14, Cv, 3)
    poly(c, [(1, 4), (8, 6), (8, 14), (1, 12)], 'paint', 0)
    poly(c, [(9, 6), (16, 4), (16, 12), (9, 14)], 'paint', 1)
    c.vline(8, 6, 15, 'paint', 2); c.vline(9, 6, 15, 'paint', 3)
    for k, y in enumerate((7, 9, 11)):
        for x in range(2, 8):
            c.put(x, y + (x - 1) * 2 // 7, 'stone', 2)
        for x in range(10, 16):
            c.put(x, y + (16 - x) * 2 // 7, 'stone', 2)
    c.vline(12, 1, 6, Cv, 1); c.vline(13, 1, 5, Cv, 2)   # ribbon marker
    c.put(12, 7, Cv, 1)
    return c


def marina():
    c = Canvas()
    c.vline(9, 1, 12, 'wood', 1)
    poly(c, [(10, 2), (10, 11), (16.5, 11)], 'paint', 0)
    poly(c, [(8, 3), (8, 11), (2, 11)], 'paint', 1)
    c.dither(11, 7, 15, 10, 'paint', 1)
    c.vline(8, 4, 10, 'paint', 2)
    poly(c, [(1, 12), (17, 12), (15, 15), (3, 15)], 'roof', 1)
    c.hline(2, 16, 12, 'roof', 0)
    c.hline(3, 15, 15, 'roof', 3)
    c.hline(2, 16, 13, 'paint', 1)
    for x in range(0, 18):
        c.put(x, 16 + (x % 4 == 0), 'glass', 1 if x % 3 else 0)
    return c


def mayor():
    c = Canvas()
    W = 'wall'
    c.vline(8, 0, 4, 'wood', 2)
    c.rect(9, 0, 12, 2, 'roof', 1); c.hline(9, 12, 0, 'roof', 0); c.put(12, 2, 'roof', 2)
    # house body
    c.rect(1, 8, 16, 16, W, 1)
    c.vline(1, 8, 16, W, 0)
    c.vline(16, 8, 16, W, 3)
    poly(c, [(0, 8), (3, 5), (14, 5), (17, 8)], 'roofs', 1)
    c.hline(3, 14, 5, 'roofs', 0)
    # portico + pediment
    for k in range(4):
        c.hline(8 - k - 1, 9 + k + 1, 4 + k, W, 0 if k < 3 else 2)
    c.put(8, 5, 'glass', 2); c.put(9, 5, 'glass', 2)
    c.hline(4, 13, 8, W, 2)
    for x in (5, 8, 10, 13):
        c.vline(x, 9, 15, 'paint', 0)
        c.vline(x + 1, 9, 15, W, 2)
    c.rect(2, 10, 3, 12, 'glass', 1); c.rect(14, 10, 15, 12, 'glass', 2)
    c.hline(0, 17, 16, W, 3)
    c.hline(4, 13, 17, W, 2)
    return c


def prison():
    c = Canvas()
    S = 'stone'
    c.rect(1, 6, 12, 16, S, 1)
    c.vline(1, 6, 16, S, 0)
    c.hline(1, 12, 6, S, 0)
    for y in range(8, 16, 2):  # block courses
        for x in range(2 + (y // 2) % 2 * 2, 12, 4):
            c.put(x, y, S, 2)
    c.rect(3, 8, 9, 13, 'ink', 0)
    for x in (4, 6, 8):
        c.vline(x, 8, 13, 'metal', 0)
    c.hline(3, 9, 10, 'metal', 1)
    # watchtower with lamp (yellow accent)
    c.rect(13, 5, 15, 16, S, 2); c.vline(13, 5, 16, S, 1); c.vline(15, 5, 16, S, 3)
    c.rect(12, 2, 16, 4, S, 1); c.hline(12, 16, 2, S, 0)
    c.rect(13, 3, 15, 3, 'yellow', 0)
    c.put(14, 1, S, 2)
    c.hline(0, 16, 17, S, 3)
    return c


def zoo():
    c = Canvas()
    Y = 'yellow'
    # legs
    for x in (7, 9, 13, 15):
        c.vline(x, 11, 16, Y, 2 if x in (9, 15) else 1)
        c.put(x, 17, 'stone', 3)
    # body
    c.rect(7, 8, 15, 11, Y, 1)
    c.hline(8, 15, 8, Y, 0)
    c.hline(7, 15, 11, Y, 2)
    c.put(16, 8, Y, 2); c.put(17, 9, Y, 2); c.put(17, 10, Y, 3)  # tail
    # neck to the upper left
    for k in range(7):
        c.rect(5 + k // 2 - 0, 7 - k, 6 + k // 2 + 1, 7 - k, Y, 1)
        c.put(5 + k // 2, 7 - k, Y, 0)
    # head
    c.rect(1, 1, 5, 3, Y, 1); c.hline(2, 5, 1, Y, 0); c.put(1, 3, Y, 2)
    c.put(3, 0, 'stone', 3); c.put(5, 0, 'stone', 3)  # ossicones
    c.put(3, 2, 'ink', 0)
    # patches
    for (x, y) in ((9, 9), (12, 9), (14, 10), (10, 10), (7, 6), (6, 4), (8, 3)):
        c.put(x, y, Y, 3)
    c.put(13, 8, Y, 3)
    return c


SPRITES = {
    'bulldozer': (bulldozer, 'tool'), 'road': (road, 'transport'), 'rail': (rail, 'transport'),
    'wire': (wire, 'utility'), 'park': (park, 'tool'), 'residential': (residential, 'res'),
    'commercial': (commercial, 'com'), 'industrial': (industrial, 'ind'), 'police': (police, 'service'),
    'fire': (fire, 'service'), 'stadium': (stadium, 'landmark'), 'seaport': (seaport, 'landmark'),
    'coal': (coal, 'utility'), 'nuclear': (nuclear, 'utility'), 'airport': (airport, 'landmark'),
    'special': (special, 'landmark'), 'query': (query, 'tool'),
    'amusement': (amusement, 'landmark'), 'bigpark': (bigpark, 'landmark'), 'casino': (casino, 'landmark'),
    'fountain': (fountain, 'landmark'), 'library': (library, 'landmark'), 'marina': (marina, 'landmark'),
    'mayor': (mayor, 'landmark'), 'prison': (prison, 'landmark'), 'zoo': (zoo, 'landmark'),
}
TOOLBAR = ['bulldozer', 'road', 'rail', 'wire', 'park', 'residential', 'commercial', 'industrial',
           'police', 'fire', 'stadium', 'seaport', 'coal', 'nuclear', 'airport', 'special', 'query']
LANDMARKS = ['zoo', 'fountain', 'library', 'mayor', 'casino', 'amusement', 'prison', 'marina', 'bigpark']
ACCENTS = {
    'bulldozer': {'yellow'}, 'road': {'line', 'cone'}, 'rail': {'cone'}, 'wire': {'gold'},
    'park': {'grass'}, 'residential': {'zr'}, 'commercial': {'zc'}, 'industrial': {'zi'},
    'police': {'navy', 'roof'}, 'fire': {'roof'}, 'stadium': {'wood'}, 'seaport': {'navy'},
    'coal': {'gold', 'brick'}, 'nuclear': {'gold'}, 'airport': {'navy'}, 'special': {'gold'},
    'query': {'gold'}, 'amusement': {'roof'}, 'bigpark': {'grass'}, 'casino': {'roof'},
    'fountain': {'glass'}, 'library': {'roof'}, 'marina': {'roof'}, 'mayor': {'roof'},
    'prison': {'yellow'}, 'zoo': {'yellow'},
}

SEPIA = [hx(c) for c in ('#F6EEDC', '#D4C6A8', '#A49274', '#6C5C46')]
STONE = [hx(c) for c in ('#C8BEAA', '#A89A82', '#82725A', '#5E4E3C')]
ROOFS = [hx(c) for c in ('#B8AA92', '#94846C', '#72604A', '#4E4032')]
SLATE = [hx(c) for c in ('#C4D2DC', '#8CA2B4', '#5E7488', '#3C4A5C')]
ROAD = [hx(c) for c in ('#8E8272', '#786C5E', '#605648', '#483E34')]
INK = hx('#2E241C')
ZONE = {
    'zr': [hx(c) for c in ('#8ECA68', '#3E8C36', '#28622C', '#1E4420')],
    'zc': [hx(c) for c in ('#8AAAE8', '#3264B8', '#234686', '#182E62')],
    'zi': [hx(c) for c in ('#F6DE78', '#E0BA42', '#B48A28', '#7C5C1C')],
}


def colour(name, mat, shade):
    if mat == 'ink':
        return INK
    if mat in ZONE:
        return ZONE[mat][shade]
    if mat in ACCENTS[name]:
        return mix(RAMPS[mat][shade], SEPIA[shade], 0.15)  # muted toward stone
    if mat == 'asphalt':
        return ROAD[shade]
    if mat == 'glass':
        return SLATE[shade]
    if mat == 'roofs':
        return ROOFS[shade]
    if mat == 'stone':
        return STONE[shade]
    return SEPIA[shade]


def render(name):
    cv = SPRITES[name][0]()
    img = Image.new('RGBA', (ICON, ICON), (0, 0, 0, 0))
    for y in range(N):
        for x in range(N):
            p = cv.px[y][x]
            if p is not None:
                img.putpixel((x + 1, y + 1), colour(name, *p) + (255,))
    for y in range(ICON):
        for x in range(ICON):
            sx, sy = x - 1, y - 1
            if 0 <= sx < N and 0 <= sy < N and cv.px[sy][sx] is not None:
                continue
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nx, ny = sx + dx, sy + dy
                if 0 <= nx < N and 0 <= ny < N and cv.px[ny][nx] is not None and (nx, ny) not in cv.hard:
                    img.putpixel((x, y), INK + (255,))
                    break
    return img.resize((ICON * SCALE, ICON * SCALE), Image.NEAREST)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--out', type=pathlib.Path, help='write PNGs here instead of beside this script')
    ap.add_argument('--check', action='store_true', help='fail if a committed PNG differs')
    args = ap.parse_args()
    out = args.out or ROOT
    out.mkdir(parents=True, exist_ok=True)
    bad = []
    for name in TOOLBAR + LANDMARKS:
        img = render(name)
        if args.check:
            path = ROOT / f'{name}.png'
            try:
                have = Image.open(path).convert('RGBA')
            except OSError:
                bad.append(name)
                continue
            if have.size != img.size or have.tobytes() != img.tobytes():
                bad.append(name)
        else:
            img.save(out / f'{name}.png', optimize=True)
    if bad:
        print('icons differ from the generator: ' + ', '.join(bad), file=sys.stderr)
        return 1
    print(f'{len(TOOLBAR) + len(LANDMARKS)} icons ' + ('match' if args.check else f'written to {out}'))
    return 0


if __name__ == '__main__':
    sys.exit(main())
