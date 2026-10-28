// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#include "controls_page.hpp"
#include "menu_model.hpp"
#include "text_edit.hpp"

#include <cmath>
#include <iostream>
#include <vector>

namespace {

int fail(const char *message)
{
    std::cerr << message << "\n";
    return 1;
}

} // namespace

int main()
{
    if (menu_table::actionable_count() != 50) {
        return fail("expected 50 actionable menu rows");
    }
    if (mnemonic_of("Rename Cit_y…") != 'y') {
        return fail("Rename should use Y");
    }
    for (int column = 0; column < menu_table::kColumnCount; ++column) {
        bool used[26] = {};
        const MenuEntry *items = menu_table::column_items(column);
        const int count = menu_table::column_count(column);
        for (int i = 0; i < count; ++i) {
            const char mark = mnemonic_of(items[i].label);
            if (mark < 'a' || mark > 'z') {
                continue;
            }
            const int slot = mark - 'a';
            if (used[slot]) {
                return fail("a menu column reuses a mnemonic");
            }
            used[slot] = true;
        }
    }
    if (menu_table::accel_action(AccelKey::N, static_cast<int>(AccelMod::Ctrl)) != Action::NewCity) {
        return fail("Ctrl+N should start a new city");
    }
    if (menu_table::accel_action(AccelKey::P, static_cast<int>(AccelMod::Ctrl)) != Action::TogglePause) {
        return fail("Ctrl+P should toggle pause");
    }
    if (menu_table::accel_action(AccelKey::D1, static_cast<int>(AccelMod::Ctrl)) != Action::Slow) {
        return fail("Ctrl+1 should be slow");
    }
    if (menu_table::hidden_zoom_accel(AccelKey::Plus, static_cast<int>(AccelMod::Ctrl)) != Action::ZoomIn ||
        menu_table::hidden_zoom_accel(AccelKey::KpSub, static_cast<int>(AccelMod::Ctrl)) != Action::ZoomOut) {
        return fail("hidden zoom accelerators");
    }
    const int options = menu_table::column_count(1);
    const MenuEntry *option_items = menu_table::column_items(1);
    if (options < 3 || option_items[options - 3].action != Action::ClassicLook ||
        option_items[options - 2].action != Action::SvgaLook ||
        option_items[options - 1].action != Action::SpookyLook || option_items[options - 1].kind != ItemKind::Radio ||
        option_items[options - 3].radio_group != 2 || option_items[options - 2].radio_group != 2 ||
        option_items[options - 1].radio_group != 2) {
        return fail("Classic, SVGA, and Spooky Look should be the last Options radios");
    }
    MenuNav nav;
    if (!nav.handle_key(AccelKey::None, static_cast<int>(AccelMod::Alt), 's')) {
        return fail("Alt+S should open System");
    }
    if (nav.open_menu != 0) {
        return fail("System is the first menu");
    }
    nav.handle_key(AccelKey::Return, 0, 0);
    if (nav.activated != Action::NewCity) {
        return fail("Enter on the first System row should be New City");
    }
    MenuNav options_nav;
    if (!options_nav.handle_key(AccelKey::None, static_cast<int>(AccelMod::Alt), 'o') || options_nav.open_menu != 1) {
        return fail("Alt+O should open Options");
    }
    if (!options_nav.handle_key(AccelKey::Letter, 0, 'y') || options_nav.activated != Action::SpookyLook) {
        return fail("Y should activate Spooky Look");
    }

    MenuNav windows;
    if (!windows.handle_key(AccelKey::None, static_cast<int>(AccelMod::Alt), 'w') || windows.open_menu != 3) {
        return fail("Alt+W should open Windows");
    }
    const MenuEntry *window_items = menu_table::column_items(3);
    const int window_count = menu_table::column_count(3);
    int last = window_count - 1;
    while (last > 0 && window_items[last].kind == ItemKind::Separator) {
        --last;
    }
    for (int i = 0; i < window_count + 2 && windows.cursor != last; ++i) {
        windows.handle_key(AccelKey::Down, 0, 0);
    }
    if (windows.cursor != last) {
        return fail("Down did not reach the last Windows row");
    }
    windows.handle_key(AccelKey::Return, 0, 0);
    if (windows.activated != Action::Messages) {
        return fail("the last Windows row should be Messages");
    }

    const double text_h = 17;
    const double avails[] = {478, 485, 520, 678};
    for (double avail : avails) {
        for (int column = 1; column <= 3; column += 2) {
            const MenuEntry *items = menu_table::column_items(column);
            const int count = menu_table::column_count(column);
            int n_item = 0;
            int n_sep = 0;
            for (int i = 0; i < count; ++i) {
                if (items[i].kind == ItemKind::Separator) {
                    ++n_sep;
                } else {
                    ++n_item;
                }
            }
            const menu_table::MenuPitch pitch = menu_table::menu_pitch(avail, n_item, n_sep, 36, text_h);
            if (pitch.text_dy + text_h > pitch.row_h - 0.5) {
                return fail("a menu label does not fit in its row");
            }
            double rel = 5;
            double last_top = 5;
            for (int i = 0; i < count; ++i) {
                if (i == count - 1) {
                    last_top = rel;
                }
                rel += items[i].kind == ItemKind::Separator ? 8.0 : pitch.row_h;
            }
            const double content = rel;
            const double view = content < avail ? content : avail;
            const double row_h = items[count - 1].kind == ItemKind::Separator ? 8.0 : pitch.row_h;
            const double scroll = menu_table::menu_scroll_for_row(last_top, row_h, view, content);
            const double top = last_top - scroll;
            const double bottom = top + row_h;
            if (top < -0.5 || bottom > view + 0.5) {
                std::cerr << "avail " << avail << " column " << column << " top " << top << " bottom " << bottom
                          << " view " << view << "\n";
                return fail("the last menu row is clipped when it has focus");
            }
        }
    }
    if (pause_run_hint("Y", "\u2630").find("then Options") == std::string::npos ||
        pause_run_hint("\u25B3", "Options").find("Options then Options") != std::string::npos ||
        pause_run_hint("\u25B3", "Options").find("or Options,") == std::string::npos) {
        return fail("the PlayStation pause hint names Options twice");
    }
    {
        constexpr int n = 18;
        double tops[n];
        double heights[n];
        double rel = 5;
        for (int i = 0; i < n; ++i) {
            tops[i] = rel;
            heights[i] = 30;
            rel += 30;
        }
        const double content = rel;
        const double view = 200;
        const double scroll = menu_table::menu_reveal_scroll(tops, heights, n, n - 1, view, content);
        const int first = menu_table::menu_scroll_row(tops, n, scroll);
        if (std::fabs(scroll - (tops[first] - tops[0])) > 0.1) {
            return fail("a scrolled menu starts in the middle of a row");
        }
        if (tops[n - 1] + heights[n - 1] > scroll + view + 0.5 || tops[n - 1] < scroll - 0.5) {
            return fail("the last menu row is not in view when it has focus");
        }
        const int stepped = menu_table::menu_scroll_row(tops, n, menu_table::menu_row_scroll(tops, n, first + 1));
        if (stepped != first + 1) {
            return fail("the menu wheel did not move by one row");
        }
    }
    if (field_backspace_would_delete("Mesa", 0, 0) || !field_backspace_would_delete("Mesa", 2, 2) ||
        !field_backspace_would_delete("Mesa", 0, 4) || field_backspace_would_delete("", 0, 0)) {
        return fail("backspace is offered when the caret cannot delete");
    }
    return 0;
}
