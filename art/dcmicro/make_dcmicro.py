#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Lunduke City authors
# See COPYING and NOTICE.

"""Map sheets for the Data Center and the Micro Nuclear Plant.

    python3 art/dcmicro/make_dcmicro.py          # rewrite data/special-tiles
    python3 art/dcmicro/make_dcmicro.py --check  # compare with the checked-in files

Writes, under data/special-tiles/:
  datacenter[-svga].png         built Data Center, 48x48 (design B3)
  datacenter-vacant[-svga].png  vacant lot: the vacant industrial lot with a
                                yellow/blue dashed border (4 px) and a blue D
  micro-nuclear[-svga].png      Micro Nuclear Plant, 16x16 (design A)
  datacenter-glass.png          48x48 night mask: opaque white where the
                                facade glass is. Night Mode lights these
                                pixels on a powered Data Center. Roof solar
                                panels are not glass.
There is no -spooky art; the Spooky look falls back to the SVGA sheets.
The ground is atlas tile 0 of each look and the vacant lot is atlas tiles
612-620, so the atlases are read here and never written.
"""

from __future__ import annotations

import sys
from pathlib import Path

try:
    import numpy as np
except ImportError:  # the meson check skips (77) on builders without numpy
    if '--check' in sys.argv[1:]:
        print('numpy is not installed; skipping')
        raise SystemExit(77)
    raise
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import designs  # noqa: E402
import px  # noqa: E402

OUT = px.ROOT / 'data' / 'special-tiles'


def glass_mask(canvas) -> np.ndarray:
    """Facade glass only: glass materials outside the roof rectangle."""
    m = np.zeros((canvas.H, canvas.W, 4), np.uint8)
    for y in range(canvas.H):
        for x in range(canvas.W):
            on_roof = designs.X0 <= x <= designs.X1 and designs.Y0 <= y <= designs.Y1
            if canvas.m[y, x] in ('glass', 'glass_d') and not on_roof:
                m[y, x] = (255, 255, 255, 255)
    return m


def sheets() -> dict[str, np.ndarray]:
    out: dict[str, np.ndarray] = {}
    dc = designs.dc_b3()
    mn = designs.mn_r3a()
    for look, suffix in (('classic', ''), ('svga', '-svga')):
        for name, art in (('datacenter', dc.render(look)), ('micro-nuclear', mn.render(look)),
                          ('datacenter-vacant', designs.vacant_d(look, 'blue', 4))):
            bad = px.check_palette(art, look)
            if bad:
                raise SystemExit(f'{name}{suffix}: colours outside the {look} palette: {sorted(bad)}')
            out[f'{name}{suffix}.png'] = art
    out['datacenter-glass.png'] = glass_mask(dc)
    return out


def main() -> int:
    check = '--check' in sys.argv[1:]
    status = 0
    for name, art in sheets().items():
        path = OUT / name
        if check:
            mode = 'RGBA' if art.shape[2] == 4 else 'RGB'
            have = np.array(Image.open(path).convert(mode)) if path.exists() else None
            if have is None or have.shape != art.shape or (have != art).any():
                print(f'differs: {path}')
                status = 1
            continue
        Image.fromarray(art).save(path)
        print(f'wrote {path}')
    return status


if __name__ == '__main__':
    raise SystemExit(main())
