#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Lunduke City authors
# See COPYING and NOTICE.

"""SVGA and Spooky moving sprites.

Original drawings. The classic obj<type>-<frame>.png files are not rewritten.
Each new sheet has the same pixel size and the same frame count. Frame 0 of
each file is engine frame 1. Headings follow the engine: 1 north, then
clockwise. Light comes from the upper left. A blank classic frame stays blank.

    python3 art/sprites/make_sprites.py
    python3 art/sprites/make_sprites.py --check
"""

from __future__ import annotations

import hashlib
import math
import sys
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "svga"))
from vga_palette import vga_palette_8bit  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "third_party/micropolis-assets/images/micropolisEngine"

INK = (0, 0, 0)
SHADOW = (32, 65, 32)
WATER_SHADOW = (0, 0, 65)
NIGHT_WATER_SHADOW = (16, 0, 65)

# Engine frame 1 is north. Later frames step 45 degrees clockwise.
HEADING = (0, 45, 90, 135, 180, 225, 270, 315)


def _blank(w: int, h: int) -> Image.Image:
    return Image.new("RGBA", (w, h), (0, 0, 0, 0))


def _poly(px, pts: list[tuple[int, int]], colour: tuple[int, int, int], w: int, h: int) -> None:
    if len(pts) < 3:
        return
    y0 = max(0, min(p[1] for p in pts))
    y1 = min(h - 1, max(p[1] for p in pts))
    for y in range(y0, y1 + 1):
        xs: list[float] = []
        for i, (x1, py1) in enumerate(pts):
            x2, py2 = pts[(i + 1) % len(pts)]
            if (py1 <= y < py2) or (py2 <= y < py1):
                if py2 == py1:
                    continue
                xs.append(x1 + (y - py1) * (x2 - x1) / (py2 - py1))
        xs.sort()
        for i in range(0, len(xs) - len(xs) % 2, 2):
            left = max(0, int(round(xs[i])))
            right = min(w - 1, int(round(xs[i + 1])))
            for x in range(left, right + 1):
                px[x, y] = (*colour, 255)


def _world(right: float, forward: float, heading: float, cx: float, cy: float) -> tuple[int, int]:
    rad = math.radians(heading)
    s, c = math.sin(rad), math.cos(rad)
    x = cx + right * c + forward * s
    y = cy + right * s - forward * c
    return int(round(x)), int(round(y))


def _place(pts: list[tuple[float, float]], heading: float, cx: float, cy: float) -> list[tuple[int, int]]:
    return [_world(r, f, heading, cx, cy) for r, f in pts]


def _shadow(image: Image.Image, colour: tuple[int, int, int]) -> None:
    src = image.copy()
    sp = src.load()
    px = image.load()
    w, h = image.size
    for y in range(h):
        for x in range(w):
            if sp[x, y][3] == 0:
                continue
            sx, sy = x + 2, y + 2
            if 0 <= sx < w and 0 <= sy < h and sp[sx, sy][3] == 0 and px[sx, sy][3] == 0:
                px[sx, sy] = (*colour, 255)


def _lit_edge(image: Image.Image, lit: tuple[int, int, int], body: tuple[int, int, int]) -> None:
    """Upper-left pixels of the body take the lit colour."""
    src = image.copy()
    sp = src.load()
    px = image.load()
    w, h = image.size
    for y in range(h):
        for x in range(w):
            if sp[x, y][:3] != body:
                continue
            up = y == 0 or sp[x, y - 1][3] == 0 or sp[x, y - 1][:3] != body
            left = x == 0 or sp[x - 1, y][3] == 0 or sp[x - 1, y][:3] != body
            if up or left:
                px[x, y] = (*lit, 255)


def _boat(size: int, heading: float, spooky: bool) -> Image.Image:
    image = _blank(size, size)
    px = image.load()
    cx = cy = (size - 1) / 2
    if spooky:
        hull, deck, cabin, lit, lamp = (48, 32, 65), (65, 44, 65), (32, 0, 65), (97, 81, 113), (255, 255, 85)
        shadow = NIGHT_WATER_SHADOW
    else:
        hull, deck, cabin, lit, lamp = (113, 85, 56), (182, 182, 182), (56, 56, 113), (203, 203, 203), (255, 255, 125)
        shadow = WATER_SHADOW
    hull_pts = _place([(-7, -12), (7, -12), (5, 8), (0, 16), (-5, 8)], heading, cx, cy)
    _poly(px, hull_pts, hull, size, size)
    deck_pts = _place([(-4, -6), (4, -6), (3, 6), (0, 12), (-3, 6)], heading, cx, cy)
    _poly(px, deck_pts, deck, size, size)
    cabin_pts = _place([(-3, -2), (3, -2), (3, 5), (-3, 5)], heading, cx, cy)
    _poly(px, cabin_pts, cabin, size, size)
    _lit_edge(image, lit, hull)
    lamp_at = _world(0, 10, heading, cx, cy)
    if 0 <= lamp_at[0] < size and 0 <= lamp_at[1] < size:
        px[lamp_at] = (*lamp, 255)
    if spooky:
        # A second lamp on the cabin, so the hull reads on night water.
        cabin_lamp = _world(-2, 2, heading, cx, cy)
        if 0 <= cabin_lamp[0] < size and 0 <= cabin_lamp[1] < size:
            px[cabin_lamp] = (*lamp, 255)
    _shadow(image, shadow)
    return image


def _vehicle(
    size: int,
    heading: float,
    length: float,
    width: float,
    body: tuple[int, int, int],
    lit: tuple[int, int, int],
    window: tuple[int, int, int],
    nose: tuple[int, int, int],
) -> Image.Image:
    image = _blank(size, size)
    px = image.load()
    cx = cy = (size - 1) / 2
    half_l, half_w = length / 2, width / 2
    shape = [(-half_w, -half_l + 2), (half_w, -half_l + 2), (half_w - 1, half_l), (0, half_l + 2), (-half_w + 1, half_l)]
    _poly(px, _place(shape, heading, cx, cy), body, size, size)
    _lit_edge(image, lit, body)
    win = [(-half_w + 2, half_l - 6), (half_w - 2, half_l - 6), (half_w - 3, half_l - 2), (-half_w + 3, half_l - 2)]
    _poly(px, _place(win, heading, cx, cy), window, size, size)
    cap = [(-2, half_l - 1), (2, half_l - 1), (0, half_l + 2)]
    _poly(px, _place(cap, heading, cx, cy), nose, size, size)
    _shadow(image, SHADOW)
    return image


def _train(size: int, heading: float, spooky: bool) -> Image.Image:
    if spooky:
        body, lit, window, nose = (65, 44, 65), (113, 81, 113), (255, 255, 85), (170, 85, 0)
    else:
        body, lit, window, nose = (56, 56, 56), (162, 162, 162), (125, 158, 255), (170, 0, 0)
    image = _vehicle(size, heading, 20, 8, body, lit, window, nose)
    # Two wheels on the near side, so it reads as a carriage rather than a bus.
    px = image.load()
    cx = cy = (size - 1) / 2
    for forward in (-4, 4):
        x, y = _world(5, forward, heading, cx, cy)
        if 0 <= x < size and 0 <= y < size:
            px[x, y] = (*INK, 255)
    return image


def _bus(size: int, heading: float, spooky: bool) -> Image.Image:
    if spooky:
        body, lit, window, nose = (65, 48, 32), (113, 85, 56), (255, 255, 85), (255, 125, 0)
    else:
        body, lit, window, nose = (113, 113, 0), (255, 255, 85), (125, 158, 255), (65, 65, 0)
    return _vehicle(size, heading, 18, 8, body, lit, window, nose)


def _heli(size: int, heading: float, spooky: bool) -> Image.Image:
    image = _blank(size, size)
    px = image.load()
    cx = cy = (size - 1) / 2
    if spooky:
        body, lit, glass = (48, 32, 65), (113, 81, 113), (255, 255, 85)
    else:
        body, lit, glass = (130, 130, 130), (203, 203, 203), (125, 158, 255)
    cabin = [(-5, -2), (5, -2), (4, 6), (-4, 6)]
    _poly(px, _place(cabin, heading, cx, cy), body, size, size)
    tail = [(-1, -12), (1, -12), (1, -2), (-1, -2)]
    _poly(px, _place(tail, heading, cx, cy), body, size, size)
    _lit_edge(image, lit, body)
    # Rotor seen from above: a long ellipse across the screen, not a side disc.
    for i in range(-10, 11):
        x = int(cx) + i
        y = int(cy) - 1
        if 0 <= x < size and 0 <= y < size:
            px[x, y] = (*INK, 255)
        y2 = int(cy) + i // 3
        x2 = int(cx)
        if 0 <= x2 < size and 0 <= y2 < size and abs(i) > 2:
            px[x2, y2] = (*INK, 255)
    glass_at = _world(0, 4, heading, cx, cy)
    if 0 <= glass_at[0] < size and 0 <= glass_at[1] < size:
        px[glass_at] = (*glass, 255)
    _shadow(image, SHADOW)
    return image


def _plane(size: int, heading: float, spooky: bool, lift: int) -> Image.Image:
    image = _blank(size, size)
    px = image.load()
    cx = (size - 1) / 2
    cy = (size - 1) / 2 - lift
    if spooky:
        body, lit, wing = (97, 97, 97), (182, 182, 182), (48, 32, 65)
        lamp = (255, 255, 85)
    else:
        body, lit, wing = (182, 182, 182), (227, 227, 227), (56, 56, 113)
        lamp = (170, 0, 0)
    fuse = [(-2, -14), (2, -14), (2, 12), (0, 16), (-2, 12)]
    _poly(px, _place(fuse, heading, cx, cy), body, size, size)
    wings = [(-14, -2), (14, -2), (12, 3), (-12, 3)]
    _poly(px, _place(wings, heading, cx, cy), wing, size, size)
    tail = [(-6, -12), (6, -12), (2, -8), (-2, -8)]
    _poly(px, _place(tail, heading, cx, cy), wing, size, size)
    _lit_edge(image, lit, body)
    nose = _world(0, 15, heading, cx, cy)
    if 0 <= nose[0] < size and 0 <= nose[1] < size:
        px[nose] = (*lamp, 255)
    # The shadow stays on the ground while the takeoff frames rise.
    src = image.copy()
    sp = src.load()
    for y in range(size):
        for x in range(size):
            if sp[x, y][3] == 0:
                continue
            sx, sy = x + 2, y + 2 + lift
            if 0 <= sx < size and 0 <= sy < size and px[sx, sy][3] == 0:
                px[sx, sy] = (*SHADOW, 255)
    return image


def _monster(size: int, heading: float, phase: int, spooky: bool) -> Image.Image:
    image = _blank(size, size)
    px = image.load()
    bob = (0, 1, 0)[phase % 3]
    cx = (size - 1) / 2
    cy = (size - 1) / 2 + bob
    if spooky:
        hide, lit, belly, eye = (48, 32, 65), (97, 81, 113), (182, 182, 182), (255, 255, 85)
    else:
        hide, lit, belly, eye = (0, 65, 0), (0, 113, 0), (113, 113, 56), (255, 255, 0)
    body = [(-8, -6), (8, -6), (10, 4), (4, 10), (-4, 10), (-10, 4)]
    _poly(px, _place(body, heading, cx, cy), hide, size, size)
    back = [(-5, -4), (5, -4), (4, 2), (-4, 2)]
    _poly(px, _place(back, heading, cx, cy), lit, size, size)
    head = [(-5, 6), (5, 6), (4, 14), (-4, 14)]
    _poly(px, _place(head, heading, cx, cy), hide, size, size)
    jaw = [(-3, 12), (3, 12), (2, 16), (-2, 16)]
    _poly(px, _place(jaw, heading, cx, cy), belly, size, size)
    # Arms out to the sides, a crest, and a short tail. Feet swap with the phase.
    arm_l = [(-10, 2), (-16, 4 + phase), (-14, 8), (-8, 6)]
    arm_r = [(10, 2), (16, 6 - phase), (14, 10), (8, 6)]
    _poly(px, _place(arm_l, heading, cx, cy), hide, size, size)
    _poly(px, _place(arm_r, heading, cx, cy), hide, size, size)
    tail = [(-2, -6), (2, -6), (1, -14), (-1, -14)]
    _poly(px, _place(tail, heading, cx, cy), hide, size, size)
    crest = [(-1, 8), (1, 8), (0, 4)]
    _poly(px, _place(crest, heading, cx, cy), belly, size, size)
    eye_at = _world(-2, 12, heading, cx, cy)
    if 0 <= eye_at[0] < size and 0 <= eye_at[1] < size:
        px[eye_at] = (*eye, 255)
    _shadow(image, SHADOW)
    return image


def _tornado(size: int, frame: int, spooky: bool) -> Image.Image:
    image = _blank(size, size)
    px = image.load()
    if spooky:
        light, mid, dark = (113, 81, 113), (65, 44, 65), (32, 0, 65)
    else:
        light, mid, dark = (203, 203, 203), (130, 130, 130), (81, 81, 81)
    shift = (0, 2, -2)[frame]
    cx = size // 2 + shift
    for i, (ry, rx, colour) in enumerate(
        (
            (4, 10, light),
            (10, 7, mid),
            (16, 5, dark),
            (22, 3, mid),
            (28, 2, dark),
        )
    ):
        wobble = shift if i % 2 == 0 else -shift
        for y in range(ry, min(size, ry + 6)):
            for x in range(size):
                if ((x - cx - wobble) / rx) ** 2 + ((y - (ry + 3)) / 3) ** 2 <= 1:
                    px[x, y] = (*colour, 255)
    return image


def _explosion(size: int, frame: int, spooky: bool) -> Image.Image:
    image = _blank(size, size)
    px = image.load()
    radius = (4, 8, 12, 16, 20, 22)[frame]
    cx = cy = size // 2
    core = (255, 255, 85)
    mid = (255, 125, 0)
    edge = (113, 0, 0) if spooky else (170, 0, 0)
    smoke = (65, 44, 65) if spooky else (81, 81, 81)
    for y in range(size):
        for x in range(size):
            d = math.hypot(x - cx, y - cy)
            if d > radius:
                continue
            if d < radius * 0.35:
                px[x, y] = (*core, 255)
            elif d < radius * 0.7:
                px[x, y] = (*mid, 255)
            else:
                px[x, y] = (*edge, 255)
    # A few smoke pixels on the upper left, where the light is.
    if frame >= 2:
        for dx, dy in ((-2, -2), (2, -3), (-3, 1), (3, -1)):
            x = cx + int(dx * radius / 8)
            y = cy + int(dy * radius / 8)
            if 0 <= x < size and 0 <= y < size:
                px[x, y] = (*smoke, 255)
    return image


def _frames(spooky: bool) -> list[tuple[str, Image.Image]]:
    suffix = "spooky" if spooky else "svga"
    out: list[tuple[str, Image.Image]] = []

    def add(type_id: int, index: int, image: Image.Image) -> None:
        out.append((f"obj{type_id}-{index}-{suffix}.png", image))

    # Train. File 4 is blank in the classic set, and the engine shows it.
    for index, heading in enumerate((0, 90, 180, 270)):
        add(1, index, _train(32, heading, spooky))
    add(1, 4, _blank(32, 32))
    for index, heading in enumerate(HEADING):
        add(2, index, _heli(32, heading, spooky))
    for index, heading in enumerate(HEADING):
        add(3, index, _plane(48, heading, spooky, 0))
    for index, lift in enumerate((2, 5, 8)):
        add(3, 8 + index, _plane(48, 90, spooky, lift))
    for index, heading in enumerate(HEADING):
        add(4, index, _boat(48, heading, spooky))
    # Four walks of three frames, then four turn poses.
    for index in range(12):
        heading = (45, 135, 225, 315)[index // 3]
        add(5, index, _monster(48, heading, index % 3, spooky))
    for index, heading in enumerate((45, 90, 135, 180)):
        add(5, 12 + index, _monster(48, heading, 1, spooky))
    for index in range(3):
        add(6, index, _tornado(48, index, spooky))
    for index in range(6):
        add(7, index, _explosion(48, index, spooky))
    for index, heading in enumerate((0, 90, 45, 315)):
        add(8, index, _bus(32, heading, spooky))
    return out


def _check_image(image: Image.Image, palette: set[tuple[int, int, int]], name: str) -> None:
    px = image.load()
    w, h = image.size
    for y in range(h):
        for x in range(w):
            pixel = px[x, y]
            if pixel[3] == 0:
                continue
            if pixel[3] != 255 or pixel[:3] not in palette:
                raise SystemExit(f"{name} left the VGA palette at {(x, y)}: {pixel}")


def build() -> list[tuple[str, Image.Image]]:
    palette = set(vga_palette_8bit())
    frames = _frames(False) + _frames(True)
    expected = {
        1: (5, (32, 32)),
        2: (8, (32, 32)),
        3: (11, (48, 48)),
        4: (8, (48, 48)),
        5: (16, (48, 48)),
        6: (3, (48, 48)),
        7: (6, (48, 48)),
        8: (4, (32, 32)),
    }
    seen: dict[tuple[int, str], int] = {}
    for name, image in frames:
        _check_image(image, palette, name)
        kind, index, look = name[:-4].split("-")
        type_id = int(kind[3:])
        count, size = expected[type_id]
        if image.size != size:
            raise SystemExit(f"{name} is {image.size}, expected {size}")
        seen[(type_id, look)] = seen.get((type_id, look), 0) + 1
        classic = OUT / f"obj{type_id}-{index}.png"
        with Image.open(classic) as opened:
            if opened.size != image.size:
                raise SystemExit(f"{name} size does not match {classic.name}")
            if type_id == 1 and index == "4":
                opaque = sum(1 for pixel in image.get_flattened_data() if pixel[3])
                if opaque:
                    raise SystemExit("train frame 5 must stay blank")
    for type_id, (count, _size) in expected.items():
        for look in ("svga", "spooky"):
            if seen.get((type_id, look)) != count:
                raise SystemExit(f"type {type_id} {look} has {seen.get((type_id, look))} frames, expected {count}")
    return frames


def main(argv: list[str]) -> int:
    frames = build()
    if "--check" in argv:
        failed = False
        for name, image in frames:
            path = OUT / name
            if not path.is_file():
                print(f"FAIL missing {name}")
                failed = True
                continue
            with Image.open(path) as opened:
                if opened.convert("RGBA").tobytes() != image.tobytes():
                    print(f"FAIL {name} {hashlib.sha256(image.tobytes()).hexdigest()[:12]}")
                    failed = True
        if failed:
            return 1
        digest = hashlib.sha256()
        for name, image in frames:
            digest.update(name.encode())
            digest.update(image.tobytes())
        print(f"OK moving sprites {digest.hexdigest()[:12]}")
        return 0
    for name, image in frames:
        image.save(OUT / name)
    digest = hashlib.sha256()
    for name, image in frames:
        digest.update(name.encode())
        digest.update(image.tobytes())
    print(f"wrote {len(frames)} sprites {digest.hexdigest()}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
