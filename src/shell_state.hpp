// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

#pragma once

#include "chart.hpp"
#include "hotspots.hpp"
#include "demand_tick.hpp"
#include "funds_tick.hpp"
#include "look_fx.hpp"
#include "pad_feel.hpp"
#include "city_recovery.hpp"
#include "city_session.hpp"
#include "map_fit.hpp"
#include "map_view.hpp"
#include "menu_model.hpp"
#include "overlay_map.hpp"
#include "redraw.hpp"
#include "present.hpp"
#include "scene_fade.hpp"
#include "side_widgets.hpp"
#include "sound_player.hpp"
#include "terrain_history.hpp"
#include "tool_palette.hpp"
#include "bindings.hpp"
#include "controls_page.hpp"
#include "ui_scale.hpp"

#include <SDL3/SDL.h>

using Look = ui_scale::Look;
using ui_scale::look_from_setting;
using ui_scale::look_to_setting;

#include <cairo.h>

#include <array>
#include <chrono>
#include <memory>
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
    Overwrite,
    Settings,
    Controls,
    HowToPlay,
    Welcome,
    QuitConfirm,
    Messages,
    Ledger,
    FileRename,
    FileDelete,
    Bindings,
    BulldozeConfirm,
    // Leaving the terrain editor with changes: keep editing or discard.
    TerrainDiscard,
    // Map Complete on a map that is mostly water: go back or use it anyway.
    TerrainLowLand,
    // NOTICE and COPYING, opened from About. Closing returns to About.
    License
};

enum class AfterSave : int { Nothing = 0, NewCity, Load, Scenario, Quit, OpenPath, FilesLoad, Recover };

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

// A block of dialog text, in the same coordinates as the hotspots. The
// review pass checks these against each other and against the buttons.
struct TextSpan {
    double x = 0;
    double y = 0;
    double w = 0;
    double h = 0;
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
    void write_final_shot();
    void layout();
    void draw(cairo_t *cr);
    void draw_menubar(cairo_t *cr);
    void draw_info_band(cairo_t *cr);
    void draw_funds_floats(cairo_t *cr);
    void draw_news_line(cairo_t *cr, const std::string &tool_text, const std::string &hint);
    void draw_handheld_hud(cairo_t *cr);
    void draw_minimap_overlay(cairo_t *cr);
    void draw_slate_milestone(cairo_t *cr, double x, double y, double w, double h);
    double minimap_clearance() const;
    void draw_hover_card(cairo_t *cr);
    void refresh_continue_card();
    void apply_skin();
    bool handheld_device() const;
    void draw_menu_dropdown(cairo_t *cr);
    void draw_dialog(cairo_t *cr);
    void draw_welcome_background(cairo_t *cr);
    // Clouds, aircraft lights and sea glints over the title art. The art is
    // never edited; masks built from its pixels keep them to the sky and sea.
    void draw_welcome_life(cairo_t *cr);
    int welcome_ms_ = 0;
    bool welcome_live_ = false;
    int welcome_drawn_step_ = -1;
    bool welcome_frame_due() const { return welcome_live_ && welcome_ms_ / 66 != welcome_drawn_step_; }
    void draw_year_cover(cairo_t *cr, double margin, double band_h);
    civic::YearDigest year_digest() const;
    std::string welcome_continue_label() const;
    void begin_scene_fade();
    void skip_motion();
    void begin_event_pan(int tile_x, int tile_y);
    void note_year_stamp();
    void advance_presentation(int elapsed_ms);
    void note_disaster_fx();
    void paint_scene_fade();
    void draw_osk(cairo_t *cr, double x, double y, double w, double max_h);

    void apply_action(Action action);
    void on_hotspot(int id, double x, double y);
    void on_dialog_button(int id);
    bool dialog_pauses() const;
    void set_paused_for_dialog();
    void close_dialog();
    // Close the dialog. When the launch screen is still the way back in,
    // open it again instead of leaving the window on an untouched city.
    void dismiss_dialog();
    void open_dialog(Dialog dialog, bool prompted = true);
    void arm_modal_grace();
    bool modal_blocks_pointer() const;
    bool modal_blocks_pad() const;
    void open_gamepad_id(SDL_JoystickID id, bool make_active);
    bool jump_list_edge(bool to_end);
    void note_animation();
    void clear_pause_notice();
    void proceed(AfterSave next);
    void request_with_save(AfterSave next);
    void begin_load();
    void begin_save_as();
    void begin_files(bool save);
    void cancel_dialog();
    // Moves the License viewer by delta pixels, clamped to its text.
    void scroll_license(int delta);
    void cycle_speed();
    void center_on_cursor();
    void select_save_name();
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
    // Empty replacement drops the path. Either way the recent file is rewritten.
    void retarget_recent(const std::string &path, const std::string &replacement);
    void load_recent();
    void center_on_fraction(double fx, double fy);
    // Hand the current camera to the session so the next save keeps it,
    // and put back the camera a loaded save kept (else the map centre).
    void note_view_for_save();
    void restore_loaded_view();
    void clamp_scroll();
    void fit_map_to_view();
    double map_local_x(double x) const;
    double map_local_y(double y) const;
    void zoom_by(int delta);
    // Keep the map point under (anchor_x, anchor_y) fixed. Window pixels.
    void zoom_by(int delta, double anchor_x, double anchor_y);
    // Pull a resting gamepad cursor off the grey band beside the map.
    // push_* is the stick; a push outward steps off the port onto the UI.
    // Down is clamped onto the map so the cursor cannot fall into the hint bar.
    void clamp_gamepad_cursor(float push_x, float push_y);
    void toggle_fullscreen();
    void settle_windowed_frame();
    bool windowed_frame_current() const;
    void match_renderer_output();
    void finish_year_and_close(bool jump);
    void toggle_overlay(CitySession::MapLayer layer);
    void reroll_terrain();
    // Terrain editor (New City > Edit Terrain). The editor paints a copy of
    // the pending map on a session of its own that never runs; the city in
    // session_ is untouched and stays paused until a city is generated.
    // The seed and land knobs on the settings page, and whether they parse.
    bool new_city_land(std::array<int, 5> &land) const;
    // The painted map, when it belongs to the land the page shows now.
    const std::vector<unsigned short> *pending_terrain_for_page() const;
    void open_terrain_editor();
    // apply: Map Complete keeps the painted map for the settings page.
    void close_terrain_editor(bool apply);
    // Cancel, Escape, gamepad B: leave at once, or ask first when the map
    // was changed since the editor opened.
    void cancel_terrain_editor();
    // Map Complete: asks first when little of the map is land.
    void complete_terrain_editor();
    // New terrain or another land knob would replace a painted map. The
    // first press warns and returns false; pressing the same control again
    // drops the painted map and returns true.
    bool confirm_drop_terrain(int id);
    void paint_terrain_line(int x0, int y0, int x1, int y1);
    // Ctrl+Z (Cmd+Z on a Mac) and Ctrl+Y / Ctrl+Shift+Z: one brush stroke
    // back or forward. False when there is nothing to undo or redo.
    bool step_terrain_history(bool forward);
    void select_terrain_piece(int index);
    void set_terrain_brush(int size);
    bool on_terrain_hotspot(int id);
    void draw_terrain_palette(cairo_t *cr);
    void draw_terrain_band(cairo_t *cr);
    void draw_terrain_status(cairo_t *cr, const std::string &hint);
    std::string terrain_hint() const;
    // The keyboard hint for the terrain status line, longest form first.
    std::vector<std::string> terrain_hint_forms() const;
    void jump_notice(int index);
    void arm_touch_tool();
    // Drop a pending tap, end a drag, and forget every finger. A drag that
    // already placed tiles keeps them. Nothing here restores the map.
    void reset_touch_state();
    // Ends a left-button minimap drag and its mouse capture.
    void end_mini_drag();
    // Escape's last step, and gamepad Back's: end any map stroke in progress
    // and go back to Inspect, the same as a right-click.
    void escape_to_inspect();
    std::string context_hint() const;
    std::string dialog_title_hint() const;
    std::string idle_message() const;
    void set_speed(int speed);
    void toggle_pause();
    void cycle_tool(int delta);
    void select_tool(int index);
    void press_palette(double x, double y);
    // Table row the map tool places: the fixed building of a special
    // button (Data Center, Micro nuclear, a landmark), -1 for an engine tool.
    int placing_special() const;
    // A tool's hint ("Zoo: $100. ..."), plus why a landmark is locked
    // (population gate or cap) when the city cannot buy it yet.
    std::string tool_hint_now(int index) const;
    // Bit i is set when palette button i is a landmark the city cannot buy.
    unsigned locked_tool_mask() const;
    // Width the minimap overlay takes from the right of the map view, and the
    // horizontal letterbox that keeps a narrow map clear of it.
    int minimap_reserve_x() const;
    int map_letterbox_x() const;
    double scroll_max_x() const;
    void demo_place_gifts(bool query_one);
    void sync_title();
    void note_message();
    void arm_smoke_pad();
    bool show_osk() const;
    // Stays up for the rest of the dialog once a pad or a finger has opened it,
    // so the page does not jump when the pointer moves.
    bool osk_visible() const;
    std::string *active_field();
    void type_text(const std::string &text);
    void backspace_field();
    // Drop a field error once the text no longer has that problem.
    void clear_resolved_field_error();
    void select_field();
    void move_field_caret(int direction, bool extend, bool home, bool end);
    void paste_field();
    int hit(double x, double y) const;
    void push_hot(double x, double y, double w, double h, int id);
    void push_focus(int id);
    void move_focus(int delta, bool wrap = true);
    bool focus_is_flat_buttons() const;
    void leave_text_field();
    void service_gamepad(int elapsed_ms);
    void advance_pad_drag(int dx, int dy, int elapsed_ms);
    void apply_pad_direction(SDL_GamepadButton button);
    // One tile toward the map edge, when that tile is still on screen.
    void step_cursor_on_map(int dx, int dy);
    // Held D-pad is panning or walking the cursor, so the loop stays awake.
    bool map_dpad_repeating() const;
    void note_speed_feedback();
    void note_controller_connected();
    void activate_dialog_start();
    int dialog_accept_id() const;
    int control_px(int design, int floor) const;
    // Up/Down on the on-screen keyboard. Returns false when focus is not a key.
    // The bottom row steps out to the control under the keyboard.
    bool move_osk_vertically(int direction);
    // Up/Down in a dialog: the list, then the keyboard, then a button row.
    // Down on the file-dialog buttons returns to the selected row.
    bool nudge_dialog_up_down(int direction);
    // New City land page. Up and Down keep the column; Left and Right do not.
    bool nudge_new_city_column(int direction);
    // The orange list ring tracks the blue selection.
    void align_list_focus();
    void scroll_menu_by(int rows);
    void scroll_menu_to_pointer(double y);
    void autoscroll_menu();
    void activate_focus();
    void activate_file_row();
    void button(cairo_t *cr, double x, double y, double w, double h, const char *label, int id, bool primary,
                bool selected = false, bool enabled = true);
    void label(cairo_t *cr, const char *font, const std::string &text, double x, double y, int wrap);
    void frame_panel(cairo_t *cr, double x, double y, double w, double h, const char *title);
    void paint_vga_veil(cairo_t *cr);
    void refresh_place_cue();
    void absorb_placement_note();
    // A synthesized interface cue (ui_sounds::Cue), unless sound is off.
    void play_cue(int cue);
    void show_problem_on_map(int kind, bool alternate);
    void queue_milestones(const std::vector<int> &ids);
    void note_civic_month();
    void maybe_mayor_note();
    bool return_from_overlay();
    const char *glyph(SDL_GamepadButton button) const;
    const char *glyph_for(int pad_act) const;
    bool take_bound_key(SDL_Keycode key, int mods);
    void apply_key_act(int act);
    void sync_pointer_cursor();
    void scrub_graph(int direction);
    void fill_random_city_name();
    void offer_bind_key(const std::string &token);
    void offer_bind_pad(int button);
    // pad_side clears the button column. Otherwise the key column. A row with
    // only one of them clears that one.
    void clear_bind_row(bool pad_side);
    void return_to_file_list();
    bool file_row_selected() const;
    std::string chord_token(SDL_Keycode key, int mods) const;
    ControlsKeyText controls_keys() const;
    void schedule_autosave();
    bool adjust_focused_slider(int direction, int coarse = 1);
    bool nudge_file_list(int delta, bool page, bool force);
    void reveal_file_list();
    // Touch drag. Moves the window and leaves the selection where it is.
    void scroll_open_list(int delta);
    // Keep a touch-scrolled window put. A selection change reveals that row.
    void place_list_window(int index, int count);
    void run_review_checks();
    int dp(int px) const;
    bool minimap_engraved_active() const;
    // Paper or frame around the minimap, and its bottom edge including the
    // engraved caption strip.
    int mini_pad() const;
    int mini_bottom() const;
    const char *ui_font() const;
    const char *body_font() const;
    const char *title_font() const;
    const char *menu_font() const;
    const char *bold_font() const;
    void refresh_ui_scale();
    // What Auto resolves to on this display, in percent (Settings label).
    int auto_scale_percent() const;
    bool allow_saved_settings() const;
    void load_window_settings();
    void save_window_settings();
    void note_window_geometry();
    void apply_loaded_settings();
    // Remember the player's look. The drawn sheet may stay Classic.
    void set_look_preference(Look look);
    void clear_transient_message();
    void note_background_hold();
    void release_occlusion();
    // Apply a speed that a load just established. While the window is held,
    // that speed is what resume will run.
    void adopt_loaded_speed(int loaded);
    // Put the global preferences back after a city file's own options.
    void restore_play_preferences();
    void note_keyboard_input();
    void note_background_write_failed(const char *text);
    void dismiss_background_notice();
    std::string visual_stamp() const;
    redraw::Reasons redraw_reasons() const;
    bool attach_test_pad();
    void pump_events();
    void release_pad_drag();
    void forget_gamepad(SDL_JoystickID id);
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
    std::vector<SDL_Gamepad *> pads_;
    SDL_Joystick *virtual_joy_ = nullptr;

    int win_w_ = 1280;
    int win_h_ = 800;
    float ui_scale_ = 1.0f;
    float user_scale_ = 0.0f;
    // The sheet on screen: 0 Classic, 1 SVGA, 2 Spooky. A missing sheet stays 0.
    int drawn_look_ = 0;
    ui_scale::LookPreference look_pref_{};
    float scale_env_ = 0.0f;
    std::string font_ui_ = "Sans 13";
    std::string font_body_ = "Sans 14";
    std::string font_title_ = "Sans Bold 18";
    std::string font_menu_ = "Sans 13";
    std::string font_bold_ = "Sans Bold 13";
    bool has_saved_window_ = false;
    bool want_maximized_ = false;
    bool size_override_ = false;
    int saved_x_ = 0;
    int saved_y_ = 0;
    int saved_w_ = 1280;
    int saved_h_ = 800;
    int normal_x_ = 0;
    int normal_y_ = 0;
    int normal_w_ = 1280;
    int normal_h_ = 800;
    bool normal_valid_ = false;
    bool windowed_maximized_ = false;
    // Set while fullscreen is being left, so a screen-sized resize is not
    // stored as the windowed rectangle.
    bool restoring_windowed_ = false;
    // Pixel density of the windowed frame. Fullscreen must not replace it.
    float windowed_density_ = 1.0f;
    int windowed_settle_tries_ = 0;
    int fullscreen_hold_frames_ = 0;
    int view_x_ = 0;
    int view_y_ = 0;
    int view_w_ = 1;
    int map_ox_ = 0;
    int map_oy_ = 0;
    int view_h_ = 1;
    int side_x_ = 0;
    int side_w_ = 120;
    int cell_ = 48;
    int menu_h_ = 34;
    int status_h_ = 34;
    int pal_y_ = 0;
    int mini_y_ = 0;
    int mini_h_ = 80;
    int demand_y_ = 0;
    int demand_h_ = 100;
    // Interface skin. Slate is the default; Light is the earlier cream chrome.
    bool slate_ = true;
    bool open_slate_ = true;
    // Night lighting over the map. Off unless the player turns it on.
    bool night_ = false;
    bool open_night_ = false;
    // Engraved paper minimap (SVGA and Spooky looks). Classic always keeps
    // the tile-colour minimap.
    bool minimap_engraved_ = true;
    bool open_minimap_engraved_ = true;
    // 0 Auto (handheld on a Steam Deck), 1 Desktop, 2 Handheld.
    int layout_pref_ = 0;
    int open_layout_pref_ = 0;
    // Resolved by layout(): the full-screen map with the quick bar.
    bool handheld_ = false;
    // Slate info band under the menu bar (desktop layout only).
    int band_y_ = 0;
    int band_h_ = 0;
    bool band_goal_shown_ = false; // the Slate band drew its GOAL segment
    // Minimap rectangle. The desktop tray uses side_x_/side_w_.
    int mini_x_ = 0;
    int mini_w_ = 0;
    // Handheld HUD: space the map keeps clear at each edge for chrome.
    int hud_top_ = 0;
    int hud_bottom_ = 0;
    int hud_right_ = 0;
    int quick_y_ = 0;
    double quick_x_ = 0;
    double quick_slot_ = 0;
    int quick_h_ = 0;
    int hint_h_ = 0;
    // Continue card on the title screen: the last city's glance, cached.
    std::string continue_path_;
    bool continue_ok_ = false;
    std::string continue_name_;
    std::string continue_detail_;
    std::vector<std::uint32_t> continue_thumb_; // 120x100 ARGB, empty when unreadable
    // Small caps for the Slate labels, rebuilt when the UI scale changes.
    const char *tag_font() const;
    mutable std::string font_tag_;
    // locked_tool_mask() walks the map for the landmark caps. It is drawn
    // every frame, so the answer is kept until the sim ticks, the map
    // changes or money moves (CitySession::change_fingerprint).
    mutable std::string locked_mask_key_;
    mutable unsigned locked_mask_ = 0;
    mutable float font_tag_scale_ = -1.0f;

    double scroll_x_ = 0;
    double scroll_y_ = 0;
    double pointer_x_ = 0;
    double pointer_y_ = 0;
    bool pointer_down_ = false;
    bool pointer_on_map_ = false;
    int speed_ = 0;
    // Global preferences. A loaded city does not replace these.
    int pref_speed_ = -1;
    int pref_disasters_ = -1;
    int pref_auto_budget_ = -1;
    int pref_auto_bulldoze_ = -1;
    int pref_auto_goto_ = -1;
    // 1 sound on, 0 muted. A loaded city must not replace this.
    int pref_sound_ = -1;
    std::chrono::steady_clock::time_point modal_grace_until_{};
    bool modal_swallow_mouse_ = false;
    bool modal_swallow_pad_ = false;
    int anim_epoch_ = 0;
    int anim_slice_ = 0;
    unsigned tile_sample_ = 0;
    unsigned sprite_sample_ = 0;
    bool samples_ready_ = false;
    int volume_ = 100;
    int scale_percent_ = 0;
    bool pause_unfocused_ = false;
    bool paused_for_unfocus_ = false;
    int unfocus_speed_ = 0;
    int open_volume_ = 100;
    int open_scale_percent_ = 0;
    bool open_pause_unfocused_ = false;
    ui_scale::StoredSettings loaded_{};
    int paused_from_ = 2;
    int tool_index_ = 26; // kDefaultToolIndex (Query)
    unsigned city_generation_ = 1;
    // The rolling Funds readout and its floating change; the band or the
    // handheld chip sets the float anchor each frame it draws.
    FundsTicker funds_tick_;
    DemandTicker demand_tick_;
    int crisis_cycle_ = 0;
    int traffic_peak_ = 0;
    int traffic_month_ = -1;
    unsigned traffic_peak_generation_ = 0;
    double funds_float_x_ = -1.0;
    double funds_float_y_ = 0.0;
    unsigned save_generation_ = 0;

    Dialog dialog_ = Dialog::None;
    // A5: the building a bulldozer click is waiting to clear.
    int bulldoze_x_ = -1;
    int bulldoze_y_ = -1;
    int bulldoze_cost_ = 0;
    std::string bulldoze_name_;
    bool bulldoze_drag_noted_ = false;
    bool budget_is_tax_ = false;
    bool budget_cover_ = false;
    bool tax_pause_hold_ = false;
    bool overlay_return_eval_ = false;
    int eval_why_ = -1;
    int dwell_x_ = -999;
    int dwell_y_ = -999;
    int dwell_ms_ = 0;
    bool milestone_notes_ = true;
    int card_id_ = -1;
    int card_ms_ = 0;
    // Milestone ceremony (effects_fx): ms since it began, -1 when idle.
    // ceremony_won_ puts it on the Scenario won box instead of the card.
    int ceremony_ms_ = -1;
    bool ceremony_won_ = false;
    void draw_ceremony(cairo_t *cr, double x, double y, double w, double h);
    // Weather over the map view: rain, snow or a storm for the city's
    // month (effects_fx::weather_for). Cosmetic, on the ambient clock.
    void draw_weather(cairo_t *cr);
    int weather_kind_ = 0;
    int weather_prev_ = 0;
    int weather_since_ = 0;
    int weather_stamp_ = -1;
    bool weather_live_ = false;
    int weather_drawn_step_ = -1;
    bool weather_frame_due() const { return weather_live_ && map_.fx_ms() / 50 != weather_drawn_step_; }
    // The last toast chime; a burst of notices chimes once.
    std::chrono::steady_clock::time_point toast_cue_at_{};
    std::vector<int> card_queue_;
    int hover_edge_ms_ = 0;
    int tracked_hover_ = -1;
    bool receipt_on_ = false;
    bool receipt_ok_ = false;
    int receipt_left_ = 0;
    int receipt_top_ = 0;
    int receipt_w_ = 1;
    int receipt_h_ = 1;
    int receipt_age_ = 0;
    int receipt_life_ = 280;
    std::string receipt_text_;
    AfterSave after_save_ = AfterSave::Nothing;
    std::string queued_path_;
    std::vector<std::string> pending_opens_;
    bool quitting_ = false;

    std::string field_a_;
    std::string field_b_;
    int field_caret_ = 0;
    int field_anchor_ = 0;
    std::string dialog_error_;
    // A note in dialog_error_ that is a result, not a problem. Drawn in ink,
    // not the red used for a name or a file error.
    bool dialog_note_ok_ = false;
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
    // File and scenario scrollbar. Messages has no track, so it never sets this.
    // A touch does not synthesize a mouse-up, so this also clears on finger
    // up or cancel, in reset_touch_state(), and on buttonless mouse motion.
    bool list_dragging_ = false;
    // Set while a touch drag has moved the window away from the selection.
    // Paint must not snap the scroll back until the selection itself moves.
    bool list_scroll_free_ = false;
    float list_touch_y_ = -1.0f;
    float list_touch_origin_x_ = 0;
    float list_touch_origin_y_ = 0;
    bool list_touch_row_ = false;
    bool list_touch_dragged_ = false;
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
    int graph_mask_ = 0x7f;
    bool graph_rolled_ = false;
    int graph_pointer_x_ = -1;
    double graph_chart_x_ = 0;
    double graph_chart_y_ = 0;
    double graph_chart_w_ = 0;
    double graph_chart_h_ = 0;
    std::string graph_readout_;
    bool overlay_on_ = false;
    bool wheel_zooms_ = false;
    bool open_wheel_zooms_ = false;
    // Screen shake for quakes and other disasters (Settings).
    bool screen_shake_ = true;
    bool open_screen_shake_ = true;
    bool open_fullscreen_ = false;
    int controls_page_ = 0;
    int controls_scroll_ = 0;
    int controls_scroll_max_ = 0;
    // The License viewer: its text (loaded when opened), the scroll offset
    // in pixels, the largest offset and the visible height from the last
    // frame (Page Up/Down step), and the finger's last y for a touch drag.
    std::string license_text_;
    int license_scroll_ = 0;
    int license_scroll_max_ = 0;
    int license_view_h_ = 0;
    // How to play: pixel scroll when the two columns are taller than the window.
    int howto_scroll_ = 0;
    int howto_scroll_max_ = 0;
    int howto_view_h_ = 0;
    double license_view_x_ = 0;
    double license_view_y_ = 0;
    double license_view_w_ = 0;
    float license_touch_y_ = -1.0f;
    int osk_last_name_ = -1;
    bool goal_dismissed_ = false;
    int goal_scenario_ = -1;
    std::chrono::steady_clock::time_point goal_until_{};
    bool pause_noted_ = false;
    bool touch_pending_ = false;
    bool touch_armed_ = false;
    bool touch_minimap_ = false;
    // Left button went down on the minimap and is still held: motion keeps
    // recentring the view, even past the minimap's edge.
    bool mini_drag_ = false;
    float touch_down_x_ = 0;
    float touch_down_y_ = 0;
    bool pan_pointer_ = false;
    bool pan_moved_ = false;
    int pan_button_ = 0;
    double pan_start_x_ = 0;
    double pan_start_y_ = 0;
    double pan_scroll_x_ = 0;
    double pan_scroll_y_ = 0;
    int terrain_key_ = 0;
    bool terrain_edit_ = false;
    std::unique_ptr<CitySession> terrain_session_;
    // UI self-test scratch for the terrain editor steps.
    std::vector<unsigned short> ui_city_before_;
    std::vector<unsigned short> ui_terrain_painted_;
    // The map being painted, in rows (y * kWorldW + x).
    std::vector<unsigned short> terrain_work_;
    // Map Complete's result and the seed and knobs it was painted over
    // (seed, island, rivers, lakes, trees). Used only while those match.
    std::vector<unsigned short> pending_terrain_;
    std::array<int, 5> pending_land_{};
    std::array<int, 5> terrain_land_{};
    int terrain_piece_ = 0;
    int terrain_brush_ = 1;
    // Map words changed since the editor opened; Cancel asks first when > 0.
    int terrain_changed_ = 0;
    // Brush strokes to undo and redo, and the map as the editor opened it
    // (an undo back to it leaves nothing for Cancel to ask about).
    TerrainHistory terrain_history_;
    std::vector<unsigned short> terrain_opened_;
    // A settings control that warned it would drop the painted map.
    int terrain_confirm_id_ = 0;
    // The game's view, put back when the editor closes.
    int terrain_saved_tile_ = 16;
    double terrain_saved_scroll_x_ = 0;
    double terrain_saved_scroll_y_ = 0;
    bool terrain_saved_overlay_ = false;
    // The seed an "auto" field stands for while the New City dialog is up.
    // The land preview draws it and Generate builds it, so they match.
    int auto_seed_ = 1;
    cairo_surface_t *terrain_preview_ = nullptr;
    double overlay_x_ = 0;
    double overlay_y_ = 0;
    double overlay_w_ = 0;
    double overlay_h_ = 0;
    bool query_pinned_ = false;
    bool query_anchored_ = false;
    double query_anchor_x_ = 0;
    double query_anchor_y_ = 0;
    int shown_message_serial_ = 0;
    int shown_query_serial_ = 0;
    std::string shown_message_;
    std::chrono::steady_clock::time_point hint_until_{};
    std::chrono::steady_clock::time_point started_{};
    bool recovery_checked_ = false;
    // Set once the player has picked a city this session. Recovery is not
    // offered after that, and its default button is Not Now if it is.
    bool city_chosen_ = false;
    // True from the launch screen until a city is actually started, loaded,
    // or a scenario begins. Cancelling those flows comes back here.
    bool welcome_gate_ = false;
    // Short dither and panel settle. Off for tests, smoke, and demos so a
    // screenshot is the settled screen. Click or a key skips either one.
    bool motion_fx_ = false;
    // Settings > Effects (look_fx::Effects). Off also stops motion_fx_.
    int effects_ = 0;
    int open_effects_ = 0;
    bool motion_on() const { return motion_fx_ && look_fx::motion_allowed(effects_); }
    void apply_effects();
    bool look_live_ = false;
    SceneFade scene_fade_{};
    // A dialog eases in (slide, and scale with a fade off Classic) over
    // kDialogEaseMs; the Year in Review keeps the clock running while its
    // cards land and the Funds and People figures count up.
    static constexpr int kDialogEaseMs = 170;
    static constexpr int kYearRevealMs = 900;
    int dialog_anim_ms_ = -1;
    // A new city or scenario opens through an iris while the map settles
    // from a slight zoom; -1 is idle.
    static constexpr int kRevealMs = 700;
    int reveal_ms_ = -1;
    bool reveal_fresh_ = false;
    void begin_city_reveal();
    void draw_city_reveal(cairo_t *cr);
    int dialog_anim_len() const;
    bool event_pan_on_ = false;
    double event_from_x_ = 0;
    double event_from_y_ = 0;
    double event_to_x_ = 0;
    double event_to_y_ = 0;
    double event_written_x_ = 0;
    double event_written_y_ = 0;
    int event_pan_ms_ = 0;
    bool marker_on_ = false;
    int marker_tx_ = 0;
    int marker_ty_ = 0;
    int marker_ms_ = -1;
    YearStamp year_stamp_{};
    bool puff_on_ = false;
    int puff_ms_ = 0;
    int puff_tx_ = 0;
    int puff_ty_ = 0;
    bool pop_on_ = false;
    int pop_ms_ = 0;
    int pop_tx_ = 0;
    int pop_ty_ = 0;
    int pop_tw_ = 1;
    int pop_th_ = 1;
    bool flash_on_ = false;
    int flash_ms_ = 0;
    std::string disaster_seen_;
    bool inspect_card_shown_ = false;
    double inspect_card_w_ = 0;
    double inspect_card_h_ = 0;
    double dialog_hot_dy_ = 0;
    double dialog_hot_scale_ = 1.0;
    double dialog_hot_cx_ = 0;
    double dialog_hot_cy_ = 0;
    std::string own_recovery_;
    std::string autosave_fingerprint_;
    bool autosave_warned_ = false;
    bool notice_sticky_ = false;
    std::chrono::steady_clock::time_point next_autosave_{};
    // 0 is off. 60 matches the interval the game used before the setting existed.
    int autosave_seconds_ = 60;
    int open_autosave_ = 60;
    ControlBinds binds_{};
    ControlBinds binds_undo_{};
    bool bind_listen_ = false;
    bool key_cursor_ = false;
    std::string notice_text_;
    std::chrono::steady_clock::time_point notice_until_{};
    // The sticky notice is drawn as the first alert in the map corner. Its
    // rectangle, for the click or tap that dismisses it.
    double notice_bar_x_ = -1;
    double notice_bar_y_ = -1;
    double notice_bar_w_ = 0;
    double notice_bar_h_ = 0;
    // Alerts in the bottom-right corner of the map. Each one lasts about five
    // seconds and fades out. A repeat of the newest refreshes it instead of
    // stacking, and at most three stack at once. When the stack is full,
    // priority beats age so a disaster, pause, briefing, or "Playing …"
    // notice stays visible.
    struct Toast {
        std::string text;
        bool disaster = false;
        std::chrono::steady_clock::time_point until{};
        // civic::advice_for kind to jump to when the toast is clicked. -1 = none.
        int jump_kind = -1;
        int priority = 0;
    };
    std::vector<Toast> toasts_;
    // Last mid-year mayor's note: stamp is year*12+month. -1 = none yet.
    int mayor_note_checked_ = -1;
    int mayor_note_last_ = -1;
    int mayor_note_kind_ = -1;
    std::chrono::steady_clock::time_point toast_seen_until_{};
    std::string toast_last_text_;
    static constexpr int kToastMs = 5000;
    static constexpr int kToastFadeMs = 700;
    static constexpr std::size_t kToastMax = 3;
    void sync_toasts(std::chrono::steady_clock::time_point now);
    void draw_toasts(cairo_t *cr, std::chrono::steady_clock::time_point now);
    int toast_wake_ms(std::chrono::steady_clock::time_point now) const;
    std::string file_target_;
    int quake_strength_ = 0;
    std::chrono::steady_clock::time_point quake_until_{};
    std::chrono::steady_clock::time_point quake_started_{};

    std::vector<Hotspot> hot_;
    std::vector<Hotspot> next_hot_;
    std::vector<int> focus_order_;
    std::vector<int> next_focus_;
    std::vector<SliderGeom> sliders_;
    std::vector<SliderGeom> next_sliders_;
    std::vector<TextSpan> text_spans_;
    int hover_id_ = 0;
    int focus_id_ = 0;
    int slider_drag_ = 0;
    bool gamepad_nav_ = false;
    bool touch_used_ = false;
    bool osk_latched_ = false;
    double menu_scroll_ = 0;
    int menu_scroll_menu_ = -2;
    int menu_scroll_cursor_ = -2;
    bool menu_thumb_drag_ = false;
    int menu_edge_dir_ = 0;
    std::chrono::steady_clock::time_point menu_edge_at_{};
    double menu_drop_x_ = 0;
    double menu_drop_w_ = 0;
    double menu_drop_h_ = 0;
    double menu_content_h_ = 0;
    double menu_view_limit_ = 0;
    double menu_item_top_[64] = {};
    double menu_item_h_[64] = {};
    int menu_item_n_ = 0;
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
    bool zoom_l_latched_ = false;
    bool zoom_r_latched_ = false;
    bool pad_dragging_ = false;
    int pad_repeat_ms_ = 0;
    int pad_held_dx_ = 0;
    int pad_held_dy_ = 0;
    pad_feel::RepeatClock drag_repeat_{};
    pad_feel::RepeatClock nav_repeat_{};
    SDL_GamepadButton nav_repeat_button_ = SDL_GAMEPAD_BUTTON_INVALID;
    bool y_hold_ = false;
    bool y_hold_fresh_ = false;
    int y_hold_ms_ = 0;
    bool y_hold_fired_ = false;
    // L1/R1 held: the preview tool is shown (section, name, and price) and
    // only selected when the shoulder is released. After a short wait the
    // preview jumps a whole toolbar section (tool_groups::section_jump).
    int shoulder_hold_dir_ = 0;
    bool shoulder_hold_fresh_ = false;
    int shoulder_hold_ms_ = 0;
    int shoulder_hold_steps_ = 0;
    int shoulder_preview_ = -1;
    bool nav_repeat_fresh_ = false;
    bool budget_was_dirty_ = false;
    float raw_lx_ = 0.0f;
    float raw_ly_ = 0.0f;
    float raw_rx_ = 0.0f;
    float raw_ry_ = 0.0f;
    int touch_slider_id_ = 0;
    bool touch_panning_ = false;
    float touch_pan_x_ = 0.0f;
    float touch_pan_y_ = 0.0f;
    Uint32 touch_down_ticks_ = 0;
    bool osk_shift_ = false;
    bool ui_test_ = false;
    bool review_ = false;
    // How many pixels a year-card box was shorter than its text. Zero means
    // every measured line was given a box tall enough to hold it.
    double year_box_shortfall_ = 0;
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
    // A cancelled system dialog must not apply a later answer.
    bool file_ignore_ = false;
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
    bool presented_ = false;
    bool blink_drew_ = false;
    bool window_exposed_ = false;
    bool window_suspended_ = false;
    bool unfocused_ = false;
    bool background_held_ = false;
    int background_speed_ = 0;
    float wheel_acc_y_ = 0.0f;
    int redraws_ = 0;
    int skipped_redraws_ = 0;
    int surface_builds_ = 0;
    std::string visual_stamp_;
    cairo_surface_t *frame_surface_ = nullptr;
};
