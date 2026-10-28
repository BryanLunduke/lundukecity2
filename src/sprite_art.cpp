// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#include "sprite_art.hpp"

#include "assets.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace {

struct Frame {
    cairo_surface_t *surface = nullptr;
    int look = -1;
};

constexpr int kTypes = 8;
constexpr int kFrames = 16;

int &active_look()
{
    static int look = 0;
    return look;
}

std::vector<Frame> &frames()
{
    static std::vector<Frame> stored(static_cast<std::size_t>(kTypes * kFrames));
    return stored;
}

Frame &slot(int type, int frame_index)
{
    return frames()[static_cast<std::size_t>((type - 1) * kFrames + frame_index)];
}

const char *look_suffix(int look)
{
    if (look == 1) {
        return "-svga";
    }
    if (look == 2) {
        return "-spooky";
    }
    return "";
}

cairo_surface_t *open_sprite(const std::string &relative)
{
    const std::string path = asset_file(relative);
    if (path.empty()) {
        return nullptr;
    }
    cairo_surface_t *surface = cairo_image_surface_create_from_png(path.c_str());
    if (surface == nullptr || cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        if (surface != nullptr) {
            cairo_surface_destroy(surface);
        }
        return nullptr;
    }
    return surface;
}

bool load_frame(int type, int frame_index)
{
    Frame &frame = slot(type, frame_index);
    const int look = active_look();
    if (frame.surface != nullptr && frame.look == look) {
        return true;
    }
    if (frame.surface != nullptr) {
        cairo_surface_destroy(frame.surface);
        frame.surface = nullptr;
        frame.look = -1;
    }
    const std::string stem = "images/micropolisEngine/obj" + std::to_string(type) + "-" +
                             std::to_string(frame_index);
    // A look sheet is optional. A missing file keeps the classic frame.
    cairo_surface_t *surface = nullptr;
    std::string loaded = stem + ".png";
    if (look == 1 || look == 2) {
        const std::string styled = stem + look_suffix(look) + ".png";
        surface = open_sprite(styled);
        if (surface != nullptr) {
            loaded = styled;
        }
    }
    if (surface == nullptr) {
        surface = open_sprite(stem + ".png");
        loaded = stem + ".png";
    }
    if (surface == nullptr) {
        std::cerr << "Lunduke City 2: could not read sprite " << loaded << "\n";
        return false;
    }
    frame.surface = surface;
    frame.look = look;
    return true;
}

} // namespace

void sprite_art_set_look(int look)
{
    if (look < 0 || look > 2) {
        look = 0;
    }
    active_look() = look;
}

void sprite_art_shutdown()
{
    for (Frame &frame : frames()) {
        if (frame.surface != nullptr) {
            cairo_surface_destroy(frame.surface);
            frame.surface = nullptr;
            frame.look = -1;
        }
    }
}

cairo_surface_t *sprite_frame(int type, int frame)
{
    if (type < 1 || type > kTypes || frame < 1) {
        return nullptr;
    }
    int index = frame - 1;
    if (index >= kFrames) {
        index = kFrames - 1;
    }
    if (!load_frame(type, index)) {
        for (int back = index - 1; back >= 0; --back) {
            if (load_frame(type, back) || slot(type, back).surface != nullptr) {
                return slot(type, back).surface;
            }
        }
        return nullptr;
    }
    return slot(type, index).surface;
}
