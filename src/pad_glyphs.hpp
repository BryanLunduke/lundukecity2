// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

#pragma once

#include <SDL3/SDL.h>

#include <cstring>

// Face names follow the pad SDL reports. A Deck session used to ignore that
// and keep Xbox letters even after a PlayStation pad was plugged in.

struct FaceGlyphs {
    const char *south = "A";
    const char *east = "B";
    const char *west = "X";
    const char *north = "Y";
    const char *left = "LB";
    const char *right = "RB";
    const char *menu = "Start";
    const char *view = "Back";
    const char *l3 = "LS";
    const char *r3 = "RS";
};

inline bool pad_is_playstation(int gamepad_type, int south_label)
{
    if (gamepad_type == SDL_GAMEPAD_TYPE_PS3 || gamepad_type == SDL_GAMEPAD_TYPE_PS4 ||
        gamepad_type == SDL_GAMEPAD_TYPE_PS5) {
        return true;
    }
    return south_label == SDL_GAMEPAD_BUTTON_LABEL_CROSS;
}

// A Deck session labels the Deck's own buttons (L1, R1, the menu button)
// only for the built-in controller. A plugged-in Xbox pad keeps LB, RB and
// Start. In game mode Steam shows every pad as a virtual one (vendor 28de),
// so a virtual Xbox-type pad beside another pad is the external one; alone,
// it is the Deck. `others` counts the other open pads.
inline bool pad_uses_deck_names(bool deck_session, bool have_pad, unsigned vendor, unsigned product, int gamepad_type,
                                const char *name, int others)
{
    if (!deck_session) {
        return false;
    }
    if (!have_pad) {
        return true;
    }
    if ((vendor == 0x28de && product == 0x1205) || (name != nullptr && std::strstr(name, "Deck") != nullptr)) {
        return true;
    }
    if (vendor != 0x28de) {
        return false;
    }
    const bool xbox_type = gamepad_type == SDL_GAMEPAD_TYPE_XBOX360 || gamepad_type == SDL_GAMEPAD_TYPE_XBOXONE;
    return !(xbox_type && others > 0);
}

inline FaceGlyphs face_glyphs(bool playstation, bool deck, int gamepad_type)
{
    FaceGlyphs glyphs;
    const bool xbox = !playstation && !deck;
    if (playstation) {
        glyphs.south = "\u2715";
        glyphs.east = "\u25CB";
        glyphs.west = "\u25A1";
        glyphs.north = "\u25B3";
    }
    glyphs.left = xbox ? "LB" : "L1";
    glyphs.right = xbox ? "RB" : "R1";
    glyphs.l3 = xbox ? "LS" : "L3";
    glyphs.r3 = xbox ? "RS" : "R3";
    if (playstation) {
        glyphs.menu = "Options";
        glyphs.view = gamepad_type == SDL_GAMEPAD_TYPE_PS5 ? "Create" : "Share";
    } else if (deck) {
        glyphs.menu = "\u2630";
        glyphs.view = "View";
    }
    return glyphs;
}
