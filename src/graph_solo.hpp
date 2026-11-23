// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.
#pragma once

// Click to solo; click the same swatch again to restore all seven series.
inline int graph_solo_mask(int current, int series)
{
    if (series < 0 || series >= 7) {
        return current;
    }
    const int solo = 1 << series;
    return current == solo ? 0x7f : solo;
}
