// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#include "shell_state.hpp"

#include "graph_legend.hpp"
#include "notice_bar.hpp"
#include "text_paint.hpp"
#include "theme.hpp"
#include "tools.hpp"
#include "version.hpp"

#include <cairomm/context.h>
#include <cairomm/surface.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace {

constexpr int kMapId = 1;
constexpr int kPaletteId = 2;
constexpr int kMinimapId = 3;
constexpr int kWelcomeId = 4;
constexpr int kMenuTitle = 10;
constexpr int kHScroll = 50;
constexpr int kVScroll = 51;
constexpr int kMenuItem = 1000;
constexpr int kRecentItem = 2000;
constexpr int kListItem = 7000;
constexpr int kFieldName = 6001;
constexpr int kFieldSeed = 6002;
constexpr int kOsk = 6100;
constexpr int kSlider = 8000;
constexpr int kChoice = 8300;
constexpr int kCancel = 1;
constexpr int kOk = 2;
constexpr int kDiscard = 3;
constexpr int kSave = 4;
constexpr int kBack = 5;
constexpr int kNext = 6;
constexpr int kGenerate = 7;
constexpr int kPlay = 8;
constexpr int kRename = 9;
constexpr int kClose = 10;
constexpr int kReset = 11;
constexpr int kCollect = 12;
constexpr int kKeep = 13;
constexpr int kStay = 14;
constexpr int kScenarioList = 15;
constexpr int kRecover = 16;
constexpr int kNotNow = 17;
constexpr int kDelete = 18;
constexpr int kUp = 19;
constexpr int kSystemFiles = 20;
constexpr int kShortScale = 21;
constexpr int kLongScale = 22;

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

Cairo::RefPtr<Cairo::Context> same_surface(cairo_t *cr)
{
    auto surface = Cairo::RefPtr<Cairo::ImageSurface>(new Cairo::ImageSurface(cairo_get_target(cr), false));
    return Cairo::Context::create(surface);
}

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
    SDL_GetWindowSizeInPixels(window_, &win_w_, &win_h_);
    if (win_w_ < 640) {
        win_w_ = 640;
    }
    if (win_h_ < 480) {
        win_h_ = 480;
    }
    menu_h_ = 36;
    welcome_h_ = welcome_ ? 52 : 0;
    status_h_ = 36;
    message_h_ = 44;
    const int body = win_h_ - menu_h_ - welcome_h_ - status_h_ - message_h_;
    cell_ = 48;
    while (cell_ > 36 && ToolPalette::height_for(cell_) + 210 > body) {
        cell_ -= 4;
    }
    palette_.set_cell(cell_);
    side_w_ = ToolPalette::width_for(cell_);
    side_x_ = 6;
    pal_y_ = menu_h_ + welcome_h_ + 6;
    mini_y_ = pal_y_ + ToolPalette::height_for(cell_) + 8;
    mini_h_ = std::max(64, side_w_ * 5 / 6);
    demand_y_ = mini_y_ + mini_h_ + 8;
    demand_h_ = std::max(72, menu_h_ + welcome_h_ + body - demand_y_ - 6);
    view_x_ = side_x_ + side_w_ + 8;
    view_y_ = menu_h_ + welcome_h_;
    view_w_ = std::max(32, win_w_ - view_x_ - 8);
    view_h_ = std::max(32, win_h_ - view_y_ - status_h_ - message_h_);
    cursor_x_ = std::max(0.0f, std::min(cursor_x_, static_cast<float>(win_w_ - 1)));
    cursor_y_ = std::max(0.0f, std::min(cursor_y_, static_cast<float>(win_h_ - 1)));
}

void Shell::button(cairo_t *cr, double x, double y, double w, double h, const char *label, int id, bool primary)
{
    push_hot(x, y, w, h, id);
    push_focus(id);
    const bool hot = hover_id_ == id || focus_id_ == id;
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
    if (focus_id_ == id && (gamepad_nav_ || dialog_ != Dialog::None)) {
        cairo_set_source_rgb(cr, theme::kFocusR, theme::kFocusG, theme::kFocusB);
        cairo_set_line_width(cr, 2);
        cairo_rectangle(cr, x + 2, y + 2, w - 4, h - 4);
        cairo_stroke(cr);
    }
    const std::string plain = strip_mnemonic(label);
    const TextMetrics metrics = measure_text(cr, theme::kUiFont, plain, -1);
    const double tx = x + (w - metrics.width) / 2.0;
    const double ty = y + (h - metrics.height) / 2.0;
    const bool light = primary || hot;
    paint_mnemonic(cr, theme::kUiFont, label, tx, ty, menu_.alt_shown, light ? 1 : theme::kTextR, light ? 1 : theme::kTextG,
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
    fill(cr, x, y, w, 32, theme::kSelectR, theme::kSelectG, theme::kSelectB);
    paint_text(cr, theme::kTitleFont, title, x + 12, y + 6, 1, 1, 1, static_cast<int>(w - 24));
    stroke_rect(cr, x, y, w, h, theme::kMenuFrameR, theme::kMenuFrameG, theme::kMenuFrameB);
}

void Shell::draw_osk(cairo_t *cr, double x, double y, double w)
{
    if (!show_osk() || active_field() == nullptr) {
        return;
    }
    static const char *keys[] = {"q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "a", "s", "d", "f", "g", "h",
                                 "j", "k", "l", "z", "x", "c", "v", "b", "n", "m", "space", "bksp", "0", "1",
                                 "2",    "3", "4", "5", "6", "7", "8", "9", "auto"};
    const int count = static_cast<int>(sizeof(keys) / sizeof(keys[0]));
    const double key_w = std::max(28.0, std::min(46.0, (w - 8) / 10.0));
    const double key_h = 32;
    for (int i = 0; i < count; ++i) {
        const int col = i % 10;
        const int row = i / 10;
        const double kx = x + col * (key_w + 2);
        const double ky = y + row * (key_h + 2);
        button(cr, kx, ky, i >= 26 && i <= 27 ? key_w * 1.4 : key_w, key_h, keys[i], kOsk + i, false);
    }
}

void Shell::draw_menu(cairo_t *cr)
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
        const TextMetrics metrics = measure_text(cr, theme::kMenuFont, plain, -1);
        const double w = metrics.width + 22;
        const int id = kMenuTitle + c;
        push_hot(x, 2, w, menu_h_ - 4, id);
        const bool open = menu_.open_menu == c;
        if (open || hover_id_ == id) {
            fill(cr, x, 3, w, menu_h_ - 6, theme::kSelectR, theme::kSelectG, theme::kSelectB);
            stroke_rect(cr, x, 3, w, menu_h_ - 6, theme::kSelectEdgeR, theme::kSelectEdgeG, theme::kSelectEdgeB);
        }
        paint_mnemonic(cr, theme::kMenuFont, title, x + 10, 9, menu_.alt_shown || menu_.open(),
                       open || hover_id_ == id ? 1 : theme::kTextR, open || hover_id_ == id ? 1 : theme::kTextG,
                       open || hover_id_ == id ? 1 : theme::kTextB);
        x += w + 2;
    }
    if (!menu_.open() || dialog_ != Dialog::None) {
        return;
    }
    const int column = menu_.open_menu;
    const MenuEntry *items = menu_table::column_items(column);
    const int count = menu_table::column_count(column);
    double drop_x = 4;
    for (int c = 0; c < column; ++c) {
        const TextMetrics metrics = measure_text(cr, theme::kMenuFont, strip_mnemonic(menu_table::column_title(c)), -1);
        drop_x += metrics.width + 24;
    }
    const double row_h = 32;
    const double drop_w = 320;
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
                cairo_arc(cr, drop_x + 15, y + 16, 6, 0, 6.3);
                cairo_stroke(cr);
                if (on) {
                    cairo_arc(cr, drop_x + 15, y + 16, 3, 0, 6.3);
                    cairo_fill(cr);
                }
            }
        }
        paint_mnemonic(cr, theme::kMenuFont, item.label, drop_x + 28, y + 7, menu_.alt_shown, theme::kTextR,
                       theme::kTextG, theme::kTextB);
        if (item.accel_text != nullptr) {
            const TextMetrics accel = measure_text(cr, theme::kMenuFont, item.accel_text, -1);
            paint_text(cr, theme::kMenuFont, item.accel_text, drop_x + drop_w - accel.width - 12, y + 7, theme::kAccelR,
                       theme::kAccelG, theme::kAccelB, -1);
        }
        y += row_h;
    }
    if (!menu_.recent_open) {
        return;
    }
    const double rx = drop_x + drop_w - 4;
    const int rows = std::max(1, static_cast<int>(recent_.size()));
    const double rh = rows * row_h + 8;
    fill(cr, rx, menu_h_ + 4 + 2 * row_h, 340, rh, 1, 1, 1);
    stroke_rect(cr, rx, menu_h_ + 4 + 2 * row_h, 340, rh, theme::kMenuFrameR, theme::kMenuFrameG, theme::kMenuFrameB);
    if (recent_.empty()) {
        paint_text(cr, theme::kMenuFont, "No recent cities", rx + 10, menu_h_ + 4 + 2 * row_h + 8, theme::kTextR,
                   theme::kTextG, theme::kTextB, 320);
        return;
    }
    for (int i = 0; i < static_cast<int>(recent_.size()); ++i) {
        const double ry = menu_h_ + 8 + 2 * row_h + i * row_h;
        const int id = kRecentItem + i;
        push_hot(rx + 2, ry, 336, row_h, id);
        if (menu_.recent_cursor == i || hover_id_ == id) {
            fill(cr, rx + 3, ry, 334, row_h, theme::kHoverR, theme::kHoverG, theme::kHoverB);
        }
        const std::string shown = recent_[static_cast<std::size_t>(i)];
        const auto slash = shown.find_last_of("/\\");
        paint_text(cr, theme::kMenuFont, slash == std::string::npos ? shown : shown.substr(slash + 1), rx + 10, ry + 7,
                   theme::kTextR, theme::kTextG, theme::kTextB, 310);
    }
}

void Shell::draw_dialog(cairo_t *cr)
{
    if (dialog_ == Dialog::None) {
        return;
    }
    const double dw = std::min(860.0, win_w_ - 48.0);
    const double dh = std::min(640.0, win_h_ - 48.0);
    const double x = (win_w_ - dw) / 2.0;
    const double y = (win_h_ - dh) / 2.0;
    auto buttons_at = [&](double by, const std::vector<std::pair<const char *, int>> &buttons) {
        const double gap = 8;
        const double bw = std::max(120.0, (dw - 24 - gap * (buttons.size() - 1)) / buttons.size());
        double bx = x + dw - 12 - bw * buttons.size() - gap * (buttons.size() - 1);
        for (const auto &button_def : buttons) {
            button(cr, bx, by, bw, 36, button_def.first, button_def.second, button_def.second == focus_id_);
            bx += bw + gap;
        }
    };
    if (dialog_ == Dialog::Unsaved) {
        frame_panel(cr, x, y, dw, 220, "Save changes?");
        label(cr, theme::kBodyFont, "Save changes to this city before continuing?", x + 16, y + 52, static_cast<int>(dw - 32));
        buttons_at(y + 160, {{"_Cancel", kCancel}, {"_Discard", kDiscard}, {"_Save", kSave}});
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
        frame_panel(cr, x, y, dw, 240, title);
        label(cr, theme::kBodyFont, question, x + 16, y + 52, static_cast<int>(dw - 32));
        label(cr, theme::kBodyFont, detail, x + 16, y + 84, static_cast<int>(dw - 32));
        buttons_at(y + 180, {{"_Cancel", kCancel}, {"_Start", kOk}});
        return;
    }
    if (dialog_ == Dialog::NewCity) {
        frame_panel(cr, x, y, dw, dh, "New City");
        if (new_page_ == 0) {
            label(cr, theme::kBodyFont, "Name the city.", x + 16, y + 48, static_cast<int>(dw - 32));
            fill(cr, x + 16, y + 80, dw - 32, 36, 1, 1, 1);
            stroke_rect(cr, x + 16, y + 80, dw - 32, 36, theme::kMenuFrameR, theme::kMenuFrameG, theme::kMenuFrameB);
            push_hot(x + 16, y + 80, dw - 32, 36, kFieldName);
            push_focus(kFieldName);
            paint_text(cr, theme::kBodyFont, field_a_.empty() ? " " : field_a_, x + 24, y + 88, theme::kTextR,
                       theme::kTextG, theme::kTextB, static_cast<int>(dw - 48));
            if (!dialog_error_.empty()) {
                paint_text(cr, theme::kUiFont, dialog_error_, x + 16, y + 126, 0.6, 0.1, 0.1, static_cast<int>(dw - 32));
            }
            draw_osk(cr, x + 16, y + 170, dw - 32);
            buttons_at(y + dh - 52, {{"_Cancel", kCancel}, {"_Next", kNext}});
        } else if (new_page_ == 1) {
            label(cr, theme::kUiFont, "Difficulty, land, and seed. Default land is the usual generator.", x + 16, y + 42,
                  static_cast<int>(dw - 32));
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
            for (int g = 0; g < 5; ++g) {
                const double gy = y + 78 + g * 42;
                paint_text(cr, theme::kUiFont, groups[g], x + 16, gy + 8, theme::kTextR, theme::kTextG, theme::kTextB, 120);
                for (int o = 0; o < 3; ++o) {
                    button(cr, x + 150 + o * 150, gy, 140, 34, options[g][o], kChoice + g * 10 + o, selected[g] == o);
                }
            }
            paint_text(cr, theme::kUiFont, "Seed", x + 16, y + 300, theme::kTextR, theme::kTextG, theme::kTextB, 80);
            fill(cr, x + 150, y + 292, 220, 34, 1, 1, 1);
            stroke_rect(cr, x + 150, y + 292, 220, 34, theme::kMenuFrameR, theme::kMenuFrameG, theme::kMenuFrameB);
            push_hot(x + 150, y + 292, 220, 34, kFieldSeed);
            push_focus(kFieldSeed);
            paint_text(cr, theme::kBodyFont, field_b_, x + 158, y + 298, theme::kTextR, theme::kTextG, theme::kTextB, 200);
            if (!dialog_error_.empty()) {
                paint_text(cr, theme::kUiFont, dialog_error_, x + 16, y + 336, 0.6, 0.1, 0.1, static_cast<int>(dw - 32));
            }
            buttons_at(y + dh - 52, {{"_Back", kBack}, {"_Next", kNext}});
        } else {
            std::string summary = "Generate \"" + field_a_ + "\".\nThis replaces the city on the map.";
            label(cr, theme::kBodyFont, summary, x + 16, y + 56, static_cast<int>(dw - 32));
            buttons_at(y + dh - 52, {{"_Back", kBack}, {"_Generate", kGenerate}});
        }
        return;
    }
    if (dialog_ == Dialog::Scenario) {
        frame_panel(cr, x, y, dw, dh, "Play Scenario");
        label(cr, theme::kUiFont, "Choose a scenario. Playing it replaces the city on the map.", x + 16, y + 42,
              static_cast<int>(dw - 32));
        const double row_h = 56;
        const int visible = std::min(CitySession::kScenarioCount, static_cast<int>((dh - 140) / row_h));
        for (int i = 0; i < visible; ++i) {
            const CitySession::ScenarioDef &def = CitySession::scenario_def(i);
            const double ry = y + 78 + i * row_h;
            const int id = kListItem + i;
            push_hot(x + 12, ry, dw - 24, row_h - 4, id);
            push_focus(id);
            if (scenario_index_ == i || focus_id_ == id) {
                fill(cr, x + 12, ry, dw - 24, row_h - 4, theme::kHoverR, theme::kHoverG, theme::kHoverB);
            }
            const std::string line = std::string(def.name) + " (" + std::to_string(def.year) + ")";
            paint_text(cr, theme::kBodyFont, line, x + 20, ry + 4, theme::kTextR, theme::kTextG, theme::kTextB,
                       static_cast<int>(dw - 40));
            paint_text(cr, theme::kUiFont, def.goal, x + 20, ry + 26, theme::kAccelR, theme::kAccelG, theme::kAccelB,
                       static_cast<int>(dw - 40));
        }
        buttons_at(y + dh - 52, {{"_Cancel", kCancel}, {"_Play", kPlay}});
        return;
    }
    if (dialog_ == Dialog::Rename) {
        frame_panel(cr, x, y, dw, 280, "Rename City");
        label(cr, theme::kBodyFont, "New name for this city:", x + 16, y + 48, static_cast<int>(dw - 32));
        fill(cr, x + 16, y + 84, dw - 32, 36, 1, 1, 1);
        stroke_rect(cr, x + 16, y + 84, dw - 32, 36, theme::kMenuFrameR, theme::kMenuFrameG, theme::kMenuFrameB);
        push_hot(x + 16, y + 84, dw - 32, 36, kFieldName);
        push_focus(kFieldName);
        paint_text(cr, theme::kBodyFont, field_a_, x + 24, y + 92, theme::kTextR, theme::kTextG, theme::kTextB,
                   static_cast<int>(dw - 48));
        if (!dialog_error_.empty()) {
            paint_text(cr, theme::kUiFont, dialog_error_, x + 16, y + 128, 0.6, 0.1, 0.1, static_cast<int>(dw - 32));
        }
        draw_osk(cr, x + 16, y + 156, dw - 32);
        buttons_at(y + 230, {{"_Cancel", kCancel}, {"_Rename", kRename}});
        return;
    }
    if (dialog_ == Dialog::About) {
        frame_panel(cr, x, y, dw, 360, "About Lunduke City 2");
        const std::string body = std::string(kProductName) + " " + kPackageVersion +
                                 "\n\nA city-building game for LCOS, Steam Deck, and Windows. "
                                 "Hard fork of Lunduke City. The simulation is the engine described in NOTICE.\n\n"
                                 "Independent work. Not affiliated with Electronic Arts.\n"
                                 "GPL-3.0-or-later. See NOTICE and COPYING.";
        label(cr, theme::kBodyFont, body, x + 16, y + 48, static_cast<int>(dw - 32));
        buttons_at(y + 300, {{"_Close", kClose}});
        return;
    }
    if (dialog_ == Dialog::Recovery || dialog_ == Dialog::RecoveryDelete) {
        frame_panel(cr, x, y, dw, 280, dialog_ == Dialog::Recovery ? "Recover unsaved city?" : "Delete this unsaved city?");
        std::string body = dialog_ == Dialog::Recovery
                               ? "Lunduke City 2 found a city from a previous session that was not saved:\n" +
                                     city_recovery::describe(recovery_)
                               : city_recovery::describe(recovery_) + "\n\nThis cannot be undone.";
        if (dialog_ == Dialog::Recovery && recovery_left_ > 0) {
            body += "\n(" + std::to_string(recovery_left_) + " older one" + (recovery_left_ == 1 ? " is" : "s are") +
                    " kept for later.)";
        }
        label(cr, theme::kBodyFont, body, x + 16, y + 48, static_cast<int>(dw - 32));
        if (dialog_ == Dialog::Recovery) {
            buttons_at(y + 220, {{"_Discard…", kDelete}, {"_Not Now", kNotNow}, {"_Recover", kRecover}});
        } else {
            buttons_at(y + 220, {{"_Cancel", kCancel}, {"_Delete", kDelete}});
        }
        return;
    }
    if (dialog_ == Dialog::Files || dialog_ == Dialog::FileWait) {
        const char *title = files_save_ ? "Save City" : "Load City";
        frame_panel(cr, x, y, dw, dh, dialog_ == Dialog::FileWait ? "System file dialog" : title);
        if (dialog_ == Dialog::FileWait) {
            label(cr, theme::kBodyFont, "Waiting for the system file dialog. If it does not appear, browse in the window.",
                  x + 16, y + 56, static_cast<int>(dw - 32));
            buttons_at(y + dh - 52, {{"_Cancel", kCancel}, {"Browse in the window", kSystemFiles}});
            return;
        }
        label(cr, theme::kUiFont, files_dir_, x + 16, y + 40, static_cast<int>(dw - 32));
        const double list_top = y + 68;
        const double list_h = dh - 160;
        const int visible = std::max(1, static_cast<int>(list_h / 32));
        for (int i = 0; i < visible && i < static_cast<int>(files_.size()); ++i) {
            const double ry = list_top + i * 32;
            const int id = kListItem + i;
            push_hot(x + 12, ry, dw - 24, 30, id);
            push_focus(id);
            if (list_index_ == i) {
                fill(cr, x + 12, ry, dw - 24, 30, theme::kHoverR, theme::kHoverG, theme::kHoverB);
            }
            paint_text(cr, theme::kBodyFont, files_[static_cast<std::size_t>(i)], x + 20, ry + 5, theme::kTextR,
                       theme::kTextG, theme::kTextB, static_cast<int>(dw - 48));
        }
        if (files_.empty()) {
            paint_text(cr, theme::kBodyFont, "No .cty files in this folder.", x + 20, list_top + 6, theme::kAccelR,
                       theme::kAccelG, theme::kAccelB, static_cast<int>(dw - 40));
        }
        if (files_save_) {
            fill(cr, x + 16, y + dh - 100, dw - 32, 34, 1, 1, 1);
            stroke_rect(cr, x + 16, y + dh - 100, dw - 32, 34, theme::kMenuFrameR, theme::kMenuFrameG, theme::kMenuFrameB);
            push_hot(x + 16, y + dh - 100, dw - 32, 34, kFieldName);
            push_focus(kFieldName);
            paint_text(cr, theme::kBodyFont, field_a_, x + 24, y + dh - 92, theme::kTextR, theme::kTextG, theme::kTextB,
                       static_cast<int>(dw - 48));
        }
        buttons_at(y + dh - 52, {{"_Up", kUp}, {"_Cancel", kCancel}, {files_save_ ? "_Save" : "_Open", files_save_ ? kSave : kOk}});
        return;
    }
    if (dialog_ == Dialog::LoadError || dialog_ == Dialog::SaveError || dialog_ == Dialog::Info) {
        frame_panel(cr, x, y, dw, 280, info_title_.c_str());
        label(cr, theme::kBodyFont, info_body_, x + 16, y + 48, static_cast<int>(dw - 32));
        buttons_at(y + 220, {{"_OK", kOk}});
        return;
    }
    if (dialog_ == Dialog::Overwrite) {
        frame_panel(cr, x, y, dw, 220, "Replace file?");
        label(cr, theme::kBodyFont, "A city file with that name already exists. Replace it?", x + 16, y + 52,
              static_cast<int>(dw - 32));
        buttons_at(y + 150, {{"_Cancel", kCancel}, {"_Replace", kSave}});
        return;
    }
    if (dialog_ == Dialog::Outcome) {
        frame_panel(cr, x, y, dw, 300, info_title_.c_str());
        label(cr, theme::kBodyFont, info_body_, x + 16, y + 48, static_cast<int>(dw - 32));
        buttons_at(y + 240, {{"Stay _paused", kStay}, {"_Scenario list", kScenarioList}, {"_Keep playing", kKeep}});
        return;
    }
    if (dialog_ == Dialog::Budget) {
        frame_panel(cr, x, y, dw, dh, budget_is_tax_ ? "Taxes collected" : "Budget");
        const auto book = session_.budget();
        std::string head = std::string(book.estimates ? "Estimates. " : "") + "Funds " + graph_money(book.funds) +
                           "    Tax " + graph_money(book.taxes) + "    Cash flow " + graph_money(book.cash_flow);
        label(cr, theme::kBodyFont, head, x + 16, y + 44, static_cast<int>(dw - 32));
        auto slider = [&](int index, const char *name, int value, int min, int max, const std::string &note, double sy) {
            paint_text(cr, theme::kBodyFont, std::string(name) + "  " + std::to_string(value) + "%", x + 16, sy,
                       theme::kTextR, theme::kTextG, theme::kTextB, static_cast<int>(dw - 32));
            const double sx = x + 16;
            const double sw = dw - 32;
            fill(cr, sx, sy + 26, sw, 16, 1, 1, 1);
            const double t = (value - min) / static_cast<double>(std::max(1, max - min));
            fill(cr, sx, sy + 26, sw * t, 16, theme::kSelectR, theme::kSelectG, theme::kSelectB);
            stroke_rect(cr, sx, sy + 26, sw, 16, theme::kButtonEdgeR, theme::kButtonEdgeG, theme::kButtonEdgeB);
            const int id = kSlider + index;
            push_hot(sx, sy + 20, sw, 28, id);
            push_focus(id);
            next_sliders_.push_back(SliderGeom{id, sx, sy + 20, sw, 28, min, max});
            if (focus_id_ == id) {
                cairo_set_source_rgb(cr, theme::kFocusR, theme::kFocusG, theme::kFocusB);
                cairo_set_line_width(cr, 2);
                cairo_rectangle(cr, sx + 1, sy + 22, sw - 2, 24);
                cairo_stroke(cr);
            }
            if (!note.empty()) {
                paint_text(cr, theme::kUiFont, note, x + 16, sy + 46, theme::kAccelR, theme::kAccelG, theme::kAccelB,
                           static_cast<int>(dw - 32));
            }
        };
        slider(1, "Tax rate", tax_, 0, 20, "", y + 84);
        slider(2, "Roads", road_, 0, 100, book.road_note, y + 150);
        slider(3, "Police", police_, 0, 100, book.police_note, y + 230);
        slider(4, "Fire", fire_, 0, 100, book.fire_note, y + 310);
        const std::string spent = "Road " + graph_money(book.road_spent) + "   Police " + graph_money(book.police_spent) +
                                  "   Fire " + graph_money(book.fire_spent);
        label(cr, theme::kUiFont, spent, x + 16, y + 400, static_cast<int>(dw - 32));
        if (budget_is_tax_) {
            buttons_at(y + dh - 52, {{"_Reset", kReset}, {"_Collect Taxes", kCollect}});
        } else {
            buttons_at(y + dh - 52, {{"_Cancel", kCancel}, {"_OK", kOk}});
        }
        return;
    }
    if (dialog_ == Dialog::Graphs) {
        frame_panel(cr, x, y, dw, dh, "Graphs");
        button(cr, x + 16, y + 42, 140, 32, "10 year", kShortScale, !graphs_long_);
        button(cr, x + 164, y + 42, 140, 32, "120 year", kLongScale, graphs_long_);
        auto live = same_surface(cr);
        live->translate(x + 16, y + 84);
        draw_history_chart(live, &session_, graphs_long_, static_cast<int>(dw - 32),
                           static_cast<int>(std::max(80.0, dh - 250)));
        double ly = y + dh - 130;
        for (const ChartLegend &entry : chart_legend(&session_, graphs_long_)) {
            cairo_set_source_rgb(cr, entry.r, entry.g, entry.b);
            cairo_rectangle(cr, x + 16, ly + 4, 18, 8);
            cairo_fill(cr);
            paint_text(cr, theme::kUiFont, entry.caption, x + 40, ly, theme::kTextR, theme::kTextG, theme::kTextB, 360);
            ly += 18;
        }
        buttons_at(y + dh - 52, {{"_Close", kClose}});
        return;
    }
    if (dialog_ == Dialog::Evaluation) {
        frame_panel(cr, x, y, dw, dh, "Evaluation");
        const auto eval = session_.evaluation();
        std::string body;
        if (eval.no_residents) {
            body = "Nobody lives here yet, so there is no poll and no list of problems.";
        } else {
            body = "Score " + std::to_string(eval.score) + " (" + std::to_string(eval.score_delta) + ")\n" +
                   eval.category + "\n" + std::to_string(eval.yes_percent) + "% approve\nPopulation " +
                   std::to_string(eval.population) + "\n" + eval.difficulty + "\n";
            for (const auto &problem : eval.problems) {
                body += problem.name + " (" + std::to_string(problem.votes) + ")\n";
            }
        }
        const std::string progress = session_.scenario_progress();
        if (!progress.empty()) {
            body += "\n" + progress;
        }
        label(cr, theme::kBodyFont, body, x + 16, y + 48, static_cast<int>(dw - 32));
        buttons_at(y + dh - 52, {{"_Close", kClose}});
        return;
    }
    if (dialog_ == Dialog::Overlay) {
        frame_panel(cr, x, y, dw, dh, overlay_.title());
        label(cr, theme::kUiFont, overlay_.legend(), x + 16, y + 40, static_cast<int>(dw - 32));
        const int ow = static_cast<int>(dw - 32);
        const int oh = static_cast<int>(std::max(80.0, dh - 140));
        overlay_x_ = x + 16;
        overlay_y_ = y + 64;
        overlay_w_ = ow;
        overlay_h_ = oh;
        auto live = same_surface(cr);
        live->translate(overlay_x_, overlay_y_);
        overlay_.sync();
        overlay_.draw(live, ow, oh);
        push_hot(overlay_x_, overlay_y_, ow, oh, 90);
        buttons_at(y + dh - 52, {{"_Close", kClose}});
        return;
    }
    if (dialog_ == Dialog::Query) {
        frame_panel(cr, x, y, std::min(dw, 520.0), 300, "Query");
        label(cr, theme::kBodyFont, info_body_, x + 16, y + 48, static_cast<int>(std::min(dw, 520.0) - 32));
        buttons_at(y + 240, {{"_Close", kClose}});
    }
}

void Shell::draw(cairo_t *cr)
{
    menu_.recent_count = static_cast<int>(recent_.size());
    fill(cr, 0, 0, win_w_, win_h_, theme::kWindowR, theme::kWindowG, theme::kWindowB);
    draw_menu(cr);
    if (welcome_) {
        fill(cr, 0, menu_h_, win_w_, welcome_h_, theme::kTooltipR, theme::kTooltipG, theme::kTooltipB);
        const std::string line = std::string("Welcome to Lunduke City 2. The clock is ") +
                                 (speed_ <= 0 ? "paused." : std::string("running at ") + speed_word(speed_) + ".") +
                                 " Choose a speed under Options, start a city with System, New City, or pick Play Scenario.";
        paint_text(cr, theme::kUiFont, line, 10, menu_h_ + 6, theme::kTextR, theme::kTextG, theme::kTextB, win_w_ - 140);
        button(cr, win_w_ - 120, menu_h_ + 8, 100, 32, "Dismiss", kWelcomeId, false);
    }
    {
        auto live = same_surface(cr);
        live->translate(side_x_, pal_y_);
        palette_.draw(live);
    }
    push_hot(side_x_, pal_y_, side_w_, ToolPalette::height_for(cell_), kPaletteId);
    {
        auto live = same_surface(cr);
        live->translate(side_x_, mini_y_);
        minimap_.sync();
        minimap_.draw(live, side_w_, mini_h_);
    }
    push_hot(side_x_, mini_y_, side_w_, mini_h_, kMinimapId);
    {
        auto live = same_surface(cr);
        live->translate(side_x_, demand_y_);
        demand_.draw(live, side_w_, demand_h_);
    }
    stroke_rect(cr, view_x_ - 1, view_y_ - 1, view_w_ + 2, view_h_ + 2, theme::kMenuFrameR, theme::kMenuFrameG,
                theme::kMenuFrameB);
    cairo_save(cr);
    cairo_rectangle(cr, view_x_, view_y_, view_w_, view_h_);
    cairo_clip(cr);
    {
        auto live = same_surface(cr);
        live->rectangle(view_x_, view_y_, view_w_, view_h_);
        live->clip();
        live->translate(view_x_ - scroll_x_, view_y_ - scroll_y_);
        map_.sync();
        map_.draw(live);
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
    (void)kHScroll;
    (void)kVScroll;
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
    paint_text(cr, theme::kUiFont, status, 8, status_y + 8, theme::kTextR, theme::kTextG, theme::kTextB, win_w_ / 2);
    if (pad_ != nullptr || gamepad_nav_) {
        const std::string hint = std::string(glyph(SDL_GAMEPAD_BUTTON_SOUTH)) + " build   " +
                                 glyph(SDL_GAMEPAD_BUTTON_EAST) + " cancel   LB/RB tools   Start menu";
        const TextMetrics metrics = measure_text(cr, theme::kUiFont, hint, -1);
        paint_text(cr, theme::kUiFont, hint, win_w_ - metrics.width - 8, status_y + 8, theme::kTextR, theme::kTextG,
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
    paint_text(cr, theme::kBodyFont, bar, 8, win_h_ - message_h_ + 10, theme::kTextR, theme::kTextG, theme::kTextB,
               win_w_ - 16);
    const std::string goal = session_.scenario_progress();
    if (!goal.empty() && dialog_ == Dialog::None) {
        paint_text(cr, theme::kUiFont, goal, view_x_ + 8, view_y_ + 8, theme::kTextR, theme::kTextG, theme::kTextB,
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
}

void Shell::render()
{
    layout();
    clamp_scroll();
    auto surface = Cairo::ImageSurface::create(Cairo::FORMAT_ARGB32, win_w_, win_h_);
    auto context = Cairo::Context::create(surface);
    next_hot_.clear();
    next_focus_.clear();
    next_sliders_.clear();
    draw(context->cobj());
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
    surface->flush();
    if (!shot_path_.empty()) {
        cairo_surface_write_to_png(surface->cobj(), shot_path_.c_str());
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
        SDL_UpdateTexture(texture_, nullptr, surface->get_data(), surface->get_stride());
        SDL_RenderClear(renderer_);
        SDL_RenderTexture(renderer_, texture_, nullptr, nullptr);
        SDL_RenderPresent(renderer_);
    }
}
