// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#include "menu_model.hpp"

#include <iostream>

namespace {

int fail(const char *message)
{
    std::cerr << message << "\n";
    return 1;
}

} // namespace

int main()
{
    if (menu_table::actionable_count() != 43) {
        return fail("expected 43 actionable menu rows");
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
    return 0;
}
