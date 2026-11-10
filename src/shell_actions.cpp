// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#include "shell_state.hpp"

#include "pad_glyphs.hpp"

#include "atomic_write.hpp"
#include "city_seed.hpp"
#include "file_list.hpp"
#include "list_scroll.hpp"
#include "osk_layout.hpp"
#include "text_edit.hpp"
#include "platform.hpp"
#include "save_path.hpp"
#include "special_buildings.hpp"
#include "tile_atlas.hpp"
#include "placement_hint.hpp"
#include "tools.hpp"
#include "version.hpp"
#include "view_math.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
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
    if (tax_pause_hold_) {
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
    if (!motion_fx_ || map_.dragging()) {
        return;
    }
    scene_fade_.start();
}

void Shell::skip_motion()
{
    scene_fade_.skip();
    panel_settle_ = 0;
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
    return "Continue: " + base;
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
    civic::YearDigest digest = civic::make_year_digest(
        book.previous_funds, book.funds, true, book.estimates, report.population, session_.ledger(), votes, top_name,
        top_votes, top_kind, report.score_delta, report.deltas_known, session_.city_year(), notes);
    digest.ended_year = session_.review_year();
    digest.opening_funds = book.previous_funds;
    digest.closing_funds = book.funds;
    digest.tax_percent = book.tax_percent;
    return digest;
}

void Shell::open_dialog(Dialog dialog, bool prompted)
{
    reset_touch_state();
    menu_.close();
    special_open_ = false;
    dialog_ = dialog;
    dialog_error_.clear();
    panel_settle_ = 0;
    if (motion_fx_ && (dialog == Dialog::Budget || dialog == Dialog::Evaluation || dialog == Dialog::Graphs ||
                       dialog == Dialog::Messages || dialog == Dialog::Welcome || dialog == Dialog::Scenario)) {
        panel_settle_ = 2;
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
    if (dialog == Dialog::NewCity) {
        field_a_ = "New City";
        field_b_ = "auto";
        new_page_ = 0;
        new_level_ = CitySession::kLevelEasy;
        new_island_ = CitySession::kTerrainDefault;
        new_rivers_ = CitySession::kTerrainDefault;
        new_lakes_ = CitySession::kTerrainDefault;
        new_trees_ = CitySession::kTerrainDefault;
        focus_id_ = kFieldName;
        field_select_all(field_a_, field_caret_, field_anchor_);
    } else if (dialog == Dialog::Rename) {
        field_a_ = session_.city_name();
        focus_id_ = kFieldName;
        field_select_all(field_a_, field_caret_, field_anchor_);
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
        focus_id_ = files_.empty() ? kUp : kListItem;
    } else if (dialog == Dialog::Welcome) {
        focus_id_ = kWelcomeNew;
    } else if (dialog == Dialog::QuitConfirm) {
        // Cancel is the focused button, so a stray A does not leave the game.
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
        focus_id_ = kClose;
    } else if (dialog == Dialog::Controls) {
        // Deck already leads with the gamepad page. A desktop pad should too,
        // once that pad was the last input.
        controls_page_ = (gamepad_nav_ && !deck_) ? 1 : 0;
        focus_id_ = kCancel;
    } else if (dialog == Dialog::Settings) {
        open_volume_ = volume_;
        open_wheel_zooms_ = wheel_zooms_;
        open_fullscreen_ = window_ != nullptr && (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) != 0;
        open_scale_percent_ = user_scale_ <= 0.05f ? 0 : static_cast<int>(std::lround(user_scale_ * 100.0f));
        if (open_scale_percent_ < 0) {
            open_scale_percent_ = 0;
        }
        scale_percent_ = open_scale_percent_;
        open_pause_unfocused_ = pause_unfocused_;
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

int Shell::special_tool_index() const
{
    for (int i = 0; i < kToolCount; ++i) {
        if (tool_by_index(i)->engine_id == kSpecialEngineId) {
            return i;
        }
    }
    return kToolCount - 1;
}

void Shell::select_tool(int index)
{
    if (index < 0) {
        index = kToolCount - 1;
    }
    if (index >= kToolCount) {
        index = 0;
    }
    const int special = special_tool_index();
    const bool entering = index == special && tool_index_ != index;
    const bool leaving = tool_index_ == special && index != special;
    tool_index_ = index;
    palette_.set_selected(index);
    map_.set_tool(tool_by_index(index)->engine_id);
    if (entering) {
        menu_.close();
        special_open_ = true;
        if (special_cursor_ < 0 || special_cursor_ >= special_building_count()) {
            special_cursor_ = special_choice_ >= 0 ? special_choice_ : 0;
        }
    }
    if (leaving) {
        special_open_ = false;
    }
}

void Shell::cycle_tool(int delta)
{
    if (special_open_) {
        special_open_ = false;
    }
    select_tool(tool_index_ + delta);
}

void Shell::press_palette(double x, double y)
{
    const int index = palette_.index_at(x, y);
    if (index >= 0 && index == tool_index_ && tool_by_index(index)->engine_id == kSpecialEngineId) {
        special_open_ = !special_open_;
        if (special_open_) {
            menu_.close();
            if (special_cursor_ < 0 || special_cursor_ >= special_building_count()) {
                special_cursor_ = special_choice_ >= 0 ? special_choice_ : 0;
            }
        }
        return;
    }
    palette_.press(x, y);
}

void Shell::move_special_cursor(int delta)
{
    const int count = special_building_count();
    if (count < 1) {
        return;
    }
    int next = special_cursor_ + delta;
    if (next < 0) {
        next = count - 1;
    }
    if (next >= count) {
        next = 0;
    }
    special_cursor_ = next;
}

void Shell::choose_special(int index)
{
    if (index < 0 || index >= special_building_count()) {
        return;
    }
    special_choice_ = index;
    special_cursor_ = index;
    const int slot = special_tool_index();
    if (tool_index_ != slot) {
        select_tool(slot);
    }
    special_open_ = false;
    map_.set_tool(kSpecialEngineId);
}

void Shell::clamp_scroll()
{
    const double max_x = std::max(0, map_.pixel_width() - view_w_);
    const double max_y = std::max(0, map_.pixel_height() - view_h_);
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
        scroll_x_ = scroll_for_map_fraction(fx, ax, view_x_, map_letterbox(view_w_, map_.pixel_width()), map_.pixel_width());
        scroll_y_ = scroll_for_map_fraction(fy, ay, view_y_, map_letterbox(view_h_, map_.pixel_height()), map_.pixel_height());
        clamp_scroll();
    }
    map_ox_ = map_letterbox(view_w_, map_.pixel_width());
    map_oy_ = map_letterbox(view_h_, map_.pixel_height());
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
    scroll_x_ = scroll_for_map_fraction(fx, ax, view_x_, map_letterbox(view_w_, map_.pixel_width()), map_.pixel_width());
    scroll_y_ = scroll_for_map_fraction(fy, ay, view_y_, map_letterbox(view_h_, map_.pixel_height()), map_.pixel_height());
    clamp_scroll();
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
    const int ox = map_letterbox(view_w_, map_.pixel_width());
    const int oy = map_letterbox(view_h_, map_.pixel_height());
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
    cursor_x_ = static_cast<float>(x);
    cursor_y_ = static_cast<float>(y);
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
    if (special_open_ && dialog_ == Dialog::None && !menu_.open()) {
        if (dir == AccelKey::Up || dir == AccelKey::Left) {
            move_special_cursor(-1);
        } else {
            move_special_cursor(1);
        }
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
            move_focus(delta, false);
            return;
        }
        if (focus_is_flat_buttons()) {
            if (dir == AccelKey::Up || dir == AccelKey::Down) {
                return;
            }
            move_focus(delta, false);
            return;
        }
        if ((dir == AccelKey::Left || dir == AccelKey::Right) &&
            adjust_focused_slider(dir == AccelKey::Left ? -1 : 1)) {
            return;
        }
        if ((dir == AccelKey::Up && nudge_dialog_up_down(-1)) || (dir == AccelKey::Down && nudge_dialog_up_down(1))) {
            return;
        }
        move_focus(delta);
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

bool Shell::map_dpad_repeating() const
{
    return nav_repeat_.held && dialog_ == Dialog::None && !menu_.open() && !special_open_ && !pad_dragging_;
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

void Shell::toggle_fullscreen()
{
    if (window_ == nullptr) {
        return;
    }
    const bool full = (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) != 0;
    SDL_SetWindowFullscreen(window_, !full);
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
    city_recovery::write_info(own_recovery_, session_.city_name(), session_.save_path(), session_.date_text());
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
    const int loaded_speed = session_.speed();
    restore_play_preferences();
    if (background_held_) {
        // The file's own speed wins over the speed from before the window
        // was covered. restore_play_preferences may have stored the preference.
        adopt_loaded_speed(loaded_speed);
    }
    center_on_fraction(0.5, 0.5);
    const std::string folder = parent_dir(path);
    if (!folder.empty()) {
        remember_city_folder(folder);
    }
    remember_recent(path);
    sync_title();
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
        autosave_fingerprint_ = session_.change_fingerprint();
        set_speed(0);
        center_on_fraction(0.5, 0.5);
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
        // Same fallback as SVGA: a missing sheet stays Classic on screen,
        // the menu check follows the drawn sheet, and the stored choice stays.
        set_look_preference(Look::Spooky);
        map_.invalidate_all();
        save_window_settings();
        break;
    case Action::Fire:
    case Action::Flood:
    case Action::Tornado:
    case Action::Earthquake:
    case Action::Monster:
    case Action::Meltdown:
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
    case Action::About:
        open_dialog(Dialog::About);
        break;
    case Action::Settings:
        open_dialog(Dialog::Settings);
        break;
    case Action::Controls:
        open_dialog(Dialog::Controls);
        break;
    case Action::HowToPlay:
        open_dialog(Dialog::HowToPlay);
        break;
    case Action::RecentIndex:
    case Action::None:
        break;
    }
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
        if (id == kWheelZoom) {
            wheel_zooms_ = !wheel_zooms_;
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
            user_scale_ = scale_percent_ <= 0 ? 0.0f : static_cast<float>(scale_percent_) / 100.0f;
            sound_.set_volume(static_cast<float>(volume_) / 100.0f);
            if (window_ != nullptr) {
                const bool full = (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) != 0;
                if (full != open_fullscreen_) {
                    toggle_fullscreen();
                }
            }
            refresh_ui_scale();
            close_dialog();
            return;
        }
        if (id == kOk || id == kSave) {
            user_scale_ = scale_percent_ <= 0 ? 0.0f : static_cast<float>(scale_percent_) / 100.0f;
            sound_.set_volume(static_cast<float>(volume_) / 100.0f);
            refresh_ui_scale();
            save_window_settings();
            close_dialog();
            return;
        }
    }
    if (dialog_ == Dialog::Controls) {
        close_dialog();
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
        // Quit, B, and Escape ask first. A second B from New City or the
        // scenario list must not leave the game.
        open_dialog(Dialog::QuitConfirm);
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
            CitySession::NewCitySpec spec;
            spec.name = field_a_;
            spec.seed = seed;
            spec.seed_was_set = set;
            spec.difficulty = new_level_;
            spec.island = new_island_;
            spec.rivers = new_rivers_;
            spec.lakes = new_lakes_;
            spec.trees = new_trees_;
    const bool from_welcome = welcome_gate_;
    welcome_gate_ = false;
    city_chosen_ = true;
    close_dialog();
    const int serial = session_.message_serial();
    ++city_generation_;
    reset_touch_state();
    clear_transient_message();
            session_.new_city(spec);
            if (session_.message_serial() == serial) {
                restore_play_preferences();
            }
            center_on_fraction(0.5, 0.5);
            sync_title();
            if (from_welcome) {
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
                const int id_num = CitySession::scenario_def(index).id;
                ++city_generation_;
                reset_touch_state();
                clear_transient_message();
                if (!session_.load_scenario(id_num)) {
                    info_title_ = "Play Scenario";
                    info_body_ = "Could not start that scenario.";
                    open_dialog(Dialog::Info);
                } else {
                    welcome_gate_ = false;
                    city_chosen_ = true;
                    adopt_loaded_speed(session_.speed());
                    center_on_fraction(0.5, 0.5);
                    sync_title();
                    begin_scene_fade();
                }
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
        if (budget_cover_ && budget_is_tax_ && (id == kCancel || id == kBack)) {
            budget_cover_ = false;
            focus_id_ = kSlider + 1;
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
            const civic::YearDigest digest = year_digest();
            if (digest.locate) {
                center_on_fraction((digest.locate_x + 0.5) / CitySession::kWorldW,
                                   (digest.locate_y + 0.5) / CitySession::kWorldH);
            }
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
    if (special_open_ && dialog_ == Dialog::None && !menu_.open()) {
        if (id >= kSpecialItem && id < kSpecialItem + special_building_count()) {
            choose_special(id - kSpecialItem);
            return;
        }
        if (id == kHotNone) {
            special_open_ = false;
            return;
        }
    }
    if (id >= kMenuTitle && id < kMenuTitle + 5 && dialog_ == Dialog::None) {
        special_open_ = false;
        const int column = id - kMenuTitle;
        if (menu_.open_menu == column) {
            menu_.close();
        } else {
            menu_.open_menu = column;
            menu_.cursor = 0;
            menu_.alt_shown = true;
            menu_.recent_open = false;
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
                *active_field() = names[pick];
                field_caret_ = static_cast<int>(active_field()->size());
                field_anchor_ = field_caret_;
                clear_resolved_field_error();
            }
        } else if (key.kind == OskKind::Shift) {
            osk_shift_ = !osk_shift_;
        } else if (key.text != nullptr) {
            const char *insert = (osk_shift_ && key.text_shift != nullptr) ? key.text_shift : key.text;
            type_text(insert);
        }
        return;
    }
    if (id >= kGraphSeries && id < kGraphSeries + 16 && dialog_ == Dialog::Graphs) {
        graph_mask_ ^= 1 << (id - kGraphSeries);
        focus_id_ = id;
        return;
    }
    if (id == kNewTerrain && dialog_ == Dialog::NewCity) {
        reroll_terrain();
        focus_id_ = id;
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
    if (dialog_ == Dialog::NewCity && (focus_id_ == kFieldName || focus_id_ == kFieldSeed || new_page_ == 0)) {
        if (focus_id_ == kFieldSeed || (new_page_ == 1 && focus_id_ != kFieldName)) {
            if (focus_id_ == kFieldSeed) {
                return &field_b_;
            }
        }
        if (new_page_ == 0 || focus_id_ == kFieldName) {
            return &field_a_;
        }
    }
    if (dialog_ == Dialog::Rename && (focus_id_ == kFieldName || focus_id_ == 0)) {
        return &field_a_;
    }
    if (dialog_ == Dialog::Files && files_save_ &&
        (focus_id_ == kFieldName || (focus_id_ >= kOsk && focus_id_ < kOsk + 64))) {
        return &field_a_;
    }
    if ((dialog_ == Dialog::NewCity && new_page_ == 1 && focus_id_ == kFieldSeed) ||
        (dialog_ == Dialog::Rename) || (dialog_ == Dialog::Files && files_save_ && focus_id_ == kFieldName) ||
        (dialog_ == Dialog::NewCity && new_page_ == 0)) {
        return focus_id_ == kFieldSeed ? &field_b_ : &field_a_;
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

void Shell::push_hot(double x, double y, double w, double h, int id)
{
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
                      focus_id_ == kGenerate;
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
        (focus_id_ == kUp || focus_id_ == kCancel || focus_id_ == kSave || focus_id_ == kOk)) {
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
    if (dialog_ != Dialog::Files && dialog_ != Dialog::Scenario && dialog_ != Dialog::Messages) {
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
    if (notice_sticky_ || !(gamepad_nav_ || deck_)) {
        return;
    }
    std::string text;
    if (speed_ <= 0) {
        text = std::string("Paused. ") + glyph(SDL_GAMEPAD_BUTTON_NORTH) + " resumes";
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
    gamepad_nav_ = true;
    if (notice_sticky_) {
        return;
    }
    std::string text = "Controller connected";
    if (speed_ <= 0) {
        text += ". ";
        text += glyph(SDL_GAMEPAD_BUTTON_NORTH);
        text += " resumes";
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
    case Dialog::Welcome:
        return kWelcomeNew;
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
    const int count = scenarios ? CitySession::kScenarioCount
                                : messages ? static_cast<int>(session_.notices().size())
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
    if ((!files && !scenarios && !messages) || delta == 0) {
        return false;
    }
    const bool on_list = focus_id_ >= kListItem && focus_id_ < kListItem + 64;
    if (!on_list && !force && !page) {
        return false;
    }
    const int count = scenarios ? CitySession::kScenarioCount
                               : messages ? static_cast<int>(session_.notices().size())
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
    if (dialog_ == Dialog::Rename) {
        on_dialog_button(kRename);
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

const char *Shell::glyph(SDL_GamepadButton button) const
{
    int type = SDL_GAMEPAD_TYPE_UNKNOWN;
    int south = SDL_GAMEPAD_BUTTON_LABEL_UNKNOWN;
    if (pad_ != nullptr) {
        type = SDL_GetGamepadType(pad_);
        south = SDL_GetGamepadButtonLabel(pad_, SDL_GAMEPAD_BUTTON_SOUTH);
    }
    const bool playstation = pad_ != nullptr && pad_is_playstation(type, south);
    const FaceGlyphs glyphs = face_glyphs(playstation, deck_, type);
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

void Shell::absorb_placement_note()
{
    CitySession::PlacementNote note;
    if (!session_.take_placement_note(note)) {
        return;
    }
    const ToolFootprint foot = tool_footprint(note.tool);
    receipt_on_ = true;
    receipt_ok_ = note.ok;
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
        const std::string amount = civic::dollars(note.spent);
        if (note.tiles > 1) {
            receipt_text_ = std::to_string(note.tiles) + " tiles \u00b7 \u2212" + amount;
        } else {
            receipt_text_ = "\u2212" + amount;
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

void Shell::refresh_place_cue()
{
    MapCue cue;
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
    const bool placeable = engine >= 0 && engine != tool_by_index(kToolCount - 1)->engine_id;
    if (placeable && hovering && dialog_ == Dialog::None && !menu_.open()) {
        const int special = engine == kSpecialEngineId ? special_choice_ : -1;
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
        if (engine == kSpecialEngineId && special >= 0) {
            const std::string blocked = session_.special_menu_detail(special);
            if (!blocked.empty() && blocked[0] != '$') {
                cue.mark = static_cast<int>(PlaceMark::Blocked) + 1;
                cue.tag = blocked;
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
