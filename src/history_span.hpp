// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#pragma once

// History index 0 is the newest sample. A fresh city stores zeros for
// every month that has not happened yet. Skip that unfilled tail. An
// isolated zero between two real samples is a missing month, not a drop
// to the axis, even when several months are missing. A run of zeros, and
// a zero at the newest sample, are real readings and stay on the line.

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
    if (values[index] != 0.0 || index == 0 || index + 1 > oldest) {
        return false;
    }
    return values[index - 1] != 0.0 && values[index + 1] != 0.0;
}
