// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

// Hotspot ids are one enum. Dialog buttons must not reuse the map, toolbar,
// minimap, or menu-title ids. OSK Up/Down moves by row.

#include "hotspots.hpp"
#include "osk_layout.hpp"

#include <cmath>
#include <iostream>
#include <string>

namespace {

int fail(const std::string &message)
{
    std::cerr << message << "\n";
    return 1;
}

} // namespace

int main()
{
    static_assert(kCancel != kMapId, "Cancel must not share the map id");
    static_assert(kOk != kPaletteId, "OK must not share the toolbar id");
    static_assert(kDiscard != kMinimapId, "Discard must not share the minimap id");
    static_assert(kWelcomeNew != kMapId, "Start a New City must not share the map id");
    static_assert(kClose != kMenuTitle, "Close must not share the System menu id");
    static_assert(kCancel > kMenuTitle + 5, "dialog buttons overlap menu titles");
    static_assert(kWelcomeQuit < kMenuItem, "dialog buttons overlap menu items");
    static_assert(kMenuItem + 5 * 40 < kRecentItem, "menu items overlap recent files");
    static_assert(kRecentItem + 32 < kFieldName, "recent files overlap text fields");
    static_assert(kFieldSeed < kOsk && kOsk + 64 < kListItem, "OSK overlaps a neighbour range");
    static_assert(kListItem + 64 < kSlider && kSlider + 8 < kChoice, "list, slider, or choice ranges overlap");
    static_assert(kOverlayMap < kCancel && kOverlayMap != kMenuTitle, "overlay map id is not unique");

    const int ids[] = {
        kMapId,       kPaletteId, kMinimapId,    kMenuTitle,    kHScroll,      kVScroll,     kOverlayMap,  kCancel,
        kOk,          kDiscard,   kSave,         kBack,      kNext,         kGenerate,     kPlay,        kRename,
        kClose,       kReset,     kCollect,      kKeep,      kStay,         kScenarioList, kRecover,     kNotNow,
        kDelete,      kUp,        kSystemFiles,  kShortScale, kLongScale,   kWelcomeNew,   kWelcomeLoad, kWelcomeScenario,
        kWelcomeQuit,
        kMenuItem,    kRecentItem,
        kFieldName,   kFieldSeed, kOsk,          kListItem,  kSlider,       kChoice,
    };
    const int n = static_cast<int>(sizeof(ids) / sizeof(ids[0]));
    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            if (ids[i] == ids[j]) {
                return fail("duplicate hotspot id " + std::to_string(ids[i]));
            }
        }
    }
    if (is_modal_control(kMapId) || is_modal_control(kPaletteId) || is_modal_control(kMinimapId) ||
        is_modal_control(kMenuTitle) || is_modal_control(kMenuTitle + 4) || is_modal_control(kMenuItem)) {
        return fail("a background hotspot is treated as a dialog control");
    }
    if (!is_modal_control(kCancel) || !is_modal_control(kSave) || !is_modal_control(kClose) || !is_modal_control(kOsk) ||
        !is_modal_control(kSlider + 1) || !is_modal_control(kFieldName) || !is_dialog_button(kLongScale) ||
        !is_dialog_button(kWelcomeNew) || !is_dialog_button(kWelcomeQuit)) {
        return fail("dialog controls are not recognised");
    }

    // Synthetic rows: Down from the left key picks the nearer key, not the next index.
    const OskKey fake[] = {
        {"a", nullptr, nullptr, nullptr, OskKind::Text, 0, 1},
        {"b", nullptr, nullptr, nullptr, OskKind::Text, 0, 1},
        {"c", nullptr, nullptr, nullptr, OskKind::Text, 1, 1},
        {"d", nullptr, nullptr, nullptr, OskKind::Text, 1, 1},
    };
    const double fake_center[] = {0, 100, 40, 90};
    const char fake_present[] = {1, 1, 1, 1};
    if (osk_vertical_target(fake, 4, 0, 1, fake_center, fake_present) != 2) {
        return fail("Down from the left key did not pick the nearest key below");
    }
    if (osk_vertical_target(fake, 4, 1, 1, fake_center, fake_present) != 3) {
        return fail("Down from the right key did not pick the nearest key below");
    }
    if (osk_vertical_target(fake, 4, 0, -1, fake_center, fake_present) != -1) {
        return fail("Up from the top row should stay put");
    }

    OskPlacement placed[64];
    const double width = 800;
    const int placed_n = osk_place(16, 40, width, 400, placed, 64);
    int count = 0;
    const OskKey *keys = osk_keys(count);
    if (placed_n != count) {
        return fail("keyboard placement dropped a key");
    }
    double center[64] = {};
    char present[64] = {};
    const OskPlacement *auto_key = nullptr;
    for (int i = 0; i < placed_n; ++i) {
        const OskPlacement &box = placed[i];
        if (box.x + box.w > 16 + width + 0.51) {
            return fail("key " + std::to_string(box.index) + " extends past the field");
        }
        if (box.index < 0 || box.index >= count) {
            return fail("placed key index is out of range");
        }
        center[box.index] = box.x + box.w * 0.5;
        present[box.index] = 1;
        if (keys[box.index].kind == OskKind::Auto) {
            auto_key = &box;
        }
    }
    if (auto_key == nullptr) {
        return fail("keyboard has no auto key");
    }
    const double gap = kOskGap;
    const double unit = (width - gap * 9.0) / 10.0;
    const double auto_w = 2.0 * unit + gap;
    if (std::fabs(auto_key->w - auto_w) > 0.6) {
        return fail("auto key was clipped instead of fitted (" + std::to_string(auto_key->w) + ")");
    }
    if (auto_key->x + auto_key->w > 16 + width + 0.51) {
        return fail("auto key still overflows the field");
    }
    // q is index 0, a is 10, p is 9, l is 18.
    if (osk_vertical_target(keys, count, 0, 1, center, present) != 10) {
        return fail("Down from q should reach a");
    }
    if (osk_vertical_target(keys, count, 9, 1, center, present) != 18) {
        return fail("Down from p should reach l");
    }
    if (osk_vertical_target(keys, count, 18, -1, center, present) != 9) {
        return fail("Up from l should reach p");
    }
    if (osk_vertical_target(keys, count, 0, -1, center, present) != -1 ||
        osk_vertical_target(keys, count, count - 1, 1, center, present) != -1) {
        return fail("vertical move walked off the keyboard");
    }
    const int order[] = {6001, 6100, 6101, 6102, 105};
    if (osk_exit_focus(order, 5, 6101, 1, 6100, 6100 + 64) != 105 ||
        osk_exit_focus(order, 5, 6101, -1, 6100, 6100 + 64) != 6001 ||
        osk_exit_focus(order, 5, 6102, 1, 6100, 6100 + 64) != 105) {
        return fail("Down on the bottom keyboard row walked sideways");
    }
    return 0;
}
