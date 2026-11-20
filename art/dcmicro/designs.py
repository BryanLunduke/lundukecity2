# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Bryan Lunduke
# See COPYING and NOTICE.

"""Data Center = option B reworked (one wide glass-and-steel block, true
projection facade grid, roof grid continuing the facade bays) + the vacant 'D' lot;
Micro Nuclear = small 1x1 building with the 4x4 plant's atom emblem."""
import numpy as np
from PIL import Image
import px
K = {'K': 'ink', 'W': 'white', 'L': 'lgrey', 'G': 'grey', 'D': 'dgrey', 'Y': 'yellow', 'R': 'red', 'r': 'red_hot',
     'O': 'orange', 'g': 'led_g', 'b': 'glass', 'B': 'glass_d', 'c': 'checker', 'S': 'solar', 'l': 'solar_ln',
     'A': 'asphalt', 'P': 'paving', 'T': 'green', 't': 'dgreen', 'w': 'wall_wh', 'y': 'yellow_d'}

BAY = 5          # one structural bay, in pixels, on roof and facades alike
X0, Y0, X1, Y1, H = 13, 4, 43, 24, 10   # 6 x 4 bays exactly, so the roof edges land on bay lines


def tree(c, x, y):
    c.art([".TT", "TTt", "Ttt"], x, y, K, True, 2)


def dc_block(c, podium):
    """One wide block. Roof: solar panels in BAY x BAY cells framed by steel lines.
    The roof's u-lines continue down the south face as mullions (diagonal in the
    projection), its v-lines continue down the west face. Floor lines every 3 px of depth.
    podium: depth rows from which the facade turns into louvred steel (None = all glass)."""
    def roof(u, v):
        if u % BAY == 0 or v % BAY == 0 or u == X1 - X0 or v == Y1 - Y0:
            return 'lgrey'
        # one solar module per bay, tilted to the sun: lit top row, dark cells below
        return 'glass' if v % BAY == 1 else 'solar'
    def south(u, s):
        steel = podium is not None and s >= podium
        if u % BAY == 0 or u == X1 - X0:
            return 'grey' if not steel else 'dgrey'
        if steel:   # horizontal louvre slats = constant depth rows
            return 'dgrey' if s % 2 == 0 else 'wall_sh'
        if s % 3 == 0:
            return 'grey'
        return 'glass_d'
    def west(v, s):
        steel = podium is not None and s >= podium
        if v % BAY == 0 or v == Y1 - Y0:
            return 'lgrey' if not steel else 'grey'
        if steel:   # same slats on the west face (constant depth = vertical in screen)
            return 'grey' if s % 2 == 0 else 'wall_lit'
        if s % 3 == 0:
            return 'lgrey'
        # r3 fix: r2 drew glints where (v + s) % 7 == 0, i.e. on fixed screen rows, which read
        # as 3 flat white lines across this face. No glints now; the west face is built
        # only from the true-projection grid (diagonal mullions, vertical floor lines), like the south face.
        return 'glass'
    c.box(X0, Y0, X1, Y1, H, roof=roof, west=west, south=south)


def generators(c, y=39):
    for i in range(5):
        x = 8 + i * 6
        c.box(x, y + 1, x + 3, y + 3, 2, roof='roof', west='wall_lit', south='wall_sh')
        c.put(x + 3, y + 1, 'yellow'); c.put(x + 2, y + 2, 'ink')
    c.cylinder(42, y - 1, 2, 2, top='roof_l', lit='wall_lit', dark='wall_sh', hi='wall_wh')


def dc_b1():
    """B1: all-glass facades on true-projection bays, aligned solar roof, generator row + fuel tank."""
    c = px.Canvas(48, 48); c.frame()
    c.rect(3, 39, 44, 44, 'paving')
    dc_block(c, None)
    generators(c)
    c.shadow(4)
    return c


def dc_b2():
    """B2: glass upper floors over a louvred steel plinth (reads as plant, not office), aligned solar roof, generator row."""
    c = px.Canvas(48, 48); c.frame()
    c.rect(3, 39, 44, 44, 'paving')
    dc_block(c, 7)
    generators(c)
    c.shadow(4)
    return c


def dc_b3():
    """B3: as B2 but no generator row: security fence and a gated driveway instead."""
    c = px.Canvas(48, 48); c.frame()
    dc_block(c, 7)
    for x in range(3, 45):
        if x % 2 == 1 and not 18 <= x <= 25: c.put(x, 42, 'lgrey')
    c.rect(18, 38, 25, 45, 'asphalt')
    c.box(28, 39, 30, 41, 2, roof='roof_l', west='wall_lit', south='wall_sh')
    for (x, y) in [(4, 38), (9, 39), (36, 38), (41, 39)]:
        tree(c, x, y)
    c.shadow(4)
    return c


# ---------------------------------------------------------------- vacant 'D' lot
D_CLASSIC = ["aaabbbbbbbaaaaaa",
             "aaccbbcccbbaaaaa",
             "aaacbbaaccbbaaaa",
             "aaacbbaaacbbaaaa",
             "aaacbbaaacbbaaaa",
             "aaacbbaaacbbaaaa",
             "aaacbbaaacbbaaaa",
             "aaacbbaaacbbaaaa",
             "aaacbbaaacbbaaaa",
             "aaacbbaacbbaaaaa",
             "aaabbbbbbbaaaaaa",
             "aaccccccccaaaaaa"]   # rows 18..29, cols 15..30, same grid as the Classic 'R' (240-248)


def vacant_industrial(look):
    """The look's vacant industrial lot, atlas tiles 612-620 (INDBASE) as a 48x48 sheet."""
    im = np.array(Image.open(px.A + ('tiles.png' if look == 'classic' else 'tiles-svga.png')).convert('RGB'))
    out = np.zeros((48, 48, 3), np.uint8)
    for k in range(9):
        t = 612 + k
        tx, ty = (t % 16) * 16, (t // 16) * 16
        out[(k // 3) * 16:(k // 3) * 16 + 16, (k % 3) * 16:(k % 3) * 16 + 16] = im[ty:ty + 16, tx:tx + 16]
    return out


def vacant_d(look, letter, seg):
    """Vacant Data Center lot: the vacant industrial lot (612-620) of that look with its
    1px zone line turned into yellow/blue dashes (seg px each, walking the perimeter),
    and a 'D' drawn on the R glyph grid. letter: 'blue' or 'yellow'."""
    a = vacant_industrial(look)
    Yc = (255, 255, 0) if look == 'classic' else (255, 255, 85)
    Bc = (102, 102, 230) if look == 'classic' else (125, 125, 255)
    # erase the I glyph: the lot texture repeats every 16 px across, copy from 16 px right
    for y in range(16, 31):
        for x in range(17, 30):   # stays clear of the col-46 zone ink
            a[y, x] = a[y, x + 16] if look != 'classic' else (204, 127, 102)
    # dashed border, perimeter walk so corners stay continuous
    W = 48
    per = [(x, 0) for x in range(1, W)] + [(W - 1, y) for y in range(1, W - 1)] + \
          [(x, W - 2) for x in range(W - 2, 0, -1)] + [(1, y) for y in range(W - 3, 0, -1)]
    for i, (x, y) in enumerate(per):
        a[y, x] = Yc if (i // seg) % 2 == 0 else Bc
    L = Bc if letter == 'blue' else Yc
    K = (0, 0, 0)
    glyph = {}
    for j, row in enumerate(D_CLASSIC):
        for i, ch in enumerate(row):
            if ch == 'b': glyph[(15 + i, 18 + j)] = L
            elif ch == 'c': glyph[(15 + i, 18 + j)] = K
    if look != 'classic':
        # SVGA C/I glyph halo: dark brown 734A29 right/below the letter, 8C634A around everything
        halo = {}
        for (x, y) in glyph:
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    p = (x + dx, y + dy)
                    if p not in glyph: halo.setdefault(p, (140, 99, 74))
        for (x, y), col in glyph.items():
            if col == L:
                for p in ((x + 1, y), (x, y + 1)):
                    if p not in glyph: halo[p] = (115, 74, 41)
        for p, col in halo.items(): a[p[1], p[0]] = col
    for (x, y), col in glyph.items(): a[y, x] = col
    return a


# ---------------------------------------------------------------- Micro Nuclear
def atom(c, x, y, h, big=False, tiny=False):
    """The 4x4 plant's reactor-roof emblem (13x13 at 18..30,34..46 of tile 811-816 art):
    two crossed dark-grey orbit ellipses, red nucleus, yellow electrons riding the orbits.
    Shrunk to 7x7 (or 9x9) from a nearest-neighbour downsample, then hand-cleaned."""
    if tiny:
        rows = ["YD.DD",
                "D.D.D",
                ".DRD.",
                "D.D.D",
                "DD.DY"]
    elif big:
        rows = [".DD...DD.",
                "D..D.D..D",
                "Y...D...D",
                "D..D.D..D",
                ".DD.R.DD.",
                "D..D.D..D",
                "D...D...Y",
                "D..D.D..D",
                ".DD...DD."]
    else:
        rows = [".D...D.",
                "D..D..D",
                "Y.D.D..",
                ".D.R.D.",
                "..D.D.Y",
                "D..D..D",
                ".D...D."]
    c.art(rows, x, y, K, True, h)


def mn_c2():
    """Round-2 C (reference): 11x11 roof, 1 px walls, 9 px atom."""
    c = px.Canvas(16, 16); c.frame()
    c.box(3, 2, 13, 12, 1, roof='roof', west='wall_lit', south='wall_sh', parapet='lgrey')
    atom(c, 4, 3, 1, big=True)
    c.shadow(2)
    return c


def south_wall(door_u=None):
    """South wall in deep shade (dark grey, clearly darker than the grey roof, so the roof
    edge reads as a lip above a wall) with an optional lit doorway."""
    def f(u, s):
        if door_u is not None and u in (door_u, door_u + 1) and s >= 2:
            return 'grey'
        return 'dgrey'
    return f


def west_wall(v, s):
    """Lit west wall, plain, like the plant's lit faces."""
    return 'wall_lit'


# The atom emblem's box on the micro plant (design A): atom(c, 5, 2, ...) is 9x9.
MN_ATOM = (5, 2, 13, 10)


def mn_r3a():
    """A: 10x10 roof with light parapet, 2 px walls, 9 px atom (the r2 C atom, one px less roof)."""
    c = px.Canvas(16, 16); c.frame()
    c.box(4, 2, 13, 11, 2, roof='roof', west=west_wall, south=south_wall(), parapet='lgrey', base_line=False)
    atom(c, 5, 2, 2, big=True)
    c.shadow(2)
    return c


def mn_r3b():
    """B: 9x9 roof, 3 px walls with door, 9 px atom filling the roof (no parapet)."""
    c = px.Canvas(16, 16); c.frame()
    c.box(5, 2, 13, 10, 3, roof='roof', west=west_wall, south=south_wall(4), base_line=True)
    atom(c, 5, 2, 3, big=True)
    c.shadow(3)
    return c


def mn_r3c():
    """C: 8x8 roof with parapet, 4 px walls, 7 px atom: the tallest, shows a corner of the lot."""
    c = px.Canvas(16, 16); c.frame()
    c.box(6, 2, 13, 9, 4, roof='roof', west=west_wall, south=south_wall(3), parapet='lgrey', base_line=True)
    atom(c, 7, 2, 4)
    c.shadow(4)
    return c


def mn_r3d():
    """D: B's massing (9x9 roof, 3 px walls, 9 px atom) with a row of small windows on both
    walls; the window row gives a storey scale, which reads as height at 100%."""
    c = px.Canvas(16, 16); c.frame()
    def sw(u, s):
        if s == 2 and u % 2 == 1: return 'glass_d'
        return 'dgrey'
    def ww(v, s):
        if s == 2 and v % 2 == 1: return 'glass'
        return 'wall_lit'
    c.box(5, 2, 13, 10, 3, roof='roof', west=ww, south=sw, base_line=True)
    atom(c, 5, 2, 3, big=True)
    c.shadow(3)
    return c


DESIGNS = {'dc': {'B3': dc_b3}, 'mn': {'C-r2': mn_c2, 'A': mn_r3a, 'B': mn_r3b, 'C': mn_r3c, 'D': mn_r3d}}
