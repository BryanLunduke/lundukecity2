// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#include "shell_state.hpp"

#include "list_scroll.hpp"
#include "redraw.hpp"
#include "text_paint.hpp"
#include "tile_atlas.hpp"
#include "ui_scale.hpp"

#include <string>

void Shell::run_review_checks()
{
    auto fail = [&](const std::string &message) {
        ran_ok_ = false;
        smoke_error_ = message;
    };

    // C1: rows past the first screen are selectable, and the scroll offset
    // follows the selection. D-pad, wheel and Page Down share this path.
    files_.clear();
    for (int i = 0; i < 40; ++i) {
        files_.push_back("city-" + std::to_string(i) + ".cty");
    }
    files_save_ = false;
    dialog_ = Dialog::Files;
    list_index_ = 0;
    list_scroll_ = 0;
    files_visible_ = 10;
    focus_id_ = kListItem;
    if (!nudge_file_list(1, true, true) || list_index_ != 9) {
        fail("page down should move a full screen, index " + std::to_string(list_index_));
        return;
    }
    if (!nudge_file_list(30, false, true)) {
        fail("forced nudge did not move the file list");
        return;
    }
    reveal_file_list();
    if (list_index_ < 20 || list_scroll_ > list_index_ || list_index_ >= list_scroll_ + files_visible_) {
        fail("row " + std::to_string(list_index_) + " is not inside the scrolled window starting at " +
             std::to_string(list_scroll_));
        return;
    }
    const int before = list_index_;
    focus_id_ = kListItem + (list_index_ - list_scroll_);
    if (!nudge_file_list(-1, false, false) || list_index_ != before - 1) {
        fail("d-pad up on the list should move the selection");
        return;
    }
    // A click on the last visible row selects that file, not the row number.
    list_scroll_ = 21;
    list_index_ = 21;
    on_hotspot(kListItem + 4, 0, 0);
    if (list_index_ != 25) {
        fail("clicking visible row 4 while scrolled to 21 should select file 25, got " + std::to_string(list_index_));
        return;
    }
    dialog_ = Dialog::None;

    // H1: Budget Cancel restores the rates from when the window opened,
    // including outside a tax year where the sliders apply live.
    apply_action(Action::Budget);
    if (dialog_ != Dialog::Budget || budget_is_tax_) {
        fail("budget did not open for an ordinary year");
        return;
    }
    const int opened_tax = open_tax_;
    tax_ = opened_tax + 5 > 20 ? 20 : opened_tax + 5;
    session_.set_tax(tax_);
    on_dialog_button(kCancel);
    if (dialog_ != Dialog::None || session_.tax() != opened_tax) {
        fail("budget cancel kept tax " + std::to_string(session_.tax()) + " opened at " + std::to_string(opened_tax));
        return;
    }

    // H2: Save from the unsaved prompt on a city with no file must keep the
    // action that was waiting (Quit, New City, Load, ...).
    after_save_ = AfterSave::Quit;
    apply_action(Action::Save);
    if (after_save_ != AfterSave::Quit || (dialog_ != Dialog::Files && dialog_ != Dialog::FileWait)) {
        fail("save-as from the quit prompt dropped the pending quit");
        return;
    }
    after_save_ = AfterSave::Nothing;
    close_dialog();

    // H3: a failed save from the quit prompt must not make the next save quit.
    after_save_ = AfterSave::Quit;
    save_generation_ = city_generation_;
    finish_save("/no/such/lunduke-city-dir/city.cty");
    if (after_save_ != AfterSave::Nothing || dialog_ != Dialog::SaveError || quitting_) {
        fail("failed quit-save left the game armed to quit");
        return;
    }
    on_dialog_button(kOk);
    if (quitting_ || after_save_ != AfterSave::Nothing) {
        fail("dismissing the save error quit the game");
        return;
    }

    // H4: a missing tile sheet is reported once, not every frame.
    const int attempts = tile_atlas().load_attempts();
    tile_atlas().ensure_loaded();
    tile_atlas().ensure_loaded();
    if (tile_atlas().load_attempts() != attempts) {
        fail("map tiles were opened more than once");
        return;
    }
    if (!tile_atlas().loaded() && tile_atlas().load_error().find("tiles.png") == std::string::npos) {
        fail("missing tiles did not name tiles.png");
        return;
    }

    // H5: headless play stays at the classic size and a 1x scale. A saved
    // window must not be written from the test, and a short laptop work area
    // must shrink the default window.
    if (std::abs(ui_scale_ - 1.0f) > 0.01f || font_ui_ != "Sans 13") {
        fail("headless UI scale should stay 1, got " + font_ui_);
        return;
    }
    if (win_w_ != 1280 || win_h_ != 800) {
        fail("headless window should stay 1280x800");
        return;
    }
    if (allow_saved_settings()) {
        fail("review run would persist window settings");
        return;
    }
    const ui_scale::Placement laptop = ui_scale::fit_default_window(1280, 800, 0, 0, 1366, 768);
    if (laptop.h + window_fit::kFrameTop + window_fit::kFrameBottom > 768 || laptop.w + 2 * window_fit::kFrameSide > 1366) {
        fail("default window does not fit a 1366x768 work area");
        return;
    }
    const ui_scale::Placement desktop = ui_scale::fit_default_window(1280, 800, 0, 0, 1920, 1080);
    if (desktop.w < 1280 || desktop.h < 800) {
        fail("a 1080p display should not shrink the classic window");
        return;
    }

    // H6: a paused city reuses its frame and does not repaint when nothing changed.
    if (skipped_redraws_ < 1) {
        fail("paused city repainted every frame (" + std::to_string(redraws_) + " draws)");
        return;
    }
    if (surface_builds_ != 1) {
        fail("cairo surface was rebuilt for an unchanged window");
        return;
    }
    if (text_cache_entries() < 1) {
        fail("text layouts were not cached");
        return;
    }
    redraw::Reasons idle;
    if (redraw::needed(idle)) {
        fail("an idle frame asked for a redraw");
        return;
    }
    redraw::Reasons running;
    running.sim_running = true;
    if (!redraw::needed(running)) {
        fail("a running simulation did not ask for a redraw");
        return;
    }
}
