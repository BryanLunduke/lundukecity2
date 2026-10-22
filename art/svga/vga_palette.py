#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Lunduke City authors
# See COPYING and NOTICE.

"""Default VGA Mode 13h palette.

This is the BIOS palette loaded for mode 13h: 16 EGA colours, 16 greys,
nine 24-colour cycles (24 hues x 3 saturations x 3 values), and 8 unused
black entries. 6-bit DAC levels are expanded to 8-bit by bit replication,
(v << 2) | (v >> 6), which is the usual exact conversion of a VGA DAC
value. The result matches the DOSBox / Allegro default palette.
"""

from __future__ import annotations

from pathlib import Path


def _add_ega(pal: list[tuple[int, int, int]], lo: int, melo: int, mehi: int, hi: int) -> None:
    for i in range(16):
        if i & 8:
            high, low = hi, melo
        else:
            high, low = mehi, lo
        r = g = b = low
        if i & 4:
            r = high
        if i & 2:
            g = high
        if i & 1:
            b = high
        if i == 6:
            g = melo  # dark yellow is brown
        pal.append((r, g, b))


def _add_run(
    pal: list[tuple[int, int, int]],
    start: int,
    channel: int,
    lo: int,
    melo: int,
    me: int,
    mehi: int,
    hi: int,
) -> int:
    r = g = b = lo
    if start & 4:
        r = hi
    if start & 2:
        g = hi
    if start & 1:
        b = hi
    pal.append((r, g, b))
    rising = (start & channel) != channel
    for step in range(3):
        if rising:
            value = (melo, me, mehi)[step]
        else:
            value = (mehi, me, melo)[step]
        if channel == 4:
            r = value
        elif channel == 2:
            g = value
        else:
            b = value
        pal.append((r, g, b))
    return start ^ channel


def _add_cycle(
    pal: list[tuple[int, int, int]], lo: int, melo: int, me: int, mehi: int, hi: int
) -> None:
    hue = 1  # start at blue
    for channel in (4, 1, 2, 4, 1, 2):
        hue = _add_run(pal, hue, channel, lo, melo, me, mehi, hi)


def vga_dac_6bit() -> list[tuple[int, int, int]]:
    """256 colours as VGA DAC levels, each channel 0..63."""
    pal: list[tuple[int, int, int]] = []
    _add_ega(pal, 0, 21, 42, 63)
    for grey in (0, 5, 8, 11, 14, 17, 20, 24, 28, 32, 36, 40, 45, 50, 56, 63):
        pal.append((grey, grey, grey))
    # High value, then medium, then low. Within each: high, medium, low saturation.
    _add_cycle(pal, 0, 16, 31, 47, 63)
    _add_cycle(pal, 31, 39, 47, 55, 63)
    _add_cycle(pal, 45, 49, 54, 58, 63)
    _add_cycle(pal, 0, 7, 14, 21, 28)
    _add_cycle(pal, 14, 17, 21, 24, 28)
    _add_cycle(pal, 20, 22, 24, 26, 28)
    _add_cycle(pal, 0, 4, 8, 12, 16)
    _add_cycle(pal, 8, 10, 12, 14, 16)
    _add_cycle(pal, 11, 12, 13, 15, 16)
    pal.extend((0, 0, 0) for _ in range(8))
    if len(pal) != 256:
        raise RuntimeError(f"palette length {len(pal)}")
    return pal


def dac_to_byte(level: int) -> int:
    """Expand one 6-bit DAC level to 8-bit by replicating the top bits."""
    if not 0 <= level <= 63:
        raise ValueError(level)
    shifted = level << 2
    return shifted | (shifted >> 6)


def vga_palette_8bit() -> list[tuple[int, int, int]]:
    return [tuple(dac_to_byte(c) for c in rgb) for rgb in vga_dac_6bit()]  # type: ignore[misc]


def write_gpl(path: Path, pal8: list[tuple[int, int, int]], pal6: list[tuple[int, int, int]]) -> None:
    lines = [
        "GIMP Palette",
        "Name: VGA Mode 13h default",
        "Columns: 16",
        "# Default BIOS palette for VGA mode 13h.",
        "# 0-15 EGA, 16-31 greys, 32-247 = 24 hues x 3 saturations x 3 values, 248-255 unused black.",
        "# Channels are 8-bit. They are the 6-bit DAC levels expanded by bit replication",
        "# (v << 2) | (v >> 6). A pixel is an exact palette entry when its RGB triple",
        "# equals one of these rows. The 6-bit DAC triple is noted after the index.",
    ]
    for index, (rgb, dac) in enumerate(zip(pal8, pal6)):
        lines.append(
            f"{rgb[0]:3d} {rgb[1]:3d} {rgb[2]:3d}\tIndex {index}  DAC {dac[0]} {dac[1]} {dac[2]}"
        )
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def load_gpl(path: Path) -> list[tuple[int, int, int]]:
    colours: list[tuple[int, int, int]] = []
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#") or line.startswith("GIMP") or line.startswith("Name:") or line.startswith("Columns:"):
            continue
        parts = line.replace("\t", " ").split()
        if len(parts) < 3:
            continue
        colours.append((int(parts[0]), int(parts[1]), int(parts[2])))
    if len(colours) != 256:
        raise RuntimeError(f"{path} has {len(colours)} colours, expected 256")
    return colours
