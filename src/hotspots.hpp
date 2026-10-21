// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#pragma once

// One id space for every clickable region. Dialog buttons used to reuse the
// map, toolbar, minimap, and System-menu ids, so a hover or focus on the
// city lit the matching button.

enum HotspotId : int {
    kHotNone = 0,

    kMapId = 1,
    kPaletteId = 2,
    kMinimapId = 3,

    // Menu-bar titles occupy kMenuTitle + column (columns 0..4).
    kMenuTitle = 10,

    kHScroll = 50,
    kVScroll = 51,
    // Scrollbar for the in-window file list (and the scenario list).
    kListScroll = 52,

    // The map inside a power / water / crime overlay dialog.
    kOverlayMap = 90,

    // Dialog buttons. Kept in one contiguous block, clear of every screen
    // region and of the menu-title range above.
    kCancel = 100,
    kOk = 101,
    kDiscard = 102,
    kSave = 103,
    kBack = 104,
    kNext = 105,
    kGenerate = 106,
    kPlay = 107,
    kRename = 108,
    kClose = 109,
    kReset = 110,
    kCollect = 111,
    kKeep = 112,
    kStay = 113,
    kScenarioList = 114,
    kRecover = 115,
    kNotNow = 116,
    kDelete = 117,
    kUp = 118,
    kSystemFiles = 119,
    kShortScale = 120,
    kLongScale = 121,
    // Launch screen. B / Escape asks before quitting. These start a city,
    // and Quit opens that confirmation.
    kWelcomeNew = 122,
    kWelcomeLoad = 123,
    kWelcomeScenario = 124,
    kWelcomeQuit = 125,

    kMenuItem = 1000,
    kRecentItem = 2000,
    kFieldName = 6001,
    kFieldSeed = 6002,
    kOsk = 6100,
    kListItem = 7000,
    kSlider = 8000,
    kChoice = 8300,
};

inline bool is_dialog_button(int id)
{
    return id >= kCancel && id <= kWelcomeQuit;
}

// A control that belongs to the open dialog. Everything else is part of the
// city behind the modal and must not take focus or a highlight.
inline bool is_modal_control(int id)
{
    if (is_dialog_button(id)) {
        return true;
    }
    if (id == kFieldName || id == kFieldSeed || id == kOverlayMap || id == kListScroll) {
        return true;
    }
    if (id >= kOsk && id < kOsk + 64) {
        return true;
    }
    if (id >= kListItem && id < kListItem + 64) {
        return true;
    }
    if (id >= kSlider && id < kSlider + 8) {
        return true;
    }
    if (id >= kChoice && id < kChoice + 50) {
        return true;
    }
    return false;
}
