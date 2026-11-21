#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Bryan Lunduke
"""Lay each look's open-water tile under the Marina basin.

The approved Marina (r11b) keeps its buildings, quay, piers, boats and boat
shadows. Only the plain basin water (sheet rows 21-47) is replaced by the
look's own RIVER tile, aligned to the 16px map grid, so the basin and the
open water beside it are one surface with no seam. A dark speck right under
or beside a boat or pier is that boat's shadow and stays. Running the script
again changes nothing.
"""

import pathlib

from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parent
ASSETS = ROOT.parents[1] / "third_party" / "micropolis-assets" / "images" / "micropolisEngine"
LOOKS = (("marina.png", "tiles.png"), ("marina-svga.png", "tiles-svga.png"), ("marina-spooky.png", "tiles-spooky.png"))
RIVER = 2
BASIN_TOP = 21


def river_tile(atlas):
    sheet = Image.open(ASSETS / atlas).convert("RGB")
    col, row = RIVER % 16, RIVER // 16
    return sheet.crop((col * 16, row * 16, col * 16 + 16, row * 16 + 16)).load()


def main():
    for sprite_name, atlas in LOOKS:
        water = river_tile(atlas)
        counts = {}
        for y in range(16):
            for x in range(16):
                counts[water[x, y]] = counts.get(water[x, y], 0) + 1
        palette = set(counts)
        speck = min(counts, key=counts.get)
        image = Image.open(ROOT / sprite_name).convert("RGB")
        px = image.load()
        w, h = image.size
        solid = [[px[x, y] not in palette for x in range(w)] for y in range(h)]
        for y in range(BASIN_TOP, h):
            for x in range(w):
                if solid[y][x]:
                    continue
                if px[x, y] == speck:
                    beside = any(solid[y - d][x] for d in (1, 2) if y - d >= BASIN_TOP) or (x > 0 and solid[y][x - 1])
                    if beside:
                        continue
                px[x, y] = water[x % 16, y % 16]
        image.save(ROOT / sprite_name)


if __name__ == "__main__":
    main()
