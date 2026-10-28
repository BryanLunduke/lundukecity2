#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Lunduke City authors
# See COPYING and NOTICE.

"""Before/after map crops for the art samples.

The before sheets are the copies taken at the start of the edit
(/tmp/art-before when present, otherwise the classic fallbacks). The after
sheets are the files in the tree. San Francisco is snro.222, Tokyo snro.555.

    python3 art/samples/render_samples.py
"""

from __future__ import annotations

from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
ASSETS = ROOT / "third_party/micropolis-assets"
ENGINE = ASSETS / "images/micropolisEngine"
RES = ASSETS / "res"
SPECIAL = ROOT / "data/special-tiles"
OUT = ROOT / "previews/samples"
BEFORE = Path("/tmp/art-before")

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


def stamp(image: Image.Image, sprite: Image.Image, px: int, py: int, zoom: int) -> None:
    scale = zoom / 16
    w = max(1, int(round(sprite.size[0] * scale)))
    h = max(1, int(round(sprite.size[1] * scale)))
    sized = sprite if (w, h) == sprite.size else sprite.resize((w, h), Image.Resampling.NEAREST)
    image.paste(sized, (px, py), sized)


def find_center(tiles: list[list[int]], pred, span: int = 12) -> tuple[int, int]:
    best = (20, 20)
    score = -1
    for y in range(0, WORLD_H - span, 2):
        for x in range(0, WORLD_W - span, 2):
            got = sum(int(pred(tiles[x + dx][y + dy])) for dy in range(span) for dx in range(span))
            if got > score:
                score = got
                best = (x + span // 2, y + span // 2)
    return best


def window(center: tuple[int, int], zoom: int) -> tuple[int, int, int]:
    span = max(8, min(24, 480 // zoom))
    x0 = max(0, min(WORLD_W - span, center[0] - span // 2))
    y0 = max(0, min(WORLD_H - span, center[1] - span // 2))
    return x0, y0, span


def find_tile(tiles: list[list[int]], pred, near: tuple[int, int]) -> tuple[int, int]:
    best = near
    best_d = 10**9
    for y in range(WORLD_H):
        for x in range(WORLD_W):
            if not pred(tiles[x][y]):
                continue
            d = abs(x - near[0]) + abs(y - near[1])
            if d < best_d:
                best_d = d
                best = (x, y)
    return best


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


def sheet(name: str) -> tuple[Image.Image, Image.Image]:
    before = BEFORE / name
    if name.startswith("tiles-"):
        after = ENGINE / name
    elif name.startswith("obj"):
        after = ENGINE / name
    else:
        after = SPECIAL / name
    if not before.is_file():
        before = after
    return Image.open(before).convert("RGB"), Image.open(after).convert("RGB")


def sprite_file(name: str, look: str, before: bool) -> Image.Image:
    if before or look == "classic":
        path = ENGINE / name
    else:
        path = ENGINE / name.replace(".png", f"-{look}.png")
        if not path.is_file():
            path = ENGINE / name
    return Image.open(path).convert("RGBA")


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    cities = {
        "sf": load_map(RES / "snro.222"),
        "tokyo": load_map(RES / "snro.555"),
    }
    spooky_before, spooky_after = sheet("tiles-spooky.png")
    svga_before, svga_after = sheet("tiles-svga.png")
    # sheet() opens RGB. Sprites need RGBA, so reopen those separately.

    bright = [0] * 960
    src = spooky_before.load()
    for index in range(960):
        col, row = index % 16, index // 16
        bright[index] = sum(
            src[col * 16 + x, row * 16 + y] == (0, 170, 0) for y in range(16) for x in range(16)
        )

    def dirt(tile: int) -> bool:
        return tile in (0, 1)

    def water(tile: int) -> bool:
        return 2 <= tile <= 20

    def land(tile: int) -> bool:
        return tile < 2 or tile >= 21

    centers = {}
    for city, tiles in cities.items():
        centers[(city, "m6")] = find_center(tiles, lambda tile: bright[tile] if 240 <= tile <= 404 or 761 <= tile <= 778 else 0)
        centers[(city, "p3")] = find_center(tiles, dirt, 10)
        home = centers[(city, "m6")]
        centers[(city, "water")] = find_tile(tiles, water, home)
        centers[(city, "land")] = find_tile(tiles, land, home)
        # A shore window has water for the ship and land for the rest.
        best = home
        best_score = -1
        for y in range(0, WORLD_H - 10, 2):
            for x in range(0, WORLD_W - 10, 2):
                wet = sum(water(tiles[x + dx][y + dy]) for dy in range(10) for dx in range(10))
                dry = 100 - wet
                if wet < 8 or dry < 8:
                    continue
                score = min(wet, dry)
                if score > best_score:
                    best_score = score
                    best = (x + 5, y + 5)
        centers[(city, "shore")] = best

    jobs = []
    for city in ("sf", "tokyo"):
        for zoom in ZOOMS:
            jobs.append(("m6", city, "spooky", zoom, centers[(city, "m6")], None))
    for city in ("sf", "tokyo"):
        for zoom in ZOOMS:
            jobs.append(("p1", city, "svga", zoom, centers[(city, "land")], "park"))
    for city in ("sf", "tokyo"):
        for zoom in ZOOMS:
            jobs.append(("p2", city, "svga", zoom, centers[(city, "land")], "zoo"))
    for city in ("sf", "tokyo"):
        for zoom in ZOOMS:
            jobs.append(("p3", city, "svga", zoom, centers[(city, "p3")], None))
    for look in ("svga", "spooky"):
        for city in ("sf", "tokyo"):
            for zoom in ZOOMS:
                jobs.append(("p4", city, look, zoom, centers[(city, "shore")], "sprites"))

    labels = {
        "m6": "M6 Spooky lawns",
        "p1": "P1 SVGA Big Park",
        "p2": "P2 SVGA zoo",
        "p3": "P3 SVGA dirt",
        "p4": "P4 sprites",
    }
    number = 0
    for item, city, look, zoom, center, stamp_kind in jobs:
        number += 1
        x0, y0, span = window(center, zoom)
        tiles = cities[city]
        if look == "spooky":
            before_atlas, after_atlas = spooky_before, spooky_after
        else:
            before_atlas, after_atlas = svga_before, svga_after
        before = paint_map(before_atlas, tiles, x0, y0, span, zoom)
        after = paint_map(after_atlas, tiles, x0, y0, span, zoom)
        if stamp_kind == "park":
            classic = Image.open(SPECIAL / "big-park.png").convert("RGBA")
            svga = Image.open(SPECIAL / "big-park-svga.png").convert("RGBA")
            ox = (center[0] - x0) * zoom
            oy = (center[1] - y0) * zoom
            stamp(before, classic, ox, oy, zoom)
            stamp(after, svga, ox, oy, zoom)
        elif stamp_kind == "zoo":
            old = Image.open(BEFORE / "zoo-svga.png").convert("RGBA") if (BEFORE / "zoo-svga.png").is_file() else Image.open(SPECIAL / "zoo.png").convert("RGBA")
            new = Image.open(SPECIAL / "zoo-svga.png").convert("RGBA")
            ox = (center[0] - x0) * zoom
            oy = (center[1] - y0) * zoom
            stamp(before, old, ox, oy, zoom)
            stamp(after, new, ox, oy, zoom)
        elif stamp_kind == "sprites":
            # Keep every frame inside this crop. The ship sits on water.
            # The others sit on land in the same window.
            water_cells = [
                (x0 + dx, y0 + dy)
                for dy in range(span)
                for dx in range(span)
                if water(tiles[x0 + dx][y0 + dy])
            ]
            land_cells = [
                (x0 + dx, y0 + dy)
                for dy in range(span)
                for dx in range(span)
                if land(tiles[x0 + dx][y0 + dy])
            ]
            if not water_cells:
                water_cells = [(x0 + span // 2, y0 + span // 2)]
            if not land_cells:
                land_cells = [(x0 + 1, y0 + 1)]
            names = (
                "obj4-0.png",
                "obj1-0.png",
                "obj8-1.png",
                "obj3-2.png",
                "obj2-0.png",
                "obj5-0.png",
                "obj6-1.png",
                "obj7-3.png",
            )
            spots = [water_cells[len(water_cells) // 2]]
            step = max(1, len(land_cells) // 7)
            for i in range(7):
                spots.append(land_cells[min(len(land_cells) - 1, i * step)])
            for name, (tx, ty) in zip(names, spots):
                before_sprite = sprite_file(name, look, True)
                after_sprite = sprite_file(name, look, False)
                px = (tx - x0) * zoom
                py = (ty - y0) * zoom
                stamp(before, before_sprite, px, py, zoom)
                stamp(after, after_sprite, px, py, zoom)
        title = f"{labels[item]}  {city}  {look}  {zoom}px"
        image = pair(before, after, title)
        path = OUT / f"{number:02d}-{item}-{city}-{look}-{zoom}-before-after.png"
        image.save(path)
        print(path.name, image.size)


if __name__ == "__main__":
    main()
