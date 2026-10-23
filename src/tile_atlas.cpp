// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#include "tile_atlas.hpp"

#include "assets.hpp"

#include <cairo.h>

#include <cstdint>
#include <iostream>
#include <vector>

namespace {

std::uint32_t pack(unsigned char r, unsigned char g, unsigned char b)
{
    return (255u << 24) | (static_cast<std::uint32_t>(r) << 16) |
           (static_cast<std::uint32_t>(g) << 8) | static_cast<std::uint32_t>(b);
}

} // namespace

TileAtlas &tile_atlas()
{
    static TileAtlas atlas;
    atlas.ensure_loaded();
    return atlas;
}

bool TileAtlas::ensure_loaded()
{
    if (tried_) {
        return count_ > 0;
    }
    tried_ = true;
    ++attempts_;

    const std::string path = asset_file("images/micropolisEngine/tiles.png");
    if (path.empty()) {
        error_ = asset_missing_text(asset_candidates());
        std::cerr << error_ << "\n";
        pixels_ = nullptr;
        return false;
    }

    cairo_surface_t *surface = cairo_image_surface_create_from_png(path.c_str());
    if (surface == nullptr || cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        error_ = "Lunduke City 2 could not read map tiles:\n" + path;
        std::cerr << error_ << "\n";
        if (surface != nullptr) {
            cairo_surface_destroy(surface);
        }
        pixels_ = nullptr;
        return false;
    }
    cairo_surface_flush(surface);
    if (cairo_image_surface_get_format(surface) != CAIRO_FORMAT_ARGB32 &&
        cairo_image_surface_get_format(surface) != CAIRO_FORMAT_RGB24) {
        error_ = "Lunduke City 2 map tiles are not 8-bit RGB:\n" + path;
        std::cerr << error_ << "\n";
        cairo_surface_destroy(surface);
        pixels_ = nullptr;
        return false;
    }

    const int width = cairo_image_surface_get_width(surface);
    const int height = cairo_image_surface_get_height(surface);
    columns_ = width / kSize;
    const int rows = height / kSize;
    if (columns_ < 1 || rows < 1) {
        error_ = "Lunduke City 2 map tiles are the wrong size:\n" + path;
        std::cerr << error_ << "\n";
        cairo_surface_destroy(surface);
        pixels_ = nullptr;
        return false;
    }
    count_ = columns_ * rows;

    static std::vector<std::uint32_t> stored;
    static std::vector<unsigned char> averages;
    stored.assign(static_cast<std::size_t>(count_) * kSize * kSize, 0);
    averages.assign(static_cast<std::size_t>(count_) * 3, 0);

    const int stride = cairo_image_surface_get_stride(surface);
    const unsigned char *src = cairo_image_surface_get_data(surface);

    for (int tile = 0; tile < count_; ++tile) {
        const int col = tile % columns_;
        const int row = tile / columns_;
        unsigned long sum_r = 0;
        unsigned long sum_g = 0;
        unsigned long sum_b = 0;
        std::uint32_t *dst = stored.data() + static_cast<std::size_t>(tile) * kSize * kSize;
        for (int y = 0; y < kSize; ++y) {
            const unsigned char *line = src + (row * kSize + y) * stride + col * kSize * 4;
            for (int x = 0; x < kSize; ++x) {
                // Cairo ARGB32 is native-endian 0xAARRGGBB. On little endian
                // the bytes are B, G, R, A. Read the word and unpack.
                const unsigned char *px = line + x * 4;
                const std::uint32_t word = static_cast<std::uint32_t>(px[0]) |
                                           (static_cast<std::uint32_t>(px[1]) << 8) |
                                           (static_cast<std::uint32_t>(px[2]) << 16) |
                                           (static_cast<std::uint32_t>(px[3]) << 24);
                const unsigned char b = static_cast<unsigned char>(word & 0xffu);
                const unsigned char g = static_cast<unsigned char>((word >> 8) & 0xffu);
                const unsigned char r = static_cast<unsigned char>((word >> 16) & 0xffu);
                dst[y * kSize + x] = pack(r, g, b);
                sum_r += r;
                sum_g += g;
                sum_b += b;
            }
        }
        const unsigned long n = kSize * kSize;
        averages[static_cast<std::size_t>(tile) * 3] = static_cast<unsigned char>(sum_r / n);
        averages[static_cast<std::size_t>(tile) * 3 + 1] = static_cast<unsigned char>(sum_g / n);
        averages[static_cast<std::size_t>(tile) * 3 + 2] = static_cast<unsigned char>(sum_b / n);
    }

    static std::vector<std::uint32_t> overview;
    overview.assign(static_cast<std::size_t>(count_) * kOverview * kOverview, 0);
    for (int tile = 0; tile < count_; ++tile) {
        const std::uint32_t *tile_px = stored.data() + static_cast<std::size_t>(tile) * kSize * kSize;
        std::uint32_t *dst = overview.data() + static_cast<std::size_t>(tile) * kOverview * kOverview;
        for (int y = 0; y < kOverview; ++y) {
            for (int x = 0; x < kOverview; ++x) {
                const std::uint32_t p00 = tile_px[(y * 2) * kSize + x * 2];
                const std::uint32_t p10 = tile_px[(y * 2) * kSize + x * 2 + 1];
                const std::uint32_t p01 = tile_px[(y * 2 + 1) * kSize + x * 2];
                const std::uint32_t p11 = tile_px[(y * 2 + 1) * kSize + x * 2 + 1];
                auto mix = [&](int shift) {
                    const unsigned sum = ((p00 >> shift) & 255u) + ((p10 >> shift) & 255u) + ((p01 >> shift) & 255u) +
                                         ((p11 >> shift) & 255u);
                    return (sum + 2u) / 4u;
                };
                dst[y * kOverview + x] = (mix(24) << 24) | (mix(16) << 16) | (mix(8) << 8) | mix(0);
            }
        }
    }

    cairo_surface_destroy(surface);
    pixels_ = stored.data();
    overview_ = overview.data();
    average_ = averages.data();
    return true;
}

void TileAtlas::average_color(int tile, double &r, double &g, double &b) const
{
    if (average_ == nullptr || tile < 0 || tile >= count_) {
        r = 0.80;
        g = 0.50;
        b = 0.40;
        return;
    }
    const unsigned char *p = average_ + static_cast<std::size_t>(tile) * 3;
    r = p[0] / 255.0;
    g = p[1] / 255.0;
    b = p[2] / 255.0;
}

void TileAtlas::blit(int tile, unsigned char *dest, int stride) const
{
    if (dest == nullptr || pixels_ == nullptr || reinterpret_cast<std::uintptr_t>(pixels_) < 16) {
        return;
    }
    if (tile < 0 || tile >= count_) {
        tile = 0;
    }
    const std::uint32_t *src = pixels_ + static_cast<std::size_t>(tile) * kSize * kSize;
    for (int y = 0; y < kSize; ++y) {
        auto *row = reinterpret_cast<std::uint32_t *>(dest + y * stride);
        for (int x = 0; x < kSize; ++x) {
            row[x] = src[y * kSize + x];
        }
    }
}

void TileAtlas::blit_overview(int tile, unsigned char *dest, int stride) const
{
    if (dest == nullptr || overview_ == nullptr) {
        return;
    }
    if (tile < 0 || tile >= count_) {
        tile = 0;
    }
    const std::uint32_t *src = overview_ + static_cast<std::size_t>(tile) * kOverview * kOverview;
    for (int y = 0; y < kOverview; ++y) {
        auto *row = reinterpret_cast<std::uint32_t *>(dest + y * stride);
        for (int x = 0; x < kOverview; ++x) {
            row[x] = src[y * kOverview + x];
        }
    }
}
