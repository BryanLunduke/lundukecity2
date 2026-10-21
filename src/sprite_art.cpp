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
};

constexpr int kTypes = 8;
constexpr int kFrames = 16;

Frame &slot(int type, int frame_index)
{
    static std::vector<Frame> frames(static_cast<std::size_t>(kTypes * kFrames));
    return frames[static_cast<std::size_t>((type - 1) * kFrames + frame_index)];
}

bool load_frame(int type, int frame_index)
{
    Frame &frame = slot(type, frame_index);
    if (frame.surface != nullptr) {
        return true;
    }
    const std::string relative = "images/micropolisEngine/obj" + std::to_string(type) + "-" +
                                 std::to_string(frame_index) + ".png";
    const std::string path = asset_file(relative);
    if (path.empty()) {
        return false;
    }
    cairo_surface_t *surface = cairo_image_surface_create_from_png(path.c_str());
    if (surface == nullptr || cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        std::cerr << "Lunduke City 2: could not read sprite " << relative << "\n";
        if (surface != nullptr) {
            cairo_surface_destroy(surface);
        }
        frame.surface = nullptr;
        return false;
    }
    frame.surface = surface;
    return true;
}

} // namespace

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
