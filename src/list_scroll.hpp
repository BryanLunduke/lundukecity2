// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#pragma once

#include <algorithm>

// The in-window file list (and, later, the scenario list) only has room for
// `visible` rows. The selected index is the source of truth; the scroll
// offset follows it so a row past the first screen can be reached.

inline int list_max_scroll(int count, int visible)
{
    if (visible < 1) {
        visible = 1;
    }
    if (count < 0) {
        count = 0;
    }
    return std::max(0, count - visible);
}

inline void list_reveal(int &scroll, int index, int visible, int count)
{
    if (visible < 1) {
        visible = 1;
    }
    if (count <= 0) {
        scroll = 0;
        return;
    }
    if (index < 0) {
        index = 0;
    }
    if (index >= count) {
        index = count - 1;
    }
    if (scroll < 0) {
        scroll = 0;
    }
    if (index < scroll) {
        scroll = index;
    }
    if (index >= scroll + visible) {
        scroll = index - visible + 1;
    }
    const int max_scroll = list_max_scroll(count, visible);
    if (scroll > max_scroll) {
        scroll = max_scroll;
    }
    if (scroll < 0) {
        scroll = 0;
    }
}

// Map a pointer on the scrollbar track to a row index. `t == 0` is the first
// row and `t == 1` is the last, including when the list is shorter than the
// window.
inline int list_index_from_track(double y, double track_y, double track_h, int count)
{
    if (count <= 1 || track_h <= 1.0) {
        return 0;
    }
    double t = (y - track_y) / track_h;
    if (t < 0.0) {
        t = 0.0;
    }
    if (t > 1.0) {
        t = 1.0;
    }
    return static_cast<int>(t * static_cast<double>(count - 1) + 0.5);
}

inline void list_thumb(int scroll, int count, int visible, double track_y, double track_h, double &thumb_y,
                       double &thumb_h)
{
    if (track_h < 1.0) {
        thumb_y = track_y;
        thumb_h = 0;
        return;
    }
    if (count <= visible || count <= 0 || visible < 1) {
        thumb_y = track_y;
        thumb_h = track_h;
        return;
    }
    thumb_h = std::max(18.0, track_h * static_cast<double>(visible) / static_cast<double>(count));
    if (thumb_h > track_h) {
        thumb_h = track_h;
    }
    const int max_scroll = list_max_scroll(count, visible);
    const double t = max_scroll <= 0 ? 0.0 : static_cast<double>(scroll) / static_cast<double>(max_scroll);
    thumb_y = track_y + (track_h - thumb_h) * t;
}
