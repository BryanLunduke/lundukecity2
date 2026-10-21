// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#pragma once

#include "menu_model.hpp"

#include <string>

// Unmodified letter keys for the tool column. Ctrl, Alt, and Shift chords
// stay with the menu (Ctrl+O load, Ctrl+S save, Ctrl+P pause, Ctrl+N new,
// Ctrl+Q quit). The letters below are free when no modifier is held.

inline char tool_hotkey(int index)
{
    static const char keys[] = {'b', 'r', 't', 'w', 'g', 'h', 'c', 'i', 'o', 'f', 'd', 'p', 'e', 'u', 'a', 'v', 'q'};
    if (index < 0 || index >= static_cast<int>(sizeof(keys))) {
        return 0;
    }
    return keys[index];
}

inline int tool_index_for_key(char letter)
{
    if (letter >= 'A' && letter <= 'Z') {
        letter = static_cast<char>(letter - 'A' + 'a');
    }
    for (int i = 0; i < 17; ++i) {
        if (tool_hotkey(i) == letter) {
            return i;
        }
    }
    return -1;
}

inline std::string tool_hint_with_key(int index, const char *hint)
{
    std::string text = hint != nullptr ? hint : "";
    const char key = tool_hotkey(index);
    if (key == 0) {
        return text;
    }
    if (!text.empty()) {
        text += "  ";
    }
    text.push_back('[');
    text.push_back(static_cast<char>(key - 'a' + 'A'));
    text.push_back(']');
    return text;
}

// True when an unmodified tool letter is also a menu accelerator. The
// menu table only binds those letters with Ctrl (or Ctrl+Shift).
inline bool tool_hotkey_clashes()
{
    for (int i = 0; i < 17; ++i) {
        const char key = tool_hotkey(i);
        if (key == 0) {
            continue;
        }
        if (menu_table::accel_action(AccelKey::Letter, 0) != Action::None) {
            return true;
        }
        for (int column = 0; column < menu_table::kColumnCount; ++column) {
            const MenuEntry *items = menu_table::column_items(column);
            const int count = menu_table::column_count(column);
            for (int row = 0; row < count; ++row) {
                if (items[row].mods != 0 || items[row].key == AccelKey::None) {
                    continue;
                }
                if (mnemonic_of(items[row].label) == key && items[row].mods == 0 &&
                    items[row].key == AccelKey::Letter) {
                    return true;
                }
            }
        }
    }
    return false;
}
