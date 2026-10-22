#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Lunduke City authors
# See COPYING and NOTICE.

"""Check the SVGA sample tiles against the VGA palette and the classic atlas.

A tile passes when:

* it is 16x16 RGB
* every pixel is an exact entry of palette/vga-mode13h.gpl
* the classic black ink pixels are still black, and no new black pixels appear
* every pair of edge-adjacent pixels that had different classic colours still
  has different SVGA colours (the flat-colour regions were not merged)

The same checks are repeated on the assembled police station and airport, so
a colour collision across a tile seam is caught too. Luminance-edge recall is
printed as a report of how much of the strong classic contrast survived.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

from PIL import Image

from vga_palette import load_gpl

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
TILE = 16
STRONG_LUMA = 80


def luma(rgb: tuple[int, int, int]) -> float:
    r, g, b = rgb
    return 0.299 * r + 0.587 * g + 0.114 * b


def crop_tile(atlas: Image.Image, index: int) -> Image.Image:
    columns = atlas.size[0] // TILE
    col, row = index % columns, index // columns
    return atlas.crop((col * TILE, row * TILE, col * TILE + TILE, row * TILE + TILE))


def pixels(image: Image.Image) -> list[list[tuple[int, int, int]]]:
    w, h = image.size
    src = image.convert("RGB").load()
    return [[src[x, y] for x in range(w)] for y in range(h)]


def compare(classic: Image.Image, svga: Image.Image, palette: set[tuple[int, int, int]]) -> dict:
    if classic.size != svga.size:
        raise RuntimeError(f"size {svga.size} != classic {classic.size}")
    cw, ch = classic.size
    cpx = pixels(classic)
    spx = pixels(svga)
    palette_bad = 0
    ink_bad = 0
    collapsed = 0
    boundaries = 0
    strong = 0
    strong_kept = 0
    new_strong = 0
    for y in range(ch):
        for x in range(cw):
            s = spx[y][x]
            c = cpx[y][x]
            if s not in palette:
                palette_bad += 1
            if (c == (0, 0, 0)) != (s == (0, 0, 0)):
                ink_bad += 1
            for nx, ny in ((x + 1, y), (x, y + 1)):
                if nx >= cw or ny >= ch:
                    continue
                c2 = cpx[ny][nx]
                s2 = spx[ny][nx]
                if c != c2:
                    boundaries += 1
                    if s == s2:
                        collapsed += 1
                cd = abs(luma(c) - luma(c2))
                sd = abs(luma(s) - luma(s2))
                if cd >= STRONG_LUMA:
                    strong += 1
                    if sd >= STRONG_LUMA * 0.5:
                        strong_kept += 1
                elif cd < 12 and sd >= STRONG_LUMA:
                    new_strong += 1
    return {
        "palette_bad": palette_bad,
        "ink_bad": ink_bad,
        "collapsed": collapsed,
        "boundaries": boundaries,
        "strong": strong,
        "strong_kept": strong_kept,
        "new_strong": new_strong,
        "pixels": cw * ch,
    }


def assemble(tiles: dict[int, Image.Image], base: int, width: int, height: int) -> Image.Image:
    image = Image.new("RGB", (width * TILE, height * TILE))
    for y in range(height):
        for x in range(width):
            image.paste(tiles[base + y * width + x], (x * TILE, y * TILE))
    return image


def main() -> int:
    manifest = json.loads((HERE / "manifest.json").read_text(encoding="utf-8"))
    palette = set(load_gpl(HERE / manifest["palette"]))
    atlas = Image.open(ROOT / manifest["atlas"]).convert("RGB")
    lines = [
        "SVGA sample verification",
        f"palette entries in gpl: {len(load_gpl(HERE / manifest['palette']))} "
        f"({len(palette)} unique RGB triples)",
        "",
    ]
    totals = {key: 0 for key in ("palette_bad", "ink_bad", "collapsed", "boundaries", "strong", "strong_kept", "new_strong", "pixels")}
    loaded: dict[int, Image.Image] = {}
    classic: dict[int, Image.Image] = {}
    failed = False
    for entry in manifest["tiles"]:
        path = HERE / entry["file"]
        image = Image.open(path)
        if image.size != (TILE, TILE):
            lines.append(f"FAIL {entry['file']}: size {image.size}")
            failed = True
            continue
        if image.mode not in ("RGB", "RGBA"):
            lines.append(f"FAIL {entry['file']}: mode {image.mode}")
            failed = True
            continue
        src = crop_tile(atlas, entry["id"])
        stats = compare(src, image, palette)
        loaded[entry["id"]] = image.convert("RGB")
        classic[entry["id"]] = src
        for key in totals:
            totals[key] += stats[key]
        ok = stats["palette_bad"] == 0 and stats["ink_bad"] == 0 and stats["collapsed"] == 0
        if not ok:
            failed = True
            lines.append(
                f"FAIL tile {entry['id']:4d} {entry['file']}: "
                f"palette={stats['palette_bad']} ink={stats['ink_bad']} "
                f"collapsed={stats['collapsed']}/{stats['boundaries']}"
            )
    lines.append(f"tiles checked: {len(manifest['tiles'])}")
    for name, spec in manifest["assemblies"].items():
        base, w, h = spec["base"], spec["width"], spec["height"]
        stats = compare(
            assemble(classic, base, w, h),
            assemble(loaded, base, w, h),
            palette,
        )
        ok = stats["palette_bad"] == 0 and stats["ink_bad"] == 0 and stats["collapsed"] == 0
        if not ok:
            failed = True
        recall = (stats["strong_kept"] / stats["strong"]) if stats["strong"] else 1.0
        lines.append(
            f"assembly {name} {w}x{h}: "
            f"palette={stats['palette_bad']} ink={stats['ink_bad']} "
            f"collapsed={stats['collapsed']}/{stats['boundaries']} "
            f"strong-edge recall={recall:.3f} ({stats['strong_kept']}/{stats['strong']}) "
            f"new-strong-inside-flat={stats['new_strong']} "
            f"{'OK' if ok else 'FAIL'}"
        )
    recall = (totals["strong_kept"] / totals["strong"]) if totals["strong"] else 1.0
    lines.append("")
    lines.append(
        f"per-tile totals: pixels={totals['pixels']} "
        f"palette_violations={totals['palette_bad']} "
        f"ink_mismatches={totals['ink_bad']} "
        f"collapsed_boundaries={totals['collapsed']}/{totals['boundaries']}"
    )
    lines.append(
        f"strong classic edges (luma delta >= {STRONG_LUMA}) still visible "
        f"at half that delta: {totals['strong_kept']}/{totals['strong']} ({recall:.3f})"
    )
    lines.append(
        "new strong edges inside a classic flat region "
        f"(classic delta < 12, svga delta >= {STRONG_LUMA}): {totals['new_strong']}"
    )
    result = "PASS" if not failed else "FAIL"
    lines.append("")
    lines.append(f"RESULT: {result}")
    report = "\n".join(lines) + "\n"
    (HERE / "verify-report.txt").write_text(report, encoding="utf-8")
    sys.stdout.write(report)
    return 0 if not failed else 1


if __name__ == "__main__":
    sys.exit(main())
