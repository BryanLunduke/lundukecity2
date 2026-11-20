#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Bryan Lunduke
# See COPYING and NOTICE.

"""Godzilla colours for the monster sprite (obj5-0 to obj5-15).

The monster is one sprite set shared by every tile look. Only colours
change: the shape, the frames, and transparency stay as they were.

    orange body      -> dark charcoal green scales
    red underside    -> lighter grey-green belly and legs
    white highlights -> pale dorsal spines
    yellow           -> the softer VGA yellow
    black eye        -> unchanged

Run once from the repository root. A second run finds no source colours
and leaves the files alone.

    python3 art/monster/recolor.py
"""

from __future__ import annotations

from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
SPRITES = ROOT / "third_party/micropolis-assets/images/micropolisEngine"

GODZILLA = {
    (255, 127, 0): (78, 92, 72),
    (255, 0, 0): (124, 134, 108),
    (255, 255, 255): (216, 224, 204),
    (255, 255, 0): (255, 255, 85),
}


def recolor(path: Path) -> int:
    with Image.open(path) as opened:
        mode = opened.mode
        image = opened.convert("RGBA")
    px = image.load()
    changed = 0
    for y in range(image.size[1]):
        for x in range(image.size[0]):
            r, g, b, a = px[x, y]
            if a == 0:
                continue
            target = GODZILLA.get((r, g, b))
            if target is None:
                continue
            px[x, y] = (*target, a)
            changed += 1
    if changed:
        image.convert(mode if mode in ("RGBA", "RGB") else "RGBA").save(path, optimize=True)
    return changed


def main() -> int:
    total = 0
    for frame in range(16):
        path = SPRITES / f"obj5-{frame}.png"
        count = recolor(path)
        total += count
        print(f"{path.name}: {count} pixels")
    print(f"total {total}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
