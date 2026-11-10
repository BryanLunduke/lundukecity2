#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Lunduke City authors
# See COPYING and NOTICE.

"""Meson entry for svga_atlas and spooky_atlas.

Exit 77 when Pillow cannot be imported so Meson skips the test. The atlas
scripts themselves are what a person runs to rebuild the sheets, and a
missing Pillow there is still an import error.
"""

from __future__ import annotations

import os
import sys

sys.dont_write_bytecode = True


def main() -> int:
    if len(sys.argv) < 2:
        print("usage: atlas_test.py SCRIPT [args...]", file=sys.stderr)
        return 2
    try:
        from PIL import Image
    except ImportError:
        Image = None
    if Image is None:
        print("Pillow is not installed; skipping")
        return 77
    script = sys.argv[1]
    os.execv(sys.executable, [sys.executable, script, *sys.argv[2:]])


if __name__ == "__main__":
    raise SystemExit(main())
