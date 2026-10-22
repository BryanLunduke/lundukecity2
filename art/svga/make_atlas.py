#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Lunduke City authors
# See COPYING and NOTICE.

"""SVGA look, batches 1 to 3: terrain, zones, roads, rails, power, and bridges.

Classic pixels are not redrawn. Each output pixel keeps the source tile's
flat-colour region and is painted from a VGA Mode 13h ramp. Black ink stays
black. Later batches add the other tile groups to this file.

    python3 art/svga/make_atlas.py
"""

from __future__ import annotations

import sys
from dataclasses import dataclass
from pathlib import Path

from PIL import Image

from vga_palette import vga_palette_8bit

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
ATLAS_PATH = ROOT / "third_party/micropolis-assets/images/micropolisEngine/tiles.png"
PREVIEW_DIR = ROOT / "previews"
REPORT_PATH = HERE / "verify-report.txt"
TILE = 16
NEAREST = Image.Resampling.NEAREST

BLACK = (0, 0, 0)
WHITE = (255, 255, 255)
BG = (32, 32, 32)
FG = (255, 255, 85)
INK_YELLOW = FG

SRC_GREY = (127, 127, 127)
SRC_GREEN = (0, 230, 0)
SRC_LBLUE = (102, 102, 230)
SRC_DGREY = (63, 63, 63)
SRC_BLUE = (0, 0, 230)
SRC_YELLOW = (255, 255, 0)
SRC_DGREEN = (0, 127, 0)
SRC_LGREY = (191, 191, 191)
SRC_RED = (255, 0, 0)
SRC_TAN = (204, 127, 102)
SRC_DTAN = (153, 127, 76)
SRC_ORANGE = (255, 127, 0)

# The SVGA dirt is scheme B. Scheme A is the darker field kept only so the
# A/B preview can still show the choice. Both share one mask.
# Ramp order: base, neighbour, accent, rim (also the soft shadow), contact.
# 31x31 world-locked mask. 31 does not divide 16, so a tile edge does not
# restart it. C is the accent (at least two pixels from the next accent).
# B is the neighbour (no two touch, even on the diagonal). '.' is the base.
_DIRT_MASK = (
    "CB.B....BC....B...B..B..B.B.B.B",
    "......B....B....B..............",
    ".B.B.....B...CB...B.CB....CB.B.",
    "....CB.B....B...B......B.B.....",
    "B.........B...B...B..B.....B.BC",
    "..B..B.B....B...B......B.B.....",
    "B........B....BC...C.B.....B.B.",
    "..B.C.B.C..B.....B.....C...C...",
    "C............B......B.B..B...B.",
    "..B.B.B..B.B....B..........B...",
    "B...........C.....C...B.B....B.",
    "..B.C...B......B....B.....C....",
    "B.....B...B..B...B.....B....B..",
    "..B.....B......B...B.....B.....",
    "B....B.....B.B........C....B.B.",
    "...B...C.......C..B.....B......",
    ".........B..B.......B.....B...C",
    ".B.B.B.B......B..B....B.B...B..",
    ".........B.C.......C.....C.....",
    "B..C...B.......B.B...B.....B...",
    ".....B......B..........B.....B.",
    ".......B..B.....B.B.B.....B....",
    ".B..B.........C.......C.....B.B",
    ".......C...B....B..B....B.B....",
    "B.B..B...B...B.......B.......C.",
    ".......B..........C....B.C.....",
    "...C.B...B.C...B.....B.....B.B.",
    "B.B....B.....B...B.B....B......",
    "....B....B.....B.....B......C..",
    "..B....B....B....B.B...C..B...B",
    ".....C..........C..............",
)


@dataclass(frozen=True)
class DirtScheme:
    """One dirt palette. The 31x31 mask decides which role a pixel uses."""

    name: str
    base: tuple[int, int, int]
    near: tuple[int, int, int]
    accent: tuple[int, int, int]
    rim: tuple[int, int, int]
    contact: tuple[int, int, int]
    soil_rim: tuple[int, int, int]
    soil_contact: tuple[int, int, int]

    @property
    def ramp(self) -> tuple[tuple[int, int, int], ...]:
        return (self.base, self.near, self.accent, self.rim, self.contact)

    @property
    def soil_ramp(self) -> tuple[tuple[int, int, int], ...]:
        return (self.soil_rim, self.soil_contact)


# A: the reviewed calm field. B: the same roles, one value step lighter.
# Neighbour B is (113, 69, 56), the value step above (65, 40, 32).
# (113, 56, 56) is closer in luma but pink, so the warm step is the one used.
# The accent's true next value step is orange paint (255, 125, 0), so B uses
# the EGA brown (170, 85, 0) and keeps it rare.
SCHEME_A = DirtScheme(
    "A",
    (65, 32, 0),
    (65, 40, 32),
    (113, 56, 0),
    (65, 16, 0),
    (65, 0, 16),
    (65, 0, 48),
    (65, 0, 32),
)
SCHEME_B = DirtScheme(
    "B",
    (113, 56, 0),
    (113, 69, 56),
    (170, 85, 0),
    (113, 28, 0),
    (113, 0, 28),
    (65, 48, 0),
    (65, 16, 0),
)
SCHEMES = (SCHEME_A, SCHEME_B)
# The SVGA field. Scheme A stays available for the historical comparison.
DIRT_BASE = SCHEME_B.base
DIRT_NEAR = SCHEME_B.near
DIRT_ACCENT = SCHEME_B.accent
DIRT_RIM = SCHEME_B.rim
DIRT_CONTACT = SCHEME_B.contact
DIRT_RAMP = SCHEME_B.ramp
SOIL_RAMP = SCHEME_B.soil_ramp
# Dusty rose. The face is (113, 56, 56). The highlight is the paler rose in
# the same value band, (113, 81, 81): the next value step, (255, 125, 125),
# is a hot pink. The shade is the value step below, (65, 32, 32).
# Ramp order matches the other faces: highlight, lit face, shaded face,
# shaded edge. The lit half stays on (113, 56, 56). The shaded half and the
# SE edge use the darker step. A one-pixel checker cell lands on the lit face.
ROSE_RAMP = (
    (113, 81, 81),
    (113, 56, 56),
    (65, 32, 32),
    (65, 32, 32),
)
# Blue pixels inside a red/blue dither that is meant to read red. One step
# bluer than the rose face, so the seam stays a boundary without a second hue.
ROSE_MATE = (113, 56, 69)
# Red pixels inside a red/blue dither that is meant to read blue. One step
# off the wall's darkest blue, so the stitches stay blue and the seam holds.
BLUE_MATE = (16, 0, 65)
# Commerce yellow is one value step down from neon (255, 255, 0): the face
# stays pure yellow. The highlight is the paler gold in that value band.
# The next step up is the neon yellow itself, so it is not used.
GOLD_RAMP = (
    (113, 113, 56),
    (113, 113, 0),
    (65, 65, 0),
    (65, 65, 0),
)
# Hot orange's darker pure steps are dirt colours, so the amber face is the
# dusty orange (113, 85, 56), with a gold highlight and a darker shade.
AMBER_RAMP = (
    (113, 85, 0),
    (113, 85, 56),
    (65, 48, 32),
    (65, 48, 32),
)
GRASS_LINE = (0, 170, 0)
HEDGE_LINE = (32, 65, 32)
# Zone letters and lot borders have to read at a glance. The C keeps the
# classic light-blue hue (equal red and green, high blue) at the lighter
# standard step, (125, 125, 255). (85, 85, 255) is the same hue but darker,
# and it sinks into a slate roof. The I is bright yellow one notch off neon
# (255, 255, 0): (255, 255, 85). A letter on a light roof gets a 1px dark edge.
COM_LINE = (125, 125, 255)
IND_LINE = (255, 255, 85)
LETTER_SHADE = (32, 32, 32)
# Previous pass, kept so the letter sample can show the darker marks.
_COM_LINE_BEFORE = (56, 85, 113)
_IND_LINE_BEFORE = (113, 113, 0)

GRASS_RAMP = (
    (0, 170, 0),
    (56, 113, 0),
    (28, 113, 0),
    (0, 113, 0),
    (0, 65, 32),
    (0, 65, 16),
)
HEDGE_RAMP = (
    (44, 65, 44),
    (32, 65, 32),
    (16, 65, 0),
    (0, 65, 0),
)
CONCRETE_RAMP = (
    (162, 162, 162),
    (146, 146, 146),
    (130, 130, 130),
    (113, 113, 113),
)
ROOF_RAMP = (
    (227, 227, 227),
    (203, 203, 203),
    (182, 182, 182),
    (170, 170, 170),
)
# One step calmer than the periwinkle glass. Less red in the light step,
# so the windows read blue rather than purple.
GLASS_RAMP = (
    (125, 158, 255),
    (56, 85, 113),
    (56, 56, 113),
)
# One step down from the electric (0, 0, 255) wall.
WALL_RAMP = (
    (0, 0, 170),
    (0, 0, 113),
    (0, 0, 65),
)
SLATE_RAMP = (
    (81, 81, 81),
    (69, 69, 69),
    (56, 56, 56),
)
SIGN_RAMP = (
    (255, 255, 125),
    (255, 255, 85),
    (255, 255, 0),
)
# Light-blue water. The last two are the coastline band: a darker teal,
# outside the body dither, with no red so the shore does not turn purple.
WATER_RAMP = (
    (0, 56, 113),
    (0, 85, 113),
    (0, 28, 113),
    (0, 48, 65),
    (0, 32, 65),
)
# Soil's open field uses the tan stipple. Its rim and contact stay off that
# stipple so a soil pixel can touch tan. Each dirt scheme has its own pair.
DEEP = (0, 0, 170)
SPECK = (56, 56, 56)
STONE = (130, 130, 130)
ROCK = (44, 44, 44)
# Asphalt is smooth: one body grey, one darker pixel where the road meets
# dirt, water, or the black curb. Dashes and cars are not an edge, so the
# centre line stays a hard white band on an even field.
ASPHALT_BODY = (56, 56, 56)
ASPHALT_EDGE = (44, 44, 44)
# Ties, wires, and car bodies. Flat, so a traffic frame cannot flicker.
TIE = (130, 130, 130)
WIRE = (203, 203, 203)
# A power line is a thin dark-grey stroke, the same classic grey as asphalt.
# (146, 146, 146) is light enough to read on dirt B and is not the tie grey,
# so a wire can cross a rail without the two colours collapsing.
CABLE = (146, 146, 146)
_CABLE_BEFORE = (81, 81, 81)
DASH = (255, 255, 255)
RAIL_MARK = (255, 255, 85)
LEAF = (162, 162, 162)
ORANGE = (255, 125, 0)

# Same deep-green hues as the approved lawn.
# 2/9 (0, 170, 0), 3/9 (56, 113, 0), 3/9 (28, 113, 0), 1/9 (0, 113, 0).
_GRASS_WEIGHTS = (0, 0, 1, 1, 1, 2, 2, 2, 3)
_WATER_WEIGHTS = (0, 0, 0, 0, 0, 0, 1, 2)
_EDGE_GROUND = frozenset({SRC_GREEN, SRC_DGREEN, SRC_GREY})
_MIN_CAST = 8
_ORTHO = ((0, -1), (0, 1), (-1, 0), (1, 0))
_NW = ((0, -1), (-1, 0))
_SE = ((0, 1), (1, 0))
_PAVEMENT = frozenset({SRC_GREY})
# The building C, the empty-lot C, and the empty-lot I. Every batch-2 letter matches one of these.
_C_MARK = frozenset(
    (x, y)
    for y, row in enumerate((".####.", "##..##", "##....", "##....", "##....", "##..##", ".####."))
    for x, ch in enumerate(row)
    if ch == "#"
)
_C_LOT = frozenset(
    (x, y)
    for y, row in enumerate(
        (
            "..#####..",
            ".##...##.",
            "##.....##",
            "##.......",
            "##.......",
            "##.......",
            "##.......",
            "##.......",
            "##.....##",
            ".##...##.",
            "..#####..",
        )
    )
    for x, ch in enumerate(row)
    if ch == "#"
)
_I_MARK = frozenset(
    (x, y)
    for y, row in enumerate(("####", ".##.", ".##.", ".##.", ".##.", ".##.", ".##.", ".##.", ".##.", ".##.", "####"))
    for x, ch in enumerate(row)
    if ch == "#"
)
_LIGHT_BEHIND = frozenset({SRC_GREY, SRC_LGREY, WHITE})
RECEIVERS = frozenset({SRC_TAN, SRC_DTAN, SRC_GREEN, SRC_DGREEN})
TREE_CAST = frozenset({SRC_GREEN, SRC_DGREEN})
BUILD_CAST = frozenset({SRC_GREY, SRC_LGREY, SRC_LBLUE, SRC_BLUE, SRC_DGREY, SRC_DGREEN, SRC_YELLOW})
WATER_FAMILY = frozenset({SRC_LBLUE, SRC_BLUE, BLACK})

STRONG_LUMA = 80

FONT = {
    " ": ["     ", "     ", "     ", "     ", "     ", "     ", "     "],
    "A": [" ### ", "#   #", "#   #", "#####", "#   #", "#   #", "#   #"],
    "B": ["#### ", "#   #", "#   #", "#### ", "#   #", "#   #", "#### "],
    "C": [" ### ", "#   #", "#    ", "#    ", "#    ", "#   #", " ### "],
    "D": ["#### ", "#   #", "#   #", "#   #", "#   #", "#   #", "#### "],
    "E": ["#####", "#    ", "#    ", "#### ", "#    ", "#    ", "#####"],
    "F": ["#####", "#    ", "#    ", "#### ", "#    ", "#    ", "#    "],
    "G": [" ### ", "#   #", "#    ", "# ###", "#   #", "#   #", " ### "],
    "H": ["#   #", "#   #", "#   #", "#####", "#   #", "#   #", "#   #"],
    "I": ["#####", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", "#####"],
    "K": ["#   #", "#  # ", "# #  ", "##   ", "# #  ", "#  # ", "#   #"],
    "L": ["#    ", "#    ", "#    ", "#    ", "#    ", "#    ", "#####"],
    "M": ["#   #", "## ##", "# # #", "#   #", "#   #", "#   #", "#   #"],
    "N": ["#   #", "##  #", "# # #", "#  ##", "#   #", "#   #", "#   #"],
    "O": [" ### ", "#   #", "#   #", "#   #", "#   #", "#   #", " ### "],
    "P": ["#### ", "#   #", "#   #", "#### ", "#    ", "#    ", "#    "],
    "R": ["#### ", "#   #", "#   #", "#### ", "# #  ", "#  # ", "#   #"],
    "S": [" ####", "#    ", "#    ", " ### ", "    #", "    #", "#### "],
    "T": ["#####", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", "  #  "],
    "U": ["#   #", "#   #", "#   #", "#   #", "#   #", "#   #", " ### "],
    "V": ["#   #", "#   #", "#   #", "#   #", "#   #", " # # ", "  #  "],
    "W": ["#   #", "#   #", "#   #", "# # #", "# # #", "## ##", "#   #"],
    "X": ["#   #", "#   #", " # # ", "  #  ", " # # ", "#   #", "#   #"],
    "Y": ["#   #", "#   #", " # # ", "  #  ", "  #  ", "  #  ", "  #  "],
    "0": [" ### ", "#   #", "#  ##", "# # #", "##  #", "#   #", " ### "],
    "1": ["  #  ", " ##  ", "  #  ", "  #  ", "  #  ", "  #  ", " ### "],
    "2": [" ### ", "#   #", "    #", "   # ", "  #  ", " #   ", "#####"],
    "3": ["#### ", "    #", "    #", " ### ", "    #", "    #", "#### "],
    "4": ["   # ", "  ## ", " # # ", "#  # ", "#####", "   # ", "   # "],
    "5": ["#####", "#    ", "#    ", "#### ", "    #", "    #", "#### "],
    "6": [" ### ", "#    ", "#    ", "#### ", "#   #", "#   #", " ### "],
    "7": ["#####", "    #", "   # ", "  #  ", "  #  ", "  #  ", "  #  "],
    "8": [" ### ", "#   #", "#   #", " ### ", "#   #", "#   #", " ### "],
    "9": [" ### ", "#   #", "#   #", " ####", "    #", "    #", " ### "],
    "-": ["     ", "     ", "     ", "#####", "     ", "     ", "     "],
}


@dataclass(frozen=True)
class Material:
    ramp: tuple[tuple[int, int, int], ...]
    role: str


def _mat(ramp: tuple[tuple[int, int, int], ...], role: str) -> Material:
    return Material(ramp=ramp, role=role)


def _flat(rgb: tuple[int, int, int]) -> Material:
    return Material(ramp=(rgb,), role="flat")


DIRT = _mat(DIRT_RAMP, "dirt")
GRASS = _mat(GRASS_RAMP, "grass")
HEDGE = _mat(HEDGE_RAMP, "hedge")
CONCRETE = _mat(CONCRETE_RAMP, "surface")
ROOF = _mat(ROOF_RAMP, "surface")
GLASS = _mat(GLASS_RAMP, "surface")
WALL = _mat(WALL_RAMP, "surface")
SLATE = _mat(SLATE_RAMP, "surface")
SIGN = _mat(SIGN_RAMP, "surface")
WATER = _mat(WATER_RAMP, "water")

# Outputs reserved for one classic source stay disjoint from every other source.
MATS: dict[str, dict[tuple[int, int, int], Material]] = {
    "ground": {
        SRC_TAN: DIRT,
        SRC_DGREY: _flat(SPECK),
    },
    "water": {
        SRC_LBLUE: WATER,
        SRC_BLUE: _flat(DEEP),
        SRC_TAN: DIRT,
        SRC_DTAN: _mat(SOIL_RAMP, "soil"),
        SRC_GREY: _flat(STONE),
        SRC_DGREY: _flat(ROCK),
    },
    "tree": {
        SRC_TAN: DIRT,
        SRC_DTAN: _mat(SOIL_RAMP, "soil"),
        SRC_GREEN: GRASS,
        SRC_DGREEN: HEDGE,
        SRC_GREY: _flat(LEAF),
    },
    "building": {
        SRC_TAN: DIRT,
        SRC_DTAN: _mat(SOIL_RAMP, "soil"),
        SRC_GREEN: GRASS,
        SRC_DGREEN: HEDGE,
        SRC_GREY: CONCRETE,
        SRC_DGREY: SLATE,
        SRC_LGREY: ROOF,
        SRC_LBLUE: GLASS,
        SRC_BLUE: WALL,
        SRC_YELLOW: SIGN,
        WHITE: _flat(WHITE),
        SRC_RED: _mat(ROSE_RAMP, "surface"),
        SRC_ORANGE: _flat(ORANGE),
    },
}
# Commercial and industrial share the residential recipe. Yellow and orange
# are stepped down here only, so the approved residential signs stay put.
MATS["commerce"] = dict(MATS["building"])
MATS["commerce"][SRC_YELLOW] = _mat(GOLD_RAMP, "surface")
MATS["commerce"][SRC_ORANGE] = _mat(AMBER_RAMP, "surface")
# Roads, rails, power, and bridges. White dashes stay white. Grey ties and
# the lighter wire stay flat so they read on dirt B and do not shimmer.
MATS["road"] = {
    SRC_TAN: DIRT,
    SRC_DTAN: _mat(SOIL_RAMP, "soil"),
    SRC_DGREY: _mat((ASPHALT_BODY, ASPHALT_EDGE), "asphalt"),
    SRC_GREY: _flat(TIE),
    SRC_LGREY: _flat(WIRE),
    SRC_LBLUE: WATER,
    SRC_BLUE: _flat(DEEP),
    SRC_YELLOW: _flat(RAIL_MARK),
    SRC_RED: _flat(ROSE_RAMP[1]),
    SRC_GREEN: _flat(GRASS_RAMP[3]),
    WHITE: _flat(DASH),
}


def _claim(
    owner: dict[tuple[int, int, int], tuple[int, int, int]],
    rgb: tuple[int, int, int],
    src: tuple[int, int, int],
    palette: set[tuple[int, int, int]],
    label: str,
) -> None:
    if rgb == BLACK:
        raise SystemExit(f"{label} uses black; black is reserved for ink")
    if rgb not in palette:
        raise SystemExit(f"{rgb} is not in the VGA palette ({label})")
    if rgb in owner and owner[rgb] != src:
        raise SystemExit(f"{rgb} is used by both source {owner[rgb]} and {src} ({label})")
    owner[rgb] = src


def _require_palette(palette: set[tuple[int, int, int]]) -> None:
    shared: dict[tuple[int, int, int], tuple[int, int, int]] = {}
    for kind, mats in MATS.items():
        for src, mat in mats.items():
            if src in (SRC_TAN, SRC_DTAN):
                continue
            for rgb in mat.ramp:
                _claim(shared, rgb, src, palette, f"{kind} {src}")
    # Dither mates are not on the face ramps. Each stays with one source.
    _claim(shared, ROSE_MATE, SRC_BLUE, palette, "rose dither mate")
    _claim(shared, BLUE_MATE, SRC_RED, palette, "blue dither mate")
    for scheme in SCHEMES:
        owner = dict(shared)
        for rgb in scheme.ramp:
            _claim(owner, rgb, SRC_TAN, palette, f"dirt {scheme.name}")
        for rgb in scheme.soil_ramp:
            _claim(owner, rgb, SRC_DTAN, palette, f"soil {scheme.name}")


def _hash(x: int, y: int) -> int:
    """Full-coordinate hash. The low bits depend on x and y, not on x % 16."""
    n = (x * 374761393 + y * 668265263) & 0xFFFFFFFF
    n ^= n >> 13
    n = (n * 1274126177) & 0xFFFFFFFF
    n ^= n >> 16
    return n


def _dither_index(x: int, y: int, salt: int, weights: tuple[int, ...]) -> int:
    if (_hash(x + salt * 3, y + 19) & 7) == 0:
        h = _hash(x + salt, y + salt * 5)
    else:
        h = _hash((x // 2) + salt * 17, (y // 2) + salt * 29)
    return weights[h % len(weights)]


def _assert_hash_aperiodic() -> None:
    same = 0
    total = 0
    for y in range(48):
        for x in range(48):
            base = _hash(x, y) & 15
            same += base == (_hash(x + 16, y) & 15)
            same += base == (_hash(x, y + 16) & 15)
            total += 2
    if same / total > 0.2:
        raise SystemExit(f"speckle hash still tiles at 16px (match rate {same / total:.3f})")
    clustered = 0
    cluster_total = 0
    for y in range(64):
        for x in range(64):
            here = _dither_index(x, y, 2, _WATER_WEIGHTS)
            clustered += here == _dither_index(x + 16, y, 2, _WATER_WEIGHTS)
            clustered += here == _dither_index(x, y + 16, 2, _WATER_WEIGHTS)
            cluster_total += 2
    if clustered / cluster_total > 0.95:
        raise SystemExit(
            f"water clusters still tile at 16px (match rate {clustered / cluster_total:.3f})"
        )
    # A 16px stamp would repeat every non-base pixel at +16. The mask period
    # is 31, so those pixels mostly miss.
    marked = 0
    repeated = 0
    counts: dict[tuple[int, int, int], int] = {}
    span = 93  # three periods of the 31px mask
    accent_touch = 0
    near_touch = 0
    for y in range(span):
        for x in range(span):
            here = _dirt_colour(x, y, False)
            counts[here] = counts.get(here, 0) + 1
            if here != DIRT_BASE:
                marked += 1
                repeated += here == _dirt_colour(x + 16, y, False)
            if here == DIRT_ACCENT:
                for dx, dy in _ORTHO + ((1, 1), (1, -1), (-1, 1), (-1, -1)):
                    if _dirt_colour(x + dx, y + dy, False) == DIRT_ACCENT:
                        accent_touch += 1
            elif here == DIRT_NEAR:
                for dx, dy in _ORTHO:
                    if _dirt_colour(x + dx, y + dy, False) == DIRT_NEAR:
                        near_touch += 1
    if marked == 0 or repeated / marked > 0.5:
        raise SystemExit(f"dirt texture still tiles at 16px ({repeated} of {marked})")
    if accent_touch:
        raise SystemExit(f"light accents touch ({accent_touch})")
    if near_touch:
        raise SystemExit(f"neighbour pixels clump ({near_touch})")
    pixels = span * span
    expected = {DIRT_BASE: 0.791, DIRT_NEAR: 0.170, DIRT_ACCENT: 0.040}
    for rgb, share in expected.items():
        got = counts.get(rgb, 0) / pixels
        if abs(got - share) > 0.015:
            raise SystemExit(f"dirt share {rgb} is {got:.3f}, wanted {share:.3f}")
    if counts.get(DIRT_RIM) or counts.get(DIRT_CONTACT):
        raise SystemExit("rim or shadow colour leaked into the open field")


def _in(x: int, y: int, w: int, h: int) -> bool:
    return 0 <= x < w and 0 <= y < h


def tile_kind(index: int) -> str:
    if index in (0, 1):
        return "ground"
    if 2 <= index <= 20:
        return "water"
    if 21 <= index <= 37 or 40 <= index <= 43:
        return "tree"
    if 240 <= index <= 404:
        return "building"
    # Commercial 423-611 (empty lot, then 20 densities). Industrial 612-692.
    # 852-859 and 884-915 are the industrial chimney frames. Coal smoke is later.
    if 423 <= index <= 692 or 852 <= index <= 859 or 884 <= index <= 915:
        return "commerce"
    # Roads 64-207 (16 shapes, four light frames, four heavy frames).
    # Power 208-222, rails 224-238, the road/power tile 239.
    # Open drawbridge frames 828-831 and 948-951.
    if 64 <= index <= 222 or 224 <= index <= 239 or 828 <= index <= 831 or 948 <= index <= 951:
        return "road"
    raise SystemExit(f"tile {index} is outside the painted batches")


def batch1_indices() -> list[int]:
    ids = [0, 1]
    ids += list(range(2, 21))
    ids += list(range(21, 38))
    ids += list(range(40, 44))
    ids += list(range(240, 405))
    return ids


def _caster_mask(kinds: list[list[str]], src, w: int, h: int) -> list[list[bool]]:
    mask = [[False] * w for _ in range(h)]
    for y in range(h):
        row = y // TILE
        for x in range(w):
            kind = kinds[row][x // TILE]
            colour = src[x, y]
            if kind == "tree" and colour in TREE_CAST:
                mask[y][x] = True
            elif kind in ("building", "commerce") and colour in BUILD_CAST:
                mask[y][x] = True
    return mask


def _caster_counts(src, w: int, h: int, mask: list[list[bool]]) -> dict[tuple[int, int], int]:
    counts: dict[tuple[int, int], int] = {}
    seen = [[False] * w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            if seen[y][x] or not mask[y][x]:
                continue
            colour = src[x, y]
            stack = [(x, y)]
            seen[y][x] = True
            pixels: list[tuple[int, int]] = []
            while stack:
                cx, cy = stack.pop()
                pixels.append((cx, cy))
                for dx, dy in _ORTHO:
                    nx, ny = cx + dx, cy + dy
                    if _in(nx, ny, w, h) and not seen[ny][nx] and mask[ny][nx] and src[nx, ny] == colour:
                        seen[ny][nx] = True
                        stack.append((nx, ny))
            if len(pixels) < _MIN_CAST:
                continue
            for px, py in pixels:
                counts[(px, py)] = len(pixels)
    return counts


def _casts(src, x: int, y: int, w: int, h: int, receiver: tuple[int, int, int], counts) -> bool:
    if not _in(x, y, w, h):
        return False
    if (x, y) in counts and src[x, y] != receiver:
        return True
    if src[x, y] == BLACK:
        for dx, dy in _ORTHO:
            nx, ny = x + dx, y + dy
            if (nx, ny) in counts and src[nx, ny] != receiver:
                return True
    return False


def _shadow_strength(src, x: int, y: int, w: int, h: int, counts) -> int:
    colour = src[x, y]
    if colour not in RECEIVERS:
        return 0
    strength = 0
    for dx, dy in _NW:
        nx, ny = x + dx, y + dy
        if _casts(src, nx, ny, w, h, colour, counts):
            strength = max(strength, 2)
            continue
        if not _in(nx, ny, w, h):
            continue
        mid = src[nx, ny]
        if mid != colour and mid != BLACK:
            continue
        if _casts(src, x + 2 * dx, y + 2 * dy, w, h, colour, counts):
            strength = max(strength, 1)
    return strength


def _apply_shadow(ramp: tuple[tuple[int, int, int], ...], strength: int) -> tuple[int, int, int] | None:
    if strength >= 2:
        return ramp[-1]
    if strength == 1 and len(ramp) >= 2:
        return ramp[-2]
    if strength == 1:
        return ramp[-1]
    return None


def _grass_rim(src, x: int, y: int, w: int, h: int, counts) -> bool:
    for dx, dy in _ORTHO:
        nx, ny = x + dx, y + dy
        if not _in(nx, ny, w, h):
            continue
        neighbour = src[nx, ny]
        if (nx, ny) in counts or neighbour in _PAVEMENT:
            return True
        if neighbour == BLACK:
            fx, fy = nx + dx, ny + dy
            if _in(fx, fy, w, h) and ((fx, fy) in counts or src[fx, fy] in _PAVEMENT):
                return True
    return False


def _touches_water(src, x: int, y: int, w: int, h: int) -> bool:
    """Ground pixel against open water, including through one black outline."""
    for dx, dy in _ORTHO:
        nx, ny = x + dx, y + dy
        if not _in(nx, ny, w, h):
            continue
        neighbour = src[nx, ny]
        if neighbour in (SRC_LBLUE, SRC_BLUE):
            return True
        if neighbour == BLACK:
            fx, fy = nx + dx, ny + dy
            if _in(fx, fy, w, h) and src[fx, fy] in (SRC_LBLUE, SRC_BLUE):
                return True
    return False


def _dirt_colour(x: int, y: int, edge: bool, scheme: DirtScheme = SCHEME_B) -> tuple[int, int, int]:
    """Calm field in world pixels. The edge is one solid darker pixel."""
    if edge:
        return scheme.rim
    mark = _DIRT_MASK[y % 31][x % 31]
    if mark == "C":
        return scheme.accent
    if mark == "B":
        return scheme.near
    return scheme.base


def _touches_ground(src, x: int, y: int, w: int, h: int, colours: frozenset) -> bool:
    for dx, dy in _ORTHO:
        nx, ny = x + dx, y + dy
        if not _in(nx, ny, w, h):
            continue
        neighbour = src[nx, ny]
        if neighbour in colours:
            return True
        if neighbour == BLACK:
            fx, fy = nx + dx, ny + dy
            if _in(fx, fy, w, h) and src[fx, fy] in colours:
                return True
    return False


def _dirt_edge(src, x: int, y: int, w: int, h: int) -> bool:
    """1px where dirt meets grass or pavement, including through one black pixel."""
    return _touches_ground(src, x, y, w, h, _EDGE_GROUND)


def _is_land(colour: tuple[int, int, int]) -> bool:
    return colour not in WATER_FAMILY


def _coast_strength(src, x: int, y: int, w: int, h: int) -> int:
    """2 on water touching land, 1 one pixel further out. Black outlines count."""
    strength = 0
    for dx, dy in _ORTHO:
        nx, ny = x + dx, y + dy
        if not _in(nx, ny, w, h):
            continue
        neighbour = src[nx, ny]
        if _is_land(neighbour):
            strength = 2
            continue
        if neighbour == BLACK:
            fx, fy = nx + dx, ny + dy
            if _in(fx, fy, w, h) and _is_land(src[fx, fy]):
                strength = max(strength, 2)
            continue
        fx, fy = nx + dx, ny + dy
        if not _in(fx, fy, w, h):
            continue
        beyond = src[fx, fy]
        if _is_land(beyond):
            strength = max(strength, 1)
        elif beyond == BLACK:
            gx, gy = fx + dx, fy + dy
            if _in(gx, gy, w, h) and _is_land(src[gx, gy]):
                strength = max(strength, 1)
    return strength


def _same_count(src, x: int, y: int, w: int, h: int) -> int:
    colour = src[x, y]
    count = 0
    for dx, dy in _ORTHO:
        nx, ny = x + dx, y + dy
        if _in(nx, ny, w, h) and src[nx, ny] == colour:
            count += 1
    return count


def _component_boxes(src, w: int, h: int, colours: set[tuple[int, int, int]]):
    boxes: dict[tuple[int, int], tuple[int, int, int, int]] = {}
    seen = [[False] * w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            if seen[y][x] or src[x, y] not in colours:
                continue
            colour = src[x, y]
            stack = [(x, y)]
            seen[y][x] = True
            pixels: list[tuple[int, int]] = []
            x0 = x1 = x
            y0 = y1 = y
            while stack:
                cx, cy = stack.pop()
                pixels.append((cx, cy))
                x0, y0 = min(x0, cx), min(y0, cy)
                x1, y1 = max(x1, cx), max(y1, cy)
                for dx, dy in _ORTHO:
                    nx, ny = cx + dx, cy + dy
                    if _in(nx, ny, w, h) and not seen[ny][nx] and src[nx, ny] == colour:
                        seen[ny][nx] = True
                        stack.append((nx, ny))
            box = (x0, y0, x1, y1)
            for px, py in pixels:
                boxes[(px, py)] = box
    return boxes


def _paint_surface(src, x: int, y: int, w: int, h: int, ramp: tuple, box: tuple[int, int, int, int]):
    n = len(ramp)
    if n == 1:
        return ramp[0]
    x0, y0, x1, y1 = box
    tx = (x - x0) / max(1, x1 - x0)
    ty = (y - y0) / max(1, y1 - y0)
    t = (tx + ty) / 2
    if n >= 4:
        idx = 1 + int(t * (n - 3) + 0.5)
        idx = max(1, min(n - 2, idx))
    else:
        idx = int(t * (n - 2) + 0.5)
        idx = max(0, min(n - 2, idx))
    if _same_count(src, x, y, w, h) >= 2:
        colour = src[x, y]
        nw = any(_in(x + dx, y + dy, w, h) and src[x + dx, y + dy] != colour for dx, dy in _NW)
        se = any(_in(x + dx, y + dy, w, h) and src[x + dx, y + dy] != colour for dx, dy in _SE)
        if nw and not se:
            idx -= 1
        elif se and not nw:
            idx += 1
    return ramp[max(0, min(n - 1, idx))]


def _components(src, w: int, h: int, colour: tuple[int, int, int]) -> list[list[tuple[int, int]]]:
    seen = [[False] * w for _ in range(h)]
    found: list[list[tuple[int, int]]] = []
    for y in range(h):
        for x in range(w):
            if seen[y][x] or src[x, y] != colour:
                continue
            stack = [(x, y)]
            seen[y][x] = True
            cells: list[tuple[int, int]] = []
            while stack:
                cx, cy = stack.pop()
                cells.append((cx, cy))
                for dx, dy in _ORTHO:
                    nx, ny = cx + dx, cy + dy
                    if _in(nx, ny, w, h) and not seen[ny][nx] and src[nx, ny] == colour:
                        seen[ny][nx] = True
                        stack.append((nx, ny))
            found.append(cells)
    return found


def _rel_mask(cells: list[tuple[int, int]]) -> frozenset[tuple[int, int]]:
    xs = [cell[0] for cell in cells]
    ys = [cell[1] for cell in cells]
    x0, y0 = min(xs), min(ys)
    return frozenset((x - x0, y - y0) for x, y in cells)


def _on_commerce(kinds: list[list[str]], cells: list[tuple[int, int]]) -> bool:
    return all(kinds[y // TILE][x // TILE] == "commerce" for x, y in cells)


def _zone_letters(src, w: int, h: int, kinds: list[list[str]], colour: tuple[int, int, int], masks: tuple) -> list[list[tuple[int, int]]]:
    found = []
    for cells in _components(src, w, h, colour):
        if not _on_commerce(kinds, cells):
            continue
        if _rel_mask(cells) in masks:
            found.append(cells)
    return found


def _zone_border(src, w: int, h: int, kinds: list[list[str]], colour: tuple[int, int, int]) -> list[tuple[int, int]]:
    """The lot frame of each 3x3 commerce block, even when it touches the windows."""
    span = TILE * 3
    pixels: list[tuple[int, int]] = []
    tiles_y, tiles_x = h // TILE, w // TILE
    for by in range(tiles_y - 2):
        for bx in range(tiles_x - 2):
            if any(kinds[by + dy][bx + dx] != "commerce" for dy in range(3) for dx in range(3)):
                continue
            ox, oy = bx * TILE, by * TILE
            for y in range(oy, oy + span):
                for x in range(ox, ox + span):
                    if src[x, y] != colour:
                        continue
                    lx, ly = x - ox, y - oy
                    if 1 < lx < span - 2 and 1 < ly < span - 2:
                        continue
                    neighbours = 0
                    for dx, dy in _ORTHO:
                        nx, ny = x + dx, y + dy
                        if _in(nx, ny, w, h) and src[nx, ny] == colour:
                            neighbours += 1
                    if neighbours <= 2:
                        pixels.append((x, y))
    return pixels


def _letter_shade(src, w: int, h: int, cells: list[tuple[int, int]], taken: set[tuple[int, int]]) -> dict[tuple[int, int], tuple[int, int, int]]:
    """A 1px dark edge where a letter sits on a light grey face."""
    shade: dict[tuple[int, int], tuple[int, int, int]] = {}
    group = set(cells)
    for x, y in cells:
        for dx, dy in _ORTHO:
            nx, ny = x + dx, y + dy
            if not _in(nx, ny, w, h) or (nx, ny) in group or (nx, ny) in taken:
                continue
            if src[nx, ny] not in _LIGHT_BEHIND:
                continue
            shade[(nx, ny)] = LETTER_SHADE
    return shade


def _solid_marks(
    src, w: int, h: int, kinds: list[list[str]], bright_zones: bool = True
) -> dict[tuple[int, int], tuple[int, int, int]]:
    """Lot borders and zone letters are one flat colour. Lawns stay dithered.

    A residential border is a stroke: almost every pixel has at most two
    orthogonal neighbours. A zone letter is a compact mark on a building tile.
    Tree canopies fail both tests. Commercial and industrial letters are the
    C and I shapes, and their lot borders are the frame of each 3x3.
    """
    marks: dict[tuple[int, int], tuple[int, int, int]] = {}
    for colour, solid in ((SRC_GREEN, GRASS_LINE), (SRC_DGREEN, HEDGE_LINE)):
        for cells in _components(src, w, h, colour):
            if len(cells) < 12:
                continue
            group = set(cells)
            thin = 0
            for x, y in cells:
                neighbours = sum((x + dx, y + dy) in group for dx, dy in _ORTHO)
                if neighbours <= 2:
                    thin += 1
            stroke = thin / len(cells) >= 0.9
            xs = [cell[0] for cell in cells]
            ys = [cell[1] for cell in cells]
            on_building = all(kinds[y // TILE][x // TILE] in ("building", "commerce") for x, y in cells)
            glyph = on_building and len(cells) <= 80 and max(xs) - min(xs) <= 13 and max(ys) - min(ys) <= 13
            if stroke or glyph:
                for cell in cells:
                    marks[cell] = solid
    if bright_zones:
        pairs = ((SRC_LBLUE, COM_LINE, (_C_MARK, _C_LOT)), (SRC_YELLOW, IND_LINE, (_I_MARK,)))
    else:
        pairs = ((SRC_LBLUE, _COM_LINE_BEFORE, ()), (SRC_YELLOW, _IND_LINE_BEFORE, ()))
    for colour, solid, masks in pairs:
        if masks:
            letters = _zone_letters(src, w, h, kinds, colour, masks)
            taken: set[tuple[int, int]] = {cell for group in letters for cell in group}
            for cells in letters:
                for cell in cells:
                    marks[cell] = solid
                for cell, shade in _letter_shade(src, w, h, cells, taken).items():
                    marks[cell] = shade
            for cell in _zone_border(src, w, h, kinds, colour):
                if cell not in taken:
                    marks[cell] = solid
            continue
        for cells in _components(src, w, h, colour):
            if len(cells) < 12 or not _on_commerce(kinds, cells):
                continue
            group = set(cells)
            thin = sum(sum((x + dx, y + dy) in group for dx, dy in _ORTHO) <= 2 for x, y in cells) / len(cells)
            xs = [cell[0] for cell in cells]
            ys = [cell[1] for cell in cells]
            glyph = len(cells) <= 80 and max(xs) - min(xs) <= 13 and max(ys) - min(ys) <= 13
            if thin >= 0.9 or glyph:
                for cell in cells:
                    marks[cell] = solid
    return marks


def _axis_opposed(src, x: int, y: int, w: int, h: int) -> bool:
    """True when red and blue sit on both sides of one axis: a checker, not a seam."""
    colour = src[x, y]
    if colour not in (SRC_RED, SRC_BLUE):
        return False
    other = SRC_BLUE if colour == SRC_RED else SRC_RED
    north = _in(x, y - 1, w, h) and src[x, y - 1] == other
    south = _in(x, y + 1, w, h) and src[x, y + 1] == other
    west = _in(x - 1, y, w, h) and src[x - 1, y] == other
    east = _in(x + 1, y, w, h) and src[x + 1, y] == other
    return (north and south) or (west and east)


def _dither_recolour(src, w: int, h: int) -> dict[tuple[int, int], tuple[int, int, int]]:
    """Calm a red/blue checker into one hue.

    A diagonal corner where a red roof meets a blue wall is left alone.
    A checker that continues into solid red, as on block 342's left wall,
    becomes dusty rose. A checker of red stitches in a blue wall, as on
    block 306, becomes a muted blue. The two classic colours keep different
    outputs, so the seam does not collapse.
    """
    opposed = {(x, y) for y in range(h) for x in range(w) if _axis_opposed(src, x, y, w, h)}
    chosen = set(opposed)
    for y in range(h):
        for x in range(w):
            colour = src[x, y]
            if colour not in (SRC_RED, SRC_BLUE) or (x, y) in opposed:
                continue
            touches = False
            for dx, dy in _ORTHO:
                nx, ny = x + dx, y + dy
                if _in(nx, ny, w, h) and (nx, ny) in opposed and src[nx, ny] != colour:
                    touches = True
                    break
            if not touches:
                continue
            # A cap of the checker has no solid face of its own colour behind it.
            solid = 0
            for dx, dy in _ORTHO:
                nx, ny = x + dx, y + dy
                if _in(nx, ny, w, h) and src[nx, ny] == colour and (nx, ny) not in opposed:
                    solid += 1
            if solid == 0:
                chosen.add((x, y))
    seen: set[tuple[int, int]] = set()
    out: dict[tuple[int, int], tuple[int, int, int]] = {}
    for start in chosen:
        if start in seen:
            continue
        stack = [start]
        seen.add(start)
        cells: list[tuple[int, int]] = []
        while stack:
            cx, cy = stack.pop()
            cells.append((cx, cy))
            for dx, dy in _ORTHO:
                nxt = (cx + dx, cy + dy)
                if nxt in chosen and nxt not in seen:
                    seen.add(nxt)
                    stack.append(nxt)
        red_solid = blue_solid = 0
        for cx, cy in cells:
            for dx, dy in _ORTHO:
                nx, ny = cx + dx, cy + dy
                if not _in(nx, ny, w, h) or (nx, ny) in chosen:
                    continue
                neighbour = src[nx, ny]
                if neighbour == SRC_RED:
                    red_solid += 1
                elif neighbour == SRC_BLUE:
                    blue_solid += 1
        # Block 342's strip runs into the red base. Block 306's stitches do not.
        rose = red_solid >= blue_solid
        for cx, cy in cells:
            if rose:
                out[(cx, cy)] = ROSE_RAMP[1] if src[cx, cy] == SRC_RED else ROSE_MATE
            elif src[cx, cy] == SRC_RED:
                out[(cx, cy)] = BLUE_MATE
    return out


def _road_shoulder(src, x: int, y: int, w: int, h: int, cables: set[tuple[int, int]]) -> bool:
    """Dirt rim against asphalt and ties. A wire or its pole does not count."""
    for dx, dy in _ORTHO:
        nx, ny = x + dx, y + dy
        if not _in(nx, ny, w, h) or (nx, ny) in cables:
            continue
        neighbour = src[nx, ny]
        if neighbour in (SRC_DGREY, SRC_GREY):
            return True
        if neighbour != BLACK:
            continue
        fx, fy = nx + dx, ny + dy
        if _in(fx, fy, w, h) and (fx, fy) not in cables and src[fx, fy] in (SRC_DGREY, SRC_GREY):
            return True
    return False


def _cables(src, w: int, h: int) -> set[tuple[int, int]]:
    """Thin dark-grey strokes are wires. A road fill is not."""
    wires: set[tuple[int, int]] = set()
    for cells in _components(src, w, h, SRC_DGREY):
        if len(cells) < 4:
            continue
        group = set(cells)
        thin = sum(sum((x + dx, y + dy) in group for dx, dy in _ORTHO) <= 2 for x, y in cells) / len(cells)
        if thin >= 0.85:
            wires.update(cells)
    return wires


def paint(
    original: Image.Image,
    kinds: list[list[str]],
    scheme: DirtScheme = SCHEME_B,
    bright_zones: bool = True,
    plain_wires: bool = True,
) -> Image.Image:
    """Recolour `original`. `kinds` is one label per 16px cell, row-major."""
    w, h = original.size
    if w % TILE or h % TILE:
        raise SystemExit(f"image {w}x{h} is not a multiple of {TILE}")
    src = original.load()
    out = Image.new("RGB", (w, h))
    dst = out.load()
    surface_colours = {SRC_GREY, SRC_DGREY, SRC_LGREY, SRC_LBLUE, SRC_BLUE, SRC_YELLOW, SRC_ORANGE}
    boxes = _component_boxes(src, w, h, surface_colours)
    mask = _caster_mask(kinds, src, w, h)
    counts = _caster_counts(src, w, h, mask)
    solid = _solid_marks(src, w, h, kinds, bright_zones)
    dither = _dither_recolour(src, w, h)
    cables = _cables(src, w, h)
    for y in range(h):
        kind_row = kinds[y // TILE]
        for x in range(w):
            colour = src[x, y]
            if colour == BLACK:
                dst[x, y] = BLACK
                continue
            if (x, y) in dither:
                dst[x, y] = dither[(x, y)]
                continue
            mat = MATS[kind_row[x // TILE]].get(colour)
            if mat is None:
                raise SystemExit(
                    f"no ramp for {colour} at {(x, y)} kind {kind_row[x // TILE]}"
                )
            ramp = mat.ramp
            if mat.role == "flat":
                dst[x, y] = ramp[0]
                continue
            if mat.role == "soil":
                # Open soil is the same stipple as tan. Only the pixels that
                # touch tan, concrete, or water change, so a soil fill cannot
                # share an edge colour with tan.
                strength = _shadow_strength(src, x, y, w, h, counts)
                if _touches_water(src, x, y, w, h):
                    chosen = scheme.soil_rim
                elif strength >= 2:
                    chosen = scheme.soil_contact
                elif strength == 1 or _touches_ground(src, x, y, w, h, frozenset({SRC_TAN}) | _PAVEMENT):
                    chosen = scheme.soil_rim
                elif _touches_ground(src, x, y, w, h, frozenset({SRC_GREEN, SRC_DGREEN})):
                    chosen = _dirt_colour(x, y, True, scheme)
                else:
                    chosen = _dirt_colour(x, y, False, scheme)
                dst[x, y] = chosen
                continue
            strength = _shadow_strength(src, x, y, w, h, counts)
            if (x, y) in solid and mat.role in ("grass", "hedge", "surface"):
                dst[x, y] = solid[(x, y)]
                continue
            shadowed = _apply_shadow(scheme.ramp if mat.role == "dirt" else ramp, strength) if mat.role in ("grass", "dirt", "hedge") else None
            if mat.role == "hedge":
                body = ramp[:-2]
                idx = 0 if (_hash(x, y) & 7) < 2 else min(1, len(body) - 1)
                if _grass_rim(src, x, y, w, h, counts):
                    idx = len(body) - 1
                chosen = body[idx]
                if shadowed is not None:
                    chosen = shadowed
                dst[x, y] = chosen
                continue
            if mat.role == "dirt":
                edge = _touches_water(src, x, y, w, h) or _dirt_edge(src, x, y, w, h)
                if kind_row[x // TILE] == "road":
                    if plain_wires:
                        edge = edge or _road_shoulder(src, x, y, w, h, cables)
                    elif _touches_ground(src, x, y, w, h, frozenset({SRC_DGREY, SRC_GREY})):
                        edge = True
                chosen = _dirt_colour(x, y, edge, scheme)
                if shadowed is not None:
                    chosen = shadowed
                dst[x, y] = chosen
                continue
            if mat.role == "grass":
                body = ramp[:-2]
                idx = _dither_index(x, y, 1, _GRASS_WEIGHTS)
                if _grass_rim(src, x, y, w, h, counts):
                    idx = len(body) - 1
                chosen = body[min(idx, len(body) - 1)]
                if shadowed is not None:
                    chosen = shadowed
                dst[x, y] = chosen
                continue
            if mat.role == "asphalt":
                if (x, y) in cables:
                    dst[x, y] = CABLE if plain_wires else _CABLE_BEFORE
                    continue
                # The curb is the outer edge only. A dash or a car is inside
                # the roadway, so it does not pull the centre line off centre.
                curb = _touches_ground(
                    src,
                    x,
                    y,
                    w,
                    h,
                    frozenset({SRC_TAN, SRC_DTAN, SRC_LBLUE, SRC_BLUE, BLACK, SRC_GREEN, SRC_DGREEN}),
                )
                dst[x, y] = ramp[1] if curb else ramp[0]
                continue
            if mat.role == "water":
                body = ramp[:3]
                idx = _dither_index(x, y, 2, _WATER_WEIGHTS)
                chosen = body[min(idx, len(body) - 1)]
                coast = _coast_strength(src, x, y, w, h)
                if coast >= 2:
                    chosen = ramp[4]
                elif coast == 1:
                    chosen = ramp[3]
                dst[x, y] = chosen
                continue
            if mat.role == "surface":
                dst[x, y] = _paint_surface(src, x, y, w, h, ramp, boxes.get((x, y), (x, y, x, y)))
                continue
            raise SystemExit(f"unknown role {mat.role}")
    return out


def load_atlas() -> Image.Image:
    if not ATLAS_PATH.is_file():
        raise SystemExit(f"missing tile atlas: {ATLAS_PATH}")
    image = Image.open(ATLAS_PATH).convert("RGB")
    if image.size[0] % TILE or image.size[1] % TILE:
        raise SystemExit(f"atlas {image.size} is not a multiple of {TILE}")
    return image


def crop_tile(atlas: Image.Image, index: int) -> Image.Image:
    columns = atlas.size[0] // TILE
    col, row = index % columns, index // columns
    box = (col * TILE, row * TILE, col * TILE + TILE, row * TILE + TILE)
    return atlas.crop(box)


def assemble(atlas: Image.Image, base: int, width: int, height: int) -> Image.Image:
    image = Image.new("RGB", (width * TILE, height * TILE))
    for y in range(height):
        for x in range(width):
            image.paste(crop_tile(atlas, base + y * width + x), (x * TILE, y * TILE))
    return image


def kinds_for(indexes: list[list[int]]) -> list[list[str]]:
    return [[tile_kind(index) for index in row] for row in indexes]


def paint_grid(
    atlas: Image.Image,
    grid: list[list[int]],
    scheme: DirtScheme = SCHEME_B,
    plain_wires: bool = True,
) -> tuple[Image.Image, Image.Image]:
    height = len(grid)
    width = len(grid[0])
    classic = Image.new("RGB", (width * TILE, height * TILE))
    for y, row in enumerate(grid):
        for x, index in enumerate(row):
            classic.paste(crop_tile(atlas, index), (x * TILE, y * TILE))
    return classic, paint(classic, kinds_for(grid), scheme, plain_wires=plain_wires)


def luma(rgb: tuple[int, int, int]) -> float:
    red, green, blue = rgb
    return 0.299 * red + 0.587 * green + 0.114 * blue


def compare(classic: Image.Image, svga: Image.Image, palette: set[tuple[int, int, int]]) -> dict:
    cw, ch = classic.size
    if svga.size != classic.size:
        raise RuntimeError(f"size {svga.size} != classic {classic.size}")
    cpx = classic.load()
    spx = svga.load()
    stats = {
        "palette_bad": 0,
        "ink_bad": 0,
        "collapsed": 0,
        "boundaries": 0,
        "strong": 0,
        "strong_kept": 0,
        "new_strong": 0,
        "pixels": cw * ch,
    }
    samples: list[str] = []
    for y in range(ch):
        for x in range(cw):
            src = cpx[x, y]
            dst = spx[x, y]
            if dst not in palette:
                stats["palette_bad"] += 1
                if len(samples) < 6:
                    samples.append(f"palette {dst} at {(x, y)} from {src}")
            if (src == BLACK) != (dst == BLACK):
                stats["ink_bad"] += 1
                if len(samples) < 6:
                    samples.append(f"ink {src} -> {dst} at {(x, y)}")
            for nx, ny in ((x + 1, y), (x, y + 1)):
                if nx >= cw or ny >= ch:
                    continue
                src2 = cpx[nx, ny]
                dst2 = spx[nx, ny]
                if src != src2:
                    stats["boundaries"] += 1
                    if dst == dst2:
                        stats["collapsed"] += 1
                        if len(samples) < 8:
                            samples.append(
                                f"collapsed {src}/{src2} -> {dst} at {(x, y)}-{(nx, ny)}"
                            )
                classic_delta = abs(luma(src) - luma(src2))
                svga_delta = abs(luma(dst) - luma(dst2))
                if classic_delta >= STRONG_LUMA:
                    stats["strong"] += 1
                    if svga_delta >= STRONG_LUMA * 0.5:
                        stats["strong_kept"] += 1
                elif classic_delta < 12 and svga_delta >= STRONG_LUMA:
                    stats["new_strong"] += 1
    stats["samples"] = samples
    return stats


def _ok(stats: dict) -> bool:
    return stats["palette_bad"] == 0 and stats["ink_bad"] == 0 and stats["collapsed"] == 0


def _stamp(grid: list[list[int]], index: int, x: int, y: int, width: int = 1, height: int = 1) -> None:
    for dy in range(height):
        for dx in range(width):
            if grid[y + dy][x + dx] != 0:
                raise SystemExit(f"scene overlap at {(x + dx, y + dy)}")
            grid[y + dy][x + dx] = index + dy * width + dx


def neighbourhood() -> list[list[int]]:
    """A riverbank with woods, an empty lot, and houses. Roads are a later batch."""
    width, height = 22, 15
    grid = [[0 for _ in range(width)] for _ in range(height)]
    for x in range(width):
        grid[0][x] = 2
        grid[1][x] = 13 if x % 2 == 0 else 14
    for x, index in ((2, 21), (5, 22), (8, 28), (10, 1), (15, 36), (19, 21)):
        _stamp(grid, index, x, 3)
    woods = (
        (37, 40, 41, 42),
        (43, 37, 40, 41),
        (42, 43, 40, 37),
    )
    for dy, row in enumerate(woods):
        for dx, index in enumerate(row):
            _stamp(grid, index, 1 + dx, 4 + dy)
    _stamp(grid, 240, 16, 4, 3, 3)
    for x, y, index in (
        (7, 5, 22),
        (8, 6, 21),
        (12, 4, 36),
        (13, 5, 28),
        (14, 6, 22),
        (6, 8, 30),
        (20, 10, 28),
        (5, 12, 21),
        (8, 13, 21),
        (20, 13, 28),
        (3, 14, 22),
        (10, 14, 36),
    ):
        _stamp(grid, index, x, y)
    _stamp(grid, 261, 1, 9, 3, 3)
    _stamp(grid, 315, 8, 9, 3, 3)
    _stamp(grid, 378, 15, 9, 3, 3)
    _stamp(grid, 252, 20, 7)
    for x, index in ((2, 249), (4, 250), (6, 255), (11, 258), (13, 260), (18, 252)):
        _stamp(grid, index, x, 13)
    return grid


def downtown() -> list[list[int]]:
    """Commercial, industrial, and a residential row, on open dirt."""
    width, height = 22, 14
    grid = [[0 for _ in range(width)] for _ in range(height)]
    for x, base in ((1, 423), (5, 450), (9, 468), (13, 531), (17, 603)):
        _stamp(grid, base, x, 1, 3, 3)
    for x, base in ((1, 612), (5, 621), (9, 648), (13, 675), (17, 684)):
        _stamp(grid, base, x, 5, 3, 3)
    # Tile 621 is the chimney cell. Powered, it plays the smoke frames at 852.
    grid[5][5] = 852
    _stamp(grid, 240, 1, 10, 3, 3)
    _stamp(grid, 261, 5, 10, 3, 3)
    _stamp(grid, 297, 9, 10, 3, 3)
    _stamp(grid, 315, 13, 10, 3, 3)
    for x, index in ((17, 249), (18, 252), (19, 255), (20, 258)):
        _stamp(grid, index, x, 11)
    return grid


def transport() -> list[list[int]]:
    """A road, a road/rail crossing, power lines, and a bridge over water."""
    width, height = 18, 12
    grid = [[0 for _ in range(width)] for _ in range(height)]
    for x in range(width):
        grid[2][x] = 66
    grid[2][4] = 76
    grid[2][11] = 238
    grid[2][14] = 77
    for y in (3, 4, 5):
        grid[y][4] = 67
    grid[6][4] = 237
    for x in range(width):
        if x != 4:
            grid[6][x] = 226
    for y in (0, 1, 3, 4, 5):
        grid[y][11] = 227
    for y in (7, 8):
        grid[y][4] = 67
    for y in (3, 4, 5, 7, 8):
        grid[y][14] = 211
    grid[6][14] = 221
    for x in range(width):
        grid[9][x] = 210
    grid[9][4] = 78
    grid[9][14] = 220
    for x in range(width):
        grid[10][x] = 2
        grid[11][x] = 2
    for x in range(6, 10):
        grid[10][x] = 64
    grid[10][4] = 65
    grid[11][4] = 65
    _stamp(grid, 450, 6, 3, 3, 3)
    grid[4][0] = 249
    grid[4][1] = 252
    grid[4][16] = 255
    return grid


def _blank(width: int, height: int, colour: tuple[int, int, int] = BG) -> Image.Image:
    return Image.new("RGB", (width, height), colour)


def _draw_text(image: Image.Image, text: str, x: int, y: int, scale: int, colour: tuple[int, int, int]) -> None:
    px = image.load()
    width, height = image.size
    for char in text:
        glyph = FONT.get(char, FONT[" "])
        for gy, row in enumerate(glyph):
            for gx, bit in enumerate(row):
                if bit != "#":
                    continue
                for sy in range(scale):
                    for sx in range(scale):
                        px_x = x + gx * scale + sx
                        px_y = y + gy * scale + sy
                        if 0 <= px_x < width and 0 <= px_y < height:
                            px[px_x, px_y] = colour
        x += 6 * scale


def _text(text: str, scale: int = 2, colour: tuple[int, int, int] = FG) -> Image.Image:
    width = max(1, (len(text) * 6 - 1) * scale)
    image = _blank(width, 7 * scale)
    _draw_text(image, text, 0, 0, scale, colour)
    return image


def _hstack(images: list[Image.Image], gap: int) -> Image.Image:
    height = max(image.height for image in images)
    width = sum(image.width for image in images) + gap * (len(images) - 1)
    out = _blank(width, height)
    x = 0
    for image in images:
        out.paste(image, (x, (height - image.height) // 2))
        x += image.width + gap
    return out


def _vstack(images: list[Image.Image], gap: int) -> Image.Image:
    width = max(image.width for image in images)
    height = sum(image.height for image in images) + gap * (len(images) - 1)
    out = _blank(width, height)
    y = 0
    for image in images:
        out.paste(image, (0, y))
        y += image.height + gap
    return out


def _fit_width(image: Image.Image, width: int) -> Image.Image:
    if image.width == width:
        return image
    out = _blank(width, image.height)
    out.paste(image, (0, 0))
    return out


def _banner(text: str, width: int) -> Image.Image:
    label = _text(text, 2)
    bar = _blank(width, label.height + 14)
    bar.paste(label, (8, 7))
    return bar


def _scaled(image: Image.Image, scale: int) -> Image.Image:
    return image.resize((image.width * scale, image.height * scale), NEAREST)


def _pair(classic: Image.Image, svga: Image.Image, caption: str, scale: int) -> Image.Image:
    left = _scaled(classic, scale)
    right = _scaled(svga, scale)
    pair = _hstack([left, right], 4)
    label = _text(caption, 2)
    width = max(pair.width, label.width)
    cell = _blank(width, label.height + 4 + pair.height)
    cell.paste(label, ((width - label.width) // 2, 0))
    cell.paste(pair, ((width - pair.width) // 2, label.height + 4))
    return cell


def _grid(cells: list[Image.Image], columns: int) -> Image.Image:
    rows = []
    for start in range(0, len(cells), columns):
        rows.append(_hstack(cells[start : start + columns], 12))
    return _vstack(rows, 12)


def _save(image: Image.Image, path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    image.save(path, optimize=True)


def _histogram(image: Image.Image) -> dict[tuple[int, int, int], int]:
    counts: dict[tuple[int, int, int], int] = {}
    px = image.load()
    for y in range(image.height):
        for x in range(image.width):
            rgb = px[x, y]
            counts[rgb] = counts.get(rgb, 0) + 1
    return counts


def _warcraft_compare() -> Image.Image:
    """The sampled path beside a same-size patch of the new dirt, at 3x.

    The patch is painted in world coordinates. A grass margin on each side
    shows the solid 1px rim; the middle is the calm field.
    """
    ref = ROOT / "svga-refs" / "warcraft.png"
    if not ref.is_file():
        raise SystemExit(f"missing Warcraft reference: {ref}")
    shot = Image.open(ref).convert("RGB")
    if shot.size != (640, 400):
        raise SystemExit(f"Warcraft shot is {shot.size}, expected 640x400")
    small = shot.resize((320, 200), Image.Resampling.BOX)
    # Upper path bbox is x 186-229, y 12-105. Four pixels of grass around it.
    crop = small.crop((182, 8, 234, 110))
    width, height = crop.size
    patch = Image.new("RGB", (width, height))
    dst = patch.load()
    grass = GRASS_RAMP[:-2]
    margin = 6
    for y in range(height):
        for x in range(width):
            wx, wy = x + 128, y + 64
            if x < margin or x >= width - margin:
                idx = _dither_index(wx, wy, 1, _GRASS_WEIGHTS)
                dst[x, y] = grass[min(idx, len(grass) - 1)]
            elif x == margin or x == width - margin - 1:
                dst[x, y] = _dirt_colour(wx, wy, True)
            else:
                dst[x, y] = _dirt_colour(wx, wy, False)
    left = _vstack([_text("WARCRAFT", 2), _scaled(crop, 3)], 6)
    right = _vstack([_text("SVGA", 2), _scaled(patch, 3)], 6)
    pair = _hstack([left, right], 16)
    return _vstack(
        [
            _banner("WARCRAFT PATH AND SVGA DIRT  3X", pair.width),
            pair,
        ],
        12,
    )


def main() -> int:
    _assert_hash_aperiodic()
    palette = set(vga_palette_8bit())
    _require_palette(palette)
    atlas = load_atlas()
    lines = [
        "SVGA batches 1 to 3 verification",
        "groups: terrain, residential 240-404, commercial 423-611, industrial 612-692,",
        "chimney frames 852-859 and 884-915, roads 64-207, power 208-222,",
        "rails 224-239, drawbridge frames 828-831 and 948-951",
        "held back: tiles 38-39 (unused), rubble, flood, fire, hospital, church,",
        "port, airport, civic buildings, coal smoke, the menu toggle",
        f"palette unique RGB triples: {len(palette)}",
        "Warcraft path: 640x400 shot, box-filtered to the 320x200 frame.",
        "Upper path 996 px (bbox x 186-229, y 12-105) plus the clean stretch",
        "past the catapults, 185 px. Combined 1181 px. None of the eight is a mode 13h entry.",
        "combined count  rgb       nearest VGA          idx   dist",
        " 320  (85, 40, 20)    (65, 40, 32)         209   23.32",
        " 312  (73, 32, 16)    (65, 32, 0)          186   17.89  tie with (65, 32, 32); warm kept",
        " 200  (97, 48, 24)    (113, 56, 0)         114   29.93",
        " 153  (65, 28, 16)    (65, 32, 0)          186   16.49  tie with (65, 32, 32); warm kept",
        "  74  (105, 53, 30)   (113, 56, 56)        136   27.37",
        "  69  (56, 20, 12)    (65, 16, 0)          185   15.52",
        "  48  (48, 16, 12)    (65, 16, 0)          185   20.81",
        "   5  (117, 65, 28)   (113, 69, 56)        137   28.57",
        "SVGA dirt is variant B: base (113, 56, 0) 79.1%, neighbour (113, 69, 56) 17.0%,",
        "accent (170, 85, 0) 4.0%. Rim (113, 28, 0). Contact (113, 0, 28).",
        "Variant A remains only in the A/B preview.",
        "Building red is dusty rose: face (113, 56, 56), highlight (113, 81, 81), shade (65, 32, 32).",
        "Block 342's red/blue checker is the red wall, painted rose and (113, 56, 69).",
        "Block 306's red stitches are a blue wall, painted (16, 0, 65).",
        "Glass light step is (125, 158, 255). Wall light step is (0, 0, 170).",
        "Lot borders and zone letters are solid.",
        "",
    ]
    failed = False

    def check(name: str, classic: Image.Image, svga: Image.Image) -> None:
        nonlocal failed
        stats = compare(classic, svga, palette)
        recall = (stats["strong_kept"] / stats["strong"]) if stats["strong"] else 1.0
        status = "OK" if _ok(stats) else "FAIL"
        if status == "FAIL":
            failed = True
        lines.append(
            f"{status} {name}: palette={stats['palette_bad']} ink={stats['ink_bad']} "
            f"collapsed={stats['collapsed']}/{stats['boundaries']} "
            f"strong={stats['strong_kept']}/{stats['strong']} ({recall:.3f})"
        )
        for sample in stats["samples"]:
            lines.append(f"    {sample}")

    singles = [0, 1, *range(2, 21), *range(21, 38), *range(40, 44), *range(249, 261)]
    for index in singles:
        classic = crop_tile(atlas, index)
        svga = paint(classic, [[tile_kind(index)]])
        check(f"tile {index}", classic, svga)

    blocks = [(240, 3, 3)] + [(261 + i * 9, 3, 3) for i in range(16)]
    painted_blocks: list[tuple[int, Image.Image, Image.Image]] = []
    for base, width, height in blocks:
        classic = assemble(atlas, base, width, height)
        kinds = [[tile_kind(base)] * width for _ in range(height)]
        svga = paint(classic, kinds)
        painted_blocks.append((base, classic, svga))
        check(f"block {base} {width}x{height}", classic, svga)

    scene = neighbourhood()
    classic_scene, svga_scene = paint_grid(atlas, scene)
    check(f"neighbourhood {len(scene[0])}x{len(scene)}", classic_scene, svga_scene)

    shore_rows = [row[:] for row in scene[:4]]
    classic_shore, svga_shore = paint_grid(atlas, shore_rows)
    check("shore strip", classic_shore, svga_shore)

    com_bases = [423] + [432 + i * 9 for i in range(20)]
    ind_bases = [612] + [621 + i * 9 for i in range(8)]
    smoke_ids = [*range(852, 860), *range(884, 916)]
    painted_com: list[tuple[int, Image.Image, Image.Image]] = []
    painted_ind: list[tuple[int, Image.Image, Image.Image]] = []
    for base in com_bases + ind_bases:
        classic = assemble(atlas, base, 3, 3)
        kinds = [["commerce"] * 3 for _ in range(3)]
        svga = paint(classic, kinds)
        (painted_com if base in com_bases else painted_ind).append((base, classic, svga))
        check(f"block {base} 3x3", classic, svga)
    painted_smoke: list[tuple[int, Image.Image, Image.Image]] = []
    for index in smoke_ids:
        classic = crop_tile(atlas, index)
        svga = paint(classic, [["commerce"]])
        painted_smoke.append((index, classic, svga))
        check(f"smoke {index}", classic, svga)

    down = downtown()
    classic_down, svga_down = paint_grid(atlas, down)
    check(f"downtown {len(down[0])}x{len(down)}", classic_down, svga_down)

    road_ids = [*range(64, 223), *range(224, 240), *range(828, 832), *range(948, 952)]
    painted_road: dict[int, tuple[Image.Image, Image.Image]] = {}
    for index in road_ids:
        classic = crop_tile(atlas, index)
        svga = paint(classic, [["road"]])
        painted_road[index] = (classic, svga)
        check(f"tile {index}", classic, svga)

    trip = transport()
    classic_trip, svga_trip = paint_grid(atlas, trip)
    check(f"transport {len(trip[0])}x{len(trip)}", classic_trip, svga_trip)

    def _centre_line(index: int, axis: int) -> None:
        nonlocal failed
        classic, svga = painted_road[index]
        src, dst = classic.load(), svga.load()
        asphalt = [(x, y) for y in range(16) for x in range(16) if src[x, y] == SRC_DGREY]
        dashes = [(x, y) for y in range(16) for x in range(16) if src[x, y] == WHITE]
        if not asphalt or not dashes:
            lines.append(f"FAIL tile {index} has no asphalt centre line")
            failed = True
            return
        lo = min(point[axis] for point in asphalt)
        hi = max(point[axis] for point in asphalt)
        mid = (lo + hi) / 2
        off = [point for point in dashes if abs(point[axis] - mid) > 0.5 or dst[point] != DASH]
        if off:
            lines.append(f"FAIL tile {index} centre line is off the roadway middle ({len(off)})")
            failed = True

    _centre_line(66, 1)
    _centre_line(67, 0)
    cable = painted_road[210][1]
    road = painted_road[66][1]
    cable_src = painted_road[210][0].load()
    road_src = painted_road[66][0].load()
    if any(cable.getpixel((x, y)) != CABLE for y in range(16) for x in range(16) if cable_src[x, y] == SRC_DGREY):
        lines.append("FAIL power wires on 210 are not the lighter cable grey")
        failed = True
    wire_rim = 0
    for y in range(16):
        for x in range(16):
            if cable_src[x, y] != SRC_TAN:
                continue
            if not any(
                _in(x + dx, y + dy, 16, 16) and (x + dx, y + dy) in _cables(cable_src, 16, 16)
                for dx, dy in _ORTHO
            ):
                continue
            if cable.getpixel((x, y)) in (DIRT_RIM, DIRT_CONTACT):
                wire_rim += 1
    if wire_rim:
        lines.append(f"FAIL dirt beside the wires on 210 is still a rim ({wire_rim})")
        failed = True
    if DIRT_RIM not in set(_histogram(road)):
        lines.append("FAIL the road shoulder on 66 lost its dirt rim")
        failed = True
    if any(road.getpixel((x, y)) == CABLE for y in range(16) for x in range(16) if road_src[x, y] == SRC_DGREY):
        lines.append("FAIL the roadway on 66 was painted as a cable")
        failed = True
    for index, (classic, svga) in painted_road.items():
        src, dst = classic.load(), svga.load()
        bad_dash = bad_tie = bad_asphalt = 0
        for y in range(16):
            for x in range(16):
                colour, out = src[x, y], dst[x, y]
                if colour == WHITE and out != DASH:
                    bad_dash += 1
                elif colour == SRC_GREY and out != TIE:
                    bad_tie += 1
                elif colour == SRC_YELLOW and out != RAIL_MARK:
                    bad_dash += 1
                elif colour == SRC_DGREY and out not in (ASPHALT_BODY, ASPHALT_EDGE, CABLE):
                    bad_asphalt += 1
        if bad_dash or bad_tie or bad_asphalt:
            lines.append(
                f"FAIL tile {index} transport colours dash={bad_dash} tie={bad_tie} asphalt={bad_asphalt}"
            )
            failed = True

    hot = {
        (255, 255, 0),
        (255, 255, 125),
        (255, 127, 0),
        (255, 125, 0),
        (255, 65, 0),
        (255, 190, 0),
        (255, 0, 0),
        (170, 0, 0),
        (0, 0, 255),
        (85, 85, 255),
        (85, 255, 85),
    }
    for image, name in (
        (next(image for origin, _c, image in painted_com if origin == 423), "lot 423"),
        (next(image for origin, _c, image in painted_com if origin == 468), "block 468"),
        (next(image for origin, _c, image in painted_com if origin == 531), "block 531"),
        (next(image for origin, _c, image in painted_ind if origin == 612), "lot 612"),
        (next(image for origin, _c, image in painted_ind if origin == 621), "block 621"),
        (next(image for origin, _c, image in painted_smoke if origin == 888), "smoke 888"),
    ):
        present = hot & set(_histogram(image))
        if present:
            lines.append(f"FAIL {name} still has an intense colour {sorted(present)}")
            failed = True

    def _border_solid(base: int, source: tuple[int, int, int], solid_colour: tuple[int, int, int], group) -> None:
        nonlocal failed
        classic = next(image for origin, image, _svga in group if origin == base)
        svga = next(image for origin, _classic, image in group if origin == base)
        cpx, spx = classic.load(), svga.load()
        bad = 0
        for x in range(classic.width):
            if cpx[x, 0] == source and spx[x, 0] != solid_colour:
                bad += 1
        if bad:
            lines.append(f"FAIL lot {base} border is not a solid line ({bad})")
            failed = True

    _border_solid(423, SRC_LBLUE, COM_LINE, painted_com)
    _border_solid(612, SRC_YELLOW, IND_LINE, painted_ind)

    def _letter_pixels(base: int, colour: tuple[int, int, int], mask: frozenset[tuple[int, int]]) -> list[tuple[int, int]]:
        nonlocal failed
        classic = assemble(atlas, base, 3, 3)
        src = classic.load()
        found = [cells for cells in _components(src, 48, 48, colour) if _rel_mask(cells) == mask]
        if len(found) != 1:
            lines.append(f"FAIL block {base} has {len(found)} copies of the zone letter")
            failed = True
            return []
        return found[0]

    for base in com_bases:
        mask = _C_LOT if base == 423 else _C_MARK
        svga = next(image for origin, _classic, image in painted_com if origin == base)
        pixels = _letter_pixels(base, SRC_LBLUE, mask)
        spx = svga.load()
        wrong = sum(spx[x, y] != COM_LINE for x, y in pixels)
        if wrong:
            lines.append(f"FAIL the C on {base} is not the light blue ({wrong})")
            failed = True
    i_pixels = _letter_pixels(612, SRC_YELLOW, _I_MARK)
    i_px = next(image for origin, _classic, image in painted_ind if origin == 612).load()
    i_wrong = sum(i_px[x, y] != IND_LINE for x, y in i_pixels)
    if i_wrong:
        lines.append(f"FAIL the I on 612 is not the bright yellow ({i_wrong})")
        failed = True
    for base in ind_bases:
        if base == 612:
            continue
        classic = next(image for origin, image, _svga in painted_ind if origin == base)
        src = classic.load()
        extra = [cells for cells in _components(src, 48, 48, SRC_YELLOW) if _rel_mask(cells) == _I_MARK]
        if extra:
            lines.append(f"FAIL block {base} has an I the empty-lot check did not expect")
            failed = True
    roof = next(image for origin, _classic, image in painted_com if origin == 558)
    slate = next(image for origin, _classic, image in painted_com if origin == 504)
    if LETTER_SHADE not in set(_histogram(roof)):
        lines.append("FAIL the C on light roof 558 has no dark edge")
        failed = True
    if LETTER_SHADE in set(_histogram(slate)):
        lines.append("FAIL the C on dark roof 504 grew a shadow it does not need")
        failed = True

    open_water = paint(crop_tile(atlas, 2), [["water"]])
    shore = paint(crop_tile(atlas, 13), [["water"]])
    if _histogram(open_water).get(WATER_RAMP[4], 0) or _histogram(open_water).get(WATER_RAMP[3], 0):
        lines.append("FAIL open water picked up a coastline colour")
        failed = True
    if _histogram(shore).get(WATER_RAMP[4], 0) == 0:
        lines.append("FAIL south shore has no coastline band")
        failed = True

    dirt = paint(crop_tile(atlas, 0), [["ground"]])
    dirt_counts = _histogram(dirt)
    if dirt_counts.get(DIRT_BASE, 0) < dirt_counts.get(DIRT_NEAR, 0):
        lines.append(f"FAIL dirt tile is not the lighter base: {dirt_counts}")
        failed = True
    if dirt_counts.get(DIRT_RIM, 0) or dirt_counts.get((65, 32, 0), 0) or dirt_counts.get((113, 56, 56), 0):
        lines.append(f"FAIL open dirt tile picked up an edge or the old field: {dirt_counts}")
        failed = True
    if dirt_counts.get(DIRT_NEAR, 0) == 0:
        lines.append(f"FAIL dirt tile is missing the neighbour: {dirt_counts}")
        failed = True
    forbidden = {
        (85, 255, 85),
        (113, 113, 56),
        (113, 97, 56),
        (255, 127, 0),
        (255, 0, 0),
        (170, 0, 0),
        (0, 0, 255),
        (125, 125, 255),
        (85, 85, 255),
        (255, 190, 125),
        (255, 223, 125),
        (255, 158, 125),
        (255, 235, 182),
        (65, 32, 0),
        (65, 40, 32),
        (65, 0, 16),
        (65, 0, 32),
        (65, 0, 48),
        SRC_TAN,
        SRC_DTAN,
    }
    for image, name in ((svga_scene, "neighbourhood"), (dirt, "dirt")):
        present = forbidden & set(_histogram(image))
        if present:
            lines.append(f"FAIL {name} contains forbidden colours {sorted(present)}")
            failed = True
    for base in (261, 279, 315, 351):
        block = next(image for origin, _classic, image in painted_blocks if origin == base)
        present = forbidden & set(_histogram(block))
        if present:
            lines.append(f"FAIL block {base} still has classic or reddish ground {sorted(present)}")
            failed = True
        classic = next(image for origin, image, _svga in painted_blocks if origin == base)
        cpx = classic.load()
        spx = block.load()
        missed = 0
        for y in range(classic.height):
            for x in range(classic.width):
                if cpx[x, y] in (SRC_TAN, SRC_DTAN) and spx[x, y] in (SRC_TAN, SRC_DTAN):
                    missed += 1
        if missed:
            lines.append(f"FAIL block {base} left {missed} classic ground pixels")
            failed = True

    lot_classic = next(image for origin, image, _svga in painted_blocks if origin == 240)
    lot_svga = next(image for origin, _classic, image in painted_blocks if origin == 240)
    border_bad = 0
    lcx, lsx = lot_classic.load(), lot_svga.load()
    for x in range(lot_classic.width):
        if lcx[x, 0] == SRC_GREEN and lsx[x, 0] != GRASS_LINE:
            border_bad += 1
    if border_bad:
        lines.append(f"FAIL lot 240 border is not a solid green line ({border_bad})")
        failed = True

    rose_colours = set(ROSE_RAMP)
    block_297 = next(image for origin, _classic, image in painted_blocks if origin == 297)
    classic_297 = next(image for origin, image, _svga in painted_blocks if origin == 297)
    c297, s297 = classic_297.load(), block_297.load()
    rose_bad = 0
    for y in range(classic_297.height):
        for x in range(classic_297.width):
            if c297[x, y] == SRC_RED and s297[x, y] not in rose_colours:
                rose_bad += 1
    if rose_bad:
        lines.append(f"FAIL block 297 red is not dusty rose ({rose_bad})")
        failed = True

    def _dither_bad(base: int, source: tuple[int, int, int], expect: tuple[int, int, int]) -> int:
        classic = next(image for origin, image, _svga in painted_blocks if origin == base)
        svga = next(image for origin, _classic, image in painted_blocks if origin == base)
        src, dst = classic.load(), svga.load()
        bad = 0
        for y in range(classic.height):
            for x in range(classic.width):
                if src[x, y] == source and _axis_opposed(src, x, y, classic.width, classic.height):
                    if dst[x, y] != expect:
                        bad += 1
        return bad

    rose_wall = _dither_bad(342, SRC_BLUE, ROSE_MATE)
    if rose_wall:
        lines.append(f"FAIL block 342 checker is not the rose wall ({rose_wall})")
        failed = True
    blue_stitch = _dither_bad(306, SRC_RED, BLUE_MATE)
    if blue_stitch:
        lines.append(f"FAIL block 306 checker is not the muted blue ({blue_stitch})")
        failed = True

    # Variant A is history only. It still has to pass the same palette rules.
    painted_a: list[tuple[int, Image.Image, Image.Image]] = []
    for index in singles:
        classic = crop_tile(atlas, index)
        svga = paint(classic, [[tile_kind(index)]], SCHEME_A)
        check(f"A tile {index}", classic, svga)
    for base, width, height in blocks:
        classic = assemble(atlas, base, width, height)
        kinds = [[tile_kind(base)] * width for _ in range(height)]
        svga = paint(classic, kinds, SCHEME_A)
        painted_a.append((base, classic, svga))
        check(f"A block {base} {width}x{height}", classic, svga)
    _classic_a, svga_a_scene = paint_grid(atlas, scene, SCHEME_A)
    check("A neighbourhood", classic_scene, svga_a_scene)

    import hashlib

    digest = hashlib.sha256()
    for index in [0, 1, *range(2, 21), *range(21, 38), *range(40, 44), *range(249, 261)]:
        digest.update(paint(crop_tile(atlas, index), [[tile_kind(index)]]).tobytes())
    for base, width, height in blocks:
        classic = assemble(atlas, base, width, height)
        digest.update(paint(classic, [[tile_kind(base)] * width for _ in range(height)]).tobytes())
    digest.update(svga_scene.tobytes())
    batch1_sha = "f8e48f4044b59fa7292010837f008b0e912a26fec26aeb5cee5b22b90971842d"
    if digest.hexdigest() != batch1_sha:
        lines.append(f"FAIL batch 1 tiles changed: {digest.hexdigest()}")
        failed = True
    digest2 = hashlib.sha256()
    for base in com_bases + ind_bases:
        image = next(svga for origin, _classic, svga in painted_com + painted_ind if origin == base)
        digest2.update(image.tobytes())
    for index, _classic, svga in painted_smoke:
        digest2.update(svga.tobytes())
    digest2.update(svga_down.tobytes())
    batch2_sha = "19916eac7e959f386f092441d5f1ac41bda8dfc7f46c91757171709ddb9a7923"
    if digest2.hexdigest() != batch2_sha:
        lines.append(f"FAIL batch 2 tiles changed: {digest2.hexdigest()}")
        failed = True

    # Previews. Each pair is classic on the left and the SVGA look on the right.
    water_cells = []
    for index in [0, 1, *range(2, 21)]:
        classic = crop_tile(atlas, index)
        svga = paint(classic, [[tile_kind(index)]])
        water_cells.append(_pair(classic, svga, str(index), 3))
    body_w = max(_scaled(classic_shore, 3).width, _grid(water_cells, 7).width)
    terrain = _vstack(
        [
            _banner("TERRAIN WATER AND COAST  3X", body_w),
            _banner("SHORE SAMPLE  CLASSIC ABOVE  SVGA BELOW", body_w),
            _vstack([_text("CLASSIC", 2), _scaled(classic_shore, 3)], 6),
            _vstack([_text("SVGA", 2), _scaled(svga_shore, 3)], 6),
            _banner("EACH TILE  CLASSIC LEFT  SVGA RIGHT", body_w),
            _fit_width(_grid(water_cells, 7), body_w),
        ],
        14,
    )

    tree_cells = []
    for index in [*range(21, 38), *range(40, 44)]:
        classic = crop_tile(atlas, index)
        svga = paint(classic, [[tile_kind(index)]])
        tree_cells.append(_pair(classic, svga, str(index), 3))
    tree_grid = _grid(tree_cells, 7)
    trees = _vstack(
        [
            _banner("TREES AND WOODS  3X  CLASSIC LEFT  SVGA RIGHT", tree_grid.width),
            tree_grid,
        ],
        14,
    )

    house_cells = []
    for index in range(249, 261):
        classic = crop_tile(atlas, index)
        svga = paint(classic, [["building"]])
        house_cells.append(_pair(classic, svga, str(index), 3))
    house_grid = _grid(house_cells, 6)
    block_cells = [_pair(classic, svga, str(base), 3) for base, classic, svga in painted_blocks]
    block_grid = _grid(block_cells, 4)
    res_w = max(house_grid.width, block_grid.width)
    residential = _vstack(
        [
            _banner("RESIDENTIAL  3X  CLASSIC LEFT  SVGA RIGHT", res_w),
            _banner("HOUSES 249-260", res_w),
            _fit_width(house_grid, res_w),
            _banner("EMPTY LOT 240 AND 3X3 BLOCKS", res_w),
            _fit_width(block_grid, res_w),
        ],
        14,
    )

    classic_panel = _vstack([_text("CLASSIC", 2), _scaled(classic_scene, 2)], 8)
    svga_panel = _vstack([_text("SVGA", 2), _scaled(svga_scene, 2)], 8)
    panels = _hstack([classic_panel, svga_panel], 24)
    neighbourhood_sheet = _vstack(
        [
            _banner("NEIGHBOURHOOD  32PX TILES  COMPOSED RIVERBANK", panels.width),
            panels,
        ],
        12,
    )

    compare_sheet = _warcraft_compare()

    def _scheme_panel(image: Image.Image, label: str, scale: int) -> Image.Image:
        return _vstack([_text(label, 2), _scaled(image, scale)], 8)

    ab_blocks = []
    for base in (240, 261, 297, 315, 342):
        left = next(image for origin, _classic, image in painted_a if origin == base)
        right = next(image for origin, _classic, image in painted_blocks if origin == base)
        ab_blocks.append(_pair(left, right, str(base), 3))
    ab_res_grid = _grid(ab_blocks, 3)
    ab_residential = _vstack(
        [
            _banner("DIRT A LEFT  DIRT B RIGHT  3X", ab_res_grid.width),
            _banner("LOTS 240 261 297 315 342  B IS THE SVGA DIRT", ab_res_grid.width),
            ab_res_grid,
        ],
        14,
    )
    ab_left = _scheme_panel(svga_a_scene, "DIRT A", 2)
    ab_right = _scheme_panel(svga_scene, "DIRT B", 2)
    ab_panels = _hstack([ab_left, ab_right], 24)
    ab_neighbourhood = _vstack(
        [
            _banner("NEIGHBOURHOOD  32PX  DIRT A LEFT  DIRT B RIGHT", ab_panels.width),
            ab_panels,
        ],
        12,
    )
    patch_w, patch_h = 62, 48
    patches = []
    for scheme in SCHEMES:
        patch = Image.new("RGB", (patch_w, patch_h))
        px = patch.load()
        for y in range(patch_h):
            for x in range(patch_w):
                px[x, y] = _dirt_colour(x + 128, y + 64, False, scheme)
        patches.append(_scheme_panel(patch, f"DIRT {scheme.name}", 3))
    ab_patch = _vstack(
        [
            _banner("DIRT PATCH  3X  A LEFT  B RIGHT", _hstack(patches, 16).width),
            _hstack(patches, 16),
        ],
        12,
    )

    def _zone_sheet(painted: list[tuple[int, Image.Image, Image.Image]], title: str) -> Image.Image:
        cells = [_pair(classic, svga, str(base), 3) for base, classic, svga in painted]
        grid = _grid(cells, 4)
        return _vstack([_banner(title, grid.width), grid], 14)

    def _captioned(image: Image.Image, caption: str, scale: int) -> Image.Image:
        scaled = _scaled(image, scale)
        label = _text(caption, 2)
        width = max(scaled.width, label.width)
        cell = _blank(width, label.height + 4 + scaled.height)
        cell.paste(label, ((width - label.width) // 2, 0))
        cell.paste(scaled, ((width - scaled.width) // 2, label.height + 4))
        return cell

    letter_rows = []
    for base in (423, 612, 504, 603, 621):
        classic = assemble(atlas, base, 3, 3)
        kinds = [["commerce"] * 3 for _ in range(3)]
        before = paint(classic, kinds, bright_zones=False)
        after = paint(classic, kinds, bright_zones=True)
        letter_rows.append(
            _hstack(
                [
                    _captioned(classic, f"{base} CLASSIC", 3),
                    _captioned(before, "BEFORE", 3),
                    _captioned(after, "AFTER", 3),
                ],
                18,
            )
        )
    letters = _vstack(
        [_banner("ZONE LETTERS  CLASSIC  BEFORE  AFTER", letter_rows[0].width), *letter_rows],
        12,
    )

    commercial = _zone_sheet(painted_com, "COMMERCIAL  3X  CLASSIC LEFT  SVGA RIGHT")
    smoke_cells = [_pair(classic, svga, str(index), 3) for index, classic, svga in painted_smoke]
    smoke_grid = _grid(smoke_cells, 8)
    industrial_blocks = _zone_sheet(painted_ind, "INDUSTRIAL  3X  CLASSIC LEFT  SVGA RIGHT")
    ind_w = max(industrial_blocks.width, smoke_grid.width)
    industrial = _vstack(
        [
            _fit_width(industrial_blocks, ind_w),
            _banner("CHIMNEY FRAMES  852-859 AND 884-915", ind_w),
            _fit_width(smoke_grid, ind_w),
        ],
        14,
    )
    down_classic = _vstack([_text("CLASSIC", 2), _scaled(classic_down, 2)], 8)
    down_svga = _vstack([_text("SVGA", 2), _scaled(svga_down, 2)], 8)
    down_panels = _hstack([down_classic, down_svga], 24)
    downtown_sheet = _vstack(
        [
            _banner("DOWNTOWN  32PX  COMMERCIAL  INDUSTRIAL  RESIDENTIAL", down_panels.width),
            down_panels,
        ],
        12,
    )

    _save(terrain, PREVIEW_DIR / "terrain-water-3x.png")
    _save(trees, PREVIEW_DIR / "trees-3x.png")
    _save(residential, PREVIEW_DIR / "residential-3x.png")
    _save(neighbourhood_sheet, PREVIEW_DIR / "neighbourhood-32.png")
    _save(compare_sheet, PREVIEW_DIR / "warcraft-dirt-compare.png")
    _save(ab_neighbourhood, PREVIEW_DIR / "dirt-AB-neighbourhood-32.png")
    _save(ab_residential, PREVIEW_DIR / "dirt-AB-residential-3x.png")
    _save(ab_patch, PREVIEW_DIR / "dirt-AB-patch-3x.png")
    _save(commercial, PREVIEW_DIR / "commercial-3x.png")
    _save(industrial, PREVIEW_DIR / "industrial-3x.png")
    _save(downtown_sheet, PREVIEW_DIR / "downtown-32.png")
    _save(letters, PREVIEW_DIR / "letters-sample.png")

    def _road_sheet(indexes: list[int], title: str, columns: int) -> Image.Image:
        cells = [_pair(painted_road[index][0], painted_road[index][1], str(index), 3) for index in indexes]
        grid = _grid(cells, columns)
        return _vstack([_banner(title, grid.width), grid], 14)

    def _traffic(shape: int) -> list[int]:
        return [80 + shape, 128 + shape, 112 + shape, 96 + shape, 144 + shape, 192 + shape, 176 + shape, 160 + shape]

    roads = _vstack(
        [
            _road_sheet([*range(64, 80)], "ROADS  NO TRAFFIC  3X  CLASSIC LEFT  SVGA RIGHT", 8),
            _road_sheet(_traffic(2) + _traffic(3) + _traffic(12), "LIGHT THEN HEAVY  STRAIGHT ROADS AND THE CROSSING", 8),
        ],
        16,
    )
    rails_power = _vstack(
        [
            _road_sheet([*range(224, 240)], "RAILS  3X  CLASSIC LEFT  SVGA RIGHT", 8),
            _road_sheet([*range(208, 223)], "POWER LINES", 8),
        ],
        16,
    )
    bridges = _road_sheet(
        [64, 65, 79, 95, *range(828, 832), *range(948, 952), *_traffic(0), *_traffic(1), *_traffic(15)],
        "BRIDGES  3X  CLASSIC LEFT  SVGA RIGHT",
        8,
    )
    trip_classic = _vstack([_text("CLASSIC", 2), _scaled(classic_trip, 2)], 8)
    trip_svga = _vstack([_text("SVGA", 2), _scaled(svga_trip, 2)], 8)
    trip_panels = _hstack([trip_classic, trip_svga], 24)
    transport_sheet = _vstack(
        [
            _banner("TRANSPORT  32PX  ROADS  RAIL  POWER  BRIDGE", trip_panels.width),
            trip_panels,
        ],
        12,
    )
    _save(roads, PREVIEW_DIR / "roads-3x.png")
    _save(rails_power, PREVIEW_DIR / "rails-power-3x.png")
    _save(bridges, PREVIEW_DIR / "bridges-3x.png")
    _save(transport_sheet, PREVIEW_DIR / "transport-32.png")
    _, svga_trip_before = paint_grid(atlas, trip, plain_wires=False)
    power_rows = []
    for index in (210, 211, 220, 77, 221, 208):
        classic, after = painted_road[index]
        before = paint(classic, [["road"]], plain_wires=False)
        power_rows.append(
            _hstack(
                [
                    _captioned(classic, f"{index} CLASSIC", 4),
                    _captioned(before, "BEFORE", 4),
                    _captioned(after, "AFTER", 4),
                ],
                16,
            )
        )
    power_rows.append(
        _hstack(
            [
                _captioned(svga_trip_before, "SCENE BEFORE", 2),
                _captioned(svga_trip, "SCENE AFTER", 2),
            ],
            18,
        )
    )
    power_sample = _vstack(
        [_banner("POWER LINES  CLASSIC  BEFORE  AFTER", power_rows[0].width), *power_rows],
        12,
    )
    _save(power_sample, PREVIEW_DIR / "power-sample.png")

    lines.append("")
    lines.append(
        f"dirt tile 0: { {str(k): v for k, v in sorted(dirt_counts.items(), key=lambda kv: -kv[1])} }"
    )
    lines.append(f"south shore tile 13: { {str(k): v for k, v in sorted(_histogram(shore).items(), key=lambda kv: -kv[1])} }")
    lines.append("")
    lines.append("previews:")
    for name in (
        "terrain-water-3x.png",
        "trees-3x.png",
        "residential-3x.png",
        "neighbourhood-32.png",
        "warcraft-dirt-compare.png",
        "dirt-AB-neighbourhood-32.png",
        "dirt-AB-residential-3x.png",
        "dirt-AB-patch-3x.png",
        "blocks-297-342-before-after.png",
        "commercial-3x.png",
        "industrial-3x.png",
        "downtown-32.png",
        "letters-sample.png",
        "roads-3x.png",
        "rails-power-3x.png",
        "bridges-3x.png",
        "transport-32.png",
        "power-sample.png",
    ):
        lines.append(f"  previews/{name}")
    result = "PASS" if not failed else "FAIL"
    lines.append("")
    lines.append(f"RESULT: {result}")
    report = "\n".join(lines) + "\n"
    REPORT_PATH.write_text(report, encoding="utf-8")
    sys.stdout.write(report)
    return 0 if not failed else 1


if __name__ == "__main__":
    sys.exit(main())
