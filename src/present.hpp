// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#pragma once

// Presentation timing and layout that can be asserted without a window.
// Motion stays short: the city-open dither is 140 ms, and these beats are
// in that same range.

#include <algorithm>
#include <string>

inline double glide_ease(double t)
{
    if (t < 0.0) {
        t = 0.0;
    }
    if (t > 1.0) {
        t = 1.0;
    }
    const double rest = 1.0 - t;
    return 1.0 - rest * rest * rest;
}

inline double glide_mix(double from, double to, double t)
{
    return from + (to - from) * glide_ease(t);
}

// Auto-goto pan. 280 ms, then the marker blinks.
inline constexpr int kGlideMs = 280;
inline constexpr int kMarkerMs = 640;
inline constexpr int kMarkerBlink = 160;

inline bool marker_lit(int elapsed_ms)
{
    if (elapsed_ms < 0 || elapsed_ms >= kMarkerMs) {
        return false;
    }
    return (elapsed_ms / kMarkerBlink) % 2 == 0;
}

// A look change fades only after the first sheet is on screen, and only
// when the drawn sheet actually changes.
inline bool look_change_fades(bool already_shown, int before, int after)
{
    return already_shown && before != after;
}

// Message bar and goal banner. A disaster paints only the message bar.
// The goal banner keeps the look's own banner colors.
struct BannerColors {
    double r = 1;
    double g = 1;
    double b = 0.7;
    double ink_r = 0;
    double ink_g = 0;
    double ink_b = 0;
};

inline BannerColors message_bar_colors(bool disaster, BannerColors look)
{
    if (!disaster) {
        return look;
    }
    BannerColors bar = look;
    bar.r = 128.0 / 255.0;
    bar.g = 0.0;
    bar.b = 0.0;
    bar.ink_r = 1.0;
    bar.ink_g = 1.0;
    bar.ink_b = 85.0 / 255.0;
    return bar;
}

inline BannerColors goal_banner_colors(BannerColors look)
{
    return look;
}

// "Jan 1901" when the calendar rolls. Long enough to read before the
// year-in-review card opens (kYearStampHold).
inline constexpr int kYearStampMs = 1600;
inline constexpr int kYearStampHold = 1100;

struct YearStamp {
    int watched = -1;
    bool armed = false;
    int show_year = 0;
    int elapsed_ms = -1;

    void observe(int year, int month)
    {
        if (!armed) {
            armed = true;
            watched = year;
            return;
        }
        if (year != watched && month == 0) {
            show_year = year;
            elapsed_ms = 0;
        }
        watched = year;
    }

    void advance(int ms)
    {
        if (elapsed_ms < 0) {
            return;
        }
        if (ms < 0) {
            ms = 0;
        }
        elapsed_ms += ms;
        if (elapsed_ms >= kYearStampMs) {
            elapsed_ms = -1;
        }
    }

    bool visible() const
    {
        return elapsed_ms >= 0;
    }

    std::string text() const
    {
        return "Jan " + std::to_string(show_year);
    }
};

// Bulldoze dust stays inside the tile. The status tag is drawn above the
// tile, so a speck that stays at y >= 2 cannot cover "Nothing to clear".
inline int dust_speck_y(int tile_px, int elapsed_ms, int index)
{
    if (tile_px < 4) {
        tile_px = 4;
    }
    if (elapsed_ms < 0) {
        elapsed_ms = 0;
    }
    const int rise = elapsed_ms * tile_px / 900;
    const int base = tile_px / 2 + (index % 3) * (tile_px / 8);
    int y = base - rise / 3;
    if (y < 2) {
        y = 2;
    }
    if (y > tile_px - 4) {
        y = tile_px - 4;
    }
    return y;
}

// Goal banner width so a long scenario line does not cover the hover card.
// room is the banner's outer width. False means the card is in the way and
// the banner should drop below it, using the full width.
inline bool goal_banner_room(double view_w, double pad, double card_w, bool card_up, double &room)
{
    room = std::max(0.0, view_w - pad * 2.0);
    if (!card_up || card_w <= 0.0) {
        return true;
    }
    const double beside = view_w - card_w - pad * 3.0;
    if (beside < 96.0) {
        return false;
    }
    room = beside;
    return true;
}

struct ChartPlate {
    double paper_r = 1;
    double paper_g = 1;
    double paper_b = 1;
    double ink_r = 0.15;
    double ink_g = 0.15;
    double ink_b = 0.15;
    double grid_r = 0;
    double grid_g = 0;
    double grid_b = 0;
    bool night = false;
};

inline ChartPlate chart_plate(int look)
{
    ChartPlate plate;
    if (look == 2) {
        plate.paper_r = 48.0 / 255.0;
        plate.paper_g = 32.0 / 255.0;
        plate.paper_b = 24.0 / 255.0;
        plate.ink_r = 255.0 / 255.0;
        plate.ink_g = 224.0 / 255.0;
        plate.ink_b = 160.0 / 255.0;
        plate.grid_r = 255.0 / 255.0;
        plate.grid_g = 190.0 / 255.0;
        plate.grid_b = 96.0 / 255.0;
        plate.night = true;
        return plate;
    }
    if (look == 1) {
        plate.paper_r = 214.0 / 255.0;
        plate.paper_g = 226.0 / 255.0;
        plate.paper_b = 240.0 / 255.0;
        plate.ink_r = 16.0 / 255.0;
        plate.ink_g = 24.0 / 255.0;
        plate.ink_b = 64.0 / 255.0;
        plate.grid_r = 16.0 / 255.0;
        plate.grid_g = 32.0 / 255.0;
        plate.grid_b = 80.0 / 255.0;
        return plate;
    }
    plate.paper_r = 245.0 / 255.0;
    plate.paper_g = 236.0 / 255.0;
    plate.paper_b = 214.0 / 255.0;
    plate.ink_r = 48.0 / 255.0;
    plate.ink_g = 32.0 / 255.0;
    plate.ink_b = 16.0 / 255.0;
    plate.grid_r = 80.0 / 255.0;
    plate.grid_g = 56.0 / 255.0;
    plate.grid_b = 32.0 / 255.0;
    return plate;
}

// Leave headroom so a peak does not sit on the top rule.
inline void chart_headroom(double &min_v, double &max_v)
{
    if (max_v < min_v) {
        std::swap(min_v, max_v);
    }
    if (max_v <= min_v) {
        max_v = min_v + 1.0;
        return;
    }
    const double span = max_v - min_v;
    const double floor = min_v;
    const double peak = max_v;
    max_v = peak + span * 0.18;
    if (floor > 0.0) {
        const double lifted = floor - span * 0.12;
        min_v = lifted > 0.0 ? lifted : 0.0;
    }
}

// index 0 is the newest sample and lands on the right edge.
inline double history_plot_x(int index, int oldest_kept, double left, double plot_w)
{
    if (oldest_kept <= 0) {
        return left + plot_w;
    }
    const double t = static_cast<double>(oldest_kept - index) / static_cast<double>(oldest_kept);
    return left + plot_w * t;
}
