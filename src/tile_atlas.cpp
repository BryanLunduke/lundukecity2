// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#include "tile_atlas.hpp"

#include "assets.hpp"
#include "vga_palette.hpp"

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

struct Sheet {
    std::vector<std::uint32_t> pixels;
    std::vector<std::uint32_t> overview;
    std::vector<unsigned char> averages;
};

struct Sheets {
    Sheet classic;
    Sheet svga;
};

Sheets &sheets()
{
    static Sheets stored;
    return stored;
}

// 2×2 box filter. (sum + 2) / 4 matches the overview used since the crisp
// 8px zoom. snap is only for the SVGA sheet, whose averages must stay on
// the VGA palette. Classic passes false and is unchanged.
void build_overview(const std::uint32_t *pixels, int count, std::vector<std::uint32_t> &overview, bool snap)
{
    overview.assign(static_cast<std::size_t>(count) * TileAtlas::kOverview * TileAtlas::kOverview, 0);
    for (int tile = 0; tile < count; ++tile) {
        const std::uint32_t *tile_px = pixels + static_cast<std::size_t>(tile) * TileAtlas::kSize * TileAtlas::kSize;
        std::uint32_t *dst = overview.data() + static_cast<std::size_t>(tile) * TileAtlas::kOverview * TileAtlas::kOverview;
        for (int y = 0; y < TileAtlas::kOverview; ++y) {
            for (int x = 0; x < TileAtlas::kOverview; ++x) {
                const std::uint32_t p00 = tile_px[(y * 2) * TileAtlas::kSize + x * 2];
                const std::uint32_t p10 = tile_px[(y * 2) * TileAtlas::kSize + x * 2 + 1];
                const std::uint32_t p01 = tile_px[(y * 2 + 1) * TileAtlas::kSize + x * 2];
                const std::uint32_t p11 = tile_px[(y * 2 + 1) * TileAtlas::kSize + x * 2 + 1];
                auto mix = [&](int shift) {
                    const unsigned sum = ((p00 >> shift) & 255u) + ((p10 >> shift) & 255u) + ((p01 >> shift) & 255u) +
                                         ((p11 >> shift) & 255u);
                    return (sum + 2u) / 4u;
                };
                std::uint32_t word = (mix(24) << 24) | (mix(16) << 16) | (mix(8) << 8) | mix(0);
                if (snap) {
                    word = snap_vga_rgb((word >> 16) & 255u, (word >> 8) & 255u, word & 255u);
                }
                dst[y * TileAtlas::kOverview + x] = word;
            }
        }
    }
}

bool decode_png(const std::string &path, Sheet &sheet, bool snap, int &columns, int &count, std::string &error)
{
    cairo_surface_t *surface = cairo_image_surface_create_from_png(path.c_str());
    if (surface == nullptr || cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        error = "Lunduke City 2 could not read map tiles:\n" + path;
        if (surface != nullptr) {
            cairo_surface_destroy(surface);
        }
        return false;
    }
    cairo_surface_flush(surface);
    if (cairo_image_surface_get_format(surface) != CAIRO_FORMAT_ARGB32 &&
        cairo_image_surface_get_format(surface) != CAIRO_FORMAT_RGB24) {
        error = "Lunduke City 2 map tiles are not 8-bit RGB:\n" + path;
        cairo_surface_destroy(surface);
        return false;
    }
    const int width = cairo_image_surface_get_width(surface);
    const int height = cairo_image_surface_get_height(surface);
    columns = width / TileAtlas::kSize;
    const int rows = height / TileAtlas::kSize;
    if (columns < 1 || rows < 1) {
        error = "Lunduke City 2 map tiles are the wrong size:\n" + path;
        cairo_surface_destroy(surface);
        return false;
    }
    count = columns * rows;
    sheet.pixels.assign(static_cast<std::size_t>(count) * TileAtlas::kSize * TileAtlas::kSize, 0);
    sheet.averages.assign(static_cast<std::size_t>(count) * 3, 0);
    const int stride = cairo_image_surface_get_stride(surface);
    const unsigned char *src = cairo_image_surface_get_data(surface);
    for (int tile = 0; tile < count; ++tile) {
        const int col = tile % columns;
        const int row = tile / columns;
        unsigned long sum_r = 0;
        unsigned long sum_g = 0;
        unsigned long sum_b = 0;
        std::uint32_t *dst = sheet.pixels.data() + static_cast<std::size_t>(tile) * TileAtlas::kSize * TileAtlas::kSize;
        for (int y = 0; y < TileAtlas::kSize; ++y) {
            const unsigned char *line = src + (row * TileAtlas::kSize + y) * stride + col * TileAtlas::kSize * 4;
            for (int x = 0; x < TileAtlas::kSize; ++x) {
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
                dst[y * TileAtlas::kSize + x] = pack(r, g, b);
                sum_r += r;
                sum_g += g;
                sum_b += b;
            }
        }
        const unsigned long n = TileAtlas::kSize * TileAtlas::kSize;
        sheet.averages[static_cast<std::size_t>(tile) * 3] = static_cast<unsigned char>(sum_r / n);
        sheet.averages[static_cast<std::size_t>(tile) * 3 + 1] = static_cast<unsigned char>(sum_g / n);
        sheet.averages[static_cast<std::size_t>(tile) * 3 + 2] = static_cast<unsigned char>(sum_b / n);
    }
    cairo_surface_destroy(surface);
    build_overview(sheet.pixels.data(), count, sheet.overview, snap);
    return true;
}

} // namespace

TileAtlas &tile_atlas()
{
    static TileAtlas atlas;
    atlas.ensure_loaded();
    return atlas;
}

void TileAtlas::bind_active()
{
    const Sheet &sheet = use_svga_ ? sheets().svga : sheets().classic;
    pixels_ = sheet.pixels.empty() ? nullptr : sheet.pixels.data();
    overview_ = sheet.overview.empty() ? nullptr : sheet.overview.data();
    average_ = sheet.averages.empty() ? nullptr : sheet.averages.data();
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
    int columns = 0;
    int count = 0;
    if (!decode_png(path, sheets().classic, false, columns, count, error_)) {
        std::cerr << error_ << "\n";
        pixels_ = nullptr;
        return false;
    }
    columns_ = columns;
    count_ = count;
    use_svga_ = false;
    bind_active();

    // A missing or unreadable SVGA sheet leaves Classic in place. It is not
    // an error: the game still runs.
    const std::string svga_path = asset_file("images/micropolisEngine/tiles-svga.png");
    if (!svga_path.empty()) {
        Sheet extra;
        int svga_columns = 0;
        int svga_count = 0;
        std::string svga_error;
        if (decode_png(svga_path, extra, true, svga_columns, svga_count, svga_error) && svga_count == count_ &&
            svga_columns == columns_) {
            sheets().svga = std::move(extra);
            svga_ready_ = true;
        } else if (!svga_error.empty()) {
            std::cerr << "SVGA tiles unavailable, using Classic:\n" << svga_error << "\n";
        }
    }
    return true;
}

bool TileAtlas::set_look(bool svga)
{
    ensure_loaded();
    const bool next = svga && svga_ready_ && count_ > 0;
    if (next == use_svga_) {
        return use_svga_;
    }
    use_svga_ = next;
    bind_active();
    ++generation_;
    return use_svga_;
}

void TileAtlas::forget_svga()
{
    svga_ready_ = false;
    if (!use_svga_) {
        return;
    }
    use_svga_ = false;
    bind_active();
    ++generation_;
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
