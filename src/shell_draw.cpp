// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#include "shell_state.hpp"

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
#include "notice_bar.hpp"
#include "osk_layout.hpp"
#include "redraw.hpp"
#include "text_paint.hpp"
#include "theme.hpp"
#include "scenario_preview.hpp"
#include "special_buildings.hpp"
#include "tool_keys.hpp"
#include "tools.hpp"
#include "version.hpp"

#include "micropolis.h"

#include <cstdlib>
#include "version.hpp"
#include "view_math.hpp"

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
    SDL_GetWindowSizeInPixels(window_, &win_w_, &win_h_);
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

void Shell::button(cairo_t *cr, double x, double y, double w, double h, const char *label, int id, bool primary)
{
    push_hot(x, y, w, h, id);
    push_focus(id);
    // A modal covers the city. Anything that is not part of the dialog must
    // not light up with the dialog's focus.
    const bool suppressed = dialog_ != Dialog::None && !is_modal_control(id);
    const bool hot = !suppressed && (hover_id_ == id || focus_id_ == id);
    const bool pressed = hot && pointer_down_ && hover_id_ == id;
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

void Shell::frame_panel(cairo_t *cr, double x, double y, double w, double h, const char *title)
{
    cairo_set_source_rgba(cr, 0, 0, 0, 0.35);
    cairo_rectangle(cr, 0, 0, win_w_, win_h_);
    cairo_fill(cr);
    push_hot(0, 0, win_w_, win_h_, 0);
    fill(cr, x, y, w, h, theme::kWindowR, theme::kWindowG, theme::kWindowB);
    const double kTitleBand = dp(40);
    fill(cr, x, y, w, kTitleBand, theme::chrome_select_r(), theme::chrome_select_g(), theme::chrome_select_b());
    const TextMetrics title_metrics = measure_text(cr, title_font(), title, static_cast<int>(w - 24));
    const double title_y = y + std::max(2.0, (kTitleBand - title_metrics.height) / 2.0);
    // Pad players read the hint in the title. Mouse players read it in the
    // status bar, so it is not repeated in both places.
    const bool title_hint = gamepad_nav_ || deck_ || touch_used_;
    const std::string hint = title_hint ? dialog_title_hint() : std::string();
    const TextMetrics hint_metrics = hint.empty() ? TextMetrics{} : measure_text(cr, ui_font(), hint, -1);
    const int title_wrap = static_cast<int>(std::max(40.0, w - 24 - (hint.empty() ? 0 : hint_metrics.width + 16)));
    paint_text(cr, title_font(), title, x + 12, title_y, 1, 1, 1, title_wrap);
    if (!hint.empty() && hint_metrics.width + 16 < w - title_metrics.width - 36) {
        paint_text(cr, ui_font(), hint, x + w - hint_metrics.width - 12, y + (kTitleBand - hint_metrics.height) / 2.0, 1,
                   1, 1, -1);
    }
    stroke_rect(cr, x, y, w, h, theme::kMenuFrameR, theme::kMenuFrameG, theme::kMenuFrameB);
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
    // row (the underline used to spill into the next line) and scrolls when
    // the column still will not fit above the status bar.
    int n_item = 0;
    int n_sep = 0;
    for (int i = 0; i < count; ++i) {
        if (items[i].kind == ItemKind::Separator) {
            ++n_sep;
        } else {
            ++n_item;
        }
    }
    const double avail = std::max(80.0, static_cast<double>(win_h_ - menu_h_ - status_h_ - message_h_ - 6));
    const int label_h = measure_text(cr, menu_font(), "Ag", -1).height;
    const menu_table::MenuPitch pitch = menu_table::menu_pitch(avail, n_item, n_sep, dp(36), label_h);
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
        // Snap to a row start. Stopping at content-avail used to hide the
        // last row: the next aligned step is a few pixels past that, and the
        // menu box simply gets shorter instead of slicing a row.
        int row = menu_table::menu_scroll_row(menu_item_top_, shown, menu_scroll_);
        if (row < 0) {
            row = 0;
        }
        if (row >= shown) {
            row = shown - 1;
        }
        const double end = menu_table::menu_reveal_scroll(menu_item_top_, menu_item_h_, shown, shown - 1, avail, content_h);
        // A separator at the top of a scrolled menu is a stray line. Step
        // past it when that still shows the last row; otherwise step back.
        if (row < shown && items[row].kind == ItemKind::Separator) {
            const int forward = menu_table::menu_visible_top(items, shown, row, 1);
            const double forward_scroll = menu_table::menu_row_scroll(menu_item_top_, shown, forward);
            if (forward != row && forward_scroll <= end + 0.5) {
                row = forward;
            } else {
                row = menu_table::menu_visible_top(items, shown, row, -1);
            }
        }
        menu_scroll_ = menu_table::menu_row_scroll(menu_item_top_, shown, row);
        if (menu_scroll_ > end) {
            menu_scroll_ = end;
        }
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
    const double drop_h = content_h <= avail + 0.5 ? content_h : std::min(avail, visible_bottom - menu_h_ + 4.0);
    if (drop_x + drop_w > win_w_ - 4) {
        drop_x = std::max(4.0, win_w_ - drop_w - 4);
    }
    menu_drop_x_ = drop_x;
    menu_drop_w_ = drop_w;
    menu_drop_h_ = drop_h;
    const bool menu_scrolls = content_h > avail + 0.5;
    const double row_w = menu_scrolls ? drop_w - 18.0 : drop_w - 4.0;
    // A short offset, not a blur. The same shadow is used in every look.
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
        paint_mnemonic(cr, menu_font(), item.label, drop_x + 28, text_y, menu_.alt_shown, theme::kTextR,
                       theme::kTextG, theme::kTextB);
        if (item.kind == ItemKind::Submenu) {
            paint_text(cr, menu_font(), ">", drop_x + drop_w - dp(28), text_y, theme::kTextR, theme::kTextG,
                       theme::kTextB, -1);
        }
        if (item.action == Action::Recent) {
            recent_row_y = y;
            have_recent_row = true;
        }
        if (item.accel_text != nullptr && !gamepad_nav_) {
            const TextMetrics accel = measure_text(cr, menu_font(), item.accel_text, -1);
            const double accel_pad = menu_scrolls ? 22.0 : 12.0;
            paint_text(cr, menu_font(), item.accel_text, drop_x + drop_w - accel.width - accel_pad, y + pitch.text_dy,
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

void Shell::draw_dialog(cairo_t *cr)
{
    if (dialog_ == Dialog::None) {
        return;
    }
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
        const double bh = dp(36);
        const int count = static_cast<int>(buttons.size());
        left_count = std::max(0, std::min(left_count, count));
        std::vector<double> widths(static_cast<std::size_t>(count), dp(120));
        for (int i = 0; i < count; ++i) {
            const TextMetrics metrics = measure_text(cr, ui_font(), strip_mnemonic(buttons[static_cast<std::size_t>(i)].first), -1);
            widths[static_cast<std::size_t>(i)] = button_width(metrics.width, dp(32), dp(120));
        }
        double left = px + margin;
        for (int i = 0; i < left_count; ++i) {
            const auto &button_def = buttons[static_cast<std::size_t>(i)];
            button(cr, left, by, widths[static_cast<std::size_t>(i)], bh, button_def.first, button_def.second,
                   button_def.second == primary_id);
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
                   button_def.second == primary_id);
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
    auto paint_caret = [&](double tx, double ty, double th, const std::string &text, bool focused) {
        if (!focused) {
            return;
        }
        field_clamp(text, field_caret_, field_anchor_);
        const int from = std::min(field_caret_, field_anchor_);
        const int to = std::max(field_caret_, field_anchor_);
        if (from != to) {
            const int x0 = measure_text(cr, body_font(), text.substr(0, static_cast<std::size_t>(from)), -1).width;
            const int x1 = measure_text(cr, body_font(), text.substr(0, static_cast<std::size_t>(to)), -1).width;
            cairo_set_source_rgba(cr, theme::chrome_select_r(), theme::chrome_select_g(), theme::chrome_select_b(), 0.55);
            cairo_rectangle(cr, tx + x0, ty - dp(2), std::max(2, x1 - x0), th);
            cairo_fill(cr);
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
        const std::string heading = "Welcome to Lunduke City 2";
        const std::string blurb = "Start a new city, load one you saved, or take on a scenario.";
        const double width = message_width(win_w_, margin, dp(440), dp(520));
        const double column = std::max(1.0, width - 2.0 * margin);
        const double heading_h = line_height(title_font(), heading, column);
        const double blurb_h = text_block_height(blurb, column);
        const double bh = dp(44);
        const double gap = dp(10);
        // The key hint lives in the title bar (pad) or the status bar (mouse),
        // not again under the buttons.
        const double height = dp(40) + margin + heading_h + dp(8) + blurb_h + dp(16) + bh * 4.0 + gap * 3.0 + margin;
        const DialogRect panel = place_dialog(win_w_, band_h, width, height, margin);
        frame_panel(cr, panel.x, panel.y, panel.w, panel.h, "Welcome");
        double text_y = panel.y + dp(40) + dp(12);
        text_y += place_line(title_font(), heading, panel.x + margin, text_y, panel.w - 2 * margin, theme::kTextR,
                             theme::kTextG, theme::kTextB);
        text_y += dp(8);
        text_y += place_line(body_font(), blurb, panel.x + margin, text_y, panel.w - 2 * margin, theme::kTextR, theme::kTextG,
                             theme::kTextB);
        const double bw = panel.w - 2 * margin;
        double by = text_y + dp(16);
        button(cr, panel.x + margin, by, bw, bh, "_Start a New City", kWelcomeNew, focus_id_ == kWelcomeNew);
        by += bh + gap;
        button(cr, panel.x + margin, by, bw, bh, "_Load a Saved City", kWelcomeLoad, focus_id_ == kWelcomeLoad);
        by += bh + gap;
        button(cr, panel.x + margin, by, bw, bh, "Select a _Scenario", kWelcomeScenario, focus_id_ == kWelcomeScenario);
        by += bh + gap;
        button(cr, panel.x + margin, by, bw, bh, "_Quit", kWelcomeQuit, focus_id_ == kWelcomeQuit);
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
                   {{"_Discard", kDiscard}, {"_Cancel", kCancel}, {"_Save", kSave}}, 1);
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
        label(cr, body_font(), std::string(question) + "\n" + detail, panel.x + margin, panel.y + dp(48),
              static_cast<int>(panel.w - 2 * margin));
        buttons_at(panel.x, panel.w, panel.y + panel.h - margin - dp(36), {{"_Start", kOk}, {"_Cancel", kCancel}}, 1);
        return;
    }
    if (dialog_ == Dialog::NewCity) {
        const bool osk = osk_visible() && new_page_ == 0;
        const std::string land_intro =
            "Pick a difficulty and the kind of land. Default keeps the usual rivers, lakes, and trees.";
        double want_w = new_page_ == 1 ? dp(640) : dp(520);
        double want_h = new_page_ == 1 ? dp(560) : new_page_ == 0 ? dp(190) : dp(170);
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
            push_focus(kFieldName);
            paint_text(cr, body_font(), field_a_.empty() ? " " : field_a_, x + margin + dp(8), field_y + dp(8),
                       theme::kTextR, theme::kTextG, theme::kTextB, static_cast<int>(dw - 2 * margin - dp(16)));
            const bool name_editing = focus_id_ == kFieldName || (focus_id_ >= kOsk && focus_id_ < kOsk + 64);
            paint_caret(x + margin + dp(8), field_y + dp(8), dp(18), field_a_, name_editing);
            double below = field_y + dp(36);
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
                           kChoice + g * 10 + o, selected[g] == o);
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
                paint_caret(seed_x + inset - shift, seed_y + dp(6), dp(18), field_b_, focus_id_ == kFieldSeed);
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
                            unsigned char r = 219;
                            unsigned char gch = 148;
                            unsigned char b = 66;
                            if (t >= RIVER && t <= WATER_HIGH) {
                                r = 15;
                                gch = 61;
                                b = 199;
                            } else if (t >= TREEBASE && t <= WOODS5) {
                                r = 13;
                                gch = 122;
                                b = 26;
                            } else if (t >= RESBASE && t < COMBASE) {
                                r = 41;
                                gch = 158;
                                b = 41;
                            } else if (t >= ROADBASE && t <= LASTRAIL) {
                                r = 70;
                                gch = 70;
                                b = 74;
                            }
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
            const std::string summary = "Generate \"" + field_a_ + "\". This replaces the city on the map.";
            paint_text(cr, ui_font(), summary, x + margin, summary_y, theme::kTextR, theme::kTextG, theme::kTextB,
                       static_cast<int>(dw - 2 * margin));
            buttons_at(x, dw, y + dh - margin - dp(36), {{"_Back", kBack}, {"_Generate", kGenerate}});
        } else {
            // A saved page index from an older session. Generate is on page 2 now.
            const std::string summary = "Generate \"" + field_a_ + "\".\nThis replaces the city on the map.";
            label(cr, body_font(), summary, x + margin, y + dp(52), static_cast<int>(dw - 2 * margin));
            buttons_at(x, dw, y + dh - margin - dp(36), {{"_Back", kBack}, {"_Generate", kGenerate}});
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
        label(cr, ui_font(), "Choose a scenario. Playing it replaces the city on the map.", x + margin, y + dp(46),
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
        paint_caret(x + margin + dp(8), field_y + dp(8), dp(18), field_a_, rename_editing);
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
                if (!glance.ok && !glance.summary.empty()) {
                    paint_text(cr, ui_font(), glance.summary, text_x, ry + 26, theme::kAccelR, theme::kAccelG,
                               theme::kAccelB, static_cast<int>(row_w - (text_x - x)));
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
                    paint_text(cr, ui_font(), glance.summary, text_x, ry + 26, theme::kAccelR, theme::kAccelG,
                               theme::kAccelB, static_cast<int>(row_w - (text_x - x)));
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
            paint_caret(x + 24, name_y + dp(8), dp(18), field_a_, save_editing);
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
        buttons_at(x, dw, buttons_top, {{"_Up", kUp}, {"_Cancel", kCancel}, {open_label, open_id}}, 1);
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
        const double pitch = dp(64);
        const double height = dp(40) + margin + pitch * 2 + dp(88) + dp(48) + dp(12) + dp(36) + margin;
        const DialogRect panel = place_dialog(win_w_, band_h, dp(680), height, margin);
        const double x = panel.x;
        const double y = panel.y;
        const double dw = panel.w;
        frame_panel(cr, x, y, dw, panel.h, "Settings");
        auto slider = [&](int index, const std::string &caption, const std::string &value_text, int value, int min,
                          int max, double sy) {
            paint_text(cr, bold_font(), caption, x + margin, sy, theme::kTextR, theme::kTextG, theme::kTextB,
                       static_cast<int>(dw - 2 * margin - dp(80)));
            const TextMetrics value_metrics = measure_text(cr, bold_font(), value_text, -1);
            paint_text(cr, bold_font(), value_text, x + dw - margin - value_metrics.width, sy, theme::kTextR,
                       theme::kTextG, theme::kTextB, -1);
            const double bar_y = sy + dp(28);
            const double sx = x + margin;
            const double sw = dw - 2 * margin;
            fill(cr, sx, bar_y, sw, dp(16), 1, 1, 1);
            const double t = (value - min) / static_cast<double>(std::max(1, max - min));
            fill(cr, sx, bar_y, sw * t, dp(16), theme::chrome_select_r(), theme::chrome_select_g(), theme::chrome_select_b());
            const double thumb = dp(14);
            fill(cr, sx + sw * t - thumb / 2.0, bar_y - dp(4), thumb, dp(24), 1, 1, 1);
            stroke_rect(cr, sx + sw * t - thumb / 2.0, bar_y - dp(4), thumb, dp(24), theme::chrome_select_r(), theme::chrome_select_g(),
                        theme::chrome_select_b());
            stroke_rect(cr, sx, bar_y, sw, dp(16), theme::kButtonEdgeR, theme::kButtonEdgeG, theme::kButtonEdgeB);
            const int id = kSlider + index;
            push_hot(sx, bar_y - dp(6), sw, dp(28), id);
            push_focus(id);
            next_sliders_.push_back(SliderGeom{id, sx, bar_y - dp(6), sw, static_cast<double>(dp(28)), min, max});
            if (focus_id_ == id) {
                cairo_set_source_rgb(cr, theme::kFocusR, theme::kFocusG, theme::kFocusB);
                cairo_set_line_width(cr, 2);
                cairo_rectangle(cr, sx + 1, bar_y - dp(6), sw - 2, dp(32));
                cairo_stroke(cr);
            }
        };
        const bool scale_auto = scale_percent_ <= 0;
        const std::string scale_text = scale_auto ? "Auto" : std::to_string(scale_percent_) + "%";
        slider(1, "Volume", std::to_string(volume_) + "%", volume_, 0, 100, y + dp(48));
        if (!scale_auto) {
            slider(2, "UI scale", scale_text, scale_percent_, 50, 200, y + dp(48) + pitch);
        } else {
            paint_text(cr, bold_font(), "UI scale", x + margin, y + dp(48) + pitch, theme::kDisabledR, theme::kDisabledG,
                       theme::kDisabledB, static_cast<int>(dw - 2 * margin));
            paint_text(cr, bold_font(), "Auto", x + dw - margin - dp(48), y + dp(48) + pitch, theme::kDisabledR,
                       theme::kDisabledG, theme::kDisabledB, -1);
            fill(cr, x + margin, y + dp(48) + pitch + dp(28), dw - 2 * margin, dp(16), 0.86, 0.86, 0.86);
        }
        const char *pause_label = pause_unfocused_ ? "Pause when unfocused: On" : "Pause when unfocused: Off";
        const char *auto_label = scale_auto ? "Scale: Auto" : "Scale: Manual";
        const char *wheel_label = wheel_zooms_ ? "Mouse wheel: Zoom" : "Mouse wheel: Scroll";
        const bool full = window_ != nullptr && (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) != 0;
        const char *full_label = full ? "Fullscreen: On" : "Fullscreen: Off";
        buttons_at(x, dw, y + dp(48) + pitch * 2 + dp(4), {{auto_label, kScaleAuto}, {pause_label, kReset}});
        buttons_at(x, dw, y + dp(48) + pitch * 2 + dp(48), {{wheel_label, kWheelZoom}, {full_label, kFullscreenBox}});
        buttons_at(x, dw, y + panel.h - margin - dp(36), {{"_Cancel", kCancel}, {"_OK", kOk}});
        return;
    }
    if (dialog_ == Dialog::Controls) {
        const std::string heading = controls_page_title(controls_page_, deck_);
        ControlsPadNames pad_names;
        pad_names.confirm = glyph(SDL_GAMEPAD_BUTTON_SOUTH);
        pad_names.cancel = glyph(SDL_GAMEPAD_BUTTON_EAST);
        pad_names.west = glyph(SDL_GAMEPAD_BUTTON_WEST);
        pad_names.north = glyph(SDL_GAMEPAD_BUTTON_NORTH);
        pad_names.left = glyph(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
        pad_names.right = glyph(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
        pad_names.menu = glyph(SDL_GAMEPAD_BUTTON_START);
        pad_names.view = glyph(SDL_GAMEPAD_BUTTON_BACK);
        pad_names.l3 = glyph(SDL_GAMEPAD_BUTTON_LEFT_STICK);
        pad_names.r3 = glyph(SDL_GAMEPAD_BUTTON_RIGHT_STICK);
        const std::string intro = std::string(heading) + "\n" + controls_page_body(controls_page_, deck_, pad_names);
        const bool tools = controls_page_has_tools(controls_page_, deck_);
        const std::string col_a = tools ? controls_tool_column(0, 9) : std::string();
        const std::string col_b = tools ? controls_tool_column(9, 17) : std::string();
        const double width = message_width(win_w_, margin, dp(520), dp(860));
        const double inner = width - 2 * margin;
        const double intro_h = text_block_height(intro, inner);
        const double col_w = tools ? (inner - dp(16)) / 2.0 : inner;
        const double col_h = tools ? std::max(text_block_height(col_a, col_w), text_block_height(col_b, col_w)) : 0.0;
        const double content_h = intro_h + (tools ? dp(8) + col_h : 0.0);
        const double footer_h = dp(22);
        const double buttons_h = dp(36);
        const double chrome = dp(40) + margin + footer_h + dp(8) + buttons_h + margin;
        const double max_h = std::max(chrome + dp(48), band_h - 2.0 * margin);
        const double view_h = std::max(static_cast<double>(dp(48)), std::min(content_h, max_h - chrome));
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
        paint_text(cr, ui_font(), "Left and right change page.", panel.x + margin, footer_y, theme::kTextR,
                   theme::kTextG, theme::kTextB, static_cast<int>(panel.w - 2 * margin));
        buttons_at(panel.x, panel.w, panel.y + panel.h - margin - buttons_h, {{"_Close", kCancel}});
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
        const double blocks = block_h(copy.tax_title, copy.tax_value, "") +
                              block_h("Road fund", std::to_string(road_) + "%", copy.road) +
                              block_h("Police fund", std::to_string(police_) + "%", copy.police) +
                              block_h("Fire fund", std::to_string(fire_) + "%", copy.fire);
        const double height = dp(40) + margin + row1 + gap + row2 + gap + note_h + blocks + dp(8) + dp(36) + margin;
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
        const double taxes_h =
            place_line(ui_font(), taxes, x + margin, cursor, live_col, theme::kTextR, theme::kTextG, theme::kTextB);
        const double previous_h = place_line(ui_font(), previous, x + margin + live_col + col_gap, cursor, live_col,
                                             theme::kTextR, theme::kTextG, theme::kTextB);
        cursor += std::max(taxes_h, previous_h) + gap;
        const double flow_h =
            place_line(ui_font(), flow, x + margin, cursor, live_col, theme::kTextR, theme::kTextG, theme::kTextB);
        const double funds_h = place_line(ui_font(), funds, x + margin + live_col + col_gap, cursor, live_col,
                                          theme::kTextR, theme::kTextG, theme::kTextB);
        cursor += std::max(flow_h, funds_h) + gap;
        if (!copy.closing_note.empty()) {
            const double h = place_line(ui_font(), copy.closing_note, x + margin, cursor, live_inner, theme::kAccelR,
                                        theme::kAccelG, theme::kAccelB);
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
                const double detail_h = place_line(ui_font(), detail, x + margin, cursor, live_inner, theme::kAccelR,
                                                   theme::kAccelG, theme::kAccelB);
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
            fill(cr, sx + sw * t - thumb / 2.0, bar_y - dp(4), thumb, dp(24), 1, 1, 1);
            stroke_rect(cr, sx + sw * t - thumb / 2.0, bar_y - dp(4), thumb, dp(24), theme::chrome_select_r(), theme::chrome_select_g(),
                        theme::chrome_select_b());
            stroke_rect(cr, sx, bar_y, sw, dp(16), theme::kButtonEdgeR, theme::kButtonEdgeG, theme::kButtonEdgeB);
            const int id = kSlider + index;
            push_hot(sx, bar_y - dp(6), sw, dp(28), id);
            push_focus(id);
            next_sliders_.push_back(SliderGeom{id, sx, bar_y - dp(6), sw, static_cast<double>(dp(28)), min, max});
            if (focus_id_ == id) {
                cairo_set_source_rgb(cr, theme::kFocusR, theme::kFocusG, theme::kFocusB);
                cairo_set_line_width(cr, 2);
                cairo_rectangle(cr, sx + 1, bar_y - dp(4), sw - 2, dp(24));
                cairo_stroke(cr);
            }
            cursor += dp(16) + tail;
        };
        slider(1, copy.tax_title, copy.tax_value, "", tax_, 0, 20);
        auto funded = [](int percent, std::int64_t need) {
            return std::to_string(percent) + "%  " + graph_money(need * percent / 100);
        };
        slider(2, "Road fund", funded(road_, book.road_need), copy.road, road_, 0, 100);
        slider(3, "Police fund", funded(police_, book.police_need), copy.police, police_, 0, 100);
        slider(4, "Fire fund", funded(fire_, book.fire_need), copy.fire, fire_, 0, 100);
        if (budget_is_tax_) {
            buttons_at(x, dw, y + dh - margin - dp(36), {{"_Reset", kReset}, {"_Collect Taxes", kCollect}});
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
        button(cr, x + margin, y + dp(46), dp(140), dp(32), "10 year", kShortScale, !graphs_long_);
        button(cr, x + margin + dp(148), y + dp(46), dp(140), dp(32), "120 year", kLongScale, graphs_long_);
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
                               static_cast<int>(chart_h), graph_mask_, graph_pointer_x_, &graph_readout_);
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
        std::vector<Row> right_rows = {{bold_font(), copy.problems_caption}};
        for (const std::string &problem : copy.problems) {
            right_rows.push_back(Row{body_font(), problem});
        }
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
        const double right_h = column_h(right_rows, col_w);
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
            const double wide_right = column_h(right_rows, wide_col);
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
            const double tight_right = column_h(right_rows, col_w);
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
                const double h = place_line(row.font, row.text, lx, cursor, live_col, theme::kTextR, theme::kTextG,
                                            theme::kTextB);
                cursor += h + gap;
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
        paint_rows(right_rows, right_x, right_y);
        buttons_at(x, dw, y + panel.h - margin - dp(36), {{"_Close", kClose}});
        return;
    }
    if (dialog_ == Dialog::Messages) {
        const DialogRect panel = place_dialog(win_w_, band_h, large_w, std::min(large_h, static_cast<double>(dp(520))), margin);
        const double x = panel.x;
        const double y = panel.y;
        const double dw = panel.w;
        const double dh = panel.h;
        frame_panel(cr, x, y, dw, dh, "Messages");
        label(cr, ui_font(), "Recent news. Choosing a row jumps to that place.", x + margin, y + dp(46),
              static_cast<int>(dw - 2 * margin));
        const auto &notes = session_.notices();
        const double list_top = y + dp(72);
        const double buttons_y = y + dh - margin - dp(36);
        const int row_px = dp(36);
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
                const std::string line = std::string(city_glance_month(note.month)) + " " + std::to_string(note.year) +
                                         "  " + note.text;
                paint_text(cr, ui_font(), line, x + margin + dp(8), ry + dp(8), theme::kTextR, theme::kTextG,
                           theme::kTextB, static_cast<int>(dw - 2 * margin - dp(16)));
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
        label(cr, body_font(), info_body_, panel.x + margin, panel.y + dp(48), static_cast<int>(panel.w - 2 * margin));
        buttons_at(panel.x, panel.w, panel.y + panel.h - margin - dp(36), {{"_Close", kClose}});
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
        const std::string sample = "$" + std::to_string(building.cost);
        cost_h = std::max(cost_h, measure_text(cr, ui_font(), sample, -1).height);
    }
    const int between = dp(6);
    const int text_block = name_h + between + cost_h;
    const double row_h = std::max(std::max(static_cast<double>(dp(44)), static_cast<double>(cell_ - 4)),
                                  static_cast<double>(text_block + dp(12)));
    const double icon = std::min(static_cast<double>(dp(40)), row_h - dp(8));
    const double drop_w = std::max(static_cast<double>(dp(240)), icon + dp(150));
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
        const std::string cost = "$" + std::to_string(building.cost);
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
    if (show_osk() && dialog_ != Dialog::None) {
        osk_latched_ = true;
    }
    theme::set_chrome_look(drawn_look_);
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
        TargetCr live(cr);
        cairo_translate(live.cr, side_x_, mini_y_);
        minimap_.sync();
        minimap_.draw(live.cr, side_w_, mini_h_);
        push_hot(side_x_, mini_y_, side_w_, mini_h_, kMinimapId);
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
        cairo_translate(live.cr, snapped_map_origin(view_x_, map_ox_, scroll_x_),
                        snapped_map_origin(view_y_, map_oy_, scroll_y_));
        if (gamepad_nav_ && !pad_dragging_ && !map_.dragging() && dialog_ == Dialog::None && !menu_.open()) {
            // The footprint outlines the target tile before A is pressed.
            map_.motion(map_local_x(cursor_x_), map_local_y(cursor_y_), false);
        }
        map_.sync();
        map_.draw(live.cr);
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
    push_hot(view_x_, view_y_, view_w_, view_h_, kMapId);
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
    if (notice_sticky_ && !shown_message_.empty()) {
        bar = shown_message_;
    } else if (notice.show_notice && !session_.message().empty()) {
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
        if (hover >= 0 && (hover == special || tool_by_index(hover) != nullptr)) {
            if (hover == special) {
                bar = special_status_text(special_choice_);
            } else {
                const char *tip = palette_.tooltip();
                bar = tip != nullptr ? tip : "";
            }
            if (show_keys) {
                bar = tool_hint_with_key(hover, bar.c_str());
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
    fill(cr, 0, win_h_ - message_h_, win_w_, message_h_, bar_r, bar_g, bar_b);
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
    const std::string goal = session_.scenario_progress();
    if (session_.scenario_id() != goal_scenario_) {
        goal_scenario_ = session_.scenario_id();
        goal_dismissed_ = false;
        goal_until_ = now + std::chrono::seconds(6);
    }
    const bool goal_visible = !goal.empty() && !goal_dismissed_ && now < goal_until_;
    if (goal_visible && dialog_ == Dialog::None && view_w_ > dp(32)) {
        const int pad = dp(8);
        const int wrap = std::max(1, view_w_ - pad * 4);
        const TextMetrics metrics = measure_text(cr, ui_font(), goal, wrap);
        const double gx = view_x_ + pad;
        const double gy = view_y_ + pad;
        const double gw = std::min(static_cast<double>(view_w_ - pad * 2), static_cast<double>(metrics.width + pad * 2));
        const double gh = static_cast<double>(metrics.height + pad * 2);
        double edge_r = 0;
        double edge_g = 0;
        double edge_b = 0;
        theme::banner_edge(edge_r, edge_g, edge_b);
        fill(cr, gx, gy, gw, gh, bar_r, bar_g, bar_b);
        stroke_rect(cr, gx, gy, gw, gh, edge_r, edge_g, edge_b);
        paint_text(cr, ui_font(), goal, gx + pad, gy + pad, ink_r, ink_g, ink_b, wrap);
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
                            shown == CitySession::MapLayer::Industrial || shown == CitySession::MapLayer::Transportation;
        if (!on_off) {
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
        // Spooky's banner is VGA brown. A dark chip disappears into it.
        if (theme::chrome_look() == 2) {
            cairo_set_source_rgb(cr, 1.0, 0.86, 0.35);
            cairo_set_line_width(cr, 1.0);
            double chip_w = lw - pad * 2;
            if (shown == CitySession::MapLayer::Residential) {
                chip_w = static_cast<double>(dp(14) + dp(28));
            } else if (shown == CitySession::MapLayer::Commercial || shown == CitySession::MapLayer::Industrial) {
                chip_w = static_cast<double>(dp(legend_chip_px(shown)));
            } else if (shown == CitySession::MapLayer::Transportation) {
                chip_w = static_cast<double>(dp(34) + dp(28));
            }
            cairo_rectangle(cr, lx + pad + 0.5, ramp_y + 0.5, chip_w - 1.0, dp(6) - 1.0);
            cairo_stroke(cr);
        }
    }
    if (gamepad_nav_ && dialog_ == Dialog::None) {
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
        cairo_set_source_rgb(cr, theme::kFocusR, theme::kFocusG, theme::kFocusB);
        cairo_set_line_width(cr, tile <= kMapTileOverview ? 1.0 : 2.0);
        cairo_move_to(cr, hx - arm, hy);
        cairo_line_to(cr, hx + arm, hy);
        cairo_move_to(cr, hx, hy - arm);
        cairo_line_to(cr, hx, hy + arm);
        cairo_stroke(cr);
        cairo_arc(cr, hx, hy, radius, 0, 6.3);
        cairo_stroke(cr);
    }
    // Drop-down last, above the map, palette, dialog, and gamepad cursor.
    draw_menu_dropdown(cr);
    draw_special_menu(cr);
}

std::string Shell::dialog_title_hint() const
{
    const bool pad = gamepad_nav_ || deck_ || touch_used_;
    const std::string a = glyph(SDL_GAMEPAD_BUTTON_SOUTH);
    const std::string b = glyph(SDL_GAMEPAD_BUTTON_EAST);
    const std::string menu = glyph(SDL_GAMEPAD_BUTTON_START);
    const bool osk_key = focus_id_ >= kOsk && focus_id_ < kOsk + 64;
    const bool field = focus_id_ == kFieldName || focus_id_ == kFieldSeed;
    auto b_action = [&](const std::string &typed) {
        if (field_backspace_would_delete(typed, field_caret_, field_anchor_)) {
            return b + " deletes";
        }
        if (typed.empty()) {
            return b + " closes";
        }
        return std::string();
    };
    auto with_b = [&](const std::string &lead, const std::string &typed, const std::string &tail) {
        std::string line = lead;
        const std::string act = b_action(typed);
        if (!act.empty()) {
            line += "    " + act;
        }
        if (!tail.empty()) {
            line += "    " + tail;
        }
        return line;
    };
    if (osk_key) {
        int key_count = 0;
        const OskKey *keys = osk_keys(key_count);
        const int index = focus_id_ - kOsk;
        const std::string &typed = (dialog_ == Dialog::NewCity && new_page_ > 0 && focus_id_ == kFieldSeed) ? field_b_
                                                                                                            : field_a_;
        if (index >= 0 && index < key_count && keys[index].kind == OskKind::Auto) {
            return pad ? with_b(a + " picks a name", typed, menu + " confirms") : "Enter picks a name    Esc closes";
        }
        return pad ? with_b(a + " types", typed, menu + " confirms") : "Enter types    Esc closes";
    }
    if (field) {
        if (dialog_ == Dialog::NewCity && new_page_ > 0 && focus_id_ == kFieldSeed && field_b_ == "auto") {
            return pad ? a + " types    " + b + " goes back    " + menu + " confirms"
                       : "Type here    Enter confirms    Esc goes back";
        }
        const std::string &typed = focus_id_ == kFieldSeed ? field_b_ : field_a_;
        return pad ? with_b(a + " types", typed, menu + " confirms") : "Type here    Enter confirms    Esc closes";
    }
    if (dialog_ == Dialog::Messages && session_.notices().empty()) {
        return pad ? b + " closes" : "Esc closes";
    }
    if (dialog_ == Dialog::Messages) {
        return pad ? a + " jumps    " + b + " closes    D-pad moves" : "Enter jumps    Esc closes";
    }
    if (dialog_ == Dialog::Scenario || dialog_ == Dialog::Files) {
        return pad ? a + " opens    " + b + " closes    D-pad moves" : "Enter opens    Esc closes";
    }
    // Graphs, Evaluation, Query, About, and the one-button notices have
    // nothing to confirm. Enter dismisses them.
    const bool closes = dialog_ == Dialog::Graphs || dialog_ == Dialog::Evaluation || dialog_ == Dialog::Query ||
                        dialog_ == Dialog::About || dialog_ == Dialog::Info || dialog_ == Dialog::LoadError ||
                        dialog_ == Dialog::SaveError || dialog_ == Dialog::Controls || dialog_ == Dialog::HowToPlay;
    if (closes) {
        return pad ? a + " closes" : "Enter closes";
    }
    if (focus_id_ == kCancel) {
        return pad ? a + " cancels" : "Enter cancels";
    }
    if (dialog_ == Dialog::NewCity && new_page_ > 0) {
        return pad ? a + " confirms    " + b + " goes back" : "Enter confirms    Esc goes back";
    }
    return pad ? a + " confirms    " + b + " closes" : "Enter confirms    Esc closes";
}

std::string Shell::context_hint() const
{
    const bool pad = gamepad_nav_ || deck_;
    const bool finger = touch_used_ && !pad;
    const std::string a = glyph(SDL_GAMEPAD_BUTTON_SOUTH);
    const std::string b = glyph(SDL_GAMEPAD_BUTTON_EAST);
    const std::string left = glyph(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
    const std::string right = glyph(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
    const std::string menu = glyph(SDL_GAMEPAD_BUTTON_START);
    if (menu_.open()) {
        return pad ? a + " chooses    " + b + " closes" : "Enter chooses    Esc closes";
    }
    if (dialog_ != Dialog::None) {
        // Pad players read this in the title bar. Mouse players read it here.
        if (pad || finger) {
            return {};
        }
        return dialog_title_hint();
    }
    if (special_open_) {
        return pad ? a + " selects    " + b + " closes" : "Enter selects    Esc closes";
    }
    const ToolDef *tool = tool_by_index(tool_index_);
    const int engine = tool != nullptr ? tool->engine_id : TOOL_QUERY;
    if (overlay_on_) {
        return pad ? a + " builds    " + b + " closes the map    D-pad scrolls"
                   : std::string(tool_click_phrase(engine)) + "    Esc closes the map";
    }
    if (pad) {
        return tool_press_phrase(engine, a.c_str()) + "    " + b + " back    " + left + "/" + right + " tool    " +
               menu + " menu";
    }
    if (finger) {
        std::string phrase = tool_click_phrase(engine);
        const auto at = phrase.find("click to ");
        if (at != std::string::npos) {
            phrase.replace(at, 9, "tap to ");
        }
        return phrase;
    }
    return std::string(tool_click_phrase(engine)) + "    Arrows pan    Space pauses";
}

std::string Shell::idle_message() const
{
    // The tool phrase lives in the status hint when that hint fits. Repeating
    // it here made the same words appear twice on a wide window.
    return session_.date_text();
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
    layout();
    clamp_scroll();
    if (!redraw::needed(redraw_reasons())) {
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
    if (texture_ != nullptr) {
        SDL_UpdateTexture(texture_, nullptr, cairo_image_surface_get_data(frame_surface_),
                          cairo_image_surface_get_stride(frame_surface_));
        SDL_RenderClear(renderer_);
        SDL_RenderTexture(renderer_, texture_, nullptr, nullptr);
        SDL_RenderPresent(renderer_);
    }
    cairo_destroy(context);
    visual_stamp_ = visual_stamp();
    presented_ = true;
    window_exposed_ = false;
    ++redraws_;
}
