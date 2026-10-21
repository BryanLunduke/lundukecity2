// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#pragma once

// History index 0 is the newest sample. A fresh city stores zeros for
// every month that has not happened yet, and a single missed sample is a
// hole rather than a real drop to zero. Skip the unfilled tail, and skip
// one isolated zero when every other plotted sample is non-zero.

inline bool history_point_skipped(const double *values, int count, int index)
{
    if (values == nullptr || count < 1 || index < 0 || index >= count) {
        return true;
    }
    int oldest = count - 1;
    while (oldest > 0 && values[oldest] == 0.0) {
        --oldest;
    }
    if (index > oldest) {
        return true;
    }
    int zeros = 0;
    int hole = -1;
    for (int i = 0; i <= oldest; ++i) {
        if (values[i] == 0.0) {
            ++zeros;
            hole = i;
        }
    }
    return zeros == 1 && index == hole;
}
