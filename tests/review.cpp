// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#include "assets.hpp"
#include "dialog_layout.hpp"
#include "file_list.hpp"
#include "text_edit.hpp"
#include "list_scroll.hpp"
#include "redraw.hpp"
#include "sound_player.hpp"
#include "ui_scale.hpp"
#include "window_fit.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace {

int fail(const std::string &message)
{
    std::cerr << message << "\n";
    return 1;
}

} // namespace

int main()
{
    // C1: a selection past the first screen scrolls into view, and the
    // offset never walks off the end of a short list.
    int scroll = 0;
    list_reveal(scroll, 30, 10, 40);
    if (scroll != 21) {
        return fail("selecting row 30 of 40 with 10 visible should scroll to 21, got " + std::to_string(scroll));
    }
    list_reveal(scroll, 0, 10, 40);
    if (scroll != 0) {
        return fail("selecting the first row should scroll back to 0");
    }
    list_reveal(scroll, 3, 10, 40);
    if (scroll != 0) {
        return fail("a row already on screen should not scroll");
    }
    scroll = 100;
    list_reveal(scroll, 2, 15, 8);
    if (scroll != 0) {
        return fail("a list shorter than the window should pin scroll at 0, got " + std::to_string(scroll));
    }
    if (list_max_scroll(40, 10) != 30 || list_max_scroll(4, 10) != 0) {
        return fail("max scroll");
    }
    if (list_index_from_track(0, 0, 100, 40) != 0 || list_index_from_track(100, 0, 100, 40) != 39) {
        return fail("track ends should map to the first and last row");
    }
    if (list_index_from_track(50, 0, 100, 11) != 5) {
        return fail("mid track should map to the middle row");
    }
    double ty = 0;
    double th = 0;
    list_thumb(21, 40, 10, 10, 200, ty, th);
    if (th >= 200.0 || ty <= 10.0) {
        return fail("thumb should sit below the top of a scrolled track");
    }
    list_thumb(0, 4, 10, 10, 200, ty, th);
    if (ty != 10.0 || th != 200.0) {
        return fail("a short list fills the track");
    }
    // H4: packaged search paths are absolute install locations, never the
    // process's current directory. A relative third_party/ fallback used to
    // load whatever tree happened to be in the launch folder.
    const std::vector<std::string> candidates = asset_candidates();
    for (const std::string &candidate : candidates) {
        if (candidate == "third_party/micropolis-assets" || candidate == "third_party\\micropolis-assets") {
            return fail("asset search still probes the current directory");
        }
    }
    const std::string missing = asset_missing_text(candidates.empty() ? std::vector<std::string>{"/nowhere/tiles"} : candidates);
    if (missing.find("tiles.png") == std::string::npos || missing.find("Looked in:") == std::string::npos) {
        return fail("missing-asset text does not name the file and the paths");
    }
    if (missing.find("/nowhere/tiles") == std::string::npos && candidates.empty()) {
        return fail("missing-asset text dropped the searched path");
    }
    // H5: scale follows the display unless the user set one, and the default
    // window fits the work area.
    if (std::abs(ui_scale::resolve_ui_scale(2.0f, 0.0f) - 2.0f) > 0.001f) {
        return fail("display scale was ignored");
    }
    if (std::abs(ui_scale::resolve_ui_scale(2.0f, 1.5f) - 1.5f) > 0.001f) {
        return fail("user scale did not override the display");
    }
    if (std::abs(ui_scale::resolve_ui_scale(0.0f, 0.0f) - 1.0f) > 0.001f) {
        return fail("a missing display scale should fall back to 1");
    }
    if (ui_scale::scaled_font("Sans", 13, 1.0f) != "Sans 13" || ui_scale::scaled_font("Sans Bold", 18, 2.0f) != "Sans Bold 36") {
        return fail("scaled font string");
    }
    const ui_scale::Placement laptop = ui_scale::fit_default_window(1280, 800, 0, 0, 1366, 768);
    if (laptop.w + 2 * window_fit::kFrameSide > 1366 || laptop.h + window_fit::kFrameTop + window_fit::kFrameBottom > 768) {
        return fail("1366x768 window is taller than the work area");
    }
    if (laptop.y < 0 || laptop.x < 0) {
        return fail("fitted window origin is negative");
    }
    const ui_scale::Placement wide = ui_scale::fit_default_window(1280, 800, 0, 0, 1920, 1080);
    if (wide.w <= 1280 || wide.h < 800 || wide.w + 2 * window_fit::kFrameSide > 1920) {
        return fail("1080p window was not sized from the display");
    }
    const ui_scale::Placement pulled = ui_scale::clamp_saved_window(-400, 9000, 2000, 2000, 0, 0, 1366, 768);
    if (pulled.x < 0 || pulled.y < 0 || pulled.w > 1366 || pulled.h + window_fit::kFrameTop + window_fit::kFrameBottom > 768) {
        return fail("saved window was not brought back on screen");
    }
    const std::string text = ui_scale::settings_text([] {
        ui_scale::StoredSettings settings;
        settings.has_geometry = true;
        settings.x = 40;
        settings.y = 50;
        settings.w = 1280;
        settings.h = 800;
        settings.maximized = true;
        settings.user_scale = 1.5f;
        return settings;
    }());
    const ui_scale::StoredSettings parsed = ui_scale::settings_parse(text + "future_key=yes\n");
    if (!parsed.has_geometry || parsed.x != 40 || parsed.y != 50 || parsed.w != 1280 || parsed.h != 800 || !parsed.maximized ||
        std::abs(parsed.user_scale - 1.5f) > 0.001f) {
        return fail("settings did not round-trip");
    }
    const DialogRect budget = place_dialog(1280, 800, 640, 470, 16);
    if (budget.w > 640.5 || budget.h > 470.5 || budget.x < 0 || budget.y < 0 || budget.x + budget.w > 1280 ||
        budget.y + budget.h > 800) {
        return fail("budget dialog does not fit 1280x800");
    }
    const DialogRect deck = place_dialog(1024, 640, 700, 420, 16);
    if (deck.w > 1024 - 32 || deck.h > 640 - 32 || deck.y + deck.h > 640) {
        return fail("evaluation dialog does not fit 1024x640");
    }
    if (std::abs(message_width(1920, 16, 420, 560) - 560) > 0.1 || std::abs(message_width(800, 16, 420, 560) - 560) > 0.1 ||
        std::abs(message_width(400, 16, 420, 560) - 368) > 0.1) {
        return fail("message width was not clamped");
    }
    if (std::abs(button_width(40, 32, 120) - 120) > 0.1 || std::abs(button_width(200, 32, 120) - 232) > 0.1) {
        return fail("button width");
    }
    std::vector<std::string> rows = {"city10.cty", "Notes/", "city2.cty", "a.cty", "..", "Zoo/"};
    std::sort(rows.begin(), rows.end(), file_row_less);
    if (rows.size() != 6 || rows[0] != ".." || rows[1] != "Notes/" || rows[2] != "Zoo/" || rows[3] != "a.cty" ||
        rows[4] != "city2.cty" || rows[5] != "city10.cty") {
        return fail("file list did not put .. first, then folders, then natural names");
    }
    if (!file_name_hidden(".git") || file_name_hidden("city.cty")) {
        return fail("dotfile filter");
    }
    std::string word = "caf\xc3\xa9";
    int caret = static_cast<int>(word.size());
    int anchor = caret;
    field_backspace(word, caret, anchor);
    if (word != "caf" || caret != 3) {
        return fail("backspace removed a byte of a UTF-8 character");
    }
    field_insert(word, caret, anchor, "\xc3\xa9s", 64);
    field_select_all(word, caret, anchor);
    if (field_insert(word, caret, anchor, std::string(80, 'x'), 64) || word != "caf\xc3\xa9s") {
        return fail("an over-long insert erased the selection");
    }
    field_insert(word, caret, anchor, "Z", 64);
    if (word != "Z" || caret != 1) {
        return fail("typing did not replace the selection");
    }
    if (audio_queue_limit_bytes(22050, 1, 2, 50) != 2205 || audio_bytes_to_write(0, 2205, 80000) != 2205 ||
        audio_bytes_to_write(2205, 2205, 800) != 0 || audio_bytes_to_write(2000, 2205, 800) != 205) {
        return fail("audio streams are not paced to 50 ms");
    }
    if (!text_has_path_separator("a/b") || text_has_path_separator("ab")) {
        return fail("path separator check");
    }
    if (default_cities_dir().find("Lunduke City 2") == std::string::npos) {
        return fail("default city folder is not under Documents");
    }
    const DialogRect popped = place_near(1280, 800, 480, 160, 1200, 700, 16);
    if (popped.x < 16 || popped.y < 16 || popped.x + popped.w > 1280 - 16 || popped.y + popped.h > 800 - 16) {
        return fail("query popup escaped the window");
    }
    redraw::Reasons idle;
    if (redraw::needed(idle)) {
        return fail("idle frame should not redraw");
    }
    redraw::Reasons running;
    running.sim_running = true;
    redraw::Reasons blink;
    blink.blink = true;
    if (!redraw::needed(running) || !redraw::needed(blink)) {
        return fail("simulation and blink frames should redraw");
    }
    const ui_scale::StoredSettings unknown = ui_scale::settings_parse("not_a_window=1\nui_scale=0\n");
    if (unknown.has_geometry || unknown.user_scale != 0.0f) {
        return fail("unknown settings keys were not ignored");
    }
    std::cout << "review ok\n";
    return 0;
}
