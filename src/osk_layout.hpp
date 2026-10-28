// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#pragma once

#include <algorithm>
#include <cmath>

// On-screen keyboard for gamepad and touch text fields. Rows are QWERTY.
// span is a width in letter-key units so wide keys (shift, bksp, space)
// do not overlap the next key.

enum class OskKind { Text, Space, Backspace, Shift, Auto };

struct OskKey {
    const char *label;
    const char *shifted;
    const char *text;
    const char *text_shift;
    OskKind kind;
    int row;
    float span;
};

inline const OskKey *osk_keys(int &count)
{
    static const OskKey keys[] = {
        {"q", "Q", "q", "Q", OskKind::Text, 0, 1},
        {"w", "W", "w", "W", OskKind::Text, 0, 1},
        {"e", "E", "e", "E", OskKind::Text, 0, 1},
        {"r", "R", "r", "R", OskKind::Text, 0, 1},
        {"t", "T", "t", "T", OskKind::Text, 0, 1},
        {"y", "Y", "y", "Y", OskKind::Text, 0, 1},
        {"u", "U", "u", "U", OskKind::Text, 0, 1},
        {"i", "I", "i", "I", OskKind::Text, 0, 1},
        {"o", "O", "o", "O", OskKind::Text, 0, 1},
        {"p", "P", "p", "P", OskKind::Text, 0, 1},

        {"a", "A", "a", "A", OskKind::Text, 1, 1},
        {"s", "S", "s", "S", OskKind::Text, 1, 1},
        {"d", "D", "d", "D", OskKind::Text, 1, 1},
        {"f", "F", "f", "F", OskKind::Text, 1, 1},
        {"g", "G", "g", "G", OskKind::Text, 1, 1},
        {"h", "H", "h", "H", OskKind::Text, 1, 1},
        {"j", "J", "j", "J", OskKind::Text, 1, 1},
        {"k", "K", "k", "K", OskKind::Text, 1, 1},
        {"l", "L", "l", "L", OskKind::Text, 1, 1},

        {"shift", "SHIFT", nullptr, nullptr, OskKind::Shift, 2, 1.5f},
        {"z", "Z", "z", "Z", OskKind::Text, 2, 1},
        {"x", "X", "x", "X", OskKind::Text, 2, 1},
        {"c", "C", "c", "C", OskKind::Text, 2, 1},
        {"v", "V", "v", "V", OskKind::Text, 2, 1},
        {"b", "B", "b", "B", OskKind::Text, 2, 1},
        {"n", "N", "n", "N", OskKind::Text, 2, 1},
        {"m", "M", "m", "M", OskKind::Text, 2, 1},
        {"bksp", "bksp", nullptr, nullptr, OskKind::Backspace, 2, 1.5f},

        {"1", "1", "1", "1", OskKind::Text, 3, 1},
        {"2", "2", "2", "2", OskKind::Text, 3, 1},
        {"3", "3", "3", "3", OskKind::Text, 3, 1},
        {"4", "4", "4", "4", OskKind::Text, 3, 1},
        {"5", "5", "5", "5", OskKind::Text, 3, 1},
        {"6", "6", "6", "6", OskKind::Text, 3, 1},
        {"7", "7", "7", "7", OskKind::Text, 3, 1},
        {"8", "8", "8", "8", OskKind::Text, 3, 1},
        {"9", "9", "9", "9", OskKind::Text, 3, 1},
        {"0", "0", "0", "0", OskKind::Text, 3, 1},

        {"-", "-", "-", "-", OskKind::Text, 4, 1},
        {"'", "'", "'", "'", OskKind::Text, 4, 1},
        {"space", "space", nullptr, nullptr, OskKind::Space, 4, 6},
        {"Random", "Random", nullptr, nullptr, OskKind::Auto, 4, 2},
    };
    count = static_cast<int>(sizeof(keys) / sizeof(keys[0]));
    return keys;
}

inline constexpr int kOskRows = 5;
inline constexpr double kOskGap = 4;

struct OskPlacement {
    int index = 0;
    double x = 0;
    double y = 0;
    double w = 0;
    double h = 0;
};

// Lay the keys out inside [origin, origin + width]. A wide key's span
// includes the gaps between the letter-widths it covers. Counting only the
// gaps between keys (and not those internal gaps) shifts the row right, and
// the last key — "auto" — runs past the text field.
inline int osk_place(double origin_x, double origin_y, double width, double max_h, OskPlacement *out, int out_cap)
{
    int count = 0;
    const OskKey *keys = osk_keys(count);
    if (out == nullptr || width < 80 || max_h < 40 || count > out_cap) {
        return 0;
    }
    const double gap = kOskGap;
    const double unit = std::max(8.0, (width - gap * 9.0) / 10.0);
    double key_h = 52;
    key_h = std::floor((max_h - gap * (kOskRows - 1)) / kOskRows);
    if (key_h > 56) {
        key_h = 56;
    }
    if (key_h < 28) {
        key_h = 28;
    }
    const double row_pitch = key_h + gap;
    const double full_w = 10.0 * unit + 9.0 * gap;
    const double right = origin_x + width;
    int placed = 0;
    for (int row = 0; row < kOskRows; ++row) {
        double spans = 0;
        for (int i = 0; i < count; ++i) {
            if (keys[i].row == row) {
                spans += keys[i].span;
            }
        }
        if (spans <= 0) {
            continue;
        }
        const double row_w = spans * unit + (spans - 1.0) * gap;
        double kx = origin_x + std::max(0.0, (full_w - row_w) / 2.0);
        const double ky = origin_y + row * row_pitch;
        for (int i = 0; i < count; ++i) {
            if (keys[i].row != row) {
                continue;
            }
            double kw = keys[i].span * unit + (keys[i].span - 1.0) * gap;
            if (kw > right - kx) {
                kw = right - kx;
            }
            if (kw < 0) {
                kw = 0;
            }
            out[placed++] = OskPlacement{i, kx, ky, kw, key_h};
            kx += kw + gap;
        }
    }
    return placed;
}

// Key on the next row (direction > 0) or the previous row whose horizontal
// center is nearest. -1 when that row has no key. Keys within a pixel of the
// nearest are tied; the later (right-hand) key wins, so the end of a shorter
// row moves to the end of the row above. `present` may be null; a zero entry
// skips a key that was not drawn.
inline int osk_vertical_target(const OskKey *keys, int count, int index, int direction, const double *center_x,
                               const char *present)
{
    if (keys == nullptr || center_x == nullptr || index < 0 || index >= count || direction == 0) {
        return -1;
    }
    const int row = keys[index].row + (direction > 0 ? 1 : -1);
    int best = -1;
    double best_dist = 0;
    for (int i = 0; i < count; ++i) {
        if (keys[i].row != row) {
            continue;
        }
        if (present != nullptr && present[i] == 0) {
            continue;
        }
        const double dist = std::fabs(center_x[i] - center_x[index]);
        if (best < 0 || dist < best_dist) {
            best = i;
            best_dist = dist;
        } else if (dist - best_dist <= 1.0) {
            best = i;
        }
    }
    return best;
}

// Up or Down with no adjacent row leaves the keyboard. The next key on this
// row is not a vertical step. -1 stays put.
inline int osk_exit_focus(const int *order, int count, int current, int direction, int osk_first, int osk_last)
{
    if (order == nullptr || count <= 0 || direction == 0) {
        return -1;
    }
    int at = -1;
    for (int i = 0; i < count; ++i) {
        if (order[i] == current) {
            at = i;
            break;
        }
    }
    if (at < 0) {
        return -1;
    }
    if (direction > 0) {
        for (int i = at + 1; i < count; ++i) {
            if (order[i] < osk_first || order[i] >= osk_last) {
                return order[i];
            }
        }
        return -1;
    }
    for (int i = at - 1; i >= 0; --i) {
        if (order[i] < osk_first || order[i] >= osk_last) {
            return order[i];
        }
    }
    return -1;
}
