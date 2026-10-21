// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#pragma once

// Break a line so a short last token ("in.", "$960") is not left alone.
// width_of returns the unwrapped pixel width of its argument.

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

template <typename WidthOf>
inline std::string pull_short_last_token(const std::string &text, int column_px, WidthOf width_of)
{
    if (column_px <= 0 || text.empty() || text.find('\n') != std::string::npos) {
        return text;
    }
    if (width_of(text) <= column_px) {
        return text;
    }
    std::vector<std::size_t> spaces;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == ' ') {
            spaces.push_back(i);
        }
    }
    int fit = -1;
    for (std::size_t i = 0; i < spaces.size(); ++i) {
        if (width_of(text.substr(0, spaces[i])) <= column_px) {
            fit = static_cast<int>(i);
        } else {
            break;
        }
    }
    // The first line already holds only one word, or the overflow is more
    // than a single trailing token. Leave Pango's wrap alone.
    if (fit <= 0) {
        return text;
    }
    const std::string tail = text.substr(spaces[static_cast<std::size_t>(fit)] + 1);
    if (tail.find(' ') != std::string::npos) {
        return text;
    }
    const int tail_w = width_of(tail);
    const bool short_tail = tail.size() <= 4 || (tail_w > 0 && tail_w * 10 < column_px * 3);
    if (!short_tail) {
        return text;
    }
    std::string out = text;
    out[spaces[static_cast<std::size_t>(fit - 1)]] = '\n';
    return out;
}
