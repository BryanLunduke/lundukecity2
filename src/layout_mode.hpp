// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
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
// ("Jupiter") and OLED ("Galileo"), ROG Ally / Ally X and ROG Xbox Ally,
// Legion Go (83E1), Legion Go S (83L3, 83N6, 83Q2, 83Q3) and Legion Go 2
// (83N0, 83N1), MSI Claw ("Claw A1M", "Claw 7 AI+ ...", "Claw 8 AI+ ..."),
// AYANEO, OneXPlayer, ZOTAC Zone, and GPD Win 4 / Win Mini (G1618, G1617).
// Names are from public DMI dumps, not checked on every device; SteamDeck=1
// or Settings > Layout still force the handheld layout on anything else.
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
    if (name == "Jupiter" || name == "Galileo") {
        return true;
    }
    static const char *prefixes[] = {
        "ROG Ally", "ROG Xbox Ally", "83E1", "83L3", "83N6", "83Q2", "83Q3", "83N0", "83N1", "Claw ", "Claw A1M",
        "AYANEO",   "AYA NEO",       "ONEXPLAYER", "ONEXFLY", "ZOTAC GAMING ZONE", "G1618-", "G1617-",
    };
    for (const char *prefix : prefixes) {
        if (name.rfind(prefix, 0) == 0) {
            return true;
        }
    }
    return false;
}

} // namespace layout_mode
