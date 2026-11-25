// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

#include "shell_state.hpp"
#include "graph_solo.hpp"
#include "ui_sounds.hpp"
#include "assets.hpp"
#include "layout_mode.hpp"

#include "pad_glyphs.hpp"

#include "atomic_write.hpp"
#include "city_seed.hpp"
#include "file_list.hpp"
#include "list_scroll.hpp"
#include "osk_layout.hpp"
#include "text_edit.hpp"
#include "platform.hpp"
#include "save_path.hpp"
#include "mayor_notes.hpp"
#include "messages.hpp"
#include "present.hpp"
#include "special_buildings.hpp"
#include "terrain_paint.hpp"
#include "tile_atlas.hpp"
#include "placement_hint.hpp"
#include "tools.hpp"
#include "tool_groups.hpp"
#include "welcome_rows.hpp"
#include "version.hpp"
#include "view_math.hpp"

#include "micropolis.h"
#include "tutorial_goals.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

std::string path_join(const std::string &dir, const std::string &name)
{
    if (dir.empty()) {
        return name;
    }
#ifdef _WIN32
    const char sep = '\\';
#else
    const char sep = '/';
#endif
    if (dir.back() == '/' || dir.back() == '\\') {
        return dir + name;
    }
    return dir + sep + name;
}

std::string parent_dir(const std::string &path)
{
    const auto slash = path.find_last_of("/\\");
    if (slash == std::string::npos) {
        return {};
    }
    if (slash == 0) {
        return path.substr(0, 1);
    }
    return path.substr(0, slash);
}

bool ends_with_ci(const std::string &text, const char *suffix)
{
    const std::size_t n = std::char_traits<char>::length(suffix);
    if (text.size() < n) {
        return false;
    }
    for (std::size_t i = 0; i < n; ++i) {
        if (std::tolower(static_cast<unsigned char>(text[text.size() - n + i])) !=
            std::tolower(static_cast<unsigned char>(suffix[i]))) {
            return false;
        }
    }
    return true;
}

} // namespace

bool Shell::dialog_pauses() const
{
    // The terrain editor stands in for the New City page: the city behind it
    // stays paused.
    if (tax_pause_hold_ || terrain_edit_) {
        return true;
    }
    switch (dialog_) {
    case Dialog::None:
    case Dialog::Graphs:
    case Dialog::Evaluation:
    case Dialog::Overlay:
    case Dialog::Query:
    case Dialog::Messages:
    case Dialog::Ledger:
        return false;
    case Dialog::Budget:
        return budget_is_tax_;
    default:
        return true;
    }
}

void Shell::set_paused_for_dialog()
{
    session_.set_simulation_paused(dialog_pauses());
}

void Shell::close_dialog()
{
    if (tax_pause_hold_ && (dialog_ == Dialog::Evaluation || dialog_ == Dialog::Graphs || dialog_ == Dialog::Ledger)) {
        dialog_ = Dialog::Budget;
        hover_id_ = 0;
        focus_id_ = budget_cover_ ? kYearBudget : (kSlider + 1);
        set_paused_for_dialog();
        return;
    }
    const Dialog closing = dialog_;
    dialog_ = Dialog::None;
    dialog_error_.clear();
    osk_shift_ = false;
    osk_latched_ = false;
    if (text_input_on_ && window_ != nullptr) {
        SDL_StopTextInput(window_);
        text_input_on_ = false;
    }
    if (closing == Dialog::LoadError) {
        sync_title();
    }
    // The hover card described the tile under the pointer before the card
    // opened, and the view may have moved since (Inspect's Show power, a
    // zoom). Read it again from where the pointer is now.
    map_.clear_hover();
    if (!gamepad_nav_ && !handheld_ && pointer_x_ >= view_x_ && pointer_x_ < view_x_ + view_w_ && pointer_y_ >= view_y_ &&
        pointer_y_ < view_y_ + view_h_ &&
        !(mini_h_ > 0 && pointer_x_ >= mini_x_ - mini_pad() && pointer_x_ < mini_x_ + mini_w_ + mini_pad() &&
          pointer_y_ >= mini_y_ - mini_pad() && pointer_y_ < mini_bottom() + mini_pad())) {
        map_.motion(map_local_x(pointer_x_), map_local_y(pointer_y_), false);
    }
    set_paused_for_dialog();
}

void Shell::dismiss_dialog()
{
    close_dialog();
    if (welcome_gate_ && dialog_ == Dialog::None) {
        open_dialog(Dialog::Welcome);
    }
}

void Shell::arm_modal_grace()
{
    modal_grace_until_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(350);
    float x = 0;
    float y = 0;
    const SDL_MouseButtonFlags buttons = SDL_GetMouseState(&x, &y);
    modal_swallow_mouse_ = (buttons & SDL_BUTTON_LMASK) != 0;
    modal_swallow_pad_ = false;
    for (SDL_Gamepad *pad : pads_) {
        if (pad != nullptr && SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_SOUTH)) {
            modal_swallow_pad_ = true;
        }
    }
    if (pad_ != nullptr && SDL_GetGamepadButton(pad_, SDL_GAMEPAD_BUTTON_SOUTH)) {
        modal_swallow_pad_ = true;
    }
}

bool Shell::modal_blocks_pointer() const
{
    if (dialog_ == Dialog::None) {
        return false;
    }
    if (std::chrono::steady_clock::now() < modal_grace_until_) {
        return true;
    }
    return modal_swallow_mouse_;
}

bool Shell::modal_blocks_pad() const
{
    if (dialog_ == Dialog::None) {
        return false;
    }
    if (std::chrono::steady_clock::now() < modal_grace_until_) {
        return true;
    }
    return modal_swallow_pad_;
}

void Shell::clear_pause_notice()
{
    if (notice_sticky_) {
        return;
    }
    if (shown_message_.compare(0, 12, "Game paused.") == 0) {
        shown_message_.clear();
        hint_until_ = std::chrono::steady_clock::now();
    }
}

void Shell::begin_scene_fade()
{
    if (!motion_on() || map_.dragging()) {
        return;
    }
    scene_fade_.start();
}

void Shell::begin_city_reveal()
{
    if (!motion_on() || map_.dragging()) {
        return;
    }
    scene_fade_.skip();
    reveal_ms_ = 0;
    // Building the city took this loop step; start the clock on the next.
    reveal_fresh_ = true;
}

void Shell::apply_effects()
{
    effects_ = look_fx::clamp_effects(effects_);
    // Tests, smoke runs and demos (no motion_fx_) get the still frame.
    map_.set_effects(motion_fx_ ? effects_ : look_fx::EffectsOff);
    if (!motion_on()) {
        skip_motion();
    }
}

void Shell::skip_motion()
{
    scene_fade_.skip();
    dialog_anim_ms_ = -1;
    reveal_ms_ = -1;
    event_pan_on_ = false;
    marker_on_ = false;
    puff_on_ = false;
    pop_on_ = false;
    flash_on_ = false;
    year_stamp_.elapsed_ms = -1;
}

void Shell::begin_event_pan(int tile_x, int tile_y)
{
    marker_tx_ = tile_x;
    marker_ty_ = tile_y;
    const double fx = (static_cast<double>(tile_x) + 0.5) / CitySession::kWorldW;
    const double fy = (static_cast<double>(tile_y) + 0.5) / CitySession::kWorldH;
    if (!motion_on()) {
        center_on_fraction(fx, fy);
        event_pan_on_ = false;
        marker_on_ = false;
        return;
    }
    const double ax = view_x_ + view_w_ * 0.5;
    const double ay = view_y_ + view_h_ * 0.5;
    double target_x = scroll_for_map_fraction(fx, ax, view_x_, map_letterbox_x(), map_.pixel_width());
    double target_y = scroll_for_map_fraction(fy, ay, view_y_, map_letterbox_y(), map_.pixel_height());
    const double max_x = scroll_max_x();
    const double max_y = scroll_max_y();
    target_x = std::max(0.0, std::min(max_x, target_x));
    target_y = std::max(0.0, std::min(max_y, target_y));
    marker_on_ = true;
    marker_ms_ = 0;
    const double dx = target_x - scroll_x_;
    const double dy = target_y - scroll_y_;
    if (dx * dx + dy * dy < 64.0) {
        center_on_fraction(fx, fy);
        event_pan_on_ = false;
        return;
    }
    event_from_x_ = scroll_x_;
    event_from_y_ = scroll_y_;
    event_to_x_ = target_x;
    event_to_y_ = target_y;
    event_written_x_ = scroll_x_;
    event_written_y_ = scroll_y_;
    event_pan_ms_ = 0;
    event_pan_on_ = true;
    marker_ms_ = -kGlideMs;
}

void Shell::note_year_stamp()
{
    const std::string date = session_.date_text();
    const int month = date.compare(0, 3, "Jan") == 0 ? 0 : 1;
    if (motion_on() || !year_stamp_.armed) {
        year_stamp_.observe(session_.city_year(), month);
    }
}

void Shell::advance_presentation(int elapsed_ms)
{
    if (elapsed_ms < 0) {
        elapsed_ms = 0;
    }
    funds_tick_.observe(session_.funds(), city_generation_, motion_on() && city_chosen_ && !welcome_gate_);
    funds_tick_.advance(elapsed_ms);
    const bool tick_ok = motion_on() && city_chosen_ && !welcome_gate_;
    demand_tick_.observe(session_.res_demand(), session_.com_demand(), session_.ind_demand(), city_generation_, tick_ok);
    demand_tick_.advance(elapsed_ms);
    if (city_chosen_ && !welcome_gate_) {
        int tx = 0;
        int ty = 0;
        int peak = 0;
        const bool have = session_.worst_traffic(tx, ty, peak);
        if (traffic_peak_generation_ != city_generation_) {
            traffic_peak_ = have ? peak : 0;
            traffic_month_ = session_.game_month_index();
            traffic_peak_generation_ = city_generation_;
        } else if (have) {
            const int month = session_.game_month_index();
            if (month != traffic_month_) {
                // A jump into heavy traffic (band 2, 128+) posts a NEWS line
                // with a place the mayor can jump to. Display only.
                if (peak >= 128 && peak > traffic_peak_ + 24) {
                    session_.note_traffic_spike(tx, ty);
                    shown_message_ = "Traffic is getting worse.";
                    shown_message_serial_ = session_.message_serial();
                    hint_until_ = std::chrono::steady_clock::now() + std::chrono::seconds(5);
                }
                traffic_peak_ = peak;
                traffic_month_ = month;
            }
        }
    }
    if (reveal_ms_ >= 0) {
        if (reveal_fresh_) {
            reveal_fresh_ = false;
        } else {
            // A step is capped so the first frames of a fresh map (slow to
            // build) do not swallow the opening.
            reveal_ms_ = reveal_ms_ >= kRevealMs ? -1 : std::min(kRevealMs, reveal_ms_ + std::min(elapsed_ms, 33));
        }
    }
    if (dialog_anim_ms_ >= 0) {
        // The frame drawn at the full length is the settled one; the next
        // step ends the clock.
        const int len = dialog_anim_len();
        if (dialog_ == Dialog::None || dialog_anim_ms_ >= len) {
            dialog_anim_ms_ = -1;
        } else {
            dialog_anim_ms_ = std::min(len, dialog_anim_ms_ + elapsed_ms);
        }
    }
    if (event_pan_on_) {
        if (std::fabs(scroll_x_ - event_written_x_) > 0.75 || std::fabs(scroll_y_ - event_written_y_) > 0.75) {
            event_pan_on_ = false;
        } else {
            event_pan_ms_ += elapsed_ms;
            const double t = static_cast<double>(event_pan_ms_) / static_cast<double>(kGlideMs);
            scroll_x_ = glide_mix(event_from_x_, event_to_x_, t);
            scroll_y_ = glide_mix(event_from_y_, event_to_y_, t);
            clamp_scroll();
            event_written_x_ = scroll_x_;
            event_written_y_ = scroll_y_;
            if (event_pan_ms_ >= kGlideMs) {
                event_pan_on_ = false;
            }
        }
    }
    if (marker_on_) {
        marker_ms_ += marker_step_ms(elapsed_ms);
        if (marker_ms_ >= kMarkerMs) {
            marker_on_ = false;
        }
    }
    const std::string date = session_.date_text();
    const int month = date.compare(0, 3, "Jan") == 0 ? 0 : 1;
    if (motion_on() || !year_stamp_.armed) {
        year_stamp_.observe(session_.city_year(), month);
    }
    year_stamp_.advance(elapsed_ms);
    if (puff_on_) {
        puff_ms_ += elapsed_ms;
        if (puff_ms_ >= 380) {
            puff_on_ = false;
        }
    }
    if (pop_on_) {
        pop_ms_ += elapsed_ms;
        if (pop_ms_ >= 180) {
            pop_on_ = false;
        }
    }
    if (flash_on_) {
        flash_ms_ += elapsed_ms;
        if (flash_ms_ >= 180) {
            flash_on_ = false;
        }
    }
}

void Shell::note_disaster_fx()
{
    const std::string text = session_.message();
    if (text == disaster_seen_) {
        return;
    }
    disaster_seen_ = text;
    if (!motion_on() || !notice_is_disaster(text)) {
        return;
    }
    flash_on_ = true;
    flash_ms_ = 0;
    if (notice_shakes(text) && quake_strength_ <= 0) {
        quake_strength_ = 240;
        quake_started_ = std::chrono::steady_clock::now();
        quake_until_ = quake_started_ + std::chrono::milliseconds(220);
    }
}

std::string Shell::welcome_continue_label() const
{
    if (recent_.empty()) {
        return {};
    }
    const std::string &path = recent_.front();
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    std::string base = path;
    const auto slash = base.find_last_of("/\\");
    if (slash != std::string::npos) {
        base = base.substr(slash + 1);
    }
    if (base.size() > 4) {
        const std::string tail = base.substr(base.size() - 4);
        if (tail == ".cty" || tail == ".CTY") {
            base.resize(base.size() - 4);
        }
    }
    if (base.empty()) {
        return {};
    }
    return "_Continue: " + base;
}

civic::YearDigest Shell::year_digest() const
{
    const auto book = session_.budget();
    const CitySession::Evaluation report = session_.evaluation();
    int votes[civic::kProblems] = {};
    const char *top_name = nullptr;
    int top_votes = -1;
    int top_kind = -1;
    if (!report.no_residents && !report.problems.empty()) {
        top_name = report.problems[0].name.c_str();
        top_votes = report.problems[0].votes;
        top_kind = report.problems[0].kind;
        for (const CitySession::Problem &problem : report.problems) {
            if (problem.kind >= 0 && problem.kind < civic::kProblems) {
                votes[problem.kind] = problem.votes;
            }
        }
    }
    std::vector<civic::DigestNotice> notes;
    notes.reserve(session_.notices().size());
    for (const CitySession::CityNotice &notice : session_.notices()) {
        civic::DigestNotice one;
        one.text = notice.text;
        one.year = notice.year;
        one.month = notice.month;
        one.has_place = notice.has_place;
        one.tile_x = notice.tile_x;
        one.tile_y = notice.tile_y;
        notes.push_back(one);
    }
    // Start of year is the balance when the year began, so Gained/Lost
    // covers the whole year's building and income, not just January's
    // collection. Older saves without it keep the pre-collection balance.
    // Review nov19 #13: End of year is the treasury now, the same number
    // as the Funds readout behind the review. It used to add this
    // January's pending budget ($817 here, $912 in the top bar).
    const civic::Ledger &ledger = session_.ledger();
    const std::int64_t opening = ledger.have_start_funds ? ledger.start_funds : book.previous_funds;
    const std::int64_t closing = session_.funds();
    civic::YearDigest digest = civic::make_year_digest(
        opening, closing, true, false, report.population, session_.ledger(), votes, top_name, top_votes, top_kind,
        report.score_delta, report.deltas_known, session_.city_year(), notes);
    digest.ended_year = session_.review_year();
    digest.opening_funds = opening;
    digest.closing_funds = closing;
    digest.tax_percent = book.tax_percent;
    return digest;
}

void Shell::open_dialog(Dialog dialog, bool prompted)
{
    reset_touch_state();
    menu_.close();
    dialog_ = dialog;
    dialog_error_.clear();
    dialog_anim_ms_ = -1;
    if (motion_on() && (dialog == Dialog::Budget || dialog == Dialog::Evaluation || dialog == Dialog::Graphs ||
                       dialog == Dialog::Messages || dialog == Dialog::Welcome || dialog == Dialog::Scenario ||
                       dialog == Dialog::Query || dialog == Dialog::Disaster || dialog == Dialog::Settings ||
                       dialog == Dialog::About || dialog == Dialog::HowToPlay || dialog == Dialog::Info || dialog == Dialog::License ||
                       dialog == Dialog::Ledger || dialog == Dialog::Outcome)) {
        dialog_anim_ms_ = 0;
    }
    if (!prompted) {
        arm_modal_grace();
    } else {
        modal_grace_until_ = {};
        modal_swallow_mouse_ = false;
        modal_swallow_pad_ = false;
    }
    // A hover left over from the map must not light a dialog button.
    hover_id_ = 0;
    focus_id_ = kOk;
    if (dialog == Dialog::Budget) {
        session_.note_tutorial_budget();
    }
    if (dialog == Dialog::Query) {
        session_.note_tutorial_inspect();
    }
    if (dialog == Dialog::NewCity) {
        field_a_ = "New City";
        field_b_ = "auto";
        // "auto" means this seed until the dialog closes; the preview and
        // Generate both use it.
        auto_seed_ = roll_city_seed(static_cast<unsigned>(std::time(nullptr)), static_cast<unsigned>(SDL_GetTicks()));
        terrain_key_ = 0;
        // A painted map belongs to this visit to the page only.
        pending_terrain_.clear();
        terrain_confirm_id_ = 0;
        new_page_ = 0;
        new_level_ = CitySession::kLevelEasy;
        new_island_ = CitySession::kTerrainDefault;
        new_rivers_ = CitySession::kTerrainDefault;
        new_lakes_ = CitySession::kTerrainDefault;
        new_trees_ = CitySession::kTerrainDefault;
        focus_id_ = kFieldName;
        field_select_all(field_a_, field_caret_, field_anchor_);
        // A pad starts on the keyboard's first key, lit, so the first A
        // types (review nov25 #17: the keys showed no focus and the first
        // A only moved to 'q').
        if (gamepad_nav_ && show_osk()) {
            focus_id_ = kOsk;
        }
    } else if (dialog == Dialog::Rename) {
        field_a_ = session_.city_name();
        focus_id_ = kFieldName;
        field_select_all(field_a_, field_caret_, field_anchor_);
        if (gamepad_nav_ && show_osk()) {
            focus_id_ = kOsk;
        }
    } else if (dialog == Dialog::Files && files_save_) {
        field_a_ = proposed_city_filename(session_.city_name(), session_.save_path());
        focus_id_ = kFieldName;
        select_save_name();
    } else if (dialog == Dialog::Unsaved) {
        focus_id_ = kSave;
    } else if (dialog == Dialog::Disaster || dialog == Dialog::RecoveryDelete || dialog == Dialog::Overwrite) {
        focus_id_ = kCancel;
    } else if (dialog == Dialog::Recovery) {
        // Recover is the useful default on the launch screen. Once a city
        // is already loaded, Enter must not replace it.
        focus_id_ = city_chosen_ ? kNotNow : kRecover;
    } else if (dialog == Dialog::Scenario) {
        focus_id_ = kListItem;
        list_index_ = scenario_index_;
    } else if (dialog == Dialog::Files && !files_save_) {
        focus_id_ = files_.empty() ? kCancel : kListItem;
    } else if (dialog == Dialog::Welcome) {
        focus_id_ = welcome_continue_label().empty() ? kWelcomeNew : kWelcomeContinue;
    } else if (dialog == Dialog::Graphs) {
        focus_id_ = kGraphPlot;
        graph_pointer_x_ = -1;
    } else if (dialog == Dialog::Bindings) {
        binds_undo_ = binds_;
        list_index_ = 0;
        list_scroll_ = 0;
        bind_listen_ = false;
        focus_id_ = kListItem;
    } else if (dialog == Dialog::FileRename) {
        field_a_ = file_target_;
        focus_id_ = kFieldName;
        select_save_name();
    } else if (dialog == Dialog::FileDelete) {
        focus_id_ = kCancel;
    } else if (dialog == Dialog::QuitConfirm || dialog == Dialog::BulldozeConfirm || dialog == Dialog::TerrainDiscard ||
               dialog == Dialog::TerrainLowLand) {
        // Cancel is the focused button, so a stray A does not leave the game
        // or knock down the building.
        focus_id_ = kCancel;
    } else if (dialog == Dialog::Outcome) {
        focus_id_ = kKeep;
    } else if (dialog == Dialog::Budget) {
        focus_id_ = budget_cover_ ? kYearBudget : (kSlider + 1);
        budget_was_dirty_ = session_.dirty();
    } else if (dialog == Dialog::Ledger) {
        focus_id_ = kClose;
    } else if (dialog == Dialog::Messages) {
        focus_id_ = session_.notices().empty() ? kClose : kListItem;
        list_index_ = 0;
        list_scroll_ = 0;
    } else if (dialog == Dialog::HowToPlay || dialog == Dialog::About) {
        howto_scroll_ = 0;
        howto_scroll_max_ = 0;
        focus_id_ = kClose;
    } else if (dialog == Dialog::License) {
        // Read the files each time the viewer opens; they are small.
        license_text_ = license_viewer_text();
        license_scroll_ = 0;
        license_touch_y_ = -1.0f;
        focus_id_ = kClose;
    } else if (dialog == Dialog::Controls) {
        // Deck already leads with the gamepad page. A desktop pad should too,
        // once that pad was the last input.
        controls_page_ = (gamepad_nav_ && !deck_) ? 1 : 0;
        focus_id_ = kCancel;
    } else if (dialog == Dialog::Settings) {
        open_volume_ = volume_;
        open_wheel_zooms_ = wheel_zooms_;
        open_screen_shake_ = screen_shake_;
        open_fullscreen_ = window_ != nullptr && (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) != 0;
        open_scale_percent_ = user_scale_ <= 0.05f ? 0 : static_cast<int>(std::lround(user_scale_ * 100.0f));
        if (open_scale_percent_ < 0) {
            open_scale_percent_ = 0;
        }
        scale_percent_ = open_scale_percent_;
        open_pause_unfocused_ = pause_unfocused_;
        open_autosave_ = autosave_seconds_;
        open_slate_ = slate_;
        open_night_ = night_;
        open_effects_ = effects_;
        open_minimap_engraved_ = minimap_engraved_;
        open_layout_pref_ = layout_pref_;
        focus_id_ = kSlider + 1;
    }
    set_paused_for_dialog();
}

void Shell::sync_title()
{
    if (window_ == nullptr) {
        return;
    }
    if (dialog_ == Dialog::LoadError) {
        const std::string failed = std::string(kProductName) + " - Could not load city";
        SDL_SetWindowTitle(window_, failed.c_str());
        return;
    }
    if (welcome_gate_ && !city_chosen_) {
        SDL_SetWindowTitle(window_, kProductName);
        return;
    }
    const std::string title = std::string(kProductName) + " - " + session_.city_name();
    SDL_SetWindowTitle(window_, title.c_str());
}

void Shell::note_message()
{
    sync_title();
}

void Shell::set_speed(int speed)
{
    if (speed < 0) {
        speed = 0;
    }
    if (speed > 3) {
        speed = 3;
    }
    if (background_held_) {
        // The window is covered. Remember the choice and keep the sim paused
        // until the hold ends.
        if (session_.outcome_pause_pending() && speed > 0) {
            session_.resume_after_outcome();
            speed = session_.speed();
        }
        background_speed_ = speed;
        pref_speed_ = speed;
        speed_ = 0;
        session_.set_speed(0);
        return;
    }
    speed_ = speed;
    session_.set_speed(speed);
    if (session_.outcome_pause_pending() && speed > 0) {
        session_.resume_after_outcome();
        speed_ = session_.speed();
    }
    pref_speed_ = speed_;
    if (speed_ > 0) {
        clear_pause_notice();
    }
}

void Shell::toggle_pause()
{
    if (speed_ == 0) {
        set_speed(paused_from_ > 0 ? paused_from_ : 2);
    } else {
        paused_from_ = speed_;
        set_speed(0);
    }
}

void Shell::cycle_speed()
{
    // Held Y changes speed only. Pause stays on a tap of Y.
    if (speed_ < 1 || speed_ >= 3) {
        set_speed(1);
        return;
    }
    set_speed(speed_ + 1);
}

void Shell::scroll_license(int delta)
{
    license_scroll_ = std::max(0, std::min(license_scroll_max_, license_scroll_ + delta));
}

void Shell::cancel_dialog()
{
    if (dialog_ == Dialog::NewCity && new_page_ > 0) {
        on_dialog_button(kBack);
        return;
    }
    on_dialog_button(kCancel);
}

void Shell::center_on_cursor()
{
    int tx = 0;
    int ty = 0;
    if (!map_tile_at(map_local_x(cursor_x_), map_local_y(cursor_y_), map_.shake_x(), map_.shake_y(), map_.tile_size(),
                     CitySession::kWorldW, CitySession::kWorldH, tx, ty)) {
        return;
    }
    center_on_fraction((tx + 0.5) / CitySession::kWorldW, (ty + 0.5) / CitySession::kWorldH);
}

void Shell::select_save_name()
{
    field_select_all(field_a_, field_caret_, field_anchor_);
    if (path_has_cty_suffix(field_a_) && field_a_.size() > 4) {
        field_caret_ = static_cast<int>(field_a_.size()) - 4;
    }
}

int Shell::placing_special() const
{
    return tool_fixed_special(tool_by_index(tool_index_));
}

std::string Shell::tool_hint_now(int index) const
{
    const ToolDef *tool = tool_by_index(index);
    std::string text = tool->hint;
    const int fixed = tool_fixed_special(tool);
    if (fixed >= 0) {
        const std::string detail = session_.special_menu_detail(fixed);
        if (!detail.empty() && detail[0] != '$') {
            text += ". " + detail;
        }
    }
    return text;
}

unsigned Shell::locked_tool_mask() const
{
    const std::string key = session_.change_fingerprint() + ":" + std::to_string(session_.population());
    if (!key.empty() && key == locked_mask_key_) {
        return locked_mask_;
    }
    unsigned mask = 0;
    for (int i = 0; i < kPaletteToolCount && i < 32; ++i) {
        const int fixed = tool_fixed_special(tool_by_index(i));
        if (fixed < 0) {
            continue;
        }
        const std::string detail = session_.special_menu_detail(fixed);
        if (!detail.empty() && detail[0] != '$') {
            mask |= 1u << i;
        }
    }
    locked_mask_key_ = key;
    locked_mask_ = mask;
    return mask;
}

void Shell::select_tool(int index)
{
    touch_quick_shown_ = -1;
    if (index < 0) {
        index = kToolCount - 1;
    }
    if (index >= kToolCount) {
        index = 0;
    }
    tool_index_ = index;
    palette_.set_selected(index);
    map_.set_tool(tool_by_index(index)->engine_id);
}

void Shell::cycle_tool(int delta)
{
    if (terrain_edit_) {
        // The shoulders step through the terrain pieces instead.
        const int count = terrain_paint::kPieceCount;
        select_terrain_piece(((terrain_piece_ + delta) % count + count) % count);
        return;
    }
    select_tool(tool_index_ + delta);
}

void Shell::press_palette(double x, double y)
{
    palette_.press(x, y);
}

int Shell::minimap_reserve_x() const
{
    // The minimap frame (mini_pad() outside mini_w_) plus a small gap,
    // measured from the right edge of the view.
    if (mini_h_ <= 0 || mini_w_ <= 0) {
        return 0;
    }
    return std::max(0, view_x_ + view_w_ - (mini_x_ - mini_pad()) + dp(4));
}

int Shell::map_letterbox_x() const
{
    return map_letterbox_clear(view_w_, map_.pixel_width(), minimap_reserve_x());
}

double Shell::scroll_max_x() const
{
    // The minimap sits over the top-right corner of the map. The view may
    // scroll past the right edge by its width so those tiles can be reached.
    return map_scroll_limit(view_w_, map_.pixel_width(), map_letterbox_x(), minimap_reserve_x());
}

int Shell::map_reserve_y() const
{
    // Handheld: the quick bar and hint strip float over the bottom of the
    // map. The view may scroll past the bottom edge by their height so the
    // last rows can be brought up above them.
    if (!handheld_ || quick_h_ <= 0) {
        return 0;
    }
    return std::max(0, view_y_ + view_h_ - quick_y_);
}

int Shell::map_letterbox_y() const
{
    return map_letterbox_clear(view_h_, map_.pixel_height(), map_reserve_y());
}

double Shell::scroll_max_y() const
{
    return map_scroll_limit(view_h_, map_.pixel_height(), map_letterbox_y(), map_reserve_y());
}

void Shell::clamp_scroll()
{
    const double max_x = scroll_max_x();
    const double max_y = scroll_max_y();
    scroll_x_ = std::max(0.0, std::min(max_x, scroll_x_));
    scroll_y_ = std::max(0.0, std::min(max_y, scroll_y_));
}

void Shell::fit_map_to_view()
{
    // 8px is the overview. It is allowed to letterbox so the whole city stays
    // in view. Every larger zoom still grows until the window is covered, and
    // a closer zoom is never shrunk.
    const int cover = map_.tile_size() <= kMapTileOverview
                          ? map_.tile_size()
                          : tile_size_to_cover(view_w_, view_h_, CitySession::kWorldW, CitySession::kWorldH,
                                               map_.tile_size(), kMapTileSource, kMaxMapTile);
    if (cover != map_.tile_size()) {
        const double ax = view_x_ + view_w_ * 0.5;
        const double ay = view_y_ + view_h_ * 0.5;
        const double fx = map_point_fraction(ax, view_x_, map_ox_, scroll_x_, map_.pixel_width());
        const double fy = map_point_fraction(ay, view_y_, map_oy_, scroll_y_, map_.pixel_height());
        map_.set_tile_size(cover);
        scroll_x_ = scroll_for_map_fraction(fx, ax, view_x_, map_letterbox_x(), map_.pixel_width());
        scroll_y_ = scroll_for_map_fraction(fy, ay, view_y_, map_letterbox_y(), map_.pixel_height());
        clamp_scroll();
    }
    map_ox_ = map_letterbox_x();
    map_oy_ = map_letterbox_y();
    clamp_scroll();
}

double Shell::map_local_x(double x) const
{
    // Same device pixel the map was drawn on. A fractional scroll is floored
    // at paint time; hit testing has to follow that or an 8px tile boundary
    // lands on the neighbour.
    return x - snapped_map_origin(view_x_, map_ox_, scroll_x_);
}

double Shell::map_local_y(double y) const
{
    return y - snapped_map_origin(view_y_, map_oy_, scroll_y_);
}

void Shell::center_on_fraction(double fx, double fy)
{
    const double ax = view_x_ + view_w_ * 0.5;
    const double ay = view_y_ + view_h_ * 0.5;
    scroll_x_ = scroll_for_map_fraction(fx, ax, view_x_, map_letterbox_x(), map_.pixel_width());
    scroll_y_ = scroll_for_map_fraction(fy, ay, view_y_, map_letterbox_y(), map_.pixel_height());
    clamp_scroll();
}

void Shell::start_scenario(int id_num)
{
    ++city_generation_;
    center_pad_cursor();
    reset_touch_state();
    clear_transient_message();
    if (!session_.load_scenario(id_num)) {
        info_title_ = "Play Scenario";
        info_body_ = "Could not start that scenario.";
        open_dialog(Dialog::Info);
        return;
    }
    welcome_gate_ = false;
    city_chosen_ = true;
    adopt_loaded_speed(session_.speed());
    if (id_num == SC_LEARN_TO_PLAY) {
        center_on_learn_field();
    } else {
        center_on_fraction(0.5, 0.5);
    }
    sync_title();
    begin_city_reveal();
}

void Shell::center_on_learn_field()
{
    // The desktop hint is pinned over the top of the map (about three
    // lines at the default 1011x601 window). Put the pad's first teaching
    // rows (the park at kParkY) just below it, and keep as much of the pad
    // in view as the window allows. The Deck's hint is a short chip, so it
    // keeps the plain field centre.
    double cy = tutorial::kFieldY + 0.5;
    const int tile = std::max(1, map_.tile_size());
    if (!handheld_ && view_h_ > 0) {
        const double rows = static_cast<double>(view_h_) / tile;
        const double hint_rows = static_cast<double>(hud_top_ + dp(104)) / tile;
        double top = tutorial::kParkY - 1 - hint_rows;
        // A tall window has room to spare: do not push the pad lower than
        // the plain centre would.
        top = std::min(top, cy - rows / 2.0);
        // A short window: keep the plant row in view before the pad margin.
        top = std::max(top, tutorial::kPlantY + 2 - rows);
        cy = top + rows / 2.0;
    }
    center_on_fraction((tutorial::kFieldX + 0.5) / CitySession::kWorldW, cy / CitySession::kWorldH);
}

void Shell::note_view_for_save()
{
    save_view::View view;
    view.valid = true;
    view.tile_size = map_.tile_size();
    view.center_x = std::clamp(map_point_fraction(view_x_ + view_w_ * 0.5, view_x_, map_ox_, scroll_x_, map_.pixel_width()),
                               0.0, 1.0);
    view.center_y = std::clamp(map_point_fraction(view_y_ + view_h_ * 0.5, view_y_, map_oy_, scroll_y_, map_.pixel_height()),
                               0.0, 1.0);
    session_.set_view_for_save(view);
}

void Shell::restore_loaded_view()
{
    const save_view::View &view = session_.loaded_view();
    if (!view.valid) {
        center_on_fraction(0.5, 0.5);
        return;
    }
    // Same steps the Recover path uses for the autosave's view.
    map_.set_tile_size(opening_map_tile(handheld_, view.tile_size));
    map_ox_ = map_letterbox_x();
    map_oy_ = map_letterbox_y();
    center_on_fraction(view.center_x, view.center_y);
}

void Shell::zoom_by(int delta)
{
    zoom_by(delta, view_x_ + view_w_ * 0.5, view_y_ + view_h_ * 0.5);
}

void Shell::zoom_by(int delta, double anchor_x, double anchor_y)
{
    const int before = map_.tile_size();
    // Read the anchor before the tile size changes. The grey band is not
    // part of the city; a centred 8px map would otherwise zoom in about
    // 13 tiles to the right of the tile under the view centre.
    const double fx = map_point_fraction(anchor_x, view_x_, map_ox_, scroll_x_, map_.pixel_width());
    const double fy = map_point_fraction(anchor_y, view_y_, map_oy_, scroll_y_, map_.pixel_height());
    map_.set_tile_size(step_map_tile(before, delta));
    if (map_.tile_size() == before) {
        return;
    }
    const int ox = map_letterbox_x();
    const int oy = map_letterbox_y();
    scroll_x_ = scroll_for_map_fraction(fx, anchor_x, view_x_, ox, map_.pixel_width());
    scroll_y_ = scroll_for_map_fraction(fy, anchor_y, view_y_, oy, map_.pixel_height());
    clamp_scroll();
    map_ox_ = ox;
    map_oy_ = oy;
}

void Shell::clamp_gamepad_cursor(float push_x, float push_y)
{
    if (!gamepad_nav_ || view_w_ < 1 || view_h_ < 1) {
        return;
    }
    const double view_r = static_cast<double>(view_x_) + view_w_;
    const double view_b = static_cast<double>(view_y_) + view_h_;
    double x = cursor_x_;
    double y = cursor_y_;
    const double map_left = snapped_map_origin(view_x_, map_ox_, scroll_x_);
    const double map_top = snapped_map_origin(view_y_, map_oy_, scroll_y_);
    const double vis_left = std::max(static_cast<double>(view_x_), map_left);
    const double vis_top = std::max(static_cast<double>(view_y_), map_top);
    const double vis_right = std::min(view_r, map_left + map_.pixel_width());
    const double vis_bottom = std::min(view_b, map_top + map_.pixel_height());
    const bool in_port = x >= view_x_ && x < view_r && y >= view_y_ && y < view_b;
    if (in_port) {
        x = clamp_cursor_off_letterbox(x, push_x, view_x_, view_r, vis_left, vis_right);
        y = clamp_cursor_off_letterbox(y, push_y, view_y_, view_b, vis_top, vis_bottom);
    }
    // A right push used to step onto the port edge and stay there: the next
    // frame treats that edge as outside the port. At 8px the edge is the
    // grey band. The toolbar is on the left, so the right side is not a control.
    if (y >= view_y_ && x >= vis_right && x >= view_x_) {
        x = vis_right - 1.0;
    }
    // Holding the stick down used to step past the map into the hint bar.
    // The next frame treats that as outside the port, and the D-pad pans
    // instead of moving the cursor, so only the stick could come back.
    // The visible map is the limit at every zoom, including the 8px overview.
    if (x >= static_cast<double>(view_x_) && y >= vis_bottom && vis_bottom > vis_top) {
        y = vis_bottom - 1.0;
    }
    // Handheld: the quick bar and hint strip cover the bottom of the map.
    // The cursor may rest on the bar to pick a tool but not on the strip
    // below it, and a drag stops at the bar's top edge so it never lays
    // tiles out of sight. The last rows scroll up above the bar instead.
    const int reserve = map_reserve_y();
    if (reserve > 0) {
        const double bar_bottom = static_cast<double>(quick_y_ + quick_h_ + dp(4));
        const double limit = pad_dragging_ ? static_cast<double>(view_b - reserve) : bar_bottom;
        if (y >= limit) {
            y = limit - 1.0;
        }
    }
    // Handheld drag: the top chips and the zoom buttons cover the map too.
    // A line drawn toward them used to park the cursor under the "+" button
    // (the edge scroll only woke 28px from the window edge), with its tag cut
    // off. The drag stops at the open map instead and the edge scroll starts
    // there (shell.cpp, map_safe_rect).
    if (handheld_ && pad_dragging_) {
        double sx = 0;
        double sy = 0;
        double sw = 0;
        double sh = 0;
        map_safe_rect(sx, sy, sw, sh);
        x = std::clamp(x, sx, sx + sw - 1.0);
        y = std::clamp(y, sy, sy + sh - 1.0);
    }
    cursor_x_ = static_cast<float>(x);
    cursor_y_ = static_cast<float>(y);
}

void Shell::map_safe_rect(double &x, double &y, double &w, double &h) const
{
    x = view_x_;
    y = view_y_;
    w = view_w_;
    h = view_h_;
    if (!handheld_ || quick_h_ <= 0) {
        return;
    }
    const double top = std::max<double>(view_y_, hud_top_);
    const double right = std::min<double>(view_x_ + view_w_, win_w_ - hud_right_);
    const double bottom = std::min<double>(view_y_ + view_h_, quick_y_);
    if (right - view_x_ < dp(64) || bottom - top < dp(64)) {
        return;
    }
    y = top;
    w = right - view_x_;
    h = bottom - top;
}

void Shell::center_pad_cursor()
{
    if (!gamepad_nav_) {
        return;
    }
    double sx = 0;
    double sy = 0;
    double sw = 0;
    double sh = 0;
    map_safe_rect(sx, sy, sw, sh);
    cursor_x_ = static_cast<float>(sx + sw * 0.5);
    cursor_y_ = static_cast<float>(sy + sh * 0.5);
    clamp_gamepad_cursor(0.0f, 0.0f);
}

void Shell::apply_pad_direction(SDL_GamepadButton button)
{
    AccelKey dir = AccelKey::None;
    if (button == SDL_GAMEPAD_BUTTON_DPAD_UP) {
        dir = AccelKey::Up;
    } else if (button == SDL_GAMEPAD_BUTTON_DPAD_DOWN) {
        dir = AccelKey::Down;
    } else if (button == SDL_GAMEPAD_BUTTON_DPAD_LEFT) {
        dir = AccelKey::Left;
    } else if (button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT) {
        dir = AccelKey::Right;
    }
    if (dir == AccelKey::None || pad_dragging_) {
        return;
    }
    if (menu_.open()) {
        menu_.wrap_rows = false;
        menu_.handle_key(dir, 0, 0);
        menu_.wrap_rows = true;
        return;
    }
    if (dialog_ != Dialog::None) {
        if (dialog_ == Dialog::Controls && (dir == AccelKey::Left || dir == AccelKey::Right)) {
            controls_page_ = (controls_page_ + (dir == AccelKey::Right ? 1 : 2)) % 3;
            controls_scroll_ = 0;
            return;
        }
        if (dialog_ == Dialog::License && (dir == AccelKey::Up || dir == AccelKey::Down)) {
            scroll_license(dir == AccelKey::Up ? -dp(40) : dp(40));
            return;
        }
        if (dialog_ == Dialog::Controls && controls_scroll_max_ > 0 &&
            (dir == AccelKey::Up || dir == AccelKey::Down)) {
            const int step = dp(28);
            controls_scroll_ += dir == AccelKey::Up ? -step : step;
            if (controls_scroll_ < 0) {
                controls_scroll_ = 0;
            }
            if (controls_scroll_ > controls_scroll_max_) {
                controls_scroll_ = controls_scroll_max_;
            }
            return;
        }
        const int delta = dir == AccelKey::Up || dir == AccelKey::Left ? -1 : 1;
        if (dialog_ == Dialog::Welcome) {
            step_welcome_focus(dir);
            return;
        }
        if (focus_is_flat_buttons()) {
            if (dir == AccelKey::Up || dir == AccelKey::Down) {
                return;
            }
            move_focus(delta, false);
            return;
        }
        if ((dir == AccelKey::Left || dir == AccelKey::Right) && dialog_ == Dialog::Graphs && focus_id_ == kGraphPlot) {
            scrub_graph(dir == AccelKey::Left ? -1 : 1);
            return;
        }
        if ((dir == AccelKey::Left || dir == AccelKey::Right) &&
            adjust_focused_slider(dir == AccelKey::Left ? -1 : 1)) {
            return;
        }
        if ((dir == AccelKey::Up && nudge_dialog_up_down(-1)) || (dir == AccelKey::Down && nudge_dialog_up_down(1))) {
            return;
        }
        move_focus(delta, false);
        return;
    }
    int dx = 0;
    int dy = 0;
    if (dir == AccelKey::Left) {
        dx = -1;
    } else if (dir == AccelKey::Right) {
        dx = 1;
    } else if (dir == AccelKey::Up) {
        dy = -1;
    } else if (dir == AccelKey::Down) {
        dy = 1;
    }
    // Pan while more of the city is off screen. At the map edge that pan
    // does not move, so the cursor steps onto the next visible tile instead.
    const double before_x = scroll_x_;
    const double before_y = scroll_y_;
    const int pan = std::max(1, map_.tile_size()) * 3;
    scroll_x_ += static_cast<double>(dx) * pan;
    scroll_y_ += static_cast<double>(dy) * pan;
    clamp_scroll();
    if (std::fabs(scroll_x_ - before_x) > 0.01 || std::fabs(scroll_y_ - before_y) > 0.01) {
        return;
    }
    step_cursor_on_map(dx, dy);
}

void Shell::step_welcome_focus(AccelKey dir)
{
    // The Welcome buttons are a grid: Left and Right move along a row, Up
    // and Down between rows (Deck hardware notes, Nov 25).
    const int dx = dir == AccelKey::Left ? -1 : (dir == AccelKey::Right ? 1 : 0);
    const int dy = dir == AccelKey::Up ? -1 : (dir == AccelKey::Down ? 1 : 0);
    const bool has_continue = std::find(focus_order_.begin(), focus_order_.end(), kWelcomeContinue) != focus_order_.end();
    const bool known = std::find(focus_order_.begin(), focus_order_.end(), focus_id_) != focus_order_.end();
    if (!known) {
        move_focus(dx + dy, false);
        return;
    }
    const int next = welcome_step(focus_id_, dx, dy, has_continue);
    if (next != focus_id_ && std::find(focus_order_.begin(), focus_order_.end(), next) != focus_order_.end()) {
        focus_id_ = next;
    }
}

bool Shell::stick_navigates() const
{
    // Where the left stick steps focus like the D-pad (Deck hardware
    // notes, Nov 25): every window and menu, the on-screen keyboard
    // included. On the map it moves the cursor.
    return dialog_ != Dialog::None || menu_.open();
}

void Shell::service_stick_nav(int elapsed_ms)
{
    using pad_feel::StickDir;
    const bool live = stick_navigates() && !pad_dragging_;
    if (!live) {
        stick_nav_live_ = false;
        stick_nav_held_ = StickDir::None;
        pad_feel::repeat_clear(stick_nav_repeat_);
        return;
    }
    const StickDir dir = pad_feel::stick_nav_dir(raw_lx_, raw_ly_, stick_nav_held_);
    if (!stick_nav_live_) {
        // A window that opens under a stick already pushed (the player was
        // moving the map cursor) waits for it to come back first.
        stick_nav_live_ = true;
        stick_nav_held_ = dir;
        pad_feel::repeat_clear(stick_nav_repeat_);
        return;
    }
    auto button_for = [](StickDir d) {
        switch (d) {
        case StickDir::Up:
            return SDL_GAMEPAD_BUTTON_DPAD_UP;
        case StickDir::Down:
            return SDL_GAMEPAD_BUTTON_DPAD_DOWN;
        case StickDir::Left:
            return SDL_GAMEPAD_BUTTON_DPAD_LEFT;
        case StickDir::Right:
            return SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
        default:
            return SDL_GAMEPAD_BUTTON_INVALID;
        }
    };
    if (dir != stick_nav_held_) {
        stick_nav_held_ = dir;
        pad_feel::repeat_clear(stick_nav_repeat_);
        if (dir != StickDir::None) {
            gamepad_nav_ = true;
            apply_pad_direction(button_for(dir));
            pad_feel::repeat_arm(stick_nav_repeat_);
        }
        return;
    }
    if (dir == StickDir::None) {
        return;
    }
    const int steps = pad_feel::repeat_tick(stick_nav_repeat_, elapsed_ms);
    for (int i = 0; i < steps && stick_navigates(); ++i) {
        apply_pad_direction(button_for(dir));
    }
}

bool Shell::map_dpad_repeating() const
{
    return nav_repeat_.held && dialog_ == Dialog::None && !menu_.open() && !pad_dragging_;
}

void Shell::step_cursor_on_map(int dx, int dy)
{
    if (dx == 0 && dy == 0) {
        return;
    }
    const int tile = std::max(1, map_.tile_size());
    const double map_left = snapped_map_origin(view_x_, map_ox_, scroll_x_);
    const double map_top = snapped_map_origin(view_y_, map_oy_, scroll_y_);
    const double shake_x = map_.shake_x();
    const double shake_y = map_.shake_y();
    const double view_r = static_cast<double>(view_x_) + view_w_;
    const double view_b = static_cast<double>(view_y_) + view_h_;
    const double local_l = std::max(0.0, static_cast<double>(view_x_) - map_left - shake_x);
    const double local_t = std::max(0.0, static_cast<double>(view_y_) - map_top - shake_y);
    const double local_r = std::min(static_cast<double>(map_.pixel_width()), view_r - map_left - shake_x);
    const double local_b = std::min(static_cast<double>(map_.pixel_height()), view_b - map_top - shake_y);
    if (local_r <= local_l || local_b <= local_t) {
        return;
    }
    const int x0 = std::max(0, std::min(CitySession::kWorldW - 1, static_cast<int>(std::floor(local_l / tile))));
    const int y0 = std::max(0, std::min(CitySession::kWorldH - 1, static_cast<int>(std::floor(local_t / tile))));
    const int x1 = std::max(0, std::min(CitySession::kWorldW - 1, static_cast<int>(std::floor((local_r - 0.001) / tile))));
    const int y1 = std::max(0, std::min(CitySession::kWorldH - 1, static_cast<int>(std::floor((local_b - 0.001) / tile))));
    if (x1 < x0 || y1 < y0) {
        return;
    }
    const double vis_left = std::max(static_cast<double>(view_x_), map_left + shake_x);
    const double vis_top = std::max(static_cast<double>(view_y_), map_top + shake_y);
    const double vis_right = std::min(view_r, map_left + shake_x + map_.pixel_width());
    const double vis_bottom = std::min(view_b, map_top + shake_y + map_.pixel_height());
    int tx = 0;
    int ty = 0;
    const bool on = map_tile_at(map_local_x(cursor_x_), map_local_y(cursor_y_), map_.shake_x(), map_.shake_y(), tile,
                                CitySession::kWorldW, CitySession::kWorldH, tx, ty);
    auto place = [&](int index, double origin, double shake, double vis0, double vis1, float &cursor) {
        const double t0 = origin + shake + static_cast<double>(index) * tile;
        const double t1 = t0 + tile;
        const double hit0 = std::max(t0, vis0);
        const double hit1 = std::min(t1, vis1);
        if (hit1 <= hit0 + 0.25) {
            return false;
        }
        cursor = static_cast<float>(hit0 + (hit1 - hit0) * 0.5);
        return true;
    };
    auto nearest = [&](int dir, int first, int last, double origin, double shake, float cursor_at) {
        if (dir > 0) {
            for (int i = first; i <= last; ++i) {
                const double edge = origin + shake + static_cast<double>(i + 1) * tile;
                if (edge > static_cast<double>(cursor_at) + 0.5) {
                    return i;
                }
            }
        } else if (dir < 0) {
            for (int i = last; i >= first; --i) {
                const double edge = origin + shake + static_cast<double>(i) * tile;
                if (edge < static_cast<double>(cursor_at) - 0.5) {
                    return i;
                }
            }
        }
        return -1;
    };
    bool moved = false;
    if (dx != 0) {
        const int nx = on ? tx + dx : nearest(dx, x0, x1, map_left, shake_x, cursor_x_);
        if (nx >= x0 && nx <= x1 && !(on && nx == tx) &&
            place(nx, map_left, shake_x, vis_left, vis_right, cursor_x_)) {
            moved = true;
        }
    }
    if (dy != 0) {
        const int ny = on ? ty + dy : nearest(dy, y0, y1, map_top, shake_y, cursor_y_);
        if (ny >= y0 && ny <= y1 && !(on && ny == ty) &&
            place(ny, map_top, shake_y, vis_top, vis_bottom, cursor_y_)) {
            moved = true;
        }
    }
    if (moved) {
        clamp_gamepad_cursor(static_cast<float>(dx), static_cast<float>(dy));
    }
}

void Shell::advance_pad_drag(int dx, int dy, int elapsed_ms)
{
    if (!pad_dragging_ || dialog_ != Dialog::None || menu_.open()) {
        return;
    }
    if (elapsed_ms < 0) {
        elapsed_ms = 0;
    }
    int steps = 0;
    if (dx == 0 && dy == 0) {
        pad_held_dx_ = 0;
        pad_held_dy_ = 0;
        pad_feel::repeat_clear(drag_repeat_);
    } else if (!drag_repeat_.held || pad_held_dx_ != dx || pad_held_dy_ != dy) {
        pad_held_dx_ = dx;
        pad_held_dy_ = dy;
        pad_feel::repeat_arm(drag_repeat_);
        steps = 1;
    } else {
        steps = pad_feel::repeat_tick(drag_repeat_, elapsed_ms);
    }
    if (steps > 0) {
        const float tile = static_cast<float>(std::max(map_.tile_size(), 8));
        cursor_x_ = static_cast<float>(std::max(0.0, std::min<double>(win_w_ - 1, cursor_x_ + steps * dx * tile)));
        cursor_y_ = static_cast<float>(std::max(0.0, std::min<double>(win_h_ - 1, cursor_y_ + steps * dy * tile)));
        if (cursor_x_ < static_cast<float>(view_x_)) {
            cursor_x_ = static_cast<float>(view_x_);
        }
        if (cursor_y_ < static_cast<float>(view_y_)) {
            cursor_y_ = static_cast<float>(view_y_);
        }
        clamp_gamepad_cursor(static_cast<float>(dx), static_cast<float>(dy));
    }
    pad_repeat_ms_ = drag_repeat_.remaining_ms;
}

void Shell::service_gamepad(int elapsed_ms)
{
    if (elapsed_ms < 0) {
        elapsed_ms = 0;
    }
    if (pad_ != nullptr) {
        SDL_UpdateGamepads();
    }
    if (y_hold_fresh_) {
        // The frame that saw the press may include a long idle wait. That
        // time is not part of the hold. The up event clears the hold, so this
        // does not depend on the pad driver repeating the button.
        y_hold_fresh_ = false;
    } else if (y_hold_) {
        y_hold_ms_ += elapsed_ms;
        if (!y_hold_fired_ && y_hold_ms_ >= pad_feel::kSpeedHoldMs && dialog_ == Dialog::None && !menu_.open()) {
            cycle_speed();
            note_speed_feedback();
            y_hold_fired_ = true;
        }
    }
    if (shoulder_hold_fresh_) {
        // Same as the Y hold: the press frame's idle wait is not hold time.
        shoulder_hold_fresh_ = false;
    } else if (shoulder_hold_dir_ != 0) {
        if (dialog_ != Dialog::None || menu_.open()) {
            shoulder_hold_dir_ = 0;
            shoulder_preview_ = -1;
        } else {
            shoulder_hold_ms_ += elapsed_ms;
            // First section jump after 450 ms, then one every 300 ms.
            // The current tool stays put until the shoulder is released.
            const int due = 450 + shoulder_hold_steps_ * 300;
            if (shoulder_hold_ms_ >= due) {
                const int from = shoulder_preview_ >= 0 ? shoulder_preview_ : tool_index_;
                shoulder_preview_ = tool_groups::section_jump(from, shoulder_hold_dir_, kToolCount);
                ++shoulder_hold_steps_;
            }
        }
    }
    if (pad_dragging_ && pad_ != nullptr && dialog_ == Dialog::None && !menu_.open()) {
        int dx = 0;
        int dy = 0;
        if (SDL_GetGamepadButton(pad_, SDL_GAMEPAD_BUTTON_DPAD_LEFT)) {
            dx -= 1;
        }
        if (SDL_GetGamepadButton(pad_, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) {
            dx += 1;
        }
        if (SDL_GetGamepadButton(pad_, SDL_GAMEPAD_BUTTON_DPAD_UP)) {
            dy -= 1;
        }
        if (SDL_GetGamepadButton(pad_, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) {
            dy += 1;
        }
        advance_pad_drag(dx, dy, elapsed_ms);
    } else if (!pad_dragging_) {
        pad_held_dx_ = 0;
        pad_held_dy_ = 0;
        pad_feel::repeat_clear(drag_repeat_);
    }
    service_stick_nav(elapsed_ms);
    if (!pad_dragging_ && nav_repeat_.held && pad_ != nullptr &&
        nav_repeat_button_ != SDL_GAMEPAD_BUTTON_INVALID) {
        if (!SDL_GetGamepadButton(pad_, nav_repeat_button_)) {
            nav_repeat_button_ = SDL_GAMEPAD_BUTTON_INVALID;
            pad_feel::repeat_clear(nav_repeat_);
            nav_repeat_fresh_ = false;
        } else if (nav_repeat_fresh_) {
            nav_repeat_fresh_ = false;
        } else {
            const int steps = pad_feel::repeat_tick(nav_repeat_, elapsed_ms);
            for (int i = 0; i < steps; ++i) {
                apply_pad_direction(nav_repeat_button_);
            }
        }
    }
}

void Shell::match_renderer_output()
{
    if (renderer_ == nullptr || window_ == nullptr || win_w_ < 1 || win_h_ < 1) {
        return;
    }
    int pixel_w = 0;
    int pixel_h = 0;
    SDL_GetWindowSizeInPixels(window_, &pixel_w, &pixel_h);
    // Recreating while SDL still believes the window is the screen builds
    // another screen-sized renderer. Wait until the window size itself is back.
    if (std::abs(pixel_w - win_w_) > 2 || std::abs(pixel_h - win_h_) > 2) {
        return;
    }
    int output_w = 0;
    int output_h = 0;
    if (!SDL_GetRenderOutputSize(renderer_, &output_w, &output_h)) {
        return;
    }
    if (output_w == win_w_ && output_h == win_h_) {
        return;
    }
    if (texture_ != nullptr) {
        SDL_DestroyTexture(texture_);
        texture_ = nullptr;
        tex_w_ = 0;
        tex_h_ = 0;
    }
    SDL_Renderer *replacement = SDL_CreateRenderer(window_, nullptr);
    if (replacement == nullptr) {
        return;
    }
    SDL_DestroyRenderer(renderer_);
    renderer_ = replacement;
    SDL_SetRenderVSync(renderer_, 1);
}

bool Shell::windowed_frame_current() const
{
    if (window_ == nullptr || !normal_valid_ || normal_w_ < 320 || normal_h_ < 240) {
        return false;
    }
    int ww = 0;
    int wh = 0;
    int x = 0;
    int y = 0;
    int renderer_w = 0;
    int renderer_h = 0;
    SDL_GetWindowSize(window_, &ww, &wh);
    SDL_GetWindowPosition(window_, &x, &y);
    if (renderer_ != nullptr) {
        SDL_GetRenderOutputSize(renderer_, &renderer_w, &renderer_h);
    }
    if (fullscreen_exit_needs_drawable_refresh(ww, wh, renderer_w, renderer_h, normal_w_, normal_h_, windowed_density_)) {
        return false;
    }
    return !fullscreen_exit_position_is_spurious(x, y, normal_x_, normal_y_);
}

void Shell::settle_windowed_frame()
{
    if (window_ == nullptr || !normal_valid_ || normal_w_ < 320 || normal_h_ < 240) {
        return;
    }
    // SDL_SyncWindow waits out its whole timeout when the restored size does
    // not match the size it still expects from fullscreen. Several of those
    // waits kept the previous frame on screen for about a second. Pump until
    // the change is visible instead, and only for a short moment.
    auto pump_for = [](Uint64 budget_ms, const auto &done) {
        const Uint64 end = SDL_GetTicks() + budget_ms;
        do {
            SDL_PumpEvents();
            if (done()) {
                return true;
            }
            if (SDL_GetTicks() >= end) {
                break;
            }
            SDL_Delay(1);
        } while (true);
        return done();
    };
    if ((SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) != 0) {
        pump_for(20, [&]() { return (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) == 0; });
    }
    if ((SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) != 0) {
        return;
    }
    int ww = 0;
    int wh = 0;
    SDL_GetWindowSize(window_, &ww, &wh);
    const bool size_back = std::abs(ww - normal_w_) <= 2 && std::abs(wh - normal_h_) <= 2;
    int x = 0;
    int y = 0;
    SDL_GetWindowPosition(window_, &x, &y);
    const bool position_back = !fullscreen_exit_position_is_spurious(x, y, normal_x_, normal_y_);
    if (size_back && position_back) {
        // The frame is back. A renderer that is still the screen size is
        // rebuilt by the caller; another resize would only flash.
        return;
    }
    // A same-size resize does not emit a configure once the window manager
    // has already restored the frame. Change it, wait until that change is
    // visible, then put the real size and position back.
    SDL_SetWindowSize(window_, normal_w_ + 1, normal_h_);
    pump_for(20, [&]() {
        int live_w = 0;
        int live_h = 0;
        SDL_GetWindowSize(window_, &live_w, &live_h);
        return live_w == normal_w_ + 1 && live_h == normal_h_;
    });
    SDL_SetWindowSize(window_, normal_w_, normal_h_);
    SDL_SetWindowPosition(window_, normal_x_, normal_y_);
    pump_for(20, [&]() {
        int live_w = 0;
        int live_h = 0;
        int live_x = 0;
        int live_y = 0;
        SDL_GetWindowSize(window_, &live_w, &live_h);
        SDL_GetWindowPosition(window_, &live_x, &live_y);
        return std::abs(live_w - normal_w_) <= 2 && std::abs(live_h - normal_h_) <= 2 &&
               !fullscreen_exit_position_is_spurious(live_x, live_y, normal_x_, normal_y_);
    });
}

void Shell::toggle_fullscreen()
{
    if (window_ == nullptr) {
        return;
    }
    const bool full = (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) != 0;
    if (!full) {
        note_window_geometry();
        restoring_windowed_ = false;
        fullscreen_hold_frames_ = 0;
        SDL_SetWindowFullscreen(window_, true);
        // A late configure must not leave the windowed layout in the corner
        // of the screen. Read the pixel size until it leaves that rectangle.
        const Uint64 end = SDL_GetTicks() + 40;
        do {
            SDL_PumpEvents();
            int pw = 0;
            int ph = 0;
            SDL_GetWindowSizeInPixels(window_, &pw, &ph);
            if (pw > normal_w_ + 48 || ph > normal_h_ + 48) {
                break;
            }
            if (SDL_GetTicks() >= end) {
                break;
            }
            SDL_Delay(1);
        } while (true);
        layout();
        match_renderer_output();
        save_window_settings();
        return;
    }
    windowed_settle_tries_ = 0;
    restoring_windowed_ = normal_valid_;
    SDL_SetWindowFullscreen(window_, false);
    settle_windowed_frame();
    layout();
    match_renderer_output();
    save_window_settings();
}

void Shell::finish_year_and_close(bool jump)
{
    int tile_x = 0;
    int tile_y = 0;
    bool jump_ok = false;
    if (jump) {
        const civic::YearDigest digest = year_digest();
        jump_ok = digest.locate;
        tile_x = digest.locate_x;
        tile_y = digest.locate_y;
    }
    session_.set_tax(tax_);
    session_.set_road_funding(road_);
    session_.set_police_funding(police_);
    session_.set_fire_funding(fire_);
    if (budget_is_tax_) {
        const auto book = session_.budget();
        queue_milestones(session_.note_civic_year(book.cash_flow));
        session_.remember_civic_year();
    }
    session_.finish_budget_edit();
    budget_is_tax_ = false;
    budget_cover_ = false;
    tax_pause_hold_ = false;
    close_dialog();
    if (jump_ok) {
        center_on_fraction((tile_x + 0.5) / CitySession::kWorldW, (tile_y + 0.5) / CitySession::kWorldH);
        // The camera clamps at the edge, so the tile may not be centered.
        // The same blink used by auto-goto keeps the target obvious.
        marker_tx_ = tile_x;
        marker_ty_ = tile_y;
        marker_ms_ = 0;
        marker_on_ = true;
    }
}

void Shell::toggle_overlay(CitySession::MapLayer layer)
{
    if (overlay_on_ && overlay_.layer() == layer) {
        overlay_on_ = false;
        return;
    }
    overlay_.set_layer(layer);
    overlay_on_ = true;
    overlay_.sync();
}

void Shell::reroll_terrain()
{
    const unsigned salt = static_cast<unsigned>(SDL_GetTicks()) * 1103515245u + 12345u;
    const int seed = 1000 + static_cast<int>(salt % 900000);
    field_b_ = std::to_string(seed);
    terrain_key_ = 0;
}

void Shell::jump_notice(int index)
{
    const auto &notes = session_.notices();
    if (index < 0 || index >= static_cast<int>(notes.size())) {
        return;
    }
    const CitySession::CityNotice &note = notes[static_cast<std::size_t>(index)];
    if (!note.has_place) {
        return;
    }
    center_on_fraction((note.tile_x + 0.5) / CitySession::kWorldW, (note.tile_y + 0.5) / CitySession::kWorldH);
    if (dialog_ == Dialog::Messages) {
        close_dialog();
    }
}

void Shell::arm_touch_tool()
{
    if (!touch_pending_ || touch_armed_) {
        return;
    }
    touch_armed_ = true;
    touch_pending_ = false;
    pointer_on_map_ = true;
    pointer_down_ = true;
    map_.press(map_local_x(touch_down_x_), map_local_y(touch_down_y_));
    map_.motion(map_local_x(pointer_x_), map_local_y(pointer_y_), true);
}

void Shell::end_mini_drag()
{
    if (!mini_drag_) {
        return;
    }
    mini_drag_ = false;
    if (!pointer_on_map_ && !pan_pointer_) {
        SDL_CaptureMouse(false);
    }
}

void Shell::escape_to_inspect()
{
    if (pad_dragging_ || pointer_on_map_) {
        const bool mouse_stroke = pointer_on_map_ && !pad_dragging_;
        // Tiles already placed stay; the stroke just ends here.
        release_pad_drag();
        if (mouse_stroke) {
            SDL_CaptureMouse(false);
        }
    }
    select_tool(kToolCount - 1);
}

void Shell::reset_touch_state()
{
    if (touch_armed_) {
        map_.release();
        pointer_on_map_ = false;
        pointer_down_ = false;
    }
    session_.end_tool_stroke();
    touch_pending_ = false;
    touch_armed_ = false;
    touch_minimap_ = false;
    touch_panning_ = false;
    touch_slider_id_ = 0;
    slider_drag_ = 0;
    pinch_dist_ = 0;
    list_touch_y_ = -1.0f;
    license_touch_y_ = -1.0f;
    list_touch_row_ = false;
    list_touch_dragged_ = false;
    list_dragging_ = false;
    fingers_.clear();
}

void Shell::load_recent()
{
    recent_.clear();
    const std::string dir = city_config_dir();
    if (dir.empty()) {
        return;
    }
    std::ifstream in(dir + "/recent.txt");
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
            line.pop_back();
        }
        if (path_is_absolute(line)) {
            recent_.push_back(line);
        }
    }
    menu_.recent_count = static_cast<int>(recent_.size());
}

void Shell::remember_recent(const std::string &path)
{
    if (!path_is_absolute(path)) {
        return;
    }
    recent_.erase(std::remove(recent_.begin(), recent_.end(), path), recent_.end());
    recent_.insert(recent_.begin(), path);
    if (recent_.size() > 10) {
        recent_.resize(10);
    }
    menu_.recent_count = static_cast<int>(recent_.size());
    const std::string dir = city_config_dir();
    if (dir.empty()) {
        return;
    }
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    std::string body;
    for (const std::string &item : recent_) {
        body += item;
        body += '\n';
    }
    if (!write_text_atomic(dir + "/recent.txt", body)) {
        note_background_write_failed("Could not update the recent-cities list.");
    }
}

void Shell::retarget_recent(const std::string &path, const std::string &replacement)
{
    if (path.empty()) {
        return;
    }
    bool changed = false;
    std::vector<std::string> next;
    next.reserve(recent_.size());
    for (const std::string &item : recent_) {
        bool same = item == path;
        if (!same) {
            std::error_code cmp;
            same = std::filesystem::equivalent(item, path, cmp);
        }
        if (!same) {
            next.push_back(item);
            continue;
        }
        changed = true;
        if (!replacement.empty()) {
            next.push_back(replacement);
        }
    }
    if (!changed) {
        return;
    }
    recent_.clear();
    for (const std::string &item : next) {
        if (std::find(recent_.begin(), recent_.end(), item) == recent_.end()) {
            recent_.push_back(item);
        }
    }
    menu_.recent_count = static_cast<int>(recent_.size());
    const std::string dir = city_config_dir();
    if (dir.empty()) {
        return;
    }
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    std::string body;
    for (const std::string &item : recent_) {
        body += item;
        body += '\n';
    }
    if (!write_text_atomic(dir + "/recent.txt", body)) {
        note_background_write_failed("Could not update the recent-cities list.");
    }
}

void Shell::drop_recovery()
{
    city_recovery::remove(own_recovery_);
    own_recovery_.clear();
    autosave_fingerprint_.clear();
}

void Shell::autosave(bool force)
{
    if (!session_.needs_save_prompt()) {
        return;
    }
    const std::string fingerprint = session_.change_fingerprint();
    if (!force && fingerprint == autosave_fingerprint_ && !own_recovery_.empty()) {
        return;
    }
    if (own_recovery_.empty()) {
        own_recovery_ = city_recovery::new_recovery_path();
        if (own_recovery_.empty()) {
            note_background_write_failed("Autosave is off. This city will not be recovered if the game closes.");
            return;
        }
    }
    if (!session_.write_recovery(own_recovery_)) {
        note_background_write_failed("Autosave failed. This city will not be recovered if the game closes.");
        return;
    }
    // The view and the NEWS log ride along so Recover does not reset them.
    std::vector<city_recovery::Entry::News> news;
    for (const CitySession::CityNotice &notice : session_.notices()) {
        city_recovery::Entry::News line;
        line.text = notice.text;
        line.tile_x = notice.tile_x;
        line.tile_y = notice.tile_y;
        line.month = notice.month;
        line.year = notice.year;
        line.has_place = notice.has_place;
        news.push_back(line);
    }
    const double cx = map_point_fraction(view_x_ + view_w_ * 0.5, view_x_, map_ox_, scroll_x_, map_.pixel_width());
    const double cy = map_point_fraction(view_y_ + view_h_ * 0.5, view_y_, map_oy_, scroll_y_, map_.pixel_height());
    city_recovery::write_info(own_recovery_, session_.city_name(), session_.save_path(), session_.date_text(),
                              map_.tile_size(), std::clamp(cx, 0.0, 1.0), std::clamp(cy, 0.0, 1.0), news);
    autosave_fingerprint_ = fingerprint;
}

void Shell::remember_saved(const std::string &path)
{
    remember_city_folder(path);
    remember_recent(path);
    drop_recovery();
    sync_title();
}

void Shell::finish_save(const std::string &path)
{
    if (path.empty()) {
        return;
    }
    if (save_generation_ != city_generation_) {
        info_title_ = "Save City";
        info_body_ = "The city changed while the Save dialog was open. Nothing was saved.";
        open_dialog(Dialog::Info);
        return;
    }
    note_view_for_save();
    if (!session_.save_city_as(path)) {
        info_title_ = "Save City";
        info_body_ = session_.message().empty() ? "Could not save the city." : session_.message();
        // A failed save must not leave Quit (or New/Load) armed for the next Ctrl+S.
        after_save_ = AfterSave::Nothing;
        open_dialog(Dialog::SaveError);
        return;
    }
    remember_saved(path);
    const AfterSave next = after_save_;
    after_save_ = AfterSave::Nothing;
    if (next != AfterSave::Nothing) {
        proceed(next);
    }
}

void Shell::finish_open(const std::string &path)
{
    const bool from_welcome = welcome_gate_;
    reset_touch_state();
    clear_transient_message();
    if (path.empty() || !session_.load_city(path)) {
        std::string reason = session_.last_load_error();
        if (reason.empty()) {
            reason = path.empty() ? "That file has no local path." : CitySession::load_problem(path);
        }
        if (reason.empty()) {
            reason = "The file could not be read as a city. It may be damaged.";
        }
        info_title_ = "Could not load city";
        info_body_ = reason + "\n\n" + path;
        if (!city_chosen_) {
            welcome_gate_ = true;
        }
        open_dialog(Dialog::LoadError);
        sync_title();
        return;
    }
    welcome_gate_ = false;
    city_chosen_ = true;
    ++city_generation_;
    center_pad_cursor();
    const int loaded_speed = session_.speed();
    restore_play_preferences();
    if (background_held_) {
        // The file's own speed wins over the speed from before the window
        // was covered. restore_play_preferences may have stored the preference.
        adopt_loaded_speed(loaded_speed);
    }
    restore_loaded_view();
    const std::string folder = parent_dir(path);
    if (!folder.empty()) {
        remember_city_folder(folder);
    }
    remember_recent(path);
    sync_title();
    if (dialog_ == Dialog::Welcome) {
        close_dialog();
    }
    if (from_welcome) {
        begin_scene_fade();
    }
}

void Shell::refresh_files()
{
    files_.clear();
    std::error_code ec;
    if (files_dir_.empty() || !std::filesystem::is_directory(files_dir_, ec)) {
        const std::string remembered = remembered_city_folder();
        if (!remembered.empty()) {
            files_dir_ = remembered;
        } else {
            files_dir_ = default_cities_dir();
        }
    }
    if (!files_dir_.empty() && !std::filesystem::is_directory(files_dir_, ec) && !headless_ && !smoke_ && !ui_test_ &&
        !review_) {
        std::filesystem::create_directories(files_dir_, ec);
    }
    if (std::filesystem::is_directory(files_dir_, ec)) {
        for (const auto &entry : std::filesystem::directory_iterator(files_dir_, ec)) {
            if (ec) {
                break;
            }
            const std::string name = entry.path().filename().string();
            if (name.empty() || name == "." || name == ".." || file_name_hidden(name)) {
                continue;
            }
            if (entry.is_directory(ec)) {
                files_.push_back(name + "/");
            } else if (ends_with_ci(name, ".cty")) {
                files_.push_back(name);
            }
        }
    }
    std::sort(files_.begin(), files_.end(), file_row_less);
    if (list_index_ >= static_cast<int>(files_.size())) {
        list_index_ = 0;
    }
    if (list_index_ < 0) {
        list_index_ = 0;
    }
    reveal_file_list();
}

void Shell::begin_files(bool save)
{
    files_save_ = save;
    save_generation_ = city_generation_;
    const std::string current = session_.save_path();
    if (!current.empty()) {
        files_dir_ = parent_dir(current);
    } else {
        files_dir_ = remembered_city_folder();
    }
    list_index_ = 0;
    list_scroll_ = 0;
    refresh_files();
    open_dialog(Dialog::Files);
}

void Shell::begin_load()
{
    if (deck_ || headless_ || gamepad_nav_) {
        begin_files(false);
        return;
    }
    begin_system_files(false);
}

void Shell::begin_save_as()
{
    if (deck_ || headless_ || gamepad_nav_) {
        begin_files(true);
        return;
    }
    begin_system_files(true);
}

void Shell::begin_system_files(bool save)
{
    files_save_ = save;
    save_generation_ = city_generation_;
    {
        std::lock_guard<std::mutex> lock(file_mu_);
        file_ignore_ = false;
        file_ready_ = false;
        file_state_ = 0;
        file_path_.clear();
    }
    std::string folder = session_.save_path().empty() ? remembered_city_folder() : parent_dir(session_.save_path());
    static const SDL_DialogFileFilter filters[] = {{"City files", "cty"}, {"All files", "*"}};
    open_dialog(Dialog::FileWait);
    if (save) {
        SDL_ShowSaveFileDialog(&Shell::file_callback, this, window_, filters, 2,
                               folder.empty() ? nullptr : folder.c_str());
    } else {
        SDL_ShowOpenFileDialog(&Shell::file_callback, this, window_, filters, 2,
                               folder.empty() ? nullptr : folder.c_str(), false);
    }
}

void Shell::poll_file_dialog()
{
    if (dialog_ != Dialog::FileWait) {
        return;
    }
    std::string path;
    int state = 0;
    {
        std::lock_guard<std::mutex> lock(file_mu_);
        if (file_ignore_ || !file_ready_) {
            file_ready_ = false;
            return;
        }
        state = file_state_;
        path = file_path_;
        file_ready_ = false;
    }
    close_dialog();
    if (state == 1) {
        if (files_save_) {
            finish_save(with_cty_suffix(path));
        } else {
            finish_open(path);
        }
        return;
    }
    if (state == 3) {
        begin_files(files_save_);
        return;
    }
    // The player dismissed the system dialog. Do not quit on a later save.
    after_save_ = AfterSave::Nothing;
    if (welcome_gate_ && !files_save_ && dialog_ == Dialog::None) {
        open_dialog(Dialog::Welcome);
    }
}

void Shell::proceed(AfterSave next)
{
    switch (next) {
    case AfterSave::NewCity:
        open_dialog(Dialog::NewCity);
        break;
    case AfterSave::Load:
        begin_load();
        break;
    case AfterSave::FilesLoad:
        begin_files(false);
        break;
    case AfterSave::Scenario:
        scenario_index_ = 0;
        list_index_ = 0;
        list_scroll_ = 0;
        open_dialog(Dialog::Scenario);
        break;
    case AfterSave::Quit:
        drop_recovery();
        quitting_ = true;
        break;
    case AfterSave::OpenPath:
        finish_open(queued_path_);
        queued_path_.clear();
        break;
    case AfterSave::Recover:
        if (!session_.load_recovery(recovery_.path, recovery_.origin)) {
            info_title_ = "Recover city";
            info_body_ = session_.last_load_error().empty() ? "The recovery file could not be read."
                                                            : session_.last_load_error();
            open_dialog(Dialog::Info);
            break;
        }
        own_recovery_ = recovery_.path;
        restore_play_preferences();
        welcome_gate_ = false;
        city_chosen_ = true;
        ++city_generation_;
        center_pad_cursor();
        autosave_fingerprint_ = session_.change_fingerprint();
        set_speed(0);
        {
            // Put back the NEWS log and the view the autosave recorded.
            const city_recovery::Entry saved = city_recovery::read_entry(recovery_.path);
            std::vector<CitySession::CityNotice> notices;
            for (const city_recovery::Entry::News &line : saved.news) {
                CitySession::CityNotice notice;
                notice.text = line.text;
                notice.tile_x = line.tile_x;
                notice.tile_y = line.tile_y;
                notice.month = line.month;
                notice.year = line.year;
                notice.has_place = line.has_place;
                notices.push_back(notice);
            }
            if (!notices.empty()) {
                // Keep the "Recovered" line on top of the old log.
                std::vector<CitySession::CityNotice> merged = session_.notices();
                merged.insert(merged.end(), notices.begin(), notices.end());
                session_.restore_notices(std::move(merged));
            }
            if (saved.tile_size > 0) {
                map_.set_tile_size(opening_map_tile(handheld_, saved.tile_size));
                map_ox_ = map_letterbox_x();
                map_oy_ = map_letterbox_y();
                center_on_fraction(saved.center_x, saved.center_y);
            } else {
                center_on_fraction(0.5, 0.5);
            }
        }
        sync_title();
        break;
    case AfterSave::Nothing:
        break;
    }
}

void Shell::request_with_save(AfterSave next)
{
    if (!session_.needs_save_prompt()) {
        if (next == AfterSave::Quit || next == AfterSave::NewCity || next == AfterSave::Load ||
            next == AfterSave::Scenario || next == AfterSave::OpenPath) {
            drop_recovery();
        }
        proceed(next);
        return;
    }
    after_save_ = next;
    open_dialog(Dialog::Unsaved);
}

void Shell::offer_recovery()
{
    if (city_chosen_) {
        return;
    }
    const std::vector<city_recovery::Entry> entries = city_recovery::list(own_recovery_);
    if (entries.empty()) {
        return;
    }
    recovery_ = entries.front();
    recovery_left_ = static_cast<int>(entries.size()) - 1;
    open_dialog(Dialog::Recovery);
}

void Shell::apply_action(Action action)
{
    switch (action) {
    case Action::NewCity:
        request_with_save(AfterSave::NewCity);
        break;
    case Action::Load:
        request_with_save(AfterSave::Load);
        break;
    case Action::Recent:
        request_with_save(AfterSave::FilesLoad);
        break;
    case Action::Save:
        // The camera rides along in the sidecar of a save to a known file.
        note_view_for_save();
        if (session_.save_path().empty()) {
            // A Save from "Save changes?" has no file yet. Keep the pending
            // Quit / New / Load so it still runs after Save As succeeds.
            begin_save_as();
        } else if (!session_.save_city_as(session_.save_path())) {
            info_title_ = "Save City";
            info_body_ = session_.message().empty() ? "Could not save the city." : session_.message();
            after_save_ = AfterSave::Nothing;
            open_dialog(Dialog::SaveError);
        } else {
            remember_saved(session_.save_path());
            const AfterSave next = after_save_;
            after_save_ = AfterSave::Nothing;
            if (next != AfterSave::Nothing) {
                proceed(next);
            }
        }
        break;
    case Action::SaveAs:
        after_save_ = AfterSave::Nothing;
        begin_save_as();
        break;
    case Action::Scenario:
        request_with_save(AfterSave::Scenario);
        break;
    case Action::Rename:
        open_dialog(Dialog::Rename);
        break;
    case Action::Undo:
        // Release nov24 #18: the last build or bulldoze stroke.
        if (!welcome_gate_ && !terrain_edit_) {
            session_.undo_last_stroke();
        }
        break;
    case Action::Quit:
        // A saved city used to leave on one pad press. Ask first. An unsaved
        // city already gets the save prompt, which is the confirmation.
        if (gamepad_nav_ && !session_.needs_save_prompt()) {
            open_dialog(Dialog::QuitConfirm);
            break;
        }
        request_with_save(AfterSave::Quit);
        break;
    case Action::AutoBudget:
        session_.set_auto_budget(!session_.auto_budget());
        pref_auto_budget_ = session_.auto_budget() ? 1 : 0;
        save_window_settings();
        break;
    case Action::AutoBulldoze:
        session_.set_auto_bulldoze(!session_.auto_bulldoze());
        pref_auto_bulldoze_ = session_.auto_bulldoze() ? 1 : 0;
        save_window_settings();
        break;
    case Action::DisastersEnabled:
        session_.set_disasters(!session_.disasters());
        pref_disasters_ = session_.disasters() ? 1 : 0;
        save_window_settings();
        break;
    case Action::AutoGoto:
        session_.set_auto_goto(!session_.auto_goto());
        pref_auto_goto_ = session_.auto_goto() ? 1 : 0;
        save_window_settings();
        break;
    case Action::Mute:
        session_.set_sound_enabled(!session_.sound_enabled());
        sound_.set_muted(!session_.sound_enabled());
        pref_sound_ = session_.sound_enabled() ? 1 : 0;
        save_window_settings();
        break;
    case Action::SetPause:
        paused_from_ = speed_ > 0 ? speed_ : paused_from_;
        set_speed(0);
        save_window_settings();
        break;
    case Action::Slow:
        set_speed(1);
        save_window_settings();
        break;
    case Action::Medium:
        set_speed(2);
        save_window_settings();
        break;
    case Action::Fast:
        set_speed(3);
        save_window_settings();
        break;
    case Action::TogglePause:
        toggle_pause();
        save_window_settings();
        break;
    case Action::ZoomIn:
        zoom_by(2);
        break;
    case Action::ZoomOut:
        zoom_by(-2);
        break;
    case Action::ClassicLook:
        set_look_preference(Look::Classic);
        map_.invalidate_all();
        save_window_settings();
        break;
    case Action::SvgaLook:
        // A missing sheet stays on Classic for this session, and the menu
        // check follows the sheet that is drawn. The stored choice stays SVGA.
        set_look_preference(Look::Svga);
        map_.invalidate_all();
        save_window_settings();
        break;
    case Action::SpookyLook:
        // Spooky is off the menu for now. Nothing should send this; if
        // anything does, it is SVGA.
        set_look_preference(Look::Svga);
        map_.invalidate_all();
        save_window_settings();
        break;
    case Action::Fire:
    case Action::Flood:
    case Action::Tornado:
    case Action::Earthquake:
    case Action::Monster:
    case Action::Meltdown:
    case Action::Riot:
    case Action::AutonomousZone:
    case Action::LionEscape:
    case Action::PrisonEscape:
        disaster_ = action;
        open_dialog(Dialog::Disaster);
        break;
    case Action::Budget: {
        const auto book = session_.budget();
        tax_ = book.tax_percent;
        road_ = book.road_percent;
        police_ = book.police_percent;
        fire_ = book.fire_percent;
        open_tax_ = tax_;
        open_road_ = road_;
        open_police_ = police_;
        open_fire_ = fire_;
        budget_is_tax_ = session_.budget_pending();
        budget_cover_ = budget_is_tax_;
        if (budget_is_tax_) {
            tax_pause_hold_ = true;
        }
        open_dialog(Dialog::Budget);
        break;
    }
    case Action::Ledger:
        open_dialog(Dialog::Ledger);
        break;
    case Action::Graphs:
        graphs_long_ = false;
        open_dialog(Dialog::Graphs);
        break;
    case Action::Evaluation:
        session_.update_evaluation();
        open_dialog(Dialog::Evaluation);
        break;
    case Action::Power:
        toggle_overlay(CitySession::MapLayer::Power);
        break;
    case Action::Water:
        toggle_overlay(CitySession::MapLayer::Zones);
        break;
    case Action::Zones:
        toggle_overlay(CitySession::MapLayer::Zones);
        break;
    case Action::ZoneResidential:
        toggle_overlay(CitySession::MapLayer::Residential);
        break;
    case Action::ZoneCommercial:
        toggle_overlay(CitySession::MapLayer::Commercial);
        break;
    case Action::ZoneIndustrial:
        toggle_overlay(CitySession::MapLayer::Industrial);
        break;
    case Action::Transportation:
        toggle_overlay(CitySession::MapLayer::Transportation);
        break;
    case Action::Pollution:
        toggle_overlay(CitySession::MapLayer::Pollution);
        break;
    case Action::Crime:
        toggle_overlay(CitySession::MapLayer::Crime);
        break;
    case Action::LandValue:
        toggle_overlay(CitySession::MapLayer::LandValue);
        break;
    case Action::Traffic:
        toggle_overlay(CitySession::MapLayer::Traffic);
        break;
    case Action::PoliceMap:
        toggle_overlay(CitySession::MapLayer::Police);
        break;
    case Action::FireMap:
        toggle_overlay(CitySession::MapLayer::Fire);
        break;
    case Action::Population:
        toggle_overlay(CitySession::MapLayer::Population);
        break;
    case Action::Growth:
        toggle_overlay(CitySession::MapLayer::Growth);
        break;
    case Action::Messages:
        list_index_ = 0;
        list_scroll_ = 0;
        open_dialog(Dialog::Messages);
        break;
    case Action::Fullscreen:
        toggle_fullscreen();
        break;
    case Action::NightMode:
        night_ = !night_;
        map_.set_night(night_);
        save_window_settings();
        break;
    case Action::About:
        open_dialog(Dialog::About);
        break;
    case Action::Settings:
        open_dialog(Dialog::Settings);
        break;
    case Action::Controls:
        open_dialog(Dialog::Controls);
        break;
    case Action::RemapControls:
        open_dialog(Dialog::Bindings);
        break;
    case Action::HowToPlay:
        open_dialog(Dialog::HowToPlay);
        break;
    case Action::RecentIndex:
    case Action::None:
        break;
    }
}

void Shell::apply_skin()
{
    // The skin and the layout change the chrome around the map, so the
    // window is laid out again. The map art itself does not change.
    layout();
}

bool Shell::handheld_device() const
{
    static int cached = -1;
    if (cached < 0) {
        cached = 0;
#if defined(__linux__)
        if (std::getenv("LUNDUKE_CITY2_REVIEW") == nullptr && std::getenv("LUNDUKE_CITY2_UI_TEST") == nullptr) {
            std::ifstream in("/sys/class/dmi/id/product_name");
            std::string product;
            if (in && std::getline(in, product)) {
                cached = layout_mode::handheld_product(product) ? 1 : 0;
            }
        }
#endif
    }
    return cached == 1;
}

void Shell::on_dialog_button(int id)
{
    if (dialog_ == Dialog::Settings) {
        if (id == kReset) {
            pause_unfocused_ = !pause_unfocused_;
            return;
        }
        if (id == kScaleAuto) {
            scale_percent_ = scale_percent_ <= 0 ? 100 : 0;
            return;
        }
        if (id == kAutosave) {
            static const int steps[] = {60, 300, 600, 0};
            int index = 0;
            for (; index < 4; ++index) {
                if (steps[index] == autosave_seconds_) {
                    break;
                }
            }
            autosave_seconds_ = steps[(index + 1) % 4];
            return;
        }
        if (id == kWheelZoom) {
            wheel_zooms_ = !wheel_zooms_;
            return;
        }
        if (id == kShakeBox) {
            screen_shake_ = !screen_shake_;
            if (!screen_shake_) {
                map_.set_shake(0, 0);
            }
            return;
        }
        if (id == kSkinBox) {
            slate_ = !slate_;
            apply_skin();
            return;
        }
        if (id == kNightBox) {
            night_ = !night_;
            map_.set_night(night_);
            return;
        }
        if (id == kEffectsBox) {
            effects_ = (look_fx::clamp_effects(effects_) + 1) % look_fx::EffectsCount;
            apply_effects();
            return;
        }
        if (id == kMinimapBox) {
            minimap_engraved_ = !minimap_engraved_;
            return;
        }
        if (id == kLayoutBox) {
            layout_pref_ = (layout_mode::clamp_choice(layout_pref_) + 1) % 3;
            apply_skin();
            return;
        }
        if (id == kFullscreenBox) {
            toggle_fullscreen();
            return;
        }
        if (id == kCancel) {
            volume_ = open_volume_;
            scale_percent_ = open_scale_percent_;
            pause_unfocused_ = open_pause_unfocused_;
            wheel_zooms_ = open_wheel_zooms_;
            screen_shake_ = open_screen_shake_;
            autosave_seconds_ = open_autosave_;
            if (slate_ != open_slate_ || layout_pref_ != open_layout_pref_) {
                slate_ = open_slate_;
                layout_pref_ = open_layout_pref_;
                apply_skin();
            }
            night_ = open_night_;
            map_.set_night(night_);
            effects_ = open_effects_;
            apply_effects();
            minimap_engraved_ = open_minimap_engraved_;
            schedule_autosave();
            user_scale_ = scale_percent_ <= 0 ? 0.0f : static_cast<float>(scale_percent_) / 100.0f;
            sound_.set_volume(static_cast<float>(volume_) / 100.0f);
            if (window_ != nullptr) {
                const bool full = (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) != 0;
                if (full != open_fullscreen_) {
                    toggle_fullscreen();
                }
            }
            refresh_ui_scale();
            // Release nov24 #16: from Welcome, back to Welcome.
            dismiss_dialog();
            return;
        }
        if (id == kOk || id == kSave) {
            user_scale_ = scale_percent_ <= 0 ? 0.0f : static_cast<float>(scale_percent_) / 100.0f;
            sound_.set_volume(static_cast<float>(volume_) / 100.0f);
            refresh_ui_scale();
            schedule_autosave();
            save_window_settings();
            dismiss_dialog();
            return;
        }
    }
    if (dialog_ == Dialog::Controls) {
        if (id == kControlsPrev || id == kControlsNext || id == kPageDot0 || id == kPageDot1 || id == kPageDot2) {
            int page = controls_page_;
            if (id == kControlsNext) {
                page = (controls_page_ + 1) % 3;
            } else if (id == kControlsPrev) {
                page = (controls_page_ + 2) % 3;
            } else if (id == kPageDot0) {
                page = deck_ ? 1 : 0;
            } else if (id == kPageDot1) {
                page = deck_ ? 0 : 1;
            } else {
                page = 2;
            }
            // Page dots are the visible page (keyboard, gamepad, touch), which
            // a Deck session shows in a different order.
            if (id == kPageDot0 || id == kPageDot1 || id == kPageDot2) {
                const int shown = id == kPageDot0 ? 0 : id == kPageDot1 ? 1 : 2;
                if (!deck_) {
                    page = shown;
                } else if (shown == 0) {
                    page = 1;
                } else if (shown == 1) {
                    page = 0;
                } else {
                    page = 2;
                }
            }
            controls_page_ = page;
            controls_scroll_ = 0;
            return;
        }
        if (id == kRemap) {
            open_dialog(Dialog::Bindings);
            return;
        }
        close_dialog();
        return;
    }
    if (dialog_ == Dialog::Bindings) {
        if (id == kReset) {
            binds_ = default_binds();
            dialog_error_.clear();
            bind_listen_ = false;
            return;
        }
        if (id == kCancel) {
            binds_ = binds_undo_;
            bind_listen_ = false;
            close_dialog();
            return;
        }
        if (id == kOk) {
            bind_listen_ = false;
            save_window_settings();
            close_dialog();
            return;
        }
        return;
    }
    if (dialog_ == Dialog::FileRename) {
        if (id == kRename || id == kOk) {
            const SaveFileResult renamed = rename_city_file(files_dir_, file_target_, field_a_);
            if (!renamed.ok) {
                dialog_error_ = renamed.error;
                return;
            }
            const std::string target = save_rename_target(field_a_);
            const std::filesystem::path previous =
                files_dir_.empty() ? std::filesystem::path(file_target_) : std::filesystem::path(files_dir_) / file_target_;
            const std::filesystem::path next =
                target.empty() || files_dir_.empty() ? std::filesystem::path(target) : std::filesystem::path(files_dir_) / target;
            if (!session_.save_path().empty() && session_.save_path() == previous.string() && !target.empty()) {
                session_.set_save_path(next.string());
            }
            if (!target.empty()) {
                retarget_recent(previous.string(), next.string());
            }
            return_to_file_list();
            return;
        }
        return_to_file_list();
        return;
    }
    if (dialog_ == Dialog::FileDelete) {
        if (id == kDelete || id == kOk) {
            const SaveFileResult removed = delete_city_file(files_dir_, file_target_);
            if (!removed.ok) {
                dialog_error_ = removed.error;
                return;
            }
            const std::string gone =
                (files_dir_.empty() ? std::filesystem::path(file_target_) : std::filesystem::path(files_dir_) / file_target_)
                    .string();
            retarget_recent(gone, "");
            return_to_file_list();
            return;
        }
        return_to_file_list();
        return;
    }
    if (dialog_ == Dialog::Welcome) {
        if (id == kWelcomeNew) {
            open_dialog(Dialog::NewCity);
            return;
        }
        if (id == kWelcomeLoad) {
            begin_load();
            return;
        }
        if (id == kWelcomeScenario) {
            scenario_index_ = 0;
            list_index_ = 0;
            list_scroll_ = 0;
            open_dialog(Dialog::Scenario);
            return;
        }
        if (id == kWelcomeContinue) {
            if (!recent_.empty()) {
                queued_path_ = recent_.front();
                request_with_save(AfterSave::OpenPath);
            }
            return;
        }
        if (id == kWelcomeHowTo) {
            open_dialog(Dialog::HowToPlay);
            return;
        }
        if (id == kWelcomeSettings) {
            // Release nov24 #16: closing Settings returns to Welcome.
            open_dialog(Dialog::Settings);
            return;
        }
        if (id == kWelcomeLearn) {
            // Release nov24 #7: straight into Brookside, as Play does from
            // the picker's (now first) Learn to Play row.
            close_dialog();
            start_scenario(SC_LEARN_TO_PLAY);
            return;
        }
        // Quit, B, and Escape ask first. A second B from New City or the
        // scenario list must not leave the game.
        open_dialog(Dialog::QuitConfirm);
        return;
    }
    if (dialog_ == Dialog::TerrainDiscard) {
        // Close the question first: the editor is under it either way.
        close_dialog();
        if (id == kDiscard) {
            close_terrain_editor(false);
        }
        return;
    }
    if (dialog_ == Dialog::TerrainLowLand) {
        // Keep editing, or keep the mostly-water map as it is.
        close_dialog();
        if (id == kOk) {
            close_terrain_editor(true);
        }
        return;
    }
    if (dialog_ == Dialog::BulldozeConfirm) {
        const int x = bulldoze_x_;
        const int y = bulldoze_y_;
        bulldoze_x_ = -1;
        bulldoze_y_ = -1;
        if (id == kOk) {
            close_dialog();
            if (x >= 0 && y >= 0) {
                session_.use_tool(TOOL_BULLDOZER, x, y);
                session_.end_tool_stroke();
            }
            return;
        }
        dismiss_dialog();
        return;
    }
    if (dialog_ == Dialog::QuitConfirm) {
        if (id == kOk) {
            welcome_gate_ = false;
            close_dialog();
            request_with_save(AfterSave::Quit);
            return;
        }
        dismiss_dialog();
        return;
    }
    if (id == kSystemFiles) {
        const bool save = files_save_;
        close_dialog();
        begin_system_files(save);
        return;
    }
    if (id == kUp) {
        files_dir_ = parent_dir(files_dir_);
        list_index_ = 0;
        list_scroll_ = 0;
        refresh_files();
        return;
    }
    if (id == kShortScale) {
        graphs_long_ = false;
        return;
    }
    if (id == kLongScale) {
        graphs_long_ = true;
        return;
    }
    if (dialog_ == Dialog::Unsaved) {
        if (id == kSave) {
            close_dialog();
            apply_action(Action::Save);
            return;
        }
        if (id == kDiscard) {
            session_.discard_pending_budget();
            drop_recovery();
            const AfterSave next = after_save_;
            after_save_ = AfterSave::Nothing;
            close_dialog();
            proceed(next);
            return;
        }
        after_save_ = AfterSave::Nothing;
        dismiss_dialog();
        return;
    }
    if (dialog_ == Dialog::Disaster) {
        if (id == kOk) {
            const Action disaster = disaster_;
            close_dialog();
            switch (disaster) {
            case Action::Fire:
                session_.disaster_fire();
                break;
            case Action::Flood:
                session_.disaster_flood();
                break;
            case Action::Tornado:
                session_.disaster_tornado();
                break;
            case Action::Earthquake:
                session_.disaster_earthquake();
                break;
            case Action::Monster:
                session_.disaster_monster();
                break;
            case Action::Meltdown:
                session_.disaster_meltdown();
                break;
            default:
                if (menu_table::city_event_index(disaster) >= 0) {
                    session_.start_event(static_cast<city_events::Kind>(menu_table::city_event_index(disaster)));
                }
                break;
            }
            return;
        }
        close_dialog();
        return;
    }
    if (dialog_ == Dialog::NewCity) {
        if (id == kCancel) {
            dismiss_dialog();
            return;
        }
        if (id == kBack) {
            if (new_page_ > 0) {
                --new_page_;
            }
            // The seed field is not on this page. Leave the caret in the
            // name, with nothing selected and no leftover seed error.
            focus_id_ = kFieldName;
            field_caret_ = static_cast<int>(field_a_.size());
            field_anchor_ = field_caret_;
            dialog_error_.clear();
            return;
        }
        if (id == kEditTerrain && new_page_ >= 1) {
            open_terrain_editor();
            return;
        }
        if (id == kNext && new_page_ == 0) {
            if (!CitySession::name_is_usable(field_a_)) {
                dialog_error_ = "Enter a name that is not only spaces.";
                return;
            }
            dialog_error_.clear();
            new_page_ = 1;
            focus_id_ = kFieldSeed;
            field_select_all(field_b_, field_caret_, field_anchor_);
            return;
        }
        if (id == kGenerate || (id == kNext && new_page_ >= 1)) {
            int seed = 0;
            bool set = false;
            const bool name_ok = CitySession::name_is_usable(field_a_);
            const bool seed_ok = take_city_seed(field_b_, seed, set);
            if (!name_ok || !seed_ok) {
                dialog_error_ = new_city_field_error(name_ok, seed_ok);
                return;
            }
            resolve_auto_seed(auto_seed_, seed, set);
            CitySession::NewCitySpec spec;
            spec.name = field_a_;
            spec.seed = seed;
            spec.seed_was_set = set;
            spec.difficulty = new_level_;
            spec.island = new_island_;
            spec.rivers = new_rivers_;
            spec.lakes = new_lakes_;
            spec.trees = new_trees_;
            if (const std::vector<unsigned short> *painted = pending_terrain_for_page()) {
                spec.terrain = *painted;
            }
    const bool from_welcome = welcome_gate_;
    welcome_gate_ = false;
    city_chosen_ = true;
    close_dialog();
    const int serial = session_.message_serial();
    ++city_generation_;
    center_pad_cursor();
    reset_touch_state();
    clear_transient_message();
            session_.new_city(spec);
            if (session_.message_serial() == serial) {
                restore_play_preferences();
            }
            center_on_fraction(0.5, 0.5);
            sync_title();
            if (motion_on()) {
                begin_city_reveal();
            } else if (from_welcome) {
                begin_scene_fade();
            }
        }
        return;
    }
    if (dialog_ == Dialog::Scenario) {
        if (id == kPlay) {
            const int index = scenario_index_;
            close_dialog();
            if (index >= 0 && index < CitySession::kScenarioCount) {
                start_scenario(CitySession::scenario_def(CitySession::picker_scenario(index)).id);
            }
            return;
        }
        dismiss_dialog();
        return;
    }
    if (dialog_ == Dialog::Rename) {
        if (id == kRename || id == kOk) {
            if (!CitySession::name_is_usable(field_a_)) {
                dialog_error_ = "Enter a name that is not only spaces.";
                return;
            }
            session_.rename_city(field_a_);
            close_dialog();
            sync_title();
            return;
        }
        close_dialog();
        return;
    }
    if (dialog_ == Dialog::Recovery) {
        if (id == kRecover) {
            const city_recovery::Entry entry = recovery_;
            close_dialog();
            recovery_ = entry;
            request_with_save(AfterSave::Recover);
            return;
        }
        if (id == kDelete) {
            open_dialog(Dialog::RecoveryDelete);
            return;
        }
        const bool back_to_welcome = welcome_gate_ && !city_chosen_;
        close_dialog();
        if (back_to_welcome) {
            open_dialog(Dialog::Welcome);
        }
        return;
    }
    if (dialog_ == Dialog::RecoveryDelete) {
        if (id == kOk || id == kDelete) {
            city_recovery::remove(recovery_.path);
            close_dialog();
            offer_recovery();
            return;
        }
        offer_recovery();
        return;
    }
    if (dialog_ == Dialog::Files) {
        if (id == kRename || id == kDelete) {
            if (!file_row_selected()) {
                return;
            }
            file_target_ = files_[static_cast<std::size_t>(list_index_)];
            open_dialog(id == kRename ? Dialog::FileRename : Dialog::FileDelete);
            return;
        }
        if (id == kOk || id == kSave || id == kPlay) {
            if (files_save_) {
                if (text_has_path_separator(field_a_)) {
                    dialog_error_ = "That name cannot contain / or \\.";
                    return;
                }
                if (!save_name_has_stem(field_a_)) {
                    dialog_error_ = "Enter a file name. The name cannot be only .cty.";
                    return;
                }
                if (save_name_starts_with_dot(field_a_)) {
                    dialog_error_ = "Names cannot start with a dot.";
                    return;
                }
                const std::string path = with_cty_suffix(path_join(files_dir_, field_a_));
                std::error_code ec;
                if (std::filesystem::exists(path, ec) && path != session_.save_path()) {
                    queued_path_ = path;
                    open_dialog(Dialog::Overwrite);
                    return;
                }
                close_dialog();
                finish_save(path);
                return;
            }
            if (list_index_ >= 0 && list_index_ < static_cast<int>(files_.size())) {
                const std::string name = files_[static_cast<std::size_t>(list_index_)];
                if (name == "..") {
                    files_dir_ = parent_dir(files_dir_);
                    list_index_ = 0;
                    list_scroll_ = 0;
                    refresh_files();
                    return;
                }
                if (!name.empty() && name.back() == '/') {
                    files_dir_ = path_join(files_dir_, name.substr(0, name.size() - 1));
                    list_index_ = 0;
                    list_scroll_ = 0;
                    refresh_files();
                    return;
                }
                const std::string path = path_join(files_dir_, name);
                close_dialog();
                finish_open(path);
            }
            return;
        }
        dismiss_dialog();
        return;
    }
    if (dialog_ == Dialog::Overwrite) {
        if (id == kOk || id == kSave) {
            const std::string path = queued_path_;
            close_dialog();
            finish_save(path);
            return;
        }
        // Cancel returns to Save As with the name and folder the player typed.
        const std::string typed = field_a_;
        const std::string dir = files_dir_;
        const AfterSave pending = after_save_;
        begin_files(true);
        after_save_ = pending;
        if (!dir.empty()) {
            files_dir_ = dir;
            list_index_ = 0;
            list_scroll_ = 0;
            refresh_files();
        }
        field_a_ = typed;
        select_save_name();
        focus_id_ = kFieldName;
        return;
    }
    if (dialog_ == Dialog::Budget) {
        if (budget_cover_ && budget_is_tax_ && (id == kCancel || id == kBack || id == kClose)) {
            finish_year_and_close(false);
            return;
        }
        if (id == kYearBudget) {
            budget_cover_ = false;
            focus_id_ = kSlider + 1;
            return;
        }
        if (id == kYearEval) {
            tax_pause_hold_ = true;
            session_.update_evaluation();
            open_dialog(Dialog::Evaluation);
            return;
        }
        if (id == kYearGraphs) {
            tax_pause_hold_ = true;
            open_dialog(Dialog::Graphs);
            return;
        }
        if (id == kYearLocate && budget_cover_ && budget_is_tax_) {
            finish_year_and_close(true);
            return;
        }
        if (id == kReset) {
            tax_ = open_tax_;
            road_ = open_road_;
            police_ = open_police_;
            fire_ = open_fire_;
            session_.restore_budget_rates(tax_, road_, police_, fire_);
            return;
        }
        if (id == kCollect || id == kOk) {
            session_.set_tax(tax_);
            session_.set_road_funding(road_);
            session_.set_police_funding(police_);
            session_.set_fire_funding(fire_);
            if (budget_is_tax_) {
                const auto book = session_.budget();
                queue_milestones(session_.note_civic_year(book.cash_flow));
                session_.remember_civic_year();
            }
            session_.finish_budget_edit();
            budget_is_tax_ = false;
            budget_cover_ = false;
            tax_pause_hold_ = false;
            close_dialog();
            return;
        }
        // The year-end window says the year is collected when it closes.
        // Escape is not a way to skip the taxes. The rates on the sliders
        // are what gets collected, same as Collect Taxes. A budget opened
        // from the menu has nothing waiting, so Cancel still restores the
        // rates from when that window opened.
        if (session_.budget_pending()) {
            session_.set_tax(tax_);
            session_.set_road_funding(road_);
            session_.set_police_funding(police_);
            session_.set_fire_funding(fire_);
            const auto book = session_.budget();
            queue_milestones(session_.note_civic_year(book.cash_flow));
            session_.remember_civic_year();
            session_.finish_budget_edit();
            budget_is_tax_ = false;
            budget_cover_ = false;
            tax_pause_hold_ = false;
            close_dialog();
            return;
        }
        session_.restore_budget_rates(open_tax_, open_road_, open_police_, open_fire_);
        tax_ = open_tax_;
        road_ = open_road_;
        police_ = open_police_;
        fire_ = open_fire_;
        session_.set_dirty(budget_was_dirty_);
        close_dialog();
        return;
    }
    if (dialog_ == Dialog::Outcome) {
        close_dialog();
        if (id == kKeep) {
            if (session_.outcome_pause_pending()) {
                session_.resume_after_outcome();
                speed_ = session_.speed();
            }
        } else if (id == kScenarioList) {
            if (session_.outcome_pause_pending()) {
                session_.stay_paused_after_outcome();
                speed_ = session_.speed();
            }
            request_with_save(AfterSave::Scenario);
        } else if (session_.outcome_pause_pending()) {
            session_.stay_paused_after_outcome();
            speed_ = session_.speed();
        }
        return;
    }
    if (dialog_ == Dialog::About && id == kViewLicense) {
        open_dialog(Dialog::License);
        return;
    }
    if (dialog_ == Dialog::License) {
        // Close, Escape and B go back to About, where the viewer came from.
        open_dialog(Dialog::About);
        focus_id_ = kViewLicense;
        return;
    }
    if (dialog_ == Dialog::Query) {
        query_pinned_ = false;
        hint_until_ = std::chrono::steady_clock::now();
        close_dialog();
        return;
    }
    if (dialog_ == Dialog::FileWait) {
        {
            std::lock_guard<std::mutex> lock(file_mu_);
            file_ignore_ = true;
            file_ready_ = false;
        }
        after_save_ = AfterSave::Nothing;
        dismiss_dialog();
        shown_message_ = "Close the system file dialog if it is still open. Its choice will be ignored.";
        shown_message_serial_ = session_.message_serial();
        hint_until_ = std::chrono::steady_clock::now() + std::chrono::seconds(8);
        return;
    }
    if (dialog_ == Dialog::Files || dialog_ == Dialog::SaveError) {
        after_save_ = AfterSave::Nothing;
    }
    dismiss_dialog();
}

void Shell::on_hotspot(int id, double x, double y)
{
    if (on_terrain_hotspot(id)) {
        return;
    }
    if (dialog_ == Dialog::None && !menu_.open() && id >= kToastItem && id < kToastItem + 8) {
        const int index = id - kToastItem;
        if (index >= 0 && index < static_cast<int>(toasts_.size()) && toasts_[static_cast<std::size_t>(index)].jump_kind >= 0) {
            show_problem_on_map(toasts_[static_cast<std::size_t>(index)].jump_kind, false);
        }
        return;
    }
    if (dialog_ == Dialog::NewCity && id != terrain_confirm_id_) {
        // A warning that a control would drop the painted map holds only
        // until something else is pressed.
        terrain_confirm_id_ = 0;
    }
    if (dialog_ == Dialog::None && !menu_.open()) {
        if (id >= kSpeedPill && id < kSpeedPill + 4) {
            static const Action speeds[4] = {Action::SetPause, Action::Slow, Action::Medium, Action::Fast};
            apply_action(speeds[id - kSpeedPill]);
            return;
        }
        if (id == kWorstTraffic) {
            int tx = 0;
            int ty = 0;
            int peak = 0;
            if (session_.worst_traffic(tx, ty, peak)) {
                begin_event_pan(tx, ty);
            }
            return;
        }
        if (id == kGoalStrip) {
            // Review nov24 #5: show the full goal (the tutorial step and
            // what to do) again, or hide a banner that is already up.
            const auto now = std::chrono::steady_clock::now();
            const bool up = !goal_dismissed_ && now < goal_until_ && goal_banner_allowed();
            if (up) {
                goal_dismissed_ = true;
            } else {
                goal_dismissed_ = false;
                goal_recalled_ = true;
                const bool learn = session_.scenario_id() == SC_LEARN_TO_PLAY;
                goal_until_ = now + std::chrono::seconds(learn && !handheld_ ? kGoalPinSeconds : 20);
            }
            return;
        }
        if (id == kCrisisStrip) {
            const CitySession::CrisisReport crisis = session_.crisis_report();
            if (!crisis.sites.empty()) {
                crisis_cycle_ = (crisis_cycle_ % static_cast<int>(crisis.sites.size()) + static_cast<int>(crisis.sites.size())) %
                                static_cast<int>(crisis.sites.size());
                const CitySession::CrisisSite &site = crisis.sites[static_cast<std::size_t>(crisis_cycle_)];
                begin_event_pan(site.x, site.y);
                ++crisis_cycle_;
            }
            return;
        }
        if (id >= kDemandMeter && id < kDemandMeter + 3) {
            const int which = id - kDemandMeter;
            // kTools: 4 residential, 5 commercial, 6 industrial.
            select_tool(4 + which);
            const double value = which == 0 ? session_.res_demand() : (which == 1 ? session_.com_demand() : session_.ind_demand());
            const char *zone = which == 0 ? "homes" : (which == 1 ? "shops" : "industry");
            const char *tool = which == 0 ? "Residential" : (which == 1 ? "Commercial" : "Industrial");
            std::string text;
            if (value > 0.15) {
                text = std::string("The city wants more ") + zone + ". The " + tool + " tool is selected.";
            } else if (value < -0.15) {
                text = std::string("There are already more ") + zone + " than the city wants. The " + tool + " tool is selected.";
            } else {
                text = std::string("Demand for ") + zone + " is steady. The " + tool + " tool is selected.";
            }
            shown_message_ = text;
            shown_message_serial_ = session_.message_serial();
            hint_until_ = std::chrono::steady_clock::now() + std::chrono::seconds(4);
            return;
        }
        if (id >= kQuickTool && id < kQuickTool + kToolCount) {
            const int index = id - kQuickTool;
            // Touch: the narrow slots (about 44 px, 5 mm on a Deck) are too
            // close for a fingertip. The first tap opens that section at
            // full size; a tap on a full-size slot chooses (Deck review #19).
            const int shown = touch_quick_shown_ >= 0 ? touch_quick_shown_ : tool_index_;
            if (touch_used_ && handheld_ && !tool_groups::same_section(index, shown)) {
                touch_quick_shown_ = index;
                return;
            }
            touch_quick_shown_ = -1;
            select_tool(index);
            return;
        }
        if (id == kHudZoomIn || id == kHudZoomOut) {
            zoom_by(id == kHudZoomIn ? 1 : -1);
            return;
        }
        if (id == kHudCenter) {
            if (gamepad_nav_) {
                center_on_cursor();
            } else {
                center_on_fraction(0.5, 0.5);
            }
            return;
        }
    }
    if (id == kHudMenu && dialog_ == Dialog::None) {
        if (menu_.open()) {
            menu_.close();
        } else {
            menu_.open_by_pointer(0);
        }
        return;
    }
    if (id >= kMenuTitle && id < kMenuTitle + 5 && dialog_ == Dialog::None) {
        const int column = id - kMenuTitle;
        if (menu_.open_menu == column && menu_slid_open_) {
            // The pointer slid onto this title and opened it; the click
            // that follows means "this one", not "close" (review nov25 #11:
            // a trackpad click on the next title used to close the menu).
            menu_slid_open_ = false;
        } else if (menu_.open_menu == column) {
            menu_.close();
        } else {
            menu_.open_by_pointer(column);
            menu_slid_open_ = false;
        }
        return;
    }
    if (id == kMenuScroll && menu_.open()) {
        menu_thumb_drag_ = true;
        scroll_menu_to_pointer(y);
        return;
    }
    if (id >= kMenuItem && id < kRecentItem) {
        const int packed = id - kMenuItem;
        menu_.open_menu = packed / 40;
        menu_.cursor = packed % 40;
        menu_.activate_current();
        if (menu_.activated == Action::RecentIndex && menu_.activated_recent >= 0 &&
            menu_.activated_recent < static_cast<int>(recent_.size())) {
            queued_path_ = recent_[static_cast<std::size_t>(menu_.activated_recent)];
            request_with_save(AfterSave::OpenPath);
        } else if (menu_.activated != Action::None) {
            apply_action(menu_.activated);
        }
        return;
    }
    if (id >= kRecentItem && id < kRecentItem + 20) {
        const int index = id - kRecentItem;
        if (index >= 0 && index < static_cast<int>(recent_.size())) {
            menu_.close();
            queued_path_ = recent_[static_cast<std::size_t>(index)];
            request_with_save(AfterSave::OpenPath);
        }
        return;
    }
    if (id >= kListItem && id < kListItem + 64) {
        const int row = id - kListItem;
        if (dialog_ == Dialog::Bindings) {
            list_index_ = list_scroll_ + row;
            if (list_index_ >= bind_row_count()) {
                list_index_ = bind_row_count() - 1;
            }
            focus_id_ = id;
            bind_listen_ = true;
            dialog_error_.clear();
            dialog_note_ok_ = false;
            return;
        }
        if (dialog_ == Dialog::Files) {
            const int picked = list_scroll_ + row;
            if (picked == list_index_ && picked >= 0 && picked < static_cast<int>(files_.size())) {
                focus_id_ = id;
                activate_file_row();
                return;
            }
            list_index_ = picked;
            if (list_index_ >= static_cast<int>(files_.size())) {
                list_index_ = static_cast<int>(files_.size()) - 1;
            }
            if (list_index_ < 0) {
                list_index_ = 0;
            }
            reveal_file_list();
        } else if (dialog_ == Dialog::Scenario) {
            const int picked = list_scroll_ + row;
            if (picked == scenario_index_) {
                focus_id_ = id;
                on_dialog_button(kPlay);
                return;
            }
            scenario_index_ = picked;
            if (scenario_index_ >= CitySession::kScenarioCount) {
                scenario_index_ = CitySession::kScenarioCount - 1;
            }
            if (scenario_index_ < 0) {
                scenario_index_ = 0;
            }
            list_index_ = scenario_index_;
            reveal_file_list();
        } else if (dialog_ == Dialog::Messages) {
            const int picked = list_scroll_ + row;
            list_index_ = picked;
            if (list_index_ >= static_cast<int>(session_.notices().size())) {
                list_index_ = static_cast<int>(session_.notices().size()) - 1;
            }
            if (list_index_ < 0) {
                list_index_ = 0;
            }
            reveal_file_list();
            jump_notice(list_index_);
        } else {
            list_index_ = row;
        }
        focus_id_ = id;
        return;
    }
    if (id == kListScroll && dialog_ == Dialog::Bindings) {
        list_dragging_ = true;
        const int max_scroll = list_max_scroll(bind_row_count(), std::max(1, files_visible_));
        double along = list_track_h_ <= 1.0 ? 0.0 : (y - list_track_y_) / list_track_h_;
        if (along < 0.0) {
            along = 0.0;
        }
        if (along > 1.0) {
            along = 1.0;
        }
        list_scroll_ = static_cast<int>(std::lround(along * max_scroll));
        if (list_scroll_ > max_scroll) {
            list_scroll_ = max_scroll;
        }
        return;
    }
    if (id == kListScroll && (dialog_ == Dialog::Files || dialog_ == Dialog::Scenario)) {
        list_dragging_ = true;
        const int count =
            dialog_ == Dialog::Scenario ? CitySession::kScenarioCount : static_cast<int>(files_.size());
        const int index = list_index_from_track(y, list_track_y_, list_track_h_, count);
        if (dialog_ == Dialog::Scenario) {
            scenario_index_ = index;
        }
        list_index_ = dialog_ == Dialog::Scenario ? scenario_index_ : index;
        reveal_file_list();
        const int row = list_index_ - list_scroll_;
        if (row >= 0 && row < 64) {
            focus_id_ = kListItem + row;
        }
        return;
    }
    if (id == kFieldName || id == kFieldSeed) {
        focus_id_ = id;
        return;
    }
    if (id >= kOsk && id < kOsk + 64) {
        int count = 0;
        const OskKey *keys = osk_keys(count);
        const int index = id - kOsk;
        if (index < 0 || index >= count) {
            return;
        }
        const OskKey &key = keys[index];
        focus_id_ = id;
        if (key.kind == OskKind::Backspace) {
            backspace_field();
        } else if (key.kind == OskKind::Space) {
            type_text(" ");
        } else if (key.kind == OskKind::Auto) {
            if (active_field() != nullptr) {
                fill_random_city_name();
            }
        } else if (key.kind == OskKind::Shift) {
            osk_shift_ = !osk_shift_;
        } else if (key.text != nullptr) {
            const char *insert = (osk_shift_ && key.text_shift != nullptr) ? key.text_shift : key.text;
            type_text(insert);
        }
        return;
    }
    if (id == kGraphRoll && dialog_ == Dialog::Graphs) {
        graph_rolled_ = !graph_rolled_;
        graph_pointer_x_ = -1;
        focus_id_ = kGraphRoll;
        return;
    }
    if (id >= kGraphSeries && id < kGraphSeries + 16 && dialog_ == Dialog::Graphs) {
        graph_mask_ = graph_solo_mask(graph_mask_, id - kGraphSeries);
        focus_id_ = id;
        return;
    }
    if (id == kRandomName && dialog_ == Dialog::NewCity) {
        fill_random_city_name();
        focus_id_ = kFieldName;
        return;
    }
    if (id == kNewTerrain && dialog_ == Dialog::NewCity) {
        focus_id_ = id;
        if (confirm_drop_terrain(id)) {
            reroll_terrain();
        }
        return;
    }
    if (id >= kSlider && id < kSlider + 8) {
        slider_drag_ = id;
        for (const SliderGeom &slider : sliders_) {
            if (slider.id != id || slider.w <= 1) {
                continue;
            }
            const double t = std::max(0.0, std::min(1.0, (x - slider.x) / slider.w));
            int value = slider.min + static_cast<int>(std::lround(t * (slider.max - slider.min)));
            if (dialog_ == Dialog::Settings) {
                if (id == kSlider + 1) {
                    volume_ = value;
                    sound_.set_volume(static_cast<float>(volume_) / 100.0f);
                } else if (id == kSlider + 2 && scale_percent_ > 0) {
                    int snapped = 50 + static_cast<int>(std::lround((value - 50) / 25.0) * 25);
                    if (snapped < 50) {
                        snapped = 50;
                    }
                    if (snapped > 200) {
                        snapped = 200;
                    }
                    scale_percent_ = snapped;
                }
            } else if (id == kSlider + 1) {
                tax_ = value;
                session_.set_tax(tax_);
            } else if (id == kSlider + 2) {
                road_ = value;
                session_.set_road_funding(road_);
            } else if (id == kSlider + 3) {
                police_ = value;
                session_.set_police_funding(police_);
            } else if (id == kSlider + 4) {
                fire_ = value;
                session_.set_fire_funding(fire_);
            }
        }
        focus_id_ = id;
        return;
    }
    if (id >= kChoice && id < kChoice + 50) {
        const int packed = id - kChoice;
        const int group = packed / 10;
        const int option = packed % 10;
        if (group >= 1 && group <= 4 && dialog_ == Dialog::NewCity) {
            // A land knob that changes makes new terrain; a painted map
            // would be dropped, so that asks first.
            static const int values[4][3] = {
                {CitySession::kTerrainDefault, CitySession::kTerrainOff, CitySession::kIslandAlways},
                {CitySession::kTerrainDefault, CitySession::kTerrainOff, CitySession::kRiversCurvy},
                {CitySession::kTerrainDefault, CitySession::kTerrainOff, CitySession::kLakesMany},
                {CitySession::kTerrainDefault, CitySession::kTerrainOff, CitySession::kTreesWooded},
            };
            const int now[4] = {new_island_, new_rivers_, new_lakes_, new_trees_};
            focus_id_ = id;
            if (option >= 0 && option <= 2 && values[group - 1][option] != now[group - 1] && !confirm_drop_terrain(id)) {
                return;
            }
        }
        if (group == 0) {
            new_level_ = option;
        } else if (group == 1) {
            new_island_ = option == 0 ? CitySession::kTerrainDefault
                                      : option == 1 ? CitySession::kTerrainOff : CitySession::kIslandAlways;
        } else if (group == 2) {
            new_rivers_ = option == 0 ? CitySession::kTerrainDefault
                                      : option == 1 ? CitySession::kTerrainOff : CitySession::kRiversCurvy;
        } else if (group == 3) {
            new_lakes_ = option == 0 ? CitySession::kTerrainDefault
                                     : option == 1 ? CitySession::kTerrainOff : CitySession::kLakesMany;
        } else if (group == 4) {
            new_trees_ = option == 0 ? CitySession::kTerrainDefault
                                     : option == 1 ? CitySession::kTerrainOff : CitySession::kTreesWooded;
        }
        focus_id_ = id;
        return;
    }
    if (id >= kEvalProblem && id < kEvalProblem + 4 && dialog_ == Dialog::Evaluation) {
        const int index = id - kEvalProblem;
        const CitySession::Evaluation report = session_.evaluation();
        if (index >= 0 && index < static_cast<int>(report.problems.size()) && report.problems[static_cast<std::size_t>(index)].kind >= 0) {
            show_problem_on_map(report.problems[static_cast<std::size_t>(index)].kind, false);
        }
        return;
    }
    if (id >= kEvalWhy && id < kEvalWhy + 4 && dialog_ == Dialog::Evaluation) {
        eval_why_ = id - kEvalWhy;
        focus_id_ = id;
        return;
    }
    if (id == kEvalShow && dialog_ == Dialog::None && overlay_return_eval_) {
        return_from_overlay();
        return;
    }
    if ((id == kEvalShow || id == kEvalAlt) && dialog_ == Dialog::Evaluation && eval_why_ >= 0) {
        const CitySession::Evaluation report = session_.evaluation();
        if (eval_why_ < static_cast<int>(report.problems.size())) {
            const int kind = report.problems[static_cast<std::size_t>(eval_why_)].kind;
            if (kind >= 0) {
                show_problem_on_map(kind, id == kEvalAlt);
            }
        }
        return;
    }
    if (id == kInspectPower) {
        const double keep_x = scroll_x_;
        const double keep_y = scroll_y_;
        if (dialog_ == Dialog::Query) {
            close_dialog();
        }
        overlay_.set_layer(CitySession::MapLayer::Power);
        overlay_on_ = true;
        overlay_.sync();
        scroll_x_ = keep_x;
        scroll_y_ = keep_y;
        overlay_return_eval_ = false;
        return;
    }
    if (id == kInspectDetails && dialog_ == Dialog::None) {
        int hx = 0;
        int hy = 0;
        if (map_.hover_tile(hx, hy)) {
            session_.use_tool(tool_by_index(kToolCount - 1)->engine_id, hx, hy);
        }
        return;
    }
    if (id == kCardDismiss) {
        card_id_ = -1;
        card_ms_ = 0;
        return;
    }
    if (id == kLedgerNotes && dialog_ == Dialog::Ledger) {
        milestone_notes_ = !milestone_notes_;
        return;
    }
    if (dialog_ != Dialog::None && is_dialog_button(id)) {
        on_dialog_button(id);
        return;
    }
    if (menu_.open() && dialog_ == Dialog::None &&
        (id == kHotNone || id == kMapId || id == kPaletteId || id == kMinimapId)) {
        menu_.close();
        return;
    }
    if (dialog_ != Dialog::None && id == 0) {
        if (dialog_ == Dialog::Graphs || dialog_ == Dialog::Evaluation || dialog_ == Dialog::Overlay ||
            dialog_ == Dialog::Query) {
            close_dialog();
        }
    } else if (menu_.open() && id == 0) {
        menu_.close();
    }
}

bool Shell::show_osk() const
{
    return gamepad_nav_ || deck_ || touch_used_;
}

bool Shell::osk_visible() const
{
    return show_osk() || osk_latched_;
}

std::string *Shell::active_field()
{
    const bool osk = focus_id_ >= kOsk && focus_id_ < kOsk + 64;
    const bool name = focus_id_ == kFieldName || osk;
    if (dialog_ == Dialog::NewCity) {
        if (new_page_ == 0 && name) {
            return &field_a_;
        }
        if (focus_id_ == kFieldSeed) {
            return &field_b_;
        }
        if (focus_id_ == kFieldName) {
            return &field_a_;
        }
        return nullptr;
    }
    if ((dialog_ == Dialog::Rename || dialog_ == Dialog::FileRename) && name) {
        return &field_a_;
    }
    if (dialog_ == Dialog::Files && files_save_ && name) {
        return &field_a_;
    }
    return nullptr;
}

void Shell::select_field()
{
    std::string *field = active_field();
    if (field == nullptr) {
        return;
    }
    field_select_all(*field, field_caret_, field_anchor_);
}

void Shell::type_text(const std::string &text)
{
    std::string *field = active_field();
    if (field == nullptr || text.empty()) {
        return;
    }
    if (field == &field_a_ && text_has_path_separator(text)) {
        dialog_error_ = "That name cannot contain / or \\.";
        return;
    }
    const bool city_name = field == &field_a_ && (dialog_ == Dialog::NewCity || dialog_ == Dialog::Rename);
    const int limit = field == &field_b_ ? kCitySeedFieldMax : city_name ? kCityNameMaxBytes : 64;
    if (!field_insert(*field, field_caret_, field_anchor_, text, limit)) {
        dialog_error_ = field == &field_b_ ? "That seed is too long." : "That name is too long.";
    } else {
        clear_resolved_field_error();
    }
}

void Shell::clear_resolved_field_error()
{
    if (dialog_error_.empty()) {
        return;
    }
    if (dialog_error_ == "Enter a name that is not only spaces.") {
        if (CitySession::name_is_usable(field_a_)) {
            dialog_error_.clear();
        }
        return;
    }
    if (dialog_error_ == "Check the name." || dialog_error_ == "Check the seed." ||
        dialog_error_ == "Check the name and the seed.") {
        int seed = 0;
        bool set = false;
        const bool name_ok = CitySession::name_is_usable(field_a_);
        const bool seed_ok = take_city_seed(field_b_, seed, set);
        if (name_ok && seed_ok) {
            dialog_error_.clear();
            return;
        }
        if (dialog_error_ == "Check the name and the seed.") {
            if (name_ok) {
                dialog_error_ = "Check the seed.";
            } else if (seed_ok) {
                dialog_error_ = "Check the name.";
            }
        } else if (dialog_error_ == "Check the name." && name_ok) {
            dialog_error_.clear();
        } else if (dialog_error_ == "Check the seed." && seed_ok) {
            dialog_error_.clear();
        }
        return;
    }
    if (dialog_error_ == "That name cannot contain / or \\.") {
        if (!text_has_path_separator(field_a_)) {
            dialog_error_.clear();
        }
        return;
    }
    if (dialog_error_ == "That name is too long." || dialog_error_ == "That seed is too long.") {
        dialog_error_.clear();
        return;
    }
    if (dialog_error_ == "Enter a file name. The name cannot be only .cty.") {
        if (save_name_has_stem(field_a_)) {
            dialog_error_.clear();
        }
        return;
    }
    if (dialog_error_ == "Names cannot start with a dot.") {
        if (!save_name_starts_with_dot(field_a_)) {
            dialog_error_.clear();
        }
    }
}

void Shell::backspace_field()
{
    std::string *field = active_field();
    if (field == nullptr || field->empty()) {
        return;
    }
    field_backspace(*field, field_caret_, field_anchor_);
    clear_resolved_field_error();
}

void Shell::move_field_caret(int direction, bool extend, bool home, bool end)
{
    std::string *field = active_field();
    if (field == nullptr) {
        return;
    }
    if (home) {
        field_home(*field, field_caret_, field_anchor_, extend);
        return;
    }
    if (end) {
        field_end(*field, field_caret_, field_anchor_, extend);
        return;
    }
    field_move(*field, field_caret_, field_anchor_, direction, extend);
}

void Shell::paste_field()
{
    char *clip = SDL_GetClipboardText();
    if (clip == nullptr) {
        return;
    }
    std::string *field = active_field();
    if (field == nullptr) {
        SDL_free(clip);
        return;
    }
    const int from = std::min(field_caret_, field_anchor_);
    const int to = std::max(field_caret_, field_anchor_);
    const bool city_name = field == &field_a_ && (dialog_ == Dialog::NewCity || dialog_ == Dialog::Rename);
    const int limit = field == &field_b_ ? kCitySeedFieldMax : city_name ? kCityNameMaxBytes : 64;
    const int room = limit - (static_cast<int>(field->size()) - (to - from));
    const std::string clean = sanitize_pasted_text(clip, std::max(0, room));
    SDL_free(clip);
    if (!clean.empty()) {
        type_text(clean);
    }
}

bool Shell::jump_list_edge(bool to_end)
{
    const bool scenarios = dialog_ == Dialog::Scenario;
    const bool messages = dialog_ == Dialog::Messages;
    const bool files = dialog_ == Dialog::Files;
    if (!scenarios && !messages && !files) {
        return false;
    }
    const int count = scenarios ? CitySession::kScenarioCount
                                : messages ? static_cast<int>(session_.notices().size())
                                           : static_cast<int>(files_.size());
    if (count <= 0) {
        return false;
    }
    const int next = to_end ? count - 1 : 0;
    if (scenarios) {
        scenario_index_ = next;
    }
    list_index_ = next;
    reveal_file_list();
    const bool keep_field = focus_id_ == kFieldName || focus_id_ == kFieldSeed;
    if (!keep_field) {
        const int row = list_index_ - list_scroll_;
        if (row >= 0 && row < 64) {
            focus_id_ = kListItem + row;
        }
        align_list_focus();
    }
    return true;
}

int Shell::hit(double x, double y) const
{
    for (int i = static_cast<int>(hot_.size()) - 1; i >= 0; --i) {
        const Hotspot &hot = hot_[static_cast<std::size_t>(i)];
        if (x >= hot.x && y >= hot.y && x < hot.x + hot.w && y < hot.y + hot.h) {
            return hot.id;
        }
    }
    return 0;
}

int Shell::dialog_anim_len() const
{
    return dialog_ == Dialog::Budget && budget_cover_ && budget_is_tax_ ? kYearRevealMs : kDialogEaseMs;
}

void Shell::push_hot(double x, double y, double w, double h, int id)
{
    // Hot rects follow the dialog ease: scaled about its centre, then slid.
    const double s = dialog_hot_scale_;
    if (s != 1.0) {
        x = dialog_hot_cx_ + (x - dialog_hot_cx_) * s;
        y = dialog_hot_cy_ + (y - dialog_hot_cy_) * s;
        w *= s;
        h *= s;
    }
    next_hot_.push_back(Hotspot{x, y + dialog_hot_dy_, w, h, id});
}

void Shell::push_focus(int id)
{
    // The menu and the map stay on screen under a modal. They must not join
    // the D-pad cycle.
    if (dialog_ != Dialog::None && !is_modal_control(id)) {
        return;
    }
    next_focus_.push_back(id);
}

bool Shell::move_osk_vertically(int direction)
{
    if (direction == 0 || focus_id_ < kOsk || focus_id_ >= kOsk + 64) {
        return false;
    }
    int count = 0;
    const OskKey *keys = osk_keys(count);
    const int index = focus_id_ - kOsk;
    if (index < 0 || index >= count || count > 64) {
        return false;
    }
    double center[64] = {};
    char present[64] = {};
    for (const Hotspot &hot : hot_) {
        if (hot.id < kOsk || hot.id >= kOsk + count) {
            continue;
        }
        const int key = hot.id - kOsk;
        center[key] = hot.x + hot.w * 0.5;
        present[key] = 1;
    }
    if (present[index] == 0) {
        return false;
    }
    const int target = osk_vertical_target(keys, count, index, direction, center, present);
    if (target >= 0) {
        focus_id_ = kOsk + target;
        return true;
    }
    // No row that way. Leave for the control past the keyboard. A list row
    // in the focus order is the last visible line, which is not the blue
    // selection, so land on the selection instead.
    const int leave = osk_exit_focus(focus_order_.data(), static_cast<int>(focus_order_.size()), focus_id_, direction,
                                     kOsk, kOsk + 64);
    if (leave >= kListItem && leave < kListItem + 64) {
        reveal_file_list();
        const int index_now = dialog_ == Dialog::Scenario ? scenario_index_ : list_index_;
        const int row = index_now - list_scroll_;
        if (row >= 0 && row < 64) {
            focus_id_ = kListItem + row;
        }
        return true;
    }
    if (leave >= 0) {
        focus_id_ = leave;
    }
    return true;
}

bool Shell::nudge_new_city_column(int direction)
{
    if (direction == 0 || dialog_ != Dialog::NewCity || new_page_ != 1) {
        return false;
    }
    const bool choice = focus_id_ >= kChoice && focus_id_ < kChoice + 50;
    const int packed = choice ? focus_id_ - kChoice : -1;
    const int group = choice ? packed / 10 : -1;
    const int option = choice ? packed % 10 : -1;
    const bool choice_cell = choice && group >= 0 && group <= 4 && option >= 0 && option <= 2;
    const bool land = choice_cell || focus_id_ == kFieldSeed || focus_id_ == kNewTerrain || focus_id_ == kBack ||
                      focus_id_ == kGenerate || focus_id_ == kEditTerrain;
    if (!land) {
        return false;
    }
    if (choice_cell) {
        const int next = group + (direction < 0 ? -1 : 1);
        if (next >= 0 && next <= 4) {
            focus_id_ = kChoice + next * 10 + option;
            return true;
        }
    }
    const Hotspot *from = nullptr;
    for (const Hotspot &hot : hot_) {
        if (hot.id == focus_id_) {
            from = &hot;
        }
    }
    if (from == nullptr) {
        return true;
    }
    const double cx = from->x + from->w * 0.5;
    const double cy = from->y + from->h * 0.5;
    const double band = std::max(4.0, from->h * 0.45);
    int best = -1;
    double best_dy = 0;
    double best_dx = 0;
    double best_overlap = -1.0e9;
    for (int id : focus_order_) {
        if (id == focus_id_) {
            continue;
        }
        const Hotspot *hot = nullptr;
        for (const Hotspot &item : hot_) {
            if (item.id == id) {
                hot = &item;
            }
        }
        if (hot == nullptr || hot->w < 1.0 || hot->h < 1.0) {
            continue;
        }
        const double oy = hot->y + hot->h * 0.5;
        const double dy = oy - cy;
        if (direction > 0 && dy <= band) {
            continue;
        }
        if (direction < 0 && dy >= -band) {
            continue;
        }
        const double ady = std::fabs(dy);
        const double ox = hot->x + hot->w * 0.5;
        const double adx = std::fabs(ox - cx);
        const double overlap = std::min(from->x + from->w, hot->x + hot->w) - std::max(from->x, hot->x);
        const bool closer_row = best < 0 || ady < best_dy - band;
        const bool same_row = best >= 0 && std::fabs(ady - best_dy) <= band;
        if (closer_row) {
            best = id;
            best_dy = ady;
            best_dx = adx;
            best_overlap = overlap;
        } else if (same_row) {
            const bool better_overlap = overlap > best_overlap + 1.0;
            const bool nearer = std::fabs(overlap - best_overlap) <= 1.0 && adx < best_dx - 0.5;
            if (better_overlap || nearer) {
                best = id;
                best_dx = adx;
                best_overlap = overlap;
                if (ady < best_dy) {
                    best_dy = ady;
                }
            }
        }
    }
    if (best >= 0) {
        focus_id_ = best;
    }
    return true;
}

bool Shell::nudge_dialog_up_down(int direction)
{
    if (direction == 0 || dialog_ == Dialog::None) {
        return false;
    }
    if (nudge_file_list(direction < 0 ? -1 : 1, false, false)) {
        return true;
    }
    if (move_osk_vertically(direction)) {
        return true;
    }
    if (nudge_new_city_column(direction)) {
        return true;
    }
    // The Save/Load buttons are one row. Down from Up used to step to
    // Cancel. Down returns to the selected file; Left and Right still
    // move along the buttons.
    if (direction > 0 && dialog_ == Dialog::Files &&
        (focus_id_ == kUp || focus_id_ == kCancel || focus_id_ == kSave || focus_id_ == kOk || focus_id_ == kRename ||
         focus_id_ == kDelete)) {
        if (files_.empty()) {
            return false;
        }
        const int visible = std::max(1, files_visible_);
        const int row = list_index_ - list_scroll_;
        if (row >= 0 && row < visible && row < 64) {
            focus_id_ = kListItem + row;
            return true;
        }
    }
    return false;
}

void Shell::align_list_focus()
{
    if (dialog_ != Dialog::Files && dialog_ != Dialog::Scenario && dialog_ != Dialog::Messages &&
        dialog_ != Dialog::Bindings) {
        return;
    }
    if (focus_id_ < kListItem || focus_id_ >= kListItem + 64) {
        return;
    }
    const int index = dialog_ == Dialog::Scenario ? scenario_index_ : list_index_;
    const int row = index - list_scroll_;
    const int visible = std::max(1, files_visible_);
    if (row >= 0 && row < visible && row < 64) {
        focus_id_ = kListItem + row;
    }
}

void Shell::scroll_menu_by(int rows)
{
    if (!menu_.open() || menu_item_n_ <= 0 || rows == 0) {
        return;
    }
    int row = menu_table::menu_scroll_row(menu_item_top_, menu_item_n_, menu_scroll_);
    row += rows;
    if (row < 0) {
        row = 0;
    }
    if (row >= menu_item_n_) {
        row = menu_item_n_ - 1;
    }
    const MenuEntry *items = menu_table::column_items(menu_.open_menu);
    const int column_n = menu_table::column_count(menu_.open_menu);
    if (items != nullptr && row < column_n) {
        row = menu_table::menu_visible_top(items, column_n, row, rows);
    }
    const double end = menu_table::menu_reveal_scroll(menu_item_top_, menu_item_h_, menu_item_n_, menu_item_n_ - 1,
                                                      menu_view_limit_, menu_content_h_);
    double scroll = menu_table::menu_row_scroll(menu_item_top_, menu_item_n_, row);
    if (scroll > end) {
        scroll = end;
    }
    menu_scroll_ = scroll;
    if (hover_id_ >= kMenuItem && hover_id_ < kRecentItem) {
        hover_id_ = 0;
    }
}

void Shell::scroll_menu_to_pointer(double y)
{
    if (!menu_.open() || menu_item_n_ <= 0 || menu_drop_h_ < 1.0) {
        return;
    }
    double t = (y - static_cast<double>(menu_h_)) / menu_drop_h_;
    if (t < 0.0) {
        t = 0.0;
    }
    if (t > 1.0) {
        t = 1.0;
    }
    const int last = menu_item_n_ - 1;
    const double end = menu_table::menu_reveal_scroll(menu_item_top_, menu_item_h_, menu_item_n_, last, menu_view_limit_,
                                                      menu_content_h_);
    const double desired = t * end;
    const int row = menu_table::menu_scroll_row(menu_item_top_, menu_item_n_, desired);
    menu_scroll_ = menu_table::menu_row_scroll(menu_item_top_, menu_item_n_, row);
}

void Shell::autoscroll_menu()
{
    if (!menu_.open() || menu_thumb_drag_ || menu_drop_h_ < 1.0 || menu_item_n_ <= 0) {
        menu_edge_dir_ = 0;
        return;
    }
    const bool inside = pointer_x_ >= menu_drop_x_ && pointer_x_ < menu_drop_x_ + menu_drop_w_ &&
                        pointer_y_ >= static_cast<double>(menu_h_) &&
                        pointer_y_ < static_cast<double>(menu_h_) + menu_drop_h_;
    int dir = 0;
    if (inside) {
        if (pointer_y_ < static_cast<double>(menu_h_) + 18.0) {
            dir = -1;
        } else if (pointer_y_ > static_cast<double>(menu_h_) + menu_drop_h_ - 18.0) {
            dir = 1;
        }
    }
    if (dir == 0) {
        menu_edge_dir_ = 0;
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (dir != menu_edge_dir_ || now - menu_edge_at_ >= std::chrono::milliseconds(160)) {
        scroll_menu_by(dir);
        menu_edge_dir_ = dir;
        menu_edge_at_ = now;
    }
}

bool Shell::focus_is_flat_buttons() const
{
    if (dialog_ == Dialog::Welcome || dialog_ == Dialog::None || focus_order_.empty()) {
        return false;
    }
    for (int id : focus_order_) {
        if (!is_dialog_button(id)) {
            return false;
        }
    }
    return true;
}

void Shell::leave_text_field()
{
    if (dialog_ == Dialog::NewCity && new_page_ > 0) {
        focus_id_ = kBack;
        return;
    }
    focus_id_ = kCancel;
}

int Shell::control_px(int design, int floor) const
{
    const int px = dp(design);
    if (deck_ || touch_used_) {
        return std::max(px, floor);
    }
    return px;
}

void Shell::note_speed_feedback()
{
    if (!(gamepad_nav_ || deck_)) {
        return;
    }
    std::string text;
    if (speed_ <= 0) {
        const char *pause = glyph_for(PadPause);
        text = pause[0] == '\0' ? std::string("Paused.") : std::string("Paused. ") + pause + " resumes";
    } else if (speed_ == 1) {
        text = "Speed: Slow";
    } else if (speed_ >= 3) {
        text = "Speed: Fast";
    } else {
        text = "Speed: Medium";
    }
    shown_message_ = text;
    shown_message_serial_ = session_.message_serial();
    hint_until_ = std::chrono::steady_clock::now() + std::chrono::seconds(3);
}

void Shell::note_controller_connected()
{
    if (notice_sticky_) {
        return;
    }
    std::string text = "Controller connected";
    if (speed_ <= 0) {
        const char *pause = glyph_for(PadPause);
        if (pause[0] != '\0') {
            text += ". ";
            text += pause;
            text += " resumes";
        }
    }
    shown_message_ = text;
    shown_message_serial_ = session_.message_serial();
    hint_until_ = std::chrono::steady_clock::now() + std::chrono::seconds(3);
}

int Shell::dialog_accept_id() const
{
    // The affirmative button, not whatever happens to be focused. Dialogs
    // whose safe default is Cancel have none, so Start does not press it.
    switch (dialog_) {
    case Dialog::Budget:
        if (budget_cover_ && budget_is_tax_) {
            return kYearBudget;
        }
        return budget_is_tax_ ? kCollect : kOk;
    case Dialog::Settings:
        return kOk;
    case Dialog::NewCity:
        return new_page_ == 0 ? kNext : kGenerate;
    case Dialog::Rename:
        return kRename;
    case Dialog::Files:
        return files_save_ ? kSave : (files_.empty() ? 0 : kOk);
    case Dialog::FileRename:
        return kRename;
    case Dialog::Welcome:
        return welcome_continue_label().empty() ? kWelcomeNew : kWelcomeContinue;
    case Dialog::Scenario:
        return kPlay;
    case Dialog::Unsaved:
        return kSave;
    case Dialog::Outcome:
        return kKeep;
    case Dialog::Recovery:
        return city_chosen_ ? 0 : kRecover;
    default:
        return 0;
    }
}

void Shell::activate_dialog_start()
{
    const int accept = dialog_accept_id();
    if (accept == 0) {
        return;
    }
    on_dialog_button(accept);
}

void Shell::move_focus(int delta, bool wrap)
{
    if (focus_order_.empty() || delta == 0) {
        return;
    }
    int index = 0;
    bool found = false;
    for (int i = 0; i < static_cast<int>(focus_order_.size()); ++i) {
        if (focus_order_[static_cast<std::size_t>(i)] == focus_id_) {
            index = i;
            found = true;
            break;
        }
    }
    if (!found) {
        index = 0;
    }
    const int count = static_cast<int>(focus_order_.size());
    int next = index + delta;
    if (wrap) {
        next = (next + count) % count;
    } else if (next < 0 || next >= count) {
        return;
    }
    focus_id_ = focus_order_[static_cast<std::size_t>(next)];
    if (focus_id_ >= kListItem && focus_id_ < kListItem + 64) {
        if (dialog_ == Dialog::Files) {
            list_index_ = list_scroll_ + (focus_id_ - kListItem);
            if (list_index_ >= static_cast<int>(files_.size())) {
                list_index_ = static_cast<int>(files_.size()) - 1;
            }
            if (list_index_ < 0) {
                list_index_ = 0;
            }
            reveal_file_list();
            align_list_focus();
        } else if (dialog_ == Dialog::Messages) {
            list_index_ = list_scroll_ + (focus_id_ - kListItem);
            if (list_index_ >= static_cast<int>(session_.notices().size())) {
                list_index_ = static_cast<int>(session_.notices().size()) - 1;
            }
            if (list_index_ < 0) {
                list_index_ = 0;
            }
            reveal_file_list();
            align_list_focus();
        } else if (dialog_ == Dialog::Scenario) {
            scenario_index_ = list_scroll_ + (focus_id_ - kListItem);
            if (scenario_index_ >= CitySession::kScenarioCount) {
                scenario_index_ = CitySession::kScenarioCount - 1;
            }
            if (scenario_index_ < 0) {
                scenario_index_ = 0;
            }
            list_index_ = scenario_index_;
            reveal_file_list();
            align_list_focus();
        } else {
            list_index_ = focus_id_ - kListItem;
        }
    }
}

void Shell::reveal_file_list()
{
    list_scroll_free_ = false;
    const bool scenarios = dialog_ == Dialog::Scenario;
    const bool messages = dialog_ == Dialog::Messages;
    const bool bindings = dialog_ == Dialog::Bindings;
    const int count = scenarios ? CitySession::kScenarioCount
                                : messages ? static_cast<int>(session_.notices().size())
                                : bindings ? bind_row_count()
                                           : static_cast<int>(files_.size());
    const int index = scenarios ? scenario_index_ : list_index_;
    list_reveal(list_scroll_, index, std::max(1, files_visible_), count);
    if (scenarios) {
        list_index_ = scenario_index_;
    }
}

void Shell::scroll_open_list(int delta)
{
    if (delta == 0) {
        return;
    }
    const bool scenarios = dialog_ == Dialog::Scenario;
    const bool messages = dialog_ == Dialog::Messages;
    const bool files = dialog_ == Dialog::Files;
    if (!scenarios && !messages && !files) {
        return;
    }
    const int count = scenarios ? CitySession::kScenarioCount
                                : messages ? static_cast<int>(session_.notices().size())
                                           : static_cast<int>(files_.size());
    const int visible = std::max(1, files_visible_);
    int next = list_scroll_ + delta;
    const int max_scroll = list_max_scroll(count, visible);
    if (next < 0) {
        next = 0;
    }
    if (next > max_scroll) {
        next = max_scroll;
    }
    if (next == list_scroll_) {
        return;
    }
    list_scroll_ = next;
    list_scroll_free_ = true;
    const int selected = scenarios ? scenario_index_ : list_index_;
    if (focus_id_ >= kListItem && focus_id_ < kListItem + 64) {
        const int row = selected - list_scroll_;
        if (row >= 0 && row < visible && row < 64) {
            focus_id_ = kListItem + row;
        } else {
            // Keep D-pad on the list without ringing a different row.
            focus_id_ = kListItem + 63;
        }
    }
}

void Shell::place_list_window(int index, int count)
{
    const int visible = std::max(1, files_visible_);
    if (list_scroll_free_) {
        const int max_scroll = list_max_scroll(count, visible);
        if (list_scroll_ < 0) {
            list_scroll_ = 0;
        }
        if (list_scroll_ > max_scroll) {
            list_scroll_ = max_scroll;
        }
        return;
    }
    list_reveal(list_scroll_, index, visible, count);
}

bool Shell::nudge_file_list(int delta, bool page, bool force)
{
    const bool files = dialog_ == Dialog::Files && !files_.empty();
    const bool scenarios = dialog_ == Dialog::Scenario;
    const bool messages = dialog_ == Dialog::Messages && !session_.notices().empty();
    const bool bindings = dialog_ == Dialog::Bindings;
    if ((!files && !scenarios && !messages && !bindings) || delta == 0) {
        return false;
    }
    const bool on_list = focus_id_ >= kListItem && focus_id_ < kListItem + 64;
    if (!on_list && !force && !page) {
        return false;
    }
    const int count = scenarios ? CitySession::kScenarioCount
                               : messages ? static_cast<int>(session_.notices().size())
                               : bindings ? bind_row_count()
                                          : static_cast<int>(files_.size());
    int index = scenarios ? scenario_index_ : list_index_;
    const int step = page ? std::max(1, files_visible_ - 1) : std::max(1, std::abs(delta));
    const int sign = delta < 0 ? -1 : 1;
    int next = index + sign * step;
    // Up on the first row used to give up and move the orange ring onto a
    // button, leaving the blue selection where it was.
    if (!page && !force && on_list && next < 0) {
        align_list_focus();
        return true;
    }
    if (!page && !force && on_list && next >= count) {
        return false;
    }
    if (next < 0) {
        next = 0;
    }
    if (next >= count) {
        next = count - 1;
    }
    if (scenarios) {
        scenario_index_ = next;
    }
    list_index_ = next;
    reveal_file_list();
    const bool keep_field = focus_id_ == kFieldName || focus_id_ == kFieldSeed;
    if (!keep_field) {
        const int row = list_index_ - list_scroll_;
        if (row >= 0 && row < 64) {
            focus_id_ = kListItem + row;
        }
        align_list_focus();
    }
    return true;
}

void Shell::activate_file_row()
{
    if (dialog_ != Dialog::Files) {
        return;
    }
    if (list_index_ >= 0 && list_index_ < static_cast<int>(files_.size()) &&
        file_row_is_dir(files_[static_cast<std::size_t>(list_index_)])) {
        const std::string name = files_[static_cast<std::size_t>(list_index_)];
        if (name == "..") {
            files_dir_ = parent_dir(files_dir_);
        } else {
            files_dir_ = path_join(files_dir_, name.substr(0, name.size() - 1));
        }
        list_index_ = 0;
        list_scroll_ = 0;
        refresh_files();
        return;
    }
    on_dialog_button(files_save_ ? kSave : kOk);
}

void Shell::activate_focus()
{
    if (focus_id_ >= kOsk && focus_id_ < kOsk + 64) {
        on_hotspot(focus_id_, 0, 0);
        return;
    }
    if (focus_id_ >= kGraphSeries && focus_id_ < kGraphSeries + 16) {
        on_hotspot(focus_id_, 0, 0);
        return;
    }
    if (focus_id_ >= kChoice && focus_id_ < kChoice + 50) {
        on_hotspot(focus_id_, 0, 0);
        return;
    }
    if (focus_id_ >= kSlider && focus_id_ < kSlider + 8) {
        if (dialog_ != Dialog::None) {
            on_dialog_button(kOk);
        }
        return;
    }
    if (dialog_ != Dialog::None && is_dialog_button(focus_id_)) {
        on_dialog_button(focus_id_);
        return;
    }
    if (dialog_ == Dialog::Scenario) {
        on_dialog_button(kPlay);
        return;
    }
    if (dialog_ == Dialog::Messages) {
        jump_notice(list_index_);
        return;
    }
    if (dialog_ == Dialog::Files) {
        if (files_save_ && focus_id_ == kFieldName) {
            // A on the name opens the on-screen keyboard. Start, and the
            // Save button, still write the file.
            if (show_osk()) {
                focus_id_ = kOsk;
                return;
            }
            on_dialog_button(kSave);
            return;
        }
        activate_file_row();
        return;
    }
    if (dialog_ == Dialog::NewCity) {
        on_dialog_button(new_page_ == 0 ? kNext : kGenerate);
        return;
    }
    if (dialog_ == Dialog::Rename || dialog_ == Dialog::FileRename) {
        on_dialog_button(kRename);
        return;
    }
    if (dialog_ == Dialog::Bindings && focus_id_ >= kListItem && focus_id_ < kListItem + 64) {
        bind_listen_ = !bind_listen_;
        if (bind_listen_) {
            dialog_error_.clear();
        }
    }
}

bool Shell::adjust_focused_slider(int direction, int coarse)
{
    if (coarse < 1) {
        coarse = 1;
    }
    for (const SliderGeom &slider : sliders_) {
        if (slider.id != focus_id_) {
            continue;
        }
        int step = slider.id == kSlider + 1 ? direction : direction * 5;
        step *= coarse;
        int current = tax_;
        if (dialog_ == Dialog::Settings) {
            if (slider.id == kSlider + 1) {
                current = volume_;
                step = direction * 5;
            } else if (slider.id == kSlider + 2) {
                if (scale_percent_ <= 0) {
                    return false;
                }
                current = scale_percent_;
                step = direction * 25;
            }
        } else if (slider.id == kSlider + 2) {
            current = road_;
        } else if (slider.id == kSlider + 3) {
            current = police_;
        } else if (slider.id == kSlider + 4) {
            current = fire_;
        }
        const int value = std::max(slider.min, std::min(slider.max, current + step));
        if (dialog_ == Dialog::Settings) {
            if (slider.id == kSlider + 1) {
                volume_ = value;
                sound_.set_volume(static_cast<float>(volume_) / 100.0f);
            } else if (slider.id == kSlider + 2) {
                scale_percent_ = value;
            }
        } else if (slider.id == kSlider + 1) {
            tax_ = value;
            session_.set_tax(tax_);
        } else if (slider.id == kSlider + 2) {
            road_ = value;
            session_.set_road_funding(road_);
        } else if (slider.id == kSlider + 3) {
            police_ = value;
            session_.set_police_funding(police_);
        } else if (slider.id == kSlider + 4) {
            fire_ = value;
            session_.set_fire_funding(fire_);
        }
        return true;
    }
    return false;
}

void Shell::arm_smoke_pad()
{
    if (!attach_test_pad()) {
        return;
    }
    const int before = tool_index_;
    if (virtual_joy_ != nullptr) {
        SDL_SetJoystickVirtualButton(virtual_joy_, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, true);
    }
    SDL_UpdateJoysticks();
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        handle_event(event);
    }
    if (virtual_joy_ != nullptr) {
        SDL_SetJoystickVirtualButton(virtual_joy_, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, false);
        SDL_UpdateJoysticks();
        while (SDL_PollEvent(&event)) {
            handle_event(event);
        }
    }
    if (tool_index_ == before) {
        SDL_Event pushed{};
        pushed.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
        pushed.gbutton.button = SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER;
        pushed.gbutton.down = true;
        handle_event(pushed);
        // The tool is chosen when the shoulder is released, so a down
        // event alone is not enough.
        pushed.type = SDL_EVENT_GAMEPAD_BUTTON_UP;
        pushed.gbutton.down = false;
        handle_event(pushed);
    }
    if (tool_index_ == before) {
        ran_ok_ = false;
        smoke_error_ = "gamepad shoulder did not cycle tools";
    }
    const int touch_before = tool_index_;
    SDL_Event finger{};
    finger.type = SDL_EVENT_FINGER_DOWN;
    finger.tfinger.fingerID = 1;
    finger.tfinger.x = 0.5f;
    finger.tfinger.y = 0.55f;
    handle_event(finger);
    finger.type = SDL_EVENT_FINGER_UP;
    handle_event(finger);
    (void)touch_before;
    if (!touch_used_) {
        ran_ok_ = false;
        smoke_error_ = "touch path did not run";
    }
}

void SDLCALL Shell::file_callback(void *userdata, const char *const *filelist, int)
{
    if (userdata == nullptr) {
        return;
    }
    static_cast<Shell *>(userdata)->on_file_result(filelist);
}

FaceGlyphs Shell::face_glyph_set() const
{
    int type = SDL_GAMEPAD_TYPE_UNKNOWN;
    int south = SDL_GAMEPAD_BUTTON_LABEL_UNKNOWN;
    if (pad_ != nullptr) {
        type = SDL_GetGamepadType(pad_);
        south = SDL_GetGamepadButtonLabel(pad_, SDL_GAMEPAD_BUTTON_SOUTH);
    }
    const bool playstation = pad_ != nullptr && pad_is_playstation(type, south);
    bool deck_names = deck_;
    if (deck_ && pad_ != nullptr) {
        int others = 0;
        for (SDL_Gamepad *have : pads_) {
            others += have != nullptr && have != pad_ ? 1 : 0;
        }
        deck_names = pad_uses_deck_names(true, true, SDL_GetGamepadVendor(pad_), SDL_GetGamepadProduct(pad_), type,
                                         SDL_GetGamepadName(pad_), others);
    }
    return face_glyphs(playstation, deck_names, type);
}

const char *Shell::glyph(SDL_GamepadButton button) const
{
    const FaceGlyphs glyphs = face_glyph_set();
    switch (button) {
    case SDL_GAMEPAD_BUTTON_SOUTH:
        return glyphs.south;
    case SDL_GAMEPAD_BUTTON_EAST:
        return glyphs.east;
    case SDL_GAMEPAD_BUTTON_WEST:
        return glyphs.west;
    case SDL_GAMEPAD_BUTTON_NORTH:
        return glyphs.north;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
        return glyphs.left;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
        return glyphs.right;
    case SDL_GAMEPAD_BUTTON_START:
        return glyphs.menu;
    case SDL_GAMEPAD_BUTTON_BACK:
        return glyphs.view;
    case SDL_GAMEPAD_BUTTON_LEFT_STICK:
        return glyphs.l3;
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK:
        return glyphs.r3;
    default:
        return "";
    }
}

std::string Shell::chord_token(SDL_Keycode key, int mods) const
{
    std::string token;
    if ((mods & static_cast<int>(AccelMod::Ctrl)) != 0) {
        token += "ctrl+";
    }
    if ((mods & static_cast<int>(AccelMod::Shift)) != 0) {
        token += "shift+";
    }
    if ((mods & static_cast<int>(AccelMod::Alt)) != 0) {
        token += "alt+";
    }
    std::string name;
    if (key >= 'a' && key <= 'z') {
        name = std::string(1, static_cast<char>(key));
    } else if (key >= 'A' && key <= 'Z') {
        name = std::string(1, static_cast<char>(key - 'A' + 'a'));
    } else if (key >= '0' && key <= '9') {
        name = std::string(1, static_cast<char>(key));
    } else {
        switch (key) {
        case SDLK_SPACE:
            name = "space";
            break;
        case SDLK_EQUALS:
            name = "equal";
            break;
        case SDLK_PLUS:
            name = "plus";
            break;
        case SDLK_MINUS:
            name = "minus";
            break;
        case SDLK_KP_PLUS:
            name = "kp_add";
            break;
        case SDLK_KP_MINUS:
            name = "kp_sub";
            break;
        case SDLK_F1:
        case SDLK_F2:
        case SDLK_F3:
        case SDLK_F4:
        case SDLK_F5:
        case SDLK_F6:
        case SDLK_F7:
        case SDLK_F8:
        case SDLK_F9:
        case SDLK_F10:
        case SDLK_F11:
        case SDLK_F12:
            name = "f" + std::to_string(static_cast<int>(key - SDLK_F1) + 1);
            break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            name = "return";
            break;
        case SDLK_ESCAPE:
            name = "escape";
            break;
        case SDLK_UP:
            name = "up";
            break;
        case SDLK_DOWN:
            name = "down";
            break;
        case SDLK_LEFT:
            name = "left";
            break;
        case SDLK_RIGHT:
            name = "right";
            break;
        default:
            break;
        }
    }
    if (name.empty()) {
        return {};
    }
    token += name;
    return normalize_chord(token);
}

void Shell::apply_key_act(int act)
{
    switch (act) {
    case KeyPause:
        toggle_pause();
        break;
    case KeyNewCity:
        apply_action(Action::NewCity);
        break;
    case KeyLoad:
        apply_action(Action::Load);
        break;
    case KeySave:
        apply_action(Action::Save);
        break;
    case KeySaveAs:
        apply_action(Action::SaveAs);
        break;
    case KeyQuit:
        apply_action(Action::Quit);
        break;
    case KeySlow:
        apply_action(Action::Slow);
        break;
    case KeyMedium:
        apply_action(Action::Medium);
        break;
    case KeyFast:
        apply_action(Action::Fast);
        break;
    case KeyZoomIn:
        zoom_by(2);
        break;
    case KeyZoomOut:
        zoom_by(-2);
        break;
    case KeyBudget:
        apply_action(Action::Budget);
        break;
    case KeyGraphs:
        apply_action(Action::Graphs);
        break;
    case KeyEvaluation:
        apply_action(Action::Evaluation);
        break;
    case KeyFullscreen:
        toggle_fullscreen();
        break;
    default:
        if (act >= KeyTool0 && act < KeyActCount) {
            select_tool(act - KeyTool0);
        }
        break;
    }
}

bool Shell::take_bound_key(SDL_Keycode key, int mods)
{
    const std::string token = chord_token(key, mods);
    if (token.empty()) {
        return false;
    }
    const int act = key_act_for_token(binds_, token);
    if (act >= 0) {
        apply_key_act(act);
        return true;
    }
    return token_is_shadowed(binds_, token);
}

void Shell::sync_pointer_cursor()
{
    if (gamepad_nav_) {
        SDL_HideCursor();
    } else {
        SDL_ShowCursor();
    }
}

void Shell::scrub_graph(int direction)
{
    if (graph_chart_w_ < 2 || direction == 0) {
        return;
    }
    const int step = std::max(4, static_cast<int>(graph_chart_w_ / 20.0));
    if (graph_pointer_x_ < 0) {
        graph_pointer_x_ = direction < 0 ? 0 : graph_chart_w_ - 1;
        return;
    }
    graph_pointer_x_ += direction * step;
    if (graph_pointer_x_ < 0) {
        graph_pointer_x_ = 0;
    }
    if (graph_pointer_x_ >= graph_chart_w_) {
        graph_pointer_x_ = graph_chart_w_ - 1;
    }
}

void Shell::fill_random_city_name()
{
    static const char *names[] = {
        "Pine Harbor", "Red Mesa", "Oak Crossing", "Cedar Bluff", "Silver Ford",
        "Little Falls", "West Ridge", "Copper Bend", "Green Hollow", "Stone Ferry",
    };
    const unsigned salt = static_cast<unsigned>(SDL_GetTicks());
    int pick = static_cast<int>(salt % 10);
    if (pick == osk_last_name_) {
        pick = (pick + 1) % 10;
    }
    osk_last_name_ = pick;
    field_a_ = names[pick];
    field_caret_ = static_cast<int>(field_a_.size());
    field_anchor_ = field_caret_;
    clear_resolved_field_error();
}

void Shell::offer_bind_key(const std::string &token)
{
    if (dialog_ != Dialog::Bindings || token.empty()) {
        return;
    }
    if (list_index_ < 0 || list_index_ >= bind_row_count()) {
        return;
    }
    const BindRow spec = bind_row(list_index_);
    if (spec.key < 0) {
        dialog_error_ = "That row takes a button, not a key.";
        dialog_note_ok_ = false;
        bind_listen_ = false;
        return;
    }
    int other = -1;
    const std::string norm = normalize_chord(token);
    for (int i = 0; i < KeyActCount; ++i) {
        if (i != spec.key && binds_.key[i] == norm) {
            other = i;
            break;
        }
    }
    swap_key(binds_, spec.key, token);
    if (other >= 0) {
        dialog_error_ = chord_label(token) + " swapped with " + key_act_label(other) + ".";
        dialog_note_ok_ = true;
    } else {
        dialog_error_.clear();
        dialog_note_ok_ = false;
    }
    bind_listen_ = false;
}

void Shell::offer_bind_pad(int button)
{
    if (dialog_ != Dialog::Bindings) {
        return;
    }
    if (list_index_ < 0 || list_index_ >= bind_row_count()) {
        return;
    }
    const BindRow spec = bind_row(list_index_);
    if (spec.pad < 0) {
        dialog_error_ = "That row takes a key, not a button.";
        dialog_note_ok_ = false;
        bind_listen_ = false;
        return;
    }
    if (button >= 0 && !pad_button_rebindable(button)) {
        dialog_error_ = "That button cannot be remapped.";
        dialog_note_ok_ = false;
        bind_listen_ = false;
        return;
    }
    int other = -1;
    for (int i = 0; i < PadActCount; ++i) {
        if (i != spec.pad && button >= 0 && binds_.pad[i] == button) {
            other = i;
            break;
        }
    }
    if (!swap_pad(binds_, spec.pad, button)) {
        dialog_error_ = "Confirm, Cancel, and Back stay bound.";
        dialog_note_ok_ = false;
        bind_listen_ = false;
        return;
    }
    if (other >= 0) {
        const char *who = "another action";
        for (int i = 0; i < bind_row_count(); ++i) {
            if (bind_row(i).pad == other) {
                who = bind_row(i).label;
                break;
            }
        }
        dialog_error_ = std::string("That button swapped with ") + who + ".";
        dialog_note_ok_ = true;
    } else {
        dialog_error_.clear();
        dialog_note_ok_ = false;
    }
    bind_listen_ = false;
}

void Shell::clear_bind_row(bool pad_side)
{
    if (dialog_ != Dialog::Bindings || list_index_ < 0 || list_index_ >= bind_row_count()) {
        return;
    }
    const BindRow spec = bind_row(list_index_);
    const bool pad_column = pad_side ? spec.pad >= 0 : spec.key < 0 && spec.pad >= 0;
    if (pad_column && pad_act_required(spec.pad)) {
        dialog_error_ = "Confirm, Cancel, and Back stay bound.";
        dialog_note_ok_ = false;
        bind_listen_ = false;
        return;
    }
    bool cleared = false;
    if (pad_side) {
        if (spec.pad >= 0) {
            binds_.pad[spec.pad] = -1;
            cleared = true;
        } else if (spec.key >= 0) {
            binds_.key[spec.key].clear();
            cleared = true;
        }
    } else if (spec.key >= 0) {
        binds_.key[spec.key].clear();
        cleared = true;
    } else if (spec.pad >= 0) {
        binds_.pad[spec.pad] = -1;
        cleared = true;
    }
    bind_listen_ = false;
    if (cleared) {
        dialog_error_ = "Binding cleared.";
        dialog_note_ok_ = true;
    }
}

void Shell::return_to_file_list()
{
    dialog_ = Dialog::Files;
    dialog_error_.clear();
    bind_listen_ = false;
    refresh_files();
    if (files_.empty()) {
        focus_id_ = kCancel;
        list_index_ = 0;
        return;
    }
    if (list_index_ >= static_cast<int>(files_.size())) {
        list_index_ = static_cast<int>(files_.size()) - 1;
    }
    const int row = list_index_ - list_scroll_;
    focus_id_ = row >= 0 && row < 64 ? kListItem + row : kListItem;
}

bool Shell::file_row_selected() const
{
    if (dialog_ != Dialog::Files || list_index_ < 0 || list_index_ >= static_cast<int>(files_.size())) {
        return false;
    }
    const std::string &name = files_[static_cast<std::size_t>(list_index_)];
    return !name.empty() && name != ".." && name.back() != '/';
}

ControlsKeyText Shell::controls_keys() const
{
    ControlsKeyText keys;
    auto put = [&](std::string &dest, int act) {
        const std::string label = chord_label(binds_.key[act]);
        if (!label.empty() && label != "None") {
            dest = label;
        }
    };
    put(keys.pause, KeyPause);
    put(keys.neu, KeyNewCity);
    put(keys.load, KeyLoad);
    put(keys.save, KeySave);
    put(keys.save_as, KeySaveAs);
    put(keys.quit, KeyQuit);
    put(keys.slow, KeySlow);
    put(keys.medium, KeyMedium);
    put(keys.fast, KeyFast);
    put(keys.zoom_in, KeyZoomIn);
    put(keys.zoom_out, KeyZoomOut);
    put(keys.budget, KeyBudget);
    put(keys.graphs, KeyGraphs);
    put(keys.evaluation, KeyEvaluation);
    put(keys.fullscreen, KeyFullscreen);
    for (int i = 0; i < KeyActCount - KeyTool0; ++i) {
        put(keys.tool[i], KeyTool0 + i);
    }
    return keys;
}

void Shell::schedule_autosave()
{
    if (autosave_seconds_ <= 0) {
        next_autosave_ = std::chrono::steady_clock::time_point::max();
        return;
    }
    next_autosave_ = std::chrono::steady_clock::now() + std::chrono::seconds(autosave_seconds_);
}

const char *Shell::glyph_for(int pad_act) const
{
    if (pad_act < 0 || pad_act >= PadActCount || binds_.pad[pad_act] < 0) {
        return "";
    }
    return glyph(static_cast<SDL_GamepadButton>(binds_.pad[pad_act]));
}

ControlsPadNames Shell::controls_pad_names() const
{
    ControlsPadNames names;
    auto named = [](const char *glyph) { return glyph != nullptr && glyph[0] != '\0' ? glyph : "None"; };
    names.confirm = named(glyph_for(PadConfirm));
    names.cancel = named(glyph_for(PadCancel));
    names.west = named(glyph_for(PadQuery));
    names.north = named(glyph_for(PadPause));
    names.left = named(glyph_for(PadPrevTool));
    names.right = named(glyph_for(PadNextTool));
    names.menu = named(glyph_for(PadMenu));
    names.view = named(glyph_for(PadBack));
    names.l3 = named(glyph_for(PadOverlay));
    names.r3 = named(glyph_for(PadCenter));
    // The triggers are not remappable; name them for the pad in hand.
    const FaceGlyphs glyphs = face_glyph_set();
    names.l2 = glyphs.l2;
    names.r2 = glyphs.r2;
    return names;
}

std::string Shell::pad_wording(const std::string &text) const
{
    // Learn to Play names keys and clicks. A Deck or controller player gets
    // the same step in button words.
    if (!(deck_ || gamepad_nav_) || text.empty()) {
        return text;
    }
    tutorial::PadWords words;
    words.confirm = glyph_for(PadConfirm);
    words.query = glyph_for(PadQuery);
    words.pause = glyph_for(PadPause);
    words.prev = glyph_for(PadPrevTool);
    words.next = glyph_for(PadNextTool);
    words.menu = glyph_for(PadMenu);
    return tutorial::pad_text(text, words);
}

void Shell::absorb_placement_note()
{
    CitySession::PlacementNote note;
    if (!session_.take_placement_note(note)) {
        return;
    }
    const ToolFootprint foot = tool_footprint(note.tool);
    play_cue(ui_sounds::cue_for_stroke(note.ok, note.tool == TOOL_BULLDOZER, short_build_reason(note.reason)));
    receipt_on_ = true;
    receipt_ok_ = note.ok;
    if (motion_on() && note.ok) {
        if (note.tool == TOOL_BULLDOZER) {
            puff_on_ = true;
            puff_ms_ = 0;
            puff_tx_ = note.x;
            puff_ty_ = note.y;
        } else {
            pop_tx_ = note.width > 0 ? note.x - note.anchor_x : note.x;
            pop_ty_ = note.height > 0 ? note.y - note.anchor_y : note.y;
            pop_tw_ = note.width > 0 ? note.width : 1;
            pop_th_ = note.height > 0 ? note.height : 1;
            // Zones and plants report their centre; the footprint says how big.
            const int rise_w = note.width > 0 ? note.width : std::max(1, foot.width);
            const int rise_h = note.height > 0 ? note.height : std::max(1, foot.height);
            if (rise_w * rise_h >= 4) {
                // A building: it rises out of the lot. Roads, rails, wires
                // and single park tiles keep the quick outline pop.
                const int rise_x = note.width > 0 ? note.x - note.anchor_x : note.x - foot.cursor_to_left;
                const int rise_y = note.height > 0 ? note.y - note.anchor_y : note.y - foot.cursor_to_top;
                map_.start_rise(rise_x, rise_y, rise_w, rise_h, false);
            } else {
                pop_on_ = true;
                pop_ms_ = 0;
            }
        }
    }
    if (note.width > 0 && note.height > 0) {
        receipt_left_ = note.x - note.anchor_x;
        receipt_top_ = note.y - note.anchor_y;
        receipt_w_ = note.width;
        receipt_h_ = note.height;
    } else {
        receipt_left_ = note.x - foot.cursor_to_left;
        receipt_top_ = note.y - foot.cursor_to_top;
        receipt_w_ = std::max(1, foot.width);
        receipt_h_ = std::max(1, foot.height);
    }
    receipt_age_ = 0;
    if (note.ok) {
        receipt_life_ = 280;
        const std::string amount = civic::dollars(note.spent > 0 ? -note.spent : note.spent);
        if (note.tiles > 1) {
            receipt_text_ = std::to_string(note.tiles) + " tiles \u00b7 " + amount;
        } else {
            receipt_text_ = amount;
        }
    } else {
        receipt_life_ = 1400;
        receipt_text_ = short_build_reason(note.reason);
        receipt_w_ = 1;
        receipt_h_ = 1;
        receipt_left_ = note.x;
        receipt_top_ = note.y;
    }
}

void Shell::queue_milestones(const std::vector<int> &ids)
{
    if (!milestone_notes_) {
        return;
    }
    for (int id : ids) {
        card_queue_.push_back(id);
    }
}

void Shell::note_civic_month()
{
    queue_milestones(session_.note_civic_month(true));
    maybe_mayor_note();
}

void Shell::maybe_mayor_note()
{
    if (!city_chosen_ || welcome_gate_ || terrain_edit_ || dialog_ != Dialog::None || menu_.open()) {
        return;
    }
    const int stamp = mayor_note_stamp(session_.city_year(), session_.city_month());
    if (stamp == mayor_note_checked_) {
        return;
    }
    mayor_note_checked_ = stamp;
    if (!mayor_note_due(stamp, mayor_note_last_)) {
        return;
    }
    session_.update_evaluation();
    const CitySession::Evaluation report = session_.evaluation();
    if (report.problems.empty()) {
        return;
    }
    const CitySession::Problem &top = report.problems[0];
    if (!mayor_note_has_complaint(top.kind, top.votes, report.no_residents)) {
        return;
    }
    shown_message_ = mayor_note_text(top.kind);
    shown_message_serial_ = session_.message_serial();
    hint_until_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(kToastMs);
    mayor_note_kind_ = top.kind;
    mayor_note_last_ = stamp;
}

bool Shell::return_from_overlay()
{
    overlay_on_ = false;
    if (!overlay_return_eval_) {
        return true;
    }
    overlay_return_eval_ = false;
    session_.update_evaluation();
    open_dialog(Dialog::Evaluation);
    return true;
}

void Shell::show_problem_on_map(int kind, bool alternate)
{
    const civic::Advice advice = civic::advice_for(kind);
    const int layer = alternate ? advice.alt_layer : advice.layer;
    const double keep_x = scroll_x_;
    const double keep_y = scroll_y_;
    if (layer < 0) {
        if (!alternate && advice.show != nullptr && std::string(advice.show) == "Budget") {
            if (tax_pause_hold_) {
                dialog_ = Dialog::Budget;
                focus_id_ = budget_cover_ ? kYearBudget : (kSlider + 1);
                return;
            }
            apply_action(Action::Budget);
        }
        return;
    }
    overlay_.set_layer(static_cast<CitySession::MapLayer>(layer));
    overlay_on_ = true;
    overlay_.sync();
    scroll_x_ = keep_x;
    scroll_y_ = keep_y;
    overlay_return_eval_ = true;
    if (dialog_ == Dialog::Evaluation) {
        if (tax_pause_hold_) {
            dialog_ = Dialog::None;
            hover_id_ = 0;
            set_paused_for_dialog();
        } else {
            close_dialog();
        }
    }
}

int shell_map_word(int x, int y, const void *ctx)
{
    const CitySession *session = static_cast<const CitySession *>(ctx);
    if (session == nullptr) {
        return -1;
    }
    return session->map_value(x, y);
}

void Shell::refresh_place_cue()
{
    MapCue cue;
    if (terrain_edit_) {
        // The brush square under the pointer, named for the piece it lays.
        int hx = 0;
        int hy = 0;
        if (dialog_ == Dialog::None && map_.hover_tile(hx, hy)) {
            cue.active = true;
            cue.mark = 1;
            cue.left = hx - terrain_brush_ / 2;
            cue.top = hy - terrain_brush_ / 2;
            cue.width = terrain_brush_;
            cue.height = terrain_brush_;
            if (slate_ || handheld_) {
                cue.tag = std::string(terrain_paint::piece_name(static_cast<terrain_paint::Piece>(terrain_piece_))) +
                          (terrain_brush_ >= terrain_paint::kBrushLarge ? " \u00b7 3x3" : " \u00b7 1x1");
            }
        }
        map_.set_cue(cue);
        return;
    }
    if (receipt_on_) {
        cue.receipt = true;
        cue.receipt_ok = receipt_ok_;
        cue.rleft = receipt_left_;
        cue.rtop = receipt_top_;
        cue.rwidth = receipt_w_;
        cue.rheight = receipt_h_;
        cue.receipt_text = receipt_text_;
    }
    const ToolDef *tool = tool_by_index(tool_index_);
    const int engine = tool != nullptr ? tool->engine_id : -1;
    int hx = 0;
    int hy = 0;
    const bool hovering = map_.hover_tile(hx, hy);
    if (engine == tool_by_index(kToolCount - 1)->engine_id && hovering && dialog_ == Dialog::None && !menu_.open()) {
        if (hx == dwell_x_ && hy == dwell_y_ && dwell_ms_ >= 280) {
            const CitySession::TileInspect card = session_.inspect_tile(hx, hy);
            if (card.zone) {
                cue.zone = true;
                cue.zleft = card.left;
                cue.ztop = card.top;
                cue.zwidth = card.width;
                cue.zheight = card.height;
            }
        }
    }
    const int query_id = tool_by_index(kToolCount - 1)->engine_id;
    const bool placeable = engine != query_id && (engine >= 0 || engine == kSpecialEngineId);
    if (placeable && hovering && dialog_ == Dialog::None && !menu_.open()) {
        const int special = engine == kSpecialEngineId ? placing_special() : -1;
        const PlaceForecast forecast =
            forecast_tile(engine, hx, hy, special, session_.funds(), session_.auto_bulldoze(),
                          [this](int x, int y) { return session_.map_value(x, y); });
        cue.active = true;
        cue.mark = static_cast<int>(forecast.mark) + 1;
        cue.left = forecast.left;
        cue.top = forecast.top;
        cue.width = std::max(1, forecast.width);
        cue.height = std::max(1, forecast.height);
        cue.tag = forecast.tag;
        if ((slate_ || handheld_) && tool != nullptr && engine != kSpecialEngineId && forecast.cost > 0 &&
            (forecast.mark == PlaceMark::Ready || forecast.mark == PlaceMark::Caution) &&
            cue.tag.find('$') == std::string::npos) {
            // Slate shows the price at the cursor even when the forecast has
            // only a word (or nothing) to say.
            cue.tag = (cue.tag.empty() ? std::string(tool->name) : cue.tag) + " \u00b7 " + civic::dollars(forecast.cost);
        }
        if (engine == kSpecialEngineId && special >= 0) {
            cue.sprite = special;
            const std::string blocked = session_.special_menu_detail(special);
            if (!blocked.empty() && blocked[0] != '$') {
                cue.mark = static_cast<int>(PlaceMark::Blocked) + 1;
                cue.tag = blocked;
            }
        }
        if (engine == TOOL_BULLDOZER && forecast.mark == PlaceMark::Ready) {
            int index = -1;
            int cell = 0;
            if (special_sheet_at(hx, hy, shell_map_word, &session_, index, cell) && index >= 0) {
                const SpecialBuilding &building = special_building(index);
                const int columns = std::max(1, building.width);
                const int ox = cell % columns;
                const int oy = cell / columns;
                cue.left = hx - ox;
                cue.top = hy - oy;
                cue.width = building.width;
                cue.height = building.height;
                cue.outline = true;
            }
        }
        if (map_.dragging() && session_.stroke_tiles() > 0 && tile_matches_tool(engine, session_.map_value(hx, hy))) {
            cue.mark = 1;
            if (session_.stroke_tiles() > 1) {
                cue.tag = std::to_string(session_.stroke_tiles()) + " tiles \u00b7 " + civic::dollars(session_.stroke_spent());
            } else {
                cue.tag = civic::dollars(session_.stroke_spent());
            }
        } else if (map_.dragging() && session_.stroke_tiles() > 1 &&
                   (forecast.mark == PlaceMark::Ready || forecast.mark == PlaceMark::Caution)) {
            const long long pending = session_.stroke_spent() + forecast.cost;
            cue.tag = std::to_string(session_.stroke_tiles() + 1) + " tiles \u00b7 " + civic::dollars(pending);
        }
    }
    map_.set_cue(cue);
}

void Shell::play_cue(int cue)
{
    if (!sound_.muted()) {
        sound_.play_cue(cue);
    }
}
