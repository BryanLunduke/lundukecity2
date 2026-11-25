// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

#include "shell_state.hpp"

#include "osk_layout.hpp"
#include "special_buildings.hpp"
#include "tool_groups.hpp"
#include "terrain_paint.hpp"
#include "tools.hpp"

#include <cstdlib>
#include <SDL3/SDL.h>

#include <cairo.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

int g_menu_rows = 0;
bool g_menu_seen = false;

const Hotspot *find_hot(const std::vector<Hotspot> &hot, int id, double min_y)
{
    const Hotspot *found = nullptr;
    for (const Hotspot &item : hot) {
        if (item.id == id && item.y >= min_y) {
            found = &item;
        }
    }
    return found;
}

bool channel_at(cairo_surface_t *surface, int x, int y, int &r, int &g, int &b)
{
    if (surface == nullptr || cairo_image_surface_get_format(surface) != CAIRO_FORMAT_ARGB32) {
        return false;
    }
    const int w = cairo_image_surface_get_width(surface);
    const int h = cairo_image_surface_get_height(surface);
    if (x < 0 || y < 0 || x >= w || y >= h) {
        return false;
    }
    const int stride = cairo_image_surface_get_stride(surface);
    const auto *data = cairo_image_surface_get_data(surface);
    std::uint32_t pixel = 0;
    std::memcpy(&pixel, data + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride) + static_cast<std::size_t>(x) * 4u, 4);
    r = static_cast<int>((pixel >> 16) & 255u);
    g = static_cast<int>((pixel >> 8) & 255u);
    b = static_cast<int>(pixel & 255u);
    return true;
}

bool rects_overlap(const Hotspot &a, const Hotspot &b)
{
    return a.x < b.x + b.w - 0.5 && b.x < a.x + a.w - 0.5 && a.y < b.y + b.h - 0.5 && b.y < a.y + a.h - 0.5;
}

} // namespace

bool Shell::attach_test_pad()
{
    if (pad_ != nullptr && virtual_joy_ != nullptr) {
        gamepad_nav_ = true;
        return true;
    }
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_SOUTH) | (1u << SDL_GAMEPAD_BUTTON_EAST) |
                       (1u << SDL_GAMEPAD_BUTTON_WEST) | (1u << SDL_GAMEPAD_BUTTON_NORTH) |
                       (1u << SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) | (1u << SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) |
                       (1u << SDL_GAMEPAD_BUTTON_START) | (1u << SDL_GAMEPAD_BUTTON_DPAD_UP) |
                       (1u << SDL_GAMEPAD_BUTTON_DPAD_DOWN) | (1u << SDL_GAMEPAD_BUTTON_DPAD_LEFT) |
                       (1u << SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
    desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_LEFTX) | (1u << SDL_GAMEPAD_AXIS_LEFTY) |
                     (1u << SDL_GAMEPAD_AXIS_RIGHTX) | (1u << SDL_GAMEPAD_AXIS_RIGHTY) |
                     (1u << SDL_GAMEPAD_AXIS_LEFT_TRIGGER) | (1u << SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
    desc.name = "Lunduke City 2 Pad";
    const SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
    if (id == 0) {
        ran_ok_ = false;
        smoke_error_ = "virtual gamepad did not attach";
        return false;
    }
    if (pad_ == nullptr) {
        pad_ = SDL_OpenGamepad(id);
        if (pad_ != nullptr) {
            pads_.push_back(pad_);
        }
    }
    if (pad_ == nullptr) {
        ran_ok_ = false;
        smoke_error_ = "virtual gamepad did not open";
        return false;
    }
    virtual_joy_ = SDL_GetGamepadJoystick(pad_);
    gamepad_nav_ = true;
    return virtual_joy_ != nullptr;
}

void Shell::pump_events()
{
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) {
            request_with_save(AfterSave::Quit);
        } else {
            handle_event(event);
        }
    }
}

void Shell::release_pad_drag()
{
    map_.release();
    session_.end_tool_stroke();
    pad_dragging_ = false;
    pointer_on_map_ = false;
    pointer_down_ = false;
    pad_repeat_ms_ = 0;
    pad_held_dx_ = 0;
    pad_held_dy_ = 0;
    pad_feel::repeat_clear(drag_repeat_);
}

void Shell::forget_gamepad(SDL_JoystickID id)
{
    if (id == 0) {
        return;
    }
    SDL_Gamepad *closing = nullptr;
    for (auto it = pads_.begin(); it != pads_.end(); ++it) {
        if (*it != nullptr && SDL_GetGamepadID(*it) == id) {
            closing = *it;
            pads_.erase(it);
            break;
        }
    }
    if (closing == nullptr && pad_ != nullptr && SDL_GetGamepadID(pad_) == id) {
        closing = pad_;
    }
    const bool was_active = pad_ != nullptr && SDL_GetGamepadID(pad_) == id;
    if (closing == nullptr && !was_active) {
        return;
    }
    if (closing != nullptr) {
        if (virtual_joy_ != nullptr && SDL_GetGamepadJoystick(closing) == virtual_joy_) {
            virtual_joy_ = nullptr;
        }
        SDL_CloseGamepad(closing);
    }
    if (was_active) {
        if (pad_ == closing) {
            pad_ = nullptr;
        }
        stick_x_ = 0;
        stick_y_ = 0;
        pan_x_ = 0;
        pan_y_ = 0;
        y_hold_ = false;
        y_hold_fresh_ = false;
        y_hold_fired_ = false;
        raw_lx_ = 0;
        raw_ly_ = 0;
        raw_rx_ = 0;
        raw_ry_ = 0;
        trig_l_ = 0;
        trig_r_ = 0;
        zoom_l_latched_ = false;
        zoom_r_latched_ = false;
        release_pad_drag();
    }
    if (pad_ == closing) {
        pad_ = nullptr;
    }
    if (pad_ == nullptr) {
        for (SDL_Gamepad *have : pads_) {
            if (have != nullptr) {
                pad_ = have;
                break;
            }
        }
    }
    if (pad_ == nullptr) {
        int count = 0;
        SDL_JoystickID *ids = SDL_GetGamepads(&count);
        if (ids != nullptr) {
            for (int i = 0; i < count; ++i) {
                if (ids[i] == id) {
                    continue;
                }
                pad_ = SDL_OpenGamepad(ids[i]);
                if (pad_ != nullptr) {
                    pads_.push_back(pad_);
                    break;
                }
            }
            SDL_free(ids);
        }
    }
    if (pad_ != nullptr) {
        return;
    }
    if (gamepad_nav_) {
        set_speed(0);
        shown_message_ = "Controller disconnected";
        shown_message_serial_ = session_.message_serial();
        hint_until_ = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    }
}

void Shell::gamepad_use_cursor(bool press)
{
    if (!press) {
        release_pad_drag();
        return;
    }
    pointer_x_ = cursor_x_;
    pointer_y_ = cursor_y_;
    const int id = hit(pointer_x_, pointer_y_);
    hover_id_ = id;
    if (id == kMapId && dialog_ == Dialog::None && !menu_.open()) {
        pointer_down_ = true;
        pointer_on_map_ = true;
        pad_repeat_ms_ = 0;
        pad_dragging_ = map_.press(map_local_x(pointer_x_), map_local_y(pointer_y_));
        if (!pad_dragging_) {
            pointer_on_map_ = false;
            pointer_down_ = false;
        }
        return;
    }
    if (id == kPaletteId && dialog_ == Dialog::None && !menu_.open()) {
        palette_.press(pointer_x_ - side_x_, pointer_y_ - pal_y_);
        return;
    }
    if (id == kMinimapId && dialog_ == Dialog::None && !menu_.open()) {
        minimap_.press(pointer_x_ - mini_x_, pointer_y_ - mini_y_, mini_w_, mini_h_);
        return;
    }
    on_hotspot(id, pointer_x_, pointer_y_);
}

void Shell::note_ui_frame(cairo_surface_t *surface)
{
    if (!ui_test_ || !ran_ok_) {
        return;
    }
    if (ui_step_ == 1 && menu_.open() && dialog_ == Dialog::None) {
        const int column = ui_column_;
        if (menu_.open_menu != column) {
            ran_ok_ = false;
            smoke_error_ = "menu column " + std::to_string(column) + " was not the open menu";
            quitting_ = true;
            return;
        }
        const MenuEntry *items = menu_table::column_items(column);
        const int count = menu_table::column_count(column);
        for (int i = 0; i < count; ++i) {
            if (items[i].kind == ItemKind::Separator) {
                continue;
            }
            const int id = kMenuItem + column * 40 + i;
            const Hotspot *hot = find_hot(hot_, id, 0);
            if (hot == nullptr) {
                ran_ok_ = false;
                smoke_error_ = "menu row missing: column " + std::to_string(column) + " index " + std::to_string(i);
                quitting_ = true;
                return;
            }
            if (hot->y + hot->h > win_h_ + 1 || hot->x + hot->w > win_w_ + 1) {
                ran_ok_ = false;
                smoke_error_ = "menu row clipped: column " + std::to_string(column) + " index " + std::to_string(i);
                quitting_ = true;
                return;
            }
            const int sx = static_cast<int>(hot->x + 40);
            const int sy = static_cast<int>(hot->y + 4);
            int r = 0;
            int g = 0;
            int b = 0;
            // The Slate skin draws dark menus: a row reads as its plain or
            // highlighted fill instead of the light paper.
            auto near_ink = [&](int ir, int ig, int ib) {
                return std::abs(r - ir) <= 8 && std::abs(g - ig) <= 8 && std::abs(b - ib) <= 8;
            };
            const bool read_ok = channel_at(surface, sx, sy, r, g, b);
            const bool row_ok = (slate_ || handheld_) ? (near_ink(0x23, 0x2C, 0x38) || near_ink(0x3A, 0x4B, 0x63))
                                                      : (r > 170 && g > 170 && b > 170);
            if (!read_ok || !row_ok) {
                ran_ok_ = false;
                smoke_error_ = "menu row drawn under the map: column " + std::to_string(column) + " index " +
                               std::to_string(i) + " pixel " + std::to_string(r) + "," + std::to_string(g) + "," +
                               std::to_string(b);
                quitting_ = true;
                return;
            }
            if (hit(sx, sy) != id) {
                ran_ok_ = false;
                smoke_error_ = "menu row not topmost hit: column " + std::to_string(column) + " index " +
                               std::to_string(i) + " hit " + std::to_string(hit(sx, sy));
                quitting_ = true;
                return;
            }
            ++g_menu_rows;
        }
        g_menu_seen = true;
    }
    if (ui_step_ == 6 && dialog_ == Dialog::Graphs) {
        const Hotspot *close = find_hot(hot_, kClose, menu_h_ + 40);
        if (close == nullptr) {
            ran_ok_ = false;
            smoke_error_ = "graphs close button missing";
            quitting_ = true;
            return;
        }
        const int sx = static_cast<int>(close->x + 8);
        const int sy = static_cast<int>(close->y - 6);
        int r = 0;
        int g = 0;
        int b = 0;
        // The pixel above Close must be bare page: cream paper, or the dark
        // slate page (0x1E2631) when Slate dialogs are on (U4).
        const bool got = channel_at(surface, sx, sy, r, g, b);
        const bool bare = slate_ ? (std::abs(r - 0x1E) <= 12 && std::abs(g - 0x26) <= 12 && std::abs(b - 0x31) <= 12)
                                 : (r >= 160 && g >= 160 && b >= 160);
        if (!got || !bare) {
            ran_ok_ = false;
            smoke_error_ = "graphs legend overlaps Close (" + std::to_string(r) + "," + std::to_string(g) + "," +
                           std::to_string(b) + ")";
            quitting_ = true;
        }
    }
    if (ui_step_ == 18 && dialog_ == Dialog::Unsaved) {
        auto sample = [&](const Hotspot *hot, int &r, int &g, int &b) {
            // Left padding a quarter of the way down: inside the fill (the
            // gradient is still light there), clear of the
            // label and of the focus ring's inner line, which hugs the
            // outline a few pixels in from every edge. (A sample near the
            // top-left corner landed on that line whenever the hotspot sat
            // on a fractional pixel, so the result depended on font metrics
            // and scale, which differ under Wine.)
            return hot != nullptr && channel_at(surface, static_cast<int>(std::floor(hot->x + 10.0)),
                                                static_cast<int>(std::floor(hot->y + std::max(8.0, hot->h / 4.0))), r, g, b);
        };
        const Hotspot *cancel = find_hot(hot_, kCancel, 0);
        const Hotspot *save = find_hot(hot_, kSave, 0);
        int cr = 0;
        int cg = 0;
        int cb = 0;
        int sr = 0;
        int sg = 0;
        int sb = 0;
        if (!sample(cancel, cr, cg, cb) || !sample(save, sr, sg, sb)) {
            ran_ok_ = false;
            smoke_error_ = "save dialog is missing Cancel or Save";
            quitting_ = true;
            return;
        }
        // Map hover must not turn Cancel blue. Save itself stays the focused blue.
        // Slate buttons have a dark face (U4); classic ones are light.
        // A slate face is a little blue-grey (about 53,65,84); lit is the
        // primary blue (about 51,107,184).
        if (cb > cr + (slate_ ? 60 : 20) || (!slate_ && cr < 180)) {
            ran_ok_ = false;
            smoke_error_ = "Cancel lit up while the pointer was over the map (" + std::to_string(cr) + "," +
                           std::to_string(cg) + "," + std::to_string(cb) + ")";
            quitting_ = true;
            return;
        }
        // Focus comes from the model; the pixels then have to show it. The
        // fill stays the primary blue, and the ring is the button's own edge
        // turned focus blue (an unfocused edge is gold). The edge stroke is
        // at least 3px wide, centred on the outline, so the pixel half a
        // pixel inside the top edge is ring at any fractional position.
        int er = 0;
        int eg = 0;
        int eb = 0;
        const bool edge_ok = channel_at(surface, static_cast<int>(std::floor(save->x + save->w / 2.0)),
                                        static_cast<int>(std::floor(save->y + 0.5)), er, eg, eb);
        if (focus_id_ != kSave || sb < sr + 20 || !edge_ok || eb < er + 20) {
            ran_ok_ = false;
            smoke_error_ = "Save is not the focused button (focus " + std::to_string(focus_id_) + ", fill " +
                           std::to_string(sr) + "," + std::to_string(sg) + "," + std::to_string(sb) + ", ring " +
                           std::to_string(er) + "," + std::to_string(eg) + "," + std::to_string(eb) + ")";
            quitting_ = true;
            return;
        }
        // The yellow welcome strip used to sit under the menu. That band is map now.
        int yr = 0;
        int yg = 0;
        int yb = 0;
        if (!channel_at(surface, win_w_ / 2, menu_h_ + 4, yr, yg, yb) || (yr > 230 && yg > 220 && yb > 120 && yb < 210)) {
            ran_ok_ = false;
            smoke_error_ = "the welcome bar is still drawn under the menu";
            quitting_ = true;
        }
    }
}

void Shell::drive_ui_test()
{
    if (!ran_ok_ || quitting_) {
        return;
    }
    auto fail = [&](const std::string &message) {
        ran_ok_ = false;
        smoke_error_ = message;
        quitting_ = true;
    };
    auto tap = [&](SDL_GamepadButton button, const auto &unchanged) {
        if (virtual_joy_ != nullptr) {
            SDL_SetJoystickVirtualButton(virtual_joy_, button, true);
            SDL_UpdateJoysticks();
            pump_events();
        }
        if (unchanged()) {
            SDL_Event event{};
            event.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
            event.gbutton.button = static_cast<Uint8>(button);
            event.gbutton.down = true;
            handle_event(event);
        }
        if (virtual_joy_ != nullptr) {
            SDL_SetJoystickVirtualButton(virtual_joy_, button, false);
            SDL_UpdateJoysticks();
            pump_events();
        }
        if (button == SDL_GAMEPAD_BUTTON_SOUTH) {
            SDL_Event up{};
            up.type = SDL_EVENT_GAMEPAD_BUTTON_UP;
            up.gbutton.button = SDL_GAMEPAD_BUTTON_SOUTH;
            up.gbutton.down = false;
            handle_event(up);
        }
    };
    auto click_at = [&](double x, double y) {
        int ww = 1;
        int wh = 1;
        int pw = 1;
        int ph = 1;
        SDL_GetWindowSize(window_, &ww, &wh);
        SDL_GetWindowSizeInPixels(window_, &pw, &ph);
        SDL_Event event{};
        event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        event.button.button = SDL_BUTTON_LEFT;
        event.button.down = true;
        event.button.clicks = 1;
        event.button.x = static_cast<float>(x * ww / std::max(pw, 1));
        event.button.y = static_cast<float>(y * wh / std::max(ph, 1));
        handle_event(event);
        event.type = SDL_EVENT_MOUSE_BUTTON_UP;
        event.button.down = false;
        handle_event(event);
    };
    auto key_down = [&](SDL_Keycode key) {
        SDL_Event event{};
        event.type = SDL_EVENT_KEY_DOWN;
        event.key.key = key;
        event.key.down = true;
        event.key.mod = SDL_KMOD_NONE;
        handle_event(event);
    };
    auto open_column = [&](int column) {
        menu_.open_menu = column;
        menu_.cursor = 0;
        menu_.recent_open = false;
        menu_.alt_shown = true;
        menu_.activated = Action::None;
    };

    switch (ui_step_) {
    case 0:
        if (!attach_test_pad()) {
            quitting_ = true;
            return;
        }
        if (view_y_ != menu_h_ + band_h_) {
            fail("the welcome bar still reserves a row of the map");
            return;
        }
        if (session_.needs_save_prompt()) {
            fail("startup asked to save a city that has not been edited");
            return;
        }
        {
            // The sprite is centered in the first tool button. Sample that
            // interior, not the button bevel, which is only two or three greys.
            // A balanced row is centred under its section header, so the
            // button is not the palette's top-left corner.
            double cell_x = 0;
            double cell_y = 0;
            if (!palette_.cell_origin(0, cell_x, cell_y)) {
                fail("the first toolbar button has no cell");
                return;
            }
            const int cell = palette_.cell();
            const int box = std::max(8, cell - 12);
            const int origin_x = static_cast<int>(std::lround(cell_x)) + 5;
            const int origin_y = static_cast<int>(std::lround(cell_y)) + 5;
            int colors = 0;
            int seen_r[25] = {};
            int seen_g[25] = {};
            int seen_b[25] = {};
            for (int i = 0; i < 25; ++i) {
                const int sx = side_x_ + origin_x + (box * (1 + (i % 5))) / 6;
                const int sy = pal_y_ + origin_y + (box * (1 + (i / 5))) / 6;
                int r = 0;
                int g = 0;
                int b = 0;
                if (!channel_at(frame_surface_, sx, sy, r, g, b)) {
                    continue;
                }
                bool known = false;
                for (int c = 0; c < colors; ++c) {
                    if (std::abs(seen_r[c] - r) < 12 && std::abs(seen_g[c] - g) < 12 && std::abs(seen_b[c] - b) < 12) {
                        known = true;
                        break;
                    }
                }
                if (!known && colors < 24) {
                    seen_r[colors] = r;
                    seen_g[colors] = g;
                    seen_b[colors] = b;
                    ++colors;
                }
            }
            if (colors < 5) {
                fail("toolbar icons are still flat blocks (" + std::to_string(colors) + " colors)");
                return;
            }
            // The sprite is authored to fill the button face. A 24px icon
            // centered in this slot covers about 60% and leaves a grey margin.
            const int slot = std::max(8, cell - 8);
            const int face_x = static_cast<int>(std::lround(cell_x)) + 4;
            const int face_y = static_cast<int>(std::lround(cell_y)) + 4;
            int min_x = slot;
            int min_y = slot;
            int max_x = -1;
            int max_y = -1;
            int colored = 0;
            for (int y = 0; y < slot; ++y) {
                for (int x = 0; x < slot; ++x) {
                    int r = 0;
                    int g = 0;
                    int b = 0;
                    if (!channel_at(frame_surface_, side_x_ + face_x + x, pal_y_ + face_y + y, r, g, b)) {
                        continue;
                    }
                    if (std::abs(r - g) < 14 && std::abs(g - b) < 14 && r > 170) {
                        continue;
                    }
                    ++colored;
                    min_x = std::min(min_x, x);
                    min_y = std::min(min_y, y);
                    max_x = std::max(max_x, x);
                    max_y = std::max(max_y, y);
                }
            }
            const int span_x = max_x >= min_x ? max_x - min_x + 1 : 0;
            const int span_y = max_y >= min_y ? max_y - min_y + 1 : 0;
            if (colored < (slot * slot) / 4 || span_x * 4 < slot * 3 || span_y * 4 < slot * 3) {
                fail("toolbar icons do not fill the button (" + std::to_string(span_x) + "x" + std::to_string(span_y) +
                     ")");
                return;
            }
        }
        g_menu_rows = 0;
        g_menu_seen = false;
        ui_column_ = 0;
        open_column(0);
        ui_step_ = 1;
        return;
    case 1: {
        if (!g_menu_seen) {
            fail("open menu was not drawn");
            return;
        }
        if (ui_column_ == 0) {
            const int tool = tool_index_;
            tap(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, [&] { return tool_index_ == tool; });
            if (tool_index_ != tool || !menu_.open()) {
                fail("gamepad shoulder reached the toolbar while a menu was open");
                return;
            }
        }
        g_menu_seen = false;
        ++ui_column_;
        if (ui_column_ < menu_table::kColumnCount) {
            open_column(ui_column_);
            return;
        }
        if (g_menu_rows != menu_table::actionable_count()) {
            fail("menu rows seen " + std::to_string(g_menu_rows) + " wanted " +
                 std::to_string(menu_table::actionable_count()));
            return;
        }
        menu_.close();
        ui_step_ = 2;
        return;
    }
    case 2:
        open_column(3);
        ui_step_ = 3;
        return;
    case 3: {
        const Hotspot *hot = find_hot(hot_, kMenuItem + 3 * 40 + 2, 0);
        if (hot == nullptr) {
            fail("Evaluation menu row has no hotspot");
            return;
        }
        click_at(hot->x + hot->w * 0.5, hot->y + hot->h * 0.5);
        if (dialog_ != Dialog::Evaluation || menu_.open()) {
            fail("mouse did not activate Evaluation");
            return;
        }
        ui_step_ = 4;
        return;
    }
    case 4:
        close_dialog();
        open_column(3);
        ui_step_ = 5;
        return;
    case 5: {
        const Hotspot *hot = find_hot(hot_, kMenuItem + 3 * 40 + 1, 0);
        if (hot == nullptr || win_w_ < 1 || win_h_ < 1) {
            fail("Graphs menu row has no hotspot");
            return;
        }
        SDL_Event finger{};
        finger.type = SDL_EVENT_FINGER_DOWN;
        finger.tfinger.fingerID = 7;
        finger.tfinger.x = static_cast<float>((hot->x + hot->w * 0.5) / win_w_);
        finger.tfinger.y = static_cast<float>((hot->y + hot->h * 0.5) / win_h_);
        handle_event(finger);
        finger.type = SDL_EVENT_FINGER_UP;
        handle_event(finger);
        if (dialog_ != Dialog::Graphs || menu_.open()) {
            fail("touch did not activate Graphs");
            return;
        }
        ui_step_ = 6;
        return;
    }
    case 6: {
        const double dw = std::min(860.0, win_w_ - 48.0);
        const double dh = std::min(640.0, win_h_ - 48.0);
        const double x = (win_w_ - dw) / 2.0;
        const double y = (win_h_ - dh) / 2.0;
        const Hotspot *close = find_hot(hot_, kClose, y);
        if (close == nullptr || close->x < x - 1 || close->x + close->w > x + dw + 1 || close->y + close->h > y + dh + 1) {
            fail("graphs Close is outside the dialog");
            return;
        }
        close_dialog();
        info_body_ = "Residential zone - population 120, land value 40, powered";
        open_dialog(Dialog::Query);
        ui_step_ = 7;
        return;
    }
    case 7: {
        const Hotspot *close = find_hot(hot_, kClose, 0);
        if (close == nullptr || close->x < 0 || close->y < menu_h_ || close->x + close->w > win_w_ ||
            close->y + close->h > win_h_) {
            fail("query Close is outside the window");
            return;
        }
        if (close->w > 220) {
            fail("query Close is stretched across the dialog");
            return;
        }
        close_dialog();
        gamepad_nav_ = true;
        open_dialog(Dialog::NewCity);
        ui_step_ = 8;
        return;
    }
    case 8: {
        int key_count = 0;
        osk_keys(key_count);
        std::vector<Hotspot> keys;
        for (const Hotspot &hot : hot_) {
            if (hot.id >= kOsk && hot.id < kOsk + key_count) {
                keys.push_back(hot);
            }
        }
        std::sort(keys.begin(), keys.end(), [](const Hotspot &a, const Hotspot &b) { return a.id < b.id; });
        if (static_cast<int>(keys.size()) != key_count) {
            fail("on-screen keyboard has " + std::to_string(keys.size()) + " keys");
            return;
        }
        for (int i = 0; i < key_count; ++i) {
            for (int j = i + 1; j < key_count; ++j) {
                if (rects_overlap(keys[static_cast<std::size_t>(i)], keys[static_cast<std::size_t>(j)])) {
                    fail("on-screen keyboard keys overlap");
                    return;
                }
            }
        }
        if (win_w_ >= 1200 && win_h_ >= 700) {
            for (const Hotspot &key : keys) {
                if (key.w < 47 || key.h < 47) {
                    fail("on-screen keyboard key is smaller than 48px");
                    return;
                }
            }
        }
        for (int i = 1; i < 10; ++i) {
            if (keys[static_cast<std::size_t>(i)].x <= keys[static_cast<std::size_t>(i - 1)].x) {
                fail("on-screen keyboard top row is not left to right");
                return;
            }
        }
        if (!(keys[10].y > keys[0].y + 8 && keys[10].x > keys[0].x && keys[20].y > keys[10].y + 8)) {
            fail("on-screen keyboard is not QWERTY");
            return;
        }
        const double field_w = std::min(860.0, win_w_ - 48.0);
        const double field_right = (win_w_ - field_w) / 2.0 + field_w - 16.0;
        for (const Hotspot &key : keys) {
            if (key.x + key.w > field_right + 0.75) {
                fail("on-screen keyboard key " + std::to_string(key.id) + " extends past the field");
                return;
            }
        }
        int key_n = 0;
        const OskKey *okeys = osk_keys(key_n);
        std::vector<double> center(static_cast<std::size_t>(key_n), 0);
        std::vector<char> present(static_cast<std::size_t>(key_n), 0);
        for (const Hotspot &key : keys) {
            const int index = key.id - kOsk;
            if (index < 0 || index >= key_n) {
                continue;
            }
            center[static_cast<std::size_t>(index)] = key.x + key.w * 0.5;
            present[static_cast<std::size_t>(index)] = 1;
        }
        if (osk_vertical_target(okeys, key_n, 0, 1, center.data(), present.data()) != 10 ||
            osk_vertical_target(okeys, key_n, 9, 1, center.data(), present.data()) != 18) {
            fail("OSK rows do not line up q-above-a and p-above-l");
            return;
        }
        focus_id_ = kOsk;
        tap(SDL_GAMEPAD_BUTTON_DPAD_DOWN, [&] { return focus_id_ == kOsk; });
        if (focus_id_ != kOsk + 10) {
            fail("D-pad Down from q went to " + std::to_string(focus_id_) + " instead of a");
            return;
        }
        focus_id_ = kOsk + 9;
        tap(SDL_GAMEPAD_BUTTON_DPAD_DOWN, [&] { return focus_id_ == kOsk + 9; });
        if (focus_id_ != kOsk + 18) {
            fail("D-pad Down from p went to " + std::to_string(focus_id_) + " instead of l");
            return;
        }
        tap(SDL_GAMEPAD_BUTTON_DPAD_UP, [&] { return focus_id_ == kOsk + 18; });
        if (focus_id_ != kOsk + 9) {
            fail("D-pad Up from l went to " + std::to_string(focus_id_) + " instead of p");
            return;
        }
        focus_id_ = kOsk;
        tap(SDL_GAMEPAD_BUTTON_DPAD_UP, [&] { return focus_id_ == kOsk; });
        if (focus_id_ != kFieldName) {
            fail("D-pad Up from the top OSK row did not return to the text field");
            return;
        }
        focus_id_ = kOsk + key_n - 1;
        tap(SDL_GAMEPAD_BUTTON_DPAD_DOWN, [&] { return focus_id_ == kOsk + key_n - 1; });
        if (focus_id_ != kCancel) {
            fail("D-pad Down from the last OSK row did not reach Cancel");
            return;
        }
        close_dialog();
        ui_step_ = 9;
        return;
    }
    case 9: {
        const bool was_open = menu_.open();
        tap(SDL_GAMEPAD_BUTTON_START, [&] { return menu_.open() == was_open; });
        if (!menu_.open() || menu_.open_menu != 0) {
            fail("gamepad Start did not open the System menu");
            return;
        }
        ui_step_ = 10;
        return;
    }
    case 10:
        for (int i = 0; i < 3; ++i) {
            const int column = menu_.open_menu;
            tap(SDL_GAMEPAD_BUTTON_DPAD_RIGHT, [&] { return menu_.open_menu == column; });
        }
        if (menu_.open_menu != 3) {
            fail("gamepad could not reach the Analysis menu");
            return;
        }
        tap(SDL_GAMEPAD_BUTTON_SOUTH, [&] { return dialog_ == Dialog::None; });
        if (dialog_ != Dialog::Budget) {
            fail("gamepad did not activate Budget");
            return;
        }
        ui_step_ = 11;
        return;
    case 11: {
        if (focus_id_ != kSlider + 1 || sliders_.empty()) {
            fail("budget did not focus the tax slider");
            return;
        }
        const int tax0 = tax_;
        const int road0 = road_;
        const int focus0 = focus_id_;
        tap(SDL_GAMEPAD_BUTTON_DPAD_RIGHT, [&] { return tax_ == tax0 && focus_id_ == focus0; });
        if (tax_ != tax0 + 1 || focus_id_ != kSlider + 1) {
            fail("gamepad Right changed focus instead of the tax slider");
            return;
        }
        key_down(SDLK_LEFT);
        if (tax_ != tax0 || focus_id_ != kSlider + 1) {
            fail("keyboard Left did not adjust the focused slider");
            return;
        }
        key_down(SDLK_DOWN);
        if (focus_id_ != kSlider + 2) {
            fail("Down did not move focus off the tax slider");
            return;
        }
        key_down(SDLK_LEFT);
        if (road_ != road0 - 5 || focus_id_ != kSlider + 2) {
            fail("Left did not adjust the road slider");
            return;
        }
        key_down(SDLK_UP);
        if (focus_id_ != kSlider + 1) {
            fail("Up did not move focus back to the tax slider");
            return;
        }
        close_dialog();
        ui_step_ = 17;
        return;
    }
    case 17:
        open_dialog(Dialog::Unsaved);
        // Stale map hover. Cancel used to share this id and light up.
        hover_id_ = kMapId;
        ui_step_ = 18;
        return;
    case 18: {
        if (dialog_ != Dialog::Unsaved) {
            fail("save dialog did not open");
            return;
        }
        if (kCancel == kMapId || kOk == kPaletteId || kDiscard == kMinimapId || kWelcomeNew == kMapId ||
            kClose == kMenuTitle) {
            fail("dialog button id still matches a screen region");
            return;
        }
        std::vector<int> seen;
        for (const Hotspot &hot : hot_) {
            if (hot.id == kHotNone) {
                continue;
            }
            if (std::find(seen.begin(), seen.end(), hot.id) != seen.end()) {
                fail("duplicate hotspot id " + std::to_string(hot.id));
                return;
            }
            seen.push_back(hot.id);
        }
        bool saw_cancel = false;
        bool saw_discard = false;
        bool saw_save = false;
        for (int id : focus_order_) {
            if (!is_modal_control(id)) {
                fail("modal focus includes background id " + std::to_string(id));
                return;
            }
            saw_cancel = saw_cancel || id == kCancel;
            saw_discard = saw_discard || id == kDiscard;
            saw_save = saw_save || id == kSave;
        }
        if (!saw_cancel || !saw_discard || !saw_save) {
            fail("save dialog is missing a button in the focus cycle");
            return;
        }
        if (std::find(focus_order_.begin(), focus_order_.end(), kMapId) != focus_order_.end() ||
            std::find(focus_order_.begin(), focus_order_.end(), kMenuTitle) != focus_order_.end()) {
            fail("background hotspot joined the modal focus cycle");
            return;
        }
        const int steps = static_cast<int>(focus_order_.size()) + 1;
        for (int i = 0; i < steps; ++i) {
            const int before = focus_id_;
            tap(SDL_GAMEPAD_BUTTON_DPAD_DOWN, [&] { return focus_id_ == before; });
            if (!is_modal_control(focus_id_)) {
                fail("D-pad left the dialog onto id " + std::to_string(focus_id_));
                return;
            }
        }
        close_dialog();
        ui_step_ = 12;
        return;
    }
    case 12: {
        open_column(0);
        ui_step_ = 13;
        return;
    }
    case 13: {
        const std::int64_t funds = session_.funds();
        const int tool = tool_index_;
        const double side_x = side_x_ + 8;
        const double side_y = menu_h_ + 420.0;
        const double map_x = static_cast<double>(win_w_ - 40);
        const double map_y = view_y_ + view_h_ * 0.5;
        if (!menu_.open() || hit(side_x, side_y) != 0 || hit(map_x, map_y) != 0) {
            fail("open menu did not capture the map and toolbar");
            return;
        }
        click_at(side_x, side_y);
        if (menu_.open() || dialog_ != Dialog::None || tool_index_ != tool || session_.funds() != funds) {
            fail("a toolbar click while the menu was open fell through");
            return;
        }
        open_column(0);
        ui_step_ = 16;
        return;
    }
    case 16: {
        const std::int64_t funds = session_.funds();
        const int tool = tool_index_;
        const double map_x = static_cast<double>(win_w_ - 40);
        const double map_y = view_y_ + view_h_ * 0.5;
        if (!menu_.open() || hit(map_x, map_y) != 0) {
            fail("reopened menu did not capture the map");
            return;
        }
        click_at(map_x, map_y);
        if (menu_.open() || dialog_ != Dialog::None || tool_index_ != tool || session_.funds() != funds) {
            fail("a map click while the menu was open fell through");
            return;
        }
        ui_step_ = 14;
        return;
    }
    case 14: {
        select_tool(1);
        int tx = CitySession::kWorldW / 2;
        int ty = CitySession::kWorldH / 2;
        bool found = false;
        for (int radius = 0; radius < 45 && !found; ++radius) {
            for (int dy = -radius; dy <= radius && !found; ++dy) {
                for (int dx = -radius; dx <= radius; ++dx) {
                    const int x = CitySession::kWorldW / 2 + dx;
                    const int y = CitySession::kWorldH / 2 + dy;
                    if (x < 2 || y < 2 || x + 6 >= CitySession::kWorldW || y + 2 >= CitySession::kWorldH) {
                        continue;
                    }
                    bool dry = true;
                    for (int i = 0; i < 6; ++i) {
                        if (session_.layer_value(CitySession::MapLayer::Water, x + i, y) != 0) {
                            dry = false;
                            break;
                        }
                    }
                    if (dry) {
                        tx = x;
                        ty = y;
                        found = true;
                    }
                }
            }
        }
        if (!found) {
            fail("no dry land for the drag-build");
            return;
        }
        center_on_fraction((tx + 0.5) / CitySession::kWorldW, (ty + 0.5) / CitySession::kWorldH);
        cursor_x_ = static_cast<float>(view_x_ + view_w_ / 2.0);
        cursor_y_ = static_cast<float>(view_y_ + view_h_ / 2.0);
        if (hit(cursor_x_, cursor_y_) != kMapId) {
            fail("gamepad cursor is not on the map");
            return;
        }
        ui_map_before_.assign(static_cast<std::size_t>(CitySession::kWorldW * CitySession::kWorldH), 0);
        for (int y = 0; y < CitySession::kWorldH; ++y) {
            for (int x = 0; x < CitySession::kWorldW; ++x) {
                ui_map_before_[static_cast<std::size_t>(y * CitySession::kWorldW + x)] =
                    session_.map_value(x, y) & 0x03ff;
            }
        }
        stick_x_ = 0;
        stick_y_ = 0;
        if (virtual_joy_ != nullptr) {
            SDL_SetJoystickVirtualButton(virtual_joy_, SDL_GAMEPAD_BUTTON_SOUTH, true);
            SDL_UpdateJoysticks();
            pump_events();
        }
        if (!pad_dragging_) {
            SDL_Event event{};
            event.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
            event.gbutton.button = SDL_GAMEPAD_BUTTON_SOUTH;
            event.gbutton.down = true;
            handle_event(event);
        }
        if (!pad_dragging_) {
            fail("A did not press the map");
            return;
        }
        if (virtual_joy_ != nullptr) {
            SDL_SetJoystickVirtualAxis(virtual_joy_, SDL_GAMEPAD_AXIS_LEFTX, 30000);
            SDL_SetJoystickVirtualButton(virtual_joy_, SDL_GAMEPAD_BUTTON_DPAD_RIGHT, true);
            SDL_UpdateJoysticks();
            pump_events();
        }
        if (stick_x_ < 0.5f) {
            SDL_Event axis{};
            axis.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
            axis.gaxis.axis = SDL_GAMEPAD_AXIS_LEFTX;
            axis.gaxis.value = 30000;
            handle_event(axis);
        }
        if (stick_x_ < 0.5f) {
            fail("stick did not deflect");
            return;
        }
        ui_drag_frames_ = 0;
        ui_step_ = 15;
        return;
    }
    case 15: {
        if (!pad_dragging_) {
            fail("A released the drag before the button came up");
            return;
        }
        ++ui_drag_frames_;
        if (ui_drag_frames_ < 28) {
            return;
        }
        stick_x_ = 0;
        stick_y_ = 0;
        if (virtual_joy_ != nullptr) {
            SDL_SetJoystickVirtualAxis(virtual_joy_, SDL_GAMEPAD_AXIS_LEFTX, 0);
            SDL_SetJoystickVirtualButton(virtual_joy_, SDL_GAMEPAD_BUTTON_DPAD_RIGHT, false);
            SDL_SetJoystickVirtualButton(virtual_joy_, SDL_GAMEPAD_BUTTON_SOUTH, false);
            SDL_UpdateJoysticks();
            pump_events();
        }
        if (pad_dragging_) {
            SDL_Event up{};
            up.type = SDL_EVENT_GAMEPAD_BUTTON_UP;
            up.gbutton.button = SDL_GAMEPAD_BUTTON_SOUTH;
            up.gbutton.down = false;
            handle_event(up);
        }
        if (pad_dragging_) {
            fail("A up did not release the drag");
            return;
        }
        int changed = 0;
        for (int y = 0; y < CitySession::kWorldH; ++y) {
            for (int x = 0; x < CitySession::kWorldW; ++x) {
                const int now = session_.map_value(x, y) & 0x03ff;
                if (now != ui_map_before_[static_cast<std::size_t>(y * CitySession::kWorldW + x)]) {
                    ++changed;
                }
            }
        }
        if (changed < 3) {
            fail("holding A and moving did not drag-build (" + std::to_string(changed) + " tiles)");
            return;
        }
        ui_step_ = 19;
        return;
    }
    case 19:
        if (view_y_ != menu_h_ + band_h_) {
            fail("the welcome bar still reserves a row of the map");
            return;
        }
        welcome_gate_ = true;
        open_dialog(Dialog::Welcome);
        ui_step_ = 20;
        return;
    case 20: {
        if (dialog_ != Dialog::Welcome) {
            fail("the welcome screen did not open");
            return;
        }
        if (find_hot(hot_, kWelcomeNew, 0) == nullptr || find_hot(hot_, kWelcomeLoad, 0) == nullptr ||
            find_hot(hot_, kWelcomeScenario, 0) == nullptr || find_hot(hot_, kWelcomeHowTo, 0) == nullptr ||
            find_hot(hot_, kWelcomeQuit, 0) == nullptr || find_hot(hot_, kWelcomeLearn, 0) == nullptr) {
            fail("the welcome screen is missing a button");
            return;
        }
        const bool can_continue = find_hot(hot_, kWelcomeContinue, 0) != nullptr;
        if (can_continue) {
            if (focus_id_ != kWelcomeContinue) {
                fail("the welcome screen did not focus Continue");
                return;
            }
            tap(SDL_GAMEPAD_BUTTON_DPAD_DOWN, [&] { return focus_id_ == kWelcomeContinue; });
            if (focus_id_ != kWelcomeNew) {
                fail("D-pad Down did not move from Continue to Start a new city");
                return;
            }
        } else if (focus_id_ != kWelcomeNew) {
            fail("the welcome screen did not focus Start a New City");
            return;
        }
        if (!can_continue) {
            tap(SDL_GAMEPAD_BUTTON_DPAD_UP, [&] { return focus_id_ == kWelcomeNew; });
            if (focus_id_ != kWelcomeNew) {
                fail("Welcome focus wrapped onto Quit");
                return;
            }
        }
        // Release nov24 #7: Learn to Play sits beside Start a new city.
        tap(SDL_GAMEPAD_BUTTON_DPAD_DOWN, [&] { return focus_id_ == kWelcomeNew; });
        if (focus_id_ != kWelcomeLearn) {
            fail("D-pad Down did not move to Learn to Play");
            return;
        }
        tap(SDL_GAMEPAD_BUTTON_DPAD_DOWN, [&] { return focus_id_ == kWelcomeLearn; });
        if (focus_id_ != kWelcomeLoad) {
            fail("D-pad Down did not move to Load a Saved City");
            return;
        }
        tap(SDL_GAMEPAD_BUTTON_DPAD_DOWN, [&] { return focus_id_ == kWelcomeLoad; });
        if (focus_id_ != kWelcomeScenario) {
            fail("D-pad Down did not move to Select a Scenario");
            return;
        }
        tap(SDL_GAMEPAD_BUTTON_DPAD_DOWN, [&] { return focus_id_ == kWelcomeScenario; });
        if (focus_id_ != kWelcomeHowTo) {
            fail("D-pad Down did not move to How to play");
            return;
        }
        tap(SDL_GAMEPAD_BUTTON_DPAD_DOWN, [&] { return focus_id_ == kWelcomeHowTo; });
        if (focus_id_ != kWelcomeQuit) {
            fail("D-pad Down did not move to Quit");
            return;
        }
        tap(SDL_GAMEPAD_BUTTON_DPAD_UP, [&] { return focus_id_ == kWelcomeQuit; });
        if (focus_id_ != kWelcomeHowTo) {
            fail("D-pad Up did not return to How to play");
            return;
        }
        tap(SDL_GAMEPAD_BUTTON_DPAD_UP, [&] { return focus_id_ == kWelcomeHowTo; });
        if (focus_id_ != kWelcomeScenario) {
            fail("D-pad Up did not return to Select a Scenario");
            return;
        }
        tap(SDL_GAMEPAD_BUTTON_SOUTH, [&] { return dialog_ == Dialog::Welcome; });
        if (dialog_ != Dialog::Scenario) {
            fail("A did not open the scenario picker");
            return;
        }
        ui_step_ = 21;
        return;
    }
    case 21: {
        if (dialog_ != Dialog::Scenario) {
            fail("the scenario picker closed on its own");
            return;
        }
        const Hotspot *row0 = find_hot(hot_, kListItem, 0);
        const Hotspot *row1 = find_hot(hot_, kListItem + 1, 0);
        if (row0 == nullptr || row1 == nullptr) {
            fail("the scenario list has no rows");
            return;
        }
        auto differ = [&](int dx, int dy) {
            int r0 = 0;
            int g0 = 0;
            int b0 = 0;
            int r1 = 0;
            int g1 = 0;
            int b1 = 0;
            const int x0 = static_cast<int>(row0->x + 14 + dx);
            const int y0 = static_cast<int>(row0->y + 10 + dy);
            const int x1 = static_cast<int>(row1->x + 14 + dx);
            const int y1 = static_cast<int>(row1->y + 10 + dy);
            if (!channel_at(frame_surface_, x0, y0, r0, g0, b0) || !channel_at(frame_surface_, x1, y1, r1, g1, b1)) {
                return false;
            }
            return r0 != r1 || g0 != g1 || b0 != b1;
        };
        if (!differ(0, 0) && !differ(8, 6) && !differ(20, 16) && !differ(4, 24)) {
            fail("scenario previews are missing or identical");
            return;
        }
        tap(SDL_GAMEPAD_BUTTON_EAST, [&] { return dialog_ == Dialog::Scenario; });
        if (dialog_ != Dialog::Welcome) {
            fail("B on the scenario picker did not return to the welcome screen");
            return;
        }
        ui_step_ = 22;
        return;
    }
    case 22: {
        const Hotspot *start = find_hot(hot_, kWelcomeNew, 0);
        if (start == nullptr || dialog_ != Dialog::Welcome) {
            fail("the welcome screen did not come back");
            return;
        }
        click_at(start->x + start->w * 0.5, start->y + start->h * 0.5);
        if (dialog_ != Dialog::NewCity) {
            fail("Start a New City did not open");
            return;
        }
        // B deletes the selected name and stays. A second B leaves the field.
        // A third B closes. An empty field must not drop the whole dialog.
        tap(SDL_GAMEPAD_BUTTON_EAST, [&] { return field_a_ == "New City"; });
        if (dialog_ != Dialog::NewCity || !field_a_.empty()) {
            fail("B on the new-city name should delete it and stay, field is '" + field_a_ + "'");
            return;
        }
        tap(SDL_GAMEPAD_BUTTON_EAST, [&] { return focus_id_ == kFieldName || (focus_id_ >= kOsk && focus_id_ < kOsk + 64); });
        if (dialog_ != Dialog::NewCity) {
            fail("B on an empty name closed New City");
            return;
        }
        if (focus_id_ == kFieldName || (focus_id_ >= kOsk && focus_id_ < kOsk + 64)) {
            fail("B on an empty name did not leave the field");
            return;
        }
        tap(SDL_GAMEPAD_BUTTON_EAST, [&] { return dialog_ == Dialog::NewCity; });
        if (dialog_ != Dialog::Welcome) {
            fail("B after leaving the name did not return to the welcome screen");
            return;
        }
        ui_step_ = 23;
        return;
    }
    case 23: {
        const Hotspot *load = find_hot(hot_, kWelcomeLoad, 0);
        if (load == nullptr) {
            fail("Load a Saved City is not on the welcome screen");
            return;
        }
        click_at(load->x + load->w * 0.5, load->y + load->h * 0.5);
        if (dialog_ != Dialog::Files && dialog_ != Dialog::FileWait) {
            fail("Load a Saved City did not open a file picker");
            return;
        }
        tap(SDL_GAMEPAD_BUTTON_EAST, [&] { return dialog_ == Dialog::Files || dialog_ == Dialog::FileWait; });
        if (dialog_ != Dialog::Welcome) {
            fail("cancelling Load did not return to the welcome screen");
            return;
        }
        session_.new_city("New City");
        if (session_.needs_save_prompt()) {
            fail("an untouched generated city still wants to be saved");
            return;
        }
        tap(SDL_GAMEPAD_BUTTON_EAST, [&] { return dialog_ == Dialog::Welcome; });
        if (dialog_ == Dialog::Unsaved) {
            fail("leaving the welcome screen asked to save an untouched city");
            return;
        }
        if (quitting_) {
            fail("B on the welcome screen quit without confirmation");
            return;
        }
        if (dialog_ != Dialog::QuitConfirm) {
            fail("B on the welcome screen did not ask to quit");
            return;
        }
        if (focus_id_ != kCancel) {
            fail("the quit confirmation did not focus Cancel");
            return;
        }
        tap(SDL_GAMEPAD_BUTTON_EAST, [&] { return dialog_ == Dialog::QuitConfirm; });
        if (quitting_ || dialog_ != Dialog::Welcome) {
            fail("B on the quit confirmation did not return to the welcome screen");
            return;
        }
        welcome_gate_ = false;
        close_dialog();
        session_.load_scenario(CitySession::scenario_def(0).id);
        speed_ = 0;
        session_.set_speed(0);
        ui_step_ = 24;
        return;
    }
    case 24: {
        if (dialog_ != Dialog::None) {
            fail("the scenario goal was covered by a dialog");
            return;
        }
        const std::string goal = session_.scenario_progress();
        if (goal.empty()) {
            fail("a running scenario has no goal text");
            return;
        }
        if (slate_ && !handheld_) {
            // The Slate desktop shows the goal in the info band above the map.
            int br = 0;
            int bg = 0;
            int bb = 0;
            if (band_h_ <= 0 || !band_goal_shown_ ||
                !channel_at(frame_surface_, win_w_ - 4, band_y_ + band_h_ / 2, br, bg, bb) || br > 60 || bg > 60 || bb > 70) {
                fail("the Slate info band does not show the scenario goal");
                return;
            }
            session_.new_city("New City");
            if (session_.needs_save_prompt()) {
                fail("an untouched generated city still wants to be saved");
                return;
            }
            ui_step_ = 27;
            return;
        }
        int pr = 0;
        int pg = 0;
        int pb = 0;
        int mr = 0;
        int mg = 0;
        int mb = 0;
        const int px = view_x_ + 16;
        const int py = view_y_ + 12;
        if (!channel_at(frame_surface_, px, py, pr, pg, pb) || !channel_at(frame_surface_, px, view_y_ + 2, mr, mg, mb)) {
            fail("could not sample the scenario goal");
            return;
        }
        const bool panel = pr > 230 && pg > 230 && pb > 150 && pb < 210;
        const bool map_same = std::abs(pr - mr) < 12 && std::abs(pg - mg) < 12 && std::abs(pb - mb) < 12;
        if (!panel || map_same) {
            fail("scenario goal text has no backing panel");
            return;
        }
        int dark = 0;
        for (int x = view_x_ + 20; x < view_x_ + 220 && x < win_w_; x += 2) {
            int r = 0;
            int g = 0;
            int b = 0;
            if (channel_at(frame_surface_, x, view_y_ + 22, r, g, b) && r < 80 && g < 80 && b < 80) {
                ++dark;
            }
        }
        if (dark < 2) {
            fail("scenario goal text is not drawn on the panel");
            return;
        }
        session_.new_city("New City");
        if (session_.needs_save_prompt()) {
            fail("an untouched generated city still wants to be saved");
            return;
        }
        ui_step_ = 27;
        return;
    }
    case 27: {
        // Every landmark has its own toolbar button (no dropdown). A click
        // picks the building itself.
        for (int row = 0; row < special_menu_count(); ++row) {
            int button = -1;
            for (int i = 0; i < kPaletteToolCount; ++i) {
                if (tool_fixed_special(tool_by_index(i)) == row) {
                    button = i;
                }
            }
            if (button < 0) {
                fail("a special building has no toolbar button");
                return;
            }
        }
        const int slot = tool_groups::groups()[5].first;
        const int cell = palette_.cell();
        double cx = 0;
        double cy = 0;
        palette_.cell_origin(slot, cx, cy);
        const double x = side_x_ + cx + cell / 2.0;
        const double y = pal_y_ + cy + cell / 2.0;
        click_at(x, y);
        if (tool_index_ != slot || placing_special() < 0 ||
            std::string(special_building(placing_special()).name) != "Zoo") {
            fail("the Zoo button did not pick the Zoo");
            return;
        }
        ui_step_ = 28;
        return;
    }
    case 28: {
        if (tool_hint_now(tool_index_).find('$') == std::string::npos) {
            fail("the status line does not name the special building cost");
            return;
        }
        // The shoulder buttons walk through the landmarks like any tool.
        const int zoo = tool_groups::groups()[5].first;
        select_tool(zoo - 1);
        cycle_tool(1);
        if (tool_index_ != zoo || placing_special() < 0) {
            fail("the shoulder button did not reach the Zoo");
            return;
        }
        cycle_tool(1);
        if (tool_index_ != zoo + 1 || placing_special() < 0 ||
            std::string(special_building(placing_special()).name) != "Amusement Park") {
            fail("the shoulder button did not step to the Amusement Park");
            return;
        }
        // The Mayor's House waits for 2,000 people: greyed, and the hint says so.
        const int mayor = tool_groups::groups()[5].first + 6;
        if (session_.population() < 2000 && ((locked_tool_mask() >> mayor) & 1u) == 0) {
            fail("the locked Mayor's House button is not greyed");
            return;
        }
        // One D-pad tap while A is held lays one road tile and spends $10.
        // A hold waits, then repeats. Y pauses and resumes the previous speed.
        select_tool(1);
        int road_x = -1;
        int road_y = -1;
        for (int y = 4; y < CitySession::kWorldH - 4 && road_x < 0; ++y) {
            for (int x = 4; x < CitySession::kWorldW - 10; ++x) {
                bool open = true;
                for (int i = 0; i < 8; ++i) {
                    if ((session_.map_value(x + i, y) & 0x03ff) != 0) {
                        open = false;
                        break;
                    }
                }
                if (open) {
                    road_x = x;
                    road_y = y;
                    break;
                }
            }
        }
        if (road_x < 0) {
            fail("no clear run of land for the D-pad road");
            return;
        }
        center_on_fraction((road_x + 0.5) / CitySession::kWorldW, (road_y + 0.5) / CitySession::kWorldH);
        render();
        const int road_tile = std::max(map_.tile_size(), 8);
        cursor_x_ = static_cast<float>(snapped_map_origin(view_x_, map_ox_, scroll_x_) + (road_x + 0.5) * road_tile);
        cursor_y_ = static_cast<float>(snapped_map_origin(view_y_, map_oy_, scroll_y_) + (road_y + 0.5) * road_tile);
        if (hit(cursor_x_, cursor_y_) != kMapId) {
            fail("the D-pad road cursor is not on the map");
            return;
        }
        {
            SDL_Event down{};
            down.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
            down.gbutton.button = SDL_GAMEPAD_BUTTON_SOUTH;
            down.gbutton.down = true;
            handle_event(down);
        }
        if (!pad_dragging_) {
            fail("A did not start the road drag");
            return;
        }
        auto changed_tiles = [&]() {
            int n = 0;
            for (int y = 0; y < CitySession::kWorldH; ++y) {
                for (int x = 0; x < CitySession::kWorldW; ++x) {
                    const int now = session_.map_value(x, y) & 0x03ff;
                    if (now != ui_map_before_[static_cast<std::size_t>(y * CitySession::kWorldW + x)]) {
                        ++n;
                    }
                }
            }
            return n;
        };
        ui_map_before_.assign(static_cast<std::size_t>(CitySession::kWorldW * CitySession::kWorldH), 0);
        for (int y = 0; y < CitySession::kWorldH; ++y) {
            for (int x = 0; x < CitySession::kWorldW; ++x) {
                ui_map_before_[static_cast<std::size_t>(y * CitySession::kWorldW + x)] = session_.map_value(x, y) & 0x03ff;
            }
        }
        const float cursor_before_x = cursor_x_;
        int tile_before_x = -1;
        int tile_before_y = -1;
        map_tile_at(map_local_x(cursor_x_), map_local_y(cursor_y_), map_.shake_x(), map_.shake_y(), map_.tile_size(),
                    CitySession::kWorldW, CitySession::kWorldH, tile_before_x, tile_before_y);
        const auto funds_before_tap = session_.funds();
        auto follow_drag = [&]() {
            if (!pad_dragging_) {
                return;
            }
            pointer_x_ = cursor_x_;
            pointer_y_ = cursor_y_;
            map_.motion(map_local_x(cursor_x_), map_local_y(cursor_y_), true);
        };
        advance_pad_drag(1, 0, 16);
        follow_drag();
        advance_pad_drag(0, 0, 40);
        follow_drag();
        const int tapped = changed_tiles();
        const auto spent = funds_before_tap - session_.funds();
        if (tapped != 1 || spent != 10) {
            int tile_after_x = -1;
            int tile_after_y = -1;
            map_tile_at(map_local_x(cursor_x_), map_local_y(cursor_y_), map_.shake_x(), map_.shake_y(), map_.tile_size(),
                        CitySession::kWorldW, CitySession::kWorldH, tile_after_x, tile_after_y);
            fail("one D-pad tap laid " + std::to_string(tapped) + " tiles and cost $" +
                 std::to_string(static_cast<long long>(spent)) + " from " + std::to_string(tile_before_x) + "," +
                 std::to_string(tile_before_y) + " px " + std::to_string(cursor_before_x) + "->" +
                 std::to_string(cursor_x_) + " to " + std::to_string(tile_after_x) + "," +
                 std::to_string(tile_after_y) + " want " + std::to_string(road_x) + " tool " +
                 std::to_string(tool_index_) + " msg " + session_.message());
            return;
        }
        if (session_.message() == "Cannot build here.") {
            fail("a successful road drag reported Cannot build here");
            return;
        }
        for (int y = 0; y < CitySession::kWorldH; ++y) {
            for (int x = 0; x < CitySession::kWorldW; ++x) {
                ui_map_before_[static_cast<std::size_t>(y * CitySession::kWorldW + x)] = session_.map_value(x, y) & 0x03ff;
            }
        }
        advance_pad_drag(1, 0, 16);
        follow_drag();
        const int hold_first = changed_tiles();
        advance_pad_drag(1, 0, 250);
        follow_drag();
        const int hold_waiting = changed_tiles();
        advance_pad_drag(1, 0, 80);
        follow_drag();
        const int hold_repeat = changed_tiles();
        advance_pad_drag(0, 0, 16);
        if (hold_first != 1 || hold_waiting != 1 || hold_repeat != 2) {
            fail("D-pad hold repeat was " + std::to_string(hold_first) + "/" + std::to_string(hold_waiting) + "/" +
                 std::to_string(hold_repeat));
            return;
        }
        {
            SDL_Event up{};
            up.type = SDL_EVENT_GAMEPAD_BUTTON_UP;
            up.gbutton.button = SDL_GAMEPAD_BUTTON_SOUTH;
            up.gbutton.down = false;
            handle_event(up);
        }
        if (pad_dragging_) {
            fail("the road drag did not release");
            return;
        }
        const int speed_was = 2;
        set_speed(speed_was);
        {
            SDL_Event down{};
            down.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
            down.gbutton.button = SDL_GAMEPAD_BUTTON_NORTH;
            down.gbutton.down = true;
            handle_event(down);
            SDL_Event up{};
            up.type = SDL_EVENT_GAMEPAD_BUTTON_UP;
            up.gbutton.button = SDL_GAMEPAD_BUTTON_NORTH;
            up.gbutton.down = false;
            handle_event(up);
        }
        if (speed_ != 0) {
            fail("Y did not pause");
            return;
        }
        {
            SDL_Event down{};
            down.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
            down.gbutton.button = SDL_GAMEPAD_BUTTON_NORTH;
            down.gbutton.down = true;
            handle_event(down);
            SDL_Event up{};
            up.type = SDL_EVENT_GAMEPAD_BUTTON_UP;
            up.gbutton.button = SDL_GAMEPAD_BUTTON_NORTH;
            up.gbutton.down = false;
            handle_event(up);
        }
        if (speed_ != speed_was) {
            fail("Y did not resume the previous speed");
            return;
        }
        set_speed(3);
        {
            SDL_Event down{};
            down.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
            down.gbutton.button = SDL_GAMEPAD_BUTTON_NORTH;
            down.gbutton.down = true;
            handle_event(down);
        }
        service_gamepad(16);
        service_gamepad(520);
        if (speed_ != 1) {
            fail("holding Y from Fast went to speed " + std::to_string(speed_) + " instead of Slow");
            return;
        }
        {
            SDL_Event up{};
            up.type = SDL_EVENT_GAMEPAD_BUTTON_UP;
            up.gbutton.button = SDL_GAMEPAD_BUTTON_NORTH;
            up.gbutton.down = false;
            handle_event(up);
        }
        if (speed_ != 1) {
            fail("releasing a held Y changed the speed");
            return;
        }
        set_speed(0);
        ui_step_ = 40;
        return;
    }
    case 40: {
        // Minimap: a held left drag keeps recentring, clamps past the edge,
        // and lets go on release or when a dialog opens.
        {
            const Hotspot *mini = find_hot(hot_, kMinimapId, 0);
            if (mini == nullptr || dialog_ != Dialog::None || menu_.open()) {
                fail("no minimap to drag");
                return;
            }
            int ww = 1;
            int wh = 1;
            int pw = 1;
            int ph = 1;
            SDL_GetWindowSize(window_, &ww, &wh);
            SDL_GetWindowSizeInPixels(window_, &pw, &ph);
            auto mouse = [&](Uint32 type, double x, double y, bool held) {
                SDL_Event event{};
                event.type = type;
                const float wx = static_cast<float>(x * ww / std::max(pw, 1));
                const float wy = static_cast<float>(y * wh / std::max(ph, 1));
                if (type == SDL_EVENT_MOUSE_MOTION) {
                    event.motion.x = wx;
                    event.motion.y = wy;
                    event.motion.state = held ? SDL_BUTTON_LMASK : 0;
                } else {
                    event.button.button = SDL_BUTTON_LEFT;
                    event.button.down = type == SDL_EVENT_MOUSE_BUTTON_DOWN;
                    event.button.clicks = 1;
                    event.button.x = wx;
                    event.button.y = wy;
                }
                handle_event(event);
            };
            const double cx = mini->x + mini->w * 0.5;
            const double cy = mini->y + mini->h * 0.5;
            const int tool_before = tool_index_;
            mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, cx, cy, true);
            const double centre_x = scroll_x_;
            const double centre_y = scroll_y_;
            if (!mini_drag_) {
                fail("pressing the minimap did not start a drag");
                return;
            }
            // Right of centre, then past the right edge (clamps to the map's
            // east edge, further than any inside point), then further still.
            mouse(SDL_EVENT_MOUSE_MOTION, mini->x + mini->w * 0.6, cy, true);
            const double inside_x = scroll_x_;
            mouse(SDL_EVENT_MOUSE_MOTION, mini->x + mini->w + 300, cy, true);
            const double edge_x = scroll_x_;
            mouse(SDL_EVENT_MOUSE_MOTION, mini->x + mini->w + 900, cy, true);
            if (!(inside_x > centre_x) || !(edge_x > inside_x) || scroll_x_ != edge_x || scroll_y_ != centre_y) {
                fail("minimap drag scroll " + std::to_string(centre_x) + " -> " + std::to_string(inside_x) + " -> " +
                     std::to_string(edge_x) + " -> " + std::to_string(scroll_x_));
                return;
            }
            mouse(SDL_EVENT_MOUSE_MOTION, cx, mini->y + mini->h + 400, true);
            const double below_y = scroll_y_;
            if (!(below_y >= centre_y) || pointer_on_map_ || tool_index_ != tool_before) {
                fail("dragging off the bottom of the minimap moved the map or drew");
                return;
            }
            mouse(SDL_EVENT_MOUSE_BUTTON_UP, cx, mini->y + mini->h + 400, false);
            if (mini_drag_) {
                fail("releasing the button did not end the minimap drag");
                return;
            }
            const double after_x = scroll_x_;
            mouse(SDL_EVENT_MOUSE_MOTION, cx, cy, false);
            if (scroll_x_ != after_x) {
                fail("the minimap kept following after the button came up");
                return;
            }
            mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, cx, cy, true);
            open_dialog(Dialog::About);
            mouse(SDL_EVENT_MOUSE_MOTION, mini->x + 2, cy, true);
            if (mini_drag_ || scroll_x_ != centre_x) {
                fail("a dialog opening did not end the minimap drag");
                return;
            }
            cancel_dialog();
            mouse(SDL_EVENT_MOUSE_BUTTON_UP, cx, cy, false);
        }
        // Escape's last step (after rebind, dialog, menu, overlay, sticky
        // notice, keyboard cursor) drops the tool to Inspect, like a
        // right-click; gamepad Back does the same. B alone does not.
        {
            const int inspect = kToolCount - 1;
            const int build_tool = 1;
            if (dialog_ != Dialog::None || menu_.open()) {
                fail("Escape check started under a dialog or menu");
                return;
            }
            if (notice_sticky_) {
                dismiss_background_notice();
            }
            const bool overlay_was = overlay_on_;
            overlay_on_ = false;
            select_tool(build_tool);
            key_cursor_ = true;
            key_down(SDLK_ESCAPE);
            if (key_cursor_ || tool_index_ != build_tool) {
                fail("Escape should hide the keyboard cursor first and keep the tool");
                return;
            }
            key_down(SDLK_ESCAPE);
            if (tool_index_ != inspect) {
                fail("Escape did not go back to Inspect (tool " + std::to_string(tool_index_) + ")");
                return;
            }
            select_tool(build_tool);
            menu_.open_first();
            key_down(SDLK_ESCAPE);
            if (menu_.open() || tool_index_ != build_tool) {
                fail("Escape with a menu open should close the menu and keep the tool");
                return;
            }
            // A mouse stroke in progress ends with the tool.
            const Hotspot *map_spot = find_hot(hot_, kMapId, 0);
            if (map_spot == nullptr) {
                fail("no map hotspot for the Escape stroke check");
                return;
            }
            {
                int ww = 1;
                int wh = 1;
                int pw = 1;
                int ph = 1;
                SDL_GetWindowSize(window_, &ww, &wh);
                SDL_GetWindowSizeInPixels(window_, &pw, &ph);
                SDL_Event press{};
                press.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
                press.button.button = SDL_BUTTON_LEFT;
                press.button.down = true;
                press.button.clicks = 1;
                press.button.x = static_cast<float>((map_spot->x + map_spot->w * 0.3) * ww / std::max(pw, 1));
                press.button.y = static_cast<float>((map_spot->y + map_spot->h * 0.6) * wh / std::max(ph, 1));
                handle_event(press);
                if (!pointer_on_map_) {
                    fail("the map press for the Escape check did not start a stroke");
                    return;
                }
                key_down(SDLK_ESCAPE);
                if (pointer_on_map_ || map_.dragging() || tool_index_ != inspect) {
                    fail("Escape did not end the map stroke and go to Inspect");
                    return;
                }
                press.type = SDL_EVENT_MOUSE_BUTTON_UP;
                press.button.down = false;
                handle_event(press);
            }
            select_tool(build_tool);
            {
                SDL_Event b{};
                b.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
                b.gbutton.button = SDL_GAMEPAD_BUTTON_EAST;
                b.gbutton.down = true;
                handle_event(b);
                if (tool_index_ != build_tool) {
                    fail("B on the map changed the tool");
                    return;
                }
                SDL_Event back{};
                back.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
                back.gbutton.button = SDL_GAMEPAD_BUTTON_BACK;
                back.gbutton.down = true;
                handle_event(back);
                if (tool_index_ != inspect) {
                    fail("gamepad Back did not go back to Inspect");
                    return;
                }
            }
            gamepad_nav_ = false;
            overlay_on_ = overlay_was;
        }
        // Cloud shadows: their own clock, scaled by the speed (Slow 0.75,
        // Medium 1, Fast 2); the shared ambient clock is not.
        {
            const int effects_were = map_.effects();
            map_.set_effects(look_fx::EffectsFull);
            double moved[4] = {0, 0, 0, 0};
            for (int speed = 1; speed <= 3; ++speed) {
                const double before = map_.cloud_ms();
                map_.advance_fx(400, true, speed);
                moved[speed] = map_.cloud_ms() - before;
            }
            const double before_paused = map_.cloud_ms();
            map_.advance_fx(400, false, 3);
            moved[0] = map_.cloud_ms() - before_paused;
            map_.set_effects(effects_were);
            if (std::abs(moved[1] - 300.0) > 0.01 || std::abs(moved[2] - 400.0) > 0.01 ||
                std::abs(moved[3] - 800.0) > 0.01 || moved[0] != 0.0) {
                fail("cloud clock moved " + std::to_string(moved[1]) + "/" + std::to_string(moved[2]) + "/" +
                     std::to_string(moved[3]) + " (paused " + std::to_string(moved[0]) + ") for 400 ms");
                return;
            }
        }
        // On to the terrain editor, from the second New City page.
        open_dialog(Dialog::NewCity);
        field_a_ = "Painted";
        field_b_ = "4242";
        new_page_ = 1;
        ui_step_ = 41;
        return;
    }
    case 41: {
        // Edit Terrain opens the editor on the land the page previews. The
        // live city stays paused and untouched underneath.
        const Hotspot *edit = find_hot(hot_, kEditTerrain, 0);
        if (edit == nullptr || dialog_ != Dialog::NewCity) {
            fail("the New City land page has no Edit Terrain button");
            return;
        }
        ui_city_before_ = session_.terrain_tiles();
        click_at(edit->x + edit->w * 0.5, edit->y + edit->h * 0.5);
        if (!terrain_edit_ || dialog_ != Dialog::None || terrain_session_ == nullptr || !dialog_pauses()) {
            fail("Edit Terrain did not open the terrain editor");
            return;
        }
        std::vector<unsigned short> generated;
        if (!session_.preview_terrain(4242, new_island_, new_rivers_, new_lakes_, new_trees_, generated) ||
            generated != terrain_work_) {
            fail("the editor did not start from the previewed land");
            return;
        }
        ui_step_ = 42;
        return;
    }
    case 42: {
        // The toolbar holds only terrain and the brush; the top bar keeps the
        // city name with Cancel and Map Complete.
        for (int i = 0; i < terrain_paint::kPieceCount; ++i) {
            if (find_hot(hot_, kTerrainPiece + i, 0) == nullptr) {
                fail("a terrain piece is missing from the toolbar");
                return;
            }
        }
        if (find_hot(hot_, kPaletteId, 0) != nullptr || find_hot(hot_, kMenuTitle, 0) != nullptr ||
            find_hot(hot_, kTerrainBrush + 1, 0) == nullptr || find_hot(hot_, kTerrainComplete, 0) == nullptr ||
            find_hot(hot_, kTerrainCancel, 0) == nullptr) {
            fail("the editor toolbar or top bar is wrong");
            return;
        }
        const Hotspot *water = find_hot(hot_, kTerrainPiece + 1, 0);
        click_at(water->x + water->w * 0.5, water->y + water->h * 0.5);
        const Hotspot *big = find_hot(hot_, kTerrainBrush + 1, 0);
        click_at(big->x + big->w * 0.5, big->y + big->h * 0.5);
        if (terrain_piece_ != 1 || terrain_brush_ != terrain_paint::kBrushLarge) {
            fail("the toolbar did not pick Water with the 3x3 brush");
            return;
        }
        // Fill the middle of the view with land first, then paint water on it
        // with one click so the 3x3 square is certain to change.
        key_down(SDLK_1);
        const Hotspot *map_spot = find_hot(hot_, kMapId, 0);
        if (map_spot == nullptr || terrain_piece_ != 0) {
            fail("no map to paint, or 1 did not pick Ground");
            return;
        }
        const double px = map_spot->x + map_spot->w * 0.5;
        const double py = map_spot->y + map_spot->h * 0.5;
        click_at(px, py);
        key_down(SDLK_2);
        const std::vector<unsigned short> before = terrain_work_;
        click_at(px, py);
        int wet = 0;
        for (std::size_t i = 0; i < before.size(); ++i) {
            if (!terrain_paint::is_water(before[i]) && terrain_paint::is_water(terrain_work_[i])) {
                ++wet;
            }
        }
        if (wet != 9 || terrain_changed_ == 0 || session_.terrain_tiles() != ui_city_before_) {
            fail("a 3x3 water click turned " + std::to_string(wet) + " tiles wet (or touched the city)");
            return;
        }
        // Ctrl+Z takes the water click back; Ctrl+Y lays it again.
        {
            const std::vector<unsigned short> wet_map = terrain_work_;
            auto chord = [&](SDL_Keycode key) {
                SDL_Event event{};
                event.type = SDL_EVENT_KEY_DOWN;
                event.key.key = key;
                event.key.down = true;
                event.key.mod = SDL_KMOD_LCTRL;
                handle_event(event);
            };
            chord(SDLK_Z);
            if (terrain_work_ != before || terrain_session_->terrain_tiles() != before || !terrain_edit_) {
                fail("Ctrl+Z did not undo the last brush stroke");
                return;
            }
            chord(SDLK_Y);
            if (terrain_work_ != wet_map || terrain_session_->terrain_tiles() != wet_map || terrain_changed_ == 0) {
                fail("Ctrl+Y did not redo the brush stroke");
                return;
            }
        }
        key_down(SDLK_B);
        if (terrain_brush_ != terrain_paint::kBrushSmall) {
            fail("B did not switch the brush back to 1x1");
            return;
        }
        // Gamepad: the shoulders walk the pieces and Y flips the brush; X
        // (Inspect elsewhere) does nothing here.
        {
            auto pad = [&](SDL_GamepadButton button) {
                SDL_Event e{};
                e.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
                e.gbutton.button = static_cast<Uint8>(button);
                e.gbutton.down = true;
                handle_event(e);
                e.type = SDL_EVENT_GAMEPAD_BUTTON_UP;
                e.gbutton.down = false;
                handle_event(e);
            };
            const int piece_was = terrain_piece_;
            pad(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
            const int piece_next = terrain_piece_;
            pad(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
            pad(SDL_GAMEPAD_BUTTON_NORTH);
            const int brush_pad = terrain_brush_;
            pad(SDL_GAMEPAD_BUTTON_NORTH);
            pad(SDL_GAMEPAD_BUTTON_WEST);
            if (piece_next != (piece_was + 1) % terrain_paint::kPieceCount || terrain_piece_ != piece_was ||
                brush_pad != terrain_paint::kBrushLarge || terrain_brush_ != terrain_paint::kBrushSmall ||
                !terrain_edit_ || dialog_ != Dialog::None) {
                fail("the gamepad did not cycle terrain and flip the brush");
                return;
            }
            gamepad_nav_ = false;
        }
        // Map Complete on a map that is nearly all water asks first;
        // Escape there keeps editing with the map as it was.
        {
            const std::vector<unsigned short> kept = terrain_work_;
            terrain_work_.assign(kept.size(), terrain_paint::piece_tile(terrain_paint::Piece::Water));
            key_down(SDLK_RETURN);
            if (dialog_ != Dialog::TerrainLowLand || !terrain_edit_) {
                fail("Map Complete on an all-water map did not warn first");
                return;
            }
            key_down(SDLK_ESCAPE);
            if (dialog_ != Dialog::None || !terrain_edit_) {
                fail("Escape on the low-land warning did not keep editing");
                return;
            }
            terrain_work_ = kept;
        }
        // Escape after painting asks first; Escape there keeps editing.
        key_down(SDLK_ESCAPE);
        if (dialog_ != Dialog::TerrainDiscard || !terrain_edit_) {
            fail("Escape after painting did not ask before discarding");
            return;
        }
        key_down(SDLK_ESCAPE);
        if (dialog_ != Dialog::None || !terrain_edit_ || terrain_changed_ == 0) {
            fail("Escape on the discard question did not keep editing");
            return;
        }
        ui_terrain_painted_ = terrain_work_;
        ui_step_ = 43;
        return;
    }
    case 43: {
        // Map Complete keeps the edits and returns to the land page, which
        // previews them; Generate builds the city on them.
        const Hotspot *done = find_hot(hot_, kTerrainComplete, 0);
        if (done == nullptr) {
            fail("no Map Complete button");
            return;
        }
        click_at(done->x + done->w * 0.5, done->y + done->h * 0.5);
        if (terrain_edit_ || dialog_ != Dialog::NewCity || new_page_ != 1 || pending_terrain_ != ui_terrain_painted_ ||
            pending_terrain_for_page() == nullptr || session_.terrain_tiles() != ui_city_before_) {
            fail("Map Complete did not keep the edits and return to the land page");
            return;
        }
        // Typing another seed sets the edits aside; the old seed brings them back.
        field_b_ = "77";
        if (pending_terrain_for_page() != nullptr) {
            fail("a different seed still used the painted map");
            return;
        }
        field_b_ = "4242";
        ui_step_ = 44;
        return;
    }
    case 44: {
        const Hotspot *generate = find_hot(hot_, kGenerate, 0);
        if (generate == nullptr || find_hot(hot_, kEditTerrain, 0) == nullptr) {
            fail("the land page lost its buttons after Map Complete");
            return;
        }
        // The land page came back under the pointer; a person waits out the
        // click-through grace before pressing again.
        modal_grace_until_ = {};
        click_at(generate->x + generate->w * 0.5, generate->y + generate->h * 0.5);
        const std::vector<unsigned short> city = session_.terrain_tiles();
        int mismatched = 0;
        for (std::size_t i = 0; i < city.size() && i < ui_terrain_painted_.size(); ++i) {
            if (terrain_paint::is_water(city[i]) != terrain_paint::is_water(ui_terrain_painted_[i])) {
                ++mismatched;
            }
        }
        if (dialog_ == Dialog::NewCity || city.size() != ui_terrain_painted_.size() || mismatched != 0 ||
            session_.city_name() != "Painted") {
            fail("the new city was not built on the painted map (" + std::to_string(mismatched) + " tiles differ) name=" + session_.city_name() + " dlg=" + std::to_string(static_cast<int>(dialog_)) + " same=" + std::to_string(city == ui_city_before_) + " err=" + dialog_error_);
            return;
        }
        // The quit test still expects a clean city.
        session_.set_dirty(false);
        open_dialog(Dialog::About);
        ui_step_ = 45;
        return;
    }
    case 45: {
        // About offers View License, which opens NOTICE and COPYING.
        const Hotspot *view = find_hot(hot_, kViewLicense, 0);
        if (view == nullptr || dialog_ != Dialog::About || find_hot(hot_, kClose, 0) == nullptr) {
            fail("About has no View License button");
            return;
        }
        modal_grace_until_ = {};
        click_at(view->x + view->w * 0.5, view->y + view->h * 0.5);
        if (dialog_ != Dialog::License || license_text_.find("ADDITIONAL TERMS") == std::string::npos ||
            license_text_.find("GNU GENERAL PUBLIC LICENSE") == std::string::npos) {
            fail("View License did not open NOTICE and COPYING");
            return;
        }
        ui_step_ = 46;
        return;
    }
    case 46: {
        // Drawn once, so the viewer knows how much it can scroll.
        if (license_scroll_max_ <= 0 || license_scroll_ != 0 || license_view_h_ <= 0) {
            fail("the License viewer has nothing to scroll");
            return;
        }
        key_down(SDLK_DOWN);
        const int by_key = license_scroll_;
        key_down(SDLK_PAGEDOWN);
        const int by_page = license_scroll_;
        key_down(SDLK_END);
        const int at_end = license_scroll_;
        key_down(SDLK_HOME);
        if (by_key <= 0 || by_page <= by_key || at_end != license_scroll_max_ || license_scroll_ != 0) {
            fail("the keyboard does not scroll the License viewer");
            return;
        }
        SDL_Event wheel{};
        wheel.type = SDL_EVENT_MOUSE_WHEEL;
        wheel.wheel.y = -1;
        handle_event(wheel);
        if (license_scroll_ <= 0) {
            fail("the wheel does not scroll the License viewer");
            return;
        }
        license_scroll_ = 0;
        tap(SDL_GAMEPAD_BUTTON_DPAD_DOWN, [&] { return license_scroll_ == 0; });
        if (license_scroll_ <= 0 || dialog_ != Dialog::License) {
            fail("the D-pad does not scroll the License viewer");
            return;
        }
        nav_repeat_button_ = SDL_GAMEPAD_BUTTON_INVALID;
        pad_feel::repeat_clear(nav_repeat_);
        license_scroll_ = 0;
        {
            // A finger dragged up the text moves it up.
            const float fx = static_cast<float>((license_view_x_ + license_view_w_ * 0.5) / std::max(1, win_w_));
            const float top = static_cast<float>((license_view_y_ + license_view_h_ * 0.75) / std::max(1, win_h_));
            const float lift = static_cast<float>((license_view_y_ + license_view_h_ * 0.25) / std::max(1, win_h_));
            SDL_Event finger{};
            finger.tfinger.fingerID = 7;
            finger.tfinger.x = fx;
            finger.tfinger.y = top;
            finger.type = SDL_EVENT_FINGER_DOWN;
            handle_event(finger);
            finger.type = SDL_EVENT_FINGER_MOTION;
            finger.tfinger.y = lift;
            handle_event(finger);
            finger.type = SDL_EVENT_FINGER_UP;
            handle_event(finger);
        }
        if (license_scroll_ < license_view_h_ / 4 || dialog_ != Dialog::License) {
            fail("a finger drag does not scroll the License viewer");
            return;
        }
        key_down(SDLK_ESCAPE);
        if (dialog_ != Dialog::About || focus_id_ != kViewLicense) {
            fail("Escape in the License viewer did not go back to About");
            return;
        }
        close_dialog();
        welcome_gate_ = true;
        open_dialog(Dialog::Welcome);
        ui_step_ = 25;
        return;
    }
    case 25: {
        const Hotspot *quit = find_hot(hot_, kWelcomeQuit, 0);
        if (quit == nullptr || dialog_ != Dialog::Welcome) {
            fail("Quit is not on the welcome screen");
            return;
        }
        click_at(quit->x + quit->w * 0.5, quit->y + quit->h * 0.5);
        if (dialog_ != Dialog::QuitConfirm) {
            fail("Quit did not ask for confirmation");
            return;
        }
        ui_step_ = 26;
        return;
    }
    case 26: {
        const Hotspot *confirm = find_hot(hot_, kOk, 0);
        if (confirm == nullptr) {
            fail("the quit confirmation has no Quit button");
            return;
        }
        click_at(confirm->x + confirm->w * 0.5, confirm->y + confirm->h * 0.5);
        if (dialog_ == Dialog::Unsaved) {
            fail("confirming quit asked to save an untouched city");
            return;
        }
        if (!quitting_) {
            fail("confirming quit did not leave the game");
            return;
        }
        return;
    }
    default:
        quitting_ = true;
        return;
    }
}
