// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

#pragma once

#include <algorithm>
#include <string>
#include <vector>

// Toast stack policy: when more than `max` alerts are up, drop the lowest
// priority first. Age is only the tie-breaker, so a disaster, a pause, a
// "Playing …" notice, or a goal briefing stays readable on a short Deck
// screen instead of being pushed off by a newer ordinary hint.

inline constexpr int kToastPriorityDisaster = 100;
inline constexpr int kToastPriorityPause = 80;
inline constexpr int kToastPriorityBriefing = 75;
inline constexpr int kToastPriorityPlaying = 70;
inline constexpr int kToastPriorityNote = 40;
inline constexpr int kToastPriorityLow = 20;

inline int toast_priority_for(const std::string &text, bool disaster)
{
    if (disaster) {
        return kToastPriorityDisaster;
    }
    if (text.compare(0, 11, "Game paused") == 0 || text.compare(0, 14, "Paused because") == 0) {
        return kToastPriorityPause;
    }
    if (text.find(" years left") != std::string::npos || text.find(" year left") != std::string::npos ||
        text.compare(0, 10, "Time is up") == 0) {
        return kToastPriorityBriefing;
    }
    if (text.compare(0, 8, "Playing ") == 0) {
        return kToastPriorityPlaying;
    }
    if (text.compare(0, 14, "Mayor's note: ") == 0) {
        return kToastPriorityNote;
    }
    return kToastPriorityLow;
}

// Drop the lowest-priority entry until the queue fits. Equal priority
// drops the oldest (the earlier index).
template <typename Toast>
void toast_trim(std::vector<Toast> &toasts, std::size_t max_count)
{
    while (toasts.size() > max_count) {
        std::size_t drop = 0;
        for (std::size_t i = 1; i < toasts.size(); ++i) {
            if (toasts[i].priority < toasts[drop].priority) {
                drop = i;
            }
        }
        toasts.erase(toasts.begin() + static_cast<std::ptrdiff_t>(drop));
    }
}

// Draw high priority first (nearest the bottom corner). Same priority
// keeps newer toasts closer to that corner, matching the old newest-first
// stack.
template <typename Toast>
std::vector<std::size_t> toast_draw_order(const std::vector<Toast> &toasts)
{
    std::vector<std::size_t> order(toasts.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
        order[i] = i;
    }
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        if (toasts[a].priority != toasts[b].priority) {
            return toasts[a].priority > toasts[b].priority;
        }
        return a > b;
    });
    return order;
}
