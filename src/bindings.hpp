// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#pragma once

// Keyboard chords and gamepad buttons the player can rebind. Factory chords
// match the keys the menus and the tool column already used. A missing
// settings.txt line keeps that factory chord, so an older file still loads.

#include "menu_model.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

enum KeyAct : int {
    KeyPause = 0,
    KeyNewCity,
    KeyLoad,
    KeySave,
    KeySaveAs,
    KeyQuit,
    KeySlow,
    KeyMedium,
    KeyFast,
    KeyZoomIn,
    KeyZoomOut,
    KeyBudget,
    KeyGraphs,
    KeyEvaluation,
    KeyFullscreen,
    KeyTool0,
    KeyActCount = KeyTool0 + 17
};

enum PadAct : int {
    PadConfirm = 0,
    PadCancel,
    PadQuery,
    PadPause,
    PadBack,
    PadMenu,
    PadPrevTool,
    PadNextTool,
    PadOverlay,
    PadCenter,
    PadActCount
};

// SDL_GamepadButton values. Kept here so the binding file does not need SDL.
inline constexpr int kPadSouth = 0;
inline constexpr int kPadEast = 1;
inline constexpr int kPadWest = 2;
inline constexpr int kPadNorth = 3;
inline constexpr int kPadBack = 4;
inline constexpr int kPadStart = 6;
inline constexpr int kPadLeftStick = 7;
inline constexpr int kPadRightStick = 8;
inline constexpr int kPadLeftShoulder = 9;
inline constexpr int kPadRightShoulder = 10;

struct ControlBinds {
    std::string key[KeyActCount];
    int pad[PadActCount];
};

struct BindRow {
    const char *label;
    int key; // KeyAct, or -1
    int pad; // PadAct, or -1
};

inline const char *key_act_id(int act)
{
    switch (act) {
    case KeyPause:
        return "pause";
    case KeyNewCity:
        return "new";
    case KeyLoad:
        return "load";
    case KeySave:
        return "save";
    case KeySaveAs:
        return "save_as";
    case KeyQuit:
        return "quit";
    case KeySlow:
        return "slow";
    case KeyMedium:
        return "medium";
    case KeyFast:
        return "fast";
    case KeyZoomIn:
        return "zoom_in";
    case KeyZoomOut:
        return "zoom_out";
    case KeyBudget:
        return "budget";
    case KeyGraphs:
        return "graphs";
    case KeyEvaluation:
        return "evaluation";
    case KeyFullscreen:
        return "fullscreen";
    default:
        break;
    }
    if (act >= KeyTool0 && act < KeyActCount) {
        static const char *ids[] = {"bulldoze", "roads", "rail", "power", "park", "residential", "commercial",
                                    "industrial", "police", "fire", "stadium", "seaport", "coal", "nuclear",
                                    "airport", "special", "query"};
        return ids[act - KeyTool0];
    }
    return "";
}

inline int key_act_from_id(const std::string &id)
{
    for (int i = 0; i < KeyActCount; ++i) {
        if (id == key_act_id(i)) {
            return i;
        }
    }
    return -1;
}

inline const char *pad_act_id(int act)
{
    switch (act) {
    case PadConfirm:
        return "confirm";
    case PadCancel:
        return "cancel";
    case PadQuery:
        return "query";
    case PadPause:
        return "pause";
    case PadBack:
        return "back";
    case PadMenu:
        return "menu";
    case PadPrevTool:
        return "prev_tool";
    case PadNextTool:
        return "next_tool";
    case PadOverlay:
        return "overlay";
    case PadCenter:
        return "center";
    default:
        return "";
    }
}

inline int pad_act_from_id(const std::string &id)
{
    for (int i = 0; i < PadActCount; ++i) {
        if (id == pad_act_id(i)) {
            return i;
        }
    }
    return -1;
}

inline const char *key_act_label(int act)
{
    switch (act) {
    case KeyPause:
        return "Pause";
    case KeyNewCity:
        return "New city";
    case KeyLoad:
        return "Load city";
    case KeySave:
        return "Save city";
    case KeySaveAs:
        return "Save city as";
    case KeyQuit:
        return "Quit";
    case KeySlow:
        return "Slow";
    case KeyMedium:
        return "Medium";
    case KeyFast:
        return "Fast";
    case KeyZoomIn:
        return "Zoom in";
    case KeyZoomOut:
        return "Zoom out";
    case KeyBudget:
        return "Budget";
    case KeyGraphs:
        return "Graphs";
    case KeyEvaluation:
        return "Evaluation";
    case KeyFullscreen:
        return "Fullscreen";
    default:
        break;
    }
    if (act >= KeyTool0 && act < KeyActCount) {
        static const char *names[] = {"Bulldozer", "Roads", "Rail", "Power lines", "Park", "Residential",
                                      "Commercial", "Industrial", "Police", "Fire dept", "Stadium", "Seaport",
                                      "Coal power", "Nuclear", "Airport", "Special", "Query"};
        return names[act - KeyTool0];
    }
    return "";
}

inline const char *pad_button_token(int button)
{
    switch (button) {
    case kPadSouth:
        return "south";
    case kPadEast:
        return "east";
    case kPadWest:
        return "west";
    case kPadNorth:
        return "north";
    case kPadBack:
        return "back";
    case kPadStart:
        return "start";
    case kPadLeftStick:
        return "l3";
    case kPadRightStick:
        return "r3";
    case kPadLeftShoulder:
        return "lb";
    case kPadRightShoulder:
        return "rb";
    default:
        return "";
    }
}

inline int pad_button_from_token(const std::string &token)
{
    if (token == "south" || token == "a") {
        return kPadSouth;
    }
    if (token == "east" || token == "b") {
        return kPadEast;
    }
    if (token == "west" || token == "x") {
        return kPadWest;
    }
    if (token == "north" || token == "y") {
        return kPadNorth;
    }
    if (token == "back" || token == "view") {
        return kPadBack;
    }
    if (token == "start" || token == "menu") {
        return kPadStart;
    }
    if (token == "l3" || token == "leftstick") {
        return kPadLeftStick;
    }
    if (token == "r3" || token == "rightstick") {
        return kPadRightStick;
    }
    if (token == "lb" || token == "l1") {
        return kPadLeftShoulder;
    }
    if (token == "rb" || token == "r1") {
        return kPadRightShoulder;
    }
    return -1;
}

inline bool pad_button_rebindable(int button)
{
    return pad_button_token(button)[0] != '\0';
}

inline std::string factory_key(int act)
{
    switch (act) {
    case KeyPause:
        return "space";
    case KeyNewCity:
        return "ctrl+n";
    case KeyLoad:
        return "ctrl+o";
    case KeySave:
        return "ctrl+s";
    case KeySaveAs:
        return "ctrl+shift+s";
    case KeyQuit:
        return "ctrl+q";
    case KeySlow:
        return "ctrl+1";
    case KeyMedium:
        return "ctrl+2";
    case KeyFast:
        return "ctrl+3";
    case KeyZoomIn:
        return "ctrl+equal";
    case KeyZoomOut:
        return "ctrl+minus";
    case KeyBudget:
        return "ctrl+b";
    case KeyGraphs:
        return "ctrl+g";
    case KeyEvaluation:
        return "ctrl+e";
    case KeyFullscreen:
        return "f11";
    default:
        break;
    }
    if (act >= KeyTool0 && act < KeyActCount) {
        static const char letters[] = {'b', 'r', 't', 'w', 'g', 'h', 'c', 'i', 'o', 'f', 'd', 'p', 'e', 'u', 'a', 'v', 'q'};
        return std::string(1, letters[act - KeyTool0]);
    }
    return {};
}

inline int factory_pad(int act)
{
    switch (act) {
    case PadConfirm:
        return kPadSouth;
    case PadCancel:
        return kPadEast;
    case PadQuery:
        return kPadWest;
    case PadPause:
        return kPadNorth;
    case PadBack:
        return kPadBack;
    case PadMenu:
        return kPadStart;
    case PadPrevTool:
        return kPadLeftShoulder;
    case PadNextTool:
        return kPadRightShoulder;
    case PadOverlay:
        return kPadLeftStick;
    case PadCenter:
        return kPadRightStick;
    default:
        return -1;
    }
}

inline ControlBinds default_binds()
{
    ControlBinds binds;
    for (int i = 0; i < KeyActCount; ++i) {
        binds.key[i] = factory_key(i);
    }
    for (int i = 0; i < PadActCount; ++i) {
        binds.pad[i] = factory_pad(i);
    }
    return binds;
}

inline std::string lower_copy(const std::string &text)
{
    std::string out = text;
    for (char &ch : out) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return out;
}

// Modifiers in a stable order, then the key. "Shift+Ctrl+S" and "ctrl+s"
// with a shift flag become "ctrl+shift+s".
inline std::string normalize_chord(const std::string &token)
{
    const std::string raw = lower_copy(token);
    bool ctrl = false;
    bool shift = false;
    bool alt = false;
    std::string key;
    std::size_t i = 0;
    while (i < raw.size()) {
        const auto plus = raw.find('+', i);
        const std::string part = raw.substr(i, plus == std::string::npos ? std::string::npos : plus - i);
        if (part == "ctrl" || part == "control") {
            ctrl = true;
        } else if (part == "shift") {
            shift = true;
        } else if (part == "alt") {
            alt = true;
        } else if (!part.empty()) {
            key = part;
        }
        if (plus == std::string::npos) {
            break;
        }
        i = plus + 1;
    }
    if (key == "equals" || key == "=") {
        key = "equal";
    } else if (key == "-" || key == "hyphen") {
        key = "minus";
    } else if (key == "+") {
        key = "plus";
    }
    std::string out;
    if (ctrl) {
        out += "ctrl+";
    }
    if (shift) {
        out += "shift+";
    }
    if (alt) {
        out += "alt+";
    }
    out += key;
    return out;
}

inline std::string chord_label(const std::string &token)
{
    const std::string norm = normalize_chord(token);
    if (norm.empty()) {
        return "None";
    }
    std::string label;
    std::size_t i = 0;
    while (i < norm.size()) {
        const auto plus = norm.find('+', i);
        std::string part = norm.substr(i, plus == std::string::npos ? std::string::npos : plus - i);
        if (part == "ctrl") {
            part = "Ctrl";
        } else if (part == "shift") {
            part = "Shift";
        } else if (part == "alt") {
            part = "Alt";
        } else if (part == "space") {
            part = "Space";
        } else if (part == "equal") {
            part = "=";
        } else if (part == "minus") {
            part = "-";
        } else if (part == "plus") {
            part = "+";
        } else if (part == "f11") {
            part = "F11";
        } else if (part == "kp_add") {
            part = "Keypad +";
        } else if (part == "kp_sub") {
            part = "Keypad -";
        } else if (part == "return") {
            part = "Enter";
        } else if (part == "escape") {
            part = "Esc";
        } else if (part.size() == 1) {
            part[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(part[0])));
        } else if (!part.empty()) {
            part[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(part[0])));
        }
        if (!label.empty()) {
            label += "+";
        }
        label += part;
        if (plus == std::string::npos) {
            break;
        }
        i = plus + 1;
    }
    return label;
}

inline int key_act_for_token(const ControlBinds &binds, const std::string &token)
{
    const std::string norm = normalize_chord(token);
    if (norm.empty()) {
        return -1;
    }
    for (int i = 0; i < KeyActCount; ++i) {
        if (binds.key[i] == norm) {
            return i;
        }
    }
    if ((norm == "ctrl+plus" || norm == "ctrl+kp_add") && binds.key[KeyZoomIn] == factory_key(KeyZoomIn)) {
        return KeyZoomIn;
    }
    if (norm == "ctrl+kp_sub" && binds.key[KeyZoomOut] == factory_key(KeyZoomOut)) {
        return KeyZoomOut;
    }
    return -1;
}

inline bool token_is_factory(const std::string &token)
{
    const std::string norm = normalize_chord(token);
    for (int i = 0; i < KeyActCount; ++i) {
        if (factory_key(i) == norm) {
            return true;
        }
    }
    return norm == "ctrl+plus" || norm == "ctrl+kp_add" || norm == "ctrl+kp_sub";
}

// True when this chord used to do something and the player has moved that
// action elsewhere. The old shortcut must not also fire.
inline bool token_is_shadowed(const ControlBinds &binds, const std::string &token)
{
    if (key_act_for_token(binds, token) >= 0) {
        return false;
    }
    return token_is_factory(token);
}

// Returns false and sets conflict when another action already uses the chord.
// An empty token clears the binding. Rebinding an action to its own chord is fine.
// steal lets a settings line take a factory chord (Pause set to B clears the
// bulldozer key). Two explicit lines that share a chord keep the earlier one.
inline bool assign_key(ControlBinds &binds, int act, const std::string &token, int &conflict, bool steal = false)
{
    conflict = -1;
    if (act < 0 || act >= KeyActCount) {
        return false;
    }
    const std::string norm = normalize_chord(token);
    if (!norm.empty()) {
        for (int i = 0; i < KeyActCount; ++i) {
            if (i == act || binds.key[i] != norm) {
                continue;
            }
            if (steal && binds.key[i] == factory_key(i)) {
                binds.key[i].clear();
                continue;
            }
            conflict = i;
            return false;
        }
    }
    binds.key[act] = norm;
    return true;
}

inline bool assign_pad(ControlBinds &binds, int act, int button, int &conflict, bool steal = false)
{
    conflict = -1;
    if (act < 0 || act >= PadActCount) {
        return false;
    }
    if (button >= 0) {
        if (!pad_button_rebindable(button)) {
            return false;
        }
        for (int i = 0; i < PadActCount; ++i) {
            if (i == act || binds.pad[i] != button) {
                continue;
            }
            if (steal && binds.pad[i] == factory_pad(i)) {
                binds.pad[i] = -1;
                continue;
            }
            conflict = i;
            return false;
        }
    }
    binds.pad[act] = button;
    return true;
}

// The chord moves to this action. The action that had it receives this
// action's previous chord, which may be empty. That is how a full pad can
// be rearranged, and how a binding can be cleared by swapping onto nothing.
inline void swap_key(ControlBinds &binds, int act, const std::string &token)
{
    if (act < 0 || act >= KeyActCount) {
        return;
    }
    const std::string norm = normalize_chord(token);
    const std::string previous = binds.key[act];
    int other = -1;
    if (!norm.empty()) {
        for (int i = 0; i < KeyActCount; ++i) {
            if (i != act && binds.key[i] == norm) {
                other = i;
                break;
            }
        }
    }
    binds.key[act] = norm;
    if (other >= 0) {
        binds.key[other] = previous == norm ? std::string() : previous;
    }
}

inline void swap_pad(ControlBinds &binds, int act, int button)
{
    if (act < 0 || act >= PadActCount) {
        return;
    }
    const int previous = binds.pad[act];
    int other = -1;
    if (button >= 0) {
        for (int i = 0; i < PadActCount; ++i) {
            if (i != act && binds.pad[i] == button) {
                other = i;
                break;
            }
        }
    }
    binds.pad[act] = button;
    if (other >= 0) {
        binds.pad[other] = previous == button ? -1 : previous;
    }
}

inline int pad_act_for_button(const ControlBinds &binds, int button)
{
    if (button < 0) {
        return -1;
    }
    for (int i = 0; i < PadActCount; ++i) {
        if (binds.pad[i] == button) {
            return i;
        }
    }
    return -1;
}

inline std::string binds_override_text(const ControlBinds &binds)
{
    const ControlBinds factory = default_binds();
    std::string out;
    for (int i = 0; i < KeyActCount; ++i) {
        if (binds.key[i] == factory.key[i]) {
            continue;
        }
        out += "bind_key_";
        out += key_act_id(i);
        out += "=";
        out += binds.key[i];
        out += "\n";
    }
    for (int i = 0; i < PadActCount; ++i) {
        if (binds.pad[i] == factory.pad[i]) {
            continue;
        }
        out += "bind_pad_";
        out += pad_act_id(i);
        out += "=";
        out += binds.pad[i] < 0 ? "" : pad_button_token(binds.pad[i]);
        out += "\n";
    }
    return out;
}

// Unknown bind lines are ignored. A repeated chord keeps the earlier action
// and drops the later one, so a hand-edited file cannot leave two actions
// on the same key.
inline ControlBinds binds_from_text(const std::string &text)
{
    ControlBinds binds = default_binds();
    std::size_t i = 0;
    while (i < text.size()) {
        auto end = text.find('\n', i);
        if (end == std::string::npos) {
            end = text.size();
        }
        std::string line = text.substr(i, end - i);
        i = end + 1;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const auto eq = line.find('=');
        if (eq == std::string::npos || eq == 0) {
            continue;
        }
        const std::string key = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);
        if (key.compare(0, 9, "bind_key_") == 0) {
            const int act = key_act_from_id(key.substr(9));
            if (act < 0) {
                continue;
            }
            int conflict = -1;
            if (!assign_key(binds, act, value, conflict, true)) {
                continue;
            }
        } else if (key.compare(0, 9, "bind_pad_") == 0) {
            const int act = pad_act_from_id(key.substr(9));
            if (act < 0) {
                continue;
            }
            if (value.empty()) {
                binds.pad[act] = -1;
                continue;
            }
            const int button = pad_button_from_token(lower_copy(value));
            if (button < 0) {
                continue;
            }
            int conflict = -1;
            if (!assign_pad(binds, act, button, conflict, true)) {
                continue;
            }
        }
    }
    return binds;
}

inline int bind_row_count()
{
    return 40;
}

inline BindRow bind_row(int index)
{
    static const BindRow rows[] = {
        {"Pause", KeyPause, PadPause},
        {"Confirm", -1, PadConfirm},
        {"Cancel", -1, PadCancel},
        {"Back", -1, PadBack},
        {"Open the menu", -1, PadMenu},
        {"Previous tool", -1, PadPrevTool},
        {"Next tool", -1, PadNextTool},
        {"Map overlay", -1, PadOverlay},
        {"Center on cursor", -1, PadCenter},
        {"New city", KeyNewCity, -1},
        {"Load city", KeyLoad, -1},
        {"Save city", KeySave, -1},
        {"Save city as", KeySaveAs, -1},
        {"Quit", KeyQuit, -1},
        {"Slow", KeySlow, -1},
        {"Medium", KeyMedium, -1},
        {"Fast", KeyFast, -1},
        {"Zoom in", KeyZoomIn, -1},
        {"Zoom out", KeyZoomOut, -1},
        {"Budget", KeyBudget, -1},
        {"Graphs", KeyGraphs, -1},
        {"Evaluation", KeyEvaluation, -1},
        {"Fullscreen", KeyFullscreen, -1},
        {"Bulldozer", KeyTool0 + 0, -1},
        {"Roads", KeyTool0 + 1, -1},
        {"Rail", KeyTool0 + 2, -1},
        {"Power lines", KeyTool0 + 3, -1},
        {"Park", KeyTool0 + 4, -1},
        {"Residential", KeyTool0 + 5, -1},
        {"Commercial", KeyTool0 + 6, -1},
        {"Industrial", KeyTool0 + 7, -1},
        {"Police", KeyTool0 + 8, -1},
        {"Fire dept", KeyTool0 + 9, -1},
        {"Stadium", KeyTool0 + 10, -1},
        {"Seaport", KeyTool0 + 11, -1},
        {"Coal power", KeyTool0 + 12, -1},
        {"Nuclear", KeyTool0 + 13, -1},
        {"Airport", KeyTool0 + 14, -1},
        {"Special", KeyTool0 + 15, -1},
        {"Query", KeyTool0 + 16, PadQuery},
    };
    static_assert(sizeof(rows) / sizeof(rows[0]) == 40, "bind row count");
    if (index < 0 || index >= 40) {
        return BindRow{"", -1, -1};
    }
    return rows[index];
}

inline int key_act_for_menu(Action action)
{
    switch (action) {
    case Action::NewCity:
        return KeyNewCity;
    case Action::Load:
        return KeyLoad;
    case Action::Save:
        return KeySave;
    case Action::SaveAs:
        return KeySaveAs;
    case Action::Quit:
        return KeyQuit;
    case Action::Slow:
        return KeySlow;
    case Action::Medium:
        return KeyMedium;
    case Action::Fast:
        return KeyFast;
    case Action::ZoomIn:
        return KeyZoomIn;
    case Action::ZoomOut:
        return KeyZoomOut;
    case Action::Budget:
        return KeyBudget;
    case Action::Graphs:
        return KeyGraphs;
    case Action::Evaluation:
        return KeyEvaluation;
    case Action::Fullscreen:
        return KeyFullscreen;
    case Action::TogglePause:
        return KeyPause;
    default:
        return -1;
    }
}

inline int tool_index_for_act(int act)
{
    if (act < KeyTool0 || act >= KeyActCount) {
        return -1;
    }
    return act - KeyTool0;
}
