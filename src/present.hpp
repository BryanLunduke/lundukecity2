// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#pragma once

// Presentation timing and layout that can be asserted without a window.
// Motion stays in the same family as the city-open dither. The dither itself
// is a few hundred milliseconds. These smaller beats stay shorter than that.

#include <algorithm>
#include <cmath>
#include <cstdlib>
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

// Auto-goto pan. 280 ms, then the marker blinks for a couple of seconds.
inline constexpr int kGlideMs = 280;
inline constexpr int kMarkerMs = 2200;
inline constexpr int kMarkerBlink = 200;

// A long idle frame must not skip the blink. The ring steps at most one
// blink-slice per call so a paused city still flashes for the whole beat.
inline int marker_step_ms(int elapsed_ms)
{
    if (elapsed_ms < 0) {
        return 0;
    }
    const int slice = kMarkerBlink / 2;
    if (elapsed_ms > slice) {
        return slice;
    }
    return elapsed_ms;
}

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

// The selection wash must cover the glyphs, including the baseline, and it
// must stay inside the text field. box_top/box_h are the field rectangle.
struct SelectionBand {
    double top = 0;
    double height = 0;
};

inline SelectionBand selection_band(double text_top, double line_h, double box_top, double box_h)
{
    const double glyph_h = std::max(1.0, line_h);
    const double above = 1.0;
    const double below = std::max(2.0, glyph_h * 0.2);
    double top = text_top - above;
    double bottom = text_top + glyph_h + below;
    if (box_h > 4.0) {
        const double limit_top = box_top + 2.0;
        const double limit_bottom = box_top + box_h - 2.0;
        if (top < limit_top) {
            top = limit_top;
        }
        if (bottom > limit_bottom) {
            bottom = limit_bottom;
        }
        // Keep the ink covered when the field has room for the whole line.
        if (text_top >= limit_top && text_top + glyph_h <= limit_bottom) {
            if (top > text_top) {
                top = text_top;
            }
            if (bottom < text_top + glyph_h) {
                bottom = text_top + glyph_h;
            }
        }
    }
    if (bottom < top + 1.0) {
        bottom = top + 1.0;
    }
    SelectionBand band;
    band.top = top;
    band.height = bottom - top;
    return band;
}

// Pixel size used for layout. While fullscreen is being left, the drawable
// can still report the screen. The layout follows the restored window until
// the drawable agrees with it.
struct LayoutPixels {
    int w = 1;
    int h = 1;
};

inline LayoutPixels layout_pixels_leaving_fullscreen(bool restoring, int live_w, int live_h, int restored_w,
                                                    int restored_h, float density)
{
    LayoutPixels out;
    out.w = std::max(1, live_w);
    out.h = std::max(1, live_h);
    if (!restoring || restored_w < 320 || restored_h < 240) {
        return out;
    }
    if (!(density > 0.0f)) {
        density = 1.0f;
    }
    const int want_w = std::max(1, static_cast<int>(std::lround(static_cast<double>(restored_w) * density)));
    const int want_h = std::max(1, static_cast<int>(std::lround(static_cast<double>(restored_h) * density)));
    if (std::abs(live_w - want_w) <= 48 && std::abs(live_h - want_h) <= 48) {
        return out;
    }
    out.w = want_w;
    out.h = want_h;
    return out;
}

// Fullscreen uses the live pixel size. A restore in progress must not keep
// the windowed layout once the window is fullscreen again.
inline LayoutPixels layout_pixels_for_window(bool fullscreen, bool restoring, int pixel_w, int pixel_h, int restored_w,
                                            int restored_h, float density)
{
    if (fullscreen) {
        LayoutPixels out;
        out.w = std::max(1, pixel_w);
        out.h = std::max(1, pixel_h);
        return out;
    }
    return layout_pixels_leaving_fullscreen(restoring, pixel_w, pixel_h, restored_w, restored_h, density);
}

struct LayoutPoint {
    double x = 0;
    double y = 0;
};

// Map a window-coordinate point into the layout. After leaving fullscreen the
// point stays in the restored window, even if the drawable is still the screen.
inline LayoutPoint window_point_to_layout(double x, double y, bool restoring, int window_w, int window_h, int pixel_w,
                                         int pixel_h, int restored_w, int restored_h, float density)
{
    const LayoutPixels laid =
        layout_pixels_leaving_fullscreen(restoring, pixel_w, pixel_h, restored_w, restored_h, density);
    LayoutPoint point;
    point.x = x * static_cast<double>(laid.w) / static_cast<double>(std::max(window_w, 1));
    point.y = y * static_cast<double>(laid.h) / static_cast<double>(std::max(window_h, 1));
    return point;
}

// The X window can already be the windowed size while SDL's size, or the
// renderer output, is still the screen. A same-size resize does not refresh
// that drawable. True means the caller has to change the size and change it
// back before drawing.
inline bool fullscreen_exit_needs_drawable_refresh(int window_w, int window_h, int renderer_w, int renderer_h,
                                                  int restored_w, int restored_h, float density)
{
    if (restored_w < 320 || restored_h < 240) {
        return false;
    }
    if (!(density > 0.0f)) {
        density = 1.0f;
    }
    const bool window_stale = std::abs(window_w - restored_w) > 48 || std::abs(window_h - restored_h) > 48;
    const int want_w = std::max(1, static_cast<int>(std::lround(static_cast<double>(restored_w) * density)));
    const int want_h = std::max(1, static_cast<int>(std::lround(static_cast<double>(restored_h) * density)));
    const bool renderer_stale = renderer_w > 0 && renderer_h > 0 &&
                                (std::abs(renderer_w - want_w) > 48 || std::abs(renderer_h - want_h) > 48);
    return window_stale || renderer_stale;
}

// A leave-fullscreen configure often reports the screen origin. That is not
// where the windowed frame was.
inline bool fullscreen_exit_position_is_spurious(int live_x, int live_y, int kept_x, int kept_y)
{
    return std::abs(live_x - kept_x) > 48 || std::abs(live_y - kept_y) > 48;
}

// Where to put the windowed frame when the renderer output is still the
// screen. A bottom-origin drawable shows the bottom of that output, so the
// frame has to sit there or the menu is clipped. A top-origin drawable shows
// the top, so the frame stays at the origin. Never stretch it to the screen.
struct PresentPlace {
    double x = 0;
    double y = 0;
    double w = 1;
    double h = 1;
    bool fitted = false;
};

inline PresentPlace present_place(int layout_w, int layout_h, int output_w, int output_h, bool bottom_origin)
{
    PresentPlace place;
    place.w = std::max(1, layout_w);
    place.h = std::max(1, layout_h);
    if (output_w > layout_w + 48 || output_h > layout_h + 48) {
        place.fitted = true;
        if (bottom_origin) {
            place.y = std::max(0, output_h - layout_h);
        }
    }
    return place;
}

// Skip at most one present while the drawable is still the screen, so a
// stale frame cannot sit on screen and the loop cannot freeze.
inline bool hold_stale_fullscreen_present(bool restoring, bool drawable_stale, int skipped)
{
    return restoring && drawable_stale && skipped < 1;
}

struct YearBoxPlan {
    double top_h = 1;
    double low_h = 1;
};

// Bottom cards keep the height their text needs. Extra paper is used first.
// The top cards shrink before a bottom line is clipped.
inline YearBoxPlan plan_year_boxes(double room, double extra, double gap, double top_need, double low_need)
{
    if (gap < 0.0) {
        gap = 0.0;
    }
    if (top_need < 1.0) {
        top_need = 1.0;
    }
    if (low_need < 1.0) {
        low_need = 1.0;
    }
    if (extra < 0.0) {
        extra = 0.0;
    }
    if (room < 1.0) {
        room = 1.0;
    }
    const double need = top_need + gap + low_need;
    double grow = 0.0;
    if (need > room) {
        grow = std::min(extra, need - room);
    }
    const double usable = room + grow;
    YearBoxPlan plan;
    if (top_need + gap + low_need <= usable + 0.5) {
        plan.top_h = top_need;
        plan.low_h = low_need;
        return plan;
    }
    plan.low_h = std::min(low_need, std::max(1.0, usable - gap - 1.0));
    plan.top_h = std::max(1.0, usable - gap - plan.low_h);
    if (plan.low_h + 0.5 < low_need && plan.top_h > 1.0) {
        const double give = std::min(plan.top_h - 1.0, low_need - plan.low_h);
        plan.top_h -= give;
        plan.low_h += give;
    }
    return plan;
}

// Focused dialog buttons use the Quit notice's blue, not a pale wash.
struct DialogFocusFill {
    double r = 0.20;
    double g = 0.42;
    double b = 0.72;
};

inline DialogFocusFill dialog_focus_fill()
{
    return {};
}

// The thumb stays on the track at both ends. At 100% it used to hang past
// the right end by half its width.
inline double slider_thumb_x(double track_x, double track_w, double thumb_w, double t)
{
    if (t < 0.0) {
        t = 0.0;
    }
    if (t > 1.0) {
        t = 1.0;
    }
    if (thumb_w < 1.0) {
        thumb_w = 1.0;
    }
    if (track_w < thumb_w) {
        thumb_w = std::max(1.0, track_w);
    }
    return track_x + (std::max(0.0, track_w - thumb_w)) * t;
}

// Settings rows. The value and the slider sit under the caption, and the
// next caption starts after the slider, so they cannot share a line.
struct SettingsStack {
    double caption = 0;
    double bar = 0;
    double next_caption = 0;
};

inline SettingsStack settings_stack(double origin, double text_h, double bar_h, double gap)
{
    SettingsStack row;
    row.caption = origin;
    row.bar = origin + text_h + gap;
    row.next_caption = row.bar + bar_h + gap;
    return row;
}

inline bool settings_stack_clear(const SettingsStack &row, double text_h)
{
    return row.bar >= row.caption + text_h && row.next_caption >= row.bar + 1.0;
}

enum class YearCardClick { Budget, Evaluation, Graphs, Map, Close, Escape };

// Close, Escape, and Map leave the year card. Map also jumps. Budget opens
// the sliders and stays in the dialog.
inline bool year_card_dismisses(YearCardClick click)
{
    return click == YearCardClick::Close || click == YearCardClick::Escape || click == YearCardClick::Map;
}

inline bool year_card_jumps(YearCardClick click)
{
    return click == YearCardClick::Map;
}

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
