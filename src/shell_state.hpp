// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#pragma once

#include "chart.hpp"
#include "hotspots.hpp"
#include "city_recovery.hpp"
#include "city_session.hpp"
#include "map_view.hpp"
#include "menu_model.hpp"
#include "overlay_map.hpp"
#include "side_widgets.hpp"
#include "sound_player.hpp"
#include "tool_palette.hpp"

#include <SDL3/SDL.h>

#include <cairo.h>

#include <chrono>
#include <mutex>
#include <string>
#include <vector>

enum class Dialog : int {
    None = 0,
    Unsaved,
    Disaster,
    NewCity,
    Scenario,
    Rename,
    About,
    Recovery,
    RecoveryDelete,
    Files,
    FileWait,
    LoadError,
    SaveError,
    Outcome,
    Budget,
    Graphs,
    Evaluation,
    Overlay,
    Query,
    Info,
    Overwrite
};

enum class AfterSave : int { Nothing = 0, NewCity, Load, Scenario, Quit, OpenPath, FilesLoad };

struct Hotspot {
    double x = 0;
    double y = 0;
    double w = 0;
    double h = 0;
    int id = 0;
};

struct SliderGeom {
    int id = 0;
    double x = 0;
    double y = 0;
    double w = 0;
    double h = 0;
    int min = 0;
    int max = 0;
};

class Shell {
public:
    int run(int argc, char **argv);

    void on_file_result(const char *const *filelist);

private:
    bool init_video();
    void bind_session();
    void shutdown();
    void loop();
    void handle_event(const SDL_Event &event);
    void tick_sim(int elapsed_ms);
    void render();
    void layout();
    void draw(cairo_t *cr);
    void draw_menubar(cairo_t *cr);
    void draw_menu_dropdown(cairo_t *cr);
    void draw_dialog(cairo_t *cr);
    void draw_osk(cairo_t *cr, double x, double y, double w, double max_h);

    void apply_action(Action action);
    void on_hotspot(int id, double x, double y);
    void on_dialog_button(int id);
    bool dialog_pauses() const;
    void set_paused_for_dialog();
    void close_dialog();
    void open_dialog(Dialog dialog);
    void proceed(AfterSave next);
    void request_with_save(AfterSave next);
    void begin_load();
    void begin_save_as();
    void begin_files(bool save);
    void refresh_files();
    void begin_system_files(bool save);
    void remember_saved(const std::string &path);
    static void SDLCALL file_callback(void *userdata, const char *const *filelist, int filter);
    void finish_open(const std::string &path);
    void finish_save(const std::string &path);
    void poll_file_dialog();
    void offer_recovery();
    void autosave(bool force);
    void drop_recovery();
    void remember_recent(const std::string &path);
    void load_recent();
    void center_on_fraction(double fx, double fy);
    void clamp_scroll();
    void zoom_by(int delta);
    void set_speed(int speed);
    void toggle_pause();
    void cycle_tool(int delta);
    void select_tool(int index);
    void sync_title();
    void note_message();
    void arm_smoke_pad();
    bool show_osk() const;
    std::string *active_field();
    void type_text(const std::string &text);
    void backspace_field();
    int hit(double x, double y) const;
    void push_hot(double x, double y, double w, double h, int id);
    void push_focus(int id);
    void move_focus(int delta);
    // Up/Down on the on-screen keyboard. Returns false when focus is not a
    // key, or the adjacent row does not exist (the caller then moves linearly).
    bool move_osk_vertically(int direction);
    void activate_focus();
    void button(cairo_t *cr, double x, double y, double w, double h, const char *label, int id, bool primary);
    void label(cairo_t *cr, const char *font, const std::string &text, double x, double y, int wrap);
    void frame_panel(cairo_t *cr, double x, double y, double w, double h, const char *title);
    const char *glyph(SDL_GamepadButton button) const;
    bool adjust_focused_slider(int direction);
    bool nudge_file_list(int delta, bool page, bool force);
    void reveal_file_list();
    void run_review_checks();
    bool attach_test_pad();
    void pump_events();
    void release_pad_drag();
    void gamepad_use_cursor(bool press);
    void drive_ui_test();
    void note_ui_frame(cairo_surface_t *surface);

    CitySession session_;
    MapView map_;
    ToolPalette palette_;
    MinimapView minimap_;
    DemandView demand_;
    OverlayMap overlay_;
    SoundPlayer sound_;
    MenuNav menu_;

    SDL_Window *window_ = nullptr;
    SDL_Renderer *renderer_ = nullptr;
    SDL_Texture *texture_ = nullptr;
    int tex_w_ = 0;
    int tex_h_ = 0;
    SDL_Gamepad *pad_ = nullptr;
    SDL_Joystick *virtual_joy_ = nullptr;

    int win_w_ = 1280;
    int win_h_ = 800;
    int view_x_ = 0;
    int view_y_ = 0;
    int view_w_ = 1;
    int view_h_ = 1;
    int side_x_ = 0;
    int side_w_ = 120;
    int cell_ = 48;
    int menu_h_ = 34;
    int welcome_h_ = 0;
    int status_h_ = 34;
    int message_h_ = 42;
    int pal_y_ = 0;
    int mini_y_ = 0;
    int mini_h_ = 80;
    int demand_y_ = 0;
    int demand_h_ = 100;

    double scroll_x_ = 0;
    double scroll_y_ = 0;
    double pointer_x_ = 0;
    double pointer_y_ = 0;
    bool pointer_down_ = false;
    bool pointer_on_map_ = false;
    int speed_ = 0;
    int paused_from_ = 2;
    int tool_index_ = 3;
    unsigned city_generation_ = 1;
    unsigned save_generation_ = 0;

    Dialog dialog_ = Dialog::None;
    bool budget_is_tax_ = false;
    AfterSave after_save_ = AfterSave::Nothing;
    std::string queued_path_;
    std::vector<std::string> pending_opens_;
    bool quitting_ = false;

    std::string field_a_;
    std::string field_b_;
    std::string dialog_error_;
    std::string info_title_;
    std::string info_body_;
    int new_page_ = 0;
    int new_level_ = 0;
    int new_island_ = -1;
    int new_rivers_ = -1;
    int new_lakes_ = -1;
    int new_trees_ = -1;
    int list_index_ = 0;
    int list_scroll_ = 0;
    int files_visible_ = 1;
    double list_track_y_ = 0;
    double list_track_h_ = 1;
    bool list_dragging_ = false;
    float list_touch_y_ = -1.0f;
    int scenario_index_ = 0;
    bool files_save_ = false;
    std::string files_dir_;
    std::vector<std::string> files_;
    std::vector<std::string> recent_;
    city_recovery::Entry recovery_;
    int recovery_left_ = 0;
    Action disaster_ = Action::None;
    int open_tax_ = 7;
    int open_road_ = 100;
    int open_police_ = 100;
    int open_fire_ = 100;
    int tax_ = 7;
    int road_ = 100;
    int police_ = 100;
    int fire_ = 100;
    bool graphs_long_ = false;
    double overlay_x_ = 0;
    double overlay_y_ = 0;
    double overlay_w_ = 0;
    double overlay_h_ = 0;
    bool query_pinned_ = false;
    int shown_message_serial_ = 0;
    int shown_query_serial_ = 0;
    std::string shown_message_;
    std::chrono::steady_clock::time_point hint_until_{};
    std::chrono::steady_clock::time_point started_{};
    bool recovery_checked_ = false;
    bool welcome_ = true;
    std::string own_recovery_;
    std::string autosave_fingerprint_;
    std::chrono::steady_clock::time_point next_autosave_{};
    int quake_strength_ = 0;
    std::chrono::steady_clock::time_point quake_until_{};
    std::chrono::steady_clock::time_point quake_started_{};

    std::vector<Hotspot> hot_;
    std::vector<Hotspot> next_hot_;
    std::vector<int> focus_order_;
    std::vector<int> next_focus_;
    std::vector<SliderGeom> sliders_;
    std::vector<SliderGeom> next_sliders_;
    int hover_id_ = 0;
    int focus_id_ = 0;
    int slider_drag_ = 0;
    bool gamepad_nav_ = false;
    bool touch_used_ = false;
    bool deck_ = false;
    bool headless_ = false;
    float cursor_x_ = 640;
    float cursor_y_ = 400;
    float stick_x_ = 0;
    float stick_y_ = 0;
    float pan_x_ = 0;
    float pan_y_ = 0;
    float trig_l_ = 0;
    float trig_r_ = 0;
    int zoom_cool_ = 0;
    bool pad_dragging_ = false;
    int pad_repeat_ms_ = 0;
    bool osk_shift_ = false;
    bool ui_test_ = false;
    bool review_ = false;
    int ui_step_ = 0;
    int ui_column_ = 0;
    int ui_drag_frames_ = 0;
    int ui_tax_before_ = 0;
    std::vector<int> ui_map_before_;

    struct Finger {
        SDL_FingerID id = 0;
        float x = 0;
        float y = 0;
    };
    std::vector<Finger> fingers_;
    float pinch_dist_ = 0;

    bool text_input_on_ = false;
    bool file_ready_ = false;
    int file_state_ = 0;
    std::string file_path_;
    std::mutex file_mu_;

    bool smoke_ = false;
    bool demo_exit_ = false;
    std::string demo_;
    std::string shot_path_;
    std::string save_path_env_;
    int smoke_phase_ = 0;
    bool ran_ok_ = true;
    std::string smoke_error_;
    int frame_ = 0;
};
