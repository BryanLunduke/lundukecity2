// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#pragma once

#include <string>

// Which window layout to use. The handheld layout (full-screen map, quick
// bar, touch buttons) is for a Steam Deck or another handheld PC. A desktop
// with a keyboard and mouse keeps the toolbar layout unless the player
// picks Handheld in Settings.
namespace layout_mode {

enum Choice { Auto = 0, Desktop = 1, Handheld = 2 };

inline int clamp_choice(int value)
{
    return value < Auto || value > Handheld ? Auto : value;
}

inline const char *choice_label(int value)
{
    switch (clamp_choice(value)) {
    case Desktop:
        return "Desktop";
    case Handheld:
        return "Handheld";
    default:
        return "Auto";
    }
}

// Auto follows the device: SteamDeck=1 or a known handheld board.
inline bool use_handheld(int choice, bool deck, bool handheld_device)
{
    switch (clamp_choice(choice)) {
    case Desktop:
        return false;
    case Handheld:
        return true;
    default:
        return deck || handheld_device;
    }
}

// DMI product names of handheld PCs with a built-in pad: Steam Deck LCD
// ("Jupiter") and OLED ("Galileo"), ROG Ally, Legion Go.
inline bool handheld_product(const std::string &product)
{
    std::string name;
    for (char ch : product) {
        if (ch != '\n' && ch != '\r') {
            name.push_back(ch);
        }
    }
    while (!name.empty() && name.back() == ' ') {
        name.pop_back();
    }
    return name == "Jupiter" || name == "Galileo" || name.rfind("ROG Ally", 0) == 0 || name.rfind("83E1", 0) == 0;
}

} // namespace layout_mode
