// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

#pragma once

#include <SDL3/SDL.h>

#include <cstring>
#include <string>
#include <vector>

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
    const char *l2 = "LT";
    const char *r2 = "RT";
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
    glyphs.l2 = xbox ? "LT" : "L2";
    glyphs.r2 = xbox ? "RT" : "R2";
    if (playstation) {
        glyphs.menu = "Options";
        glyphs.view = gamepad_type == SDL_GAMEPAD_TYPE_PS5 ? "Create" : "Share";
    } else if (deck) {
        glyphs.menu = "\u2630";
        glyphs.view = "View";
    }
    return glyphs;
}

// The pad legend under an open window reads its button words from the
// window's header hint, so the two never disagree (Deck review tidy b):
// "A or B closes" gives A "Close" and B "Close"; "A confirms" gives A
// "Confirm". Empty when the header does not name that button.
inline std::string legend_imperative(const std::string &phrase)
{
    std::string out = phrase;
    while (!out.empty() && (out.back() == '.' || out.back() == ' ')) {
        out.pop_back();
    }
    const std::size_t space = out.find(' ');
    std::string first = out.substr(0, space);
    const std::string rest = space == std::string::npos ? std::string() : out.substr(space);
    if (first == "goes") {
        first = "go";
    } else if (first.size() > 1 && first.back() == 's') {
        first.pop_back();
    }
    if (!first.empty() && first[0] >= 'a' && first[0] <= 'z') {
        first[0] = static_cast<char>(first[0] - 'a' + 'A');
    }
    return first + rest;
}

inline std::string legend_words_from_hint(const std::string &hint, const std::string &glyph)
{
    if (glyph.empty()) {
        return {};
    }
    std::size_t start = 0;
    while (start < hint.size()) {
        std::size_t end = hint.find(". ", start);
        const std::string piece = hint.substr(start, end == std::string::npos ? std::string::npos : end - start);
        start = end == std::string::npos ? hint.size() : end + 2;
        std::vector<std::string> words;
        std::size_t at = 0;
        while (at < piece.size()) {
            const std::size_t next = piece.find(' ', at);
            words.push_back(piece.substr(at, next == std::string::npos ? std::string::npos : next - at));
            at = next == std::string::npos ? piece.size() : next + 1;
        }
        std::size_t verb = 0;
        if (words.size() >= 4 && words[1] == "or" && (words[0] == glyph || words[2] == glyph)) {
            verb = 3;
        } else if (words.size() >= 2 && words[0] == glyph && words[1] != "or") {
            verb = 1;
        }
        if (verb == 0) {
            continue;
        }
        std::string phrase;
        for (std::size_t i = verb; i < words.size(); ++i) {
            phrase += (i == verb ? "" : " ") + words[i];
        }
        return legend_imperative(phrase);
    }
    return {};
}
