// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#include "shell_state.hpp"

#include "focus_ring.hpp"

#include "assets.hpp"
#include "city_glance.hpp"
#include "city_seed.hpp"
#include "controls_page.hpp"
#include "vga_palette.hpp"
#include "dialog_copy.hpp"
#include "dialog_layout.hpp"
#include "file_list.hpp"
#include "text_edit.hpp"
#include "graph_legend.hpp"
#include "heat_field.hpp"
#include "inspect_report.hpp"
#include "layout_mode.hpp"
#include "tool_groups.hpp"
#include "list_scroll.hpp"
#include "messages.hpp"
#include "notice_bar.hpp"
#include "present.hpp"
#include "osk_layout.hpp"
#include "redraw.hpp"
#include "text_paint.hpp"
#include "theme.hpp"
#include "tile_atlas.hpp"
#include "scenario_preview.hpp"
#include "special_buildings.hpp"
#include "tool_keys.hpp"
#include "placement_hint.hpp"
#include "tools.hpp"
#include "version.hpp"

#include "micropolis.h"

#include <cstdlib>
#include "version.hpp"
#include "view_math.hpp"
#include "zoom_keys.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <initializer_list>
#include <cmath>
#include <string>

namespace {

const char *speed_word(int speed)
{
    switch (speed) {
    case 1:
        return "Slow";
    case 2:
        return "Medium";
    case 3:
        return "Fast";
    default:
        return "Paused";
    }
}

// A hint clause is "A closes". An unbound button contributes nothing, so the
// line never reads " closes" or "A closes  ".
std::string hint_button(const std::string &glyph, const char *words)
{
    if (glyph.empty() || words == nullptr || words[0] == '\0') {
        return {};
    }
    return glyph + " " + words;
}

std::string hint_pair(const std::string &left, const std::string &right, const char *words)
{
    if (words == nullptr || words[0] == '\0' || (left.empty() && right.empty())) {
        return {};
    }
    if (left.empty()) {
        return right + " " + words;
    }
    if (right.empty()) {
        return left + " " + words;
    }
    return left + "/" + right + " " + words;
}

// Clauses are separated by ". " and a missing clause is dropped. That keeps
// one space between words when a button name is absent.
std::string hint_join(std::initializer_list<std::string> parts)
{
    std::string out;
    for (const std::string &part : parts) {
        std::string piece;
        for (char ch : part) {
            if (ch == ' ' && (piece.empty() || piece.back() == ' ')) {
                continue;
            }
            piece.push_back(ch);
        }
        while (!piece.empty() && piece.back() == ' ') {
            piece.pop_back();
        }
        if (piece.empty()) {
            continue;
        }
        if (!out.empty()) {
            out += ". ";
        }
        out += piece;
    }
    return out;
}

void stroke_slider_focus(cairo_t *cr, double x, double y, double w, double h)
{
    const SliderFocusRing ring = slider_focus_ring();
    auto apply = [&](Rgb8 colour) {
        cairo_set_source_rgb(cr, colour.r / 255.0, colour.g / 255.0, colour.b / 255.0);
    };
    apply(ring.outer);
    cairo_set_line_width(cr, 3.5);
    cairo_rectangle(cr, x, y, w, h);
    cairo_stroke(cr);
    apply(ring.inner);
    cairo_set_line_width(cr, 1.5);
    cairo_rectangle(cr, x + 1.0, y + 1.0, std::max(1.0, w - 2.0), std::max(1.0, h - 2.0));
    cairo_stroke(cr);
}

// A second context on the same image surface, with its own transform.
// cairo_create references the target; destroying the context releases it.
struct TargetCr {
    cairo_t *cr;
    explicit TargetCr(cairo_t *parent)
        : cr(cairo_create(cairo_get_target(parent)))
    {
    }
    ~TargetCr() { cairo_destroy(cr); }
    TargetCr(const TargetCr &) = delete;
    TargetCr &operator=(const TargetCr &) = delete;
};

void fill(cairo_t *cr, double x, double y, double w, double h, double r, double g, double b)
{
    cairo_set_source_rgb(cr, r, g, b);
    cairo_rectangle(cr, x, y, w, h);
    cairo_fill(cr);
}

void stroke_rect(cairo_t *cr, double x, double y, double w, double h, double r, double g, double b)
{
    cairo_set_source_rgb(cr, r, g, b);
    cairo_set_line_width(cr, 1);
    cairo_rectangle(cr, x + 0.5, y + 0.5, w - 1, h - 1);
    cairo_stroke(cr);
}

} // namespace

void Shell::layout()
{
    if (window_ == nullptr) {
        return;
    }
    refresh_ui_scale();
    int frame_w = 0;
    int frame_h = 0;
    int pixel_w = 0;
    int pixel_h = 0;
    SDL_GetWindowSize(window_, &frame_w, &frame_h);
    SDL_GetWindowSizeInPixels(window_, &pixel_w, &pixel_h);
    int renderer_w = 0;
    int renderer_h = 0;
    if (renderer_ != nullptr) {
        SDL_GetRenderOutputSize(renderer_, &renderer_w, &renderer_h);
    }
    const bool fullscreen = window_ != nullptr && (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) != 0;
    // Leaving fullscreen is authoritative even while the flag and the
    // reported pixels are still the screen. Entering clears that flag first.
    const LayoutPixels laid = layout_pixels_for_window(fullscreen, restoring_windowed_, pixel_w, pixel_h, normal_w_,
                                                      normal_h_, windowed_density_);
    if (restoring_windowed_ &&
        !fullscreen_exit_needs_drawable_refresh(frame_w, frame_h, renderer_w, renderer_h, normal_w_, normal_h_,
                                               windowed_density_)) {
        int pos_x = 0;
        int pos_y = 0;
        SDL_GetWindowPosition(window_, &pos_x, &pos_y);
        if (!fullscreen_exit_position_is_spurious(pos_x, pos_y, normal_x_, normal_y_)) {
            restoring_windowed_ = false;
            windowed_settle_tries_ = 0;
            fullscreen_hold_frames_ = 0;
        }
    }
    win_w_ = laid.w;
    win_h_ = laid.h;
    const float fitted = ui_scale::clamp_ui_scale(ui_scale_, win_w_, win_h_);
    if (std::fabs(fitted - ui_scale_) > 0.001f) {
        ui_scale_ = fitted;
        font_ui_ = ui_scale::scaled_font("Sans", 13, ui_scale_);
        font_body_ = ui_scale::scaled_font("Sans", 14, ui_scale_);
        font_title_ = ui_scale::scaled_font("Sans Bold", 18, ui_scale_);
        font_menu_ = ui_scale::scaled_font("Sans", 13, ui_scale_);
        font_bold_ = ui_scale::scaled_font("Sans Bold", 13, ui_scale_);
    }
    if (win_w_ < 1) {
        win_w_ = 1;
    }
    if (win_h_ < 1) {
        win_h_ = 1;
    }
    menu_h_ = dp(36);
    status_h_ = dp(36);
    handheld_ = layout_mode::use_handheld(layout_pref_, deck_, handheld_device());
    band_y_ = menu_h_;
    band_h_ = 0;
    hud_top_ = 0;
    hud_bottom_ = 0;
    hud_right_ = 0;
    quick_y_ = 0;
    quick_h_ = 0;
    hint_h_ = 0;
    palette_.set_style(slate_ || handheld_, 0);
    if (handheld_) {
        // Handheld: the map is the whole screen. The menu bar shows only
        // while a menu is open; a quick bar and a hint strip sit at the
        // bottom and a compact info pill at the top, all over the map.
        status_h_ = 0;
        cell_ = ui_scale::toolbar_cell(ui_scale_, std::max(1, win_h_));
        side_x_ = 0;
        side_w_ = 0;
        pal_y_ = win_h_;
        palette_.set_columns(2);
        palette_.set_cell(cell_);
        hint_h_ = dp(28);
        quick_h_ = std::min(dp(66), std::max(dp(44), (win_w_ - dp(40)) / kToolCount));
        quick_y_ = win_h_ - hint_h_ - quick_h_ - dp(6);
        hud_top_ = dp(54);
        hud_bottom_ = win_h_ - quick_y_ + dp(6);
        hud_right_ = dp(70);
        mini_w_ = std::min(dp(200), std::max(dp(120), win_w_ / 6));
        mini_h_ = mini_w_ * 5 / 6;
        mini_x_ = win_w_ - mini_w_ - dp(10);
        mini_y_ = dp(10);
        demand_y_ = 0;
        demand_h_ = 0;
        view_x_ = 0;
        view_y_ = 0;
        view_w_ = std::max(dp(32), win_w_);
        view_h_ = std::max(dp(32), win_h_);
    } else {
        if (slate_) {
            band_h_ = dp(46);
        }
        // No message bar under the status line. Alerts are drawn over the map.
        const int body = win_h_ - menu_h_ - band_h_ - status_h_;
        cell_ = ui_scale::toolbar_cell(ui_scale_, std::max(1, body));
        side_x_ = dp(6);
        pal_y_ = menu_h_ + band_h_ + dp(6);
        const int status_top = win_h_ - status_h_;
        const int gap = dp(8);
        const int demand_ok = dp(64);
        const int header = slate_ ? dp(18) : 0;
        auto pal_height = [&](int columns) {
            return header > 0 ? ToolPalette::grouped_height_for(cell_, columns, header)
                              : ToolPalette::height_for(cell_, columns);
        };
        // The minimap sits over the top-right corner of the map (as in the
        // handheld layout), so the tray is only the toolbar, plus the demand
        // meters under it in the classic skin (Slate keeps them in the info
        // band). Three columns when they fit, four in a short window, and a
        // smaller cell only when even four columns would run into the status
        // line (ui_scale has normally shrunk the whole UI before that).
        const int room = status_top - pal_y_ - (slate_ ? 0 : gap + demand_ok);
        int columns = 3;
        if (pal_height(columns) > room) {
            columns = 4;
        }
        const int step = std::max(1, dp(2));
        while (pal_height(columns) > room && cell_ - step >= dp(28)) {
            cell_ -= step;
        }
        palette_.set_columns(columns);
        palette_.set_cell(cell_);
        palette_.set_style(slate_, header);
        side_w_ = ToolPalette::width_for(cell_, columns);
        demand_y_ = pal_y_ + pal_height(columns) + gap;
        demand_h_ = slate_ ? 0 : std::max(0, std::min(dp(kMaxDemandPx), status_top - demand_y_));
        view_x_ = side_x_ + side_w_ + dp(8);
        view_y_ = menu_h_ + band_h_;
        view_w_ = std::max(dp(32), win_w_ - view_x_ - dp(8));
        view_h_ = std::max(dp(32), win_h_ - view_y_ - status_h_);
        // Same size rule as the handheld minimap. Inset from the right edge
        // past the map's vertical scroll bar.
        mini_w_ = std::min(dp(200), std::max(dp(120), view_w_ / 6));
        mini_h_ = mini_w_ * 5 / 6;
        if (mini_h_ + dp(20) > view_h_ || mini_w_ + dp(28) > view_w_) {
            mini_h_ = 0;
        }
        mini_x_ = view_x_ + view_w_ - mini_w_ - dp(14);
        mini_y_ = view_y_ + dp(10);
    }
    map_.set_slate_tags(slate_ || handheld_);
    cursor_x_ = std::max(0.0f, std::min(cursor_x_, static_cast<float>(win_w_ - 1)));
    cursor_y_ = std::max(0.0f, std::min(cursor_y_, static_cast<float>(win_h_ - 1)));
    fit_map_to_view();
    clamp_gamepad_cursor(0.0f, 0.0f);
}

void round_dialog_rect(cairo_t *cr, double x, double y, double w, double h, double radius)
{
    const double rr = std::min(radius, std::min(w, h) / 2.0);
    cairo_new_path(cr);
    cairo_arc(cr, x + w - rr, y + rr, rr, -1.5708, 0);
    cairo_arc(cr, x + w - rr, y + h - rr, rr, 0, 1.5708);
    cairo_arc(cr, x + rr, y + h - rr, rr, 1.5708, 3.1416);
    cairo_arc(cr, x + rr, y + rr, rr, 3.1416, 4.7124);
    cairo_close_path(cr);
}

void Shell::button(cairo_t *cr, double x, double y, double w, double h, const char *label, int id, bool primary,
                    bool selected, bool enabled)
{
    if (enabled) {
        push_hot(x, y, w, h, id);
        push_focus(id);
    }
    // A modal covers the city. Anything that is not part of the dialog must
    // not light up with the dialog's focus.
    const bool suppressed = dialog_ != Dialog::None && !is_modal_control(id);
    const bool hot = enabled && !suppressed && (hover_id_ == id || focus_id_ == id);
    const bool pressed = hot && pointer_down_ && hover_id_ == id;
    const bool focused = enabled && !suppressed && focus_id_ == id;
    // Dialog buttons match the Year in Review card: a gold edge, a blue
    // primary, and a warm gray secondary.
    if (dialog_ != Dialog::None) {
        const double gold_r = 0.72;
        const double gold_g = 0.55;
        const double gold_b = 0.18;
        round_dialog_rect(cr, x, y, w, h, std::min(6.0, h / 2.0));
        cairo_pattern_t *shade = cairo_pattern_create_linear(x, y, x, y + h);
        if (!enabled) {
            cairo_pattern_add_color_stop_rgb(shade, 0, 0.78, 0.76, 0.70);
            cairo_pattern_add_color_stop_rgb(shade, 1, 0.68, 0.66, 0.60);
        } else if (selected) {
            // A chosen chip stays cream with a gold edge. Blue is the action
            // and the focus, so a selected row is not another blue button.
            cairo_pattern_add_color_stop_rgb(shade, 0, 0.99, 0.96, 0.88);
            cairo_pattern_add_color_stop_rgb(shade, 1, 0.93, 0.86, 0.70);
        } else if (primary) {
            cairo_pattern_add_color_stop_rgb(shade, 0, pressed ? 0.08 : 0.20, pressed ? 0.32 : 0.42, pressed ? 0.62 : 0.72);
            cairo_pattern_add_color_stop_rgb(shade, 1, 0.07, 0.24, 0.52);
        } else {
            const double lift = hot ? 0.06 : 0.0;
            cairo_pattern_add_color_stop_rgb(shade, 0, 0.78 + lift, 0.77 + lift, 0.75 + lift);
            cairo_pattern_add_color_stop_rgb(shade, 1, 0.62, 0.58, 0.50);
        }
        cairo_set_source(cr, shade);
        cairo_fill_preserve(cr);
        cairo_pattern_destroy(shade);
        const bool ring = focused && w > 18 && h > 18;
        if (ring) {
            // Focus follows the button's own outline: the edge itself turns
            // focus blue, a little heavier than the gold edge it replaces.
            cairo_set_source_rgb(cr, 0.10, 0.35, 0.78);
            cairo_set_line_width(cr, std::max(3.0, h / 10.0));
        } else {
            cairo_set_source_rgb(cr, enabled ? gold_r : 0.62, enabled ? gold_g : 0.58, enabled ? gold_b : 0.48);
            cairo_set_line_width(cr, selected ? std::max(3.0, h / 12.0) : std::max(2.0, h / 16.0));
        }
        cairo_stroke(cr);
        if (selected && enabled && w > 22 && h > 16) {
            const double mark = std::min(h * 0.42, 14.0);
            const double mx = x + 8;
            const double my = y + (h - mark) / 2.0;
            cairo_set_source_rgb(cr, gold_r, gold_g * 0.85, gold_b);
            cairo_set_line_width(cr, std::max(2.0, mark / 6.0));
            cairo_move_to(cr, mx, my + mark * 0.55);
            cairo_line_to(cr, mx + mark * 0.35, my + mark * 0.9);
            cairo_line_to(cr, mx + mark, my + mark * 0.15);
            cairo_stroke(cr);
        }
        if (ring && primary && !selected) {
            // A blue edge on the blue primary needs a light line just inside
            // it to read as focus. It hugs the same rounded outline.
            const double inset = std::max(3.0, h / 10.0);
            round_dialog_rect(cr, x + inset, y + inset, w - 2.0 * inset, h - 2.0 * inset,
                              std::max(1.0, std::min(6.0, h / 2.0) - inset / 2.0));
            cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
            cairo_set_line_width(cr, 1.5);
            cairo_stroke(cr);
        }
        const std::string plain = strip_mnemonic(label);
        const TextMetrics metrics = measure_text(cr, ui_font(), plain, -1);
        const double shift = selected ? 8.0 : 0.0;
        const double tx = x + (w - metrics.width + shift) / 2.0;
        const double ty = y + (h - metrics.height) / 2.0;
        if (!enabled) {
            paint_mnemonic(cr, ui_font(), label, tx, ty, menu_.alt_shown, 0.45, 0.42, 0.38);
        } else if (primary && !selected) {
            paint_mnemonic(cr, ui_font(), label, tx, ty, menu_.alt_shown, 1, 1, 1);
        } else {
            paint_mnemonic(cr, ui_font(), label, tx, ty, menu_.alt_shown, 0.12, 0.09, 0.06);
        }
        return;
    }
    cairo_pattern_t *pattern = cairo_pattern_create_linear(x, y, x, y + h);
    if (pressed) {
        cairo_pattern_add_color_stop_rgb(pattern, 0, theme::kButtonLoR, theme::kButtonLoG, theme::kButtonLoB);
        cairo_pattern_add_color_stop_rgb(pattern, 1, theme::kButtonEdgeR, theme::kButtonEdgeG, theme::kButtonEdgeB);
    } else if (primary) {
        cairo_pattern_add_color_stop_rgb(pattern, 0, theme::chrome_select_hi_r(), theme::chrome_select_hi_g(), theme::chrome_select_hi_b());
        cairo_pattern_add_color_stop_rgb(pattern, 1, theme::chrome_select_r(), theme::chrome_select_g(), theme::chrome_select_b());
    } else if (hot) {
        cairo_pattern_add_color_stop_rgb(pattern, 0, theme::chrome_hover_r(), theme::chrome_hover_g(), theme::chrome_hover_b());
        cairo_pattern_add_color_stop_rgb(pattern, 1, theme::chrome_hover_r(), theme::chrome_hover_g(), theme::chrome_hover_b());
    } else {
        cairo_pattern_add_color_stop_rgb(pattern, 0, theme::kButtonHiR, theme::kButtonHiG, theme::kButtonHiB);
        cairo_pattern_add_color_stop_rgb(pattern, 1, theme::kButtonLoR, theme::kButtonLoG, theme::kButtonLoB);
    }
    cairo_set_source(cr, pattern);
    cairo_rectangle(cr, x, y, w, h);
    cairo_fill(cr);
    cairo_pattern_destroy(pattern);
    stroke_rect(cr, x, y, w, h, theme::kButtonEdgeR, theme::kButtonEdgeG, theme::kButtonEdgeB);
    if (!suppressed && focus_id_ == id && (gamepad_nav_ || dialog_ != Dialog::None)) {
        // On the button's own edge, not an inner box.
        cairo_set_source_rgb(cr, theme::kFocusR, theme::kFocusG, theme::kFocusB);
        cairo_set_line_width(cr, 2);
        cairo_rectangle(cr, x + 1, y + 1, w - 2, h - 2);
        cairo_stroke(cr);
    }
    const std::string plain = strip_mnemonic(label);
    const TextMetrics metrics = measure_text(cr, ui_font(), plain, -1);
    const double tx = x + (w - metrics.width) / 2.0;
    const double ty = y + (h - metrics.height) / 2.0;
    const bool light = primary;
    paint_mnemonic(cr, ui_font(), label, tx, ty, menu_.alt_shown, light ? 1 : theme::kTextR, light ? 1 : theme::kTextG,
                   light ? 1 : theme::kTextB);
}

void Shell::label(cairo_t *cr, const char *font, const std::string &text, double x, double y, int wrap)
{
    paint_text(cr, font, text, x, y, theme::kTextR, theme::kTextG, theme::kTextB, wrap);
}

void Shell::paint_vga_veil(cairo_t *cr)
{
    static cairo_surface_t *tile = nullptr;
    if (tile == nullptr) {
        tile = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 2, 2);
        unsigned char *pixels = cairo_image_surface_get_data(tile);
        const int stride = cairo_image_surface_get_stride(tile);
        auto put = [&](int px, int py, unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
            unsigned char *byte = pixels + py * stride + px * 4;
            byte[0] = b;
            byte[1] = g;
            byte[2] = r;
            byte[3] = a;
        };
        put(0, 0, 0, 0, 0, 255);
        put(1, 0, 0, 0, 0, 0);
        put(0, 1, 0, 0, 0, 0);
        put(1, 1, 32, 32, 32, 255);
        cairo_surface_mark_dirty(tile);
    }
    cairo_pattern_t *pattern = cairo_pattern_create_for_surface(tile);
    cairo_pattern_set_extend(pattern, CAIRO_EXTEND_REPEAT);
    cairo_pattern_set_filter(pattern, CAIRO_FILTER_NEAREST);
    cairo_set_source(cr, pattern);
    cairo_rectangle(cr, 0, 0, win_w_, win_h_);
    cairo_fill(cr);
    cairo_pattern_destroy(pattern);
}

void Shell::frame_panel(cairo_t *cr, double x, double y, double w, double h, const char *title)
{
    // Same materials as the Year in Review card: a navy rim, a gold edge,
    // and a cream page. The city tiles and the welcome art are untouched.
    const double navy_r = 0.02;
    const double navy_g = 0.08;
    const double navy_b = 0.32;
    const double gold_r = 0.72;
    const double gold_g = 0.55;
    const double gold_b = 0.18;
    const double paper_r = 0.976;
    const double paper_g = 0.922;
    const double paper_b = 0.808;
    const double title_r = 0.45;
    const double title_g = 0.04;
    const double title_b = 0.02;
    cairo_set_source_rgba(cr, 0, 0, 0, 0.35);
    cairo_rectangle(cr, 0, 0, win_w_, win_h_);
    cairo_fill(cr);
    const double rim = std::max(4.0, static_cast<double>(dp(6)));
    fill(cr, x, y, w, h, navy_r, navy_g, navy_b);
    stroke_rect(cr, x + 1.5, y + 1.5, w - 3, h - 3, gold_r, gold_g, gold_b);
    fill(cr, x + rim, y + rim, std::max(1.0, w - 2 * rim), std::max(1.0, h - 2 * rim), paper_r, paper_g, paper_b);
    push_hot(0, 0, win_w_, win_h_, 0);
    const double kTitleBand = dp(40);
    const TextMetrics title_metrics = measure_text(cr, title_font(), title, static_cast<int>(w - 24));
    const double title_y = y + std::max(rim, (kTitleBand - title_metrics.height) / 2.0);
    const bool title_hint = gamepad_nav_ || deck_ || touch_used_;
    const std::string hint = title_hint ? dialog_title_hint() : std::string();
    const TextMetrics hint_metrics = hint.empty() ? TextMetrics{} : measure_text(cr, ui_font(), hint, -1);
    const int title_wrap = static_cast<int>(std::max(40.0, w - rim * 2 - 16 - (hint.empty() ? 0 : hint_metrics.width + 16)));
    paint_text(cr, title_font(), title, x + rim + 8, title_y, title_r, title_g, title_b, title_wrap);
    if (!hint.empty() && hint_metrics.width + 16 < w - title_metrics.width - 48) {
        paint_text(cr, ui_font(), hint, x + w - rim - hint_metrics.width - 8, title_y, 0.28, 0.16, 0.06, -1);
    }
    cairo_set_source_rgb(cr, gold_r, gold_g, gold_b);
    cairo_set_line_width(cr, 2);
    cairo_move_to(cr, x + rim + 8, y + kTitleBand);
    cairo_line_to(cr, x + w - rim - 8, y + kTitleBand);
    cairo_stroke(cr);
}

void Shell::draw_osk(cairo_t *cr, double x, double y, double w, double max_h)
{
    if (!osk_visible() || w < 80 || max_h < 40) {
        return;
    }
    int count = 0;
    const OskKey *keys = osk_keys(count);
    OskPlacement placed[64];
    const int n = osk_place(x, y, w, max_h, placed, 64);
    cairo_save(cr);
    cairo_rectangle(cr, x, y, w, max_h);
    cairo_clip(cr);
    for (int i = 0; i < n; ++i) {
        const OskPlacement &box = placed[i];
        if (box.index < 0 || box.index >= count) {
            continue;
        }
        const OskKey &key = keys[box.index];
        const bool shifted = osk_shift_ && key.kind == OskKind::Text && key.shifted != nullptr;
        const char *label = shifted ? key.shifted : key.label;
        const bool primary = (key.kind == OskKind::Shift && osk_shift_) || focus_id_ == kOsk + box.index;
        button(cr, box.x, box.y, box.w, box.h, label, kOsk + box.index, primary);
    }
    cairo_restore(cr);
}

void Shell::draw_menubar(cairo_t *cr)
{
    const bool dark = slate_ || handheld_;
    cairo_pattern_t *bar = cairo_pattern_create_linear(0, 0, 0, menu_h_);
    if (dark) {
        cairo_pattern_add_color_stop_rgb(bar, 0, 0x1D / 255.0, 0x25 / 255.0, 0x30 / 255.0);
        cairo_pattern_add_color_stop_rgb(bar, 1, 0x15 / 255.0, 0x1B / 255.0, 0x23 / 255.0);
    } else {
        cairo_pattern_add_color_stop_rgb(bar, 0, theme::kMenuTopR, theme::kMenuTopG, theme::kMenuTopB);
        cairo_pattern_add_color_stop_rgb(bar, 1, theme::kMenuBotR, theme::kMenuBotG, theme::kMenuBotB);
    }
    cairo_set_source(cr, bar);
    cairo_rectangle(cr, 0, 0, win_w_, menu_h_);
    cairo_fill(cr);
    cairo_pattern_destroy(bar);
    if (dark) {
        fill(cr, 0, menu_h_ - 1, win_w_, 1, 0x2E / 255.0, 0x38 / 255.0, 0x46 / 255.0);
    }
    const double ink_r = dark ? 0xEE / 255.0 : theme::kTextR;
    const double ink_g = dark ? 0xE8 / 255.0 : theme::kTextG;
    const double ink_b = dark ? 0xDA / 255.0 : theme::kTextB;
    double x = 4;
    for (int c = 0; c < menu_table::kColumnCount; ++c) {
        const char *title = menu_table::column_title(c);
        const std::string plain = strip_mnemonic(title);
        const TextMetrics metrics = measure_text(cr, menu_font(), plain, -1);
        const double w = metrics.width + 22;
        const int id = kMenuTitle + c;
        push_hot(x, 2, w, menu_h_ - 4, id);
        const bool open = menu_.open_menu == c;
        // While a menu is open, only that title is lit. A hover left on
        // System used to stay blue after the keyboard moved to Options.
        const bool lit = dialog_ == Dialog::None && (open || (hover_id_ == id && !menu_.open()));
        if (lit && dark) {
            fill(cr, x, 3, w, menu_h_ - 6, 0x30 / 255.0, 0x3D / 255.0, 0x50 / 255.0);
            fill(cr, x + 6, menu_h_ - 6, w - 12, 2, 0xF2 / 255.0, 0xC1 / 255.0, 0x4E / 255.0);
        } else if (lit) {
            fill(cr, x, 3, w, menu_h_ - 6, theme::chrome_select_r(), theme::chrome_select_g(), theme::chrome_select_b());
            stroke_rect(cr, x, 3, w, menu_h_ - 6, theme::chrome_select_edge_r(), theme::chrome_select_edge_g(), theme::chrome_select_edge_b());
        }
        paint_mnemonic(cr, menu_font(), title, x + 10, 9, menu_.alt_shown || menu_.open(), lit && !dark ? 1 : ink_r,
                       lit && !dark ? 1 : ink_g, lit && !dark ? 1 : ink_b);
        x += w + 2;
    }
    const std::string zoom = zoom_menu_label(map_.tile_size());
    const TextMetrics zoom_metrics = measure_text(cr, menu_font(), zoom, -1);
    const double zoom_x = static_cast<double>(win_w_) - zoom_metrics.width - 12.0;
    if (zoom_x > x + 8.0) {
        paint_text(cr, menu_font(), zoom, zoom_x, 9, dark ? 0x9A / 255.0 : theme::kTextR, dark ? 0xA6 / 255.0 : theme::kTextG,
                   dark ? 0xB8 / 255.0 : theme::kTextB, -1);
    }
}

void Shell::draw_menu_dropdown(cairo_t *cr)
{
    struct Ink {
        double r;
        double g;
        double b;
    };
    const bool dark = slate_ || handheld_;
    const Ink ink_bg = dark ? Ink{0x23 / 255.0, 0x2C / 255.0, 0x38 / 255.0} : Ink{theme::kMenuBgR, theme::kMenuBgG, theme::kMenuBgB};
    const Ink ink_frame =
        dark ? Ink{0x4A / 255.0, 0x56 / 255.0, 0x6A / 255.0} : Ink{theme::kMenuFrameR, theme::kMenuFrameG, theme::kMenuFrameB};
    const Ink ink_text = dark ? Ink{0xEE / 255.0, 0xE8 / 255.0, 0xDA / 255.0} : Ink{theme::kTextR, theme::kTextG, theme::kTextB};
    const Ink ink_accel = dark ? Ink{0x9A / 255.0, 0xA6 / 255.0, 0xB8 / 255.0} : Ink{theme::kAccelR, theme::kAccelG, theme::kAccelB};
    const Ink ink_hover = dark ? Ink{0x3A / 255.0, 0x4B / 255.0, 0x63 / 255.0}
                               : Ink{theme::chrome_hover_r(), theme::chrome_hover_g(), theme::chrome_hover_b()};
    const Ink ink_track = dark ? Ink{0x2E / 255.0, 0x38 / 255.0, 0x46 / 255.0} : Ink{0.86, 0.86, 0.86};
    if (!menu_.open() || dialog_ != Dialog::None) {
        menu_scroll_menu_ = -2;
        menu_thumb_drag_ = false;
        menu_edge_dir_ = 0;
        return;
    }
    // Everything under an open menu belongs to the menu until it closes.
    // Rows are pushed after this full-window catcher, so hit() finds them
    // first. The menu bar itself is above this rect and stays clickable.
    push_hot(0, menu_h_, win_w_, std::max(0, win_h_ - menu_h_), 0);
    const int column = menu_.open_menu;
    const MenuEntry *items = menu_table::column_items(column);
    const int count = menu_table::column_count(column);
    double drop_x = 4;
    for (int c = 0; c < column; ++c) {
        const TextMetrics metrics = measure_text(cr, menu_font(), strip_mnemonic(menu_table::column_title(c)), -1);
        drop_x += metrics.width + 24;
    }
    // 36px rows read well at 1280. A short window keeps the label inside the
    // row. A desktop popup may cover the status and hint bars; stopping
    // above them pushed the last Options row off a 1024x640 window. On a
    // Deck the popup stops above those bars and scrolls, so the hint stays.
    int n_item = 0;
    int n_sep = 0;
    for (int i = 0; i < count; ++i) {
        if (items[i].kind == ItemKind::Separator) {
            ++n_sep;
        } else {
            ++n_item;
        }
    }
    double avail = std::max(80.0, static_cast<double>(win_h_ - menu_h_ - 4));
    if (deck_) {
        const double bars = static_cast<double>(status_h_);
        avail = std::max(120.0, static_cast<double>(win_h_ - menu_h_) - bars - 4.0);
    }
    const int label_h = measure_text(cr, menu_font(), "Ag", -1).height;
    menu_table::MenuPitch pitch =
        menu_table::menu_pitch(avail, n_item, n_sep, static_cast<double>(control_px(36, 48)), label_h);
    if (deck_ || touch_used_) {
        const double mark = std::min(pitch.row_h - 10.0, std::max(pitch.mark, 28.0));
        pitch.mark = mark;
        pitch.mark_dy = std::max(2.0, (pitch.row_h - mark) * 0.5);
    }
    const double row_h = pitch.row_h;
    const double drop_w = dp(320);
    double content_h = 5;
    double item_top[64];
    const int shown = count > 64 ? 64 : count;
    {
        double rel = 5;
        for (int i = 0; i < shown; ++i) {
            item_top[i] = rel;
            const double h = items[i].kind == ItemKind::Separator ? 8.0 : row_h;
            rel += h;
        }
        content_h = rel;
    }
    for (int i = 0; i < shown; ++i) {
        menu_item_top_[i] = item_top[i];
        menu_item_h_[i] = items[i].kind == ItemKind::Separator ? 8.0 : row_h;
    }
    menu_item_n_ = shown;
    menu_content_h_ = content_h;
    menu_view_limit_ = avail;
    if (menu_.open_menu != menu_scroll_menu_ || menu_.cursor != menu_scroll_cursor_) {
        if (menu_.cursor >= 0 && menu_.cursor < shown) {
            menu_scroll_ = menu_table::menu_reveal_scroll(menu_item_top_, menu_item_h_, shown, menu_.cursor, avail,
                                                          content_h);
        } else {
            menu_scroll_ = 0;
        }
        menu_scroll_menu_ = menu_.open_menu;
        menu_scroll_cursor_ = menu_.cursor;
    }
    {
        // Snap to a row start. A separator stays at the top only when
        // stepping past it would hide the last row. Never step backward:
        // that kept Messages below the fold on a short window.
        const double end = menu_table::menu_reveal_scroll(menu_item_top_, menu_item_h_, shown, shown - 1, avail, content_h);
        menu_scroll_ = menu_table::menu_clamped_scroll(menu_item_top_, items, shown, menu_scroll_, end);
    }
    const double scroll = menu_scroll_;
    double visible_bottom = static_cast<double>(menu_h_) + 5.0;
    for (int i = 0; i < shown; ++i) {
        const double y = static_cast<double>(menu_h_) + item_top[i] - scroll;
        const double h = menu_item_h_[i];
        if (y + 0.5 < static_cast<double>(menu_h_)) {
            continue;
        }
        if (y + h > static_cast<double>(menu_h_) + avail + 0.5) {
            continue;
        }
        visible_bottom = y + h;
    }
    double drop_h = content_h <= avail + 0.5 ? content_h : std::min(avail, visible_bottom - menu_h_ + 4.0);
    const double max_drop = std::max(80.0, static_cast<double>(win_h_ - menu_h_ - 4));
    if (drop_h > max_drop) {
        drop_h = max_drop;
    }
    if (drop_x + drop_w > win_w_ - 4) {
        drop_x = std::max(4.0, win_w_ - drop_w - 4);
    }
    menu_drop_x_ = drop_x;
    menu_drop_w_ = drop_w;
    menu_drop_h_ = drop_h;
    const bool menu_scrolls = content_h > avail + 0.5;
    const double row_w = menu_scrolls ? drop_w - 18.0 : drop_w - 4.0;
    // A short offset, not a blur.
    cairo_set_source_rgba(cr, 0, 0, 0, 0.18);
    cairo_rectangle(cr, drop_x + 3, menu_h_ + 4, drop_w, drop_h);
    cairo_fill(cr);
    fill(cr, drop_x, menu_h_, drop_w, drop_h, ink_bg.r, ink_bg.g, ink_bg.b);
    stroke_rect(cr, drop_x, menu_h_, drop_w, drop_h, ink_frame.r, ink_frame.g, ink_frame.b);
    cairo_save(cr);
    cairo_rectangle(cr, drop_x, menu_h_, drop_w, drop_h);
    cairo_clip(cr);
    double recent_row_y = menu_h_ + 5;
    bool have_recent_row = false;
    for (int i = 0; i < shown; ++i) {
        const MenuEntry &item = items[i];
        const double y = menu_h_ + item_top[i] - scroll;
        const double h = item.kind == ItemKind::Separator ? 8.0 : row_h;
        // A row that starts inside the menu but runs past the bottom used to
        // be clipped through the middle of the label.
        if (y + 0.5 < menu_h_ || y + h > menu_h_ + drop_h + 0.5) {
            continue;
        }
        if (item.kind == ItemKind::Separator) {
            cairo_set_source_rgb(cr, ink_frame.r, ink_frame.g, ink_frame.b);
            cairo_move_to(cr, drop_x + 4, y + 4);
            cairo_line_to(cr, drop_x + drop_w - 4, y + 4);
            cairo_stroke(cr);
            continue;
        }
        const int id = kMenuItem + column * 40 + i;
        push_hot(drop_x + 2, y, row_w, h, id);
        const bool selected = menu_.shows_cursor_at(i) || hover_id_ == id;
        if (selected) {
            fill(cr, drop_x + 3, y, drop_w - 6, row_h, ink_hover.r, ink_hover.g, ink_hover.b);
            if (gamepad_nav_ && menu_.shows_cursor_at(i)) {
                cairo_set_source_rgb(cr, theme::kFocusR, theme::kFocusG, theme::kFocusB);
                cairo_set_line_width(cr, 2);
                const double inset = h >= 28.0 ? 2.0 : 1.0;
                cairo_rectangle(cr, drop_x + 4, y + inset, drop_w - 8, h - inset * 2);
                cairo_stroke(cr);
            }
        }
        if (item.kind == ItemKind::Check || item.kind == ItemKind::Radio) {
            bool on = false;
            if (item.action == Action::AutoBudget) {
                on = session_.auto_budget();
            } else if (item.action == Action::AutoBulldoze) {
                on = session_.auto_bulldoze();
            } else if (item.action == Action::DisastersEnabled) {
                on = session_.disasters();
            } else if (item.action == Action::AutoGoto) {
                on = session_.auto_goto();
            } else if (item.action == Action::Mute) {
                on = !session_.sound_enabled();
            } else if (item.action == Action::Fullscreen) {
                on = window_ != nullptr && (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) != 0;
            } else if (item.action == Action::NightMode) {
                on = night_;
            } else if (item.action == Action::Power) {
                on = overlay_on_ && overlay_.layer() == CitySession::MapLayer::Power;
            } else if (item.action == Action::Zones) {
                on = overlay_on_ && overlay_.layer() == CitySession::MapLayer::Zones;
            } else if (item.action == Action::ZoneResidential) {
                on = overlay_on_ && overlay_.layer() == CitySession::MapLayer::Residential;
            } else if (item.action == Action::ZoneCommercial) {
                on = overlay_on_ && overlay_.layer() == CitySession::MapLayer::Commercial;
            } else if (item.action == Action::ZoneIndustrial) {
                on = overlay_on_ && overlay_.layer() == CitySession::MapLayer::Industrial;
            } else if (item.action == Action::Transportation) {
                on = overlay_on_ && overlay_.layer() == CitySession::MapLayer::Transportation;
            } else if (item.action == Action::Pollution) {
                on = overlay_on_ && overlay_.layer() == CitySession::MapLayer::Pollution;
            } else if (item.action == Action::Crime) {
                on = overlay_on_ && overlay_.layer() == CitySession::MapLayer::Crime;
            } else if (item.action == Action::LandValue) {
                on = overlay_on_ && overlay_.layer() == CitySession::MapLayer::LandValue;
            } else if (item.action == Action::Traffic) {
                on = overlay_on_ && overlay_.layer() == CitySession::MapLayer::Traffic;
            } else if (item.action == Action::PoliceMap) {
                on = overlay_on_ && overlay_.layer() == CitySession::MapLayer::Police;
            } else if (item.action == Action::FireMap) {
                on = overlay_on_ && overlay_.layer() == CitySession::MapLayer::Fire;
            } else if (item.action == Action::Population) {
                on = overlay_on_ && overlay_.layer() == CitySession::MapLayer::Population;
            } else if (item.action == Action::Growth) {
                on = overlay_on_ && overlay_.layer() == CitySession::MapLayer::Growth;
            } else if (item.action == Action::SetPause) {
                on = speed_ == 0;
            } else if (item.action == Action::Slow) {
                on = speed_ == 1;
            } else if (item.action == Action::Medium) {
                on = speed_ == 2;
            } else if (item.action == Action::Fast) {
                on = speed_ == 3;
            } else if (item.action == Action::ClassicLook) {
                on = drawn_look_ == 0;
            } else if (item.action == Action::SvgaLook) {
                on = drawn_look_ == 1;
            } else if (item.action == Action::SpookyLook) {
                on = drawn_look_ == 2;
            }
            cairo_set_source_rgb(cr, ink_text.r, ink_text.g, ink_text.b);
            const double mark_x = drop_x + 8;
            const double mark_y = y + pitch.mark_dy;
            const double mark = pitch.mark;
            if (item.kind == ItemKind::Check) {
                cairo_rectangle(cr, mark_x, mark_y, mark, mark);
                cairo_stroke(cr);
                if (on) {
                    cairo_move_to(cr, mark_x + mark * 0.15, mark_y + mark * 0.55);
                    cairo_line_to(cr, mark_x + mark * 0.40, mark_y + mark * 0.80);
                    cairo_line_to(cr, mark_x + mark * 0.88, mark_y + mark * 0.18);
                    cairo_stroke(cr);
                }
            } else {
                // Pango leaves the current point at the end of the previous
                // row. cairo_arc joins to that point, which draws a diagonal
                // from the row above into this radio circle.
                const double radius = mark / 2.0;
                const double cx = mark_x + radius;
                const double cy = mark_y + radius;
                cairo_new_sub_path(cr);
                cairo_arc(cr, cx, cy, std::max(3.0, radius), 0, 6.3);
                cairo_stroke(cr);
                if (on) {
                    cairo_new_sub_path(cr);
                    cairo_arc(cr, cx, cy, std::max(1.5, radius * 0.5), 0, 6.3);
                    cairo_fill(cr);
                }
            }
        }
        const double text_y = y + pitch.text_dy;
        const double text_x = (deck_ || touch_used_) ? drop_x + 12.0 + pitch.mark : drop_x + 28.0;
        paint_mnemonic(cr, menu_font(), item.label, text_x, text_y, menu_.alt_shown, ink_text.r, ink_text.g,
                       ink_text.b);
        if (item.kind == ItemKind::Submenu) {
            paint_text(cr, menu_font(), ">", drop_x + drop_w - dp(28), text_y, ink_text.r, ink_text.g,
                       ink_text.b, -1);
        }
        if (item.action == Action::Recent) {
            recent_row_y = y;
            have_recent_row = true;
        }
        std::string accel_shown = item.accel_text != nullptr ? item.accel_text : "";
        const int bound_act = key_act_for_menu(item.action);
        if (bound_act >= 0) {
            accel_shown = chord_label(binds_.key[bound_act]);
        }
        if (!accel_shown.empty() && !gamepad_nav_) {
            const TextMetrics accel = measure_text(cr, menu_font(), accel_shown, -1);
            const double accel_pad = menu_scrolls ? 22.0 : 12.0;
            paint_text(cr, menu_font(), accel_shown, drop_x + drop_w - accel.width - accel_pad, y + pitch.text_dy,
                       ink_accel.r, ink_accel.g, ink_accel.b, -1);
        }
    }
    if (menu_scrolls) {
        const double track_x = drop_x + drop_w - 12;
        const double thumb_h = std::max(16.0, drop_h * (drop_h / content_h));
        const double max_scroll = std::max(1.0, content_h - avail);
        const double thumb_y = menu_h_ + (drop_h - thumb_h) * (scroll / max_scroll);
        fill(cr, track_x, menu_h_ + 2, 8, drop_h - 4, ink_track.r, ink_track.g, ink_track.b);
        fill(cr, track_x, thumb_y, 8, thumb_h, theme::chrome_select_r(), theme::chrome_select_g(),
             theme::chrome_select_b());
        push_hot(track_x - 4, menu_h_, 16, drop_h, kMenuScroll);
    }
    cairo_restore(cr);
    if (!menu_.recent_open) {
        return;
    }
    const int rows = std::max(1, static_cast<int>(recent_.size()));
    double label_w = 0;
    auto consider = [&](const std::string &text) {
        label_w = std::max(label_w, static_cast<double>(measure_text(cr, menu_font(), text, -1).width));
    };
    if (recent_.empty()) {
        consider("No recent cities");
    }
    for (const std::string &shown : recent_) {
        const auto slash = shown.find_last_of("/\\");
        consider(slash == std::string::npos ? shown : shown.substr(slash + 1));
    }
    const DialogRect sub = place_submenu(drop_x, drop_w, have_recent_row ? recent_row_y : menu_h_ + 4, row_h, rows,
                                         label_w + dp(28), win_w_, win_h_, dp(180));
    cairo_set_source_rgba(cr, 0, 0, 0, 0.18);
    cairo_rectangle(cr, sub.x + 3, sub.y + 3, sub.w, sub.h);
    cairo_fill(cr);
    fill(cr, sub.x, sub.y, sub.w, sub.h, dark ? ink_bg.r : 1.0, dark ? ink_bg.g : 1.0, dark ? ink_bg.b : 1.0);
    stroke_rect(cr, sub.x, sub.y, sub.w, sub.h, ink_frame.r, ink_frame.g, ink_frame.b);
    const int text_w = static_cast<int>(std::max(40.0, sub.w - dp(20)));
    if (recent_.empty()) {
        paint_text(cr, menu_font(), "No recent cities", sub.x + 10, sub.y + 8, ink_text.r, ink_text.g,
                   ink_text.b, text_w);
        return;
    }
    for (int i = 0; i < static_cast<int>(recent_.size()); ++i) {
        const double ry = sub.y + 4 + i * row_h;
        const int id = kRecentItem + i;
        push_hot(sub.x + 2, ry, std::max(1.0, sub.w - 4), row_h, id);
        if (menu_.recent_cursor == i || hover_id_ == id) {
            fill(cr, sub.x + 3, ry, std::max(1.0, sub.w - 6), row_h, ink_hover.r, ink_hover.g, ink_hover.b);
        }
        const std::string shown = recent_[static_cast<std::size_t>(i)];
        const auto slash = shown.find_last_of("/\\");
        paint_text(cr, menu_font(), slash == std::string::npos ? shown : shown.substr(slash + 1), sub.x + 10, ry + 7,
                   ink_text.r, ink_text.g, ink_text.b, text_w);
    }
}

void paint_dim_plate(cairo_t *cr, double x, double y, double w, double h, double red, double green, double blue)
{
    if (w < 1.0 || h < 1.0) {
        return;
    }
    static cairo_surface_t *mask = nullptr;
    if (mask == nullptr) {
        mask = cairo_image_surface_create(CAIRO_FORMAT_A8, 4, 4);
        unsigned char *pixels = cairo_image_surface_get_data(mask);
        const int stride = cairo_image_surface_get_stride(mask);
        static const int bayer[4][4] = {
            {0, 8, 2, 10},
            {12, 4, 14, 6},
            {3, 11, 1, 9},
            {15, 7, 13, 5},
        };
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 4; ++col) {
                pixels[row * stride + col] = bayer[row][col] >= 8 ? 210 : 0;
            }
        }
        cairo_surface_mark_dirty(mask);
    }
    cairo_save(cr);
    cairo_rectangle(cr, x, y, w, h);
    cairo_clip(cr);
    cairo_set_source_rgb(cr, red, green, blue);
    cairo_pattern_t *pattern = cairo_pattern_create_for_surface(mask);
    cairo_pattern_set_extend(pattern, CAIRO_EXTEND_REPEAT);
    cairo_pattern_set_filter(pattern, CAIRO_FILTER_NEAREST);
    cairo_mask(cr, pattern);
    cairo_pattern_destroy(pattern);
    cairo_restore(cr);
}

void Shell::draw_dialog(cairo_t *cr)
{
    if (dialog_ == Dialog::None) {
        return;
    }
    // The welcome plate is the whole window. It is painted before the
    // two-frame settle so the city art itself does not jump.
    const bool hold_title = welcome_gate_ &&
                            (dialog_ == Dialog::NewCity || dialog_ == Dialog::Scenario ||
                             (dialog_ == Dialog::Files && !files_save_));
    if (dialog_ == Dialog::Welcome || hold_title) {
        draw_welcome_background(cr);
    }
    if (hold_title) {
        paint_dim_plate(cr, 0, 0, win_w_, win_h_, 0.02, 0.03, 0.08);
    }
    struct SettleShift {
        Shell *shell;
        cairo_t *cr;
        SettleShift(Shell *self, cairo_t *context) : shell(self), cr(context)
        {
            shell->dialog_hot_dy_ = shell->panel_settle_ > 0 ? shell->panel_settle_ * 2.0 : 0.0;
            cairo_save(cr);
            cairo_translate(cr, 0, shell->dialog_hot_dy_);
        }
        ~SettleShift()
        {
            cairo_restore(cr);
            shell->dialog_hot_dy_ = 0;
        }
    } settle_shift(this, cr);
    const double margin = dp(16);
    text_spans_.clear();
    // Dialogs stay above the status line. Centering them in the full window
    // let a tall Load City panel cover "Funds" and the city name.
    const double band_h = std::max(1.0, static_cast<double>(win_h_ - status_h_));
    const double large_w = std::min(static_cast<double>(dp(860)), std::max(1.0, win_w_ - 2.0 * margin));
    const double large_h = std::min(static_cast<double>(dp(640)), std::max(1.0, band_h - 2.0 * margin));
    auto buttons_at = [&](double px, double pw, double by, const std::vector<std::pair<const char *, int>> &buttons,
                          int left_count = 0, int primary_id = -1) {
        const double gap = dp(8);
        const double bh = static_cast<double>(control_px(36, 44));
        const int count = static_cast<int>(buttons.size());
        left_count = std::max(0, std::min(left_count, count));
        std::vector<double> widths(static_cast<std::size_t>(count), dp(120));
        for (int i = 0; i < count; ++i) {
            const TextMetrics metrics = measure_text(cr, ui_font(), strip_mnemonic(buttons[static_cast<std::size_t>(i)].first), -1);
            widths[static_cast<std::size_t>(i)] = button_width(metrics.width, dp(32), dp(120));
        }
        auto control_enabled = [&](int id) {
            if (dialog_ != Dialog::Files) {
                return true;
            }
            if (id == kRename || id == kDelete) {
                return file_row_selected();
            }
            if (!files_save_ && id == kOk) {
                return !files_.empty() && list_index_ >= 0 && list_index_ < static_cast<int>(files_.size());
            }
            return true;
        };
        double left = px + margin;
        for (int i = 0; i < left_count; ++i) {
            const auto &button_def = buttons[static_cast<std::size_t>(i)];
            button(cr, left, by, widths[static_cast<std::size_t>(i)], bh, button_def.first, button_def.second,
                   button_def.second == primary_id, false, control_enabled(button_def.second));
            left += widths[static_cast<std::size_t>(i)] + gap;
        }
        double right_w = 0;
        for (int i = left_count; i < count; ++i) {
            right_w += widths[static_cast<std::size_t>(i)];
            if (i + 1 < count) {
                right_w += gap;
            }
        }
        double right = px + pw - margin - right_w;
        if (right < left) {
            right = left;
        }
        for (int i = left_count; i < count; ++i) {
            const auto &button_def = buttons[static_cast<std::size_t>(i)];
            button(cr, right, by, widths[static_cast<std::size_t>(i)], bh, button_def.first, button_def.second,
                   button_def.second == primary_id, false, control_enabled(button_def.second));
            right += widths[static_cast<std::size_t>(i)] + gap;
        }
    };
    auto text_block_height = [&](const std::string &text, double width) {
        return static_cast<double>(std::max(dp(18), measure_text(cr, body_font(), text, static_cast<int>(width)).height));
    };
    // Pango's wrapped height, not a fixed line grid. Callers stack with a gap
    // so a second line never lands on the next heading or on a slider.
    auto line_height = [&](const char *font, const std::string &text, double width) {
        const int wrap = static_cast<int>(std::max(1.0, width));
        const std::string shown = text.empty() ? std::string(" ") : text;
        return static_cast<double>(std::max(1, measure_text(cr, font, shown, wrap).height));
    };
    auto place_line = [&](const char *font, const std::string &text, double x, double y, double width, double r, double g,
                          double b) {
        const double h = line_height(font, text, width);
        const int wrap = static_cast<int>(std::max(1.0, width));
        paint_text(cr, font, text, x, y, r, g, b, wrap);
        text_spans_.push_back(TextSpan{x, y, std::max(1.0, width), h});
        return h;
    };
    // "Label: $1,200" keeps the words on the left and the figure on the right.
    auto place_split = [&](const std::string &text, double x, double y, double width) {
        const auto colon = text.rfind(": ");
        if (colon == std::string::npos || colon + 2 >= text.size()) {
            return place_line(ui_font(), text, x, y, width, theme::kTextR, theme::kTextG, theme::kTextB);
        }
        const std::string label_text = text.substr(0, colon);
        const std::string value = text.substr(colon + 2);
        const double value_w = static_cast<double>(measure_text(cr, bold_font(), value, -1).width);
        const double label_w = std::max(1.0, width - value_w - dp(8));
        const double label_h = place_line(ui_font(), label_text, x, y, label_w, theme::kTextR, theme::kTextG, theme::kTextB);
        const double value_h = place_line(bold_font(), value, x + width - value_w, y, std::max(1.0, value_w), theme::kTextR,
                                          theme::kTextG, theme::kTextB);
        return std::max(label_h, value_h);
    };
    auto paint_caret = [&](double tx, double ty, double th, const std::string &text, bool focused, double box_x,
                           double box_y, double box_w, double box_h) {
        if (!focused) {
            return;
        }
        field_clamp(text, field_caret_, field_anchor_);
        const int from = std::min(field_caret_, field_anchor_);
        const int to = std::max(field_caret_, field_anchor_);
        if (from != to) {
            const int x0 = measure_text(cr, body_font(), text.substr(0, static_cast<std::size_t>(from)), -1).width;
            const int x1 = measure_text(cr, body_font(), text.substr(0, static_cast<std::size_t>(to)), -1).width;
            const int line_h = measure_text(cr, body_font(), text.empty() ? std::string(" ") : text, -1).height;
            const SelectionBand band = selection_band(ty, std::max(static_cast<double>(line_h), th), box_y, box_h);
            cairo_save(cr);
            if (box_w > 4.0 && box_h > 4.0) {
                cairo_rectangle(cr, box_x + 2.0, box_y + 2.0, box_w - 4.0, box_h - 4.0);
                cairo_clip(cr);
            }
            cairo_set_source_rgba(cr, theme::chrome_select_r(), theme::chrome_select_g(), theme::chrome_select_b(), 0.55);
            cairo_rectangle(cr, tx + x0, band.top, std::max(2, x1 - x0), band.height);
            cairo_fill(cr);
            cairo_restore(cr);
        }
        const int cx = measure_text(cr, body_font(), text.substr(0, static_cast<std::size_t>(field_caret_)), -1).width;
        const bool blink_on = (static_cast<int>(SDL_GetTicks() / 500) % 2) == 0;
        if (!blink_on && from == to) {
            return;
        }
        cairo_set_source_rgb(cr, theme::kTextR, theme::kTextG, theme::kTextB);
        cairo_set_line_width(cr, 2);
        cairo_move_to(cr, tx + cx + 0.5, ty - dp(2));
        cairo_line_to(cr, tx + cx + 0.5, ty + th);
        cairo_stroke(cr);
    };
    if (dialog_ == Dialog::Welcome) {
        const std::string continued = welcome_continue_label();
        refresh_continue_card();
        const bool dark = slate_ || handheld_;
        const double bh = dp(34);
        const double gap = dp(6);
        // The last city gets a card with its map, date, people and funds
        // above the four plain choices.
        const double card_h = continued.empty() ? 0.0 : static_cast<double>(dp(92));
        const int rows = 4;
        const double plaque_h = dp(16) + (card_h > 0 ? card_h + dp(10) : 0.0) + rows * bh + (rows - 1) * gap + dp(14);
        const double plaque_w = std::min(static_cast<double>(card_h > 0 ? dp(480) : dp(420)),
                                         std::max(1.0, win_w_ - static_cast<double>(dp(48))));
        const double px = std::max(0.0, (win_w_ - plaque_w) / 2.0);
        double py = win_h_ - plaque_h - dp(22);
        // The title and the downtown towers live in the upper part of the art.
        // Keep the choices over the neighborhoods along the bottom.
        const double lowest_top = win_h_ * 0.56;
        if (py < lowest_top) {
            py = std::max(win_h_ * 0.40, win_h_ - plaque_h - dp(8));
        }
        const double radius = dp(12);
        auto round_box = [&](double x, double y, double w, double h, double r) {
            const double rr = std::min(r, std::min(w, h) / 2.0);
            cairo_new_path(cr);
            cairo_arc(cr, x + w - rr, y + rr, rr, -1.5708, 0);
            cairo_arc(cr, x + w - rr, y + h - rr, rr, 0, 1.5708);
            cairo_arc(cr, x + rr, y + h - rr, rr, 1.5708, 3.1416);
            cairo_arc(cr, x + rr, y + rr, rr, 3.1416, 4.7124);
            cairo_close_path(cr);
        };
        round_box(px + dp(3), py + dp(4), plaque_w, plaque_h, radius);
        cairo_set_source_rgba(cr, 0.05, 0.08, 0.16, 0.40);
        cairo_fill(cr);
        round_box(px, py, plaque_w, plaque_h, radius);
        if (dark) {
            cairo_set_source_rgba(cr, 0x1B / 255.0, 0x22 / 255.0, 0x2C / 255.0, 0.92);
            cairo_fill_preserve(cr);
            cairo_set_source_rgba(cr, 0xC9 / 255.0, 0xA6 / 255.0, 0x46 / 255.0, 0.85);
        } else {
            cairo_set_source_rgba(cr, 0.99, 0.96, 0.88, 0.90);
            cairo_fill_preserve(cr);
            cairo_set_source_rgba(cr, 0.45, 0.32, 0.10, 0.95);
        }
        cairo_set_line_width(cr, 2.0);
        cairo_stroke(cr);
        const double bw = plaque_w - dp(28);
        const double bx = px + dp(14);
        double by = py + dp(12);
        if (card_h > 0) {
            push_hot(bx, by, bw, card_h, kWelcomeContinue);
            push_focus(kWelcomeContinue);
            const bool focused = focus_id_ == kWelcomeContinue;
            const bool lit = focused || hover_id_ == kWelcomeContinue;
            round_box(bx, by, bw, card_h, dp(8));
            cairo_pattern_t *shade = cairo_pattern_create_linear(bx, by, bx, by + card_h);
            const double lift = lit ? 0.05 : 0.0;
            cairo_pattern_add_color_stop_rgb(shade, 0, std::min(1.0, 0.89 + lift), std::min(1.0, 0.76 + lift), 0.40 + lift);
            cairo_pattern_add_color_stop_rgb(shade, 1, 0.80 + lift, 0.64 + lift, 0.27 + lift);
            cairo_set_source(cr, shade);
            cairo_fill_preserve(cr);
            cairo_pattern_destroy(shade);
            if (focused) {
                if (dark) {
                    cairo_set_source_rgb(cr, 0xEE / 255.0, 0xE8 / 255.0, 0xDA / 255.0);
                } else {
                    cairo_set_source_rgb(cr, 0.10, 0.35, 0.78);
                }
                cairo_set_line_width(cr, 3.0);
            } else {
                cairo_set_source_rgb(cr, 0.45, 0.32, 0.10);
                cairo_set_line_width(cr, 1.5);
            }
            cairo_stroke(cr);
            const double pad = dp(10);
            const double thumb_h = card_h - pad * 2;
            const double thumb_w = std::floor(thumb_h * 1.2);
            double text_x = bx + pad;
            if (continue_thumb_.size() == static_cast<std::size_t>(120 * 100)) {
                cairo_surface_t *thumb = cairo_image_surface_create_for_data(
                    reinterpret_cast<unsigned char *>(continue_thumb_.data()), CAIRO_FORMAT_RGB24, 120, 100, 120 * 4);
                cairo_save(cr);
                round_box(bx + pad, by + pad, thumb_w, thumb_h, dp(4));
                cairo_clip(cr);
                cairo_translate(cr, bx + pad, by + pad);
                cairo_scale(cr, thumb_w / 120.0, thumb_h / 100.0);
                cairo_set_source_surface(cr, thumb, 0, 0);
                cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
                cairo_paint(cr);
                cairo_restore(cr);
                cairo_surface_destroy(thumb);
                round_box(bx + pad, by + pad, thumb_w, thumb_h, dp(4));
                cairo_set_source_rgba(cr, 0.25, 0.17, 0.05, 0.8);
                cairo_set_line_width(cr, 1.0);
                cairo_stroke(cr);
                text_x += thumb_w + pad * 1.4;
            }
            const int text_w = std::max(1, static_cast<int>(bx + bw - pad - text_x));
            const double ink_r = 0.16;
            const double ink_g = 0.11;
            const double ink_b = 0.03;
            const TextMetrics tag_m = measure_text(cr, tag_font(), "CONTINUE", -1);
            double ty = by + pad - dp(1);
            paint_text(cr, tag_font(), "CONTINUE", text_x, ty, 0.42, 0.29, 0.06, -1);
            ty += tag_m.height;
            std::string name = continue_name_.empty() ? strip_mnemonic(continued.c_str()) : continue_name_;
            const TextMetrics name_m = measure_text(cr, title_font(), name, -1);
            if (name_m.width > text_w) {
                while (name.size() > 1 && measure_text(cr, title_font(), name + "\u2026", -1).width > text_w) {
                    name.pop_back();
                    while (!name.empty() && (static_cast<unsigned char>(name.back()) & 0xC0) == 0x80) {
                        name.pop_back();
                    }
                }
                name += "\u2026";
            }
            paint_text(cr, title_font(), name, text_x, ty, ink_r, ink_g, ink_b, -1);
            ty += measure_text(cr, title_font(), name, -1).height + dp(2);
            std::string detail = continue_ok_ ? continue_detail_ : std::string("Saved city");
            if (measure_text(cr, ui_font(), detail, -1).width > text_w) {
                while (detail.size() > 1 && measure_text(cr, ui_font(), detail + "\u2026", -1).width > text_w) {
                    detail.pop_back();
                    while (!detail.empty() && (static_cast<unsigned char>(detail.back()) & 0xC0) == 0x80) {
                        detail.pop_back();
                    }
                }
                detail += "\u2026";
            }
            paint_text(cr, ui_font(), detail, text_x, ty, 0.30, 0.21, 0.06, -1);
            by += card_h + dp(10);
        }
        if (dark) {
            const struct {
                const char *label;
                int id;
            } rows_list[] = {{"_Start a new city", kWelcomeNew},
                             {"_Load a saved city", kWelcomeLoad},
                             {"Select a _scenario", kWelcomeScenario},
                             {"_Quit", kWelcomeQuit}};
            for (const auto &row : rows_list) {
                push_hot(bx, by, bw, bh, row.id);
                push_focus(row.id);
                const bool focused = focus_id_ == row.id;
                const bool lit = focused || hover_id_ == row.id;
                round_box(bx, by, bw, bh, dp(6));
                if (lit) {
                    cairo_set_source_rgb(cr, 0x3A / 255.0, 0x4B / 255.0, 0x63 / 255.0);
                } else {
                    cairo_set_source_rgb(cr, 0x26 / 255.0, 0x30 / 255.0, 0x3D / 255.0);
                }
                cairo_fill_preserve(cr);
                if (focused) {
                    cairo_set_source_rgb(cr, 0xC9 / 255.0, 0xA6 / 255.0, 0x46 / 255.0);
                    cairo_set_line_width(cr, 2.5);
                } else {
                    cairo_set_source_rgb(cr, 0x3A / 255.0, 0x45 / 255.0, 0x55 / 255.0);
                    cairo_set_line_width(cr, 1.0);
                }
                cairo_stroke(cr);
                const TextMetrics m = measure_text(cr, ui_font(), strip_mnemonic(row.label), -1);
                paint_mnemonic(cr, ui_font(), row.label, bx + (bw - m.width) / 2.0, by + (bh - m.height) / 2.0,
                               menu_.alt_shown, 0xEE / 255.0, 0xE8 / 255.0, 0xDA / 255.0);
                by += bh + gap;
            }
            return;
        }
        button(cr, bx, by, bw, bh, "_Start a new city", kWelcomeNew, false);
        by += bh + gap;
        button(cr, bx, by, bw, bh, "_Load a saved city", kWelcomeLoad, false);
        by += bh + gap;
        button(cr, bx, by, bw, bh, "Select a _scenario", kWelcomeScenario, false);
        by += bh + gap;
        button(cr, bx, by, bw, bh, "_Quit", kWelcomeQuit, false);
        return;
    }
    if (dialog_ == Dialog::QuitConfirm) {
        const std::string question = "Quit Lunduke City 2?";
        const double width = message_width(win_w_, margin, dp(420), dp(520));
        const double column = std::max(1.0, width - 2.0 * margin);
        const double body_h = text_block_height(question, column);
        const double height = dp(40) + margin + body_h + dp(12) + dp(36) + margin;
        const DialogRect panel = place_dialog(win_w_, band_h, width, height, margin);
        frame_panel(cr, panel.x, panel.y, panel.w, panel.h, "Quit");
        place_line(body_font(), question, panel.x + margin, panel.y + dp(40) + dp(8), panel.w - 2 * margin, theme::kTextR,
                   theme::kTextG, theme::kTextB);
        buttons_at(panel.x, panel.w, panel.y + panel.h - margin - dp(36),
                   {{"_Cancel", kCancel}, {"_Quit", kOk}}, 0, focus_id_);
        return;
    }
    if (dialog_ == Dialog::Unsaved) {
        SavePromptKind prompt = SavePromptKind::Continue;
        switch (after_save_) {
        case AfterSave::Quit:
            prompt = SavePromptKind::Quit;
            break;
        case AfterSave::NewCity:
            prompt = SavePromptKind::NewCity;
            break;
        case AfterSave::Scenario:
            prompt = SavePromptKind::Scenario;
            break;
        case AfterSave::Load:
        case AfterSave::OpenPath:
        case AfterSave::FilesLoad:
            prompt = SavePromptKind::Open;
            break;
        case AfterSave::Recover:
            prompt = SavePromptKind::Recover;
            break;
        case AfterSave::Nothing:
            break;
        }
        const std::string question = save_changes_question(prompt, session_.city_name());
        const int natural = measure_text(cr, body_font(), question, -1).width;
        // Wide enough for the usual "…another city?" sentence on one line.
        // A longer name that still overflows breaks near the middle.
        const double fitted = static_cast<double>(natural) + 2.0 * margin + static_cast<double>(dp(8));
        const double width = message_width(win_w_, margin, dp(420), std::max(static_cast<double>(dp(560)), fitted));
        const int column = static_cast<int>(std::max(1.0, width - 2.0 * margin));
        const std::string shown = natural > column ? balance_save_question(question) : question;
        const double body_h = text_block_height(shown, column);
        const double height = dp(40) + margin + body_h + dp(12) + dp(36) + margin;
        const DialogRect panel = place_dialog(win_w_, band_h, width, height, margin);
        frame_panel(cr, panel.x, panel.y, panel.w, panel.h, "Save changes?");
        place_line(body_font(), shown, panel.x + margin, panel.y + dp(40) + dp(8), panel.w - 2 * margin, theme::kTextR,
                   theme::kTextG, theme::kTextB);
        buttons_at(panel.x, panel.w, panel.y + panel.h - margin - dp(36),
                   {{"_Discard", kDiscard}, {"_Cancel", kCancel}, {"_Save", kSave}}, 1, kSave);
        return;
    }
    if (dialog_ == Dialog::Disaster) {
        const char *title = "Disaster";
        const char *question = "Start this disaster?";
        const char *detail = "This cannot be undone.";
        if (disaster_ == Action::Fire) {
            title = "Disaster: Fire";
            question = "Start a fire?";
            detail = "A fire will break out somewhere in the city. This cannot be undone.";
        } else if (disaster_ == Action::Flood) {
            title = "Disaster: Flood";
            question = "Start a flood?";
            detail = "Water will flood land near the shore. This cannot be undone.";
        } else if (disaster_ == Action::Tornado) {
            title = "Disaster: Tornado";
            question = "Start a tornado?";
            detail = "A tornado will tear through the city. This cannot be undone.";
        } else if (disaster_ == Action::Earthquake) {
            title = "Disaster: Earthquake";
            question = "Start an earthquake?";
            detail = "An earthquake will damage buildings and start fires. This cannot be undone.";
        } else if (disaster_ == Action::Monster) {
            title = "Disaster: Monster";
            question = "Release a monster?";
            detail = "A monster will walk through the city and destroy what it touches. This cannot be undone.";
        } else if (disaster_ == Action::Meltdown) {
            title = "Disaster: Meltdown";
            question = "Cause a nuclear meltdown?";
            detail = "A nuclear plant will melt down. This cannot be undone.";
        }
        const double width = message_width(win_w_, margin, dp(420), dp(520));
        const double body_h = text_block_height(std::string(question) + "\n" + detail, width - 2 * margin);
        const double height = dp(40) + margin + body_h + dp(12) + dp(36) + margin;
        const DialogRect panel = place_dialog(win_w_, band_h, width, height, margin);
        frame_panel(cr, panel.x, panel.y, panel.w, panel.h, title);
        cairo_set_source_rgb(cr, 0.55, 0.08, 0.06);
        cairo_rectangle(cr, panel.x + margin, panel.y + dp(42), panel.w - 2 * margin, dp(3));
        cairo_fill(cr);
        const double question_h = place_line(bold_font(), question, panel.x + margin, panel.y + dp(52), panel.w - 2 * margin,
                                             theme::kTextR, theme::kTextG, theme::kTextB);
        double ink_r = 0;
        double ink_g = 0;
        double ink_b = 0;
        theme::reading_ink(ink_r, ink_g, ink_b);
        place_line(body_font(), detail, panel.x + margin, panel.y + dp(52) + question_h + dp(6), panel.w - 2 * margin,
                   ink_r, ink_g, ink_b);
        buttons_at(panel.x, panel.w, panel.y + panel.h - margin - dp(36), {{"_Cancel", kCancel}, {"_Start", kOk}}, 0,
                   kOk);
        return;
    }
    if (dialog_ == Dialog::NewCity) {
        const bool osk = osk_visible() && new_page_ == 0;
        const std::string land_intro =
            "Pick a difficulty and the kind of land. Default keeps the usual rivers, lakes, and trees.";
        double want_w = new_page_ == 1 ? dp(640) : dp(520);
        double want_h = new_page_ == 1 ? dp(560) : new_page_ == 0 ? (osk ? dp(190) : dp(236)) : dp(170);
        const double land_intro_h = new_page_ == 1 ? text_block_height(land_intro, want_w - 2 * margin) : 0;
        if (new_page_ == 1) {
            want_h += std::max(0.0, land_intro_h - static_cast<double>(dp(22)));
            want_h += dp(28);
        }
        if (osk) {
            // Ten keys across need about 48px each, so the page grows past the
            // name-only size while the on-screen keyboard is up.
            want_w = std::max(want_w, static_cast<double>(dp(600)));
            want_h += dp(280);
        }
        if (!dialog_error_.empty()) {
            want_h += dp(22);
        }
        const DialogRect panel = place_dialog(win_w_, band_h, want_w, want_h, margin);
        const double x = panel.x;
        const double y = panel.y;
        const double dw = panel.w;
        const double dh = panel.h;
        frame_panel(cr, x, y, dw, dh, "New City");
        if (new_page_ == 0) {
            label(cr, body_font(), "Name the city.", x + margin, y + dp(48), static_cast<int>(dw - 2 * margin));
            const double field_y = y + dp(76);
            fill(cr, x + margin, field_y, dw - 2 * margin, dp(36), 1, 1, 1);
            stroke_rect(cr, x + margin, field_y, dw - 2 * margin, dp(36), theme::kMenuFrameR, theme::kMenuFrameG,
                        theme::kMenuFrameB);
            push_hot(x + margin, field_y, dw - 2 * margin, dp(36), kFieldName);
            paint_text(cr, body_font(), field_a_.empty() ? " " : field_a_, x + margin + dp(8), field_y + dp(8),
                       theme::kTextR, theme::kTextG, theme::kTextB, static_cast<int>(dw - 2 * margin - dp(16)));
            const bool name_editing = focus_id_ == kFieldName || (focus_id_ >= kOsk && focus_id_ < kOsk + 64);
            paint_caret(x + margin + dp(8), field_y + dp(8), dp(18), field_a_, name_editing, x + margin, field_y,
                        dw - 2 * margin, dp(36));
            double below = field_y + dp(36);
            if (!osk) {
                button(cr, x + margin, below + dp(8), dp(140), dp(32), "_Random", kRandomName, false);
                below += dp(40);
            }
            push_focus(kFieldName);
            if (!dialog_error_.empty()) {
                paint_text(cr, ui_font(), dialog_error_, x + margin, below + dp(4), 0.6, 0.1, 0.1,
                           static_cast<int>(dw - 2 * margin));
                below += dp(22);
            }
            if (osk) {
                const double osk_top = below + dp(8);
                const double osk_limit = y + dh - margin - dp(36) - dp(8);
                draw_osk(cr, x + margin, osk_top, dw - 2 * margin, osk_limit - osk_top);
            }
            buttons_at(x, dw, y + dh - margin - dp(36), {{"_Cancel", kCancel}, {"_Next", kNext}});
        } else if (new_page_ == 1) {
            label(cr, ui_font(), land_intro, x + margin, y + dp(46), static_cast<int>(dw - 2 * margin));
            const double block_top = y + dp(46) + land_intro_h + dp(10);
            const char *groups[] = {"Difficulty", "Island", "Rivers", "Lakes", "Trees"};
            const char *options[][3] = {{"Easy", "Medium", "Hard"},
                                        {"Default", "None", "Island"},
                                        {"Default", "None", "Curvy"},
                                        {"Default", "None", "Many"},
                                        {"Default", "None", "Wooded"}};
            int selected[] = {new_level_, new_island_ == CitySession::kTerrainOff ? 1 : new_island_ == CitySession::kIslandAlways ? 2 : 0,
                              new_rivers_ == CitySession::kTerrainOff ? 1 : new_rivers_ == CitySession::kRiversCurvy ? 2 : 0,
                              new_lakes_ == CitySession::kTerrainOff ? 1 : new_lakes_ == CitySession::kLakesMany ? 2 : 0,
                              new_trees_ == CitySession::kTerrainOff ? 1 : new_trees_ == CitySession::kTreesWooded ? 2 : 0};
            const double label_w = dp(108);
            const double choice_w = std::max(static_cast<double>(dp(96)), (dw - 2 * margin - label_w - dp(16)) / 3.0 - dp(6));
            for (int g = 0; g < 5; ++g) {
                const double gy = block_top + g * dp(40);
                paint_text(cr, ui_font(), groups[g], x + margin, gy + dp(8), theme::kTextR, theme::kTextG, theme::kTextB,
                           static_cast<int>(label_w));
                for (int o = 0; o < 3; ++o) {
                    button(cr, x + margin + label_w + o * (choice_w + dp(6)), gy, choice_w, dp(32), options[g][o],
                           kChoice + g * 10 + o, false, selected[g] == o);
                }
            }
            const double seed_y = block_top + 5 * dp(40);
            paint_text(cr, ui_font(), "Seed", x + margin, seed_y + dp(8), theme::kTextR, theme::kTextG, theme::kTextB,
                       static_cast<int>(label_w));
            const double seed_x = x + margin + label_w;
            const double seed_w = std::min(static_cast<double>(dp(220)), dw - margin - seed_x + x);
            fill(cr, seed_x, seed_y, seed_w, dp(32), 1, 1, 1);
            stroke_rect(cr, seed_x, seed_y, seed_w, dp(32), theme::kMenuFrameR, theme::kMenuFrameG, theme::kMenuFrameB);
            push_hot(seed_x, seed_y, seed_w, dp(32), kFieldSeed);
            push_focus(kFieldSeed);
            // One line, clipped to the box. A long seed used to wrap onto
            // the error line under the field.
            {
                const double inset = dp(8);
                const double room = std::max(1.0, seed_w - inset * 2);
                cairo_save(cr);
                cairo_rectangle(cr, seed_x + 1, seed_y + 1, std::max(1.0, seed_w - 2), dp(32) - 2);
                cairo_clip(cr);
                const int full = measure_text(cr, body_font(), field_b_, -1).width;
                double shift = 0;
                if (static_cast<double>(full) > room) {
                    field_clamp(field_b_, field_caret_, field_anchor_);
                    const int caret_x =
                        measure_text(cr, body_font(), field_b_.substr(0, static_cast<std::size_t>(field_caret_)), -1).width;
                    shift = static_cast<double>(caret_x) - (room - dp(4));
                    if (shift < 0) {
                        shift = 0;
                    }
                    const double max_shift = static_cast<double>(full) - room;
                    if (shift > max_shift) {
                        shift = max_shift;
                    }
                }
                paint_text(cr, body_font(), field_b_, seed_x + inset - shift, seed_y + dp(6), theme::kTextR, theme::kTextG,
                           theme::kTextB, -1);
                paint_caret(seed_x + inset - shift, seed_y + dp(6), dp(18), field_b_, focus_id_ == kFieldSeed, seed_x,
                            seed_y, seed_w, dp(32));
                cairo_restore(cr);
            }
            const int preview_seed = preview_seed_from_field(field_b_);
            const int preview_key = static_cast<int>(terrain_preview_key(preview_seed, new_island_, new_rivers_, new_lakes_,
                                                                         new_trees_));
            if (terrain_key_ != preview_key) {
                terrain_key_ = preview_key;
                std::vector<unsigned short> tiles;
                if (session_.preview_terrain(preview_seed, new_island_, new_rivers_, new_lakes_, new_trees_, tiles) &&
                    tiles.size() == static_cast<std::size_t>(CitySession::kWorldW * CitySession::kWorldH)) {
                    if (terrain_preview_ != nullptr) {
                        cairo_surface_destroy(terrain_preview_);
                    }
                    terrain_preview_ = cairo_image_surface_create(CAIRO_FORMAT_RGB24, CitySession::kWorldW,
                                                                  CitySession::kWorldH);
                    unsigned char *pixels = cairo_image_surface_get_data(terrain_preview_);
                    const int stride = cairo_image_surface_get_stride(terrain_preview_);
                    for (int ty = 0; ty < CitySession::kWorldH; ++ty) {
                        for (int tx = 0; tx < CitySession::kWorldW; ++tx) {
                            const unsigned short raw = tiles[static_cast<std::size_t>(ty * CitySession::kWorldW + tx)];
                            const int t = raw & LOMASK;
                            int sample = DIRT;
                            if (t >= RIVER && t <= WATER_HIGH) {
                                sample = RIVER;
                            } else if (t >= TREEBASE && t <= WOODS5) {
                                sample = WOODS;
                            } else if (t >= RESBASE && t < COMBASE) {
                                sample = RESBASE;
                            } else if (t >= ROADBASE && t <= LASTRAIL) {
                                sample = ROADBASE;
                            }
                            double sr = 219.0 / 255.0;
                            double sg = 148.0 / 255.0;
                            double sb = 66.0 / 255.0;
                            if (tile_atlas().loaded()) {
                                tile_atlas().average_color(sample, sr, sg, sb);
                            }
                            const unsigned char r = static_cast<unsigned char>(std::lround(std::max(0.0, std::min(1.0, sr)) * 255.0));
                            const unsigned char gch = static_cast<unsigned char>(std::lround(std::max(0.0, std::min(1.0, sg)) * 255.0));
                            const unsigned char b = static_cast<unsigned char>(std::lround(std::max(0.0, std::min(1.0, sb)) * 255.0));
                            auto *pixel = reinterpret_cast<std::uint32_t *>(pixels + ty * stride + tx * 4);
                            *pixel = (static_cast<std::uint32_t>(r) << 16) | (static_cast<std::uint32_t>(gch) << 8) | b;
                        }
                    }
                    cairo_surface_mark_dirty(terrain_preview_);
                }
            }
            double preview_y = seed_y + dp(40);
            if (!dialog_error_.empty()) {
                paint_text(cr, ui_font(), dialog_error_, x + margin, seed_y + dp(34), 0.6, 0.1, 0.1,
                           static_cast<int>(dw - 2 * margin));
                preview_y += dp(22);
            }
            const double buttons_y = y + dh - margin - dp(36);
            const double summary_y = buttons_y - dp(6) - dp(20);
            // The map preview is 120x100. A clamped dialog (1011x601) used to
            // draw it through the Generate line. Scale it into the gap.
            const double preview_room = std::max(0.0, summary_y - dp(8) - preview_y);
            // Grow into the gap under the preview on a tall window (1280x800).
            // A short window still shrinks the 120x100 map so it stays above
            // the Generate line. The New terrain button sits to the right.
            const double beside = std::max(1.0, static_cast<double>(dp(260) - dp(8)));
            const double max_by_width = beside / 120.0;
            const double max_by_height = preview_room / 100.0;
            const double preview_scale = std::min(max_by_width, max_by_height);
            if (terrain_preview_ != nullptr && preview_scale >= 0.35) {
                cairo_save(cr);
                cairo_translate(cr, x + margin, preview_y);
                cairo_scale(cr, preview_scale, preview_scale);
                cairo_set_source_surface(cr, terrain_preview_, 0, 0);
                cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
                cairo_paint(cr);
                cairo_restore(cr);
            }
            button(cr, x + margin + dp(260), preview_y, dp(160), dp(32), "New terrain", kNewTerrain, false);
            std::string summary = "Generate \"" + field_a_ + "\".";
            if (city_chosen_) {
                summary += " This replaces the city on the map.";
            }
            paint_text(cr, ui_font(), summary, x + margin, summary_y, theme::kTextR, theme::kTextG, theme::kTextB,
                       static_cast<int>(dw - 2 * margin));
            buttons_at(x, dw, y + dh - margin - dp(36), {{"_Back", kBack}, {"_Generate", kGenerate}}, 0, kGenerate);
        } else {
            // A saved page index from an older session. Generate is on page 2 now.
            std::string summary = "Generate \"" + field_a_ + "\".";
            if (city_chosen_) {
                summary += "\nThis replaces the city on the map.";
            }
            label(cr, body_font(), summary, x + margin, y + dp(52), static_cast<int>(dw - 2 * margin));
            buttons_at(x, dw, y + dh - margin - dp(36), {{"_Back", kBack}, {"_Generate", kGenerate}}, 0, kGenerate);
        }
        return;
    }
    if (dialog_ == Dialog::Scenario) {
        const int count = CitySession::kScenarioCount;
        const double row_h = dp(76);
        const double header = dp(70);
        const double footer = margin + dp(36) + dp(12);
        const double max_list = std::max(row_h, band_h - 2.0 * margin - header - footer);
        int want_rows = static_cast<int>(max_list / row_h);
        if (want_rows < 1) {
            want_rows = 1;
        }
        if (want_rows > count) {
            want_rows = count;
        }
        const double want_h = header + footer + want_rows * row_h;
        const double want_w = std::min(static_cast<double>(dp(680)), std::max(1.0, win_w_ - 2.0 * margin));
        const DialogRect panel = place_dialog(win_w_, band_h, want_w, want_h, margin);
        const double x = panel.x;
        const double y = panel.y;
        const double dw = panel.w;
        const double dh = panel.h;
        frame_panel(cr, x, y, dw, dh, "Play Scenario");
        const char *scenario_lead = city_chosen_ ? "Choose a scenario. Playing it replaces the city on the map."
                                                 : "Choose a scenario.";
        label(cr, ui_font(), scenario_lead, x + margin, y + dp(46),
              static_cast<int>(dw - 2 * margin));
        const double list_top = y + header;
        const double buttons_y = y + dh - margin - dp(36);
        const double detail_h = dp(52);
        const double list_room = std::max(row_h, buttons_y - dp(8) - detail_h - list_top);
        int visible = static_cast<int>(list_room / row_h);
        if (visible < 1) {
            visible = 1;
        }
        if (visible > count) {
            visible = count;
        }
        files_visible_ = visible;
        if (scenario_index_ < 0) {
            scenario_index_ = 0;
        }
        if (scenario_index_ >= count) {
            scenario_index_ = count - 1;
        }
        list_index_ = scenario_index_;
        place_list_window(scenario_index_, count);
        align_list_focus();
        const double track_w = dp(12);
        const double row_w = dw - margin - track_w - dp(16);
        const int preview_w = dp(72);
        const int preview_h = std::max(1, preview_w * CitySession::kWorldH / CitySession::kWorldW);
        for (int row = 0; row < visible; ++row) {
            const int i = list_scroll_ + row;
            if (i < 0 || i >= count) {
                break;
            }
            const CitySession::ScenarioDef &def = CitySession::scenario_def(i);
            const double ry = list_top + row * row_h;
            const int id = kListItem + row;
            const double row_box_h = std::max(1.0, row_h - dp(4));
            push_hot(x + dp(8), ry, row_w, row_box_h, id);
            push_focus(id);
            if (scenario_index_ == i) {
                fill(cr, x + dp(8), ry, row_w, row_box_h, theme::chrome_hover_r(), theme::chrome_hover_g(), theme::chrome_hover_b());
            }
            if (focus_id_ == id) {
                cairo_set_source_rgb(cr, theme::kFocusR, theme::kFocusG, theme::kFocusB);
                cairo_set_line_width(cr, 3);
                cairo_rectangle(cr, x + dp(8) + 1, ry + 1, row_w - 2, row_box_h - 2);
                cairo_stroke(cr);
            }
            const double tx = x + dp(12);
            const double ty = ry + std::max(0.0, (row_box_h - preview_h) / 2.0);
            cairo_surface_t *preview = scenario_preview(i);
            if (preview != nullptr) {
                cairo_save(cr);
                cairo_rectangle(cr, tx, ty, preview_w, preview_h);
                cairo_clip(cr);
                cairo_translate(cr, tx, ty);
                cairo_scale(cr, static_cast<double>(preview_w) / CitySession::kWorldW,
                            static_cast<double>(preview_h) / CitySession::kWorldH);
                cairo_set_source_surface(cr, preview, 0, 0);
                // The thumbnail is smaller than the 120x100 map, so a smooth
                // downscale reads more clearly than dropping tiles.
                cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
                cairo_paint(cr);
                cairo_restore(cr);
            }
            stroke_rect(cr, tx, ty, preview_w, preview_h, theme::kMenuFrameR, theme::kMenuFrameG, theme::kMenuFrameB);
            const double text_x = tx + preview_w + dp(8);
            const double text_w = std::max(1.0, x + dp(8) + row_w - text_x - dp(6));
            const std::string line = std::string(def.name) + " (" + std::to_string(def.year) + ")";
            paint_text(cr, body_font(), line, text_x, ry + dp(6), theme::kTextR, theme::kTextG, theme::kTextB,
                       static_cast<int>(text_w));
            paint_text(cr, ui_font(), def.summary, text_x, ry + dp(28), theme::kAccelR, theme::kAccelG, theme::kAccelB,
                       static_cast<int>(text_w));
        }
        {
            const CitySession::ScenarioDef &chosen = CitySession::scenario_def(scenario_index_);
            const double dy = buttons_y - detail_h;
            paint_text(cr, ui_font(), chosen.goal, x + margin, dy, theme::kTextR, theme::kTextG, theme::kTextB,
                       static_cast<int>(dw - 2 * margin));
        }
        const double track_x = x + dw - margin - track_w;
        const double track_h = visible * row_h;
        list_track_y_ = list_top;
        list_track_h_ = std::max(1.0, track_h);
        fill(cr, track_x, list_top, track_w, track_h, 0.86, 0.86, 0.86);
        double thumb_y = list_top;
        double thumb_h = track_h;
        list_thumb(list_scroll_, count, visible, list_top, track_h, thumb_y, thumb_h);
        fill(cr, track_x, thumb_y, track_w, thumb_h, theme::chrome_select_r(), theme::chrome_select_g(), theme::chrome_select_b());
        stroke_rect(cr, track_x, list_top, track_w, track_h, theme::kButtonEdgeR, theme::kButtonEdgeG, theme::kButtonEdgeB);
        push_hot(track_x - dp(4), list_top, track_w + dp(8), track_h, kListScroll);
        buttons_at(x, dw, buttons_y, {{"_Cancel", kCancel}, {"_Play", kPlay}}, 0, kPlay);
        return;
    }
    if (dialog_ == Dialog::Rename) {
        const bool osk = osk_visible();
        double want_w = osk ? dp(600) : dp(520);
        double want_h = dp(170);
        if (osk) {
            want_h += dp(280);
        }
        if (!dialog_error_.empty()) {
            want_h += dp(22);
        }
        const DialogRect panel = place_dialog(win_w_, band_h, want_w, want_h, margin);
        const double x = panel.x;
        const double y = panel.y;
        const double dw = panel.w;
        const double rename_h = panel.h;
        frame_panel(cr, x, y, dw, rename_h, "Rename City");
        label(cr, body_font(), "New name for this city:", x + margin, y + dp(48), static_cast<int>(dw - 2 * margin));
        const double field_y = y + dp(76);
        fill(cr, x + margin, field_y, dw - 2 * margin, dp(36), 1, 1, 1);
        stroke_rect(cr, x + margin, field_y, dw - 2 * margin, dp(36), theme::kMenuFrameR, theme::kMenuFrameG,
                    theme::kMenuFrameB);
        push_hot(x + margin, field_y, dw - 2 * margin, dp(36), kFieldName);
        push_focus(kFieldName);
        paint_text(cr, body_font(), field_a_, x + margin + dp(8), field_y + dp(8), theme::kTextR, theme::kTextG,
                   theme::kTextB, static_cast<int>(dw - 2 * margin - dp(16)));
        const bool rename_editing = focus_id_ == kFieldName || (focus_id_ >= kOsk && focus_id_ < kOsk + 64);
        paint_caret(x + margin + dp(8), field_y + dp(8), dp(18), field_a_, rename_editing, x + margin, field_y,
                    dw - 2 * margin, dp(36));
        double below = field_y + dp(36);
        if (!dialog_error_.empty()) {
            paint_text(cr, ui_font(), dialog_error_, x + margin, below + dp(4), 0.6, 0.1, 0.1,
                       static_cast<int>(dw - 2 * margin));
            below += dp(22);
        }
        if (osk) {
            const double osk_top = below + dp(8);
            const double osk_limit = y + rename_h - margin - dp(36) - dp(8);
            draw_osk(cr, x + margin, osk_top, dw - 2 * margin, osk_limit - osk_top);
        }
        buttons_at(x, dw, y + rename_h - margin - dp(36), {{"_Cancel", kCancel}, {"_Rename", kRename}});
        return;
    }
    if (dialog_ == Dialog::FileRename) {
        const bool osk = osk_visible();
        double want_w = osk ? dp(600) : dp(520);
        double want_h = dp(190);
        if (osk) {
            want_h += dp(280);
        }
        if (!dialog_error_.empty()) {
            want_h += dp(28);
        }
        const DialogRect panel = place_dialog(win_w_, band_h, want_w, want_h, margin);
        const double x = panel.x;
        const double y = panel.y;
        const double dw = panel.w;
        frame_panel(cr, x, y, dw, panel.h, "Rename file");
        label(cr, body_font(), "New name for " + file_target_ + ".", x + margin, y + dp(48),
              static_cast<int>(dw - 2 * margin));
        const double field_y = y + dp(84);
        fill(cr, x + margin, field_y, dw - 2 * margin, dp(36), 1, 1, 1);
        stroke_rect(cr, x + margin, field_y, dw - 2 * margin, dp(36), theme::kMenuFrameR, theme::kMenuFrameG,
                    theme::kMenuFrameB);
        push_hot(x + margin, field_y, dw - 2 * margin, dp(36), kFieldName);
        push_focus(kFieldName);
        paint_text(cr, body_font(), field_a_, x + margin + dp(8), field_y + dp(8), theme::kTextR, theme::kTextG,
                   theme::kTextB, static_cast<int>(dw - 2 * margin - dp(16)));
        const bool editing = focus_id_ == kFieldName || (focus_id_ >= kOsk && focus_id_ < kOsk + 64);
        paint_caret(x + margin + dp(8), field_y + dp(8), dp(18), field_a_, editing, x + margin, field_y,
                    dw - 2 * margin, dp(36));
        double below = field_y + dp(36);
        if (!dialog_error_.empty()) {
            paint_text(cr, ui_font(), dialog_error_, x + margin, below + dp(4), 0.6, 0.1, 0.1,
                       static_cast<int>(dw - 2 * margin));
            below += dp(28);
        }
        if (osk) {
            const double osk_top = below + dp(8);
            const double osk_limit = y + panel.h - margin - dp(36) - dp(8);
            draw_osk(cr, x + margin, osk_top, dw - 2 * margin, std::max(0.0, osk_limit - osk_top));
        }
        buttons_at(x, dw, y + panel.h - margin - dp(36), {{"_Cancel", kCancel}, {"_Rename", kRename}}, 0, kRename);
        return;
    }
    if (dialog_ == Dialog::FileDelete) {
        const std::string question = "Delete \"" + file_target_ + "\"? This cannot be undone.";
        const double width = message_width(win_w_, margin, dp(420), dp(560));
        const double body_h = text_block_height(question, width - 2 * margin);
        const double error_h = dialog_error_.empty() ? 0.0 : text_block_height(dialog_error_, width - 2 * margin) + dp(8);
        const double height = dp(40) + margin + body_h + dp(8) + error_h + dp(12) + dp(36) + margin;
        const DialogRect panel = place_dialog(win_w_, band_h, width, height, margin);
        frame_panel(cr, panel.x, panel.y, panel.w, panel.h, "Delete file?");
        label(cr, body_font(), question, panel.x + margin, panel.y + dp(48), static_cast<int>(panel.w - 2 * margin));
        if (!dialog_error_.empty()) {
            paint_text(cr, ui_font(), dialog_error_, panel.x + margin, panel.y + dp(48) + body_h + dp(8), 0.6, 0.1, 0.1,
                       static_cast<int>(panel.w - 2 * margin));
        }
        buttons_at(panel.x, panel.w, panel.y + panel.h - margin - dp(36), {{"_Cancel", kCancel}, {"_Delete", kDelete}},
                   0, kDelete);
        return;
    }
    if (dialog_ == Dialog::Bindings) {
        const double bh = static_cast<double>(control_px(36, 44));
        const double width = std::min(static_cast<double>(dp(720)), std::max(1.0, win_w_ - 2.0 * margin));
        const double height = std::min(static_cast<double>(dp(560)), std::max(1.0, band_h - 2.0 * margin));
        const DialogRect panel = place_dialog(win_w_, band_h, width, height, margin);
        frame_panel(cr, panel.x, panel.y, panel.w, panel.h, "Remap controls");
        const std::string help = "A or Enter listens. Taken bindings swap. Backspace clears.";
        const double help_w = std::max(1.0, panel.w - 2 * margin);
        const double help_h = text_block_height(help, help_w);
        label(cr, ui_font(), help, panel.x + margin, panel.y + dp(46), static_cast<int>(help_w));
        const double header_y = panel.y + dp(46) + help_h + dp(10);
        const double list_top = header_y + dp(22);
        const double buttons_y = panel.y + panel.h - margin - bh;
        const double note_w = std::max(1.0, panel.w - 2 * margin);
        const double error_h = dialog_error_.empty() ? 0.0 : text_block_height(dialog_error_, note_w) + dp(6);
        const double list_bottom = buttons_y - error_h - dp(8);
        const int row_px = control_px(32, 40);
        const int visible = std::max(1, static_cast<int>((list_bottom - list_top) / row_px));
        files_visible_ = visible;
        const int count = bind_row_count();
        if (list_index_ < 0) {
            list_index_ = 0;
        }
        if (list_index_ >= count) {
            list_index_ = count - 1;
        }
        place_list_window(list_index_, count);
        if (focus_id_ >= kListItem && focus_id_ < kListItem + 64) {
            align_list_focus();
        }
        const double row_w = panel.w - 2 * margin - dp(16);
        paint_text(cr, bold_font(), "Action", panel.x + margin + dp(8), header_y, theme::kTextR, theme::kTextG,
                   theme::kTextB, static_cast<int>(row_w * 0.46));
        paint_text(cr, bold_font(), "Key", panel.x + margin + row_w * 0.48, header_y, theme::kTextR, theme::kTextG,
                   theme::kTextB, static_cast<int>(row_w * 0.28));
        paint_text(cr, bold_font(), "Button", panel.x + margin + row_w * 0.78, header_y, theme::kTextR, theme::kTextG,
                   theme::kTextB, static_cast<int>(row_w * 0.2));
        for (int row = 0; row < visible; ++row) {
            const int index = list_scroll_ + row;
            if (index < 0 || index >= count) {
                break;
            }
            const BindRow spec = bind_row(index);
            const double ry = list_top + row * row_px;
            const int id = kListItem + row;
            push_hot(panel.x + margin, ry, row_w, row_px - 4, id);
            push_focus(id);
            if (list_index_ == index) {
                fill(cr, panel.x + margin, ry, row_w, row_px - 4, theme::chrome_hover_r(), theme::chrome_hover_g(),
                     theme::chrome_hover_b());
            }
            if (focus_id_ == id) {
                cairo_set_source_rgb(cr, theme::kFocusR, theme::kFocusG, theme::kFocusB);
                cairo_set_line_width(cr, 2.5);
                cairo_rectangle(cr, panel.x + margin + 1, ry + 1, row_w - 2, row_px - 6);
                cairo_stroke(cr);
            }
            const bool listening = bind_listen_ && list_index_ == index && focus_id_ == id;
            std::string keys = spec.key >= 0 ? chord_label(binds_.key[spec.key]) : std::string();
            std::string pad;
            if (spec.pad >= 0) {
                if (binds_.pad[spec.pad] >= 0) {
                    pad = glyph(static_cast<SDL_GamepadButton>(binds_.pad[spec.pad]));
                }
                if (pad.empty()) {
                    pad = "None";
                }
            }
            if (listening) {
                if (spec.key >= 0 && spec.pad >= 0) {
                    keys = "Press a key or button";
                    pad.clear();
                } else if (spec.key >= 0) {
                    keys = "Press a key";
                } else {
                    pad = "Press a button";
                }
            }
            paint_text(cr, ui_font(), spec.label, panel.x + margin + dp(8), ry + dp(6), theme::kTextR, theme::kTextG,
                       theme::kTextB, static_cast<int>(row_w * 0.46));
            if (!keys.empty()) {
                paint_text(cr, ui_font(), keys, panel.x + margin + row_w * 0.48, ry + dp(6), theme::kTextR, theme::kTextG,
                           theme::kTextB, static_cast<int>(row_w * 0.28));
            }
            if (!pad.empty()) {
                paint_text(cr, ui_font(), pad, panel.x + margin + row_w * 0.78, ry + dp(6), theme::kTextR, theme::kTextG,
                           theme::kTextB, static_cast<int>(row_w * 0.2));
            }
        }
        if (count > visible) {
            const double track_x = panel.x + panel.w - margin - dp(10);
            const double track_h = visible * static_cast<double>(row_px);
            list_track_y_ = list_top;
            list_track_h_ = std::max(1.0, track_h);
            fill(cr, track_x, list_top, dp(10), track_h, 0.86, 0.86, 0.86);
            double thumb_y = list_top;
            double thumb_h = track_h;
            list_thumb(list_scroll_, count, visible, list_top, track_h, thumb_y, thumb_h);
            fill(cr, track_x, thumb_y, dp(10), thumb_h, theme::chrome_select_r(), theme::chrome_select_g(),
                 theme::chrome_select_b());
            push_hot(track_x - dp(4), list_top, dp(18), track_h, kListScroll);
        }
        if (!dialog_error_.empty()) {
            const double ink_r = dialog_note_ok_ ? 0.10 : 0.6;
            const double ink_g = dialog_note_ok_ ? 0.38 : 0.1;
            const double ink_b = dialog_note_ok_ ? 0.16 : 0.1;
            paint_text(cr, ui_font(), dialog_error_, panel.x + margin, buttons_y - error_h, ink_r, ink_g, ink_b,
                       static_cast<int>(panel.w - 2 * margin));
        }
        buttons_at(panel.x, panel.w, buttons_y, {{"_Reset to defaults", kReset}, {"_Cancel", kCancel}, {"_OK", kOk}}, 1,
                   kOk);
        return;
    }
    if (dialog_ == Dialog::About) {
        const double width = message_width(win_w_, margin, dp(480), dp(640));
        const double icon = dp(72);
        const std::string body = about_text();
        const double text_w = std::max(1.0, width - 2 * margin - icon - dp(16));
        const double body_h = line_height(body_font(), body, text_w);
        const double content_h = std::max(static_cast<double>(icon), body_h);
        const double height = dp(40) + margin + content_h + dp(16) + dp(36) + margin;
        const DialogRect panel = place_dialog(win_w_, band_h, width, height, margin);
        frame_panel(cr, panel.x, panel.y, panel.w, panel.h, "About Lunduke City 2");
        const double icon_x = panel.x + margin;
        const double icon_y = panel.y + dp(48);
        static cairo_surface_t *icon_surface = nullptr;
        static bool icon_tried = false;
        if (!icon_tried) {
            icon_tried = true;
            const std::string icon_path = ui_data_file("icons/hicolor/64x64/apps/lunduke-city.png");
            if (!icon_path.empty()) {
                cairo_surface_t *raw = cairo_image_surface_create_from_png(icon_path.c_str());
                if (cairo_surface_status(raw) == CAIRO_STATUS_SUCCESS &&
                    cairo_image_surface_get_format(raw) == CAIRO_FORMAT_ARGB32) {
                    const int iw = cairo_image_surface_get_width(raw);
                    const int ih = cairo_image_surface_get_height(raw);
                    icon_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, iw, ih);
                    const int src_stride = cairo_image_surface_get_stride(raw);
                    const int dst_stride = cairo_image_surface_get_stride(icon_surface);
                    const unsigned char *src = cairo_image_surface_get_data(raw);
                    unsigned char *dst = cairo_image_surface_get_data(icon_surface);
                    for (int y = 0; y < ih; ++y) {
                        for (int x = 0; x < iw; ++x) {
                            const unsigned char *p = src + y * src_stride + x * 4;
                            auto *out = reinterpret_cast<std::uint32_t *>(dst + y * dst_stride + x * 4);
                            if (p[3] < 16) {
                                *out = 0;
                                continue;
                            }
                            *out = snap_vga_rgb(p[2], p[1], p[0]);
                        }
                    }
                    cairo_surface_mark_dirty(icon_surface);
                }
                cairo_surface_destroy(raw);
            }
        }
        if (icon_surface != nullptr) {
            const double iw = cairo_image_surface_get_width(icon_surface);
            const double scale = iw > 1.0 ? icon / iw : 1.0;
            cairo_save(cr);
            cairo_translate(cr, icon_x, icon_y);
            cairo_scale(cr, scale, scale);
            cairo_set_source_surface(cr, icon_surface, 0, 0);
            cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
            cairo_paint(cr);
            cairo_restore(cr);
        } else {
            fill(cr, icon_x, icon_y + dp(36), icon, dp(28), theme::chrome_select_r(), theme::chrome_select_g(),
                 theme::chrome_select_b());
            fill(cr, icon_x + dp(8), icon_y + dp(16), dp(18), dp(48), 0.93, 0.93, 0.90);
            fill(cr, icon_x + dp(30), icon_y + dp(4), dp(14), dp(60), 0.55, 0.62, 0.70);
            fill(cr, icon_x + dp(48), icon_y + dp(22), dp(16), dp(42), 0.75, 0.55, 0.28);
        }
        const double body_w = std::max(1.0, panel.w - 2 * margin - icon - dp(16));
        place_line(body_font(), body, icon_x + icon + dp(12), panel.y + dp(48), body_w, theme::kTextR, theme::kTextG,
                   theme::kTextB);
        buttons_at(panel.x, panel.w, panel.y + panel.h - margin - dp(36), {{"_Close", kClose}});
        return;
    }
    if (dialog_ == Dialog::HowToPlay) {
        // A heading in the title colour over each topic, the same rule the
        // Controls window uses, so the two help windows read as a pair.
        const std::vector<HowToPlaySection> sections = how_to_play_sections();
        const double width = message_width(win_w_, margin, dp(460), dp(880));
        const double inner = width - 2 * margin;
        const double head_gap = dp(4);
        const double section_gap = dp(14);
        // Two columns when the window is wide enough, so it never needs to scroll.
        const bool split = inner >= dp(640);
        const double col_gap = dp(28);
        const double col_w = split ? (inner - col_gap) / 2.0 : inner;
        const std::size_t per_col = split ? (sections.size() + 1) / 2 : sections.size();
        auto lay = [&](double x, double y, bool paint) {
            double tallest = 0;
            for (std::size_t first = 0; first < sections.size(); first += per_col) {
                const double cx = x + (first / per_col) * (col_w + col_gap);
                double cy = y;
                const std::size_t last = std::min(sections.size(), first + per_col);
                for (std::size_t i = first; i < last; ++i) {
                    const double head_h = line_height(bold_font(), sections[i].heading, col_w);
                    const double body_h = line_height(body_font(), sections[i].body, col_w);
                    if (paint) {
                        paint_text(cr, bold_font(), sections[i].heading, cx, cy, 0.45, 0.04, 0.02, static_cast<int>(col_w));
                        paint_text(cr, body_font(), sections[i].body, cx, cy + head_h + head_gap, theme::kTextR,
                                   theme::kTextG, theme::kTextB, static_cast<int>(col_w));
                    }
                    cy += head_h + head_gap + body_h + (i + 1 < last ? section_gap : 0.0);
                }
                tallest = std::max(tallest, cy - y);
            }
            return tallest;
        };
        const double body_h = lay(0, 0, false);
        const double height = dp(40) + margin + body_h + dp(16) + dp(36) + margin;
        const DialogRect panel = place_dialog(win_w_, band_h, width, height, margin);
        frame_panel(cr, panel.x, panel.y, panel.w, panel.h, "How to play");
        lay(panel.x + margin, panel.y + dp(48), true);
        buttons_at(panel.x, panel.w, panel.y + panel.h - margin - dp(36), {{"_Close", kClose}});
        return;
    }
    if (dialog_ == Dialog::Recovery || dialog_ == Dialog::RecoveryDelete) {
        const bool deleting = dialog_ == Dialog::RecoveryDelete;
        std::string body = deleting ? city_recovery::describe(recovery_) + "\nThis cannot be undone."
                                    : "Lunduke City 2 found a city from a previous session that was not saved:\n" +
                                          city_recovery::describe(recovery_);
        if (!deleting && recovery_left_ > 0) {
            body += "\n(" + std::to_string(recovery_left_) + " older one" + (recovery_left_ == 1 ? " is" : "s are") +
                    " kept for later.)";
        }
        const double width = message_width(win_w_, margin, dp(420), dp(600));
        const double body_h = text_block_height(body, width - 2 * margin);
        const double height = dp(40) + margin + body_h + dp(12) + dp(36) + margin;
        const DialogRect panel = place_dialog(win_w_, band_h, width, height, margin);
        frame_panel(cr, panel.x, panel.y, panel.w, panel.h, deleting ? "Delete this unsaved city?" : "Recover unsaved city?");
        label(cr, body_font(), body, panel.x + margin, panel.y + dp(48), static_cast<int>(panel.w - 2 * margin));
        if (!deleting) {
            buttons_at(panel.x, panel.w, panel.y + panel.h - margin - dp(36),
                       {{"_Discard…", kDelete}, {"_Not Now", kNotNow}, {"_Recover", kRecover}}, 1);
        } else {
            buttons_at(panel.x, panel.w, panel.y + panel.h - margin - dp(36), {{"_Delete", kDelete}, {"_Cancel", kCancel}},
                       1);
        }
        return;
    }
    if (dialog_ == Dialog::Files || dialog_ == Dialog::FileWait) {
        const char *title = files_save_ ? "Save City" : "Load City";
        if (dialog_ == Dialog::FileWait) {
            const char *wait = "Waiting for the system file dialog. If it does not appear, browse in the window.";
            const double width = message_width(win_w_, margin, dp(420), dp(560));
            const double body_h = text_block_height(wait, width - 2 * margin);
            const double height = dp(40) + margin + body_h + dp(12) + dp(36) + margin;
            const DialogRect panel = place_dialog(win_w_, band_h, width, height, margin);
            frame_panel(cr, panel.x, panel.y, panel.w, panel.h, "System file dialog");
            label(cr, body_font(), wait, panel.x + margin, panel.y + dp(48), static_cast<int>(panel.w - 2 * margin));
            buttons_at(panel.x, panel.w, panel.y + panel.h - margin - dp(36),
                       {{"_Cancel", kCancel}, {"Browse in the window", kSystemFiles}});
            return;
        }
        const DialogRect panel = place_dialog(win_w_, band_h, large_w, large_h, margin);
        const double x = panel.x;
        const double y = panel.y;
        const double dw = panel.w;
        const double dh = panel.h;
        frame_panel(cr, x, y, dw, dh, title);
        const TextMetrics path_metrics = measure_text(cr, ui_font(), files_dir_, static_cast<int>(dw - 32));
        label(cr, ui_font(), files_dir_, x + 16, y + dp(46), static_cast<int>(dw - 32));
        const double list_top = y + dp(46) + path_metrics.height + 6;
        const double buttons_top = y + dh - margin - dp(36);
        const double error_h = dialog_error_.empty() ? 0.0 : dp(32);
        const double name_h = files_save_ ? dp(36) : 0;
        const double name_y = buttons_top - error_h - (files_save_ ? dp(8) + name_h : 0);
        const double error_y = buttons_top - error_h + dp(4);
        double osk_h = 0;
        if (osk_visible() && files_save_) {
            const double room = name_y - dp(8) - list_top - dp(8);
            osk_h = std::min(static_cast<double>(dp(200)), std::max(0.0, room - dp(48)));
            // A short box used to force 28px keys and paint them over Up.
            if (osk_key_height(osk_h) < 1) {
                osk_h = 0;
            }
        }
        const double osk_y = name_y - dp(8) - osk_h;
        const double list_bottom = (osk_h > 0 ? osk_y : name_y) - dp(8);
        const double list_h = std::max(static_cast<double>(dp(40)), list_bottom - list_top);
        const int row_px = dp(56);
        const int visible = std::max(1, static_cast<int>(list_h / row_px));
        files_visible_ = visible;
        const int count = static_cast<int>(files_.size());
        if (count == 0) {
            list_index_ = 0;
            list_scroll_ = 0;
        } else {
            if (list_index_ < 0) {
                list_index_ = 0;
            }
            if (list_index_ >= count) {
                list_index_ = count - 1;
            }
            place_list_window(list_index_, count);
            align_list_focus();
        }
        const double row_w = dw - 24 - 16;
        for (int row = 0; row < visible; ++row) {
            const int i = list_scroll_ + row;
            if (i < 0 || i >= count) {
                break;
            }
            const double ry = list_top + row * row_px;
            const int id = kListItem + row;
            const double row_h = row_px - 4;
            push_hot(x + 12, ry, row_w, row_h, id);
            push_focus(id);
            if (list_index_ == i) {
                fill(cr, x + 12, ry, row_w, row_h, theme::chrome_hover_r(), theme::chrome_hover_g(), theme::chrome_hover_b());
            }
            if (focus_id_ == id) {
                cairo_set_source_rgb(cr, theme::kFocusR, theme::kFocusG, theme::kFocusB);
                cairo_set_line_width(cr, 3);
                cairo_rectangle(cr, x + 13, ry + 1, row_w - 2, row_h - 2);
                cairo_stroke(cr);
            }
            const std::string &name = files_[static_cast<std::size_t>(i)];
            double text_x = x + 20;
            if (!file_row_is_dir(name)) {
                std::string full = files_dir_;
                if (!full.empty() && full.back() != '/' && full.back() != '\\') {
                    full.push_back('/');
                }
                full += name;
                const CityGlance glance = city_glance(full);
                const double sub_r = theme::kAccelR;
                const double sub_g = theme::kAccelG;
                const double sub_b = theme::kAccelB;
                if (!glance.ok && !glance.summary.empty()) {
                    paint_text(cr, ui_font(), glance.summary, text_x, ry + 26, sub_r, sub_g, sub_b,
                               static_cast<int>(row_w - (text_x - x)));
                } else if (glance.ok && !glance.swatch.empty()) {
                    const int tw = 48;
                    const int th = 40;
                    cairo_surface_t *swatch = cairo_image_surface_create(CAIRO_FORMAT_RGB24, 120, 100);
                    unsigned char *pixels = cairo_image_surface_get_data(swatch);
                    const int stride = cairo_image_surface_get_stride(swatch);
                    for (int sy = 0; sy < 100; ++sy) {
                        for (int sx = 0; sx < 120; ++sx) {
                            const std::uint32_t rgb = glance.swatch[static_cast<std::size_t>(sy * 120 + sx)];
                            auto *pixel = reinterpret_cast<std::uint32_t *>(pixels + sy * stride + sx * 4);
                            *pixel = rgb;
                        }
                    }
                    cairo_surface_mark_dirty(swatch);
                    cairo_save(cr);
                    cairo_translate(cr, x + 16, ry + 4);
                    cairo_scale(cr, tw / 120.0, th / 100.0);
                    cairo_set_source_surface(cr, swatch, 0, 0);
                    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
                    cairo_paint(cr);
                    cairo_restore(cr);
                    cairo_surface_destroy(swatch);
                    text_x = x + 16 + tw + 8;
                    paint_text(cr, ui_font(), glance.summary, text_x, ry + 26, sub_r, sub_g, sub_b,
                               static_cast<int>(row_w - (text_x - x)));
                }
            }
            paint_text(cr, body_font(), name, text_x, ry + 4, theme::kTextR, theme::kTextG, theme::kTextB,
                       static_cast<int>(std::max(1.0, x + 12 + row_w - text_x - 8)));
        }
        const double track_x = x + dw - 26;
        const double track_y = list_top;
        const double track_h = visible * static_cast<double>(row_px);
        list_track_y_ = track_y;
        list_track_h_ = std::max(1.0, track_h);
        if (count > visible) {
            fill(cr, track_x, track_y, 12, track_h, 0.86, 0.86, 0.86);
            double thumb_y = track_y;
            double thumb_h = track_h;
            list_thumb(list_scroll_, count, visible, track_y, track_h, thumb_y, thumb_h);
            fill(cr, track_x, thumb_y, 12, thumb_h, theme::chrome_select_r(), theme::chrome_select_g(),
                 theme::chrome_select_b());
            stroke_rect(cr, track_x, track_y, 12, track_h, theme::kButtonEdgeR, theme::kButtonEdgeG,
                        theme::kButtonEdgeB);
            push_hot(track_x - 4, track_y, 20, track_h, kListScroll);
        }
        if (files_.empty()) {
            paint_text(cr, body_font(), "No saved cities yet.", x + 20, list_top + 8, theme::kAccelR, theme::kAccelG,
                       theme::kAccelB, static_cast<int>(dw - 56));
        }
        if (files_save_) {
            if (osk_h > 0) {
                draw_osk(cr, x + 16, osk_y, dw - 32, osk_h);
            }
            fill(cr, x + 16, name_y, dw - 32, name_h, 1, 1, 1);
            stroke_rect(cr, x + 16, name_y, dw - 32, name_h, theme::kMenuFrameR, theme::kMenuFrameG, theme::kMenuFrameB);
            push_hot(x + 16, name_y, dw - 32, name_h, kFieldName);
            push_focus(kFieldName);
            paint_text(cr, body_font(), field_a_, x + 24, name_y + dp(8), theme::kTextR, theme::kTextG, theme::kTextB,
                       static_cast<int>(dw - 48));
            const bool save_editing = focus_id_ == kFieldName || (focus_id_ >= kOsk && focus_id_ < kOsk + 64);
            paint_caret(x + 24, name_y + dp(8), dp(18), field_a_, save_editing, x + 16, name_y, dw - 32, name_h);
        }
        if (!dialog_error_.empty()) {
            paint_text(cr, ui_font(), dialog_error_, x + 16, error_y, 0.6, 0.1, 0.1, static_cast<int>(dw - 32));
        }
        bool folder_row = false;
        if (!files_save_ && list_index_ >= 0 && list_index_ < static_cast<int>(files_.size())) {
            folder_row = file_row_is_dir(files_[static_cast<std::size_t>(list_index_)]);
        }
        const char *open_label = files_save_ ? "_Save" : (folder_row ? "Open _Folder" : "_Open");
        const int open_id = files_save_ ? kSave : kOk;
        buttons_at(x, dw, buttons_top,
                   {{"_Up", kUp}, {"_Rename", kRename}, {"_Delete", kDelete}, {"_Cancel", kCancel}, {open_label, open_id}},
                   3, open_id);
        return;
    }
    if (dialog_ == Dialog::LoadError || dialog_ == Dialog::SaveError || dialog_ == Dialog::Info) {
        const double width = message_width(win_w_, margin, dp(420), dp(560));
        const double body_h = text_block_height(info_body_, width - 2 * margin);
        const double height = dp(40) + margin + body_h + dp(12) + dp(36) + margin;
        const DialogRect panel = place_dialog(win_w_, band_h, width, height, margin);
        frame_panel(cr, panel.x, panel.y, panel.w, panel.h, info_title_.c_str());
        label(cr, body_font(), info_body_, panel.x + margin, panel.y + dp(48), static_cast<int>(panel.w - 2 * margin));
        buttons_at(panel.x, panel.w, panel.y + panel.h - margin - dp(36), {{"_OK", kOk}});
        return;
    }
    if (dialog_ == Dialog::Overwrite) {
        const auto slash = queued_path_.find_last_of("/\\");
        const std::string name = slash == std::string::npos ? queued_path_ : queued_path_.substr(slash + 1);
        const std::string question = replace_file_question(name);
        const double width = message_width(win_w_, margin, dp(420), dp(520));
        const double body_h = text_block_height(question, width - 2 * margin);
        const double height = dp(40) + margin + body_h + dp(12) + dp(36) + margin;
        const DialogRect panel = place_dialog(win_w_, band_h, width, height, margin);
        frame_panel(cr, panel.x, panel.y, panel.w, panel.h, "Replace file?");
        label(cr, body_font(), question, panel.x + margin, panel.y + dp(48), static_cast<int>(panel.w - 2 * margin));
        buttons_at(panel.x, panel.w, panel.y + panel.h - margin - dp(36), {{"_Cancel", kCancel}, {"_Replace", kSave}});
        return;
    }
    if (dialog_ == Dialog::Outcome) {
        const double width = message_width(win_w_, margin, dp(420), dp(560));
        const double body_h = text_block_height(info_body_, width - 2 * margin);
        const double height = dp(40) + margin + body_h + dp(12) + dp(36) + margin;
        const DialogRect panel = place_dialog(win_w_, band_h, width, height, margin);
        frame_panel(cr, panel.x, panel.y, panel.w, panel.h, info_title_.c_str());
        label(cr, body_font(), info_body_, panel.x + margin, panel.y + dp(48), static_cast<int>(panel.w - 2 * margin));
        buttons_at(panel.x, panel.w, panel.y + panel.h - margin - dp(36),
                   {{"Stay _paused", kStay}, {"_Scenario list", kScenarioList}, {"_Keep playing", kKeep}});
        return;
    }
    if (dialog_ == Dialog::Settings) {
        const double bh = static_cast<double>(control_px(36, 44));
        const double switch_w = static_cast<double>(dp(148));
        const double text_h = static_cast<double>(std::max(dp(18), measure_text(cr, bold_font(), "Volume", -1).height));
        const double bar_h = static_cast<double>(dp(18));
        const double stack_gap = static_cast<double>(dp(12));
        const SettingsStack volume_row = settings_stack(0, text_h, bar_h, stack_gap);
        const SettingsStack scale_row = settings_stack(volume_row.next_caption, text_h, bar_h, stack_gap);
        // Seven switches in one column when the window is tall enough, else
        // two columns of four so the dialog still fits a 960x600 window.
        const double one_column = dp(48) + margin + scale_row.next_caption + (bh + stack_gap) * 7.0 + dp(8) + bh + margin;
        const bool paired = one_column > win_h_ - 2.0 * margin;
        const double toggles = (bh + stack_gap) * (paired ? 4.0 : 7.0);
        const double rows = scale_row.next_caption + toggles;
        const double height = dp(48) + margin + rows + dp(8) + bh + margin;
        const DialogRect panel = place_dialog(win_w_, band_h, paired ? dp(760) : dp(640), height, margin);
        const double x = panel.x;
        const double y = panel.y;
        const double dw = panel.w;
        frame_panel(cr, x, y, dw, panel.h, "Settings");
        const double switch_x = x + dw - margin - switch_w;
        const double content = y + dp(52);
        auto slider = [&](int index, const std::string &caption, const std::string &value_text, int value, int min,
                          int max, const SettingsStack &row, bool enabled, bool leave_switch) {
            const double sy = content + row.caption;
            const double label_right = leave_switch ? switch_x - x - margin - dp(12) : dw - 2 * margin - dp(72);
            paint_text(cr, bold_font(), caption, x + margin, sy, enabled ? theme::kTextR : theme::kDisabledR,
                       enabled ? theme::kTextG : theme::kDisabledG, enabled ? theme::kTextB : theme::kDisabledB,
                       static_cast<int>(std::max(1.0, label_right)));
            if (enabled && !value_text.empty() && !leave_switch) {
                const TextMetrics value_metrics = measure_text(cr, bold_font(), value_text, -1);
                paint_text(cr, bold_font(), value_text, x + dw - margin - value_metrics.width, sy, theme::kTextR,
                           theme::kTextG, theme::kTextB, -1);
            }
            const double bar_y = content + row.bar;
            const double sx = x + margin;
            const double sw = leave_switch ? std::max(1.0, switch_x - sx - dp(12)) : dw - 2 * margin;
            if (!enabled) {
                fill(cr, sx, bar_y, sw, bar_h, 0.86, 0.84, 0.78);
                stroke_rect(cr, sx, bar_y, sw, bar_h, 0.72, 0.55, 0.18);
                return;
            }
            fill(cr, sx, bar_y, sw, bar_h, 1, 0.98, 0.92);
            const double t = (value - min) / static_cast<double>(std::max(1, max - min));
            fill(cr, sx, bar_y, sw * t, bar_h, 0.20, 0.42, 0.72);
            const double thumb = dp(12);
            const double thumb_y = bar_y - dp(3);
            const double thumb_x = slider_thumb_x(sx, sw, thumb, t);
            fill(cr, thumb_x, thumb_y, thumb, bar_h + dp(6), 0.98, 0.94, 0.84);
            stroke_rect(cr, thumb_x, thumb_y, thumb, bar_h + dp(6), 0.72, 0.55, 0.18);
            stroke_rect(cr, sx, bar_y, sw, bar_h, 0.72, 0.55, 0.18);
            const int id = kSlider + index;
            push_hot(sx, bar_y - dp(4), sw, bar_h + dp(8), id);
            push_focus(id);
            next_sliders_.push_back(SliderGeom{id, sx, bar_y - dp(4), sw, bar_h + dp(8), min, max});
            if (focus_id_ == id) {
                stroke_slider_focus(cr, sx + 1, bar_y - dp(4), sw - 2, bar_h + dp(8));
            }
        };
        auto switch_row = [&](const char *label, const char *state, int id, double sy) {
            paint_text(cr, ui_font(), label, x + margin, sy + dp(8), theme::kTextR, theme::kTextG, theme::kTextB,
                       static_cast<int>(switch_x - x - margin - dp(8)));
            button(cr, switch_x, sy, switch_w, bh, state, id, false);
        };
        // Right-hand column in the paired layout: label at the middle.
        const double pair_w = std::min(switch_w, (dw - 2 * margin) / 2.0 - dp(150));
        const double pair_label_x = x + dw / 2.0 + dp(8);
        const double pair_switch_x = x + dw - margin - pair_w;
        auto pair_row = [&](const char *label, const char *state, int id, double sy) {
            paint_text(cr, ui_font(), label, pair_label_x, sy + dp(8), theme::kTextR, theme::kTextG, theme::kTextB,
                       static_cast<int>(std::max(1.0, pair_switch_x - pair_label_x - dp(8))));
            button(cr, pair_switch_x, sy, pair_w, bh, state, id, false);
        };
        auto left_row = [&](const char *label, const char *state, int id, double sy) {
            if (!paired) {
                switch_row(label, state, id, sy);
                return;
            }
            const double left_switch = x + dw / 2.0 - dp(8) - pair_w;
            paint_text(cr, ui_font(), label, x + margin, sy + dp(8), theme::kTextR, theme::kTextG, theme::kTextB,
                       static_cast<int>(std::max(1.0, left_switch - x - margin - dp(8))));
            button(cr, left_switch, sy, pair_w, bh, state, id, false);
        };
        const bool scale_auto = scale_percent_ <= 0;
        const std::string scale_text = scale_auto ? "Auto" : std::to_string(scale_percent_) + "%";
        slider(1, "Volume", std::to_string(volume_) + "%", volume_, 0, 100, volume_row, true, false);
        const std::string scale_caption = scale_auto ? "UI scale" : "UI scale  " + scale_text;
        slider(2, scale_caption, "", scale_auto ? 100 : scale_percent_, 50, 200, scale_row, !scale_auto, true);
        const double auto_h = std::min(bh, text_h + static_cast<double>(dp(6)));
        button(cr, switch_x, content + scale_row.caption, switch_w, auto_h, scale_auto ? "Auto" : "Manual", kScaleAuto,
               false);
        double sy = content + scale_row.next_caption;
        const double first_row = sy;
        left_row("Pause when unfocused", pause_unfocused_ ? "On" : "Off", kReset, sy);
        sy += bh + stack_gap;
        left_row("Mouse wheel", wheel_zooms_ ? "Zoom" : "Scroll", kWheelZoom, sy);
        sy += bh + stack_gap;
        const bool full = window_ != nullptr && (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) != 0;
        left_row("Fullscreen", full ? "On" : "Off", kFullscreenBox, sy);
        sy += bh + stack_gap;
        const char *autosave_state = "Off";
        std::string autosave_owned;
        if (autosave_seconds_ > 0) {
            if (autosave_seconds_ % 60 == 0) {
                const int minutes = autosave_seconds_ / 60;
                autosave_owned = std::to_string(minutes) + (minutes == 1 ? " minute" : " minutes");
            } else {
                autosave_owned = std::to_string(autosave_seconds_) + " seconds";
            }
            autosave_state = autosave_owned.c_str();
        }
        left_row("Autosave", autosave_state, kAutosave, sy);
        sy += bh + stack_gap;
        std::string layout_state = layout_mode::choice_label(layout_pref_);
        if (layout_mode::clamp_choice(layout_pref_) == layout_mode::Auto) {
            layout_state += handheld_ ? " (handheld)" : " (desktop)";
        }
        if (paired) {
            double py = first_row;
            pair_row("Interface", slate_ ? "Slate" : "Light", kSkinBox, py);
            py += bh + stack_gap;
            pair_row("Night lighting", night_ ? "On" : "Off", kNightBox, py);
            py += bh + stack_gap;
            pair_row("Layout", layout_state.c_str(), kLayoutBox, py);
        } else {
            switch_row("Interface", slate_ ? "Slate" : "Light", kSkinBox, sy);
            sy += bh + stack_gap;
            switch_row("Night lighting", night_ ? "On" : "Off", kNightBox, sy);
            sy += bh + stack_gap;
            switch_row("Layout", layout_state.c_str(), kLayoutBox, sy);
        }
        buttons_at(x, dw, y + panel.h - margin - bh, {{"_Cancel", kCancel}, {"_OK", kOk}});
        return;
    }
    if (dialog_ == Dialog::Controls) {
        const std::string heading = controls_page_title(controls_page_, deck_);
        ControlsPadNames pad_names;
        auto named = [](const char *glyph) { return glyph != nullptr && glyph[0] != '\0' ? glyph : "None"; };
        pad_names.confirm = named(glyph_for(PadConfirm));
        pad_names.cancel = named(glyph_for(PadCancel));
        pad_names.west = named(glyph_for(PadQuery));
        pad_names.north = named(glyph_for(PadPause));
        pad_names.left = named(glyph_for(PadPrevTool));
        pad_names.right = named(glyph_for(PadNextTool));
        pad_names.menu = named(glyph_for(PadMenu));
        pad_names.view = named(glyph_for(PadBack));
        pad_names.l3 = named(glyph_for(PadOverlay));
        pad_names.r3 = named(glyph_for(PadCenter));
        const ControlsKeyText key_text = controls_keys();
        const double width = message_width(win_w_, margin, dp(520), dp(1000));
        const double inner = width - 2 * margin;
        // Sections in a two-column grid, each a heading over aligned rows: the
        // key in a bold chip, its action beside it. A narrow window stacks
        // the sections in one column.
        const double chip_pad = dp(6);
        const double row_gap = dp(4);
        const double section_gap = dp(12);
        const double col_gap = dp(24);
        auto lay_page = [&](const std::vector<ControlsSection> &sections, double x0, double y0, bool paint) {
            const bool split = inner >= dp(560);
            bool has_band = false;
            for (const ControlsSection &section : sections) {
                has_band = has_band || section.side == 2;
            }
            // Wide enough for three columns: the tool keys get one of their own.
            const bool three = has_band && inner >= dp(860);
            const int grid = three ? 3 : split ? 2 : 1;
            std::vector<const ControlsSection *> column[3];
            std::vector<const ControlsSection *> band;
            for (const ControlsSection &section : sections) {
                if (section.side == 2 && !three) {
                    band.push_back(&section);
                } else {
                    column[std::min(section.side, grid - 1)].push_back(&section);
                }
            }
            auto section_block = [&](const ControlsSection &section, double x, double y, double w, int columns) {
                double cy = y;
                const double head_h = line_height(bold_font(), section.heading, w);
                if (paint) {
                    paint_text(cr, bold_font(), section.heading, x, cy, 0.45, 0.04, 0.02, static_cast<int>(w));
                    fill(cr, x, cy + head_h + dp(2), w, 1.0, 0.72, 0.55, 0.18);
                }
                cy += head_h + dp(8);
                const double sub_gap = dp(16);
                const double sub_w = (w - sub_gap * (columns - 1)) / columns;
                const int count = static_cast<int>(section.rows.size());
                const int per = (count + columns - 1) / columns;
                double bottom = cy;
                for (int c = 0; c < columns; ++c) {
                    const int begin = c * per;
                    const int end = std::min(count, begin + per);
                    double key_w = 0;
                    for (int i = begin; i < end; ++i) {
                        key_w = std::max(key_w, static_cast<double>(
                                                    measure_text(cr, bold_font(), section.rows[static_cast<std::size_t>(i)].key, -1).width));
                    }
                    key_w = std::min(key_w + 2 * chip_pad, sub_w * 0.5);
                    const double sx = x + c * (sub_w + sub_gap);
                    const double action_x = sx + key_w + dp(10);
                    const double action_w = std::max(1.0, sub_w - key_w - dp(10));
                    double ry = cy;
                    for (int i = begin; i < end; ++i) {
                        const ControlsRow &row = section.rows[static_cast<std::size_t>(i)];
                        const double key_h = line_height(bold_font(), row.key, key_w - 2 * chip_pad);
                        const double act_h = line_height(body_font(), row.action, action_w);
                        const double chip_h = key_h + dp(2);
                        const double h = std::max(chip_h, act_h);
                        if (paint) {
                            cairo_save(cr);
                            round_dialog_rect(cr, sx + 0.5, ry + 0.5, key_w - 1, chip_h - 1, dp(4));
                            cairo_set_source_rgb(cr, 0.93, 0.87, 0.72);
                            cairo_fill_preserve(cr);
                            cairo_set_source_rgb(cr, 0.72, 0.55, 0.18);
                            cairo_set_line_width(cr, 1.0);
                            cairo_stroke(cr);
                            cairo_restore(cr);
                            paint_text(cr, bold_font(), row.key, sx + chip_pad, ry + dp(1), theme::kTextR, theme::kTextG,
                                       theme::kTextB, static_cast<int>(std::max(1.0, key_w - 2 * chip_pad)));
                            paint_text(cr, body_font(), row.action, action_x, ry + (chip_h - std::min(chip_h, act_h)) / 2.0,
                                       theme::kTextR, theme::kTextG, theme::kTextB, static_cast<int>(action_w));
                        }
                        ry += h + row_gap;
                    }
                    bottom = std::max(bottom, ry);
                }
                return bottom - y;
            };
            const double col_w = (inner - col_gap * (grid - 1)) / grid;
            double y = y0;
            for (int c = 0; c < grid; ++c) {
                double cy = y0;
                for (const ControlsSection *section : column[c]) {
                    cy += section_block(*section, x0 + c * (col_w + col_gap), cy, col_w, 1) + section_gap;
                }
                y = std::max(y, cy);
            }
            for (const ControlsSection *section : band) {
                y += section_block(*section, x0, y, inner, split ? 4 : 2) + section_gap;
            }
            return std::max(0.0, y - y0 - section_gap);
        };
        // The page name rides in the title band, so the rows get the room.
        const std::vector<ControlsSection> sections = controls_page_sections(controls_page_, deck_, pad_names, key_text);
        const double content_h = lay_page(sections, 0, 0, false);
        double tallest = content_h;
        for (int page = 0; page < 3; ++page) {
            if (page == controls_page_) {
                continue;
            }
            tallest = std::max(tallest, lay_page(controls_page_sections(page, deck_, pad_names, key_text), 0, 0, false));
        }
        const double footer_h = dp(32);
        const double buttons_h = dp(36);
        const double chrome = dp(40) + margin + footer_h + dp(8) + buttons_h + margin;
        const double max_h = std::max(chrome + dp(48), band_h - 2.0 * margin);
        const double view_h = std::max(static_cast<double>(dp(48)), std::min(tallest, max_h - chrome));
        const double height = chrome + view_h;
        const DialogRect panel = place_dialog(win_w_, band_h, width, height, margin);
        const std::string panel_title = std::string("Controls \u2014 ") + heading;
        frame_panel(cr, panel.x, panel.y, panel.w, panel.h, panel_title.c_str());
        const double body_top = panel.y + dp(48);
        controls_scroll_max_ = std::max(0, static_cast<int>(std::ceil(content_h - view_h)));
        if (controls_scroll_ < 0) {
            controls_scroll_ = 0;
        }
        if (controls_scroll_ > controls_scroll_max_) {
            controls_scroll_ = controls_scroll_max_;
        }
        cairo_save(cr);
        cairo_rectangle(cr, panel.x + margin, body_top, panel.w - 2 * margin, view_h);
        cairo_clip(cr);
        const double origin = body_top - controls_scroll_;
        lay_page(sections, panel.x + margin, origin, true);
        cairo_restore(cr);
        if (controls_scroll_max_ > 0) {
            const double track_w = std::max(4.0, static_cast<double>(dp(6)));
            const double track_x = panel.x + panel.w - margin + (margin - track_w) * 0.5;
            fill(cr, track_x, body_top, track_w, view_h, 0.82, 0.82, 0.82);
            const double thumb_h = std::max(static_cast<double>(dp(18)),
                                            view_h * view_h / (view_h + static_cast<double>(controls_scroll_max_)));
            const double travel = std::max(0.0, view_h - thumb_h);
            const double thumb_y =
                body_top + travel * (static_cast<double>(controls_scroll_) / static_cast<double>(controls_scroll_max_));
            fill(cr, track_x, thumb_y, track_w, thumb_h, theme::chrome_select_r(), theme::chrome_select_g(), theme::chrome_select_b());
        }
        const double footer_y = body_top + view_h + dp(4);
        const double dot = dp(14);
        const double dot_gap = dp(8);
        const double dots_w = dot * 3 + dot_gap * 2;
        const double prev_w = dp(88);
        const double next_w = dp(88);
        button(cr, panel.x + margin, footer_y, prev_w, dp(28), "_Prev", kControlsPrev, false);
        const double dots_x = panel.x + (panel.w - dots_w) / 2.0;
        for (int page = 0; page < 3; ++page) {
            const int id = page == 0 ? kPageDot0 : page == 1 ? kPageDot1 : kPageDot2;
            const bool here = controls_page_index(controls_page_, deck_) == page;
            button(cr, dots_x + page * (dot + dot_gap), footer_y + dp(6), dot, dot, "", id, false, here);
        }
        button(cr, panel.x + panel.w - margin - next_w, footer_y, next_w, dp(28), "_Next", kControlsNext, false);
        buttons_at(panel.x, panel.w, panel.y + panel.h - margin - buttons_h, {{"_Remap", kRemap}, {"_Close", kCancel}});
        return;
    }
    if (dialog_ == Dialog::Budget && budget_cover_ && budget_is_tax_) {
        draw_year_cover(cr, margin, band_h);
        return;
    }
    if (dialog_ == Dialog::Budget) {
        const auto book = session_.budget();
        const BudgetCopy copy = budget_copy(book, budget_is_tax_);
        double gap = static_cast<double>(dp(6));
        double tail = static_cast<double>(dp(8));
        const double col_gap = static_cast<double>(dp(16));
        auto unwrapped = [&](const std::string &text) {
            return measure_text(cr, ui_font(), text, -1).width;
        };
        const int need_col = std::max(std::max(unwrapped(copy.taxes), unwrapped(copy.previous_funds)),
                                      std::max(unwrapped(copy.cash_flow), unwrapped(copy.current_funds))) +
                             dp(8);
        const double want = std::max(static_cast<double>(dp(680)), 2.0 * margin + col_gap + 2.0 * static_cast<double>(need_col));
        const double width = std::min(want, std::max(1.0, win_w_ - 2.0 * margin));
        const double inner = width - 2.0 * margin;
        const double col_w = std::max(1.0, (inner - col_gap) / 2.0);
        const int col_px = static_cast<int>(col_w);
        const std::string taxes = avoid_short_last_line(cr, ui_font(), copy.taxes, col_px);
        const std::string previous = avoid_short_last_line(cr, ui_font(), copy.previous_funds, col_px);
        const std::string flow = avoid_short_last_line(cr, ui_font(), copy.cash_flow, col_px);
        const std::string funds = avoid_short_last_line(cr, ui_font(), copy.current_funds, col_px);
        const double row1 = std::max(line_height(ui_font(), taxes, col_w), line_height(ui_font(), previous, col_w));
        const double row2 = std::max(line_height(ui_font(), flow, col_w), line_height(ui_font(), funds, col_w));
        const double note_h = copy.closing_note.empty() ? 0.0 : line_height(ui_font(), copy.closing_note, inner) + gap;
        auto block_h = [&](const std::string &caption, const std::string &value_text, const std::string &detail) {
            const double value_w = static_cast<double>(measure_text(cr, bold_font(), value_text, -1).width);
            const double cap_w = std::max(1.0, inner - value_w - gap);
            double h = line_height(bold_font(), caption, cap_w) + gap;
            if (!detail.empty()) {
                h += line_height(ui_font(), detail, inner) + gap;
            }
            h += tail + static_cast<double>(dp(16)) + tail;
            return h;
        };
        const double fund_gap = static_cast<double>(dp(14));
        const double blocks = block_h(copy.tax_title, copy.tax_value, "") + fund_gap +
                              block_h("Road fund", std::to_string(road_) + "%", copy.road) +
                              block_h("Police fund", std::to_string(police_) + "%", copy.police) +
                              block_h("Fire fund", std::to_string(fire_) + "%", copy.fire);
        const double treasury_h = static_cast<double>(dp(28));
        const double height =
            dp(40) + margin + treasury_h + row1 + gap + row2 + gap + note_h + blocks + dp(8) + dp(36) + margin;
        const DialogRect panel = place_dialog(win_w_, band_h, width, height, margin);
        const double x = panel.x;
        const double y = panel.y;
        const double dw = panel.w;
        const double dh = panel.h;
        // A short window clamps the panel. Tighten the rhythm so the fire
        // slider stays above Cancel and OK.
        if (dh + 1 < height) {
            const double overflow = height - dh;
            const double tail_save = std::max(0.0, (tail - 1.0) * 8.0);
            if (overflow <= tail_save) {
                tail = std::max(1.0, tail - overflow / 8.0);
            } else {
                tail = 1;
                gap = std::max(2.0, gap - (overflow - tail_save) / 12.0);
            }
        }
        frame_panel(cr, x, y, dw, dh, "Budget");
        const double live_inner = dw - 2.0 * margin;
        const double live_col = std::max(1.0, (live_inner - col_gap) / 2.0);
        double cursor = y + dp(48);
        {
            const double cap_h = place_line(bold_font(), "Treasury", x + margin, cursor, live_inner, theme::kTextR,
                                            theme::kTextG, theme::kTextB);
            cairo_set_source_rgb(cr, theme::chrome_select_r(), theme::chrome_select_g(), theme::chrome_select_b());
            cairo_rectangle(cr, x + margin, cursor + cap_h + 1, std::min(live_inner, static_cast<double>(dp(140))), dp(2));
            cairo_fill(cr);
            cursor += std::max(treasury_h, cap_h + dp(6));
        }
        const double taxes_h = place_split(taxes, x + margin, cursor, live_col);
        const double previous_h = place_split(previous, x + margin + live_col + col_gap, cursor, live_col);
        cursor += std::max(taxes_h, previous_h) + gap;
        const double flow_h = place_split(flow, x + margin, cursor, live_col);
        const double funds_h = place_split(funds, x + margin + live_col + col_gap, cursor, live_col);
        cursor += std::max(flow_h, funds_h) + gap;
        if (!copy.closing_note.empty()) {
            double note_r = 0;
            double note_g = 0;
            double note_b = 0;
            theme::reading_ink(note_r, note_g, note_b);
            const double h = place_line(ui_font(), copy.closing_note, x + margin, cursor, live_inner, note_r, note_g, note_b);
            cursor += h + gap;
        }
        auto slider = [&](int index, const std::string &caption, const std::string &value_text, const std::string &detail,
                          int value, int min, int max) {
            const TextMetrics value_metrics = measure_text(cr, bold_font(), value_text, -1);
            const double value_w = static_cast<double>(value_metrics.width);
            const double cap_w = std::max(1.0, live_inner - value_w - gap);
            const double cap_h = place_line(bold_font(), caption, x + margin, cursor, cap_w, theme::kTextR, theme::kTextG,
                                            theme::kTextB);
            place_line(bold_font(), value_text, x + dw - margin - value_w, cursor, std::max(1.0, value_w), theme::kTextR,
                       theme::kTextG, theme::kTextB);
            cursor += cap_h + gap;
            if (!detail.empty()) {
                double note_r = 0;
                double note_g = 0;
                double note_b = 0;
                theme::reading_ink(note_r, note_g, note_b);
                const double detail_h = place_line(ui_font(), detail, x + margin, cursor, live_inner, note_r, note_g, note_b);
                cursor += detail_h + gap;
            }
            cursor += tail;
            const double bar_y = cursor;
            const double sx = x + margin;
            const double sw = live_inner;
            fill(cr, sx, bar_y, sw, dp(16), 1, 1, 1);
            const double t = (value - min) / static_cast<double>(std::max(1, max - min));
            fill(cr, sx, bar_y, sw * t, dp(16), theme::chrome_select_r(), theme::chrome_select_g(), theme::chrome_select_b());
            const double thumb = dp(14);
            const double thumb_x = slider_thumb_x(sx, sw, thumb, t);
            fill(cr, thumb_x, bar_y - dp(4), thumb, dp(24), 1, 1, 1);
            stroke_rect(cr, thumb_x, bar_y - dp(4), thumb, dp(24), theme::chrome_select_r(), theme::chrome_select_g(),
                        theme::chrome_select_b());
            stroke_rect(cr, sx, bar_y, sw, dp(16), theme::kButtonEdgeR, theme::kButtonEdgeG, theme::kButtonEdgeB);
            const int id = kSlider + index;
            push_hot(sx, bar_y - dp(6), sw, dp(28), id);
            push_focus(id);
            next_sliders_.push_back(SliderGeom{id, sx, bar_y - dp(6), sw, static_cast<double>(dp(28)), min, max});
            if (focus_id_ == id) {
                stroke_slider_focus(cr, sx + 1, bar_y - dp(4), sw - 2, dp(24));
            }
            cursor += dp(16) + tail;
        };
        slider(1, copy.tax_title, copy.tax_value, "", tax_, 0, 20);
        cursor += fund_gap;
        auto funded = [](int percent, std::int64_t need) {
            return std::to_string(percent) + "%  " + graph_money(need * percent / 100);
        };
        slider(2, "Road fund", funded(road_, book.road_need), copy.road, road_, 0, 100);
        slider(3, "Police fund", funded(police_, book.police_need), copy.police, police_, 0, 100);
        slider(4, "Fire fund", funded(fire_, book.fire_need), copy.fire, fire_, 0, 100);
        if (budget_is_tax_) {
            buttons_at(x, dw, y + dh - margin - dp(36), {{"_Reset", kReset}, {"_Collect taxes", kCollect}});
        } else {
            buttons_at(x, dw, y + dh - margin - dp(36), {{"_Cancel", kCancel}, {"_OK", kOk}});
        }
        return;
    }
    if (dialog_ == Dialog::Graphs) {
        const std::vector<ChartLegend> legend = chart_legend(&session_, graphs_long_);
        const int legend_cols = 2;
        const int series_rows = std::max(1, (static_cast<int>(legend.size()) + legend_cols - 1) / legend_cols);
        const int legend_rows = series_rows + 1;
        const double legend_h = legend_rows * static_cast<double>(dp(20));
        const double width = std::min(static_cast<double>(dp(640)), std::max(1.0, win_w_ - 2.0 * margin));
        const double height = dp(86) + dp(220) + dp(8) + legend_h + dp(12) + dp(36) + margin;
        const DialogRect panel = place_dialog(win_w_, band_h, width, height, margin);
        const double x = panel.x;
        const double y = panel.y;
        const double dw = panel.w;
        const double dh = panel.h;
        frame_panel(cr, x, y, dw, dh, "Graphs");
        button(cr, x + margin, y + dp(46), dp(140), dp(32), "10 year", kShortScale, false, !graphs_long_);
        button(cr, x + margin + dp(148), y + dp(46), dp(140), dp(32), "120 year", kLongScale, false, graphs_long_);
        const double legend_row_h = dp(20);
        const double button_top = y + dh - margin - dp(36);
        const double legend_top = button_top - dp(12) - legend_h;
        const double chart_top = y + dp(86);
        const double chart_h = std::max(static_cast<double>(dp(64)), legend_top - dp(8) - chart_top);
        {
            TargetCr live(cr);
            cairo_translate(live.cr, x + margin, chart_top);
            graph_chart_x_ = x + margin;
            graph_chart_y_ = chart_top;
            graph_chart_w_ = dw - 2 * margin;
            graph_chart_h_ = chart_h;
            draw_history_chart(live.cr, &session_, graphs_long_, static_cast<int>(dw - 2 * margin),
                               static_cast<int>(chart_h), graph_mask_, graph_pointer_x_, &graph_readout_, ui_scale_);
        }
        push_hot(graph_chart_x_, graph_chart_y_, graph_chart_w_, graph_chart_h_, kGraphPlot);
        push_focus(kGraphPlot);
        if (focus_id_ == kGraphPlot) {
            cairo_set_source_rgb(cr, theme::kFocusR, theme::kFocusG, theme::kFocusB);
            cairo_set_line_width(cr, 2);
            cairo_rectangle(cr, graph_chart_x_ + 1, graph_chart_y_ + 1, graph_chart_w_ - 2, graph_chart_h_ - 2);
            cairo_stroke(cr);
        }
        const double col_w = (dw - 2 * margin) / legend_cols;
        for (int i = 0; i < static_cast<int>(legend.size()); ++i) {
            const ChartLegend &entry = legend[static_cast<std::size_t>(i)];
            const int col = i % legend_cols;
            const int row = i / legend_cols;
            const double lx = x + margin + col * col_w;
            const double ly = legend_top + row * legend_row_h;
            const int series_id = kGraphSeries + i;
            push_hot(lx, ly, col_w - dp(4), legend_row_h, series_id);
            push_focus(series_id);
            if (focus_id_ == series_id) {
                cairo_set_source_rgb(cr, theme::kFocusR, theme::kFocusG, theme::kFocusB);
                cairo_set_line_width(cr, 2);
                cairo_rectangle(cr, lx + 1, ly + 1, col_w - dp(6), legend_row_h - 2);
                cairo_stroke(cr);
            }
            if ((graph_mask_ & (1 << i)) == 0) {
                cairo_set_source_rgba(cr, entry.r, entry.g, entry.b, 0.25);
            } else {
                cairo_set_source_rgb(cr, entry.r, entry.g, entry.b);
            }
            cairo_rectangle(cr, lx, ly + dp(4), dp(18), dp(8));
            cairo_fill(cr);
            paint_text(cr, ui_font(), entry.caption, lx + dp(24), ly, theme::kTextR, theme::kTextG, theme::kTextB,
                       static_cast<int>(col_w - dp(28)));
        }
        {
            const double note_y = legend_top + series_rows * legend_row_h;
            const double nx = x + margin;
            const double dash[2] = {4.0, 3.0};
            cairo_set_source_rgba(cr, 0.15, 0.15, 0.15, 0.85);
            cairo_set_line_width(cr, 1.5);
            cairo_set_dash(cr, dash, 2, 0.0);
            cairo_move_to(cr, nx, note_y + dp(8));
            cairo_line_to(cr, nx + dp(18), note_y + dp(8));
            cairo_stroke(cr);
            cairo_set_dash(cr, nullptr, 0, 0.0);
            paint_text(cr, ui_font(), "Dashed line: middle of each series", nx + dp(24), note_y, theme::kTextR,
                       theme::kTextG, theme::kTextB, static_cast<int>(dw - 2 * margin - dp(24)));
        }
        buttons_at(x, dw, button_top, {{"_Close", kClose}});
        return;
    }
    if (dialog_ == Dialog::Evaluation) {
        const EvaluationCopy copy = evaluation_copy(session_.evaluation(), session_.scenario_progress());
        const std::string heading = session_.city_name() + " — " + session_.date_text();
        double gap = static_cast<double>(dp(6));
        const double col_gap = margin;
        const int delta_px = measure_text(cr, body_font(), copy.score_delta, -1).width;
        const double fitted_delta = static_cast<double>(delta_px + dp(12));
        const double want = std::max(static_cast<double>(dp(760)), 2.0 * margin + col_gap + 2.0 * fitted_delta);
        double width = std::min(want, std::max(1.0, win_w_ - 2.0 * margin));
        const double inner = width - 2.0 * margin;
        const double col_w = std::max(1.0, (inner - col_gap) / 2.0);
        const std::string score_delta =
            avoid_short_last_line(cr, body_font(), copy.score_delta, static_cast<int>(col_w));
        struct Row {
            const char *font;
            std::string text;
        };
        std::vector<Row> left_rows = {
            {bold_font(), copy.score_caption},
            {body_font(), copy.score},
            {body_font(), score_delta},
            {bold_font(), copy.opinion_caption},
            {body_font(), copy.opinion_question},
            {body_font(), copy.yes + "    " + copy.no},
        };
        const double bar_h = copy.yes_percent >= 0 ? static_cast<double>(dp(10)) : 0.0;
        const CitySession::Evaluation report = session_.evaluation();
        const bool linked_problems = !report.no_residents && !report.problems.empty();
        const int problem_n = linked_problems ? std::min(4, static_cast<int>(report.problems.size())) : 0;
        std::string advice_body;
        std::string advice_show;
        std::string advice_alt;
        if (eval_why_ >= 0 && eval_why_ < problem_n && report.problems[static_cast<std::size_t>(eval_why_)].kind >= 0) {
            const civic::Advice advice = civic::advice_for(report.problems[static_cast<std::size_t>(eval_why_)].kind);
            advice_body = advice.body;
            advice_show = advice.show == nullptr ? "" : advice.show;
            advice_alt = advice.alt == nullptr ? "" : advice.alt;
        }
        const double problem_row = static_cast<double>(dp(24));
        double problem_block = 0;
        if (problem_n > 0) {
            problem_block = problem_n * (problem_row + gap);
        } else {
            for (const std::string &problem : copy.problems) {
                problem_block += line_height(body_font(), problem, col_w) + gap;
            }
        }
        if (!advice_body.empty()) {
            problem_block += line_height(ui_font(), advice_body, col_w) + gap + static_cast<double>(dp(32));
        }
        std::vector<Row> right_rows = {{bold_font(), copy.problems_caption}};
        right_rows.push_back(Row{bold_font(), copy.stats_caption});
        right_rows.push_back(Row{body_font(), copy.population});
        right_rows.push_back(Row{body_font(), copy.migration});
        right_rows.push_back(Row{body_font(), copy.assessed});
        right_rows.push_back(Row{body_font(), copy.category});
        right_rows.push_back(Row{body_font(), copy.difficulty});
        if (!copy.scenario.empty()) {
            right_rows.push_back(Row{body_font(), copy.scenario});
        }
        auto column_h = [&](const std::vector<Row> &rows, double column) {
            double h = 0;
            for (const Row &row : rows) {
                h += line_height(row.font, row.text, column) + gap;
            }
            return h;
        };
        const double opinion_extra =
            copy.opinion_bar.empty() ? 0.0 : line_height(ui_font(), copy.opinion_bar, col_w) + gap;
        const double left_h = column_h(left_rows, col_w) + (bar_h > 0.0 ? bar_h + gap : 0.0) + opinion_extra;
        const double right_h = column_h(right_rows, col_w) + problem_block;
        const double heading_h = line_height(body_font(), heading, inner);
        double height = dp(40) + margin + heading_h + gap + std::max(left_h, right_h) + dp(8) + dp(36) + margin;
        const double band_limit = band_h - 2.0 * margin;
        // A narrow column wraps the scenario goal into the Close button.
        // Use the full window width, then a tighter gap, before clamping.
        if (height > band_limit && width < win_w_ - 2.0 * margin - 1) {
            const double wide = std::max(1.0, win_w_ - 2.0 * margin);
            const double wide_inner = wide - 2.0 * margin;
            const double wide_col = std::max(1.0, (wide_inner - col_gap) / 2.0);
            const double wide_left = column_h(left_rows, wide_col) + (bar_h > 0.0 ? bar_h + gap : 0.0) +
                                     (copy.opinion_bar.empty() ? 0.0 : line_height(ui_font(), copy.opinion_bar, wide_col) + gap);
            const double wide_right = column_h(right_rows, wide_col) + problem_block;
            const double wide_head = line_height(body_font(), heading, wide_inner);
            const double wide_h = dp(40) + margin + wide_head + gap + std::max(wide_left, wide_right) + dp(8) + dp(36) + margin;
            if (wide_h < height) {
                width = wide;
                height = wide_h;
            }
        }
        if (height > band_limit) {
            gap = 2;
            const double tight_left = column_h(left_rows, col_w) + (bar_h > 0.0 ? bar_h + gap : 0.0) +
                                      (copy.opinion_bar.empty() ? 0.0 : line_height(ui_font(), copy.opinion_bar, col_w) + gap);
            const double tight_right = column_h(right_rows, col_w) + problem_block;
            height = dp(40) + margin + heading_h + gap + std::max(tight_left, tight_right) + dp(4) + dp(36) + margin;
        }
        const DialogRect panel = place_dialog(win_w_, band_h, width, height, margin);
        const double x = panel.x;
        const double y = panel.y;
        const double dw = panel.w;
        frame_panel(cr, x, y, dw, panel.h, "Evaluation");
        const double live_inner = dw - 2.0 * margin;
        const double live_col = std::max(1.0, (live_inner - col_gap) / 2.0);
        const double head_y = y + dp(48);
        const double placed_head =
            place_line(body_font(), heading, x + margin, head_y, live_inner, theme::kTextR, theme::kTextG, theme::kTextB);
        const double left_x = x + margin;
        const double right_x = x + margin + live_col + col_gap;
        double left_y = head_y + placed_head + gap;
        double right_y = left_y;
        auto paint_rows = [&](const std::vector<Row> &rows, double lx, double &cursor) {
            for (const Row &row : rows) {
                const bool heading = row.font == bold_font();
                const double h = place_line(row.font, row.text, lx, cursor, live_col, theme::kTextR, theme::kTextG,
                                            theme::kTextB);
                cursor += h + gap;
                if (heading) {
                    cairo_set_source_rgb(cr, theme::chrome_select_r(), theme::chrome_select_g(), theme::chrome_select_b());
                    cairo_rectangle(cr, lx, cursor - gap + 1, std::min(live_col, static_cast<double>(dp(120))), dp(2));
                    cairo_fill(cr);
                }
            }
        };
        paint_rows(left_rows, left_x, left_y);
        if (copy.yes_percent >= 0) {
            const double bw = std::min(live_col, static_cast<double>(dp(180)));
            fill(cr, left_x, left_y, bw, bar_h, 0.9, 0.9, 0.9);
            fill(cr, left_x, left_y, bw * (copy.yes_percent / 100.0), bar_h, theme::chrome_select_r(), theme::chrome_select_g(),
                 theme::chrome_select_b());
            left_y += bar_h + gap;
        }
        if (!copy.opinion_bar.empty()) {
            const double opinion_h = place_line(ui_font(), copy.opinion_bar, left_x, left_y, live_col, theme::kTextR,
                                                theme::kTextG, theme::kTextB);
            left_y += opinion_h + gap;
        }
        (void)left_y;
        {
            const double caption_h = place_line(bold_font(), copy.problems_caption, right_x, right_y, live_col,
                                                theme::kTextR, theme::kTextG, theme::kTextB);
            right_y += caption_h + gap;
            cairo_set_source_rgb(cr, theme::chrome_select_r(), theme::chrome_select_g(), theme::chrome_select_b());
            cairo_rectangle(cr, right_x, right_y - gap + 1, std::min(live_col, static_cast<double>(dp(120))), dp(2));
            cairo_fill(cr);
            if (problem_n > 0) {
                for (int i = 0; i < problem_n; ++i) {
                    const CitySession::Problem &problem = report.problems[static_cast<std::size_t>(i)];
                    const std::string line = problem.name + ": " + std::to_string(problem.votes) + "%";
                    const int id = kEvalProblem + i;
                    const double why_w = dp(64);
                    push_hot(right_x, right_y, std::max(1.0, live_col - why_w - 4), problem_row, id);
                    push_focus(id);
                    if (focus_id_ == id || hover_id_ == id) {
                        stroke_rect(cr, right_x, right_y, live_col - why_w - 4, problem_row - 2, theme::chrome_select_r(),
                                    theme::chrome_select_g(), theme::chrome_select_b());
                    }
                    place_line(body_font(), line, right_x + 4, right_y + 2, live_col - why_w - 8, theme::kTextR,
                               theme::kTextG, theme::kTextB);
                    button(cr, right_x + live_col - why_w, right_y, why_w, std::max(1.0, problem_row - 2), "Why?",
                           kEvalWhy + i, false, eval_why_ == i);
                    right_y += problem_row + gap;
                }
            } else {
                for (const std::string &problem : copy.problems) {
                    const double h = place_line(body_font(), problem, right_x, right_y, live_col, theme::kTextR,
                                                theme::kTextG, theme::kTextB);
                    right_y += h + gap;
                }
            }
            if (!advice_body.empty()) {
                const double h = place_line(ui_font(), advice_body, right_x, right_y, live_col, theme::kTextR, theme::kTextG,
                                            theme::kTextB);
                right_y += h + gap;
                if (!advice_show.empty()) {
                    button(cr, right_x, right_y, std::min(live_col, static_cast<double>(dp(150))), dp(28), advice_show.c_str(),
                           kEvalShow, false);
                    if (!advice_alt.empty()) {
                        button(cr, right_x + dp(158), right_y, std::min(live_col - dp(158), static_cast<double>(dp(160))),
                               dp(28), advice_alt.c_str(), kEvalAlt, false);
                    }
                    right_y += dp(32);
                }
            }
            for (std::size_t row = 1; row < right_rows.size(); ++row) {
                const Row &line = right_rows[row];
                const double h = place_line(line.font, line.text, right_x, right_y, live_col, theme::kTextR, theme::kTextG,
                                            theme::kTextB);
                right_y += h + gap;
                if (line.font == bold_font()) {
                    cairo_set_source_rgb(cr, theme::chrome_select_r(), theme::chrome_select_g(), theme::chrome_select_b());
                    cairo_rectangle(cr, right_x, right_y - gap + 1, std::min(live_col, static_cast<double>(dp(120))), dp(2));
                    cairo_fill(cr);
                }
            }
        }
        buttons_at(x, dw, y + panel.h - margin - dp(36), {{"_Close", kClose}});
        return;
    }
    if (dialog_ == Dialog::Messages) {
        const auto &notes_for_size = session_.notices();
        const double messages_h = notes_for_size.empty()
                                      ? dp(40) + margin + dp(48) + dp(12) + static_cast<double>(control_px(36, 44)) + margin
                                      : std::min(large_h, static_cast<double>(dp(520)));
        const DialogRect panel = place_dialog(win_w_, band_h, notes_for_size.empty() ? std::min(large_w, static_cast<double>(dp(480))) : large_w,
                                              messages_h, margin);
        const double x = panel.x;
        const double y = panel.y;
        const double dw = panel.w;
        const double dh = panel.h;
        frame_panel(cr, x, y, dw, dh, "Messages");
        label(cr, ui_font(), "Recent news. Choosing a row jumps to that place.", x + margin, y + dp(46),
              static_cast<int>(dw - 2 * margin));
        const auto &notes = session_.notices();
        const double list_top = y + dp(72);
        const double buttons_y = y + dh - margin - static_cast<double>(control_px(36, 44));
        const int row_px = control_px(36, 44);
        const int visible = std::max(1, static_cast<int>((buttons_y - dp(8) - list_top) / row_px));
        files_visible_ = visible;
        const int count = static_cast<int>(notes.size());
        if (count == 0) {
            paint_text(cr, body_font(), "No messages yet.", x + margin, list_top, theme::kTextR, theme::kTextG,
                       theme::kTextB, static_cast<int>(dw - 2 * margin));
        } else {
            if (list_index_ >= count) {
                list_index_ = count - 1;
            }
            place_list_window(list_index_, count);
            align_list_focus();
            for (int row = 0; row < visible; ++row) {
                const int i = list_scroll_ + row;
                if (i < 0 || i >= count) {
                    break;
                }
                const CitySession::CityNotice &note = notes[static_cast<std::size_t>(i)];
                const double ry = list_top + row * row_px;
                const int id = kListItem + row;
                push_hot(x + margin, ry, dw - 2 * margin, row_px - 2, id);
                push_focus(id);
                if (list_index_ == i) {
                    fill(cr, x + margin, ry, dw - 2 * margin, row_px - 2, theme::chrome_hover_r(), theme::chrome_hover_g(), theme::chrome_hover_b());
                }
                if (focus_id_ == id) {
                    cairo_set_source_rgb(cr, theme::kFocusR, theme::kFocusG, theme::kFocusB);
                    cairo_set_line_width(cr, 2);
                    cairo_rectangle(cr, x + margin + 1, ry + 1, dw - 2 * margin - 2, row_px - 4);
                    cairo_stroke(cr);
                }
                const int mark = notice_mark(note.text);
                const double ix = x + margin + dp(6);
                const double iy = ry + dp(8);
                double mark_r = 0.2;
                double mark_g = 0.35;
                double mark_b = 0.7;
                if (mark == 1) {
                    mark_r = 0.7;
                    mark_g = 0.08;
                    mark_b = 0.06;
                } else if (mark == 2) {
                    mark_r = 0.72;
                    mark_g = 0.55;
                    mark_b = 0.12;
                } else if (mark == 3) {
                    mark_r = 0.1;
                    mark_g = 0.45;
                    mark_b = 0.18;
                } else if (mark == 4) {
                    mark_r = 0.85;
                    mark_g = 0.7;
                    mark_b = 0.1;
                } else if (mark == 5) {
                    mark_r = 0.25;
                    mark_g = 0.25;
                    mark_b = 0.28;
                }
                fill(cr, ix, iy, dp(12), dp(12), mark_r, mark_g, mark_b);
                stroke_rect(cr, ix, iy, dp(12), dp(12), 0, 0, 0);
                const std::string when = std::string(city_glance_month(note.month)) + " " + std::to_string(note.year);
                double when_r = 0;
                double when_g = 0;
                double when_b = 0;
                theme::reading_ink(when_r, when_g, when_b);
                const double text_x = ix + dp(18);
                const int when_w = measure_text(cr, ui_font(), when, -1).width;
                paint_text(cr, ui_font(), when, text_x, ry + dp(8), when_r, when_g, when_b, -1);
                paint_text(cr, bold_font(), note.text, text_x + when_w + dp(8), ry + dp(8), theme::kTextR, theme::kTextG,
                           theme::kTextB, static_cast<int>(std::max(1.0, dw - 2 * margin - dp(40) - when_w)));
            }
        }
        buttons_at(x, dw, buttons_y, {{"_Close", kClose}});
        return;
    }
    int inspect_x = 0;
    int inspect_y = 0;
    if (dialog_ == Dialog::Query && session_.query_tile(inspect_x, inspect_y)) {
        // The Inspect card: what the tile is, power and road, the simulator
        // readings as bars, and why a zone is not growing.
        const CitySession::InspectFacts facts = session_.inspect_facts(inspect_x, inspect_y);
        const inspect_report::Report report = inspect_report::make(facts.facts);
        const bool dark = slate_ || handheld_;
        const double ink_r = dark ? 0xEE / 255.0 : theme::kTextR;
        const double ink_g = dark ? 0xE8 / 255.0 : theme::kTextG;
        const double ink_b = dark ? 0xDA / 255.0 : theme::kTextB;
        const double dim_r = dark ? 0x9A / 255.0 : 0.38;
        const double dim_g = dark ? 0xA6 / 255.0 : 0.36;
        const double dim_b = dark ? 0xB8 / 255.0 : 0.32;
        const bool zone = inspect_report::is_zone(facts.facts.kind);
        const bool with_bars = facts.facts.kind != inspect_report::Kind::Network;
        struct Bar {
            const char *label;
            int value;
            double r;
            double g;
            double b;
        };
        std::vector<Bar> bars;
        if (with_bars) {
            if (zone) {
                bars.push_back(Bar{"Density", facts.facts.density, 0.36, 0.61, 0.90});
            }
            bars.push_back(Bar{"Land value", facts.facts.land_value, 0.42, 0.80, 0.48});
            bars.push_back(Bar{"Crime", facts.facts.crime, 0.90, 0.40, 0.35});
            bars.push_back(Bar{"Pollution", facts.facts.pollution, 0.78, 0.55, 0.25});
            bars.push_back(Bar{"Traffic", facts.facts.traffic, 0.95, 0.66, 0.25});
        }
        const double width = std::min(static_cast<double>(dp(360)), std::max(1.0, win_w_ - 2.0 * margin));
        const double inner = width - 2.0 * margin;
        const double row_h = dp(22);
        // The status pill sits right of the title; a long name ("Amusement
        // Park" beside "Needs attention") wraps under itself instead of
        // running into the pill.
        const TextMetrics status_metrics = measure_text(cr, bold_font(), report.status, -1);
        const double title_line_h = static_cast<double>(measure_text(cr, title_font(), facts.kind_name, -1).height);
        const double title_w = std::max(1.0, inner - (status_metrics.width + dp(18)) - dp(10));
        const double title_h = std::max(title_line_h, static_cast<double>(line_height(title_font(), facts.kind_name, title_w)));
        const double chip_h = dp(26);
        std::string reasons_head;
        if (!report.reasons.empty()) {
            reasons_head = zone ? (report.tone == inspect_report::Tone::Good ? "Could slow it down" : "Why it is not growing")
                                : "Needs";
        }
        double reasons_h = 0;
        for (const std::string &reason : report.reasons) {
            reasons_h += line_height(ui_font(), "\u2022  " + reason, inner) + dp(4);
        }
        double notes_h = 0;
        for (const std::string &note : facts.notes) {
            notes_h += line_height(ui_font(), "\u2022  " + note, inner) + dp(4);
        }
        const double bh = static_cast<double>(control_px(36, 44));
        const double top_pad = dark ? static_cast<double>(margin + dp(18)) : static_cast<double>(dp(52));
        double height = top_pad + title_h + dp(10) + chip_h + dp(12);
        if (!bars.empty()) {
            height += bars.size() * row_h + dp(8);
        }
        if (zone) {
            height += row_h;
        }
        if (!facts.notes.empty()) {
            height += notes_h + dp(8);
        }
        if (!reasons_head.empty()) {
            height += line_height(tag_font(), reasons_head, inner) + dp(6) + reasons_h + dp(4);
        }
        height += dp(8) + bh + margin;
        double floor_y = static_cast<double>(win_h_ - status_h_) - margin;
        if (handheld_) {
            floor_y = static_cast<double>(quick_y_) - margin;
        }
        DialogRect panel = place_dialog(win_w_, band_h, width, height, margin);
        if (query_anchored_) {
            panel = place_near(win_w_, band_h, panel.w, panel.h, query_anchor_x_, query_anchor_y_, margin);
        }
        // Stay under the menu bar and the info band (or the handheld chips).
        const double ceiling_y = static_cast<double>(handheld_ ? hud_top_ : menu_h_ + band_h_ + margin);
        if (panel.y + panel.h > floor_y) {
            panel.y = floor_y - panel.h;
        }
        if (panel.y < ceiling_y) {
            panel.y = ceiling_y;
        }
        const double x = panel.x;
        const double y = panel.y;
        if (dark) {
            // Modal like every dialog: a light dim and a catcher under the card.
            cairo_set_source_rgba(cr, 0, 0, 0, 0.22);
            cairo_rectangle(cr, 0, 0, win_w_, win_h_);
            cairo_fill(cr);
            push_hot(0, 0, win_w_, win_h_, 0);
            cairo_set_source_rgba(cr, 0, 0, 0, 0.45);
            round_dialog_rect(cr, x + 3, y + 5, panel.w, panel.h, dp(10));
            cairo_fill(cr);
            round_dialog_rect(cr, x, y, panel.w, panel.h, dp(10));
            cairo_set_source_rgb(cr, 0x1E / 255.0, 0x26 / 255.0, 0x31 / 255.0);
            cairo_fill_preserve(cr);
            cairo_set_source_rgb(cr, 0xC9 / 255.0, 0xA6 / 255.0, 0x46 / 255.0);
            cairo_set_line_width(cr, 1.5);
            cairo_stroke(cr);
        } else {
            frame_panel(cr, x, y, panel.w, panel.h, "Inspect");
        }
        if (dark) {
            place_line(tag_font(), "INSPECT", x + margin, y + margin - dp(2), inner, 0xC9 / 255.0, 0xA6 / 255.0, 0x46 / 255.0);
        }
        double cy = y + top_pad;
        place_line(title_font(), facts.kind_name, x + margin, cy, title_w, ink_r, ink_g, ink_b);
        // Status pill on the right of the title's first line.
        {
            const TextMetrics &pill = status_metrics;
            const double pw = pill.width + dp(18);
            const double ph = pill.height + dp(6);
            const double px = x + panel.w - margin - pw;
            const double py = cy + std::max(0.0, (title_line_h - ph) / 2.0);
            double pr = 0.45;
            double pg = 0.50;
            double pb = 0.58;
            if (report.tone == inspect_report::Tone::Good) {
                pr = 0.26;
                pg = 0.62;
                pb = 0.34;
            } else if (report.tone == inspect_report::Tone::Bad) {
                pr = 0.78;
                pg = 0.27;
                pb = 0.22;
            }
            round_dialog_rect(cr, px, py, pw, ph, ph / 2.0);
            cairo_set_source_rgb(cr, pr, pg, pb);
            cairo_fill(cr);
            paint_text(cr, bold_font(), report.status, px + dp(9), py + dp(3), 1, 1, 1, -1);
        }
        cy += title_h + dp(10);
        // Power and road chips.
        {
            auto chip = [&](double cx, const std::string &text, int state) {
                const TextMetrics m = measure_text(cr, ui_font(), text, -1);
                const double cw = m.width + dp(30);
                round_dialog_rect(cr, cx, cy, cw, chip_h, dp(6));
                if (dark) {
                    cairo_set_source_rgb(cr, 0x29 / 255.0, 0x32 / 255.0, 0x3F / 255.0);
                } else {
                    cairo_set_source_rgb(cr, 0.95, 0.92, 0.84);
                }
                cairo_fill(cr);
                const double dot_r = dp(4);
                cairo_new_sub_path(cr);
                cairo_arc(cr, cx + dp(12), cy + chip_h / 2.0, dot_r, 0, 6.2832);
                if (state > 0) {
                    cairo_set_source_rgb(cr, 0.36, 0.80, 0.45);
                } else if (state == 0) {
                    cairo_set_source_rgb(cr, 0.90, 0.32, 0.26);
                } else {
                    cairo_set_source_rgb(cr, 0.55, 0.58, 0.62);
                }
                cairo_fill(cr);
                paint_text(cr, ui_font(), text, cx + dp(22), cy + (chip_h - m.height) / 2.0, ink_r, ink_g, ink_b, -1);
                return cw;
            };
            std::string power_text = "No power needed";
            if (facts.facts.power == 1) {
                power_text = "Powered";
            } else if (facts.facts.power == 0) {
                power_text = "No power";
            }
            const double used = chip(x + margin, power_text, facts.facts.power);
            if (zone || facts.facts.kind == inspect_report::Kind::Building) {
                if (facts.facts.access) {
                    chip(x + margin + used + dp(8), "Road access", 1);
                } else if (!facts.facts.needs_road) {
                    // Power plants work without a road.
                    chip(x + margin + used + dp(8), "No road needed", -1);
                } else {
                    chip(x + margin + used + dp(8), "No road", 0);
                }
            }
            text_spans_.push_back(TextSpan{x + margin, cy, inner, chip_h});
        }
        cy += chip_h + dp(12);
        // Data Center / Micro Nuclear Plant facts.
        if (!facts.notes.empty()) {
            for (const std::string &note : facts.notes) {
                cy += place_line(ui_font(), "\u2022  " + note, x + margin, cy, inner, ink_r, ink_g, ink_b) + dp(4);
            }
            cy += dp(8);
        }
        // Bars start after the widest label and stop before the widest
        // value, with the same gap on both sides, at every UI scale.
        double widest_label = 0.0;
        for (const Bar &bar : bars) {
            widest_label = std::max(widest_label, static_cast<double>(measure_text(cr, ui_font(), bar.label, -1).width));
        }
        if (zone) {
            widest_label = std::max(widest_label, static_cast<double>(measure_text(cr, ui_font(), "Growth", -1).width));
        }
        double widest_value = 0.0;
        for (const char *value : {"100%", "Down", "Flat"}) {
            widest_value = std::max(widest_value, static_cast<double>(measure_text(cr, ui_font(), value, -1).width));
        }
        const inspect_report::BarLayout bar_layout =
            inspect_report::bar_layout(inner, widest_label, widest_value, static_cast<double>(dp(12)));
        const double bar_x = x + margin + bar_layout.bar_offset;
        const double bar_w = bar_layout.bar_width;
        for (const Bar &bar : bars) {
            paint_text(cr, ui_font(), bar.label, x + margin, cy + dp(2), dim_r, dim_g, dim_b, -1);
            const double track_y = cy + row_h / 2.0 - dp(4);
            round_dialog_rect(cr, bar_x, track_y, bar_w, dp(8), dp(4));
            if (dark) {
                cairo_set_source_rgb(cr, 0x2B / 255.0, 0x34 / 255.0, 0x42 / 255.0);
            } else {
                cairo_set_source_rgb(cr, 0.88, 0.85, 0.78);
            }
            cairo_fill(cr);
            const double t = std::max(0.0, std::min(1.0, bar.value / 255.0));
            if (t > 0.0) {
                round_dialog_rect(cr, bar_x, track_y, std::max(static_cast<double>(dp(8)), bar_w * t), dp(8), dp(4));
                cairo_set_source_rgb(cr, bar.r, bar.g, bar.b);
                cairo_fill(cr);
            }
            const std::string percent = std::to_string(static_cast<int>(std::lround(t * 100.0))) + "%";
            const TextMetrics pm = measure_text(cr, ui_font(), percent, -1);
            paint_text(cr, ui_font(), percent, x + panel.w - margin - pm.width, cy + dp(2), ink_r, ink_g, ink_b, -1);
            text_spans_.push_back(TextSpan{x + margin, cy, inner, row_h});
            cy += row_h;
        }
        if (zone) {
            // Growth is signed: shrinking fills left of the middle in blue.
            paint_text(cr, ui_font(), "Growth", x + margin, cy + dp(2), dim_r, dim_g, dim_b, -1);
            const double track_y = cy + row_h / 2.0 - dp(4);
            round_dialog_rect(cr, bar_x, track_y, bar_w, dp(8), dp(4));
            if (dark) {
                cairo_set_source_rgb(cr, 0x2B / 255.0, 0x34 / 255.0, 0x42 / 255.0);
            } else {
                cairo_set_source_rgb(cr, 0.88, 0.85, 0.78);
            }
            cairo_fill(cr);
            const double mid = bar_x + bar_w / 2.0;
            const double g = std::max(-1.0, std::min(1.0, facts.facts.growth / 200.0));
            // Any change shows at least a sliver, so a slow climb still reads.
            const double reach = std::max(static_cast<double>(dp(4)), bar_w / 2.0 * std::fabs(g));
            if (g > 0.0) {
                fill(cr, mid, track_y, reach, dp(8), 0.36, 0.80, 0.45);
            } else if (g < 0.0) {
                fill(cr, mid - reach, track_y, reach, dp(8), 0.36, 0.55, 0.95);
            }
            fill(cr, mid - 0.5, track_y - dp(2), 1.0, dp(12), ink_r, ink_g, ink_b);
            const char *trend = g > 0.0 ? "Up" : (g < 0.0 ? "Down" : "Flat");
            const TextMetrics tm = measure_text(cr, ui_font(), trend, -1);
            paint_text(cr, ui_font(), trend, x + panel.w - margin - tm.width, cy + dp(2), ink_r, ink_g, ink_b, -1);
            text_spans_.push_back(TextSpan{x + margin, cy, inner, row_h});
            cy += row_h;
        }
        if (!bars.empty()) {
            cy += dp(8);
        }
        if (!reasons_head.empty()) {
            std::string head = reasons_head;
            for (char &ch : head) {
                ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            }
            cy += place_line(tag_font(), head, x + margin, cy, inner, dark ? 0xF2 / 255.0 : 0.55, dark ? 0xC1 / 255.0 : 0.38,
                             dark ? 0x4E / 255.0 : 0.05) +
                  dp(6);
            for (const std::string &reason : report.reasons) {
                cy += place_line(ui_font(), "\u2022  " + reason, x + margin, cy, inner, ink_r, ink_g, ink_b) + dp(4);
            }
        }
        if (facts.offer_power) {
            buttons_at(x, panel.w, y + panel.h - margin - bh, {{"Show _power", kInspectPower}, {"_Close", kClose}});
        } else {
            buttons_at(x, panel.w, y + panel.h - margin - bh, {{"_Close", kClose}});
        }
    } else if (dialog_ == Dialog::Query) {
        const double width = message_width(win_w_, margin, dp(320), dp(480));
        const double body_h = text_block_height(info_body_.empty() ? " " : info_body_, width - 2 * margin);
        const double height = dp(40) + margin + body_h + dp(12) + dp(36) + margin;
        DialogRect panel = place_dialog(win_w_, band_h, width, height, margin);
        if (query_anchored_) {
            panel = place_near(win_w_, band_h, panel.w, panel.h, query_anchor_x_, query_anchor_y_, margin);
        }
        const double query_floor = static_cast<double>(win_h_ - status_h_) - margin;
        if (panel.y + panel.h > query_floor) {
            panel.y = std::max(static_cast<double>(menu_h_) + margin, query_floor - panel.h);
        }
        frame_panel(cr, panel.x, panel.y, panel.w, panel.h, "Inspect");
        {
            std::string body = info_body_;
            const auto split = body.find('\n');
            const std::string head = split == std::string::npos ? body : body.substr(0, split);
            const std::string rest = split == std::string::npos ? std::string() : body.substr(split + 1);
            const double head_h = place_line(bold_font(), head, panel.x + margin, panel.y + dp(48), panel.w - 2 * margin,
                                             theme::kTextR, theme::kTextG, theme::kTextB);
            cairo_set_source_rgb(cr, theme::chrome_select_r(), theme::chrome_select_g(), theme::chrome_select_b());
            cairo_rectangle(cr, panel.x + margin, panel.y + dp(48) + head_h + dp(2), std::min(panel.w - 2 * margin, static_cast<double>(dp(160))),
                            dp(2));
            cairo_fill(cr);
            if (!rest.empty()) {
                label(cr, body_font(), rest, panel.x + margin, panel.y + dp(48) + head_h + dp(8),
                      static_cast<int>(panel.w - 2 * margin));
            }
        }
        if (info_body_.find("Power: off") != std::string::npos) {
            buttons_at(panel.x, panel.w, panel.y + panel.h - margin - dp(36),
                       {{"Show _power", kInspectPower}, {"_Close", kClose}});
        } else {
            buttons_at(panel.x, panel.w, panel.y + panel.h - margin - dp(36), {{"_Close", kClose}});
        }
    }
    if (dialog_ == Dialog::Ledger) {
        const double width = std::min(static_cast<double>(dp(520)), std::max(1.0, win_w_ - 2.0 * margin));
        const double row_h = dp(28);
        const double height = dp(40) + margin + civic::kMilestones * row_h + dp(16) + dp(36) + margin;
        const DialogRect panel = place_dialog(win_w_, band_h, width, height, margin);
        frame_panel(cr, panel.x, panel.y, panel.w, panel.h, "Milestones");
        const civic::Ledger &ledger = session_.ledger();
        double cursor = panel.y + dp(48);
        for (int i = 0; i < civic::kMilestones; ++i) {
            const std::string line = std::string(civic::milestone_title(i)) + "  —  " + civic::milestone_date(ledger.hit[i]);
            place_line(body_font(), line, panel.x + margin, cursor, panel.w - 2.0 * margin, theme::kTextR, theme::kTextG,
                       theme::kTextB);
            cursor += row_h;
        }
        buttons_at(panel.x, panel.w, panel.y + panel.h - margin - dp(36),
                   {{milestone_notes_ ? "Pop-up notes: on" : "Pop-up notes: off", kLedgerNotes}, {"_Close", kClose}});
    }
}

void Shell::draw(cairo_t *cr)
{
    sync_pointer_cursor();
    if (show_osk() && dialog_ != Dialog::None) {
        osk_latched_ = true;
    }
    inspect_card_shown_ = false;
    inspect_card_w_ = 0;
    inspect_card_h_ = 0;
    if (!pointer_down_) {
        palette_.clear_pressed();
    }
    if (gamepad_nav_ && dialog_ == Dialog::None && !menu_.open()) {
        const int under = hit(cursor_x_, cursor_y_);
        if (under == kPaletteId) {
            palette_.set_focus(palette_.index_at(cursor_x_ - side_x_, cursor_y_ - pal_y_));
        } else {
            palette_.set_focus(-1);
        }
    } else {
        palette_.set_focus(-1);
    }
    menu_.recent_count = static_cast<int>(recent_.size());
    if (slate_ && !handheld_) {
        fill(cr, 0, 0, win_w_, win_h_, 0x17 / 255.0, 0x1D / 255.0, 0x26 / 255.0);
    } else {
        fill(cr, 0, 0, win_w_, win_h_, theme::kWindowR, theme::kWindowG, theme::kWindowB);
    }
    if (!handheld_) {
        draw_menubar(cr);
        if (band_h_ > 0) {
            draw_info_band(cr);
        }
        {
            TargetCr live(cr);
            cairo_translate(live.cr, side_x_, pal_y_);
            palette_.set_dimmed(locked_tool_mask());
            palette_.draw(live.cr);
        }
        push_hot(side_x_, pal_y_, side_w_, palette_.height(), kPaletteId);
        if (demand_h_ > 0) {
            TargetCr live(cr);
            cairo_translate(live.cr, side_x_, demand_y_);
            demand_.draw(live.cr, side_w_, demand_h_, ui_scale_);
        }
        if (slate_) {
            stroke_rect(cr, view_x_ - 1, view_y_ - 1, view_w_ + 2, view_h_ + 2, 0x3A / 255.0, 0x46 / 255.0, 0x58 / 255.0);
        } else {
            stroke_rect(cr, view_x_ - 1, view_y_ - 1, view_w_ + 2, view_h_ + 2, theme::kMenuFrameR, theme::kMenuFrameG,
                        theme::kMenuFrameB);
        }
    }
    cairo_save(cr);
    cairo_rectangle(cr, view_x_, view_y_, view_w_, view_h_);
    cairo_clip(cr);
    {
        TargetCr live(cr);
        cairo_rectangle(live.cr, view_x_, view_y_, view_w_, view_h_);
        cairo_clip(live.cr);
        const double map_origin_x = snapped_map_origin(view_x_, map_ox_, scroll_x_);
        const double map_origin_y = snapped_map_origin(view_y_, map_oy_, scroll_y_);
        cairo_rectangle(live.cr, map_origin_x, map_origin_y, map_.pixel_width(), map_.pixel_height());
        cairo_clip(live.cr);
        cairo_translate(live.cr, map_origin_x, map_origin_y);
        if (!map_.dragging() && dialog_ == Dialog::None && !menu_.open()) {
            // The footprint follows the pointer before the button goes down,
            // including while the city is paused. Motion updates the tile
            // under the cursor. It does not move the pointer.
            if (gamepad_nav_ && !pad_dragging_) {
                map_.motion(map_local_x(cursor_x_), map_local_y(cursor_y_), false);
            } else if (!gamepad_nav_ && !pointer_down_ && pointer_x_ >= view_x_ &&
                       pointer_x_ < view_x_ + view_w_ && pointer_y_ >= view_y_ && pointer_y_ < view_y_ + view_h_) {
                map_.motion(map_local_x(pointer_x_), map_local_y(pointer_y_), false);
            }
        }
        refresh_place_cue();
        // Night lighting steps aside under a data map; the colours there carry meaning.
        map_.set_night_hold(overlay_on_);
        map_.sync();
        map_.set_dust(puff_on_, puff_ms_, puff_tx_, puff_ty_);
        map_.draw(live.cr);
        {
            const int ts = std::max(1, map_.tile_size());
            cairo_set_antialias(live.cr, CAIRO_ANTIALIAS_NONE);
            if (marker_on_ && marker_lit(marker_ms_)) {
                const double mx = marker_tx_ * ts;
                const double my = marker_ty_ * ts;
                cairo_set_source_rgb(live.cr, 0, 0, 0);
                cairo_set_line_width(live.cr, 3);
                cairo_rectangle(live.cr, mx - 2, my - 2, ts + 4, ts + 4);
                cairo_stroke(live.cr);
                cairo_set_source_rgb(live.cr, 1, 1, 0.33);
                cairo_set_line_width(live.cr, 1);
                cairo_rectangle(live.cr, mx - 1, my - 1, ts + 2, ts + 2);
                cairo_stroke(live.cr);
            }
            if (pop_on_) {
                const double grow = pop_ms_ < 90 ? pop_ms_ / 90.0 : std::max(0.0, (180.0 - pop_ms_) / 90.0);
                const double pad = 1.0 + grow * 3.0;
                cairo_set_source_rgb(live.cr, 1, 1, 0.4);
                cairo_set_line_width(live.cr, 1);
                cairo_rectangle(live.cr, pop_tx_ * ts - pad, pop_ty_ * ts - pad, pop_tw_ * ts + pad * 2,
                                pop_th_ * ts + pad * 2);
                cairo_stroke(live.cr);
            }
        }
        if (overlay_on_) {
            overlay_.sync();
            if (overlay_.smooth()) {
                // Mute the city's colours under a heat map so the heat reads
                // first and the streets still show through.
                cairo_save(live.cr);
                cairo_rectangle(live.cr, 0, 0, map_.pixel_width(), map_.pixel_height());
                cairo_clip(live.cr);
                cairo_set_operator(live.cr, CAIRO_OPERATOR_HSL_SATURATION);
                cairo_set_source_rgb(live.cr, 0.5, 0.5, 0.5);
                cairo_paint_with_alpha(live.cr, 0.6);
                cairo_restore(live.cr);
            }
            overlay_.draw_at(live.cr, map_.tile_size());
        }
        int query_x = 0;
        int query_y = 0;
        if (dialog_ == Dialog::Query && session_.query_tile(query_x, query_y)) {
            // Outline the whole building the card describes (a 3x3 zone, a
            // 4x4 plant, a landmark), or the single tile when it is not
            // part of one.
            const int ts = std::max(1, map_.tile_size());
            const CitySession::TileInspect card = session_.inspect_tile(query_x, query_y);
            int left = query_x;
            int top = query_y;
            int width = 1;
            int height = 1;
            if (card.zone && card.width > 0 && card.height > 0) {
                left = card.left;
                top = card.top;
                width = card.width;
                height = card.height;
            }
            const double line = std::max(2.0, ts / 5.0);
            const double inset = line / 2.0;
            cairo_rectangle(live.cr, left * ts + inset, top * ts + inset, width * ts - line, height * ts - line);
            cairo_set_source_rgba(live.cr, 0.0, 0.0, 0.0, 0.45);
            cairo_set_line_width(live.cr, line + 2.0);
            cairo_stroke_preserve(live.cr);
            cairo_set_source_rgb(live.cr, theme::kFocusR, theme::kFocusG, theme::kFocusB);
            cairo_set_line_width(live.cr, line);
            cairo_stroke(live.cr);
        }
    }
    cairo_restore(cr);
    if (flash_on_) {
        const double edge = std::max(4.0, static_cast<double>(dp(6)));
        paint_dim_plate(cr, view_x_, view_y_, view_w_, edge, 0.75, 0.05, 0.04);
        paint_dim_plate(cr, view_x_, view_y_ + view_h_ - edge, view_w_, edge, 0.75, 0.05, 0.04);
        paint_dim_plate(cr, view_x_, view_y_, edge, view_h_, 0.75, 0.05, 0.04);
        paint_dim_plate(cr, view_x_ + view_w_ - edge, view_y_, edge, view_h_, 0.75, 0.05, 0.04);
    }
    push_hot(view_x_, view_y_, view_w_, view_h_, kMapId);
    if (handheld_) {
        draw_handheld_hud(cr);
    } else {
        draw_minimap_overlay(cr);
    }
    draw_hover_card(cr);
    if (overlay_on_ && overlay_return_eval_ && dialog_ == Dialog::None) {
        const double chip_w = dp(180);
        const double chip_h = dp(28);
        const double chip_x = view_x_ + dp(8);
        const double chip_y = view_y_ + view_h_ - hud_bottom_ - chip_h - dp(16);
        button(cr, chip_x, chip_y, chip_w, chip_h, "Back to Evaluation", kEvalShow, false);
    }
    if (card_id_ >= 0 && card_id_ < civic::kMilestones && dialog_ == Dialog::None && !map_.dragging()) {
        const double card_w = std::min(static_cast<double>(dp(280)), std::max(120.0, view_w_ * 0.4));
        const double card_h = dp(78);
        const double card_x = view_x_ + view_w_ - hud_right_ - card_w - dp(12);
        const double card_y = view_y_ + view_h_ - hud_bottom_ - card_h - dp(16);
        fill(cr, card_x + 2, card_y + 2, card_w, card_h, 0, 0, 0);
        fill(cr, card_x, card_y, card_w, card_h, 255.0 / 255.0, 255.0 / 255.0, 85.0 / 255.0);
        stroke_rect(cr, card_x, card_y, card_w, card_h, 0, 0, 0);
        paint_text(cr, bold_font(), civic::milestone_title(card_id_), card_x + 8, card_y + 6, 0, 0, 0,
                   static_cast<int>(card_w - 28));
        paint_text(cr, ui_font(), civic::milestone_card(card_id_), card_x + 8, card_y + dp(28), 0, 0, 0,
                   static_cast<int>(card_w - 16));
        push_hot(card_x + card_w - dp(22), card_y + 4, dp(18), dp(18), kCardDismiss);
    }
    const double max_x = std::max(0, map_.pixel_width() - view_w_);
    const double max_y = std::max(0, map_.pixel_height() - view_h_);
    if (max_x > 0 && !handheld_) {
        const double bw = std::max(20.0, view_w_ * (view_w_ / static_cast<double>(map_.pixel_width())));
        const double bx = view_x_ + (view_w_ - bw) * (scroll_x_ / max_x);
        fill(cr, bx, view_y_ + view_h_ - 8, bw, 6, theme::chrome_select_r(), theme::chrome_select_g(), theme::chrome_select_b());
    }
    if (max_y > 0 && !handheld_) {
        const double bh = std::max(20.0, view_h_ * (view_h_ / static_cast<double>(map_.pixel_height())));
        const double by = view_y_ + (view_h_ - bh) * (scroll_y_ / max_y);
        fill(cr, view_x_ + view_w_ - 8, by, 6, bh, theme::chrome_select_r(), theme::chrome_select_g(), theme::chrome_select_b());
    }
    const auto now = std::chrono::steady_clock::now();
    const bool expired = !notice_sticky_ && (hint_until_.time_since_epoch().count() == 0 || now >= hint_until_);
    const NoticeDecision notice = decide_notice(session_.message_serial(), shown_message_serial_, expired, query_pinned_);
    if (notice.show_notice && session_.message().empty() && !notice_sticky_) {
        // A stroke that went on to build retracted "Cannot build here."
        shown_message_serial_ = session_.message_serial();
        shown_message_.clear();
        hint_until_ = now;
        query_pinned_ = false;
    }
    if (notice.show_notice && !session_.message().empty()) {
        shown_message_serial_ = session_.message_serial();
        shown_message_ = session_.message();
        hint_until_ = now + std::chrono::milliseconds(kToastMs);
        query_pinned_ = false;
    }
    sync_toasts(now);
    const std::string hint = context_hint();
    std::string tool_text;
    {
        const bool show_keys = !deck_ && !gamepad_nav_;
        int shown_tool = tool_index_;
        const int hover = palette_.hover();
        const bool hovering = hover >= 0 && palette_.hover_ms() >= 350 && tool_by_index(hover) != nullptr;
        if (hovering) {
            shown_tool = hover;
        } else if (palette_.focus() >= 0) {
            shown_tool = palette_.focus();
        }
        if (shown_tool == kToolCount - 1) {
            // Query is the no-tool inspect mode: nothing to name or price.
            tool_text.clear();
        } else {
            tool_text = tool_hint_now(shown_tool);
        }
        if (show_keys && !tool_text.empty() && shown_tool >= 0 && shown_tool < KeyActCount - KeyTool0 && dialog_ == Dialog::None) {
            const std::string key_name = chord_label(binds_.key[KeyTool0 + shown_tool]);
            if (!key_name.empty() && key_name != "None") {
                tool_text += "  [" + key_name + "]";
            }
        }
        if (dialog_ != Dialog::None || menu_.open()) {
            tool_text.clear();
        }
    }
    const int status_y = win_h_ - status_h_;
    if (handheld_) {
        // The hint strip and the quick bar in the handheld HUD carry these.
    } else if (slate_) {
        draw_news_line(cr, tool_text, hint);
    } else {
        fill(cr, 0, status_y, win_w_, status_h_, theme::kMenuBotR, theme::kMenuBotG, theme::kMenuBotB);
        const std::string clock = session_.scenario_clock_text();
        const std::string meter = session_.scenario_meter();
        // Funds, the name, the speed, the date, the clock, then the goal meter on
        // the left. On the right: the tool under the pointer (or the current
        // tool) with its cost, and before it the hint for what a click does. The
        // hint gives way first when the line is short.
        std::string status = session_.funds_text() + "    " + session_.city_name() + "    " +
                             speed_word(welcome_gate_ ? 0 : speed_);
        const int full_room = std::max(1, win_w_ - 16);
        auto width_of = [&](const char *font, const std::string &text) { return measure_text(cr, font, text, -1).width; };
        auto fits_in = [&](const std::string &text, int room) { return width_of(ui_font(), text) <= room; };
        if (fits_in(status + "    " + session_.date_text(), full_room)) {
            status += "    " + session_.date_text();
        }
        if (!clock.empty() && fits_in(status + "    " + clock, full_room)) {
            status += "    " + clock;
        }
        if (!meter.empty() && fits_in(status + "    " + meter, full_room)) {
            status += "    " + meter;
        }
        const int status_w = width_of(ui_font(), status);
        int right_x = win_w_ - 8;
        const int tool_w = tool_text.empty() ? 0 : width_of(bold_font(), tool_text);
        const bool tool_fits = tool_w > 0 && status_w + 24 + tool_w <= win_w_ - 16;
        if (tool_fits) {
            right_x -= tool_w;
            paint_text(cr, bold_font(), tool_text, right_x, status_y + 8, theme::kTextR, theme::kTextG, theme::kTextB, -1);
            right_x -= dp(20);
        }
        std::string hint_line = hint;
        const int hint_room = right_x - (8 + status_w + 24);
        if (!hint_line.empty() && width_of(ui_font(), hint_line) > hint_room) {
            // Shorten the hint to the room left, or drop it when only a stub fits.
            if (hint_room < dp(120)) {
                hint_line.clear();
            } else {
                const std::string dots = "\u2026";
                while (!hint_line.empty() && width_of(ui_font(), hint_line + dots) > hint_room) {
                    // Drop one whole UTF-8 character: its continuation bytes, then its lead byte.
                    while (!hint_line.empty() && (static_cast<unsigned char>(hint_line.back()) & 0xC0) == 0x80) {
                        hint_line.pop_back();
                    }
                    if (!hint_line.empty()) {
                        hint_line.pop_back();
                    }
                }
                hint_line += dots;
            }
        }
        paint_text(cr, ui_font(), status, 8, status_y + 8, theme::kTextR, theme::kTextG, theme::kTextB, full_room);
        if (!hint_line.empty()) {
            const int hint_w = width_of(ui_font(), hint_line);
            paint_text(cr, ui_font(), hint_line, right_x - hint_w, status_y + 8, theme::kTextR, theme::kTextG, theme::kTextB,
                       -1);
        }
    }
    if (notice_sticky_ && !notice_text_.empty() && now >= notice_until_) {
        dismiss_background_notice();
    }
    draw_toasts(cr, now);
    double look_r = 0;
    double look_g = 0;
    double look_b = 0;
    double look_ink_r = 0;
    double look_ink_g = 0;
    double look_ink_b = 0;
    theme::banner_fill(look_r, look_g, look_b);
    theme::banner_ink(look_ink_r, look_ink_g, look_ink_b);
    const BannerColors goal_bar = goal_banner_colors(BannerColors{look_r, look_g, look_b, look_ink_r, look_ink_g, look_ink_b});
    const std::string goal = session_.scenario_progress();
    if (session_.scenario_id() != goal_scenario_) {
        goal_scenario_ = session_.scenario_id();
        goal_dismissed_ = false;
        goal_until_ = now + std::chrono::seconds(6);
    }
    const bool goal_visible = !goal.empty() && !goal_dismissed_ && now < goal_until_;
    double goal_bottom = 0;
    double goal_left = 0;
    double goal_right = 0;
    // The Slate info band shows the goal all the time, so the desktop
    // Slate layout has no banner to repeat it.
    if (goal_visible && dialog_ == Dialog::None && view_w_ > dp(32) && (handheld_ || !slate_)) {
        const int pad = dp(8);
        double room = 0;
        const bool beside = goal_banner_room(view_w_, pad, inspect_card_w_, inspect_card_shown_, room);
        double gy = view_y_ + pad + hud_top_;
        if (!beside) {
            gy = view_y_ + pad + hud_top_ + inspect_card_h_ + pad;
            room = std::max(0.0, static_cast<double>(view_w_) - pad * 2.0);
        }
        if (mini_h_ > 0) {
            room = std::min(room, static_cast<double>(view_w_ - mini_w_ - hud_right_ - pad * 4));
        }
        const int wrap = std::max(1, static_cast<int>(room) - pad * 2);
        const TextMetrics metrics = measure_text(cr, ui_font(), goal, wrap);
        const double gx = view_x_ + pad;
        const double gw = std::min(room, static_cast<double>(metrics.width + pad * 2));
        const double gh = static_cast<double>(metrics.height + pad * 2);
        double edge_r = 0;
        double edge_g = 0;
        double edge_b = 0;
        theme::banner_edge(edge_r, edge_g, edge_b);
        if (handheld_) {
            // Same chip style as the rest of the handheld HUD.
            round_dialog_rect(cr, gx, gy, gw, gh, dp(8));
            cairo_set_source_rgba(cr, 0x12 / 255.0, 0x17 / 255.0, 0x1F / 255.0, 0.9);
            cairo_fill_preserve(cr);
            cairo_set_source_rgb(cr, 0xC9 / 255.0, 0xA6 / 255.0, 0x46 / 255.0);
            cairo_set_line_width(cr, 1.0);
            cairo_stroke(cr);
            paint_text(cr, ui_font(), goal, gx + pad, gy + pad, 0xEE / 255.0, 0xE8 / 255.0, 0xDA / 255.0, wrap);
        } else {
            fill(cr, gx, gy, gw, gh, goal_bar.r, goal_bar.g, goal_bar.b);
            stroke_rect(cr, gx, gy, gw, gh, edge_r, edge_g, edge_b);
            paint_text(cr, ui_font(), goal, gx + pad, gy + pad, goal_bar.ink_r, goal_bar.ink_g, goal_bar.ink_b, wrap);
        }
        goal_bottom = gy + gh;
        goal_left = gx;
        goal_right = gx + gw;
    }
    if (year_stamp_.visible() && dialog_ == Dialog::None) {
        const std::string stamp = year_stamp_.text();
        const TextMetrics stamp_m = measure_text(cr, bold_font(), stamp, -1);
        const double sw = stamp_m.width + dp(16);
        const double sh = stamp_m.height + dp(10);
        const double sx = view_x_ + (view_w_ - sw) / 2.0;
        double sy = view_y_ + dp(28) + hud_top_;
        if (goal_bottom > 0 && sx < goal_right && sx + sw > goal_left && sy < goal_bottom) {
            sy = goal_bottom + dp(6);
        }
        fill(cr, sx + 2, sy + 2, sw, sh, 0, 0, 0);
        fill(cr, sx, sy, sw, sh, 1, 1, 0.33);
        stroke_rect(cr, sx, sy, sw, sh, 0, 0, 0);
        paint_text(cr, bold_font(), stamp, sx + dp(8), sy + dp(4), 0, 0, 0, -1);
    }
    if (dialog_ != Dialog::None) {
        draw_dialog(cr);
    }
    if (overlay_on_ && dialog_ == Dialog::None) {
        const int pad = dp(8);
        const int wrap = std::max(40, std::min(view_w_ / 2, dp(280)));
        const std::string legend = std::string(overlay_.title()) + "\n" + overlay_.legend();
        const TextMetrics metrics = measure_text(cr, ui_font(), legend, wrap);
        const double lw = metrics.width + pad * 2;
        const double lh = metrics.height + pad * 2 + dp(14);
        const double lx = view_x_ + view_w_ - lw - pad - (handheld_ ? hud_right_ : 0);
        const double ly = view_y_ + pad + minimap_clearance();
        double legend_r = 0;
        double legend_g = 0;
        double legend_b = 0;
        double legend_ink_r = 0;
        double legend_ink_g = 0;
        double legend_ink_b = 0;
        double legend_edge_r = 0;
        double legend_edge_g = 0;
        double legend_edge_b = 0;
        theme::banner_fill(legend_r, legend_g, legend_b);
        theme::banner_ink(legend_ink_r, legend_ink_g, legend_ink_b);
        theme::banner_edge(legend_edge_r, legend_edge_g, legend_edge_b);
        if (slate_ || handheld_) {
            legend_r = 0x1E / 255.0;
            legend_g = 0x26 / 255.0;
            legend_b = 0x31 / 255.0;
            legend_ink_r = 0xEE / 255.0;
            legend_ink_g = 0xE8 / 255.0;
            legend_ink_b = 0xDA / 255.0;
            legend_edge_r = 0xC9 / 255.0;
            legend_edge_g = 0xA6 / 255.0;
            legend_edge_b = 0x46 / 255.0;
        }
        fill(cr, lx, ly, lw, lh, legend_r, legend_g, legend_b);
        stroke_rect(cr, lx, ly, lw, lh, legend_edge_r, legend_edge_g, legend_edge_b);
        paint_text(cr, ui_font(), legend, lx + pad, ly + pad, legend_ink_r, legend_ink_g, legend_ink_b, wrap);
        const double ramp_y = ly + lh - dp(10);
        const CitySession::MapLayer shown = overlay_.layer();
        const bool on_off = shown == CitySession::MapLayer::Residential || shown == CitySession::MapLayer::Commercial ||
                            shown == CitySession::MapLayer::Industrial || shown == CitySession::MapLayer::Transportation ||
                            shown == CitySession::MapLayer::Power;
        if (shown == CitySession::MapLayer::Power) {
            fill(cr, lx + pad, ramp_y, dp(28), dp(6), 220 / 255.0, 40 / 255.0, 32 / 255.0);
            fill(cr, lx + pad + dp(34), ramp_y, dp(28), dp(6), 240 / 255.0, 140 / 255.0, 24 / 255.0);
            fill(cr, lx + pad + dp(68), ramp_y, dp(28), dp(6), 250 / 255.0, 220 / 255.0, 40 / 255.0);
        } else if (!on_off && overlay_.smooth()) {
            // The smooth map's own ramp, at full strength so it reads.
            for (int i = 0; i < 16; ++i) {
                float r = 0;
                float g = 0;
                float b = 0;
                float a = 0;
                heat_field::ramp(0.06f + 0.94f * static_cast<float>(i) / 15.0f, r, g, b, a);
                cairo_set_source_rgba(cr, r, g, b, std::min(1.0f, a + 0.3f));
                cairo_rectangle(cr, lx + pad + i * ((lw - pad * 2) / 16.0), ramp_y, (lw - pad * 2) / 16.0 + 0.5, dp(6));
                cairo_fill(cr);
            }
        } else if (!on_off) {
            for (int i = 0; i < 8; ++i) {
                const double t = i / 7.0;
                cairo_set_source_rgba(cr, 1, 0.86 * (1.0 - t), 0.12 * (1.0 - t), 0.35 + 0.6 * t);
                cairo_rectangle(cr, lx + pad + i * ((lw - pad * 2) / 8.0), ramp_y, (lw - pad * 2) / 8.0, dp(6));
                cairo_fill(cr);
            }
        } else if (shown == CitySession::MapLayer::Transportation) {
            fill(cr, lx + pad, ramp_y, dp(28), dp(6), 48 / 255.0, 48 / 255.0, 52 / 255.0);
            fill(cr, lx + pad + dp(34), ramp_y, dp(28), dp(6), 140 / 255.0, 84 / 255.0, 36 / 255.0);
        } else if (shown == CitySession::MapLayer::Commercial) {
            fill(cr, lx + pad, ramp_y, dp(28), dp(6), 48 / 255.0, 96 / 255.0, 210 / 255.0);
        } else if (shown == CitySession::MapLayer::Industrial) {
            fill(cr, lx + pad, ramp_y, dp(28), dp(6), 230 / 255.0, 190 / 255.0, 32 / 255.0);
        } else {
            fill(cr, lx + pad, ramp_y, dp(14), dp(6), 16 / 255.0, 72 / 255.0, 16 / 255.0);
            fill(cr, lx + pad + dp(14), ramp_y, dp(28), dp(6), 186 / 255.0, 255 / 255.0, 64 / 255.0);
        }
    }
    if ((gamepad_nav_ || key_cursor_) && dialog_ == Dialog::None) {
        double hx = cursor_x_;
        double hy = cursor_y_;
        double arm = 10.0;
        double radius = 9.0;
        int tx = 0;
        int ty = 0;
        const int tile = map_.tile_size();
        // At 8px the old 20px crosshair covers the tile it is aiming at.
        // Sit a short cross on the tile centre, and the footprint outlines
        // that tile before A is pressed.
        if (tile <= kMapTileOverview &&
            map_tile_at(map_local_x(cursor_x_), map_local_y(cursor_y_), map_.shake_x(), map_.shake_y(), tile,
                        CitySession::kWorldW, CitySession::kWorldH, tx, ty)) {
            arm = 3.0;
            radius = 2.5;
            hx = snapped_map_origin(view_x_, map_ox_, scroll_x_) + map_.shake_x() + (tx + 0.5) * tile;
            hy = snapped_map_origin(view_y_, map_oy_, scroll_y_) + map_.shake_y() + (ty + 0.5) * tile;
        }
        auto cross = [&](double width) {
            cairo_set_line_width(cr, width);
            cairo_move_to(cr, hx - arm, hy);
            cairo_line_to(cr, hx + arm, hy);
            cairo_move_to(cr, hx, hy - arm);
            cairo_line_to(cr, hx, hy + arm);
            cairo_stroke(cr);
            cairo_arc(cr, hx, hy, radius, 0, 6.3);
            cairo_stroke(cr);
        };
        // A dark halo keeps the orange cross visible on Classic dirt.
        cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
        cross(tile <= kMapTileOverview ? 3.0 : 4.0);
        cairo_set_source_rgb(cr, theme::kFocusR, theme::kFocusG, theme::kFocusB);
        cross(tile <= kMapTileOverview ? 1.0 : 2.0);
    }
    // Drop-down last, above the map, palette, dialog, and gamepad cursor.
    if (handheld_ && menu_.open()) {
        // The handheld layout hides the menu bar until a menu is open, so the
        // titles show which menu the shoulder buttons have reached.
        draw_menubar(cr);
    }
    draw_menu_dropdown(cr);
}

std::string Shell::dialog_title_hint() const
{
    const bool pad = gamepad_nav_ || deck_ || touch_used_;
    const std::string a = glyph_for(PadConfirm);
    const std::string b = glyph_for(PadCancel);
    const std::string menu = glyph_for(PadMenu);
    const bool osk_key = focus_id_ >= kOsk && focus_id_ < kOsk + 64;
    const bool field = focus_id_ == kFieldName || focus_id_ == kFieldSeed;
    auto b_action = [&](const std::string &typed) -> std::string {
        if (b.empty()) {
            return {};
        }
        if (field_backspace_would_delete(typed, field_caret_, field_anchor_)) {
            return b + " deletes";
        }
        return b + " back";
    };
    auto with_b = [&](const std::string &lead, const std::string &typed, const std::string &tail) {
        return hint_join({lead, b_action(typed), tail});
    };
    if (osk_key) {
        int key_count = 0;
        const OskKey *keys = osk_keys(key_count);
        const int index = focus_id_ - kOsk;
        const std::string &typed = (dialog_ == Dialog::NewCity && new_page_ > 0 && focus_id_ == kFieldSeed) ? field_b_
                                                                                                            : field_a_;
        if (index >= 0 && index < key_count && keys[index].kind == OskKind::Auto) {
            return pad ? with_b(hint_button(a, "picks a name"), typed, hint_button(menu, "confirms"))
                       : "Enter picks a name. Esc closes";
        }
        return pad ? with_b(hint_button(a, "types"), typed, hint_button(menu, "confirms")) : "Enter types. Esc closes";
    }
    if (field) {
        if (dialog_ == Dialog::NewCity && new_page_ > 0 && focus_id_ == kFieldSeed && field_b_ == "auto") {
            return pad ? hint_join({hint_button(a, "types"), hint_button(b, "goes back"), hint_button(menu, "confirms")})
                       : "Type here. Enter confirms. Esc goes back";
        }
        const std::string &typed = focus_id_ == kFieldSeed ? field_b_ : field_a_;
        return pad ? with_b(hint_button(a, "types"), typed, hint_button(menu, "confirms"))
                   : "Type here. Enter confirms. Esc closes";
    }
    if (focus_id_ >= kSlider && focus_id_ < kSlider + 8) {
        const std::string left = glyph_for(PadPrevTool);
        const std::string right = glyph_for(PadNextTool);
        return pad ? hint_join({"D-pad adjusts", hint_pair(left, right, "step"), hint_button(a, "confirms"),
                                hint_button(b, "closes")})
                   : "Left and right adjust. Enter confirms. Esc closes";
    }
    if (dialog_ == Dialog::Messages && session_.notices().empty()) {
        return pad ? hint_join({hint_button(a, "closes"), hint_button(b, "closes")}) : "Enter closes. Esc closes";
    }
    if (dialog_ == Dialog::Messages) {
        return pad ? hint_join({hint_button(a, "jumps"), hint_button(b, "closes"), "D-pad moves"})
                   : "Enter jumps. Esc closes";
    }
    if (dialog_ == Dialog::Files) {
        if (focus_id_ == kCancel) {
            return pad ? hint_join({hint_button(a, "cancels"), hint_button(b, "closes")}) : "Enter cancels. Esc closes";
        }
        if (focus_id_ == kUp) {
            return pad ? hint_join({hint_button(a, "goes up"), hint_button(b, "closes")}) : "Enter goes up. Esc closes";
        }
        if (focus_id_ == kRename) {
            return pad ? hint_join({hint_button(a, "renames"), hint_button(b, "closes")}) : "Enter renames. Esc closes";
        }
        if (focus_id_ == kDelete) {
            return pad ? hint_join({hint_button(a, "deletes"), hint_button(b, "closes")}) : "Enter deletes. Esc closes";
        }
        const char *verb = files_save_ ? "saves" : "opens";
        return pad ? hint_join({hint_button(a, verb), hint_button(b, "closes"), "D-pad moves"})
                   : std::string("Enter ") + verb + ". Esc closes";
    }
    if (dialog_ == Dialog::Scenario) {
        if (focus_id_ == kCancel) {
            return pad ? hint_join({hint_button(a, "cancels"), hint_button(b, "closes")}) : "Enter cancels. Esc closes";
        }
        return pad ? hint_join({hint_button(a, "opens"), hint_button(b, "closes"), "D-pad moves"})
                   : "Enter opens. Esc closes";
    }
    if (dialog_ == Dialog::Controls) {
        return pad ? hint_join({hint_button(a, "closes"), hint_button(b, "closes"), "D-pad pages"})
                   : "Enter closes. Left and right change page";
    }
    if (dialog_ == Dialog::Graphs && focus_id_ == kGraphPlot) {
        return pad ? hint_join({"D-pad reads the chart", hint_button(b, "closes")})
                   : "Left and right read the chart. Esc closes";
    }
    if (dialog_ == Dialog::Bindings) {
        if (bind_listen_) {
            return pad ? hint_join({"Press a button", hint_button(b, "cancels"), "Backspace clears"})
                       : "Press a key. Esc cancels. Backspace clears";
        }
        if (focus_id_ == kCancel) {
            return pad ? hint_join({hint_button(a, "cancels"), hint_button(b, "closes")}) : "Enter cancels. Esc closes";
        }
        if (focus_id_ == kReset) {
            return pad ? hint_join({hint_button(a, "resets"), hint_button(b, "closes")}) : "Enter resets. Esc closes";
        }
        if (focus_id_ == kOk) {
            return pad ? hint_join({hint_button(a, "keeps these"), hint_button(b, "closes")})
                       : "Enter keeps these. Esc closes";
        }
        const bool on_row = focus_id_ >= kListItem && focus_id_ < kListItem + 64;
        if (on_row) {
            return pad ? hint_join({hint_button(a, "listens"), hint_button(glyph_for(PadQuery), "clears"),
                                    hint_button(b, "closes")})
                       : "Enter listens. Backspace clears. Esc closes";
        }
        return pad ? hint_join({hint_button(a, "chooses"), hint_button(b, "closes")}) : "Enter chooses. Esc closes";
    }
    // Graphs, Evaluation, Query, About, and the one-button notices have
    // nothing to confirm. Enter dismisses them. B always has a way out.
    const bool closes = dialog_ == Dialog::Graphs || dialog_ == Dialog::Evaluation || dialog_ == Dialog::Query ||
                        dialog_ == Dialog::About || dialog_ == Dialog::Info || dialog_ == Dialog::LoadError ||
                        dialog_ == Dialog::SaveError || dialog_ == Dialog::HowToPlay;
    if (closes) {
        return pad ? hint_join({hint_button(a, "closes"), hint_button(b, "closes")}) : "Enter closes. Esc closes";
    }
    if (focus_id_ == kCancel) {
        return pad ? hint_join({hint_button(a, "cancels"), hint_button(b, "closes")}) : "Enter cancels. Esc closes";
    }
    if (dialog_ == Dialog::NewCity && new_page_ > 0) {
        return pad ? hint_join({hint_button(a, "confirms"), hint_button(b, "goes back")})
                   : "Enter confirms. Esc goes back";
    }
    return pad ? hint_join({hint_button(a, "confirms"), hint_button(b, "closes")}) : "Enter confirms. Esc closes";
}

void Shell::sync_toasts(std::chrono::steady_clock::time_point now)
{
    // shown_message_ and hint_until_ still say what the latest alert is.
    // A new deadline means a new alert (or the same one again); a deadline
    // already passed means the alert was taken back, so its toast goes too.
    if (hint_until_ != toast_seen_until_) {
        toast_seen_until_ = hint_until_;
        if (hint_until_ > now && !shown_message_.empty()) {
            const auto until = now + std::chrono::milliseconds(kToastMs);
            if (!toasts_.empty() && toasts_.back().text == shown_message_) {
                toasts_.back().until = until;
            } else {
                toasts_.push_back(Toast{shown_message_, notice_is_disaster(shown_message_), until});
                if (toasts_.size() > kToastMax) {
                    toasts_.erase(toasts_.begin());
                }
            }
        } else if (!toasts_.empty() && toasts_.back().text == toast_last_text_) {
            toasts_.pop_back();
        }
        toast_last_text_ = shown_message_;
    }
    toasts_.erase(std::remove_if(toasts_.begin(), toasts_.end(), [&](const Toast &toast) { return toast.until <= now; }),
                  toasts_.end());
}

int Shell::toast_wake_ms(std::chrono::steady_clock::time_point now) const
{
    // Sleep until the next fade starts, then redraw at about 30 Hz while it runs.
    int wake = 500;
    for (const Toast &toast : toasts_) {
        const auto fade_at = toast.until - std::chrono::milliseconds(kToastFadeMs);
        if (now >= fade_at) {
            return 33;
        }
        wake = std::min(wake, static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(fade_at - now).count()));
    }
    return std::max(1, wake);
}

void Shell::draw_toasts(cairo_t *cr, std::chrono::steady_clock::time_point now)
{
    notice_bar_x_ = -1;
    notice_bar_y_ = -1;
    notice_bar_w_ = 0;
    notice_bar_h_ = 0;
    const bool sticky = notice_sticky_ && !notice_text_.empty();
    if ((toasts_.empty() && !sticky) || view_w_ < dp(64) || view_h_ < dp(48)) {
        return;
    }
    // Bottom-right of the map, clear of the scroll bars, stacking upward:
    // the sticky notice nearest the corner, then the newest alert. The map
    // clip keeps them off the toolbar and the status line.
    const double edge = dp(16);
    const double max_w = std::min(static_cast<double>(dp(460)), std::max(static_cast<double>(dp(160)), view_w_ * 0.6));
    const double pad_x = dp(12);
    const double pad_y = dp(8);
    const double gap = dp(8);
    double bottom = view_y_ + view_h_ - edge - hud_bottom_;
    if (card_id_ >= 0 && card_id_ < civic::kMilestones && dialog_ == Dialog::None && !map_.dragging()) {
        bottom -= dp(78) + gap;
    }
    // Desktop alerts stop under the minimap overlay in the top-right corner.
    const double top_limit = view_y_ + dp(8) + (handheld_ ? hud_top_ : minimap_clearance());
    double look_r = 0;
    double look_g = 0;
    double look_b = 0;
    double ink_r = 0;
    double ink_g = 0;
    double ink_b = 0;
    theme::banner_fill(look_r, look_g, look_b);
    theme::banner_ink(ink_r, ink_g, ink_b);
    if (slate_ || handheld_) {
        look_r = 0x1E / 255.0;
        look_g = 0x26 / 255.0;
        look_b = 0x31 / 255.0;
        ink_r = 0xEE / 255.0;
        ink_g = 0xE8 / 255.0;
        ink_b = 0xDA / 255.0;
    }
    const BannerColors look{look_r, look_g, look_b, ink_r, ink_g, ink_b};
    cairo_save(cr);
    cairo_rectangle(cr, view_x_, view_y_, view_w_, view_h_);
    cairo_clip(cr);
    auto plate = [&](const std::string &text, const BannerColors &colors, double frame_r, double frame_g, double frame_b,
                     bool stripe, double alpha, double &x_out, double &y_out, double &w_out, double &h_out) {
        const int wrap = static_cast<int>(max_w - 2 * pad_x);
        const TextMetrics metrics = measure_text(cr, body_font(), text, wrap);
        const double w = std::min(max_w, metrics.width + 2 * pad_x);
        const double h = metrics.height + 2 * pad_y;
        const double x = view_x_ + view_w_ - edge - w - hud_right_;
        const double y = bottom - h;
        if (y < top_limit) {
            return false;
        }
        cairo_push_group(cr);
        cairo_set_source_rgba(cr, 0, 0, 0, 0.35);
        round_dialog_rect(cr, x + 2, y + 3, w, h, dp(6));
        cairo_fill(cr);
        round_dialog_rect(cr, x, y, w, h, dp(6));
        cairo_set_source_rgb(cr, colors.r, colors.g, colors.b);
        cairo_fill_preserve(cr);
        cairo_set_source_rgb(cr, frame_r, frame_g, frame_b);
        cairo_set_line_width(cr, 1.5);
        cairo_stroke(cr);
        if (stripe) {
            cairo_save(cr);
            round_dialog_rect(cr, x, y, w, h, dp(6));
            cairo_clip(cr);
            fill(cr, x, y, dp(4), h, 1.0, 0.85, 0.2);
            cairo_restore(cr);
        }
        paint_text(cr, body_font(), text, x + pad_x, y + pad_y, colors.ink_r, colors.ink_g, colors.ink_b, wrap);
        cairo_pop_group_to_source(cr);
        cairo_paint_with_alpha(cr, alpha);
        x_out = x;
        y_out = y;
        w_out = w;
        h_out = h;
        bottom = y - gap;
        return true;
    };
    double x = 0;
    double y = 0;
    double w = 0;
    double h = 0;
    if (sticky) {
        const BannerColors cream{1.0, 0.96, 0.72, 0.25, 0.12, 0.04};
        if (plate(notice_text_, cream, 0.55, 0.32, 0.08, false, 1.0, x, y, w, h)) {
            notice_bar_x_ = x;
            notice_bar_y_ = y;
            notice_bar_w_ = w;
            notice_bar_h_ = h;
        }
    }
    for (auto it = toasts_.rbegin(); it != toasts_.rend(); ++it) {
        const double left_ms =
            static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(it->until - now).count());
        const double alpha = std::max(0.0, std::min(1.0, left_ms / kToastFadeMs));
        if (alpha <= 0.0) {
            continue;
        }
        const BannerColors colors = message_bar_colors(it->disaster, look);
        if (!plate(it->text, colors, 0, 0, 0, it->disaster, alpha, x, y, w, h)) {
            break;
        }
    }
    cairo_restore(cr);
}

std::string Shell::context_hint() const
{
    const bool pad = gamepad_nav_ || deck_;
    const bool finger = touch_used_ && !pad;
    const std::string a = glyph_for(PadConfirm);
    const std::string b = glyph_for(PadCancel);
    const std::string left = glyph_for(PadPrevTool);
    const std::string right = glyph_for(PadNextTool);
    const std::string menu = glyph_for(PadMenu);
    if (menu_.open()) {
        return pad ? hint_join({hint_button(a, "chooses"), hint_button(b, "closes"), "D-pad moves"})
                   : "Enter chooses. Esc closes";
    }
    if (dialog_ != Dialog::None) {
        // Pad players read this in the title bar. Mouse players read it here.
        if (pad || finger) {
            return {};
        }
        return dialog_title_hint();
    }
    const ToolDef *tool = tool_by_index(tool_index_);
    const int engine = tool != nullptr ? tool->engine_id : TOOL_QUERY;
    // With Query (no tool picked) the map hint is left out entirely; the
    // status line's right side stays empty.
    const bool tool_hint = tool_has_status_hint(engine);
    if (overlay_on_) {
        if (!tool_hint) {
            return pad ? hint_join({hint_button(b, "closes the map"), "D-pad scrolls"}) : "Esc closes the map";
        }
        return pad ? hint_join({tool_press_text(tool, a.c_str()), hint_button(b, "closes the map"), "D-pad scrolls"})
                   : tool_click_text(tool) + ". Esc closes the map";
    }
    if (pad) {
        const int under = hit(cursor_x_, cursor_y_);
        if (under == kPaletteId) {
            std::string tools = "Tools";
            if (!a.empty()) {
                tools += ": " + a + " selects";
            }
            return hint_join({tools, hint_pair(left, right, "cycle"), hint_button(menu, "menu")});
        }
        if (under == kMinimapId) {
            std::string mini = "Minimap";
            if (!a.empty()) {
                mini += ": " + a + " jumps";
            }
            return hint_join({mini, "D-pad scrolls", hint_button(menu, "menu")});
        }
        if (under >= kMenuTitle && under < kMenuTitle + 8) {
            return a.empty() ? std::string("Opens the menu") : a + " opens the menu";
        }
        if (cursor_y_ < static_cast<float>(menu_h_) || cursor_x_ < static_cast<float>(view_x_)) {
            return hint_join({"Pointing at the panel", hint_button(a, "uses it"), hint_button(menu, "menu")});
        }
        if (!tool_hint) {
            return {};
        }
        const std::string query = glyph_for(PadQuery);
        const std::string pause = glyph_for(PadPause);
        return hint_join({tool_press_text(tool, a.c_str()), hint_button(query, "inspects"),
                          hint_button(pause, speed_ <= 0 ? "resumes" : "pauses"), hint_pair(left, right, "tool"),
                          hint_button(menu, "menu")});
    }
    if (!tool_hint) {
        return {};
    }
    if (finger) {
        std::string phrase = tool_click_text(tool);
        const auto at = phrase.find("click to ");
        if (at != std::string::npos) {
            phrase.replace(at, 9, "tap to ");
        }
        return phrase;
    }
    const std::string pause_key = chord_label(binds_.key[KeyPause]);
    std::string line = tool_click_text(tool) + ". Arrows pan";
    if (!pause_key.empty() && pause_key != "None") {
        line += ". " + pause_key + (speed_ <= 0 ? " resumes" : " pauses");
    }
    return line;
}

std::string Shell::idle_message() const
{
    // The tool phrase lives in the status hint when that hint fits. Repeating
    // it here made the same words appear twice on a wide window.
    return session_.date_text();
}

namespace {

cairo_surface_t *welcome_art()
{
    static cairo_surface_t *image = nullptr;
    static bool tried = false;
    if (tried) {
        return image;
    }
    tried = true;
    const std::string path = ui_data_file("welcome/welcome-bg.png");
    if (path.empty()) {
        return nullptr;
    }
    cairo_surface_t *loaded = cairo_image_surface_create_from_png(path.c_str());
    if (loaded != nullptr && cairo_surface_status(loaded) == CAIRO_STATUS_SUCCESS &&
        cairo_image_surface_get_width(loaded) > 0 && cairo_image_surface_get_height(loaded) > 0) {
        image = loaded;
    } else if (loaded != nullptr) {
        cairo_surface_destroy(loaded);
    }
    return image;
}

cairo_surface_t *paper_grain()
{
    static cairo_surface_t *tile = nullptr;
    if (tile != nullptr) {
        return tile;
    }
    constexpr int side = 96;
    tile = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, side, side);
    unsigned char *pixels = cairo_image_surface_get_data(tile);
    const int stride = cairo_image_surface_get_stride(tile);
    for (int y = 0; y < side; ++y) {
        unsigned char *row = pixels + y * stride;
        for (int x = 0; x < side; ++x) {
            const unsigned hash = static_cast<unsigned>(x) * 374761393u + static_cast<unsigned>(y) * 668265263u + 0x51u;
            unsigned char *pix = row + x * 4;
            pix[0] = pix[1] = pix[2] = pix[3] = 0;
            if ((hash & 15u) != 0) {
                continue;
            }
            const int alpha = 10 + static_cast<int>((hash >> 8) & 14u);
            const int tone = 80 + static_cast<int>((hash >> 16) & 30u);
            pix[0] = static_cast<unsigned char>(tone * alpha / 255);
            pix[1] = static_cast<unsigned char>((tone - 12) * alpha / 255);
            pix[2] = static_cast<unsigned char>((tone - 28) * alpha / 255);
            pix[3] = static_cast<unsigned char>(alpha);
        }
    }
    cairo_surface_mark_dirty(tile);
    return tile;
}

void round_rect(cairo_t *cr, double x, double y, double w, double h, double radius)
{
    const double rr = std::min(radius, std::min(w, h) / 2.0);
    cairo_new_path(cr);
    cairo_arc(cr, x + w - rr, y + rr, rr, -1.5708, 0);
    cairo_arc(cr, x + w - rr, y + h - rr, rr, 0, 1.5708);
    cairo_arc(cr, x + rr, y + h - rr, rr, 1.5708, 3.1416);
    cairo_arc(cr, x + rr, y + rr, rr, 3.1416, 4.7124);
    cairo_close_path(cr);
}

void filigree(cairo_t *cr, double x, double y, int corner, double r, double g, double b)
{
    cairo_save(cr);
    cairo_translate(cr, x, y);
    if (corner == 1 || corner == 3) {
        cairo_scale(cr, -1, 1);
    }
    if (corner >= 2) {
        cairo_scale(cr, 1, -1);
    }
    cairo_scale(cr, 0.72, 0.72);
    cairo_set_source_rgb(cr, r, g, b);
    cairo_set_line_width(cr, 1.3);
    cairo_arc(cr, 11, 11, 8, 3.1416, 4.7124);
    cairo_stroke(cr);
    cairo_arc(cr, 18, 7, 3.5, 3.1416, 4.7124);
    cairo_stroke(cr);
    cairo_move_to(cr, 3.5, 16);
    cairo_line_to(cr, 3.5, 3.5);
    cairo_line_to(cr, 16, 3.5);
    cairo_stroke(cr);
    cairo_move_to(cr, 13, 13);
    cairo_line_to(cr, 17, 17);
    cairo_line_to(cr, 13, 21);
    cairo_line_to(cr, 9, 17);
    cairo_close_path(cr);
    cairo_fill(cr);
    cairo_restore(cr);
}

} // namespace

void Shell::draw_welcome_background(cairo_t *cr)
{
    cairo_surface_t *image = welcome_art();
    if (image == nullptr || win_w_ < 2 || win_h_ < 2) {
        cairo_set_source_rgb(cr, 0.05, 0.28, 0.72);
        cairo_paint(cr);
        return;
    }
    const double iw = cairo_image_surface_get_width(image);
    const double ih = cairo_image_surface_get_height(image);
    const double scale = std::max(static_cast<double>(win_w_) / iw, static_cast<double>(win_h_) / ih);
    const double drawn_w = iw * scale;
    const double drawn_h = ih * scale;
    const double ox = (static_cast<double>(win_w_) - drawn_w) / 2.0;
    const double extra = drawn_h - static_cast<double>(win_h_);
    // The window is wider than the art, so the extra height is cropped.
    // Keep the gold title: take a little off the top and the rest off the bottom.
    const double oy = extra > 0.0 ? -extra * 0.12 : (static_cast<double>(win_h_) - drawn_h) / 2.0;
    cairo_save(cr);
    cairo_rectangle(cr, 0, 0, win_w_, win_h_);
    cairo_clip(cr);
    cairo_translate(cr, ox, oy);
    cairo_scale(cr, scale, scale);
    cairo_set_source_surface(cr, image, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BEST);
    cairo_paint(cr);
    cairo_restore(cr);
}

void Shell::draw_year_cover(cairo_t *cr, double margin, double band_h)
{
    const civic::YearDigest digest = year_digest();
    const double navy_r = 0.02;
    const double navy_g = 0.08;
    const double navy_b = 0.32;
    const double gold_r = 0.72;
    const double gold_g = 0.55;
    const double gold_b = 0.18;
    const double gold_hi_r = 0.91;
    const double gold_hi_g = 0.78;
    const double gold_hi_b = 0.46;
    const double paper_r = 0.976;
    const double paper_g = 0.922;
    const double paper_b = 0.808;
    const double card_r = 0.980;
    const double card_g = 0.925;
    const double card_b = 0.812;
    const double ink_r = 0.16;
    const double ink_g = 0.07;
    const double ink_b = 0.03;
    const double title_r = 0.45;
    const double title_g = 0.04;
    const double title_b = 0.02;
    const double funds_r = 0.10;
    const double funds_g = 0.31;
    const double funds_b = 0.04;
    const double people_r = 0.05;
    const double people_g = 0.12;
    const double people_b = 0.41;
    const double standing_r = 0.33;
    const double standing_g = 0.13;
    const double standing_b = 0.34;
    const double body_r = 0.12;
    const double body_g = 0.09;
    const double body_b = 0.06;

    cairo_set_source_rgba(cr, 0, 0, 0, 0.28);
    cairo_rectangle(cr, 0, menu_h_, win_w_, std::max(0.0, band_h - menu_h_));
    cairo_fill(cr);
    push_hot(0, 0, win_w_, win_h_, 0);

    double top_limit = menu_h_ + dp(6);
    double bottom_limit = band_h - dp(6);
    double avail_h = std::max(1.0, bottom_limit - top_limit);
    const double avail_w = std::max(1.0, static_cast<double>(win_w_) - 2.0 * margin);
    const double panel_w = std::min(static_cast<double>(dp(900)), avail_w);
    const double frame = std::max(6.0, static_cast<double>(dp(8)));
    const double gap = dp(8);
    const double button_h = static_cast<double>(control_px(36, 42));
    const double inner_w_est = std::max(1.0, panel_w - 2.0 * frame - dp(62));
    const double card_w_est = std::max(1.0, (inner_w_est - 2.0 * gap) / 3.0);
    const double col_w_est = std::max(1.0, (inner_w_est - gap) / 2.0);
    const double left_w_est = std::max(1.0, inner_w_est * 0.46);
    const double right_w_est = std::max(1.0, inner_w_est - inner_w_est * 0.50);
    const std::string year_text = std::to_string(digest.ended_year);
    const std::string who = session_.city_name();
    const CitySession::Evaluation report = session_.evaluation();
    auto plain_number = [](std::int64_t amount) {
        std::string with = civic::dollars(amount);
        if (!with.empty() && with[0] == '$') {
            with.erase(with.begin());
        }
        return with;
    };
    const std::string pop_line = "Population " + plain_number(report.population);
    const std::string tax_line = "Tax " + std::to_string(digest.tax_percent) + "%";
    const CitySession::CensusMix mix = session_.census_mix();
    const civic::ReviewText review = civic::make_review_text(digest, mix.homes, mix.shops, mix.jobs, report.no_residents,
                                                             report.category, report.score, report.yes_percent);
    const std::string &funds_body = review.funds;
    const std::string &people_body = review.people;
    const std::string &standing = review.standing;
    const std::string &highlights = review.highlights;
    const std::string &challenges = review.challenges;
    // A short window used to shrink the top boxes and drop Deficit and the
    // census line. Try the natural card, then a shorter header and tighter
    // padding, then the message strip, then one step down in type. The boxes
    // stay as tall as the text that was measured for the step that fits.
    struct YearFace {
        int kicker = 15;
        int year = 40;
        int city = 20;
        int meta = 14;
        int card = 23;
        int body = 14;
        double paper = 0;
        double after_header = 0;
        double before_buttons = 0;
        double after_buttons = 0;
        double box_top = 0;
        double box_mid = 0;
        double box_bot = 0;
    };
    struct YearMeasure {
        double content = 0;
        double header = 0;
        double top = 0;
        double low = 0;
        int kicker_h = 0;
        int year_h = 0;
        int city_h = 0;
        int meta_h = 0;
        std::string kicker_font;
        std::string year_font;
        std::string city_font;
        std::string meta_font;
        std::string card_font;
        std::string body_font;
    };
    auto face_at = [&](int kicker, int year, int city, int meta, int card, int body, int paper, int after_header,
                       int before_buttons, int after_buttons, int box_top_px, int box_mid, int box_bot) {
        YearFace face;
        face.kicker = kicker;
        face.year = year;
        face.city = city;
        face.meta = meta;
        face.card = card;
        face.body = body;
        face.paper = paper;
        face.after_header = after_header;
        face.before_buttons = before_buttons;
        face.after_buttons = after_buttons;
        face.box_top = box_top_px;
        face.box_mid = box_mid;
        face.box_bot = box_bot;
        return face;
    };
    const YearFace natural = face_at(15, 40, 20, 14, 23, 14, dp(16), dp(10), dp(12), dp(16), dp(6), dp(8), dp(18));
    const YearFace compact = face_at(13, 30, 16, 12, 18, 14, dp(8), dp(4), dp(6), dp(8), dp(4), dp(4), dp(8));
    YearFace small = face_at(12, 24, 15, 12, 16, 12, dp(6), dp(4), dp(6), dp(6), dp(3), dp(3), dp(6));
    auto measure_face = [&](const YearFace &face) {
        YearMeasure out;
        out.kicker_font = ui_scale::scaled_font("DejaVu Serif Bold", face.kicker, ui_scale_);
        out.year_font = ui_scale::scaled_font("DejaVu Serif Bold", face.year, ui_scale_);
        out.city_font = ui_scale::scaled_font("DejaVu Serif Bold", face.city, ui_scale_);
        out.meta_font = ui_scale::scaled_font("DejaVu Serif", face.meta, ui_scale_);
        out.card_font = ui_scale::scaled_font("DejaVu Serif Bold", face.card, ui_scale_);
        out.body_font = ui_scale::scaled_font("Sans", face.body, ui_scale_);
        out.kicker_h = measure_text(cr, out.kicker_font.c_str(), "YEAR IN REVIEW", static_cast<int>(left_w_est)).height;
        out.year_h = measure_text(cr, out.year_font.c_str(), year_text, static_cast<int>(left_w_est)).height;
        out.city_h = measure_text(cr, out.city_font.c_str(), who, static_cast<int>(right_w_est)).height;
        out.meta_h = std::max(measure_text(cr, out.meta_font.c_str(), pop_line, static_cast<int>(right_w_est)).height,
                              measure_text(cr, out.meta_font.c_str(), tax_line, static_cast<int>(right_w_est)).height);
        out.header = std::max(static_cast<double>(out.kicker_h + dp(2) + out.year_h),
                              static_cast<double>(out.city_h + dp(4) + out.meta_h + dp(2) + out.meta_h));
        auto card_need = [&](const char *title, const std::string &body, double w) {
            const int wrap = static_cast<int>(std::max(1.0, w - dp(20)));
            const int title_h = measure_text(cr, out.card_font.c_str(), title, wrap).height;
            const std::string shown = body.empty() ? std::string(" ") : body;
            const int body_h = measure_text(cr, out.body_font.c_str(), shown, wrap).height;
            return face.box_top + static_cast<double>(title_h) + face.box_mid + static_cast<double>(body_h) + face.box_bot;
        };
        out.top = std::max(card_need("Funds", funds_body, card_w_est),
                           std::max(card_need("People", people_body, card_w_est),
                                    card_need("Standing", standing, card_w_est)));
        out.low = std::max(card_need("Highlights", highlights, col_w_est), card_need("Challenges", challenges, col_w_est));
        const double chrome = 2.0 * frame + face.paper + out.header + face.after_header + face.before_buttons + button_h +
                              face.after_buttons;
        out.content = chrome + out.top + gap + out.low;
        return out;
    };
    const double band_avail = avail_h;
    const double window_top = static_cast<double>(menu_h_) + dp(2);
    const double window_bottom = static_cast<double>(win_h_ - status_h_) - dp(2);
    const double window_avail = std::max(band_avail, window_bottom - window_top);
    YearMeasure natural_m = measure_face(natural);
    YearMeasure compact_m = measure_face(compact);
    YearMeasure small_m = measure_face(small);
    int tier = year_card_tier(band_avail, window_avail, natural_m.content, compact_m.content, small_m.content);
    YearFace face = tier == 0 ? natural : (tier >= 3 ? small : compact);
    YearMeasure fitted = tier == 0 ? natural_m : (tier >= 3 ? small_m : compact_m);
    if (tier >= 3) {
        int guard = 0;
        while (fitted.content > window_avail + 0.5 && face.body > 10 && guard < 8) {
            face.body -= 1;
            face.card = std::max(12, face.card - 1);
            face.year = std::max(18, face.year - 2);
            face.paper = std::max(4.0, face.paper - 2.0);
            face.after_buttons = std::max(4.0, face.after_buttons - 1.0);
            fitted = measure_face(face);
            ++guard;
        }
    }
    if (tier >= 2) {
        top_limit = window_top;
        bottom_limit = window_bottom;
        avail_h = std::max(1.0, bottom_limit - top_limit);
        cairo_set_source_rgba(cr, 0, 0, 0, 0.28);
        cairo_rectangle(cr, 0, band_h, win_w_, std::max(0.0, bottom_limit - band_h));
        cairo_fill(cr);
    }
    const std::string serif_kicker = fitted.kicker_font;
    const std::string serif_year = fitted.year_font;
    const std::string serif_city = fitted.city_font;
    const std::string serif_meta = fitted.meta_font;
    const std::string serif_card = fitted.card_font;
    const std::string year_body_font = fitted.body_font;
    const int kicker_h = fitted.kicker_h;
    const int year_h = fitted.year_h;
    const int city_h = fitted.city_h;
    const int meta_h = fitted.meta_h;
    const double top_need = fitted.top;
    const double low_need = fitted.low;
    const double paper_pad = face.paper;
    const double header_gap = face.after_header;
    const double button_gap = face.before_buttons;
    const double box_top = face.box_top;
    const double box_mid = face.box_mid;
    const double top_h = top_need;
    const double low_h = low_need;
    year_box_shortfall_ = std::max(0.0, fitted.content - avail_h);
    const double panel_h = year_card_panel_height(avail_h, fitted.content - top_need - gap - low_need, top_need, low_need, gap);
    const double panel_x = (static_cast<double>(win_w_) - panel_w) / 2.0;
    const double panel_y = top_limit + (avail_h - panel_h) / 2.0;
    const double paper_x = panel_x + frame;
    const double paper_y = panel_y + frame;
    const double paper_w = panel_w - 2.0 * frame;
    const double paper_h = panel_h - 2.0 * frame;

    fill(cr, panel_x, panel_y, panel_w, panel_h, navy_r, navy_g, navy_b);
    stroke_rect(cr, panel_x + 1.5, panel_y + 1.5, panel_w - 3, panel_h - 3, gold_r, gold_g, gold_b);
    fill(cr, paper_x, paper_y, paper_w, paper_h, paper_r, paper_g, paper_b);
    cairo_save(cr);
    cairo_rectangle(cr, paper_x, paper_y, paper_w, paper_h);
    cairo_clip(cr);
    cairo_set_source_surface(cr, paper_grain(), paper_x, paper_y);
    cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_REPEAT);
    cairo_paint(cr);
    cairo_restore(cr);
    stroke_rect(cr, paper_x + 0.5, paper_y + 0.5, paper_w - 1, paper_h - 1, gold_r, gold_g, gold_b);
    stroke_rect(cr, paper_x + 3.5, paper_y + 3.5, paper_w - 7, paper_h - 7, gold_hi_r, gold_hi_g, gold_hi_b);
    filigree(cr, paper_x + dp(8), paper_y + dp(8), 0, gold_r, gold_g, gold_b);
    filigree(cr, paper_x + paper_w - dp(8), paper_y + dp(8), 1, gold_r, gold_g, gold_b);
    filigree(cr, paper_x + dp(8), paper_y + paper_h - dp(8), 2, gold_r, gold_g, gold_b);
    filigree(cr, paper_x + paper_w - dp(8), paper_y + paper_h - dp(8), 3, gold_r, gold_g, gold_b);

    const double inner_x = paper_x + dp(34);
    const double inner_w = std::max(1.0, paper_w - dp(62));
    const double left_w = std::max(1.0, inner_w * 0.46);
    const double right_x = inner_x + inner_w * 0.50;
    const double right_w = std::max(1.0, inner_x + inner_w - right_x);
    double cursor = paper_y + paper_pad;
    paint_text(cr, serif_kicker.c_str(), "YEAR IN REVIEW", inner_x, cursor, title_r, title_g, title_b,
               static_cast<int>(left_w));
    paint_text(cr, serif_city.c_str(), who, right_x, cursor, ink_r, ink_g, ink_b, static_cast<int>(right_w));
    paint_text(cr, serif_year.c_str(), year_text, inner_x, cursor + kicker_h + dp(2), title_r, title_g, title_b,
               static_cast<int>(left_w));
    paint_text(cr, serif_meta.c_str(), pop_line, right_x, cursor + city_h + dp(4), ink_r, ink_g, ink_b,
               static_cast<int>(right_w));
    paint_text(cr, serif_meta.c_str(), tax_line, right_x, cursor + city_h + dp(4) + meta_h + dp(2), ink_r, ink_g, ink_b,
               static_cast<int>(right_w));
    const double header_bottom = cursor + std::max(static_cast<double>(kicker_h + dp(2) + year_h),
                                                    static_cast<double>(city_h + dp(4) + meta_h + dp(2) + meta_h));
    cursor = header_bottom + header_gap;

    const double card_w = std::max(1.0, (inner_w - 2.0 * gap) / 3.0);
    const double col_w = std::max(1.0, (inner_w - gap) / 2.0);

    auto card_box = [&](double x, double y, double w, double h, const char *title, const std::string &body, double tr,
                        double tg, double tb) {
        fill(cr, x, y, w, h, card_r, card_g, card_b);
        stroke_rect(cr, x + 0.5, y + 0.5, w - 1, h - 1, gold_r, gold_g, gold_b);
        cairo_save(cr);
        cairo_rectangle(cr, x + 1, y + 1, std::max(1.0, w - 2), std::max(1.0, h - 2));
        cairo_clip(cr);
        const int title_w = static_cast<int>(std::max(1.0, w - dp(20)));
        const int title_h = measure_text(cr, serif_card.c_str(), title, title_w).height;
        paint_text(cr, serif_card.c_str(), title, x + dp(10), y + box_top, tr, tg, tb, title_w);
        const int body_w = static_cast<int>(std::max(1.0, w - dp(20)));
        paint_text(cr, year_body_font.c_str(), body, x + dp(10), y + box_top + title_h + box_mid, body_r, body_g, body_b,
                   body_w);
        cairo_restore(cr);
    };

    card_box(inner_x, cursor, card_w, top_h, "Funds", funds_body, funds_r, funds_g, funds_b);
    card_box(inner_x + card_w + gap, cursor, card_w, top_h, "People", people_body, people_r, people_g, people_b);
    card_box(inner_x + 2.0 * (card_w + gap), cursor, card_w, top_h, "Standing", standing, standing_r, standing_g,
             standing_b);
    cursor += top_h + gap;
    card_box(inner_x, cursor, col_w, low_h, "Highlights", highlights, funds_r, funds_g, funds_b);
    card_box(inner_x + col_w + gap, cursor, col_w, low_h, "Challenges", challenges, title_r, title_g, title_b);
    const double buttons_top = cursor + low_h + button_gap;

    auto ornate = [&](double x, double y, double w, double h, const char *label, int id, bool primary) {
        push_hot(x, y, w, h, id);
        push_focus(id);
        const bool hot = hover_id_ == id || focus_id_ == id;
        const bool pressed = hot && pointer_down_ && hover_id_ == id;
        round_rect(cr, x, y, w, h, dp(5));
        cairo_pattern_t *shade = cairo_pattern_create_linear(x, y, x, y + h);
        if (primary) {
            cairo_pattern_add_color_stop_rgb(shade, 0, pressed ? 0.08 : 0.20, pressed ? 0.32 : 0.42, pressed ? 0.62 : 0.72);
            cairo_pattern_add_color_stop_rgb(shade, 1, 0.07, 0.24, 0.52);
        } else {
            const double lift = hot ? 0.06 : 0.0;
            cairo_pattern_add_color_stop_rgb(shade, 0, 0.78 + lift, 0.77 + lift, 0.75 + lift);
            cairo_pattern_add_color_stop_rgb(shade, 1, 0.58, 0.57, 0.54);
        }
        cairo_set_source(cr, shade);
        cairo_fill_preserve(cr);
        cairo_pattern_destroy(shade);
        const bool focused = focus_id_ == id;
        if (focused) {
            // Focus is the button's own outline in blue, like the dialog buttons.
            cairo_set_source_rgb(cr, 0.10, 0.35, 0.78);
            cairo_set_line_width(cr, std::max(3.0, static_cast<double>(dp(3))));
        } else {
            cairo_set_source_rgb(cr, gold_r, gold_g, gold_b);
            cairo_set_line_width(cr, std::max(2.0, static_cast<double>(dp(2))));
        }
        cairo_stroke(cr);
        round_rect(cr, x + 2.5, y + 2.5, w - 5, h - 5, dp(4));
        cairo_set_source_rgba(cr, 1, 1, 1, focused && primary ? 1.0 : (primary ? 0.28 : 0.45));
        cairo_set_line_width(cr, focused && primary ? 1.5 : 1);
        cairo_stroke(cr);
        const std::string plain = strip_mnemonic(label);
        const TextMetrics metrics = measure_text(cr, ui_font(), plain, -1);
        const double tx = x + (w - metrics.width) / 2.0;
        const double ty = y + (h - metrics.height) / 2.0;
        if (primary) {
            paint_mnemonic(cr, ui_font(), label, tx, ty, menu_.alt_shown, 1, 1, 1);
        } else {
            paint_mnemonic(cr, ui_font(), label, tx, ty, menu_.alt_shown, body_r, body_g, body_b);
        }
    };

    const int count = digest.locate ? 5 : 4;
    const double side = dp(28);
    const double row_w = std::max(1.0, inner_w - 2.0 * side);
    const double bw = std::max(1.0, (row_w - (count - 1) * gap) / count);
    double bx = inner_x + side;
    ornate(bx, buttons_top, bw, button_h, "_Budget", kYearBudget, true);
    bx += bw + gap;
    ornate(bx, buttons_top, bw, button_h, "_Evaluation", kYearEval, false);
    bx += bw + gap;
    ornate(bx, buttons_top, bw, button_h, "_Graphs", kYearGraphs, false);
    if (digest.locate) {
        bx += bw + gap;
        ornate(bx, buttons_top, bw, button_h, "_Map", kYearLocate, false);
    }
    bx += bw + gap;
    ornate(bx, buttons_top, bw, button_h, "_Close", kClose, false);
}

void Shell::paint_scene_fade()
{
    if (frame_surface_ == nullptr || !scene_fade_.active()) {
        return;
    }
    cairo_surface_flush(frame_surface_);
    const int width = cairo_image_surface_get_width(frame_surface_);
    const int height = cairo_image_surface_get_height(frame_surface_);
    const int stride = cairo_image_surface_get_stride(frame_surface_);
    unsigned char *data = cairo_image_surface_get_data(frame_surface_);
    if (data == nullptr || width < 1 || height < 1) {
        return;
    }
    for (int y = 0; y < height; ++y) {
        unsigned char *row = data + y * stride;
        for (int x = 0; x < width; ++x) {
            if (!scene_fade_.darken(x, y)) {
                continue;
            }
            unsigned char *pix = row + x * 4;
            pix[0] = 20;
            pix[1] = 20;
            pix[2] = 20;
            pix[3] = 255;
        }
    }
    cairo_surface_mark_dirty(frame_surface_);
}

void Shell::write_final_shot()
{
    if (!quitting_ || shot_path_.empty() || frame_surface_ == nullptr) {
        return;
    }
    cairo_surface_write_to_png(frame_surface_, shot_path_.c_str());
    shot_path_.clear();
}

void Shell::render()
{
    if (window_suspended_ && presented_) {
        ++skipped_redraws_;
        blink_drew_ = false;
        return;
    }
    // One more try on the next frame if the drawable was not ready in the
    // toggle itself. This does not wait on a window-sync timeout.
    if (restoring_windowed_ && windowed_settle_tries_ < 1) {
        ++windowed_settle_tries_;
        settle_windowed_frame();
    }
    layout();
    if (restoring_windowed_) {
        match_renderer_output();
    }
    clamp_scroll();
    if (!restoring_windowed_ && !redraw::needed(redraw_reasons())) {
        ++skipped_redraws_;
        blink_drew_ = false;
        return;
    }
    blink_drew_ = false;
    if (frame_surface_ == nullptr || cairo_image_surface_get_width(frame_surface_) != win_w_ ||
        cairo_image_surface_get_height(frame_surface_) != win_h_) {
        if (frame_surface_ != nullptr) {
            cairo_surface_destroy(frame_surface_);
        }
        frame_surface_ = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, std::max(1, win_w_), std::max(1, win_h_));
        ++surface_builds_;
    }
    cairo_t *context = cairo_create(frame_surface_);
    next_hot_.clear();
    next_focus_.clear();
    next_sliders_.clear();
    draw(context);
    hot_.swap(next_hot_);
    focus_order_.swap(next_focus_);
    sliders_.swap(next_sliders_);
    if (active_field() != nullptr && window_ != nullptr && !text_input_on_) {
        SDL_StartTextInput(window_);
        text_input_on_ = true;
    } else if (active_field() == nullptr && text_input_on_ && window_ != nullptr) {
        SDL_StopTextInput(window_);
        text_input_on_ = false;
    }
    if (scene_fade_.active()) {
        paint_scene_fade();
    }
    cairo_surface_flush(frame_surface_);
    note_ui_frame(frame_surface_);
    if (texture_ == nullptr || tex_w_ != win_w_ || tex_h_ != win_h_) {
        if (texture_ != nullptr) {
            SDL_DestroyTexture(texture_);
        }
        texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, win_w_, win_h_);
        tex_w_ = win_w_;
        tex_h_ = win_h_;
        if (texture_ != nullptr) {
            SDL_SetTextureBlendMode(texture_, SDL_BLENDMODE_NONE);
        }
    }
    if (texture_ != nullptr && renderer_ != nullptr) {
        int output_w = 0;
        int output_h = 0;
        SDL_GetRenderOutputSize(renderer_, &output_w, &output_h);
        const bool mismatch = output_w > 0 && output_h > 0 &&
                              (std::abs(output_w - win_w_) > 48 || std::abs(output_h - win_h_) > 48);
        if (mismatch) {
            match_renderer_output();
            SDL_GetRenderOutputSize(renderer_, &output_w, &output_h);
        }
        const bool layout_is_screen =
            restoring_windowed_ && normal_valid_ && (win_w_ > normal_w_ + 48 || win_h_ > normal_h_ + 48);
        if (hold_stale_fullscreen_present(restoring_windowed_, layout_is_screen, fullscreen_hold_frames_)) {
            ++fullscreen_hold_frames_;
        } else {
            SDL_UpdateTexture(texture_, nullptr, cairo_image_surface_get_data(frame_surface_),
                              cairo_image_surface_get_stride(frame_surface_));
            SDL_RenderClear(renderer_);
            if (restoring_windowed_ && (output_w > win_w_ + 48 || output_h > win_h_ + 48)) {
                // SDL still reports the screen. The X window is already the
                // saved size. Draw that size, not a stretched screen layout.
                const SDL_Rect view{0, 0, win_w_, win_h_};
                SDL_SetRenderViewport(renderer_, &view);
                SDL_RenderTexture(renderer_, texture_, nullptr, nullptr);
                SDL_RenderPresent(renderer_);
                SDL_SetRenderViewport(renderer_, nullptr);
            } else {
                SDL_SetRenderViewport(renderer_, nullptr);
                SDL_RenderTexture(renderer_, texture_, nullptr, nullptr);
                SDL_RenderPresent(renderer_);
            }
            fullscreen_hold_frames_ = 0;
        }
    }
    if (panel_settle_ > 0) {
        --panel_settle_;
    }
    cairo_destroy(context);
    visual_stamp_ = visual_stamp();
    presented_ = true;
    window_exposed_ = false;
    ++redraws_;
}

// ---------------------------------------------------------------------------
// Slate skin and handheld layout chrome.

namespace {

struct SlateInk {
    double r;
    double g;
    double b;
};

constexpr SlateInk kSlateBand{0x1E / 255.0, 0x26 / 255.0, 0x31 / 255.0};
constexpr SlateInk kSlateDeep{0x15 / 255.0, 0x1B / 255.0, 0x23 / 255.0};
constexpr SlateInk kSlatePanel{0x29 / 255.0, 0x32 / 255.0, 0x3F / 255.0};
constexpr SlateInk kSlateEdge{0x3A / 255.0, 0x46 / 255.0, 0x58 / 255.0};
constexpr SlateInk kSlateGold{0xF2 / 255.0, 0xC1 / 255.0, 0x4E / 255.0};
constexpr SlateInk kSlateGoldDim{0xC9 / 255.0, 0xA6 / 255.0, 0x46 / 255.0};
constexpr SlateInk kSlateInk{0xEE / 255.0, 0xE8 / 255.0, 0xDA / 255.0};
constexpr SlateInk kSlateDim{0x9A / 255.0, 0xA6 / 255.0, 0xB8 / 255.0};
constexpr SlateInk kSlateGood{0x6C / 255.0, 0xCB / 255.0, 0x7A / 255.0};
constexpr SlateInk kSlateBad{0xE5 / 255.0, 0x67 / 255.0, 0x5A / 255.0};

void slate_fill(cairo_t *cr, double x, double y, double w, double h, const SlateInk &ink, double alpha = 1.0)
{
    cairo_set_source_rgba(cr, ink.r, ink.g, ink.b, alpha);
    cairo_rectangle(cr, x, y, w, h);
    cairo_fill(cr);
}

void slate_round(cairo_t *cr, double x, double y, double w, double h, double radius, const SlateInk &ink, double alpha = 1.0)
{
    round_dialog_rect(cr, x, y, w, h, radius);
    cairo_set_source_rgba(cr, ink.r, ink.g, ink.b, alpha);
    cairo_fill(cr);
}

void slate_text(cairo_t *cr, const char *font, const std::string &text, double x, double y, const SlateInk &ink)
{
    paint_text(cr, font, text, x, y, ink.r, ink.g, ink.b, -1);
}

std::string plain_count(std::int64_t value)
{
    std::string money = civic::dollars(value);
    if (!money.empty() && money[0] == '-') {
        money.erase(0, 1);
        if (!money.empty() && money[0] == '$') {
            money.erase(0, 1);
        }
        return "-" + money;
    }
    if (!money.empty() && money[0] == '$') {
        money.erase(0, 1);
    }
    return money;
}

// Cut a line to `room` pixels with an ellipsis, one UTF-8 character at a time.
std::string fit_text(cairo_t *cr, const char *font, std::string text, int room)
{
    if (room <= 0) {
        return std::string();
    }
    if (measure_text(cr, font, text, -1).width <= room) {
        return text;
    }
    const std::string dots = "\u2026";
    while (!text.empty() && measure_text(cr, font, text + dots, -1).width > room) {
        while (!text.empty() && (static_cast<unsigned char>(text.back()) & 0xC0) == 0x80) {
            text.pop_back();
        }
        if (!text.empty()) {
            text.pop_back();
        }
    }
    return text.empty() ? std::string() : text + dots;
}

// Pause, slow, normal and fast drawn as shapes so no font needs the glyphs.
void speed_glyph(cairo_t *cr, int speed, double cx, double cy, double size, const SlateInk &ink)
{
    cairo_set_source_rgb(cr, ink.r, ink.g, ink.b);
    if (speed <= 0) {
        const double bar = size * 0.22;
        cairo_rectangle(cr, cx - size * 0.32, cy - size * 0.38, bar, size * 0.76);
        cairo_rectangle(cr, cx + size * 0.10, cy - size * 0.38, bar, size * 0.76);
        cairo_fill(cr);
        return;
    }
    const double tri = size * 0.42;
    const double step = tri * 0.85;
    double start = cx - (speed - 1) * step / 2.0 - tri / 2.0;
    for (int i = 0; i < speed; ++i) {
        cairo_move_to(cr, start, cy - tri * 0.6);
        cairo_line_to(cr, start + tri, cy);
        cairo_line_to(cr, start, cy + tri * 0.6);
        cairo_close_path(cr);
        start += step;
    }
    cairo_fill(cr);
}

const char *month_short(int month)
{
    static const char *names[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    return names[month < 0 || month > 11 ? 0 : month];
}

} // namespace

const char *Shell::tag_font() const
{
    if (std::fabs(font_tag_scale_ - ui_scale_) > 0.001f || font_tag_.empty()) {
        font_tag_ = ui_scale::scaled_font("Sans Bold", 9, ui_scale_);
        font_tag_scale_ = ui_scale_;
    }
    return font_tag_.c_str();
}

void Shell::draw_info_band(cairo_t *cr)
{
    const double y = band_y_;
    const double h = band_h_;
    {
        cairo_pattern_t *grad = cairo_pattern_create_linear(0, y, 0, y + h);
        cairo_pattern_add_color_stop_rgb(grad, 0, kSlateBand.r, kSlateBand.g, kSlateBand.b);
        cairo_pattern_add_color_stop_rgb(grad, 1, 0x19 / 255.0, 0x20 / 255.0, 0x2A / 255.0);
        cairo_set_source(cr, grad);
        cairo_rectangle(cr, 0, y, win_w_, h);
        cairo_fill(cr);
        cairo_pattern_destroy(grad);
    }
    slate_fill(cr, 0, y + h - 1, win_w_, 1, kSlateGoldDim, 0.55);
    const double label_y = y + dp(6);
    const double value_y = y + dp(20);
    const double gap = dp(16);
    const int speed = welcome_gate_ ? 0 : speed_;
    const std::string funds = civic::dollars(session_.funds());
    const std::string date = session_.date_text();
    const std::string people = plain_count(session_.population());
    const std::string name = session_.city_name();
    std::string goal;
    if (session_.scenario_id() != 0) {
        goal = session_.scenario_meter();
        const std::string clock = session_.scenario_clock_text();
        if (!clock.empty()) {
            goal += goal.empty() ? clock : "  \u00b7  " + clock;
        }
    }
    const std::int64_t flow = session_.plot_cash_flow(CitySession::HistoryScale::Short, 0);
    enum Kind { City, Funds, Date, Speed, People, Demand, Goal, KindCount };
    const char *labels[KindCount] = {"CITY", "FUNDS", "DATE", "SPEED", "POPULATION", "DEMAND", "GOAL"};
    const std::string values[KindCount] = {name, funds, date, "", people, "", goal};
    const double pill = dp(26);
    const double pill_gap = dp(3);
    double widths[KindCount] = {};
    for (int k = 0; k < KindCount; ++k) {
        const double label_w = measure_text(cr, tag_font(), labels[k], -1).width;
        double value_w = values[k].empty() ? 0 : measure_text(cr, bold_font(), values[k], -1).width;
        if (k == Funds) {
            value_w += dp(14);
        }
        if (k == Speed) {
            value_w = pill * 4 + pill_gap * 3;
        }
        if (k == Demand) {
            value_w = dp(66);
        }
        widths[k] = std::max(label_w, value_w);
    }
    // Most useful first. The goal may be shortened to the room that is left;
    // the city name is the first to go on a narrow window.
    bool shown[KindCount] = {};
    const int order[] = {Funds, Date, Speed, People, Demand, Goal, City};
    double used = dp(12);
    const double room = win_w_ - dp(12);
    for (int k : order) {
        if (k == Goal && values[Goal].empty()) {
            continue;
        }
        double need = widths[k] + gap * 2;
        if (k == Goal) {
            need = std::min(need, static_cast<double>(dp(160)));
        }
        if (used + need <= room) {
            shown[k] = true;
            used += std::min(need, widths[k] + gap * 2);
        }
    }
    band_goal_shown_ = shown[Goal];
    double goal_room = 0;
    if (shown[Goal]) {
        double others = dp(12);
        for (int k = 0; k < KindCount; ++k) {
            if (shown[k] && k != Goal) {
                others += widths[k] + gap * 2;
            }
        }
        goal_room = std::max(0.0, room - others - gap * 2);
    }
    double x = dp(12);
    bool first = true;
    for (int k = 0; k < KindCount; ++k) {
        if (!shown[k]) {
            continue;
        }
        if (!first) {
            slate_fill(cr, x - gap, y + dp(9), 1, h - dp(18), kSlateEdge);
        }
        first = false;
        slate_text(cr, tag_font(), labels[k], x, label_y, kSlateDim);
        double w = widths[k];
        switch (k) {
        case Funds: {
            const bool broke = session_.funds() < 0;
            slate_text(cr, bold_font(), funds, x, value_y, broke ? kSlateBad : kSlateGold);
            const double tx = x + measure_text(cr, bold_font(), funds, -1).width + dp(6);
            const double ty = value_y + dp(8);
            if (flow != 0) {
                const SlateInk &trend = flow > 0 ? kSlateGood : kSlateBad;
                cairo_set_source_rgb(cr, trend.r, trend.g, trend.b);
                const double t = dp(5);
                if (flow > 0) {
                    cairo_move_to(cr, tx, ty + t * 0.6);
                    cairo_line_to(cr, tx + t, ty - t * 0.6);
                    cairo_line_to(cr, tx + t * 2, ty + t * 0.6);
                } else {
                    cairo_move_to(cr, tx, ty - t * 0.6);
                    cairo_line_to(cr, tx + t, ty + t * 0.6);
                    cairo_line_to(cr, tx + t * 2, ty - t * 0.6);
                }
                cairo_close_path(cr);
                cairo_fill(cr);
            }
            break;
        }
        case Speed:
            for (int i = 0; i < 4; ++i) {
                const double px = x + i * (pill + pill_gap);
                const double py = value_y - dp(1);
                const double ph = dp(20);
                const bool on = speed == i;
                const int id = kSpeedPill + i;
                const bool hot = hover_id_ == id && dialog_ == Dialog::None && !menu_.open();
                slate_round(cr, px, py, pill, ph, dp(5), on ? kSlateGold : (hot ? kSlateEdge : kSlatePanel));
                speed_glyph(cr, i, px + pill / 2.0, py + ph / 2.0, dp(12), on ? kSlateDeep : kSlateInk);
                push_hot(px, py, pill, ph, id);
            }
            break;
        case Demand: {
            const double values_d[3] = {session_.res_demand(), session_.com_demand(), session_.ind_demand()};
            const SlateInk colors[3] = {{0x6C / 255.0, 0xCB / 255.0, 0x7A / 255.0},
                                        {0x5B / 255.0, 0x9B / 255.0, 0xE6 / 255.0},
                                        {0xE8 / 255.0, 0xC2 / 255.0, 0x4A / 255.0}};
            const char *letters[3] = {"R", "C", "I"};
            const double mid = value_y + dp(8);
            for (int i = 0; i < 3; ++i) {
                const double bx = x + i * dp(23);
                slate_text(cr, tag_font(), letters[i], bx, value_y + dp(1), kSlateDim);
                const double bar_x = bx + dp(9);
                slate_fill(cr, bar_x, mid - dp(8), dp(9), dp(16), kSlateEdge);
                const double v = std::max(-1.0, std::min(1.0, values_d[i]));
                const double bar_h = v == 0.0 ? 0.0 : std::max(2.0, std::fabs(v) * dp(8));
                if (v >= 0) {
                    slate_fill(cr, bar_x, mid - bar_h, dp(9), bar_h, colors[i]);
                } else {
                    slate_fill(cr, bar_x, mid, dp(9), bar_h, kSlateBad);
                }
                slate_fill(cr, bar_x - 1, mid, dp(11), 1, kSlateDim);
            }
            break;
        }
        case Goal: {
            const std::string shown_goal = fit_text(cr, bold_font(), goal, static_cast<int>(goal_room));
            slate_text(cr, bold_font(), shown_goal, x, value_y, kSlateInk);
            w = std::min(w, goal_room);
            break;
        }
        default:
            slate_text(cr, bold_font(), values[k], x, value_y, kSlateInk);
            break;
        }
        x += w + gap * 2;
    }
}

void Shell::draw_news_line(cairo_t *cr, const std::string &tool_text, const std::string &hint)
{
    const double y = win_h_ - status_h_;
    slate_fill(cr, 0, y, win_w_, status_h_, kSlateDeep);
    slate_fill(cr, 0, y, win_w_, 1, kSlateEdge);
    const double text_y = y + (status_h_ - measure_text(cr, ui_font(), "Ag", -1).height) / 2.0;
    double right = win_w_ - dp(10);
    // Right: the tool and its price, then what a click does.
    if (!tool_text.empty()) {
        const int w = measure_text(cr, bold_font(), tool_text, -1).width;
        if (w < win_w_ / 2) {
            right -= w;
            slate_text(cr, bold_font(), tool_text, right, text_y, kSlateGold);
            right -= dp(18);
        }
    }
    // Left: the newest city notice with its month, behind a NEWS tag.
    const TextMetrics tag = measure_text(cr, tag_font(), "NEWS", -1);
    const double tag_w = tag.width + dp(14);
    const double tag_h = tag.height + dp(6);
    const double tag_y = y + (status_h_ - tag_h) / 2.0;
    slate_round(cr, dp(10), tag_y, tag_w, tag_h, dp(4), kSlateGold);
    slate_text(cr, tag_font(), "NEWS", dp(10) + dp(7), tag_y + dp(3), kSlateDeep);
    const double news_x = dp(10) + tag_w + dp(10);
    std::string news = "No news yet.";
    const auto &notices = session_.notices();
    if (!notices.empty()) {
        const CitySession::CityNotice &latest = notices.front();
        news = latest.text + "   " + month_short(latest.month) + " " + std::to_string(latest.year);
    }
    std::string hint_line = hint;
    const int news_w = measure_text(cr, ui_font(), news, -1).width;
    const int hint_room = static_cast<int>(right - (news_x + std::min(news_w, dp(360)) + dp(24)));
    hint_line = hint_room < dp(120) ? std::string() : fit_text(cr, ui_font(), hint_line, hint_room);
    if (!hint_line.empty()) {
        const int w = measure_text(cr, ui_font(), hint_line, -1).width;
        right -= w;
        slate_text(cr, ui_font(), hint_line, right, text_y, kSlateDim);
        right -= dp(24);
    }
    const std::string shown_news = fit_text(cr, ui_font(), news, static_cast<int>(right - news_x));
    slate_text(cr, ui_font(), shown_news, news_x, text_y, notices.empty() ? kSlateDim : kSlateInk);
}

void Shell::draw_hover_card(cairo_t *cr)
{
    if (dialog_ != Dialog::None || menu_.open() || tool_index_ != kToolCount - 1) {
        return;
    }
    int hx = 0;
    int hy = 0;
    if (!map_.hover_tile(hx, hy)) {
        return;
    }
    const CitySession::TileInspect card = session_.inspect_tile(hx, hy);
    if (!card.valid) {
        return;
    }
    const double card_w = std::min(static_cast<double>(dp(220)), std::max(80.0, view_w_ * 0.34));
    const double card_h = card.offer_power ? dp(132) : dp(96);
    inspect_card_shown_ = true;
    inspect_card_w_ = card_w;
    inspect_card_h_ = card_h;
    const double card_x = view_x_ + view_w_ - card_w - dp(12) - hud_right_;
    const double card_y = view_y_ + dp(8) + minimap_clearance();
    if (!slate_ && !handheld_) {
        fill(cr, card_x, card_y, card_w, card_h, theme::kMenuBgR, theme::kMenuBgG, theme::kMenuBgB);
        stroke_rect(cr, card_x, card_y, card_w, card_h, theme::kMenuFrameR, theme::kMenuFrameG, theme::kMenuFrameB);
        paint_text(cr, bold_font(), card.name, card_x + 8, card_y + 6, theme::kTextR, theme::kTextG, theme::kTextB,
                   static_cast<int>(card_w - 16));
        paint_text(cr, ui_font(), card.power, card_x + 8, card_y + dp(26), theme::kTextR, theme::kTextG, theme::kTextB,
                   static_cast<int>(card_w - 16));
        paint_text(cr, ui_font(), card.access, card_x + 8, card_y + dp(44), theme::kTextR, theme::kTextG, theme::kTextB,
                   static_cast<int>(card_w - 16));
        paint_text(cr, ui_font(), card.condition, card_x + 8, card_y + dp(62), theme::kTextR, theme::kTextG,
                   theme::kTextB, static_cast<int>(card_w - 16));
    } else {
        cairo_set_source_rgba(cr, 0, 0, 0, 0.35);
        round_dialog_rect(cr, card_x + 2, card_y + 3, card_w, card_h, dp(8));
        cairo_fill(cr);
        slate_round(cr, card_x, card_y, card_w, card_h, dp(8), kSlateBand, 0.96);
        round_dialog_rect(cr, card_x, card_y, card_w, card_h, dp(8));
        cairo_set_source_rgb(cr, kSlateEdge.r, kSlateEdge.g, kSlateEdge.b);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);
        const int wrap = static_cast<int>(card_w - 20);
        paint_text(cr, bold_font(), card.name, card_x + 10, card_y + 7, kSlateGold.r, kSlateGold.g, kSlateGold.b, wrap);
        const bool off = card.power == "Power: off";
        const bool on = card.power == "Power: on";
        auto dot_line = [&](const std::string &text, double ly, int state) {
            cairo_new_sub_path(cr);
            cairo_arc(cr, card_x + 14, ly + dp(8), dp(3.5), 0, 6.2832);
            const SlateInk &ink = state > 0 ? kSlateGood : (state == 0 ? kSlateBad : kSlateDim);
            cairo_set_source_rgb(cr, ink.r, ink.g, ink.b);
            cairo_fill(cr);
            paint_text(cr, ui_font(), text, card_x + 24, ly, kSlateInk.r, kSlateInk.g, kSlateInk.b, wrap - 14);
        };
        dot_line(card.power, card_y + dp(26), on ? 1 : (off ? 0 : -1));
        const bool access = card.access == "Road access" || card.access == "Rail access";
        dot_line(card.access, card_y + dp(44), access ? 1 : (card.access == "No direct road" ? 0 : -1));
        paint_text(cr, ui_font(), card.condition, card_x + 10, card_y + dp(62), kSlateDim.r, kSlateDim.g, kSlateDim.b, wrap);
    }
    if (card.offer_power) {
        button(cr, card_x + 8, card_y + card_h - dp(34), card_w - 16, dp(26), "Show _power", kInspectPower, false);
    }
}

void Shell::draw_minimap_overlay(cairo_t *cr)
{
    // Minimap over the top-right corner of the map, in both layouts. Click
    // or tap jumps there; the frame is the view rectangle.
    if (mini_h_ <= 0) {
        return;
    }
    if (slate_ || handheld_) {
        slate_round(cr, mini_x_ - dp(4), mini_y_ - dp(4), mini_w_ + dp(8), mini_h_ + dp(8), dp(8), kSlateBand, 0.92);
    } else {
        fill(cr, mini_x_ - dp(3), mini_y_ - dp(3), mini_w_ + dp(6), mini_h_ + dp(6), theme::kMenuBgR, theme::kMenuBgG,
             theme::kMenuBgB);
        stroke_rect(cr, mini_x_ - dp(3), mini_y_ - dp(3), mini_w_ + dp(6), mini_h_ + dp(6), theme::kMenuFrameR,
                    theme::kMenuFrameG, theme::kMenuFrameB);
    }
    minimap_.set_pin_top(false);
    const AspectBox land = minimap_.content_box(mini_w_, mini_h_);
    {
        TargetCr live(cr);
        cairo_translate(live.cr, mini_x_, mini_y_);
        minimap_.sync();
        minimap_.draw(live.cr, mini_w_, mini_h_);
    }
    push_hot(mini_x_ + land.x, mini_y_ + land.y, land.width, land.height, kMinimapId);
}

double Shell::minimap_clearance() const
{
    // Height under the top of the map that the minimap overlay takes, so a
    // card or legend at the top right can sit below it.
    return mini_h_ > 0 ? std::max(0.0, static_cast<double>(mini_y_ + mini_h_ - view_y_)) : 0.0;
}

void Shell::draw_handheld_hud(cairo_t *cr)
{
    const bool open_ui = dialog_ != Dialog::None;
    draw_minimap_overlay(cr);
    // Menu chip and the compact info pill, top left.
    const double top = dp(10);
    const double chip_h = dp(40);
    double x = dp(10);
    {
        const std::string menu_label = "Menu";
        const TextMetrics m = measure_text(cr, bold_font(), menu_label, -1);
        const double w = m.width + dp(44);
        const bool hot = hover_id_ == kHudMenu || menu_.open();
        slate_round(cr, x, top, w, chip_h, dp(10), hot ? kSlateEdge : kSlateBand, 0.94);
        for (int i = 0; i < 3; ++i) {
            slate_fill(cr, x + dp(12), top + chip_h / 2.0 - dp(6) + i * dp(5), dp(14), dp(2), kSlateGold);
        }
        slate_text(cr, bold_font(), menu_label, x + dp(34), top + (chip_h - m.height) / 2.0, kSlateInk);
        push_hot(x, top, w, chip_h, kHudMenu);
        x += w + dp(8);
    }
    {
        const int speed = welcome_gate_ ? 0 : speed_;
        const std::string funds = civic::dollars(session_.funds());
        const std::string date = session_.date_text();
        const std::string people = plain_count(session_.population()) + " people";
        const double pad = dp(12);
        const double sep = dp(14);
        const double fw = measure_text(cr, bold_font(), funds, -1).width;
        const double dw = measure_text(cr, bold_font(), date, -1).width;
        const double pw = measure_text(cr, ui_font(), people, -1).width;
        const double speed_w = dp(22);
        const double demand_w = dp(40);
        double w = pad + fw + sep + dw + sep + speed_w + sep + pw + sep + demand_w + pad;
        const double limit = mini_x_ - dp(16) - x;
        const bool with_people = w <= limit;
        if (!with_people) {
            w -= pw + sep;
        }
        slate_round(cr, x, top, w, chip_h, dp(10), kSlateBand, 0.94);
        const double ty = top + (chip_h - measure_text(cr, bold_font(), "Ag", -1).height) / 2.0;
        double cx = x + pad;
        slate_text(cr, bold_font(), funds, cx, ty, session_.funds() < 0 ? kSlateBad : kSlateGold);
        cx += fw + sep;
        slate_text(cr, bold_font(), date, cx, ty, kSlateInk);
        cx += dw + sep;
        speed_glyph(cr, speed, cx + speed_w / 2.0, top + chip_h / 2.0, dp(14), speed == 0 ? kSlateBad : kSlateInk);
        push_hot(cx - dp(4), top, speed_w + dp(8), chip_h, kSpeedPill + (speed == 0 ? 2 : 0));
        cx += speed_w + sep;
        if (with_people) {
            slate_text(cr, ui_font(), people, cx, ty, kSlateDim);
            cx += pw + sep;
        }
        const double values_d[3] = {session_.res_demand(), session_.com_demand(), session_.ind_demand()};
        const SlateInk colors[3] = {{0x6C / 255.0, 0xCB / 255.0, 0x7A / 255.0},
                                    {0x5B / 255.0, 0x9B / 255.0, 0xE6 / 255.0},
                                    {0xE8 / 255.0, 0xC2 / 255.0, 0x4A / 255.0}};
        const double mid = top + chip_h / 2.0;
        for (int i = 0; i < 3; ++i) {
            const double bx = cx + i * dp(13);
            slate_fill(cr, bx, mid - dp(10), dp(9), dp(20), kSlatePanel);
            const double v = std::max(-1.0, std::min(1.0, values_d[i]));
            const double bar_h = std::fabs(v) * dp(10);
            if (v >= 0) {
                slate_fill(cr, bx, mid - bar_h, dp(9), bar_h, colors[i]);
            } else {
                slate_fill(cr, bx, mid, dp(9), bar_h, kSlateBad);
            }
        }
    }
    // Touch buttons on the right edge: zoom in, zoom out, centre.
    {
        const double size = dp(50);
        const double bx = win_w_ - size - dp(10);
        const double span = size * 3 + dp(10) * 2;
        double by = mini_y_ + mini_h_ + (quick_y_ - (mini_y_ + mini_h_) - span) / 2.0;
        by = std::max(by, static_cast<double>(mini_y_ + mini_h_ + dp(12)));
        const int ids[3] = {kHudZoomIn, kHudZoomOut, kHudCenter};
        for (int i = 0; i < 3; ++i) {
            const double cy = by + i * (size + dp(10));
            const bool hot = hover_id_ == ids[i];
            cairo_new_sub_path(cr);
            cairo_arc(cr, bx + size / 2.0, cy + size / 2.0, size / 2.0, 0, 6.2832);
            cairo_set_source_rgba(cr, hot ? kSlateEdge.r : kSlateBand.r, hot ? kSlateEdge.g : kSlateBand.g,
                                  hot ? kSlateEdge.b : kSlateBand.b, 0.92);
            cairo_fill_preserve(cr);
            cairo_set_source_rgb(cr, kSlateGoldDim.r, kSlateGoldDim.g, kSlateGoldDim.b);
            cairo_set_line_width(cr, 1.5);
            cairo_stroke(cr);
            cairo_set_source_rgb(cr, kSlateInk.r, kSlateInk.g, kSlateInk.b);
            cairo_set_line_width(cr, dp(3));
            const double mx = bx + size / 2.0;
            const double my = cy + size / 2.0;
            const double arm = size * 0.22;
            if (i == 0 || i == 1) {
                cairo_move_to(cr, mx - arm, my);
                cairo_line_to(cr, mx + arm, my);
                if (i == 0) {
                    cairo_move_to(cr, mx, my - arm);
                    cairo_line_to(cr, mx, my + arm);
                }
                cairo_stroke(cr);
            } else {
                cairo_set_line_width(cr, dp(2));
                cairo_new_sub_path(cr);
                cairo_arc(cr, mx, my, arm * 0.9, 0, 6.2832);
                cairo_stroke(cr);
                cairo_move_to(cr, mx - arm * 1.5, my);
                cairo_line_to(cr, mx - arm * 0.5, my);
                cairo_move_to(cr, mx + arm * 0.5, my);
                cairo_line_to(cr, mx + arm * 1.5, my);
                cairo_move_to(cr, mx, my - arm * 1.5);
                cairo_line_to(cr, mx, my - arm * 0.5);
                cairo_move_to(cr, mx, my + arm * 0.5);
                cairo_line_to(cr, mx, my + arm * 1.5);
                cairo_stroke(cr);
            }
            push_hot(bx, cy, size, size, ids[i]);
        }
    }
    // Quick bar: every build tool plus Inspect, the chosen one raised
    // with its price.
    {
        const int count = kToolCount;
        const double slot = std::min(static_cast<double>(quick_h_), (win_w_ - dp(140)) / static_cast<double>(count));
        const double bar_w = slot * count + dp(16);
        const double bar_x = (win_w_ - bar_w) / 2.0;
        quick_x_ = bar_x + dp(8);
        quick_slot_ = slot;
        const double lift = dp(8);
        slate_round(cr, bar_x, quick_y_, bar_w, quick_h_ + dp(4), dp(12), kSlateBand, 0.94);
        int shown_tool = palette_.focus() >= 0 ? palette_.focus() : tool_index_;
        if (shown_tool < 0 || shown_tool >= count) {
            shown_tool = -1;
        }
        // 27 slots no longer leave room for a 12px margin round a 40px
        // icon on a 1280px screen. The icon art has its own transparent
        // margin, so it keeps 1:1 whenever the slot is 42px or wider rather
        // than dropping to half size.
        double icon = std::max(16.0, slot - dp(12));
        if (icon < 40.0 && slot >= 42.0) {
            icon = 40.0;
        }
        const unsigned locked = locked_tool_mask();
        // Thin rules between the toolbar sections (Build | Zones | Parks ...).
        for (int g = 1; g < tool_groups::kGroupCount; ++g) {
            const double rx = std::round(quick_x_ + tool_groups::groups()[g].first * slot) - 0.5;
            cairo_set_source_rgba(cr, kSlateDim.r, kSlateDim.g, kSlateDim.b, 0.55);
            cairo_set_line_width(cr, 1.0);
            cairo_move_to(cr, rx, quick_y_ + dp(8));
            cairo_line_to(cr, rx, quick_y_ + quick_h_ - dp(4));
            cairo_stroke(cr);
        }
        for (int i = 0; i < count; ++i) {
            const bool chosen = i == tool_index_;
            const double sx = quick_x_ + i * slot;
            const double sy = quick_y_ + dp(4) - (chosen ? lift : 0);
            if (chosen) {
                slate_round(cr, sx + 1, sy, slot - 2, slot - 2, dp(8), kSlatePanel);
                round_dialog_rect(cr, sx + 1, sy, slot - 2, slot - 2, dp(8));
                cairo_set_source_rgb(cr, kSlateGold.r, kSlateGold.g, kSlateGold.b);
                cairo_set_line_width(cr, 2.0);
                cairo_stroke(cr);
            } else if (hover_id_ == kQuickTool + i) {
                slate_round(cr, sx + 1, sy, slot - 2, slot - 2, dp(8), kSlateEdge);
            }
            const double ix = std::floor(sx + (slot - icon) / 2.0);
            const double iy = std::floor(sy + (slot - 2 - icon) / 2.0);
            if (i == kToolCount - 1) {
                // Inspect has no toolbar icon; draw a magnifier.
                const double r = icon * 0.26;
                const double cx = ix + icon * 0.42;
                const double cy = iy + icon * 0.42;
                cairo_save(cr);
                cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
                cairo_new_path(cr);
                cairo_arc(cr, cx, cy, r, 0, 6.2832);
                cairo_set_source_rgba(cr, 0.55, 0.72, 0.85, 0.35);
                cairo_fill_preserve(cr);
                cairo_set_source_rgb(cr, kSlateInk.r, kSlateInk.g, kSlateInk.b);
                cairo_set_line_width(cr, std::max(2.0, icon * 0.09));
                cairo_stroke(cr);
                cairo_move_to(cr, cx + r * 0.72, cy + r * 0.72);
                cairo_line_to(cr, ix + icon * 0.86, iy + icon * 0.86);
                cairo_set_source_rgb(cr, kSlateGold.r, kSlateGold.g, kSlateGold.b);
                cairo_set_line_width(cr, std::max(3.0, icon * 0.14));
                cairo_stroke(cr);
                cairo_restore(cr);
                if (palette_.focus() == i) {
                    round_dialog_rect(cr, sx + 3, sy + 2, slot - 6, slot - 6, dp(6));
                    cairo_set_source_rgb(cr, kSlateInk.r, kSlateInk.g, kSlateInk.b);
                    cairo_set_line_width(cr, 2.0);
                    cairo_stroke(cr);
                }
                push_hot(sx, quick_y_ - lift, slot, quick_h_ + lift + dp(4), kQuickTool + i);
                continue;
            }
            cairo_surface_t *image = tool_icon_named(tool_icon_stem(i));
            if (image != nullptr) {
                const int iw = cairo_image_surface_get_width(image);
                const int ih = cairo_image_surface_get_height(image);
                const double scale = icon_pixel_scale(std::max(iw, ih), static_cast<int>(icon));
                cairo_save(cr);
                cairo_translate(cr, std::floor(ix + (icon - iw * scale) / 2.0), std::floor(iy + (icon - ih * scale) / 2.0));
                cairo_scale(cr, scale, scale);
                cairo_set_source_surface(cr, image, 0, 0);
                cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
                cairo_paint(cr);
                cairo_restore(cr);
            } else {
                paint_tool_fallback(cr, i, ix, iy, icon);
            }
            if (i < 32 && (locked >> i & 1u) != 0) {
                // A landmark the city cannot buy yet: greyed, still selectable.
                slate_round(cr, sx + 2, sy + 1, slot - 4, slot - 4, dp(6), chosen ? kSlatePanel : kSlateBand, 0.62);
            }
            if (palette_.focus() == i) {
                round_dialog_rect(cr, sx + 3, sy + 2, slot - 6, slot - 6, dp(6));
                cairo_set_source_rgb(cr, kSlateInk.r, kSlateInk.g, kSlateInk.b);
                cairo_set_line_width(cr, 2.0);
                cairo_stroke(cr);
            }
            push_hot(sx, quick_y_ - lift, slot, quick_h_ + lift + dp(4), kQuickTool + i);
        }
        // L1/R1 glyphs at the bar ends.
        const std::string prev = glyph_for(PadPrevTool);
        const std::string next = glyph_for(PadNextTool);
        if (!prev.empty()) {
            const TextMetrics m = measure_text(cr, tag_font(), prev, -1);
            const double kw = m.width + dp(12);
            const double ky = quick_y_ + (quick_h_ - m.height - dp(6)) / 2.0;
            slate_round(cr, bar_x - kw - dp(8), ky, kw, m.height + dp(6), dp(4), kSlatePanel, 0.95);
            slate_text(cr, tag_font(), prev, bar_x - kw - dp(8) + dp(6), ky + dp(3), kSlateInk);
        }
        if (!next.empty()) {
            const TextMetrics m = measure_text(cr, tag_font(), next, -1);
            const double kw = m.width + dp(12);
            const double ky = quick_y_ + (quick_h_ - m.height - dp(6)) / 2.0;
            slate_round(cr, bar_x + bar_w + dp(8), ky, kw, m.height + dp(6), dp(4), kSlatePanel, 0.95);
            slate_text(cr, tag_font(), next, bar_x + bar_w + dp(8) + dp(6), ky + dp(3), kSlateInk);
        }
        // Tool name and price above the raised slot.
        if (tool_index_ >= 0 && tool_index_ < count && !open_ui) {
            const ToolDef *tool = tool_by_index(tool_index_);
            std::string caption = tool != nullptr ? tool->name : "";
            if (tool != nullptr && tool->cost > 0) {
                caption += "  " + civic::dollars(tool->cost);
            }
            const int fixed = tool_fixed_special(tool);
            if (fixed >= 0) {
                // A locked landmark says why instead of its price.
                const std::string detail = session_.special_menu_detail(fixed);
                if (!detail.empty() && detail[0] != '$') {
                    caption = std::string(tool->name) + ": " + detail;
                }
            }
            const TextMetrics m = measure_text(cr, bold_font(), caption, -1);
            const double cw = m.width + dp(16);
            const double ch = m.height + dp(8);
            double cx = quick_x_ + tool_index_ * slot + slot / 2.0 - cw / 2.0;
            cx = std::max(dp(8) * 1.0, std::min(cx, win_w_ - cw - dp(8)));
            const double cy = quick_y_ - lift - ch - dp(6);
            slate_round(cr, cx, cy, cw, ch, dp(6), kSlateDeep, 0.92);
            slate_text(cr, bold_font(), caption, cx + dp(8), cy + dp(4), kSlateGold);
        }
    }
    // Button hints along the bottom edge.
    {
        const double y = win_h_ - hint_h_;
        slate_fill(cr, 0, y, win_w_, hint_h_, kSlateDeep, 0.88);
        const ToolDef *tool = tool_by_index(tool_index_);
        const bool inspecting = tool_index_ == kToolCount - 1;
        std::vector<std::pair<std::string, std::string>> hints;
        if (menu_.open()) {
            hints = {{glyph_for(PadConfirm), "Choose"}, {glyph_for(PadCancel), "Close"}};
        } else {
            hints = {{glyph_for(PadConfirm), inspecting ? "Inspect" : (tool != nullptr ? "Build" : "Use")},
                     {glyph_for(PadCancel), "Back"},
                     {inspecting ? std::string() : glyph_for(PadQuery), "Inspect"},
                     {glyph_for(PadPause), speed_ <= 0 ? "Resume" : "Pause"},
                     {glyph_for(PadOverlay), "Data map"},
                     {glyph_for(PadMenu), "Menu"}};
        }
        double hx = dp(12);
        const double cap_h = measure_text(cr, tag_font(), "Ag", -1).height + dp(4);
        for (const auto &hint : hints) {
            if (hint.first.empty()) {
                continue;
            }
            const TextMetrics key = measure_text(cr, tag_font(), hint.first, -1);
            const TextMetrics words = measure_text(cr, ui_font(), hint.second, -1);
            const double kw = std::max(cap_h, static_cast<double>(key.width + dp(10)));
            if (hx + kw + dp(6) + words.width > win_w_ - dp(12)) {
                break;
            }
            const double ky = y + (hint_h_ - cap_h) / 2.0;
            slate_round(cr, hx, ky, kw, cap_h, cap_h / 2.0, kSlateInk);
            slate_text(cr, tag_font(), hint.first, hx + (kw - key.width) / 2.0, ky + dp(2), kSlateDeep);
            slate_text(cr, ui_font(), hint.second, hx + kw + dp(6), y + (hint_h_ - words.height) / 2.0, kSlateDim);
            hx += kw + dp(6) + words.width + dp(18);
        }
    }
}

void Shell::refresh_continue_card()
{
    if (recent_.empty()) {
        continue_path_.clear();
        continue_ok_ = false;
        continue_name_.clear();
        continue_detail_.clear();
        continue_thumb_.clear();
        return;
    }
    const std::string &path = recent_.front();
    if (path == continue_path_) {
        return;
    }
    continue_path_ = path;
    std::string name = strip_mnemonic(welcome_continue_label().c_str());
    const std::string lead = "Continue: ";
    if (name.rfind(lead, 0) == 0) {
        name = name.substr(lead.size());
    }
    continue_name_ = name;
    const CityGlance glance = city_glance(path);
    continue_ok_ = glance.ok;
    continue_detail_.clear();
    continue_thumb_.clear();
    if (!glance.ok) {
        return;
    }
    continue_detail_ = std::string(month_short(glance.month)) + " " + std::to_string(glance.year) + "  \u00b7  Pop " +
                       plain_count(glance.population) + "  \u00b7  " + civic::dollars(glance.funds);
    if (glance.swatch.size() == static_cast<std::size_t>(120 * 100)) {
        continue_thumb_.resize(glance.swatch.size());
        for (std::size_t i = 0; i < glance.swatch.size(); ++i) {
            continue_thumb_[i] = 0xFF000000u | (glance.swatch[i] & 0x00FFFFFFu);
        }
    }
}
