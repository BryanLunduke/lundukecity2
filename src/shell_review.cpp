// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#include "shell_state.hpp"

#include "city_tail.hpp"
#include "osk_layout.hpp"
#include "dialog_layout.hpp"
#include "file_list.hpp"
#include "text_edit.hpp"
#include "list_scroll.hpp"
#include "redraw.hpp"
#include "text_paint.hpp"
#include "tile_atlas.hpp"
#include "version.hpp"
#include "ui_scale.hpp"

#include <csignal>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

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

    // H2: Save from the unsaved prompt on a city with no file must keep the
    // action that was waiting (Quit, New City, Load, ...).
    after_save_ = AfterSave::Quit;
    apply_action(Action::Save);
    if (after_save_ != AfterSave::Quit || (dialog_ != Dialog::Files && dialog_ != Dialog::FileWait)) {
        fail("save-as from the quit prompt dropped the pending quit");
        return;
    }
    after_save_ = AfterSave::Nothing;
    close_dialog();

    // H3: a failed save from the quit prompt must not make the next save quit.
    after_save_ = AfterSave::Quit;
    save_generation_ = city_generation_;
    finish_save("/no/such/lunduke-city-dir/city.cty");
    if (after_save_ != AfterSave::Nothing || dialog_ != Dialog::SaveError || quitting_) {
        fail("failed quit-save left the game armed to quit");
        return;
    }
    on_dialog_button(kOk);
    if (quitting_ || after_save_ != AfterSave::Nothing) {
        fail("dismissing the save error quit the game");
        return;
    }

    // H4: a missing tile sheet is reported once, not every frame.
    const int attempts = tile_atlas().load_attempts();
    tile_atlas().ensure_loaded();
    tile_atlas().ensure_loaded();
    if (tile_atlas().load_attempts() != attempts) {
        fail("map tiles were opened more than once");
        return;
    }
    if (!tile_atlas().loaded() && tile_atlas().load_error().find("tiles.png") == std::string::npos) {
        fail("missing tiles did not name tiles.png");
        return;
    }

    // The look toggle repaints the map from the other sheet and round-trips
    // through the settings text. Classic pixels come back unchanged.
    {
        unsigned char classic_px[16 * 16 * 4];
        tile_atlas().blit(0, classic_px, 16 * 4);
        map_.sync();
        const int before_blits = map_.last_sync_blits();
        if (!tile_atlas().svga_available() || !tile_atlas().set_look(true)) {
            fail("SVGA tiles did not load");
            return;
        }
        drawn_look_ = 1;
        unsigned char svga_px[16 * 16 * 4];
        tile_atlas().blit(0, svga_px, 16 * 4);
        if (std::memcmp(classic_px, svga_px, sizeof(classic_px)) == 0) {
            fail("SVGA look did not change the ground tile");
            return;
        }
        map_.invalidate_all();
        map_.sync();
        if (map_.last_sync_blits() < 1 || map_.last_sync_blits() < before_blits) {
            fail("switching look did not redraw the map");
            return;
        }
        minimap_.sync();
        ui_scale::StoredSettings saved;
        saved.look = 1;
        const ui_scale::StoredSettings restored = ui_scale::settings_parse(ui_scale::settings_text(saved));
        if (restored.look != 1) {
            fail("SVGA look did not persist");
            return;
        }
        const ui_scale::StoredSettings absent = ui_scale::settings_parse("ui_scale=1\n");
        if (absent.look != -1) {
            fail("a settings file without look should stay unset");
            return;
        }
        tile_atlas().set_look(0);
        drawn_look_ = 0;
        unsigned char back[16 * 16 * 4];
        tile_atlas().blit(0, back, 16 * 4);
        if (std::memcmp(classic_px, back, sizeof(classic_px)) != 0) {
            fail("Classic look did not restore the ground tile");
            return;
        }
        map_.invalidate_all();
        map_.sync();
    }

    // A missing SVGA sheet is Classic on screen. Picking SVGA, or quitting,
    // must not write that fallback over a stored SVGA choice.
    {
        if (!tile_atlas().svga_available()) {
            fail("SVGA sheet was already missing");
            return;
        }
        tile_atlas().forget_svga();
        const int stored_look = loaded_.look;
        loaded_.look = 1;
        set_look_preference(loaded_.look == 1 ? Look::Svga : Look::Classic);
        if (drawn_look_ != 0 || tile_atlas().use_svga() || look_pref_.setting() != 1) {
            fail("loading SVGA with no sheet stored Classic");
            return;
        }
        apply_action(Action::SvgaLook);
        if (drawn_look_ != 0 || tile_atlas().use_svga() || look_pref_.setting() != 1) {
            fail("picking SVGA with no sheet stored Classic");
            return;
        }
        apply_action(Action::ClassicLook);
        if (drawn_look_ != 0 || tile_atlas().use_svga() || look_pref_.setting() != 0) {
            fail("picking Classic did not store Classic");
            return;
        }
        set_look_preference(Look::Svga);
        if (drawn_look_ != 0 || look_pref_.setting() != 1) {
            fail("quitting would replace a stored SVGA choice with Classic");
            return;
        }
        if (!tile_atlas().recall_svga() || !tile_atlas().svga_available()) {
            fail("SVGA sheet could not be restored");
            return;
        }
        set_look_preference(Look::Classic);
        loaded_.look = stored_look;
        if (drawn_look_ != 0 || tile_atlas().use_svga()) {
            fail("Classic did not come back after the missing-sheet check");
            return;
        }
    }

    // Spooky changes the park tile and leaves the rest of the SVGA sheet
    // alone. A missing Spooky sheet is Classic on screen. The stored choice
    // stays Spooky, the same way a missing SVGA sheet stays stored as SVGA.
    {
        if (!tile_atlas().spooky_available() || !tile_atlas().set_look(1)) {
            fail("SVGA sheet was not ready for the Spooky compare");
            return;
        }
        unsigned char svga_park[16 * 16 * 4];
        unsigned char svga_dirt[16 * 16 * 4];
        tile_atlas().blit(40, svga_park, 16 * 4);
        tile_atlas().blit(0, svga_dirt, 16 * 4);
        if (!tile_atlas().set_look(2) || !tile_atlas().use_spooky()) {
            fail("Spooky tiles did not load");
            return;
        }
        drawn_look_ = 2;
        unsigned char spooky_park[16 * 16 * 4];
        unsigned char spooky_dirt[16 * 16 * 4];
        tile_atlas().blit(40, spooky_park, 16 * 4);
        tile_atlas().blit(0, spooky_dirt, 16 * 4);
        if (std::memcmp(svga_park, spooky_park, sizeof(svga_park)) == 0) {
            fail("Spooky look did not change the park tile");
            return;
        }
        if (std::memcmp(svga_dirt, spooky_dirt, sizeof(svga_dirt)) != 0) {
            fail("Spooky look changed a tile that should match SVGA");
            return;
        }
        map_.invalidate_all();
        map_.sync();
        minimap_.sync();
        ui_scale::StoredSettings saved;
        saved.look = 2;
        const ui_scale::StoredSettings restored = ui_scale::settings_parse(ui_scale::settings_text(saved));
        if (restored.look != 2) {
            fail("Spooky look did not persist");
            return;
        }
        tile_atlas().forget_spooky();
        set_look_preference(Look::Spooky);
        if (drawn_look_ != 0 || tile_atlas().use_spooky() || look_pref_.setting() != 2) {
            fail("a missing Spooky sheet stored Classic");
            return;
        }
        apply_action(Action::SpookyLook);
        if (drawn_look_ != 0 || tile_atlas().use_spooky() || look_pref_.setting() != 2) {
            fail("picking Spooky with no sheet stored Classic");
            return;
        }
        if (!tile_atlas().recall_spooky() || !tile_atlas().spooky_available()) {
            fail("Spooky sheet could not be restored");
            return;
        }
        set_look_preference(Look::Classic);
        if (drawn_look_ != 0 || tile_atlas().use_svga() || tile_atlas().use_spooky()) {
            fail("Classic did not come back after the Spooky check");
            return;
        }
    }

    // H5: headless play stays at the classic size and a 1x scale. A saved
    // window must not be written from the test, and a short laptop work area
    // must shrink the default window.
    if (std::abs(ui_scale_ - 1.0f) > 0.01f || font_ui_ != "Sans 13") {
        fail("headless UI scale should stay 1, got " + font_ui_);
        return;
    }
    if (win_w_ != 1280 || win_h_ != 800) {
        fail("headless window should stay 1280x800");
        return;
    }
    if (allow_saved_settings()) {
        fail("review run would persist window settings");
        return;
    }
    const ui_scale::Placement laptop = ui_scale::fit_default_window(1280, 800, 0, 0, 1366, 768);
    if (laptop.h + window_fit::kFrameTop + window_fit::kFrameBottom > 768 || laptop.w + 2 * window_fit::kFrameSide > 1366) {
        fail("default window does not fit a 1366x768 work area");
        return;
    }
    const ui_scale::Placement desktop = ui_scale::fit_default_window(1280, 800, 0, 0, 1920, 1080);
    if (desktop.w < 1280 || desktop.h < 800) {
        fail("a 1080p display should not shrink the classic window");
        return;
    }

    // H6: a paused city reuses its frame and does not repaint when nothing changed.
    if (skipped_redraws_ < 1) {
        fail("paused city repainted every frame (" + std::to_string(redraws_) + " draws)");
        return;
    }
    if (surface_builds_ != 1) {
        fail("cairo surface was rebuilt for an unchanged window");
        return;
    }
    if (text_cache_entries() < 1) {
        fail("text layouts were not cached");
        return;
    }
    redraw::Reasons idle;
    if (redraw::needed(idle)) {
        fail("an idle frame asked for a redraw");
        return;
    }
    redraw::Reasons running;
    running.sim_running = true;
    if (!redraw::needed(running)) {
        fail("a running simulation did not ask for a redraw");
        return;
    }
    redraw::Reasons exposed;
    exposed.exposed = true;
    if (!redraw::needed(exposed)) {
        fail("an exposed window did not ask for a redraw");
        return;
    }

    // M1: content-sized dialogs stay inside a Steam Deck and a short laptop window.
    dialog_ = Dialog::NewCity;
    new_page_ = 0;
    focus_id_ = kFieldName;
    field_a_ = "caf\xc3\xa9";
    field_caret_ = static_cast<int>(field_a_.size());
    field_anchor_ = field_caret_;
    backspace_field();
    if (field_a_ != "caf") {
        fail("field backspace split a UTF-8 character");
        return;
    }
    type_text("/");
    if (field_a_ != "caf" || dialog_error_.find("/") == std::string::npos) {
        fail("a slash was accepted in a city name");
        return;
    }
    dialog_error_.clear();
    close_dialog();
    files_.clear();
    files_.push_back("..");
    files_.push_back(".git/");
    files_.push_back("city10.cty");
    std::sort(files_.begin(), files_.end(), file_row_less);
    if (files_.empty() || files_[0] != ".." || file_name_hidden("city.cty")) {
        fail("file list lost the parent row");
        return;
    }
    // M4: Replace-file Cancel keeps the typed name and the folder being browsed.
    field_a_ = "Other";
    files_dir_ = "/tmp";
    after_save_ = AfterSave::Quit;
    dialog_ = Dialog::Overwrite;
    on_dialog_button(kCancel);
    if (field_a_ != "Other" || dialog_ != Dialog::Files || files_dir_ != "/tmp" || after_save_ != AfterSave::Quit) {
        fail("replace-file cancel dropped the typed name");
        return;
    }
    close_dialog();
    after_save_ = AfterSave::Nothing;

    // M5: unplugging clears stick drift, adopts another pad, then pauses.
    if (!attach_test_pad()) {
        return;
    }
    SDL_VirtualJoystickDesc spare;
    SDL_INIT_INTERFACE(&spare);
    spare.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    spare.naxes = SDL_GAMEPAD_AXIS_COUNT;
    spare.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    spare.name = "Lunduke City 2 Spare";
    const SDL_JoystickID spare_id = SDL_AttachVirtualJoystick(&spare);
    const SDL_JoystickID first_id = SDL_GetGamepadID(pad_);
    stick_x_ = 0.9f;
    pan_y_ = -0.6f;
    trig_r_ = 0.5f;
    pad_dragging_ = true;
    speed_ = 2;
    forget_gamepad(first_id);
    if (spare_id == 0 || pad_ == nullptr || SDL_GetGamepadID(pad_) != spare_id || stick_x_ != 0.0f || pan_y_ != 0.0f ||
        trig_r_ != 0.0f || pad_dragging_ || speed_ != 2) {
        fail("unplug did not adopt the spare controller");
        return;
    }
    SDL_DetachVirtualJoystick(first_id);
    forget_gamepad(spare_id);
    if (pad_ != nullptr || speed_ != 0 || stick_x_ != 0.0f || shown_message_ != "Controller disconnected") {
        fail("the last controller unplug did not pause");
        return;
    }
    SDL_DetachVirtualJoystick(spare_id);

    // M7: a closer zoom is kept, the 16px zoom still covers, and the 8px
    // overview is allowed to letterbox so the whole city stays on screen.
    map_.set_tile_size(kMaxMapTile);
    layout();
    if (map_.tile_size() != kMaxMapTile) {
        fail("fitting the map shrank a closer zoom");
        return;
    }
    map_.set_tile_size(kMapTileSource);
    layout();
    if (map_.pixel_width() < view_w_ && map_.tile_size() < kMaxMapTile) {
        fail("the map left an empty band beside the city");
        return;
    }
    map_.set_tile_size(kMapTileOverview);
    layout();
    if (map_.tile_size() != kMapTileOverview || map_.pixel_width() != CitySession::kWorldW * kMapTileOverview) {
        fail("the overview zoom was snapped away from 8px");
        return;
    }
    if (demand_h_ > dp(kMaxDemandPx)) {
        fail("demand gauges stretched with the window");
        return;
    }

    // M8: scenarios past the first screen are reachable.
    dialog_ = Dialog::Scenario;
    scenario_index_ = 0;
    list_index_ = 0;
    list_scroll_ = 0;
    files_visible_ = 3;
    focus_id_ = kListItem;
    if (!nudge_file_list(1, true, true) || scenario_index_ != 2) {
        fail("scenario list did not page");
        return;
    }
    nudge_file_list(20, false, true);
    if (scenario_index_ != CitySession::kScenarioCount - 1 || scenario_index_ < list_scroll_ ||
        scenario_index_ >= list_scroll_ + files_visible_) {
        fail("the last scenario is not on screen");
        return;
    }
    close_dialog();

    // M9: settings cancel restores volume, and a key clears gamepad glyphs.
    volume_ = 40;
    open_volume_ = 80;
    scale_percent_ = 0;
    open_scale_percent_ = 0;
    dialog_ = Dialog::Settings;
    on_dialog_button(kCancel);
    if (volume_ != 80 || dialog_ != Dialog::None) {
        fail("settings cancel did not restore the volume");
        return;
    }
    const int tax_before = session_.tax();
    session_.set_tax(tax_before == 20 ? 19 : 20);
    recovery_.path = "/no/such/lunduke-recovery.cty";
    dialog_ = Dialog::Recovery;
    on_dialog_button(kRecover);
    if (!session_.needs_save_prompt() || dialog_ != Dialog::Unsaved || after_save_ != AfterSave::Recover) {
        fail("recover replaced an unsaved city without asking");
        return;
    }
    close_dialog();
    after_save_ = AfterSave::Nothing;
    session_.set_tax(tax_before);
    session_.accept_preferences();

    const int zoom_before = map_.tile_size();
    dialog_ = Dialog::About;
    SDL_SetModState(SDL_KMOD_CTRL);
    SDL_Event wheel{};
    wheel.type = SDL_EVENT_MOUSE_WHEEL;
    wheel.wheel.y = 1;
    handle_event(wheel);
    SDL_SetModState(SDL_KMOD_NONE);
    if (map_.tile_size() != zoom_before) {
        fail("ctrl+wheel zoomed behind a dialog");
        return;
    }
    close_dialog();

    dialog_ = Dialog::LoadError;
    sync_title();
    const char *title = SDL_GetWindowTitle(window_);
    if (title == nullptr || std::string(title).find(kProductName) == std::string::npos) {
        fail("the load error dropped the product name from the title");
        return;
    }
    dialog_ = Dialog::None;
    sync_title();

    gamepad_nav_ = true;
    note_keyboard_input();
    if (gamepad_nav_) {
        fail("keyboard input left gamepad hints up");
        return;
    }

    dialog_ = Dialog::None;
    menu_.open_menu = 0;
    menu_.cursor = 2;
    menu_.recent_open = true;
    recent_.clear();
    recent_.push_back(std::string("/home/player/Cities/") + std::string(48, 'W') + ".cty");
    menu_.recent_count = static_cast<int>(recent_.size());
    render();
    const Hotspot *recent_row = nullptr;
    const Hotspot *flyout = nullptr;
    for (const Hotspot &spot : hot_) {
        if (spot.id == kMenuItem + 2) {
            recent_row = &spot;
        }
        if (spot.id == kRecentItem) {
            flyout = &spot;
        }
    }
    // The row hotspot is the menu line. The flyout's first city sits on that
    // line, inset by the panel's 4px padding, and grows past the old 340px cap.
    if (recent_row == nullptr || flyout == nullptr || std::abs(flyout->y - (recent_row->y + 4.0)) > 1.0 ||
        flyout->w < 360 || flyout->w > 1100 || flyout->x < 40) {
        fail("the recent submenu did not follow its row or widen for a long name");
        return;
    }
    menu_.close();
    recent_.clear();

    const auto saved_city = std::filesystem::temp_directory_path() / "lunduke-city2-l6.cty";
    std::filesystem::remove(saved_city);
    if (!session_.save_city_as(saved_city.string()) ||
        !session_.load_city(saved_city.string()) || session_.message() != "Loaded a saved city.") {
        fail("a complete save did not load cleanly");
        return;
    }
    std::ifstream saved_in(saved_city, std::ios::binary);
    std::vector<char> saved_bytes((std::istreambuf_iterator<char>(saved_in)), std::istreambuf_iterator<char>());
    saved_in.close();
    if (saved_bytes.size() <= 27120 || classify_city_path(saved_city.string()) != CityFileKind::Complete) {
        fail("a City 2 save was not recognised as complete");
        return;
    }
    {
        std::ofstream trimmed(saved_city, std::ios::binary | std::ios::trunc);
        trimmed.write(saved_bytes.data(), static_cast<std::streamsize>(saved_bytes.size() - 10));
    }
    if (!session_.load_city(saved_city.string()) || session_.message() == "Loaded a saved city." ||
        session_.message().find("missing") == std::string::npos) {
        fail("a short City 2 tail loaded without a warning");
        return;
    }
    {
        std::ofstream classic(saved_city, std::ios::binary | std::ios::trunc);
        classic.write(saved_bytes.data(), 27120);
    }
    if (!session_.load_city(saved_city.string()) || session_.message() != "Loaded a saved city.") {
        fail("a classic 27120-byte city warned about a missing tail");
        return;
    }
    {
        std::ofstream blank(saved_city, std::ios::binary | std::ios::trunc);
        const std::vector<char> zeros(27120, 0);
        blank.write(zeros.data(), static_cast<std::streamsize>(zeros.size()));
    }
    if (session_.load_city(saved_city.string()) || session_.message().find("Loaded a saved city.") != std::string::npos) {
        fail("an all-zero file loaded as a city");
        return;
    }
    std::filesystem::remove(saved_city);

    // M1: wrapped lines keep their measured height, so headings, fund labels,
    // and the Close button do not share pixels. Checked at 1x and at a scale
    // that wraps the evaluation sentences.
    {
        auto boxes_overlap = [](const TextSpan &a, double bx, double by, double bw, double bh) {
            return a.x < bx + bw - 0.5 && bx < a.x + a.w - 0.5 && a.y < by + bh - 0.5 && by < a.y + a.h - 0.5;
        };
        auto spans_clash = [&]() {
            for (std::size_t i = 0; i < text_spans_.size(); ++i) {
                for (std::size_t j = i + 1; j < text_spans_.size(); ++j) {
                    const TextSpan &b = text_spans_[j];
                    if (boxes_overlap(text_spans_[i], b.x, b.y, b.w, b.h)) {
                        return true;
                    }
                }
            }
            return false;
        };
        auto button_clash = [&]() {
            for (const Hotspot &hot : hot_) {
                const bool button = hot.id == kClose || hot.id == kOk || hot.id == kCancel || hot.id == kCollect;
                if (!button) {
                    continue;
                }
                for (const TextSpan &span : text_spans_) {
                    if (boxes_overlap(span, hot.x, hot.y, hot.w, hot.h)) {
                        return true;
                    }
                }
            }
            return false;
        };
        auto bar_clash = [&]() {
            for (const SliderGeom &slider : sliders_) {
                const double bar_y = slider.y + dp(6);
                const double bar_h = static_cast<double>(dp(16));
                for (const TextSpan &span : text_spans_) {
                    if (boxes_overlap(span, slider.x, bar_y, slider.w, bar_h)) {
                        return true;
                    }
                }
            }
            return false;
        };
        auto apply_scale = [&](float scale) {
            ui_scale_ = scale;
            font_ui_ = ui_scale::scaled_font("Sans", 13, ui_scale_);
            font_body_ = ui_scale::scaled_font("Sans", 14, ui_scale_);
            font_title_ = ui_scale::scaled_font("Sans Bold", 18, ui_scale_);
            font_menu_ = ui_scale::scaled_font("Sans", 13, ui_scale_);
            font_bold_ = ui_scale::scaled_font("Sans Bold", 13, ui_scale_);
        };
        const Dialog checks[] = {Dialog::Evaluation, Dialog::Budget, Dialog::About};
        const char *names[] = {"evaluation", "budget", "about"};
        const float scales[] = {1.0f, 1.77f};
        for (float scale : scales) {
            apply_scale(scale);
            for (int i = 0; i < 3; ++i) {
                dialog_ = checks[i];
                presented_ = false;
                render();
                if (text_spans_.empty()) {
                    fail(std::string(names[i]) + " recorded no text");
                    return;
                }
                if (spans_clash()) {
                    fail(std::string(names[i]) + " text overlaps at scale " + std::to_string(scale));
                    return;
                }
                if (button_clash()) {
                    fail(std::string(names[i]) + " text overlaps a button at scale " + std::to_string(scale));
                    return;
                }
                if (bar_clash()) {
                    fail(std::string(names[i]) + " text overlaps a slider at scale " + std::to_string(scale));
                    return;
                }
            }
        }
        apply_scale(1.0f);
        dialog_ = Dialog::None;
        presented_ = false;
    }

    // Enter opens the highlighted folder or file in the in-window browser.
    {
        const auto root = std::filesystem::temp_directory_path() / "lunduke-city2-enter";
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root / "towns");
        {
            std::ofstream city(root / "Alpha.cty", std::ios::binary);
            city << "not-a-city";
        }
        files_save_ = false;
        files_dir_ = root.string();
        refresh_files();
        dialog_ = Dialog::Files;
        int folder = -1;
        int file = -1;
        for (int i = 0; i < static_cast<int>(files_.size()); ++i) {
            if (files_[static_cast<std::size_t>(i)] == "towns/") {
                folder = i;
            }
            if (files_[static_cast<std::size_t>(i)] == "Alpha.cty") {
                file = i;
            }
        }
        if (folder < 0 || file < 0) {
            std::filesystem::remove_all(root);
            fail("file browser did not list the folder and the city");
            return;
        }
        list_index_ = folder;
        focus_id_ = kListItem;
        activate_file_row();
        if (files_dir_.find("towns") == std::string::npos) {
            std::filesystem::remove_all(root);
            fail("Enter did not open the selected folder");
            return;
        }
        files_dir_ = root.string();
        refresh_files();
        list_index_ = file;
        focus_id_ = kListItem;
        activate_file_row();
        if (dialog_ == Dialog::Files) {
            std::filesystem::remove_all(root);
            fail("Enter did not open the selected file");
            return;
        }
        close_dialog();
        std::filesystem::remove_all(root);
    }

    // The save question is one line at 1280, or a balanced break. "city?" is
    // not left on a line by itself.
    {
        dialog_ = Dialog::Unsaved;
        after_save_ = AfterSave::Load;
        presented_ = false;
        render();
        if (text_spans_.empty() || text_spans_[0].h > static_cast<double>(dp(36))) {
            fail("save prompt left \"city?\" on its own line");
            return;
        }
        close_dialog();
        after_save_ = AfterSave::Nothing;
    }

    // M10: an autosave or recent-list failure stays until the player dismisses it,
    // and the next failure after that is shown again.
    note_background_write_failed("Autosave failed. This city will not be recovered if the game closes.");
    if (!notice_sticky_ || shown_message_.find("Autosave failed") == std::string::npos) {
        fail("autosave failure was not shown");
        return;
    }
    const auto held = hint_until_;
    if (held < std::chrono::steady_clock::now() + std::chrono::hours(1)) {
        fail("autosave failure notice expires on its own");
        return;
    }
    note_background_write_failed("Autosave failed. This city will not be recovered if the game closes.");
    if (hint_until_ != held) {
        fail("repeating the same failure restarted the notice");
        return;
    }
    dismiss_background_notice();
    if (notice_sticky_ || !shown_message_.empty()) {
        fail("dismiss did not clear the failure notice");
        return;
    }
    note_background_write_failed("Could not update the recent-cities list.");
    if (!notice_sticky_ || shown_message_.find("recent-cities") == std::string::npos) {
        fail("a later failure was not shown again");
        return;
    }
    dismiss_background_notice();

    // M9: a loaded city's speed and disasters do not become the global preferences.
    pref_speed_ = 1;
    pref_disasters_ = 0;
    session_.set_speed(3);
    session_.set_disasters(true);
    restore_play_preferences();
    if (session_.speed() != 1 || session_.disasters() || pref_speed_ != 1 || pref_disasters_ != 0) {
        fail("loading a city replaced the global speed or disasters preference");
        return;
    }
    session_.set_speed(0);

#ifndef _WIN32
    const auto previous_sigint = std::signal(SIGINT, SIG_DFL);
    std::signal(SIGINT, previous_sigint);
    if (previous_sigint == SIG_ERR || previous_sigint == SIG_DFL || previous_sigint == SIG_IGN) {
        fail("SIGINT is not hooked for autosave");
        return;
    }
#endif

    const auto shot = std::filesystem::temp_directory_path() / "lunduke-city2-l10.png";
    std::filesystem::remove(shot);
    shot_path_ = shot.string();
    render();
    render();
    if (std::filesystem::exists(shot)) {
        std::filesystem::remove(shot);
        fail("a screenshot was written before the last frame");
        return;
    }
    quitting_ = true;
    write_final_shot();
    const bool wrote_shot = std::filesystem::exists(shot);
    std::filesystem::remove(shot);
    quitting_ = false;
    if (!wrote_shot) {
        fail("the last frame was not written to the screenshot");
        return;
    }

    const DialogRect eval_deck = place_dialog(1280, 800, 700, 420, 16);
    const DialogRect budget_short = place_dialog(1024, 640, 640, 470, 16);
    if (eval_deck.y + eval_deck.h > 800 || budget_short.y + budget_short.h > 640 || budget_short.w > 1024 - 32) {
        fail("a content-sized dialog does not fit the small window");
        return;
    }

    // Touch builds like the mouse, and a second finger never restores a map.
    close_dialog();
    session_.set_speed(0);
    speed_ = 0;
    select_tool(1);
    map_.set_tile_size(16);
    center_on_fraction(0.5, 0.5);
    render();
    auto stamp = [&]() {
        unsigned long long hash = 1469598103934665603ull;
        for (int ty = 0; ty < CitySession::kWorldH; ++ty) {
            for (int tx = 0; tx < CitySession::kWorldW; ++tx) {
                hash ^= static_cast<unsigned long long>(static_cast<unsigned>(session_.map_value(tx, ty)));
                hash *= 1099511628211ull;
            }
        }
        return hash;
    };
    auto place = [&](int tx, int ty, float &wx, float &wy) {
        const double ox = snapped_map_origin(view_x_, map_ox_, scroll_x_);
        const double oy = snapped_map_origin(view_y_, map_oy_, scroll_y_);
        wx = static_cast<float>(ox + (tx + 0.5) * map_.tile_size());
        wy = static_cast<float>(oy + (ty + 0.5) * map_.tile_size());
        return wx >= view_x_ + 2 && wx < view_x_ + view_w_ - 2 && wy >= view_y_ + 2 && wy < view_y_ + view_h_ - 2;
    };
    auto finger = [&](Uint32 type, SDL_FingerID id, float wx, float wy) {
        SDL_Event event{};
        event.type = type;
        event.tfinger.type = static_cast<SDL_EventType>(type);
        event.tfinger.fingerID = id;
        event.tfinger.x = wx / static_cast<float>(std::max(1, win_w_));
        event.tfinger.y = wy / static_cast<float>(std::max(1, win_h_));
        event.tfinger.pressure = 1.0f;
        handle_event(event);
    };
    // A rest does not arm. The second finger lands before any 8px move, which
    // is the 0.2s, 0.5s, and 1s cases: time is not part of the decision.
    int rested = 0;
    for (int ty = 8; ty < 70 && rested < 3; ty += 2) {
        for (int tx = 8; tx < 90 && rested < 3; tx += 2) {
            float wx = 0;
            float wy = 0;
            if (!place(tx, ty, wx, wy)) {
                continue;
            }
            const auto funds_before = session_.funds();
            const auto map_before = stamp();
            finger(SDL_EVENT_FINGER_DOWN, 1, wx, wy);
            finger(SDL_EVENT_FINGER_MOTION, 1, wx + 3.0f, wy + 1.0f);
            finger(SDL_EVENT_FINGER_DOWN, 2, wx + 36.0f, wy + 8.0f);
            finger(SDL_EVENT_FINGER_MOTION, 1, wx + 7.0f, wy + 4.0f);
            finger(SDL_EVENT_FINGER_MOTION, 2, wx + 40.0f, wy + 11.0f);
            finger(SDL_EVENT_FINGER_UP, 2, wx + 40.0f, wy + 11.0f);
            finger(SDL_EVENT_FINGER_UP, 1, wx + 7.0f, wy + 4.0f);
            if (session_.funds() != funds_before || stamp() != map_before) {
                fail("a two-finger pan built or changed funds");
                return;
            }
            finger(SDL_EVENT_FINGER_DOWN, 1, wx, wy);
            if (session_.funds() != funds_before) {
                fail("a tap built before the finger lifted");
                return;
            }
            finger(SDL_EVENT_FINGER_UP, 1, wx, wy);
            if (session_.funds() < funds_before) {
                ++rested;
            }
        }
    }
    if (rested < 3) {
        fail("a tap did not build, or a rested second finger could not be checked on real land");
        return;
    }

    int prior_x = -1;
    int prior_y = -1;
    int prior_tile = 0;
    std::int64_t after_prior = 0;
    for (int ty = 8; ty < 70 && prior_x < 0; ty += 3) {
        for (int tx = 8; tx < 90 && prior_x < 0; tx += 3) {
            float wx = 0;
            float wy = 0;
            if (!place(tx, ty, wx, wy)) {
                continue;
            }
            const auto funds_before = session_.funds();
            map_.press(map_local_x(wx), map_local_y(wy));
            map_.release();
            if (session_.funds() < funds_before) {
                prior_x = tx;
                prior_y = ty;
                prior_tile = session_.map_value(tx, ty);
                after_prior = session_.funds();
            }
        }
    }
    if (prior_x < 0) {
        fail("a mouse click did not build a prior stroke");
        return;
    }
    bool kept = false;
    for (int ty = 12; ty < 70 && !kept; ty += 4) {
        for (int tx = 12; tx < 80 && !kept; tx += 4) {
            if (tx == prior_x && ty == prior_y) {
                continue;
            }
            float wx = 0;
            float wy = 0;
            if (!place(tx, ty, wx, wy)) {
                continue;
            }
            const float far_x = wx + static_cast<float>(map_.tile_size() * 2);
            const float far_y = wy;
            if (far_x >= view_x_ + view_w_ - 2) {
                continue;
            }
            finger(SDL_EVENT_FINGER_DOWN, 1, wx, wy);
            finger(SDL_EVENT_FINGER_MOTION, 1, far_x, far_y);
            if (session_.funds() >= after_prior) {
                finger(SDL_EVENT_FINGER_UP, 1, far_x, far_y);
                continue;
            }
            const auto after_drag = session_.funds();
            const auto after_map = stamp();
            finger(SDL_EVENT_FINGER_DOWN, 2, far_x - 30.0f, far_y + 24.0f);
            if (session_.funds() != after_drag || stamp() != after_map || session_.map_value(prior_x, prior_y) != prior_tile) {
                fail("a second finger refunded an earlier stroke");
                return;
            }
            finger(SDL_EVENT_FINGER_UP, 1, far_x, far_y);
            finger(SDL_EVENT_FINGER_UP, 2, far_x - 30.0f, far_y + 24.0f);
            if (session_.funds() != after_drag || session_.map_value(prior_x, prior_y) != prior_tile) {
                fail("lifting the fingers changed a finished stroke");
                return;
            }
            kept = true;
        }
    }
    if (!kept) {
        fail("a touch drag did not build before the second finger");
        return;
    }

    const Hotspot *mini = nullptr;
    for (const Hotspot &spot : hot_) {
        if (spot.id == kMinimapId) {
            mini = &spot;
        }
    }
    if (mini == nullptr) {
        fail("the minimap has no hotspot");
        return;
    }
    const float mini_x = static_cast<float>(mini->x + mini->w * 0.5);
    const float mini_y = static_cast<float>(mini->y + mini->h * 0.5);
    finger(SDL_EVENT_FINGER_DOWN, 1, mini_x, mini_y);
    finger(SDL_EVENT_FINGER_UP, 1, mini_x, mini_y);
    const double scroll_after_mini = scroll_x_;
    const float map_x = static_cast<float>(view_x_ + view_w_ * 0.45);
    const float map_y = static_cast<float>(view_y_ + view_h_ * 0.45);
    finger(SDL_EVENT_FINGER_DOWN, 1, map_x, map_y);
    finger(SDL_EVENT_FINGER_MOTION, 1, map_x + 24.0f, map_y);
    const double scroll_jump = scroll_x_ - scroll_after_mini;
    finger(SDL_EVENT_FINGER_UP, 1, map_x + 24.0f, map_y);
    if (scroll_jump > 80.0 || scroll_jump < -80.0) {
        fail("a minimap tap left the next map drag panning the view");
        return;
    }

    const auto previous_map = stamp();
    const auto previous_funds = session_.funds();
    reset_touch_state();
    if (!session_.load_scenario(CitySession::scenario_def(0).id)) {
        fail("could not load a city after the touch session");
        return;
    }
    render();
    const auto loaded_funds = session_.funds();
    const auto loaded_map = stamp();
    if (loaded_map == previous_map && loaded_funds == previous_funds) {
        fail("the loaded city matches the touch session");
        return;
    }
    const float pinch_x = static_cast<float>(view_x_ + view_w_ * 0.5);
    const float pinch_y = static_cast<float>(view_y_ + view_h_ * 0.5);
    finger(SDL_EVENT_FINGER_DOWN, 1, pinch_x, pinch_y);
    finger(SDL_EVENT_FINGER_DOWN, 2, pinch_x + 48.0f, pinch_y + 12.0f);
    finger(SDL_EVENT_FINGER_MOTION, 1, pinch_x - 10.0f, pinch_y + 6.0f);
    finger(SDL_EVENT_FINGER_MOTION, 2, pinch_x + 70.0f, pinch_y - 16.0f);
    finger(SDL_EVENT_FINGER_UP, 2, pinch_x + 70.0f, pinch_y - 16.0f);
    finger(SDL_EVENT_FINGER_UP, 1, pinch_x - 10.0f, pinch_y + 6.0f);
    if (session_.funds() != loaded_funds || stamp() != loaded_map) {
        fail("a pinch after load changed the loaded city");
        return;
    }
    const std::string meter = session_.scenario_meter();
    const auto pop_at = meter.find("Population ");
    if (pop_at == std::string::npos) {
        fail("the scenario meter has no population");
        return;
    }
    const auto arrow = meter.find(" →", pop_at);
    const std::string number = meter.substr(pop_at + 11, arrow == std::string::npos ? std::string::npos : arrow - (pop_at + 11));
    int digits = 0;
    for (char letter : number) {
        if (letter >= '0' && letter <= '9') {
            ++digits;
        }
    }
    if (digits >= 4 && number.find(',') == std::string::npos) {
        fail("population " + number + " has no thousands separator");
        return;
    }
    if (meter.find('$') != std::string::npos) {
        fail("population goal shows a dollar sign: " + meter);
        return;
    }
    const std::string outcome = session_.scenario_outcome_text(false);
    if (outcome.find("Population ") == std::string::npos || outcome.find('$') != std::string::npos) {
        fail("the scenario result shows population as money: " + outcome);
        return;
    }

    // A touch drag scrolls the Scenario and Load lists. The highlight stays
    // on the row that was already chosen. A short lift is the tap that changes it.
    auto find_row = [&](int id) -> const Hotspot * {
        for (const Hotspot &hot : hot_) {
            if (hot.id == id) {
                return &hot;
            }
        }
        return nullptr;
    };
    dialog_ = Dialog::Scenario;
    scenario_index_ = 6;
    list_index_ = 6;
    list_scroll_ = 0;
    list_scroll_free_ = false;
    focus_id_ = kListItem;
    render();
    const int boston_row = scenario_index_ - list_scroll_;
    const Hotspot *boston = find_row(kListItem + boston_row);
    if (boston == nullptr) {
        fail("Boston is not on the scenario list");
        return;
    }
    const int boston_index = scenario_index_;
    const float boston_x = static_cast<float>(boston->x + boston->w * 0.5);
    const float boston_y = static_cast<float>(boston->y + boston->h * 0.5);
    finger(SDL_EVENT_FINGER_DOWN, 1, boston_x, boston_y);
    finger(SDL_EVENT_FINGER_MOTION, 1, boston_x, boston_y - 96.0f);
    finger(SDL_EVENT_FINGER_UP, 1, boston_x, boston_y - 96.0f);
    if (scenario_index_ != boston_index) {
        fail("a touch drag moved the scenario selection from Boston to " +
             std::string(CitySession::scenario_def(scenario_index_).name));
        return;
    }
    render();
    const int rio_row = 7 - list_scroll_;
    const Hotspot *rio = rio_row >= 0 ? find_row(kListItem + rio_row) : nullptr;
    if (rio == nullptr) {
        fail("Rio is not on the scenario list");
        return;
    }
    const float rio_x = static_cast<float>(rio->x + rio->w * 0.5);
    const float rio_y = static_cast<float>(rio->y + rio->h * 0.5);
    finger(SDL_EVENT_FINGER_DOWN, 1, rio_x, rio_y);
    finger(SDL_EVENT_FINGER_MOTION, 1, rio_x, rio_y + 4.0f);
    finger(SDL_EVENT_FINGER_UP, 1, rio_x, rio_y + 4.0f);
    if (scenario_index_ != 7) {
        fail("a tap did not select Rio");
        return;
    }

    files_.clear();
    for (int i = 0; i < 40; ++i) {
        files_.push_back("city-" + std::to_string(i) + ".cty");
    }
    files_save_ = false;
    dialog_ = Dialog::Files;
    list_index_ = 4;
    list_scroll_ = 0;
    list_scroll_free_ = false;
    focus_id_ = kListItem;
    render();
    if (list_max_scroll(static_cast<int>(files_.size()), std::max(1, files_visible_)) < 1) {
        fail("the load list does not scroll");
        return;
    }
    const int load_row = list_index_ - list_scroll_;
    const Hotspot *load_hot = find_row(kListItem + load_row);
    if (load_hot == nullptr) {
        fail("the load list row is missing");
        return;
    }
    const float load_x = static_cast<float>(load_hot->x + load_hot->w * 0.5);
    const float load_y = static_cast<float>(load_hot->y + load_hot->h * 0.5);
    finger(SDL_EVENT_FINGER_DOWN, 1, load_x, load_y);
    finger(SDL_EVENT_FINGER_MOTION, 1, load_x, load_y - 96.0f);
    finger(SDL_EVENT_FINGER_UP, 1, load_x, load_y - 96.0f);
    if (list_index_ != 4) {
        fail("a touch drag moved the load selection");
        return;
    }
    if (list_scroll_ < 1) {
        fail("a touch drag did not scroll the load list");
        return;
    }
    const Hotspot *other = nullptr;
    int other_index = -1;
    for (const Hotspot &hot : hot_) {
        if (hot.id < kListItem || hot.id >= kListItem + 64) {
            continue;
        }
        const int index = list_scroll_ + (hot.id - kListItem);
        if (index != list_index_ && index >= 0 && index < static_cast<int>(files_.size())) {
            other = &hot;
            other_index = index;
            break;
        }
    }
    if (other == nullptr) {
        fail("the load list has no other row to tap");
        return;
    }
    const float other_x = static_cast<float>(other->x + other->w * 0.5);
    const float other_y = static_cast<float>(other->y + other->h * 0.5);
    finger(SDL_EVENT_FINGER_DOWN, 1, other_x, other_y);
    finger(SDL_EVENT_FINGER_MOTION, 1, other_x + 3.0f, other_y + 3.0f);
    finger(SDL_EVENT_FINGER_UP, 1, other_x + 3.0f, other_y + 3.0f);
    if (list_index_ != other_index) {
        fail("a tap did not select the load row under the finger");
        return;
    }

    // A touch on the scrollbar used to leave the drag flag set. Later drags
    // then moved the highlight, and a mouse move with no button dragged too.
    // The thumb itself follows the finger the same way a mouse drag does.
    files_.clear();
    for (int i = 0; i < 40; ++i) {
        files_.push_back("City" + std::to_string(i) + ".cty");
    }
    files_save_ = false;
    dialog_ = Dialog::Files;
    list_index_ = 4;
    list_scroll_ = 0;
    list_scroll_free_ = false;
    list_dragging_ = false;
    focus_id_ = kListItem;
    render();
    if (list_max_scroll(static_cast<int>(files_.size()), std::max(1, files_visible_)) < 1) {
        fail("the load list does not scroll");
        return;
    }
    const Hotspot *bar = find_row(kListScroll);
    if (bar == nullptr || bar->h < 40.0) {
        fail("the load list has no scrollbar");
        return;
    }
    const float bar_x = static_cast<float>(bar->x + bar->w * 0.5);
    const float bar_top = static_cast<float>(bar->y + 4.0);
    finger(SDL_EVENT_FINGER_DOWN, 1, bar_x, bar_top);
    finger(SDL_EVENT_FINGER_UP, 1, bar_x, bar_top);
    if (list_dragging_) {
        fail("lifting off the scrollbar left the drag flag set");
        return;
    }
    const int held_index = list_index_;
    render();
    const Hotspot *body = find_row(kListItem);
    if (body == nullptr) {
        fail("the load list has no row after a scrollbar touch");
        return;
    }
    const float body_x = static_cast<float>(body->x + body->w * 0.5);
    const float body_y = static_cast<float>(body->y + body->h * 0.5);
    finger(SDL_EVENT_FINGER_DOWN, 1, body_x, body_y);
    finger(SDL_EVENT_FINGER_MOTION, 1, body_x, body_y - 96.0f);
    finger(SDL_EVENT_FINGER_UP, 1, body_x, body_y - 96.0f);
    if (list_index_ != held_index) {
        fail("a touch drag after the scrollbar moved the highlight");
        return;
    }

    // The finger is still down, so only buttonless mouse motion can clear it.
    render();
    const Hotspot *bar_held = find_row(kListScroll);
    if (bar_held == nullptr) {
        fail("the load list scrollbar disappeared");
        return;
    }
    const float held_x = static_cast<float>(bar_held->x + bar_held->w * 0.5);
    const float held_top = static_cast<float>(bar_held->y + 4.0);
    const float held_bottom = static_cast<float>(bar_held->y + bar_held->h - 4.0);
    finger(SDL_EVENT_FINGER_DOWN, 1, held_x, held_top);
    if (!list_dragging_) {
        fail("touching the scrollbar did not start a drag");
        return;
    }
    const int mouse_index = list_index_;
    const int mouse_scroll = list_scroll_;
    int ww = 1;
    int wh = 1;
    int pw = 1;
    int ph = 1;
    SDL_GetWindowSize(window_, &ww, &wh);
    SDL_GetWindowSizeInPixels(window_, &pw, &ph);
    SDL_Event motion{};
    motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.state = 0;
    motion.motion.x = held_x * static_cast<float>(ww) / static_cast<float>(std::max(pw, 1));
    motion.motion.y = held_bottom * static_cast<float>(wh) / static_cast<float>(std::max(ph, 1));
    handle_event(motion);
    if (list_index_ != mouse_index || list_scroll_ != mouse_scroll) {
        fail("mouse motion with no button dragged the list");
        return;
    }
    if (list_dragging_) {
        fail("mouse motion with no button left the scrollbar drag set");
        return;
    }
    finger(SDL_EVENT_FINGER_UP, 1, held_x, held_top);

    // A canceled touch is not a mouse-up either.
    finger(SDL_EVENT_FINGER_DOWN, 1, held_x, held_top);
    finger(SDL_EVENT_FINGER_CANCELED, 1, held_x, held_top);
    if (list_dragging_) {
        fail("a canceled scrollbar touch left the drag flag set");
        return;
    }
    const int cancel_index = list_index_;
    render();
    const Hotspot *after_cancel = find_row(kListItem);
    if (after_cancel == nullptr) {
        fail("the load list has no row after a canceled scrollbar touch");
        return;
    }
    const float cancel_x = static_cast<float>(after_cancel->x + after_cancel->w * 0.5);
    const float cancel_y = static_cast<float>(after_cancel->y + after_cancel->h * 0.5);
    finger(SDL_EVENT_FINGER_DOWN, 1, cancel_x, cancel_y);
    finger(SDL_EVENT_FINGER_MOTION, 1, cancel_x, cancel_y - 96.0f);
    finger(SDL_EVENT_FINGER_UP, 1, cancel_x, cancel_y - 96.0f);
    if (list_index_ != cancel_index) {
        fail("a touch drag after a canceled scrollbar touch moved the highlight");
        return;
    }

    list_index_ = 0;
    list_scroll_ = 0;
    list_scroll_free_ = false;
    list_dragging_ = false;
    focus_id_ = kListItem;
    render();
    const Hotspot *thumb = find_row(kListScroll);
    if (thumb == nullptr || thumb->h < 40.0) {
        fail("the load list scrollbar is missing for the thumb drag");
        return;
    }
    const float thumb_x = static_cast<float>(thumb->x + thumb->w * 0.5);
    const float thumb_top = static_cast<float>(thumb->y + 4.0);
    const float thumb_bottom = static_cast<float>(thumb->y + thumb->h - 4.0);
    finger(SDL_EVENT_FINGER_DOWN, 1, thumb_x, thumb_top);
    const int scroll_at_top = list_scroll_;
    const int index_at_top = list_index_;
    finger(SDL_EVENT_FINGER_MOTION, 1, thumb_x, thumb_bottom);
    finger(SDL_EVENT_FINGER_UP, 1, thumb_x, thumb_bottom);
    if (list_scroll_ <= scroll_at_top || list_index_ <= index_at_top) {
        fail("dragging the scrollbar thumb down did not scroll the list down");
        return;
    }
    if (list_dragging_) {
        fail("lifting after a thumb drag left the drag flag set");
        return;
    }

    dialog_ = Dialog::None;
    list_scroll_free_ = false;
    list_dragging_ = false;

    map_.set_tile_size(kMapTileOverview);
    layout();
    clamp_scroll();
    gamepad_nav_ = true;
    cursor_x_ = static_cast<float>(win_w_ - 1);
    cursor_y_ = static_cast<float>(view_y_ + view_h_ * 0.5);
    clamp_gamepad_cursor(1.0f, 0.0f);
    const double map_right = snapped_map_origin(view_x_, map_ox_, scroll_x_) + map_.pixel_width();
    if (cursor_y_ >= view_y_ && cursor_y_ < view_y_ + view_h_ && cursor_x_ >= map_right - 0.5) {
        fail("the gamepad cursor rests in the right grey band");
        return;
    }
    auto stick_down_stays_on_map = [&](const char *zoom) {
        const double map_bottom = snapped_map_origin(view_y_, map_oy_, scroll_y_) + map_.pixel_height();
        const double limit = std::min(static_cast<double>(view_y_) + view_h_, map_bottom);
        cursor_x_ = static_cast<float>(view_x_ + view_w_ * 0.5);
        cursor_y_ = static_cast<float>(win_h_ - 2);
        clamp_gamepad_cursor(0.0f, 1.0f);
        if (!(cursor_y_ < limit - 0.5)) {
            fail(std::string("the gamepad cursor starts in the hint bar at ") + zoom);
            return false;
        }
        for (int frame = 0; frame < 40; ++frame) {
            const double step = 700.0 * (16.0 / 1000.0);
            cursor_y_ = static_cast<float>(std::min<double>(win_h_ - 1, cursor_y_ + step));
            clamp_gamepad_cursor(0.0f, 1.0f);
        }
        if (!(cursor_y_ < limit - 0.5) || cursor_y_ >= static_cast<float>(view_y_ + view_h_)) {
            fail(std::string("holding the stick down left the map at ") + zoom);
            return false;
        }
        return true;
    };
    if (!stick_down_stays_on_map("8px")) {
        return;
    }
    map_.set_tile_size(32);
    layout();
    clamp_scroll();
    if (!stick_down_stays_on_map("32px")) {
        return;
    }
    gamepad_nav_ = false;

    touch_used_ = true;
    deck_ = false;
    if (context_hint().find("Click builds") != std::string::npos) {
        fail("touch kept the keyboard hint");
        return;
    }
    note_keyboard_input();
    if (context_hint().find("click to") == std::string::npos) {
        fail("a key did not restore the keyboard hint");
        return;
    }
    int key_count = 0;
    const OskKey *keys = osk_keys(key_count);
    int random_key = -1;
    for (int i = 0; i < key_count; ++i) {
        if (keys[i].kind == OskKind::Auto) {
            random_key = i;
        }
    }
    dialog_ = Dialog::NewCity;
    gamepad_nav_ = true;
    focus_id_ = kOsk + random_key;
    if (random_key < 0 || dialog_title_hint().find("types") != std::string::npos) {
        fail("the Random key says it types");
        return;
    }
    dialog_ = Dialog::None;
    gamepad_nav_ = false;

    const std::string stamped = visual_stamp();
    overlay_on_ = !overlay_on_;
    const bool overlay_redraws = visual_stamp() != stamped;
    overlay_on_ = !overlay_on_;
    controls_page_ = 1;
    controls_scroll_ = 40;
    const bool controls_redraw = visual_stamp() != stamped;
    controls_page_ = 0;
    controls_scroll_ = 0;
    const int island_was = new_island_;
    new_island_ = CitySession::kIslandAlways;
    const bool land_redraws = visual_stamp() != stamped;
    new_island_ = island_was;
    if (!overlay_redraws || !controls_redraw || !land_redraws) {
        fail("a paused window change did not ask for a redraw");
        return;
    }
}
