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
    if (menu_table::actionable_count() != 37) {
        return fail("expected 37 actionable menu rows");
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
