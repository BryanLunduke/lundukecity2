// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#include "shell_state.hpp"

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
#include "tools.hpp"
#include "version.hpp"

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
    if (win_w_ < 1) {
        win_w_ = 1;
    }
    if (win_h_ < 1) {
        win_h_ = 1;
    }
    menu_h_ = dp(36);
    welcome_h_ = welcome_ ? dp(52) : 0;
    status_h_ = dp(36);
    message_h_ = dp(44);
    const int body = win_h_ - menu_h_ - welcome_h_ - status_h_ - message_h_;
    cell_ = dp(48);
    while (cell_ > dp(36) && ToolPalette::height_for(cell_) + dp(210) > body) {
        cell_ -= dp(4);
    }
    palette_.set_cell(cell_);
    side_w_ = ToolPalette::width_for(cell_);
    side_x_ = dp(6);
    pal_y_ = menu_h_ + welcome_h_ + dp(6);
    mini_y_ = pal_y_ + ToolPalette::height_for(cell_) + dp(8);
    mini_h_ = std::max(dp(64), side_w_ * 5 / 6);
    demand_y_ = mini_y_ + mini_h_ + dp(8);
    const int demand_room = menu_h_ + welcome_h_ + body - demand_y_ - dp(6);
    demand_h_ = std::max(dp(72), std::min(dp(kMaxDemandPx), demand_room));
    view_x_ = side_x_ + side_w_ + dp(8);
    view_y_ = menu_h_ + welcome_h_;
    view_w_ = std::max(dp(32), win_w_ - view_x_ - dp(8));
    view_h_ = std::max(dp(32), win_h_ - view_y_ - status_h_ - message_h_);
    cursor_x_ = std::max(0.0f, std::min(cursor_x_, static_cast<float>(win_w_ - 1)));
    cursor_y_ = std::max(0.0f, std::min(cursor_y_, static_cast<float>(win_h_ - 1)));
    fit_map_to_view();
}

void Shell::button(cairo_t *cr, double x, double y, double w, double h, const char *label, int id, bool primary)
{
    push_hot(x, y, w, h, id);
    push_focus(id);
    // A modal covers the city. The welcome Dismiss (and anything else that
    // is not part of the dialog) must not light up with the dialog's focus.
    const bool suppressed = dialog_ != Dialog::None && !is_modal_control(id);
    const bool hot = !suppressed && (hover_id_ == id || focus_id_ == id);
    cairo_pattern_t *pattern = cairo_pattern_create_linear(x, y, x, y + h);
    if (primary || hot) {
        cairo_pattern_add_color_stop_rgb(pattern, 0, theme::kSelectHiR, theme::kSelectHiG, theme::kSelectHiB);
        cairo_pattern_add_color_stop_rgb(pattern, 1, theme::kSelectR, theme::kSelectG, theme::kSelectB);
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
    const bool light = primary || hot;
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
    fill(cr, x, y, w, kTitleBand, theme::kSelectR, theme::kSelectG, theme::kSelectB);
    const TextMetrics title_metrics = measure_text(cr, title_font(), title, static_cast<int>(w - 24));
    const double title_y = y + std::max(2.0, (kTitleBand - title_metrics.height) / 2.0);
    paint_text(cr, title_font(), title, x + 12, title_y, 1, 1, 1, static_cast<int>(w - 24));
    stroke_rect(cr, x, y, w, h, theme::kMenuFrameR, theme::kMenuFrameG, theme::kMenuFrameB);
}

void Shell::draw_osk(cairo_t *cr, double x, double y, double w, double max_h)
{
    if (!show_osk() || active_field() == nullptr || w < 80 || max_h < 40) {
        return;
    }
    int count = 0;
    const OskKey *keys = osk_keys(count);
    OskPlacement placed[64];
    const int n = osk_place(x, y, w, max_h, placed, 64);
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
        // A focused Close button used to share the System title's id.
        const bool lit = dialog_ == Dialog::None && (open || hover_id_ == id);
        if (lit) {
            fill(cr, x, 3, w, menu_h_ - 6, theme::kSelectR, theme::kSelectG, theme::kSelectB);
            stroke_rect(cr, x, 3, w, menu_h_ - 6, theme::kSelectEdgeR, theme::kSelectEdgeG, theme::kSelectEdgeB);
        }
        paint_mnemonic(cr, menu_font(), title, x + 10, 9, menu_.alt_shown || menu_.open(),
                       lit ? 1 : theme::kTextR, lit ? 1 : theme::kTextG, lit ? 1 : theme::kTextB);
        x += w + 2;
    }
}

void Shell::draw_menu_dropdown(cairo_t *cr)
{
    if (!menu_.open() || dialog_ != Dialog::None) {
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
    const double row_h = dp(32);
    const double drop_w = dp(320);
    double drop_h = 8;
    for (int i = 0; i < count; ++i) {
        drop_h += items[i].kind == ItemKind::Separator ? 8 : row_h;
    }
    if (drop_x + drop_w > win_w_ - 4) {
        drop_x = std::max(4.0, win_w_ - drop_w - 4);
    }
    fill(cr, drop_x, menu_h_, drop_w, drop_h, theme::kMenuBgR, theme::kMenuBgG, theme::kMenuBgB);
    stroke_rect(cr, drop_x, menu_h_, drop_w, drop_h, theme::kMenuFrameR, theme::kMenuFrameG, theme::kMenuFrameB);
    double y = menu_h_ + 4;
    double recent_row_y = y;
    bool have_recent_row = false;
    for (int i = 0; i < count; ++i) {
        const MenuEntry &item = items[i];
        if (item.kind == ItemKind::Separator) {
            cairo_set_source_rgb(cr, theme::kMenuFrameR, theme::kMenuFrameG, theme::kMenuFrameB);
            cairo_move_to(cr, drop_x + 4, y + 4);
            cairo_line_to(cr, drop_x + drop_w - 4, y + 4);
            cairo_stroke(cr);
            y += 8;
            continue;
        }
        const int id = kMenuItem + column * 40 + i;
        push_hot(drop_x + 2, y, drop_w - 4, row_h, id);
        const bool selected = menu_.cursor == i || hover_id_ == id;
        if (selected) {
            fill(cr, drop_x + 3, y, drop_w - 6, row_h, theme::kHoverR, theme::kHoverG, theme::kHoverB);
            if (gamepad_nav_ && menu_.cursor == i) {
                cairo_set_source_rgb(cr, theme::kFocusR, theme::kFocusG, theme::kFocusB);
                cairo_set_line_width(cr, 2);
                cairo_rectangle(cr, drop_x + 4, y + 2, drop_w - 8, row_h - 4);
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
            } else if (item.action == Action::SetPause) {
                on = speed_ == 0;
            } else if (item.action == Action::Slow) {
                on = speed_ == 1;
            } else if (item.action == Action::Medium) {
                on = speed_ == 2;
            } else if (item.action == Action::Fast) {
                on = speed_ == 3;
            }
            cairo_set_source_rgb(cr, theme::kTextR, theme::kTextG, theme::kTextB);
            if (item.kind == ItemKind::Check) {
                cairo_rectangle(cr, drop_x + 8, y + 8, 14, 14);
                cairo_stroke(cr);
                if (on) {
                    cairo_move_to(cr, drop_x + 10, y + 15);
                    cairo_line_to(cr, drop_x + 14, y + 19);
                    cairo_line_to(cr, drop_x + 22, y + 9);
                    cairo_stroke(cr);
                }
            } else {
                // Pango leaves the current point at the end of the previous
                // row. cairo_arc joins to that point, which draws a diagonal
                // from the row above into this radio circle.
                cairo_new_sub_path(cr);
                cairo_arc(cr, drop_x + 15, y + 16, 6, 0, 6.3);
                cairo_stroke(cr);
                if (on) {
                    cairo_new_sub_path(cr);
                    cairo_arc(cr, drop_x + 15, y + 16, 3, 0, 6.3);
                    cairo_fill(cr);
                }
            }
        }
        paint_mnemonic(cr, menu_font(), item.label, drop_x + 28, y + 7, menu_.alt_shown, theme::kTextR,
                       theme::kTextG, theme::kTextB);
        if (item.action == Action::Recent) {
            recent_row_y = y;
            have_recent_row = true;
        }
        if (item.accel_text != nullptr && !gamepad_nav_) {
            const TextMetrics accel = measure_text(cr, menu_font(), item.accel_text, -1);
            paint_text(cr, menu_font(), item.accel_text, drop_x + drop_w - accel.width - 12, y + 7, theme::kAccelR,
                       theme::kAccelG, theme::kAccelB, -1);
        }
        y += row_h;
    }
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
            fill(cr, sub.x + 3, ry, std::max(1.0, sub.w - 6), row_h, theme::kHoverR, theme::kHoverG, theme::kHoverB);
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
    const double large_w = std::min(static_cast<double>(dp(860)), std::max(1.0, win_w_ - 2.0 * margin));
    const double large_h = std::min(static_cast<double>(dp(640)), std::max(1.0, win_h_ - 2.0 * margin));
    auto buttons_at = [&](double px, double pw, double by, const std::vector<std::pair<const char *, int>> &buttons,
                          int left_count = 0) {
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
                   button_def.second == focus_id_);
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
                   button_def.second == focus_id_);
            right += widths[static_cast<std::size_t>(i)] + gap;
        }
    };
    auto text_block_height = [&](const std::string &text, double width) {
        return static_cast<double>(std::max(dp(18), measure_text(cr, body_font(), text, static_cast<int>(width)).height));
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
            cairo_set_source_rgba(cr, theme::kSelectR, theme::kSelectG, theme::kSelectB, 0.28);
            cairo_rectangle(cr, tx + x0, ty - dp(2), std::max(1, x1 - x0), th);
            cairo_fill(cr);
        }
        const int cx = measure_text(cr, body_font(), text.substr(0, static_cast<std::size_t>(field_caret_)), -1).width;
        cairo_set_source_rgb(cr, theme::kTextR, theme::kTextG, theme::kTextB);
        cairo_set_line_width(cr, 1);
        cairo_move_to(cr, tx + cx + 0.5, ty - dp(2));
        cairo_line_to(cr, tx + cx + 0.5, ty + th);
        cairo_stroke(cr);
    };
    if (dialog_ == Dialog::Unsaved) {
        const char *question = "Save changes to this city before continuing?";
        switch (after_save_) {
        case AfterSave::Quit:
            question = "Save changes to this city before quitting?";
            break;
        case AfterSave::NewCity:
            question = "Save changes to this city before starting a new city?";
            break;
        case AfterSave::Scenario:
            question = "Save changes to this city before playing a scenario?";
            break;
        case AfterSave::Load:
        case AfterSave::OpenPath:
        case AfterSave::FilesLoad:
            question = "Save changes to this city before opening another city?";
            break;
        case AfterSave::Recover:
            question = "Save changes to this city before recovering another city?";
            break;
        case AfterSave::Nothing:
            break;
        }
        const double width = message_width(win_w_, margin, dp(420), dp(520));
        const double body_h = text_block_height(question, width - 2 * margin);
        const double height = dp(40) + margin + body_h + dp(12) + dp(36) + margin;
        const DialogRect panel = place_dialog(win_w_, win_h_, width, height, margin);
        frame_panel(cr, panel.x, panel.y, panel.w, panel.h, "Save changes?");
        label(cr, body_font(), question, panel.x + margin, panel.y + dp(40) + dp(8), static_cast<int>(panel.w - 2 * margin));
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
        const DialogRect panel = place_dialog(win_w_, win_h_, width, height, margin);
        frame_panel(cr, panel.x, panel.y, panel.w, panel.h, title);
        label(cr, body_font(), std::string(question) + "\n" + detail, panel.x + margin, panel.y + dp(48),
              static_cast<int>(panel.w - 2 * margin));
        buttons_at(panel.x, panel.w, panel.y + panel.h - margin - dp(36), {{"_Start", kOk}, {"_Cancel", kCancel}}, 1);
        return;
    }
    if (dialog_ == Dialog::NewCity) {
        const bool osk = show_osk() && new_page_ == 0;
        double want_w = new_page_ == 1 ? dp(600) : dp(520);
        double want_h = new_page_ == 1 ? dp(400) : new_page_ == 0 ? dp(190) : dp(170);
        if (osk) {
            // Ten keys across need about 48px each, so the page grows past the
            // name-only size while the on-screen keyboard is up.
            want_w = std::max(want_w, static_cast<double>(dp(600)));
            want_h += dp(280);
        }
        if (!dialog_error_.empty()) {
            want_h += dp(22);
        }
        const DialogRect panel = place_dialog(win_w_, win_h_, want_w, want_h, margin);
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
            paint_caret(x + margin + dp(8), field_y + dp(8), dp(18), field_a_, focus_id_ == kFieldName);
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
            label(cr, ui_font(), "Difficulty, land, and seed. Default land is the usual generator.", x + margin, y + dp(46),
                  static_cast<int>(dw - 2 * margin));
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
                const double gy = y + dp(72) + g * dp(40);
                paint_text(cr, ui_font(), groups[g], x + margin, gy + dp(8), theme::kTextR, theme::kTextG, theme::kTextB,
                           static_cast<int>(label_w));
                for (int o = 0; o < 3; ++o) {
                    button(cr, x + margin + label_w + o * (choice_w + dp(6)), gy, choice_w, dp(32), options[g][o],
                           kChoice + g * 10 + o, selected[g] == o);
                }
            }
            const double seed_y = y + dp(72) + 5 * dp(40);
            paint_text(cr, ui_font(), "Seed", x + margin, seed_y + dp(8), theme::kTextR, theme::kTextG, theme::kTextB,
                       static_cast<int>(label_w));
            const double seed_x = x + margin + label_w;
            const double seed_w = std::min(static_cast<double>(dp(220)), dw - margin - seed_x + x);
            fill(cr, seed_x, seed_y, seed_w, dp(32), 1, 1, 1);
            stroke_rect(cr, seed_x, seed_y, seed_w, dp(32), theme::kMenuFrameR, theme::kMenuFrameG, theme::kMenuFrameB);
            push_hot(seed_x, seed_y, seed_w, dp(32), kFieldSeed);
            push_focus(kFieldSeed);
            paint_text(cr, body_font(), field_b_, seed_x + dp(8), seed_y + dp(6), theme::kTextR, theme::kTextG, theme::kTextB,
                       static_cast<int>(seed_w - dp(16)));
            paint_caret(seed_x + dp(8), seed_y + dp(6), dp(18), field_b_, focus_id_ == kFieldSeed);
            if (!dialog_error_.empty()) {
                paint_text(cr, ui_font(), dialog_error_, x + margin, seed_y + dp(36), 0.6, 0.1, 0.1,
                           static_cast<int>(dw - 2 * margin));
            }
            buttons_at(x, dw, y + dh - margin - dp(36), {{"_Back", kBack}, {"_Next", kNext}});
        } else {
            const std::string summary = "Generate \"" + field_a_ + "\".\nThis replaces the city on the map.";
            label(cr, body_font(), summary, x + margin, y + dp(52), static_cast<int>(dw - 2 * margin));
            buttons_at(x, dw, y + dh - margin - dp(36), {{"_Back", kBack}, {"_Generate", kGenerate}});
        }
        return;
    }
    if (dialog_ == Dialog::Scenario) {
        const DialogRect panel = place_dialog(win_w_, win_h_, large_w, large_h, margin);
        const double x = panel.x;
        const double y = panel.y;
        const double dw = panel.w;
        const double dh = panel.h;
        frame_panel(cr, x, y, dw, dh, "Play Scenario");
        label(cr, ui_font(), "Choose a scenario. Playing it replaces the city on the map.", x + 16, y + 42,
              static_cast<int>(dw - 32));
        const double row_h = 56;
        const int count = CitySession::kScenarioCount;
        const int visible = std::max(1, std::min(count, static_cast<int>((dh - 140) / row_h)));
        files_visible_ = visible;
        if (scenario_index_ < 0) {
            scenario_index_ = 0;
        }
        if (scenario_index_ >= count) {
            scenario_index_ = count - 1;
        }
        list_index_ = scenario_index_;
        list_reveal(list_scroll_, scenario_index_, visible, count);
        const double list_top = y + 78;
        const double row_w = dw - 24 - 16;
        for (int row = 0; row < visible; ++row) {
            const int i = list_scroll_ + row;
            if (i < 0 || i >= count) {
                break;
            }
            const CitySession::ScenarioDef &def = CitySession::scenario_def(i);
            const double ry = list_top + row * row_h;
            const int id = kListItem + row;
            push_hot(x + 12, ry, row_w, row_h - 4, id);
            push_focus(id);
            if (scenario_index_ == i || focus_id_ == id) {
                fill(cr, x + 12, ry, row_w, row_h - 4, theme::kHoverR, theme::kHoverG, theme::kHoverB);
            }
            const std::string line = std::string(def.name) + " (" + std::to_string(def.year) + ")";
            paint_text(cr, body_font(), line, x + 20, ry + 4, theme::kTextR, theme::kTextG, theme::kTextB,
                       static_cast<int>(row_w - 16));
            paint_text(cr, ui_font(), def.goal, x + 20, ry + 26, theme::kAccelR, theme::kAccelG, theme::kAccelB,
                       static_cast<int>(row_w - 16));
        }
        const double track_x = x + dw - 26;
        const double track_h = visible * row_h;
        list_track_y_ = list_top;
        list_track_h_ = std::max(1.0, track_h);
        fill(cr, track_x, list_top, 12, track_h, 0.86, 0.86, 0.86);
        double thumb_y = list_top;
        double thumb_h = track_h;
        list_thumb(list_scroll_, count, visible, list_top, track_h, thumb_y, thumb_h);
        fill(cr, track_x, thumb_y, 12, thumb_h, theme::kSelectR, theme::kSelectG, theme::kSelectB);
        stroke_rect(cr, track_x, list_top, 12, track_h, theme::kButtonEdgeR, theme::kButtonEdgeG, theme::kButtonEdgeB);
        push_hot(track_x - 4, list_top, 20, track_h, kListScroll);
        buttons_at(x, dw, y + dh - margin - dp(36), {{"_Cancel", kCancel}, {"_Play", kPlay}});
        return;
    }
    if (dialog_ == Dialog::Rename) {
        const bool osk = show_osk();
        double want_w = osk ? dp(600) : dp(520);
        double want_h = dp(170);
        if (osk) {
            want_h += dp(280);
        }
        if (!dialog_error_.empty()) {
            want_h += dp(22);
        }
        const DialogRect panel = place_dialog(win_w_, win_h_, want_w, want_h, margin);
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
        paint_caret(x + margin + dp(8), field_y + dp(8), dp(18), field_a_, focus_id_ == kFieldName);
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
        const double width = message_width(win_w_, margin, dp(420), dp(560));
        const double height = dp(230);
        const DialogRect panel = place_dialog(win_w_, win_h_, width, height, margin);
        frame_panel(cr, panel.x, panel.y, panel.w, panel.h, "About Lunduke City 2");
        const double icon = dp(72);
        const double icon_x = panel.x + margin;
        const double icon_y = panel.y + dp(56);
        fill(cr, icon_x, icon_y + dp(36), icon, dp(28), theme::kSelectR, theme::kSelectG, theme::kSelectB);
        fill(cr, icon_x + dp(8), icon_y + dp(16), dp(18), dp(48), 0.93, 0.93, 0.90);
        fill(cr, icon_x + dp(30), icon_y + dp(4), dp(14), dp(60), 0.55, 0.62, 0.70);
        fill(cr, icon_x + dp(48), icon_y + dp(22), dp(16), dp(42), 0.75, 0.55, 0.28);
        const std::string body = std::string(kProductName) + " " + kPackageVersion +
                                 "\nA city-building game for LCOS, Steam Deck, and Windows. "
                                 "Hard fork of Lunduke City.\n"
                                 "Independent work. Not affiliated with Electronic Arts.\n"
                                 "GPL-3.0-or-later. See NOTICE and COPYING.";
        label(cr, body_font(), body, icon_x + icon + dp(12), panel.y + dp(52),
              static_cast<int>(panel.w - 2 * margin - icon - dp(12)));
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
        const DialogRect panel = place_dialog(win_w_, win_h_, width, height, margin);
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
            const DialogRect panel = place_dialog(win_w_, win_h_, width, height, margin);
            frame_panel(cr, panel.x, panel.y, panel.w, panel.h, "System file dialog");
            label(cr, body_font(), wait, panel.x + margin, panel.y + dp(48), static_cast<int>(panel.w - 2 * margin));
            buttons_at(panel.x, panel.w, panel.y + panel.h - margin - dp(36),
                       {{"_Cancel", kCancel}, {"Browse in the window", kSystemFiles}});
            return;
        }
        const DialogRect panel = place_dialog(win_w_, win_h_, large_w, large_h, margin);
        const double x = panel.x;
        const double y = panel.y;
        const double dw = panel.w;
        const double dh = panel.h;
        frame_panel(cr, x, y, dw, dh, title);
        label(cr, ui_font(), files_dir_, x + 16, y + 48, static_cast<int>(dw - 32));
        const double list_top = y + 68;
        const double list_h = dh - 160;
        const int visible = std::max(1, static_cast<int>(list_h / dp(32)));
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
            list_reveal(list_scroll_, list_index_, visible, count);
        }
        const double row_w = dw - 24 - 16;
        for (int row = 0; row < visible; ++row) {
            const int i = list_scroll_ + row;
            if (i < 0 || i >= count) {
                break;
            }
            const double ry = list_top + row * dp(32);
            const int id = kListItem + row;
            push_hot(x + 12, ry, row_w, 30, id);
            push_focus(id);
            if (list_index_ == i || focus_id_ == id) {
                fill(cr, x + 12, ry, row_w, 30, theme::kHoverR, theme::kHoverG, theme::kHoverB);
            }
            paint_text(cr, body_font(), files_[static_cast<std::size_t>(i)], x + 20, ry + 5, theme::kTextR,
                       theme::kTextG, theme::kTextB, static_cast<int>(row_w - 16));
        }
        const double track_x = x + dw - 26;
        const double track_y = list_top;
        const double track_h = visible * static_cast<double>(dp(32));
        list_track_y_ = track_y;
        list_track_h_ = std::max(1.0, track_h);
        fill(cr, track_x, track_y, 12, track_h, 0.86, 0.86, 0.86);
        double thumb_y = track_y;
        double thumb_h = track_h;
        list_thumb(list_scroll_, count, visible, track_y, track_h, thumb_y, thumb_h);
        fill(cr, track_x, thumb_y, 12, thumb_h, theme::kSelectR, theme::kSelectG, theme::kSelectB);
        stroke_rect(cr, track_x, track_y, 12, track_h, theme::kButtonEdgeR, theme::kButtonEdgeG, theme::kButtonEdgeB);
        push_hot(track_x - 4, track_y, 20, track_h, kListScroll);
        if (files_.empty()) {
            paint_text(cr, body_font(), "No .cty files in this folder.", x + 20, list_top + 6, theme::kAccelR,
                       theme::kAccelG, theme::kAccelB, static_cast<int>(dw - 56));
        }
        if (files_save_) {
            fill(cr, x + 16, y + dh - 100, dw - 32, 34, 1, 1, 1);
            stroke_rect(cr, x + 16, y + dh - 100, dw - 32, 34, theme::kMenuFrameR, theme::kMenuFrameG, theme::kMenuFrameB);
            push_hot(x + 16, y + dh - 100, dw - 32, 34, kFieldName);
            push_focus(kFieldName);
            paint_text(cr, body_font(), field_a_, x + 24, y + dh - 92, theme::kTextR, theme::kTextG, theme::kTextB,
                       static_cast<int>(dw - 48));
            paint_caret(x + 24, y + dh - 92, dp(18), field_a_, focus_id_ == kFieldName);
        }
        bool folder_row = false;
        if (!files_save_ && list_index_ >= 0 && list_index_ < static_cast<int>(files_.size())) {
            folder_row = file_row_is_dir(files_[static_cast<std::size_t>(list_index_)]);
        }
        const char *open_label = files_save_ ? "_Save" : (folder_row ? "Open _Folder" : "_Open");
        const int open_id = files_save_ ? kSave : kOk;
        buttons_at(x, dw, y + dh - margin - dp(36), {{"_Up", kUp}, {"_Cancel", kCancel}, {open_label, open_id}}, 1);
        return;
    }
    if (dialog_ == Dialog::LoadError || dialog_ == Dialog::SaveError || dialog_ == Dialog::Info) {
        const double width = message_width(win_w_, margin, dp(420), dp(560));
        const double body_h = text_block_height(info_body_, width - 2 * margin);
        const double height = dp(40) + margin + body_h + dp(12) + dp(36) + margin;
        const DialogRect panel = place_dialog(win_w_, win_h_, width, height, margin);
        frame_panel(cr, panel.x, panel.y, panel.w, panel.h, info_title_.c_str());
        label(cr, body_font(), info_body_, panel.x + margin, panel.y + dp(48), static_cast<int>(panel.w - 2 * margin));
        buttons_at(panel.x, panel.w, panel.y + panel.h - margin - dp(36), {{"_OK", kOk}});
        return;
    }
    if (dialog_ == Dialog::Overwrite) {
        const auto slash = queued_path_.find_last_of("/\\");
        const std::string name = slash == std::string::npos ? queued_path_ : queued_path_.substr(slash + 1);
        const std::string question = name.empty() ? "A city file with that name already exists. Replace it?"
                                                  : "Replace \"" + name + "\"? The city saved there will be overwritten.";
        const double width = message_width(win_w_, margin, dp(420), dp(520));
        const double body_h = text_block_height(question, width - 2 * margin);
        const double height = dp(40) + margin + body_h + dp(12) + dp(36) + margin;
        const DialogRect panel = place_dialog(win_w_, win_h_, width, height, margin);
        frame_panel(cr, panel.x, panel.y, panel.w, panel.h, "Replace file?");
        label(cr, body_font(), question, panel.x + margin, panel.y + dp(48), static_cast<int>(panel.w - 2 * margin));
        buttons_at(panel.x, panel.w, panel.y + panel.h - margin - dp(36), {{"_Cancel", kCancel}, {"_Replace", kSave}});
        return;
    }
    if (dialog_ == Dialog::Outcome) {
        const double width = message_width(win_w_, margin, dp(420), dp(560));
        const double body_h = text_block_height(info_body_, width - 2 * margin);
        const double height = dp(40) + margin + body_h + dp(12) + dp(36) + margin;
        const DialogRect panel = place_dialog(win_w_, win_h_, width, height, margin);
        frame_panel(cr, panel.x, panel.y, panel.w, panel.h, info_title_.c_str());
        label(cr, body_font(), info_body_, panel.x + margin, panel.y + dp(48), static_cast<int>(panel.w - 2 * margin));
        buttons_at(panel.x, panel.w, panel.y + panel.h - margin - dp(36),
                   {{"Stay _paused", kStay}, {"_Scenario list", kScenarioList}, {"_Keep playing", kKeep}});
        return;
    }
    if (dialog_ == Dialog::Settings) {
        const double pitch = dp(64);
        const double height = dp(40) + margin + pitch * 2 + dp(48) + dp(12) + dp(36) + margin;
        const DialogRect panel = place_dialog(win_w_, win_h_, dp(560), height, margin);
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
            fill(cr, sx, bar_y, sw * t, dp(16), theme::kSelectR, theme::kSelectG, theme::kSelectB);
            stroke_rect(cr, sx, bar_y, sw, dp(16), theme::kButtonEdgeR, theme::kButtonEdgeG, theme::kButtonEdgeB);
            const int id = kSlider + index;
            push_hot(sx, bar_y - dp(6), sw, dp(28), id);
            push_focus(id);
            next_sliders_.push_back(SliderGeom{id, sx, bar_y - dp(6), sw, static_cast<double>(dp(28)), min, max});
        };
        const std::string scale_text = scale_percent_ <= 0 ? "Auto" : std::to_string(scale_percent_) + "%";
        slider(1, "Volume", std::to_string(volume_) + "%", volume_, 0, 100, y + dp(48));
        slider(2, "UI scale", scale_text, scale_percent_, 0, 200, y + dp(48) + pitch);
        const char *pause_label = pause_unfocused_ ? "Pause when unfocused: On" : "Pause when unfocused: Off";
        buttons_at(x, dw, y + dp(48) + pitch * 2 + dp(8), {{pause_label, kReset}});
        buttons_at(x, dw, y + panel.h - margin - dp(36), {{"_Cancel", kCancel}, {"_OK", kOk}});
        return;
    }
    if (dialog_ == Dialog::Controls) {
        const std::string body =
            gamepad_nav_
                ? std::string(glyph(SDL_GAMEPAD_BUTTON_SOUTH)) + " activate    " +
                      glyph(SDL_GAMEPAD_BUTTON_EAST) + " back\nD-pad moves    LB/RB change tool\nStart opens the menu    " +
                      glyph(SDL_GAMEPAD_BUTTON_NORTH) + " pauses"
                : "Ctrl+N new city    Ctrl+O load    Ctrl+S save\n"
                  "Space pauses    Ctrl+1 slow    Ctrl+2 medium    Ctrl+3 fast\n"
                  "Arrow keys scroll the map    Ctrl+= zooms\n"
                  "A connected gamepad uses A, B, and the D-pad";
        const double width = message_width(win_w_, margin, dp(420), dp(640));
        const double body_h = text_block_height(body, width - 2 * margin);
        const double height = dp(40) + margin + body_h + dp(12) + dp(36) + margin;
        const DialogRect panel = place_dialog(win_w_, win_h_, width, height, margin);
        frame_panel(cr, panel.x, panel.y, panel.w, panel.h, "Controls");
        label(cr, body_font(), body, panel.x + margin, panel.y + dp(48), static_cast<int>(panel.w - 2 * margin));
        buttons_at(panel.x, panel.w, panel.y + panel.h - margin - dp(36), {{"_Close", kCancel}});
        return;
    }
    if (dialog_ == Dialog::Budget) {
        const auto book = session_.budget();
        const BudgetCopy copy = budget_copy(book, budget_is_tax_);
        const double pitch = dp(64);
        const double note_h = copy.closing_note.empty() ? 0.0 : dp(36);
        const double height = dp(40) + margin + dp(44) + note_h + dp(8) + pitch * 4 + dp(12) + dp(36) + margin;
        const DialogRect panel = place_dialog(win_w_, win_h_, dp(640), height, margin);
        const double x = panel.x;
        const double y = panel.y;
        const double dw = panel.w;
        const double dh = panel.h;
        frame_panel(cr, x, y, dw, dh, "Budget");
        const double col_w = (dw - 2 * margin - dp(16)) / 2.0;
        paint_text(cr, ui_font(), copy.taxes, x + margin, y + dp(48), theme::kTextR, theme::kTextG, theme::kTextB,
                   static_cast<int>(col_w));
        paint_text(cr, ui_font(), copy.previous_funds, x + margin + col_w + dp(16), y + dp(48), theme::kTextR, theme::kTextG,
                   theme::kTextB, static_cast<int>(col_w));
        paint_text(cr, ui_font(), copy.cash_flow, x + margin, y + dp(68), theme::kTextR, theme::kTextG, theme::kTextB,
                   static_cast<int>(col_w));
        paint_text(cr, ui_font(), copy.current_funds, x + margin + col_w + dp(16), y + dp(68), theme::kTextR, theme::kTextG,
                   theme::kTextB, static_cast<int>(col_w));
        double note_bottom = y + dp(92);
        if (!copy.closing_note.empty()) {
            paint_text(cr, ui_font(), copy.closing_note, x + margin, note_bottom, theme::kAccelR, theme::kAccelG,
                       theme::kAccelB, static_cast<int>(dw - 2 * margin));
            note_bottom += note_h;
        }
        const double sliders_top = note_bottom + dp(8);
        auto slider = [&](int index, const std::string &caption, const std::string &value_text, const std::string &detail,
                          int value, int min, int max, double sy) {
            paint_text(cr, bold_font(), caption, x + margin, sy, theme::kTextR, theme::kTextG, theme::kTextB,
                       static_cast<int>(dw - 2 * margin - dp(80)));
            const TextMetrics value_metrics = measure_text(cr, bold_font(), value_text, -1);
            paint_text(cr, bold_font(), value_text, x + dw - margin - value_metrics.width, sy, theme::kTextR, theme::kTextG,
                       theme::kTextB, -1);
            if (!detail.empty()) {
                paint_text(cr, ui_font(), detail, x + margin, sy + dp(18), theme::kAccelR, theme::kAccelG, theme::kAccelB,
                           static_cast<int>(dw - 2 * margin));
            }
            const double bar_y = sy + dp(36);
            const double sx = x + margin;
            const double sw = dw - 2 * margin;
            fill(cr, sx, bar_y, sw, dp(16), 1, 1, 1);
            const double t = (value - min) / static_cast<double>(std::max(1, max - min));
            fill(cr, sx, bar_y, sw * t, dp(16), theme::kSelectR, theme::kSelectG, theme::kSelectB);
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
        };
        slider(1, copy.tax_title, copy.tax_value, "", tax_, 0, 20, sliders_top);
        slider(2, "Road fund", std::to_string(road_) + "%", copy.road, road_, 0, 100, sliders_top + pitch);
        slider(3, "Police fund", std::to_string(police_) + "%", copy.police, police_, 0, 100, sliders_top + pitch * 2);
        slider(4, "Fire fund", std::to_string(fire_) + "%", copy.fire, fire_, 0, 100, sliders_top + pitch * 3);
        if (budget_is_tax_) {
            buttons_at(x, dw, y + dh - margin - dp(36), {{"_Reset", kReset}, {"_Collect Taxes", kCollect}});
        } else {
            buttons_at(x, dw, y + dh - margin - dp(36), {{"_Cancel", kCancel}, {"_OK", kOk}});
        }
        return;
    }
    if (dialog_ == Dialog::Graphs) {
        const DialogRect panel = place_dialog(win_w_, win_h_, large_w, large_h, margin);
        const double x = panel.x;
        const double y = panel.y;
        const double dw = panel.w;
        const double dh = panel.h;
        frame_panel(cr, x, y, dw, dh, "Graphs");
        button(cr, x + margin, y + dp(46), dp(140), dp(32), "10 year", kShortScale, !graphs_long_);
        button(cr, x + margin + dp(148), y + dp(46), dp(140), dp(32), "120 year", kLongScale, graphs_long_);
        const std::vector<ChartLegend> legend = chart_legend(&session_, graphs_long_);
        const int legend_cols = 2;
        const int legend_rows = std::max(1, (static_cast<int>(legend.size()) + legend_cols - 1) / legend_cols);
        const double legend_row_h = dp(20);
        const double legend_h = legend_rows * legend_row_h;
        const double button_top = y + dh - margin - dp(36);
        const double legend_top = button_top - dp(12) - legend_h;
        const double chart_top = y + dp(86);
        const double chart_h = std::max(static_cast<double>(dp(64)), legend_top - dp(8) - chart_top);
        {
            TargetCr live(cr);
            cairo_translate(live.cr, x + margin, chart_top);
            draw_history_chart(live.cr, &session_, graphs_long_, static_cast<int>(dw - 2 * margin), static_cast<int>(chart_h));
        }
        const double col_w = (dw - 2 * margin) / legend_cols;
        for (int i = 0; i < static_cast<int>(legend.size()); ++i) {
            const ChartLegend &entry = legend[static_cast<std::size_t>(i)];
            const int col = i % legend_cols;
            const int row = i / legend_cols;
            const double lx = x + margin + col * col_w;
            const double ly = legend_top + row * legend_row_h;
            cairo_set_source_rgb(cr, entry.r, entry.g, entry.b);
            cairo_rectangle(cr, lx, ly + dp(4), dp(18), dp(8));
            cairo_fill(cr);
            paint_text(cr, ui_font(), entry.caption, lx + dp(24), ly, theme::kTextR, theme::kTextG, theme::kTextB,
                       static_cast<int>(col_w - dp(28)));
        }
        buttons_at(x, dw, button_top, {{"_Close", kClose}});
        return;
    }
    if (dialog_ == Dialog::Evaluation) {
        const EvaluationCopy copy = evaluation_copy(session_.evaluation(), session_.scenario_progress());
        const std::string heading = session_.city_name() + " — " + session_.date_text();
        const int left_lines = 7;
        int right_lines = 2 + static_cast<int>(copy.problems.size()) + 5;
        if (!copy.scenario.empty()) {
            ++right_lines;
        }
        const double line_h = dp(22);
        const double height = dp(40) + margin + line_h + dp(8) + std::max(left_lines, right_lines) * line_h + dp(12) +
                              dp(36) + margin;
        const DialogRect panel = place_dialog(win_w_, win_h_, dp(700), height, margin);
        const double x = panel.x;
        const double y = panel.y;
        const double dw = panel.w;
        frame_panel(cr, x, y, dw, panel.h, "Evaluation");
        paint_text(cr, body_font(), heading, x + margin, y + dp(48), theme::kTextR, theme::kTextG, theme::kTextB,
                   static_cast<int>(dw - 2 * margin));
        const double col_w = (dw - 3 * margin) / 2.0;
        const double left_x = x + margin;
        const double right_x = x + margin + col_w + margin;
        const double top = y + dp(48) + line_h + dp(8);
        auto line = [&](const char *font, const std::string &text, double lx, int row) {
            paint_text(cr, font, text, lx, top + row * line_h, theme::kTextR, theme::kTextG, theme::kTextB,
                       static_cast<int>(col_w));
        };
        int row = 0;
        line(bold_font(), copy.score_caption, left_x, row++);
        line(body_font(), copy.score, left_x, row++);
        line(body_font(), copy.score_delta, left_x, row++);
        line(bold_font(), copy.opinion_caption, left_x, row++);
        line(body_font(), copy.opinion_question, left_x, row++);
        line(body_font(), copy.yes + "    " + copy.no, left_x, row++);
        if (copy.yes_percent >= 0) {
            const double bar_y = top + row * line_h + dp(4);
            const double bw = std::min(col_w, static_cast<double>(dp(180)));
            fill(cr, left_x, bar_y, bw, dp(10), 0.9, 0.9, 0.9);
            fill(cr, left_x, bar_y, bw * (copy.yes_percent / 100.0), dp(10), theme::kSelectR, theme::kSelectG,
                 theme::kSelectB);
        }
        line(ui_font(), copy.opinion_bar, left_x, row);
        row = 0;
        line(bold_font(), copy.problems_caption, right_x, row++);
        for (const std::string &problem : copy.problems) {
            line(body_font(), problem, right_x, row++);
        }
        line(bold_font(), copy.stats_caption, right_x, row++);
        line(body_font(), copy.population, right_x, row++);
        line(body_font(), copy.migration, right_x, row++);
        line(body_font(), copy.assessed, right_x, row++);
        line(body_font(), copy.category, right_x, row++);
        line(body_font(), copy.difficulty, right_x, row++);
        if (!copy.scenario.empty()) {
            line(body_font(), copy.scenario, right_x, row++);
        }
        buttons_at(x, dw, y + panel.h - margin - dp(36), {{"_Close", kClose}});
        return;
    }
    if (dialog_ == Dialog::Overlay) {
        const DialogRect panel = place_dialog(win_w_, win_h_, large_w, large_h, margin);
        const double x = panel.x;
        const double y = panel.y;
        const double dw = panel.w;
        const double dh = panel.h;
        frame_panel(cr, x, y, dw, dh, overlay_.title());
        label(cr, ui_font(), overlay_.legend(), x + margin, y + dp(48), static_cast<int>(dw - 2 * margin));
        const int ow = static_cast<int>(dw - 2 * margin);
        const int oh = static_cast<int>(std::max(static_cast<double>(dp(80)), dh - dp(148)));
        overlay_x_ = x + margin;
        overlay_y_ = y + dp(72);
        overlay_w_ = ow;
        overlay_h_ = oh;
        {
            TargetCr live(cr);
            cairo_translate(live.cr, overlay_x_, overlay_y_);
            overlay_.sync();
            overlay_.draw(live.cr, ow, oh);
        }
        push_hot(overlay_x_, overlay_y_, ow, oh, kOverlayMap);
        buttons_at(x, dw, y + dh - margin - dp(36), {{"_Close", kClose}});
        return;
    }
    if (dialog_ == Dialog::Query) {
        const double width = message_width(win_w_, margin, dp(320), dp(480));
        const double body_h = text_block_height(info_body_.empty() ? " " : info_body_, width - 2 * margin);
        const double height = dp(40) + margin + body_h + dp(12) + dp(36) + margin;
        DialogRect panel = place_dialog(win_w_, win_h_, width, height, margin);
        if (query_anchored_) {
            panel = place_near(win_w_, win_h_, panel.w, panel.h, query_anchor_x_, query_anchor_y_, margin);
        }
        frame_panel(cr, panel.x, panel.y, panel.w, panel.h, "Query");
        label(cr, body_font(), info_body_, panel.x + margin, panel.y + dp(48), static_cast<int>(panel.w - 2 * margin));
        buttons_at(panel.x, panel.w, panel.y + panel.h - margin - dp(36), {{"_Close", kClose}});
    }
}

void Shell::draw(cairo_t *cr)
{
    menu_.recent_count = static_cast<int>(recent_.size());
    fill(cr, 0, 0, win_w_, win_h_, theme::kWindowR, theme::kWindowG, theme::kWindowB);
    draw_menubar(cr);
    if (welcome_) {
        fill(cr, 0, menu_h_, win_w_, welcome_h_, theme::kTooltipR, theme::kTooltipG, theme::kTooltipB);
        const std::string line = std::string("Welcome to Lunduke City 2. The clock is ") +
                                 (speed_ <= 0 ? "paused." : std::string("running at ") + speed_word(speed_) + ".") +
                                 " Choose a speed under Options, start a city with System, New City, or pick Play Scenario.";
        paint_text(cr, ui_font(), line, 10, menu_h_ + 6, theme::kTextR, theme::kTextG, theme::kTextB, win_w_ - 140);
        button(cr, win_w_ - dp(120), menu_h_ + dp(8), dp(100), dp(32), "Dismiss", kWelcomeId, false);
    }
    {
        TargetCr live(cr);
        cairo_translate(live.cr, side_x_, pal_y_);
        palette_.draw(live.cr);
    }
    push_hot(side_x_, pal_y_, side_w_, ToolPalette::height_for(cell_), kPaletteId);
    {
        TargetCr live(cr);
        cairo_translate(live.cr, side_x_, mini_y_);
        minimap_.sync();
        minimap_.draw(live.cr, side_w_, mini_h_);
    }
    push_hot(side_x_, mini_y_, side_w_, mini_h_, kMinimapId);
    {
        TargetCr live(cr);
        cairo_translate(live.cr, side_x_, demand_y_);
        demand_.draw(live.cr, side_w_, demand_h_);
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
        cairo_translate(live.cr, view_x_ + map_ox_ - scroll_x_, view_y_ + map_oy_ - scroll_y_);
        map_.sync();
        map_.draw(live.cr);
    }
    cairo_restore(cr);
    push_hot(view_x_, view_y_, view_w_, view_h_, kMapId);
    const double max_x = std::max(0, map_.pixel_width() - view_w_);
    const double max_y = std::max(0, map_.pixel_height() - view_h_);
    if (max_x > 0) {
        const double bw = std::max(20.0, view_w_ * (view_w_ / static_cast<double>(map_.pixel_width())));
        const double bx = view_x_ + (view_w_ - bw) * (scroll_x_ / max_x);
        fill(cr, bx, view_y_ + view_h_ - 8, bw, 6, theme::kSelectR, theme::kSelectG, theme::kSelectB);
    }
    if (max_y > 0) {
        const double bh = std::max(20.0, view_h_ * (view_h_ / static_cast<double>(map_.pixel_height())));
        const double by = view_y_ + (view_h_ - bh) * (scroll_y_ / max_y);
        fill(cr, view_x_ + view_w_ - 8, by, 6, bh, theme::kSelectR, theme::kSelectG, theme::kSelectB);
    }
    const int status_y = win_h_ - status_h_ - message_h_;
    fill(cr, 0, status_y, win_w_, status_h_, theme::kMenuBotR, theme::kMenuBotG, theme::kMenuBotB);
    std::string status = session_.funds_text() + "    " + session_.city_name() + "    " + speed_word(speed_) + "    " +
                         session_.date_text();
    const int years = session_.scenario_years_left();
    if (years == 0) {
        status += "    Time is up";
    } else if (years == 1) {
        status += "    1 year left";
    } else if (years > 1) {
        status += "    " + std::to_string(years) + " years left";
    }
    paint_text(cr, ui_font(), status, 8, status_y + 8, theme::kTextR, theme::kTextG, theme::kTextB, win_w_ / 2);
    if (gamepad_nav_) {
        const std::string hint = std::string(glyph(SDL_GAMEPAD_BUTTON_SOUTH)) + " build   " +
                                 glyph(SDL_GAMEPAD_BUTTON_EAST) + " cancel   LB/RB tools   Start menu";
        const TextMetrics metrics = measure_text(cr, ui_font(), hint, -1);
        paint_text(cr, ui_font(), hint, win_w_ - metrics.width - 8, status_y + 8, theme::kTextR, theme::kTextG,
                   theme::kTextB, -1);
    }
    const auto now = std::chrono::steady_clock::now();
    const bool expired = hint_until_.time_since_epoch().count() == 0 || now >= hint_until_;
    const NoticeDecision notice = decide_notice(session_.message_serial(), shown_message_serial_, expired, query_pinned_);
    std::string bar;
    if (notice.show_notice && !session_.message().empty()) {
        shown_message_serial_ = session_.message_serial();
        shown_message_ = session_.message();
        hint_until_ = now + std::chrono::seconds(4);
        query_pinned_ = false;
        bar = shown_message_;
    } else if (!expired && !shown_message_.empty()) {
        bar = shown_message_;
    } else {
        const char *tip = palette_.tooltip();
        if (tip == nullptr || tip[0] == '\0') {
            tip = tool_by_index(tool_index_)->hint;
        }
        bar = tip != nullptr ? tip : "";
    }
    fill(cr, 0, win_h_ - message_h_, win_w_, message_h_, theme::kTooltipR, theme::kTooltipG, theme::kTooltipB);
    paint_text(cr, body_font(), bar, 8, win_h_ - message_h_ + 10, theme::kTextR, theme::kTextG, theme::kTextB,
               win_w_ - 16);
    const std::string goal = session_.scenario_progress();
    if (!goal.empty() && dialog_ == Dialog::None) {
        paint_text(cr, ui_font(), goal, view_x_ + 8, view_y_ + 8, theme::kTextR, theme::kTextG, theme::kTextB,
                   view_w_ - 16);
    }
    if (dialog_ != Dialog::None) {
        draw_dialog(cr);
    }
    if (gamepad_nav_ && dialog_ == Dialog::None) {
        cairo_set_source_rgb(cr, theme::kFocusR, theme::kFocusG, theme::kFocusB);
        cairo_set_line_width(cr, 2);
        cairo_move_to(cr, cursor_x_ - 10, cursor_y_);
        cairo_line_to(cr, cursor_x_ + 10, cursor_y_);
        cairo_move_to(cr, cursor_x_, cursor_y_ - 10);
        cairo_line_to(cr, cursor_x_, cursor_y_ + 10);
        cairo_stroke(cr);
        cairo_arc(cr, cursor_x_, cursor_y_, 9, 0, 6.3);
        cairo_stroke(cr);
    }
    // Drop-down last, above the map, palette, dialog, and gamepad cursor.
    draw_menu_dropdown(cr);
}

void Shell::render()
{
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
    if (!shot_path_.empty()) {
        cairo_surface_write_to_png(frame_surface_, shot_path_.c_str());
    }
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
    ++redraws_;
}
