#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Lunduke City authors
# See COPYING and NOTICE.

"""Before/after crops for the M6 Spooky lawn sample.

San Francisco is snro.222, Tokyo is snro.555. The before sheet is master's
tiles-spooky.png. The after sheet is the one in the tree.

    python3 art/samples/render_samples.py
"""

from __future__ import annotations

import subprocess
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
ENGINE = ROOT / "third_party/micropolis-assets/images/micropolisEngine"
RES = ROOT / "third_party/micropolis-assets/res"
OUT = ROOT / "previews/samples"
MASTER = "a2a07ac098dec5a1d17df596794b03b7a70bab32"

WORLD_W, WORLD_H = 120, 100
LOMASK = 0x03FF
HEADER = 3120
ZOOMS = (16, 32, 64)


def load_map(path: Path) -> list[list[int]]:
    data = path.read_bytes()
    tiles = [[0] * WORLD_H for _ in range(WORLD_W)]
    off = HEADER
    for x in range(WORLD_W):
        for y in range(WORLD_H):
            tiles[x][y] = (data[off] | (data[off + 1] << 8)) & LOMASK
            off += 2
    return tiles


def crop_tile(atlas: Image.Image, index: int) -> Image.Image:
    col, row = index % 16, index // 16
    return atlas.crop((col * 16, row * 16, col * 16 + 16, row * 16 + 16))


def paint_map(atlas: Image.Image, tiles: list[list[int]], x0: int, y0: int, span: int, zoom: int) -> Image.Image:
    image = Image.new("RGB", (span * zoom, span * zoom))
    cache: dict[int, Image.Image] = {}
    for y in range(span):
        for x in range(span):
            index = tiles[x0 + x][y0 + y]
            tile = cache.get(index)
            if tile is None:
                src = crop_tile(atlas, index)
                tile = src if zoom == 16 else src.resize((zoom, zoom), Image.Resampling.NEAREST)
                cache[index] = tile
            image.paste(tile, (x * zoom, y * zoom))
    return image


def find_center(tiles: list[list[int]], bright: list[int]) -> tuple[int, int]:
    span = 12
    best = (20, 20)
    score = -1
    for y in range(0, WORLD_H - span, 2):
        for x in range(0, WORLD_W - span, 2):
            got = 0
            for dy in range(span):
                for dx in range(span):
                    tile = tiles[x + dx][y + dy]
                    if 240 <= tile <= 404 or 761 <= tile <= 778:
                        got += bright[tile]
            if got > score:
                score = got
                best = (x + span // 2, y + span // 2)
    return best


def window(center: tuple[int, int], zoom: int) -> tuple[int, int, int]:
    span = max(8, min(24, 480 // zoom))
    x0 = max(0, min(WORLD_W - span, center[0] - span // 2))
    y0 = max(0, min(WORLD_H - span, center[1] - span // 2))
    return x0, y0, span


def pair(before: Image.Image, after: Image.Image, title: str) -> Image.Image:
    gap = 8
    bar = 22
    width = before.size[0] + gap + after.size[0]
    image = Image.new("RGB", (width, bar + before.size[1]), (24, 24, 28))
    draw = ImageDraw.Draw(image)
    draw.rectangle((0, 0, before.size[0], bar), fill=(48, 32, 32))
    draw.rectangle((before.size[0] + gap, 0, width, bar), fill=(32, 48, 32))
    draw.text((6, 4), "BEFORE  " + title, fill=(255, 220, 180))
    draw.text((before.size[0] + gap + 6, 4), "AFTER", fill=(200, 255, 200))
    image.paste(before, (0, bar))
    image.paste(after, (before.size[0] + gap, bar))
    return image


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    before_path = Path("/tmp/master-spooky.png")
    subprocess.check_call(
        ["git", "show", f"{MASTER}:third_party/micropolis-assets/images/micropolisEngine/tiles-spooky.png"],
        stdout=before_path.open("wb"),
    )
    before_atlas = Image.open(before_path).convert("RGB")
    after_atlas = Image.open(ENGINE / "tiles-spooky.png").convert("RGB")
    bright = [0] * 960
    src = before_atlas.load()
    for index in range(960):
        col, row = index % 16, index // 16
        bright[index] = sum(src[col * 16 + x, row * 16 + y] == (0, 170, 0) for y in range(16) for x in range(16))
    cities = {"sf": load_map(RES / "snro.222"), "tokyo": load_map(RES / "snro.555")}
    number = 0
    for city, tiles in cities.items():
        center = find_center(tiles, bright)
        for zoom in ZOOMS:
            number += 1
            x0, y0, span = window(center, zoom)
            before = paint_map(before_atlas, tiles, x0, y0, span, zoom)
            after = paint_map(after_atlas, tiles, x0, y0, span, zoom)
            image = pair(before, after, f"M6 Spooky lawns  {city}  {zoom}px")
            path = OUT / f"{number:02d}-m6-{city}-spooky-{zoom}-before-after.png"
            image.save(path)
            print(path.name, image.size)


if __name__ == "__main__":
    main()
