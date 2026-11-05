// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#pragma once

#include <SDL3/SDL.h>

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
