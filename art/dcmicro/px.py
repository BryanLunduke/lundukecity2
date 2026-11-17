# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Lunduke City authors
# See COPYING and NOTICE.

"""Data Center / Micro Nuclear pixel library: draw once in *materials*, render to Classic
(flat original 14-colour Micropolis palette) and SVGA (VGA Mode 13h colours
plus the 7 tan-dirt ramp colours, bevel/gradient roles like the SVGA atlas).
Projection = the Classic hospital/nuclear rule: roof rectangle, walls drop
down-left at 45 deg (pixel at depth s drawn at (x-s, y+s)), west face lit,
south face shaded, 1px black rim on roof N and E edges, shadow swept
down-right.  Ground = atlas tile 0 (DIRT) of each look, so seams vanish."""
from __future__ import annotations
import sys
import numpy as np
from PIL import Image
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'art' / 'svga'))
from vga_palette import vga_palette_8bit  # noqa
A = str(ROOT / 'third_party' / 'micropolis-assets' / 'images' / 'micropolisEngine') + '/'
VGA = sorted(set(vga_palette_8bit()))
DIRT_RAMP = [(181,148,115),(189,173,123),(201,184,146),(140,99,74),(115,74,41),(132,99,66),(107,66,40)]
SVGA_OK = set(VGA) | set(DIRT_RAMP)
CLASSIC_OK = {(0,0,0),(255,255,255),(63,63,63),(127,127,127),(191,191,191),(204,127,102),(153,127,76),
              (255,255,0),(255,0,0),(255,127,0),(0,230,0),(0,127,0),(0,0,230),(102,102,230)}

def hx(s):
    s = s.lstrip('#'); return tuple(int(s[i:i+2], 16) for i in (0, 2, 4))

def snap(c):
    if tuple(c) in SVGA_OK: return tuple(c)
    return min(VGA, key=lambda v: sum((a-b)**2 for a, b in zip(v, c)))

def tile0(look):
    im = Image.open(A + ('tiles.png' if look == 'classic' else 'tiles-svga.png')).convert('RGB')
    return np.array(im.crop((0, 0, 16, 16)))


_GROUND = {}
def ground(look):
    """Lot ground: the look's DIRT tile 0 (so it joins neighbouring dirt), with
    its 5 grey pebbles swapped for the dark-dirt speckle the nuclear plant lot
    (811-826) uses: 997F4C in Classic, 846342 in SVGA."""
    if look in _GROUND: return _GROUND[look]
    t = tile0(look).copy()
    peb = np.all(t == (63, 63, 63), axis=2) | np.all(t == (56, 56, 56), axis=2)
    t[peb] = (153, 127, 76) if look == 'classic' else (132, 99, 66)
    big = np.tile(t, (4, 4, 1))
    _GROUND[look] = big
    return big


# material -> (classic colour, svga rule)
# svga rule: dict(base=[c...] checker, hi=c (up/left edge), lo=c (down/right edge), grad=[c top..c bottom] by u)
M = {
 'dirt':      ('CC7F66', None),
 'ink':       ('000000', dict(base=['000000'])),
 'frame_v':   ('FFFF00', dict(base=['717100'])),
 'frame_h':   ('FFFF00', dict(base=['414100'])),
 'frame_tl':  ('FFFF00', dict(base=['717138'])),
 'white':     ('FFFFFF', dict(base=['FFFFFF'], lo='E3E3E3')),
 'lgrey':     ('BFBFBF', dict(base=['CBCBCB', 'B6B6B6'], hi='E3E3E3', lo='AAAAAA')),
 'grey':      ('7F7F7F', dict(base=['929292', '828282'], hi='A2A2A2', lo='717171')),
 'dgrey':     ('3F3F3F', dict(base=['515151', '454545'], hi='595959', lo='383838')),
 'roof':      ('7F7F7F', dict(base=['929292', '828282'], hi='A2A2A2', lo='717171')),
 'roof_l':    ('BFBFBF', dict(base=['CBCBCB', 'B6B6B6'], hi='E3E3E3', lo='AAAAAA')),
 'roof_d':    ('3F3F3F', dict(base=['515151', '454545'], hi='656565', lo='383838')),
 'wall_lit':  ('BFBFBF', dict(grad=['E3E3E3', 'CBCBCB', 'CBCBCB', 'B6B6B6', 'AAAAAA'])),
 'wall_sh':   ('7F7F7F', dict(grad=['929292', '828282', '828282', '717171'])),
 'wall_dk':   ('3F3F3F', dict(grad=['515151', '454545', '383838'])),
 'wall_wh':   ('FFFFFF', dict(grad=['FFFFFF', 'F3F3F3', 'E3E3E3', 'CBCBCB'])),
 'glass':     ('6666E6', dict(base=['003871'], hi='385571', lo='002041')),
 'glass_d':   ('0000E6', dict(base=['000071'], hi='0000AA', lo='000041')),
 'glint':     ('FFFFFF', dict(base=['7D9EFF'])),
 'solar':     ('0000E6', dict(base=['000071', '000071'], hi='003871', lo='000041')),
 'solar_ln':  ('BFBFBF', dict(base=['8282AA'])),
 'pane':      ('3F3F3F', dict(base=['383838'])),
 'yellow':    ('FFFF00', dict(base=['FFFF55'])),
 'yellow_d':  ('FFFF00', dict(base=['AAAA00'])),
 'red':       ('FF0000', dict(base=['AA0000'])),
 'red_hot':   ('FF0000', dict(base=['FF5555'])),
 'orange':    ('FF7F00', dict(base=['FF7D00'])),
 'led_g':     ('00E600', dict(base=['55FF55'])),
 'led_b':     ('6666E6', dict(base=['55FFFF'])),
 'green':     ('00E600', dict(base=['00AA00'], lo='007100')),
 'dgreen':    ('007F00', dict(base=['004110'], hi='007100')),
 'blue':      ('0000E6', dict(base=['0000AA'])),
 'asphalt':   ('3F3F3F', dict(base=['515151', '4D4D4D'], hi='595959', lo='454545')),
 'paving':    ('7F7F7F', dict(base=['929292', '929292'], hi='A2A2A2', lo='828282')),
 'checker':   (None, None),   # cooling tower throat: black/dgrey checker
}
SHADE_C = {'CC7F66': '997F4C', '3F3F3F': '000000', '7F7F7F': '3F3F3F', 'BFBFBF': '7F7F7F', 'FFFFFF': 'BFBFBF',
           '997F4C': '997F4C', '00E600': '007F00', '6666E6': '0000E6'}


class Canvas:
    def __init__(self, w, h):
        self.W, self.H = w, h
        self.m = np.full((h, w), 'dirt', dtype=object)
        self.u = np.zeros((h, w), float)      # gradient coordinate for walls
        self.solid = np.zeros((h, w), bool)   # casts shadow
        self.hgt = np.zeros((h, w), int)      # height for shadow length
        self.sh = np.zeros((h, w), bool)      # in shadow

    def ok(self, x, y):
        return 0 <= x < self.W and 0 <= y < self.H

    def put(self, x, y, mat, solid=None, h=0, u=None):
        if not self.ok(x, y): return
        self.m[y, x] = mat
        if u is not None: self.u[y, x] = u
        if solid is not None:
            self.solid[y, x] = solid
            if solid: self.hgt[y, x] = max(self.hgt[y, x], h)

    def rect(self, x0, y0, x1, y1, mat, solid=None, h=0):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                self.put(x, y, mat, solid, h)

    def frame(self):
        """Exact zone frame of the Classic/SVGA nuclear plant (811-826)."""
        W, H = self.W, self.H
        for y in range(H):
            self.put(0, y, 'ink')
        for x in range(2, W):
            self.put(x, 0, 'frame_h')
        for x in range(2, W - 1):
            self.put(x, 1, 'ink')
        for y in range(1, H - 2):
            self.put(W - 2, y, 'ink')
        for x in range(1, W - 1):
            self.put(x, H - 2, 'frame_h')
        for y in range(0, H - 1):
            self.put(1, y, 'frame_v'); self.put(W - 1, y, 'frame_v')
        for x in range(W):
            self.put(x, H - 1, 'ink')
        self.put(1, 0, 'frame_tl')

    def inner(self):
        return 2, 2, self.W - 3, self.H - 3

    def box(self, x0, y0, x1, y1, h, roof='roof', west='wall_lit', south='wall_sh', rim=True, rim_n='ink',
            base_line=True, parapet=None, parapet_west=True):
        """Roof [x0..x1]x[y0..y1]; walls h px deep down-left.  Any of roof/west/south
        may be a callable: roof(u, v), west(v, s), south(u, s) with u = x - x0,
        v = y - y0, s = wall depth 1..h, so facade lines follow the true projection
        (constant u or v runs diagonally down-left, constant s is a floor line)."""
        f = lambda m, a, b: m(a, b) if callable(m) else m
        for s in range(h, 0, -1):
            uu = (s - 0.5) / h
            for x in range(x0, x1 + 1):
                self.put(x - s, y1 + s, f(south, x - x0, s), True, h, uu)
            for y in range(y0, y1 + 1):
                self.put(x0 - s, y + s, f(west, y - y0, s), True, h, uu)
        if base_line and h >= 2:
            for x in range(x0, x1 + 1):
                self.put(x - h, y1 + h, 'dgrey', True, h, 1.0)
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                self.put(x, y, f(roof, x - x0, y - y0), True, h, 0)
        if parapet:
            if parapet_west:
                for y in range(y0, y1 + 1): self.put(x0, y, parapet, True, h)
            for x in range(x0, x1 + 1): self.put(x, y1, parapet, True, h)
        if rim:
            for x in range(x0, x1 + 1): self.put(x, y0 - 1, rim_n, True, h)
            for y in range(y0 - 1, y1 + 1): self.put(x1 + 1, y, 'ink', True, h)

    def disc(self, cx, cy, r, mat, solid=None, h=0):
        for y in range(int(cy - r - 1), int(cy + r + 2)):
            for x in range(int(cx - r - 1), int(cx + r + 2)):
                if (x - cx) ** 2 + (y - cy) ** 2 <= r * r + 0.3:
                    self.put(x, y, mat, solid, h)

    def ring(self, cx, cy, r, mat, solid=None, h=0, width=1.0):
        for y in range(int(cy - r - 2), int(cy + r + 3)):
            for x in range(int(cx - r - 2), int(cx + r + 3)):
                d = ((x - cx) ** 2 + (y - cy) ** 2) ** 0.5
                if r - width < d <= r + 0.45:
                    self.put(x, y, mat, solid, h)

    def cylinder(self, cx, cy, r, h, top='roof_l', lit='wall_wh', dark='wall_sh', rimmat='ink', hi=None):
        """Upright cylinder in the 45deg projection: top disc at (cx,cy); body swept down-left h px.
        Lit on the left (west) half, shaded on the right."""
        for s in range(h, 0, -1):
            for y in range(int(cy - r - 1), int(cy + r + 2)):
                for x in range(int(cx - r - 1), int(cx + r + 2)):
                    if (x - cx) ** 2 + (y - cy) ** 2 <= r * r + 0.3:
                        # position across the cylinder perpendicular to the sweep: (x+y) relative
                        t = ((x - cx) + (y - cy)) / (2 * r + 1e-6)  # west face (<0) lit, south face (>0) shaded
                        mat = (hi if (hi and t < -0.45) else lit) if t < 0.0 else dark
                        self.put(x - s, y + s, mat, True, h, (t + 1) / 2)
        self.disc(cx, cy, r, top, True, h)

    def sphere(self, cx, cy, r, h, levels=(('white', 0.62), ('lgrey', 0.36), ('grey', 0.12), ('dgrey', -9))):
        import math
        L = (-0.55, -0.55, 0.63)
        n_ = math.sqrt(sum(v * v for v in L)); L = tuple(v / n_ for v in L)
        for y in range(int(cy - r - 1), int(cy + r + 2)):
            for x in range(int(cx - r - 1), int(cx + r + 2)):
                dx, dy = (x - cx) / r, (y - cy) / r
                d2 = dx * dx + dy * dy
                if d2 > 1.0 + 0.3 / (r * r): continue
                nz = math.sqrt(max(0.0, 1 - min(1.0, d2)))
                b = dx * L[0] + dy * L[1] + nz * L[2]
                for mat, thr in levels:
                    if b >= thr:
                        self.put(x, y, mat, True, h); break

    def art(self, rows, ox, oy, key, solid=None, h=0):
        for j, row in enumerate(rows):
            for i, ch in enumerate(row):
                if ch in key:
                    self.put(ox + i, oy + j, key[ch], solid, h)

    def shadow(self, maxlen=4, onto=('dirt', 'asphalt', 'paving', 'grey', 'green', 'dgrey', 'roof', 'roof_l', 'roof_d')):
        sol = self.solid.copy()
        for y in range(self.H):
            for x in range(self.W):
                if not sol[y, x]: continue
                L = min(maxlen, max(1, (self.hgt[y, x] + 1) // 2))
                for k in range(1, L + 1):
                    X, Y = x + k, y + k
                    if not self.ok(X, Y) or sol[Y, X]: continue
                    if self.m[Y, X] in onto and self.hgt[Y, X] < self.hgt[y, x]:
                        self.sh[Y, X] = True
                # straight east too (classic shadows fill the corner)
                for k in range(1, L + 1):
                    X, Y = x + k, y + k - 1
                    if self.ok(X, Y) and not sol[Y, X] and self.m[Y, X] in onto and self.hgt[Y, X] < self.hgt[y, x] and k < L:
                        self.sh[Y, X] = True

    # ------------------------------------------------------------ render
    def render(self, look):
        out = np.zeros((self.H, self.W, 3), np.uint8)
        t0 = ground(look)
        m = self.m
        for y in range(self.H):
            for x in range(self.W):
                mat = m[y, x]
                if look == 'classic':
                    if mat == 'dirt':
                        c = tuple(t0[y % 64, x % 64])
                        hexc = '%02X%02X%02X' % c
                    elif mat == 'checker':
                        hexc = '000000' if (x + y) % 2 else '3F3F3F'
                    else:
                        hexc = M[mat][0]
                    if self.sh[y, x]: hexc = SHADE_C.get(hexc, hexc)
                    out[y, x] = hx(hexc)
                    continue
                # SVGA
                if mat == 'dirt':
                    c = tuple(int(v) for v in t0[y % 64, x % 64])
                    if self.sh[y, x]:
                        c = hx('6B4228') if c in (hx('846342'), hx('8C634A'), hx('734A29')) else hx('846342')
                    # contact rim: dirt just below/left of a solid wall base gets 734A29 like the atlas
                    out[y, x] = c; continue
                if mat == 'checker':
                    out[y, x] = hx('000000' if (x + y) % 2 else '515151'); continue
                rule = M[mat][1]
                same = lambda X, Y: (not self.ok(X, Y)) or m[Y, X] == mat
                if 'grad' in rule:
                    g = rule['grad']; c = g[min(len(g) - 1, int(self.u[y, x] * len(g)))]
                else:
                    b = rule['base']; c = b[1] if len(b) > 1 and (x + 2 * y) % 5 == 0 else b[0]
                    if 'hi' in rule and (not same(x - 1, y) or not same(x, y - 1)): c = rule['hi']
                    if 'lo' in rule and (not same(x + 1, y) or not same(x, y + 1)): c = rule['lo']
                rgb = hx(c)
                if self.sh[y, x]:
                    rgb = tuple(int(v * 0.62) for v in rgb)
                out[y, x] = snap(rgb)
        return out


def save(arr, path):
    Image.fromarray(arr).save(path)


def check_palette(arr, look):
    ok = CLASSIC_OK if look == 'classic' else SVGA_OK
    bad = {tuple(int(v) for v in p) for p in arr.reshape(-1, 3)} - ok
    return bad
