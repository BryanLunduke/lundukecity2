// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#pragma once

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
        {"auto", "auto", nullptr, nullptr, OskKind::Auto, 4, 2},
    };
    count = static_cast<int>(sizeof(keys) / sizeof(keys[0]));
    return keys;
}

inline constexpr int kOskRows = 5;
inline constexpr double kOskGap = 4;
