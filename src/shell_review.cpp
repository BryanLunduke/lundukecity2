// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#include "shell_state.hpp"

#include "list_scroll.hpp"

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
}
