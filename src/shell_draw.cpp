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
    message_h_ = dp(44);
    const int body = win_h_ - menu_h_ - status_h_ - message_h_;
    cell_ = ui_scale::toolbar_cell(ui_scale_, std::max(1, body));
    side_x_ = dp(6);
    pal_y_ = menu_h_ + dp(6);
    const int status_top = win_h_ - status_h_ - message_h_;
    const int gap = dp(8);
    const int mini_ok = dp(64);
    const int demand_ok = dp(64);
    // Two columns until the minimap or the demand meters would be squeezed.
    // Three columns shorten the toolbar. The minimap is dropped only when
    // even that cannot keep the meters readable. 960x600 stays usable.
    auto measure = [&](int columns, bool want_mini, int &mini_h, int &demand_y, int &demand_h) {
        const int side = ToolPalette::width_for(cell_, columns);
        const int pal_h = ToolPalette::height_for(cell_, columns);
        const int mini_y = pal_y_ + pal_h + gap;
        if (!want_mini) {
            mini_h = 0;
            demand_y = mini_y;
            demand_h = std::max(0, std::min(dp(kMaxDemandPx), status_top - demand_y));
            return demand_h >= demand_ok;
        }
        mini_h = std::max(dp(64), side * 5 / 6);
        demand_y = mini_y + mini_h + gap;
        const int demand_floor = dp(72);
        if (demand_y + demand_floor > status_top) {
            const int overflow = demand_y + demand_floor - status_top;
            mini_h = std::max(dp(36), mini_h - overflow);
            demand_y = mini_y + mini_h + gap;
        }
        demand_h = std::max(0, std::min(dp(kMaxDemandPx), status_top - demand_y));
        return mini_h >= mini_ok && demand_h >= demand_ok;
    };
    int columns = 3;
    int mini_h = 0;
    int demand_y = 0;
    int demand_h = 0;
    // Three columns once the window is tall enough. Two columns made the
    // toolbar tall and the minimap a postage stamp as the window grew.
    if (!measure(3, true, mini_h, demand_y, demand_h)) {
        columns = 2;
        if (!measure(2, true, mini_h, demand_y, demand_h)) {
            columns = 3;
            if (!measure(3, false, mini_h, demand_y, demand_h)) {
                columns = 2;
                measure(2, false, mini_h, demand_y, demand_h);
            }
        }
    }
    if (mini_h > 0) {
        const int side = ToolPalette::width_for(cell_, columns);
        const int spare = status_top - (demand_y + demand_h);
        const int cap = std::max(side, dp(160)) * 2;
        if (spare > 0 && mini_h < cap) {
            const int grow = std::min(spare, cap - mini_h);
            mini_h += grow;
            demand_y += grow;
        }
    }
    palette_.set_columns(columns);
    palette_.set_cell(cell_);
    side_w_ = ToolPalette::width_for(cell_, columns);
    mini_y_ = pal_y_ + ToolPalette::height_for(cell_, columns) + gap;
    if (mini_h <= 0) {
        mini_y_ = pal_y_ + ToolPalette::height_for(cell_, columns);
        demand_y = mini_y_ + gap;
        demand_h = std::max(0, std::min(dp(kMaxDemandPx), status_top - demand_y));
    }
    mini_h_ = mini_h;
    demand_y_ = demand_y;
    demand_h_ = demand_h;
    view_x_ = side_x_ + side_w_ + dp(8);
    view_y_ = menu_h_;
    view_w_ = std::max(dp(32), win_w_ - view_x_ - dp(8));
    view_h_ = std::max(dp(32), win_h_ - view_y_ - status_h_ - message_h_);
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
        cairo_set_source_rgb(cr, enabled ? gold_r : 0.62, enabled ? gold_g : 0.58, enabled ? gold_b : 0.48);
        cairo_set_line_width(cr, selected ? std::max(3.0, h / 12.0) : std::max(2.0, h / 16.0));
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
        if (focused && w > 18 && h > 18) {
            // Primary stays blue. Any other focused button keeps the paper
            // fill and a thick blue ring, so focus is not a second primary.
            round_dialog_rect(cr, x + 8, y + 8, w - 16, h - 16, 3);
            if (primary && !selected) {
                cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
            } else {
                cairo_set_source_rgb(cr, 0.10, 0.35, 0.78);
            }
            cairo_set_line_width(cr, 3.0);
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
        cairo_set_source_rgb(cr, theme::kFocusR, theme::kFocusG, theme::kFocusB);
        cairo_set_line_width(cr, 2);
        cairo_rectangle(cr, x + 2, y + 2, w - 4, h - 4);
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
    cairo_pattern_t *bar = cairo_pattern_create_linear(0, 0, 0, menu_h_);
    cairo_pattern_add_color_stop_rgb(bar, 0, theme::kMenuTopR, theme::kMenuTopG, theme::kMenuTopB);
    cairo_pattern_add_color_stop_rgb(bar, 1, theme::kMenuBotR, theme::kMenuBotG, theme::kMenuBotB);
    cairo_set_source(cr, bar);
    cairo_rectangle(cr, 0, 0, win_w_, menu_h_);
    cairo_fill(cr);
    cairo_pattern_destroy(bar);
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
        if (lit) {
            fill(cr, x, 3, w, menu_h_ - 6, theme::chrome_select_r(), theme::chrome_select_g(), theme::chrome_select_b());
            stroke_rect(cr, x, 3, w, menu_h_ - 6, theme::chrome_select_edge_r(), theme::chrome_select_edge_g(), theme::chrome_select_edge_b());
        }
        paint_mnemonic(cr, menu_font(), title, x + 10, 9, menu_.alt_shown || menu_.open(),
                       lit ? 1 : theme::kTextR, lit ? 1 : theme::kTextG, lit ? 1 : theme::kTextB);
        x += w + 2;
    }
    const std::string zoom = zoom_menu_label(map_.tile_size());
    const TextMetrics zoom_metrics = measure_text(cr, menu_font(), zoom, -1);
    const double zoom_x = static_cast<double>(win_w_) - zoom_metrics.width - 12.0;
    if (zoom_x > x + 8.0) {
        paint_text(cr, menu_font(), zoom, zoom_x, 9, theme::kTextR, theme::kTextG, theme::kTextB, -1);
    }
}

void Shell::draw_menu_dropdown(cairo_t *cr)
{
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
        const double bars = static_cast<double>(status_h_ + message_h_);
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
    fill(cr, drop_x, menu_h_, drop_w, drop_h, theme::kMenuBgR, theme::kMenuBgG, theme::kMenuBgB);
    stroke_rect(cr, drop_x, menu_h_, drop_w, drop_h, theme::kMenuFrameR, theme::kMenuFrameG, theme::kMenuFrameB);
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
            cairo_set_source_rgb(cr, theme::kMenuFrameR, theme::kMenuFrameG, theme::kMenuFrameB);
            cairo_move_to(cr, drop_x + 4, y + 4);
            cairo_line_to(cr, drop_x + drop_w - 4, y + 4);
            cairo_stroke(cr);
            continue;
        }
        const int id = kMenuItem + column * 40 + i;
        push_hot(drop_x + 2, y, row_w, h, id);
        const bool selected = menu_.cursor == i || hover_id_ == id;
        if (selected) {
            fill(cr, drop_x + 3, y, drop_w - 6, row_h, theme::chrome_hover_r(), theme::chrome_hover_g(), theme::chrome_hover_b());
            if (gamepad_nav_ && menu_.cursor == i) {
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
            cairo_set_source_rgb(cr, theme::kTextR, theme::kTextG, theme::kTextB);
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
        paint_mnemonic(cr, menu_font(), item.label, text_x, text_y, menu_.alt_shown, theme::kTextR, theme::kTextG,
                       theme::kTextB);
        if (item.kind == ItemKind::Submenu) {
            paint_text(cr, menu_font(), ">", drop_x + drop_w - dp(28), text_y, theme::kTextR, theme::kTextG,
                       theme::kTextB, -1);
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
                       theme::kAccelR, theme::kAccelG, theme::kAccelB, -1);
        }
    }
    if (menu_scrolls) {
        const double track_x = drop_x + drop_w - 12;
        const double thumb_h = std::max(16.0, drop_h * (drop_h / content_h));
        const double max_scroll = std::max(1.0, content_h - avail);
        const double thumb_y = menu_h_ + (drop_h - thumb_h) * (scroll / max_scroll);
        fill(cr, track_x, menu_h_ + 2, 8, drop_h - 4, 0.86, 0.86, 0.86);
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
    fill(cr, sub.x, sub.y, sub.w, sub.h, 1, 1, 1);
    stroke_rect(cr, sub.x, sub.y, sub.w, sub.h, theme::kMenuFrameR, theme::kMenuFrameG, theme::kMenuFrameB);
    const int text_w = static_cast<int>(std::max(40.0, sub.w - dp(20)));
    if (recent_.empty()) {
        paint_text(cr, menu_font(), "No recent cities", sub.x + 10, sub.y + 8, theme::kTextR, theme::kTextG,
                   theme::kTextB, text_w);
        return;
    }
    for (int i = 0; i < static_cast<int>(recent_.size()); ++i) {
        const double ry = sub.y + 4 + i * row_h;
        const int id = kRecentItem + i;
        push_hot(sub.x + 2, ry, std::max(1.0, sub.w - 4), row_h, id);
        if (menu_.recent_cursor == i || hover_id_ == id) {
            fill(cr, sub.x + 3, ry, std::max(1.0, sub.w - 6), row_h, theme::chrome_hover_r(), theme::chrome_hover_g(), theme::chrome_hover_b());
        }
        const std::string shown = recent_[static_cast<std::size_t>(i)];
        const auto slash = shown.find_last_of("/\\");
        paint_text(cr, menu_font(), slash == std::string::npos ? shown : shown.substr(slash + 1), sub.x + 10, ry + 7,
                   theme::kTextR, theme::kTextG, theme::kTextB, text_w);
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
    // Dialogs stay above the status and message bars. Centering them in the
    // full window let a tall Load City panel cover "Funds" and the city name.
    const double band_h = std::max(1.0, static_cast<double>(win_h_ - status_h_ - message_h_));
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
        const double bh = dp(34);
        const double gap = dp(6);
        const int rows = continued.empty() ? 4 : 5;
        const double plaque_h = dp(16) + rows * bh + (rows - 1) * gap + dp(14);
        const double plaque_w = std::min(static_cast<double>(dp(420)), std::max(1.0, win_w_ - static_cast<double>(dp(48))));
        const double px = std::max(0.0, (win_w_ - plaque_w) / 2.0);
        double py = win_h_ - plaque_h - dp(22);
        // The title and the downtown towers live in the upper part of the art.
        // Keep the choices over the neighborhoods along the bottom.
        const double lowest_top = win_h_ * 0.56;
        if (py < lowest_top) {
            py = std::max(win_h_ * 0.40, win_h_ - plaque_h - dp(8));
        }
        const double radius = dp(12);
        auto round_box = [&](double x, double y, double w, double h) {
            const double rr = std::min(radius, std::min(w, h) / 2.0);
            cairo_new_path(cr);
            cairo_arc(cr, x + w - rr, y + rr, rr, -1.5708, 0);
            cairo_arc(cr, x + w - rr, y + h - rr, rr, 0, 1.5708);
            cairo_arc(cr, x + rr, y + h - rr, rr, 1.5708, 3.1416);
            cairo_arc(cr, x + rr, y + rr, rr, 3.1416, 4.7124);
            cairo_close_path(cr);
        };
        round_box(px + dp(3), py + dp(4), plaque_w, plaque_h);
        cairo_set_source_rgba(cr, 0.05, 0.08, 0.16, 0.40);
        cairo_fill(cr);
        round_box(px, py, plaque_w, plaque_h);
        cairo_set_source_rgba(cr, 0.99, 0.96, 0.88, 0.90);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, 0.45, 0.32, 0.10, 0.95);
        cairo_set_line_width(cr, 2.0);
        cairo_stroke(cr);
        const double bw = plaque_w - dp(28);
        double by = py + dp(12);
        if (!continued.empty()) {
            button(cr, px + dp(14), by, bw, bh, continued.c_str(), kWelcomeContinue, false);
            by += bh + gap;
        }
        button(cr, px + dp(14), by, bw, bh, "_Start a new city", kWelcomeNew, false);
        by += bh + gap;
        button(cr, px + dp(14), by, bw, bh, "_Load a saved city", kWelcomeLoad, false);
        by += bh + gap;
        button(cr, px + dp(14), by, bw, bh, "Select a _scenario", kWelcomeScenario, false);
        by += bh + gap;
        button(cr, px + dp(14), by, bw, bh, "_Quit", kWelcomeQuit, false);
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
        const std::string body = how_to_play_text();
        const double width = message_width(win_w_, margin, dp(460), dp(640));
        const double body_h = text_block_height(body, width - 2 * margin);
        const double height = dp(40) + margin + body_h + dp(16) + dp(36) + margin;
        const DialogRect panel = place_dialog(win_w_, band_h, width, height, margin);
        frame_panel(cr, panel.x, panel.y, panel.w, panel.h, "How to play");
        label(cr, body_font(), body, panel.x + margin, panel.y + dp(48), static_cast<int>(panel.w - 2 * margin));
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
        const double toggles = (bh + stack_gap) * 4.0;
        const double rows = scale_row.next_caption + toggles;
        const double height = dp(48) + margin + rows + dp(8) + bh + margin;
        const DialogRect panel = place_dialog(win_w_, band_h, dp(640), height, margin);
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
        const bool scale_auto = scale_percent_ <= 0;
        const std::string scale_text = scale_auto ? "Auto" : std::to_string(scale_percent_) + "%";
        slider(1, "Volume", std::to_string(volume_) + "%", volume_, 0, 100, volume_row, true, false);
        const std::string scale_caption = scale_auto ? "UI scale" : "UI scale  " + scale_text;
        slider(2, scale_caption, "", scale_auto ? 100 : scale_percent_, 50, 200, scale_row, !scale_auto, true);
        const double auto_h = std::min(bh, text_h + static_cast<double>(dp(6)));
        button(cr, switch_x, content + scale_row.caption, switch_w, auto_h, scale_auto ? "Auto" : "Manual", kScaleAuto,
               false);
        double sy = content + scale_row.next_caption;
        switch_row("Pause when unfocused", pause_unfocused_ ? "On" : "Off", kReset, sy);
        sy += bh + stack_gap;
        switch_row("Mouse wheel", wheel_zooms_ ? "Zoom" : "Scroll", kWheelZoom, sy);
        sy += bh + stack_gap;
        const bool full = window_ != nullptr && (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) != 0;
        switch_row("Fullscreen", full ? "On" : "Off", kFullscreenBox, sy);
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
        switch_row("Autosave", autosave_state, kAutosave, sy);
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
        const std::string intro = std::string(heading) + "\n" + controls_page_body(controls_page_, deck_, pad_names, key_text);
        const bool tools = controls_page_has_tools(controls_page_, deck_);
        const std::string col_a = tools ? controls_tool_column(0, 9, &key_text) : std::string();
        const std::string col_b = tools ? controls_tool_column(9, 17, &key_text) : std::string();
        const double width = message_width(win_w_, margin, dp(520), dp(860));
        const double inner = width - 2 * margin;
        const double intro_h = text_block_height(intro, inner);
        const double col_w = tools ? (inner - dp(16)) / 2.0 : inner;
        const double col_h = tools ? std::max(text_block_height(col_a, col_w), text_block_height(col_b, col_w)) : 0.0;
        const double content_h = intro_h + (tools ? dp(8) + col_h : 0.0);
        double tallest = content_h;
        for (int page = 0; page < 3; ++page) {
            if (page == controls_page_) {
                continue;
            }
            const std::string page_intro =
                std::string(controls_page_title(page, deck_)) + "\n" + controls_page_body(page, deck_, pad_names, key_text);
            const bool page_tools = controls_page_has_tools(page, deck_);
            const std::string page_a = page_tools ? controls_tool_column(0, 9, &key_text) : std::string();
            const std::string page_b = page_tools ? controls_tool_column(9, 17, &key_text) : std::string();
            const double page_intro_h = text_block_height(page_intro, inner);
            const double page_col_w = page_tools ? (inner - dp(16)) / 2.0 : inner;
            const double page_col_h =
                page_tools ? std::max(text_block_height(page_a, page_col_w), text_block_height(page_b, page_col_w)) : 0.0;
            tallest = std::max(tallest, page_intro_h + (page_tools ? dp(8) + page_col_h : 0.0));
        }
        const double footer_h = dp(32);
        const double buttons_h = dp(36);
        const double chrome = dp(40) + margin + footer_h + dp(8) + buttons_h + margin;
        const double max_h = std::max(chrome + dp(48), band_h - 2.0 * margin);
        const double view_h = std::max(static_cast<double>(dp(48)), std::min(tallest, max_h - chrome));
        const double height = chrome + view_h;
        const DialogRect panel = place_dialog(win_w_, band_h, width, height, margin);
        frame_panel(cr, panel.x, panel.y, panel.w, panel.h, "Controls");
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
        label(cr, body_font(), intro, panel.x + margin, origin, static_cast<int>(panel.w - 2 * margin));
        if (tools) {
            const double cols_y = origin + intro_h + dp(8);
            label(cr, body_font(), col_a, panel.x + margin, cols_y, static_cast<int>(col_w));
            label(cr, body_font(), col_b, panel.x + margin + col_w + dp(16), cols_y, static_cast<int>(col_w));
        }
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
    if (dialog_ == Dialog::Query) {
        const double width = message_width(win_w_, margin, dp(320), dp(480));
        const double body_h = text_block_height(info_body_.empty() ? " " : info_body_, width - 2 * margin);
        const double height = dp(40) + margin + body_h + dp(12) + dp(36) + margin;
        DialogRect panel = place_dialog(win_w_, band_h, width, height, margin);
        if (query_anchored_) {
            panel = place_near(win_w_, band_h, panel.w, panel.h, query_anchor_x_, query_anchor_y_, margin);
        }
        const double query_floor = static_cast<double>(win_h_ - status_h_ - message_h_) - margin;
        if (panel.y + panel.h > query_floor) {
            panel.y = std::max(static_cast<double>(menu_h_) + margin, query_floor - panel.h);
        }
        frame_panel(cr, panel.x, panel.y, panel.w, panel.h, "Query");
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

void Shell::draw_special_menu(cairo_t *cr)
{
    if (!special_open_ || dialog_ != Dialog::None || menu_.open()) {
        return;
    }
    const int count = special_building_count();
    if (count < 1) {
        return;
    }
    // Clicks outside the list close it. Rows are pushed after this catcher.
    push_hot(0, menu_h_, win_w_, std::max(0, win_h_ - menu_h_), 0);
    const int slot = special_tool_index();
    const int columns = std::max(2, palette_.columns());
    const int col = slot % columns;
    const int row = slot / columns;
    const double anchor_x = side_x_ + 4 + (col + 1) * cell_ + 6;
    const double anchor_y = pal_y_ + 4 + row * cell_;
    int name_h = 1;
    int cost_h = 1;
    for (int i = 0; i < count; ++i) {
        const SpecialBuilding &building = special_building(i);
        name_h = std::max(name_h, measure_text(cr, bold_font(), building.name, -1).height);
        const std::string sample = session_.special_menu_detail(i);
        cost_h = std::max(cost_h, measure_text(cr, ui_font(), sample, -1).height);
    }
    const int between = dp(6);
    const int text_block = name_h + between + cost_h;
    const double row_h = std::max(std::max(static_cast<double>(dp(44)), static_cast<double>(cell_ - 4)),
                                  static_cast<double>(text_block + dp(12)));
    const double icon = std::min(static_cast<double>(dp(40)), row_h - dp(8));
    double widest = static_cast<double>(dp(240));
    for (int i = 0; i < count; ++i) {
        const std::string sample = session_.special_menu_detail(i);
        widest = std::max(widest, icon + dp(36) + measure_text(cr, ui_font(), sample, -1).width);
        widest = std::max(widest, icon + dp(36) + measure_text(cr, bold_font(), special_building(i).name, -1).width);
    }
    const double drop_w = std::min(static_cast<double>(win_w_ - 8), widest);
    double drop_x = anchor_x;
    if (drop_x + drop_w > win_w_ - 4) {
        drop_x = std::max(4.0, win_w_ - drop_w - 4);
    }
    double drop_y = anchor_y;
    const double drop_h = 8 + count * row_h;
    if (drop_y + drop_h > win_h_ - status_h_ - message_h_ - 4) {
        drop_y = std::max(static_cast<double>(menu_h_ + 4), win_h_ - status_h_ - message_h_ - 4 - drop_h);
    }
    fill(cr, drop_x, drop_y, drop_w, drop_h, theme::kMenuBgR, theme::kMenuBgG, theme::kMenuBgB);
    stroke_rect(cr, drop_x, drop_y, drop_w, drop_h, theme::kMenuFrameR, theme::kMenuFrameG, theme::kMenuFrameB);
    for (int i = 0; i < count; ++i) {
        const SpecialBuilding &building = special_building(i);
        const double y = drop_y + 4 + i * row_h;
        const bool lit = i == special_cursor_;
        if (lit) {
            fill(cr, drop_x + 3, y, drop_w - 6, row_h - 2, theme::chrome_select_r(), theme::chrome_select_g(), theme::chrome_select_b());
        }
        cairo_surface_t *sprite = tool_icon_named(building.icon);
        if (sprite != nullptr) {
            const int iw = cairo_image_surface_get_width(sprite);
            const int ih = cairo_image_surface_get_height(sprite);
            const double scale = icon_pixel_scale(std::max(iw, ih), static_cast<int>(icon));
            cairo_save(cr);
            cairo_translate(cr, std::floor(drop_x + 8), std::floor(y + (row_h - icon) / 2.0));
            cairo_scale(cr, scale, scale);
            cairo_set_source_surface(cr, sprite, 0, 0);
            cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
            cairo_paint(cr);
            cairo_restore(cr);
        }
        const std::string name = building.name;
        const std::string cost = session_.special_menu_detail(i);
        const double text_x = drop_x + 8 + icon + dp(8);
        const double text_r = lit ? 0.05 : theme::kTextR;
        const double text_g = lit ? 0.05 : theme::kTextG;
        const double text_b = lit ? 0.05 : theme::kTextB;
        const double text_y = y + std::max(4.0, (row_h - text_block) / 2.0);
        paint_text(cr, bold_font(), name, text_x, text_y, text_r, text_g, text_b,
                   static_cast<int>(drop_w - icon - dp(24)));
        paint_text(cr, ui_font(), cost, text_x, text_y + name_h + between, text_r, text_g, text_b, -1);
        push_hot(drop_x + 3, y, drop_w - 6, row_h - 2, kSpecialItem + i);
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
    fill(cr, 0, 0, win_w_, win_h_, theme::kWindowR, theme::kWindowG, theme::kWindowB);
    draw_menubar(cr);
    {
        TargetCr live(cr);
        cairo_translate(live.cr, side_x_, pal_y_);
        palette_.draw(live.cr);
    }
    push_hot(side_x_, pal_y_, side_w_, ToolPalette::height_for(cell_, palette_.columns()), kPaletteId);
    if (mini_h_ > 0) {
        minimap_.set_pin_top(false);
        const AspectBox probe = minimap_.content_box(side_w_, mini_h_);
        if (static_cast<double>(mini_h_) - probe.height >= 32.0) {
            minimap_.set_pin_top(true);
        }
        const AspectBox land = minimap_.content_box(side_w_, mini_h_);
        {
            TargetCr live(cr);
            cairo_translate(live.cr, side_x_, mini_y_);
            minimap_.sync();
            minimap_.draw(live.cr, side_w_, mini_h_);
        }
        push_hot(side_x_ + land.x, mini_y_ + land.y, land.width, land.height, kMinimapId);
        const double card_top = mini_y_ + land.y + land.height + 4.0;
        const double card_bottom = static_cast<double>(mini_y_ + mini_h_);
        if (card_bottom - card_top >= 28.0) {
            const ToolDef *card_tool = tool_by_index(tool_index_);
            std::string card_name = card_tool != nullptr ? card_tool->name : "Tool";
            std::string card_cost;
            if (card_tool != nullptr && card_tool->engine_id == kSpecialEngineId && special_choice_ >= 0 &&
                special_choice_ < special_building_count()) {
                card_name = special_building(special_choice_).name;
                card_cost = session_.special_menu_detail(special_choice_);
            } else if (card_tool != nullptr && card_tool->cost > 0) {
                card_cost = graph_money(card_tool->cost);
            } else {
                card_cost = "No charge";
            }
            if (!deck_ && !gamepad_nav_ && tool_index_ >= 0 && tool_index_ < 17) {
                const std::string key_name = chord_label(binds_.key[KeyTool0 + tool_index_]);
                if (!key_name.empty() && key_name != "None") {
                    card_cost += "  [" + key_name + "]";
                }
            }
            fill(cr, side_x_, card_top, side_w_, card_bottom - card_top, theme::kMenuBgR, theme::kMenuBgG, theme::kMenuBgB);
            stroke_rect(cr, side_x_, card_top, side_w_, card_bottom - card_top, theme::kMenuFrameR, theme::kMenuFrameG,
                        theme::kMenuFrameB);
            paint_text(cr, bold_font(), card_name, side_x_ + 6, card_top + 4, theme::kTextR, theme::kTextG, theme::kTextB,
                       side_w_ - 12);
            paint_text(cr, ui_font(), card_cost, side_x_ + 6, card_top + dp(20), theme::kTextR, theme::kTextG, theme::kTextB,
                       side_w_ - 12);
            if ((gamepad_nav_ || deck_) && card_bottom - card_top >= 52.0) {
                const char *use = glyph_for(PadConfirm);
                if (use[0] != '\0') {
                    paint_text(cr, ui_font(), std::string(use) + " uses", side_x_ + 6, card_top + dp(36), theme::kAccelR,
                               theme::kAccelG, theme::kAccelB, side_w_ - 12);
                }
            }
        }
    }
    {
        TargetCr live(cr);
        cairo_translate(live.cr, side_x_, demand_y_);
        demand_.draw(live.cr, side_w_, demand_h_, ui_scale_);
    }
    stroke_rect(cr, view_x_ - 1, view_y_ - 1, view_w_ + 2, view_h_ + 2, theme::kMenuFrameR, theme::kMenuFrameG,
                theme::kMenuFrameB);
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
            overlay_.draw_at(live.cr, map_.tile_size());
        }
        int query_x = 0;
        int query_y = 0;
        if (dialog_ == Dialog::Query && session_.query_tile(query_x, query_y)) {
            const int ts = std::max(1, map_.tile_size());
            cairo_set_source_rgb(live.cr, theme::kFocusR, theme::kFocusG, theme::kFocusB);
            cairo_set_line_width(live.cr, std::max(2.0, ts / 5.0));
            cairo_rectangle(live.cr, query_x * ts, query_y * ts, ts, ts);
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
    if (dialog_ == Dialog::None && !menu_.open() && tool_index_ == kToolCount - 1) {
        int hx = 0;
        int hy = 0;
        if (map_.hover_tile(hx, hy)) {
            const CitySession::TileInspect card = session_.inspect_tile(hx, hy);
            if (card.valid) {
                const double card_w = std::min(static_cast<double>(dp(220)), std::max(80.0, view_w_ * 0.34));
                const double card_h = card.offer_power ? dp(132) : dp(96);
                inspect_card_shown_ = true;
                inspect_card_w_ = card_w;
                inspect_card_h_ = card_h;
                const double card_x = view_x_ + view_w_ - card_w - dp(12);
                const double card_y = view_y_ + dp(8);
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
                if (card.offer_power) {
                    button(cr, card_x + 8, card_y + card_h - dp(34), card_w - 16, dp(26), "Show _power", kInspectPower, false);
                }
            }
        }
    }
    if (overlay_on_ && overlay_return_eval_ && dialog_ == Dialog::None) {
        const double chip_w = dp(180);
        const double chip_h = dp(28);
        const double chip_x = view_x_ + dp(8);
        const double chip_y = view_y_ + view_h_ - chip_h - dp(16);
        button(cr, chip_x, chip_y, chip_w, chip_h, "Back to Evaluation", kEvalShow, false);
    }
    if (card_id_ >= 0 && card_id_ < civic::kMilestones && dialog_ == Dialog::None && !map_.dragging()) {
        const double card_w = std::min(static_cast<double>(dp(280)), std::max(120.0, view_w_ * 0.4));
        const double card_h = dp(78);
        const double card_x = view_x_ + view_w_ - card_w - dp(12);
        const double card_y = view_y_ + view_h_ - card_h - dp(16);
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
    if (max_x > 0) {
        const double bw = std::max(20.0, view_w_ * (view_w_ / static_cast<double>(map_.pixel_width())));
        const double bx = view_x_ + (view_w_ - bw) * (scroll_x_ / max_x);
        fill(cr, bx, view_y_ + view_h_ - 8, bw, 6, theme::chrome_select_r(), theme::chrome_select_g(), theme::chrome_select_b());
    }
    if (max_y > 0) {
        const double bh = std::max(20.0, view_h_ * (view_h_ / static_cast<double>(map_.pixel_height())));
        const double by = view_y_ + (view_h_ - bh) * (scroll_y_ / max_y);
        fill(cr, view_x_ + view_w_ - 8, by, 6, bh, theme::chrome_select_r(), theme::chrome_select_g(), theme::chrome_select_b());
    }
    const int status_y = win_h_ - status_h_ - message_h_;
    fill(cr, 0, status_y, win_w_, status_h_, theme::kMenuBotR, theme::kMenuBotG, theme::kMenuBotB);
    const std::string hint = context_hint();
    const std::string clock = session_.scenario_clock_text();
    const std::string meter = session_.scenario_meter();
    // Funds, the name, the speed, the date, the clock, then the goal meter.
    // The meter used to be dropped so a long hint could stay. The hint is
    // what gives way, and it moves to the message line when that line is idle.
    std::string status = session_.funds_text() + "    " + session_.city_name() + "    " +
                         speed_word(welcome_gate_ ? 0 : speed_);
    const int full_room = std::max(1, win_w_ - 16);
    auto width_of = [&](const std::string &text) { return measure_text(cr, ui_font(), text, -1).width; };
    auto fits_in = [&](const std::string &text, int room) { return width_of(text) <= room; };
    if (fits_in(status + "    " + session_.date_text(), full_room)) {
        status += "    " + session_.date_text();
    }
    if (!clock.empty() && fits_in(status + "    " + clock, full_room)) {
        status += "    " + clock;
    }
    if (!meter.empty() && fits_in(status + "    " + meter, full_room)) {
        status += "    " + meter;
    }
    int hint_w = hint.empty() ? 0 : width_of(hint);
    // Pad hints stay on the message line. The status bar keeps funds, the
    // date, and the goal, which otherwise run into the edge on a Deck.
    const bool pad_hints_below = gamepad_nav_ || deck_;
    const bool hint_fits = !pad_hints_below && !hint.empty() && width_of(status) + 24 + hint_w <= win_w_;
    const int status_room = hint_fits ? std::max(1, win_w_ - hint_w - 24) : full_room;
    paint_text(cr, ui_font(), status, 8, status_y + 8, theme::kTextR, theme::kTextG, theme::kTextB, status_room);
    if (hint_fits) {
        paint_text(cr, ui_font(), hint, win_w_ - hint_w - 8, status_y + 8, theme::kTextR, theme::kTextG, theme::kTextB,
                   -1);
    }
    const auto now = std::chrono::steady_clock::now();
    const bool expired = !notice_sticky_ && (hint_until_.time_since_epoch().count() == 0 || now >= hint_until_);
    const NoticeDecision notice = decide_notice(session_.message_serial(), shown_message_serial_, expired, query_pinned_);
    std::string bar;
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
        hint_until_ = now + std::chrono::seconds(4);
        query_pinned_ = false;
        bar = shown_message_;
    } else if (!expired && !shown_message_.empty()) {
        bar = shown_message_;
    } else {
        const int hover = palette_.hover();
        const int special = special_tool_index();
        const bool show_keys = !deck_ && !gamepad_nav_;
        if (hover >= 0 && palette_.hover_ms() >= 350 && (hover == special || tool_by_index(hover) != nullptr)) {
            if (hover == special) {
                bar = session_.special_status_line(special_choice_);
            } else {
                const char *tip = palette_.tooltip();
                bar = tip != nullptr ? tip : "";
            }
            if (show_keys && hover >= 0 && hover < 17) {
                const std::string key_name = chord_label(binds_.key[KeyTool0 + hover]);
                if (!key_name.empty() && key_name != "None") {
                    if (!bar.empty()) {
                        bar += "  ";
                    }
                    bar += "[" + key_name + "]";
                }
            }
        } else if (!hint_fits && !hint.empty()) {
            bar = hint;
        } else {
            bar = idle_message();
        }
    }
    double bar_r = 0;
    double bar_g = 0;
    double bar_b = 0;
    double ink_r = 0;
    double ink_g = 0;
    double ink_b = 0;
    theme::banner_fill(bar_r, bar_g, bar_b);
    theme::banner_ink(ink_r, ink_g, ink_b);
    const BannerColors look_bar{bar_r, bar_g, bar_b, ink_r, ink_g, ink_b};
    const bool disaster_bar = notice_is_disaster(bar);
    const BannerColors message_bar = message_bar_colors(disaster_bar, look_bar);
    const BannerColors goal_bar = goal_banner_colors(look_bar);
    bar_r = message_bar.r;
    bar_g = message_bar.g;
    bar_b = message_bar.b;
    ink_r = message_bar.ink_r;
    ink_g = message_bar.ink_g;
    ink_b = message_bar.ink_b;
    fill(cr, 0, win_h_ - message_h_, win_w_, message_h_, bar_r, bar_g, bar_b);
    if (disaster_bar) {
        fill(cr, 0, win_h_ - message_h_, win_w_, 3, 1.0, 0.85, 0.2);
    }
    std::string bar_line = bar;
    const int bar_limit = std::max(8, win_w_ - 16);
    if (measure_text(cr, body_font(), bar_line, -1).width > bar_limit) {
        const std::string dots = "...";
        while (!bar_line.empty() && measure_text(cr, body_font(), bar_line + dots, -1).width > bar_limit) {
            bar_line.pop_back();
        }
        bar_line += dots;
    }
    paint_text(cr, body_font(), bar_line, 8, win_h_ - message_h_ + 10, ink_r, ink_g, ink_b, win_w_ - 16);
    notice_bar_y_ = -1;
    notice_bar_h_ = 0;
    if (notice_sticky_ && !notice_text_.empty()) {
        if (now >= notice_until_) {
            dismiss_background_notice();
        } else {
            const int notice_wrap = std::max(8, win_w_ - 16);
            const TextMetrics notice_m = measure_text(cr, ui_font(), notice_text_, notice_wrap);
            notice_bar_h_ = static_cast<double>(notice_m.height + dp(10));
            notice_bar_y_ = static_cast<double>(status_y) - notice_bar_h_;
            fill(cr, 0, notice_bar_y_, win_w_, notice_bar_h_, 1.0, 0.96, 0.72);
            stroke_rect(cr, 0, notice_bar_y_, win_w_, notice_bar_h_, 0.55, 0.32, 0.08);
            paint_text(cr, ui_font(), notice_text_, 8, notice_bar_y_ + dp(4), 0.25, 0.12, 0.04, notice_wrap);
        }
    }
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
    if (goal_visible && dialog_ == Dialog::None && view_w_ > dp(32)) {
        const int pad = dp(8);
        double room = 0;
        const bool beside = goal_banner_room(view_w_, pad, inspect_card_w_, inspect_card_shown_, room);
        double gy = view_y_ + pad;
        if (!beside) {
            gy = view_y_ + pad + inspect_card_h_ + pad;
            room = std::max(0.0, static_cast<double>(view_w_) - pad * 2.0);
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
        fill(cr, gx, gy, gw, gh, goal_bar.r, goal_bar.g, goal_bar.b);
        stroke_rect(cr, gx, gy, gw, gh, edge_r, edge_g, edge_b);
        paint_text(cr, ui_font(), goal, gx + pad, gy + pad, goal_bar.ink_r, goal_bar.ink_g, goal_bar.ink_b, wrap);
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
        double sy = view_y_ + dp(28);
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
        const double lx = view_x_ + view_w_ - lw - pad;
        const double ly = view_y_ + pad;
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
    draw_menu_dropdown(cr);
    draw_special_menu(cr);
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
    if (special_open_) {
        return pad ? hint_join({hint_button(a, "selects"), hint_button(b, "closes")}) : "Enter selects. Esc closes";
    }
    const ToolDef *tool = tool_by_index(tool_index_);
    const int engine = tool != nullptr ? tool->engine_id : TOOL_QUERY;
    if (overlay_on_) {
        return pad ? hint_join({tool_press_phrase(engine, a.c_str()), hint_button(b, "closes the map"), "D-pad scrolls"})
                   : std::string(tool_click_phrase(engine)) + ". Esc closes the map";
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
        const std::string query = glyph_for(PadQuery);
        const std::string pause = glyph_for(PadPause);
        return hint_join({tool_press_phrase(engine, a.c_str()), hint_button(query, "query"),
                          hint_button(pause, speed_ <= 0 ? "resumes" : "pauses"), hint_pair(left, right, "tool"),
                          hint_button(menu, "menu")});
    }
    if (finger) {
        std::string phrase = tool_click_phrase(engine);
        const auto at = phrase.find("click to ");
        if (at != std::string::npos) {
            phrase.replace(at, 9, "tap to ");
        }
        return phrase;
    }
    const std::string pause_key = chord_label(binds_.key[KeyPause]);
    std::string line = std::string(tool_click_phrase(engine)) + ". Arrows pan";
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
    const std::int64_t delta = digest.closing_funds - digest.opening_funds;
    const std::string flow =
        std::string(delta >= 0 ? "Surplus " : "Deficit ") + civic::dollars(delta >= 0 ? delta : -delta);
    const std::string funds_body = "Open " + civic::dollars(digest.opening_funds) + "\nClose " +
                                   civic::dollars(digest.closing_funds) + "\n" + flow;
    std::string people_body = digest.card.population;
    const auto paren = people_body.rfind(" (");
    if (paren != std::string::npos) {
        people_body.replace(paren, 2, "\n(");
    }
    const CitySession::CensusMix mix = session_.census_mix();
    const int mix_total = mix.homes + mix.shops + mix.jobs;
    if (mix_total > 0) {
        const int homes = mix.homes * 100 / mix_total;
        const int shops = mix.shops * 100 / mix_total;
        const int jobs = 100 - homes - shops;
        people_body += "\nR " + std::to_string(homes) + "%   C " + std::to_string(shops) + "%   I " +
                       std::to_string(jobs) + "%";
    }
    std::string standing = report.no_residents ? "No residents yet, so there is no poll."
                                               : report.category + ". Score " + std::to_string(report.score) + ".";
    if (!report.no_residents) {
        standing += "\n" + std::to_string(report.yes_percent) + "% in favor.";
    }
    std::string highlights;
    int shown = 0;
    for (const civic::DigestLine &event : digest.events) {
        if (event.text.find("No disaster") != std::string::npos) {
            continue;
        }
        if (!highlights.empty()) {
            highlights += "\n";
        }
        highlights += event.text;
        if (++shown >= 4) {
            break;
        }
    }
    if (highlights.empty()) {
        highlights = "A quiet year. No disaster or milestone was recorded.";
    }
    std::string challenges = digest.card.problem;
    if (!digest.card.intention.empty()) {
        if (!challenges.empty()) {
            challenges += "\n";
        }
        challenges += digest.card.intention;
    }
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
        cairo_set_source_rgb(cr, gold_r, gold_g, gold_b);
        cairo_set_line_width(cr, std::max(2.0, static_cast<double>(dp(2))));
        cairo_stroke(cr);
        round_rect(cr, x + 2.5, y + 2.5, w - 5, h - 5, dp(4));
        cairo_set_source_rgba(cr, 1, 1, 1, primary ? 0.28 : 0.45);
        cairo_set_line_width(cr, 1);
        cairo_stroke(cr);
        if (focus_id_ == id) {
            round_rect(cr, x + 4, y + 4, w - 8, h - 8, dp(3));
            cairo_set_source_rgb(cr, primary ? 1.0 : 0.15, primary ? 1.0 : 0.12, primary ? 1.0 : 0.08);
            cairo_set_line_width(cr, 1.5);
            cairo_stroke(cr);
        }
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
