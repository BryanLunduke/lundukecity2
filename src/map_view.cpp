// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#include "map_view.hpp"
#include "night_light.hpp"

#include "dither_mask.hpp"
#include "look_fx.hpp"

#include "assets.hpp"
#include "map_fit.hpp"
#include "present.hpp"
#include "special_buildings.hpp"

#include "sprite_art.hpp"
#include "tile_atlas.hpp"
#include "tools.hpp"
#include "vga_palette.hpp"
#include "view_math.hpp"

#include "micropolis.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

namespace {
// Ambient effects step at 10 frames a second; enough for smoke and water,
// and a running city already wakes about that often for the simulation.
constexpr int kFxStepMs = 100;
} // namespace

MapView::MapView()
{
    set_tile_size(tile_size_);
}

MapView::~MapView()
{
    if (map_pixels_ != nullptr) {
        cairo_surface_destroy(map_pixels_);
        map_pixels_ = nullptr;
    }
    if (light_pixels_ != nullptr) {
        cairo_surface_destroy(light_pixels_);
        light_pixels_ = nullptr;
    }
    if (window_pixels_ != nullptr) {
        cairo_surface_destroy(window_pixels_);
        window_pixels_ = nullptr;
    }
    if (night_pixels_ != nullptr) {
        cairo_surface_destroy(night_pixels_);
        night_pixels_ = nullptr;
    }
    if (lamp_pixels_ != nullptr) {
        cairo_surface_destroy(lamp_pixels_);
        lamp_pixels_ = nullptr;
    }
    if (fx_pixels_ != nullptr) {
        cairo_surface_destroy(fx_pixels_);
        fx_pixels_ = nullptr;
    }
    if (shade_pixels_ != nullptr) {
        cairo_surface_destroy(shade_pixels_);
        shade_pixels_ = nullptr;
    }
    if (cloud_pixels_ != nullptr) {
        cairo_surface_destroy(cloud_pixels_);
        cloud_pixels_ = nullptr;
    }
    if (ghost_pixels_ != nullptr) {
        cairo_surface_destroy(ghost_pixels_);
        ghost_pixels_ = nullptr;
    }
    if (dirt_pixels_ != nullptr) {
        cairo_surface_destroy(dirt_pixels_);
        dirt_pixels_ = nullptr;
    }
}

bool MapView::tick(int elapsed_ms)
{
    if (!active_) {
        return false;
    }
    blink_ms_ += elapsed_ms;
    if (blink_ms_ < 500) {
        return false;
    }
    blink_ms_ = 0;
    blink_on_ = !blink_on_;
    sync();
    return last_sync_blits_ > 0;
}

int MapView::blink_remaining_ms() const
{
    if (!active_) {
        return 500;
    }
    return std::max(1, 500 - blink_ms_);
}

void MapView::invalidate_all()
{
    class_all_ = true;
    if (!cached_raw_.empty()) {
        std::fill(cached_raw_.begin(), cached_raw_.end(), -1);
        std::fill(cached_shown_.begin(), cached_shown_.end(), -1);
    }
}

void MapView::set_session(CitySession *session)
{
    session_ = session;
    class_all_ = true;
    cached_raw_.clear();
    cached_shown_.clear();
}

void MapView::set_tool(int engine_tool)
{
    engine_tool_ = engine_tool;
}

void MapView::set_hover_tile(int tx, int ty)
{
    if (tx < 0 || ty < 0 || tx >= CitySession::kWorldW || ty >= CitySession::kWorldH) {
        clear_hover();
        return;
    }
    hover_valid_ = true;
    hover_x_ = tx;
    hover_y_ = ty;
}

void MapView::clear_hover()
{
    hover_valid_ = false;
}

bool MapView::hover_tile(int &tx, int &ty) const
{
    if (!hover_valid_) {
        return false;
    }
    tx = hover_x_;
    ty = hover_y_;
    return true;
}

bool MapView::stroke_origin(int &tx, int &ty) const
{
    if (!dragging_ || origin_x_ < 0 || origin_y_ < 0) {
        return false;
    }
    tx = origin_x_;
    ty = origin_y_;
    return true;
}

void MapView::set_dust(bool on, int elapsed_ms, int tile_x, int tile_y)
{
    dust_on_ = on;
    dust_ms_ = elapsed_ms;
    dust_tx_ = tile_x;
    dust_ty_ = tile_y;
}

void MapView::draw_dust(cairo_t *cr) const
{
    if (!dust_on_) {
        return;
    }
    const int tile = std::max(tile_size_, 1);
    const double origin_x = static_cast<double>(dust_tx_) * tile;
    const double origin_y = static_cast<double>(dust_ty_) * tile;
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    for (int i = 0; i < 10; ++i) {
        const int local_y = dust_speck_y(tile, dust_ms_, i);
        const int local_x = tile / 5 + (i * 3) % std::max(1, tile - 6);
        const int size = (i % 3 == 0) ? 4 : 3;
        if (i % 2 == 0) {
            cairo_set_source_rgb(cr, 1.0, 0.86, 0.55);
        } else {
            cairo_set_source_rgb(cr, 0.72, 0.55, 0.28);
        }
        cairo_rectangle(cr, origin_x + local_x, origin_y + local_y, size, size);
        cairo_fill(cr);
    }
    cairo_restore(cr);
}

void MapView::set_cue(const MapCue &cue)
{
    const bool placing = cue.active && cue.mark >= 1 && cue.mark <= 2 && cue.width > 0 && cue.height > 0;
    if (placing && (cue.left != pulse_left_ || cue.top != pulse_top_)) {
        // A short pulse each time the footprint snaps to a new cell. The
        // first cue of a hover only records where it is.
        if (pulse_left_ >= 0 && look_fx::motion_allowed(effects_)) {
            pulse_ms_ = 0;
        }
        pulse_left_ = cue.left;
        pulse_top_ = cue.top;
    } else if (!cue.active) {
        pulse_left_ = -1;
        pulse_top_ = -1;
        pulse_ms_ = -1;
    }
    cue_ = cue;
}

void MapView::set_tile_size(int pixels)
{
    tile_size_ = snap_map_tile(pixels);
}

void MapView::set_shake(int dx, int dy)
{
    shake_x_ = dx;
    shake_y_ = dy;
}

int MapView::pixel_width() const
{
    return CitySession::kWorldW * tile_size_;
}

int MapView::pixel_height() const
{
    return CitySession::kWorldH * tile_size_;
}

bool MapView::tile_at(double x, double y, int &tx, int &ty) const
{
    return map_tile_at(x, y, shake_x_, shake_y_, tile_size_, CitySession::kWorldW, CitySession::kWorldH, tx, ty);
}

int MapView::source_px() const
{
    return tile_size_ <= kMapTileOverview ? TileAtlas::kOverview : TileAtlas::kSize;
}

void MapView::ensure_surface()
{
    const int width = CitySession::kWorldW * source_px();
    const int height = CitySession::kWorldH * source_px();
    if (map_pixels_ == nullptr || cairo_image_surface_get_width(map_pixels_) != width ||
        cairo_image_surface_get_height(map_pixels_) != height) {
        if (map_pixels_ != nullptr) {
            cairo_surface_destroy(map_pixels_);
        }
        map_pixels_ = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
        night_stale_ = true;
        class_all_ = true;
        cached_raw_.clear();
        cached_shown_.clear();
    }
    const std::size_t cells = static_cast<std::size_t>(CitySession::kWorldW * CitySession::kWorldH);
    if (cached_raw_.size() != cells) {
        cached_raw_.assign(cells, -1);
        cached_shown_.assign(cells, -1);
    }
    if (special_cell_.size() != cells) {
        special_cell_.assign(cells, 0);
    }
}

int MapView::display_tile(int raw, const TileAtlas &atlas) const
{
    int tile = raw & LOMASK;
    if (blink_on_ && (raw & ZONEBIT) != 0 && (raw & PWRBIT) == 0 && atlas.loaded() &&
        LIGHTNINGBOLT < atlas.count()) {
        tile = LIGHTNINGBOLT;
    }
    return tile;
}

void MapView::blit_tile(int x, int y, int shown, const TileAtlas &atlas)
{
    const int px = source_px();
    unsigned char *data = cairo_image_surface_get_data(map_pixels_);
    const int stride = cairo_image_surface_get_stride(map_pixels_);
    unsigned char *dest = data + y * px * stride + x * px * 4;
    if (atlas.loaded()) {
        if (px == TileAtlas::kOverview) {
            atlas.blit_overview(shown, dest, stride);
        } else {
            atlas.blit(shown, dest, stride);
        }
        return;
    }
    const std::uint32_t dirt = (255u << 24) | (204u << 16) | (127u << 8) | 102u;
    for (int row = 0; row < px; ++row) {
        auto *out = reinterpret_cast<std::uint32_t *>(dest + row * stride);
        for (int col = 0; col < px; ++col) {
            out[col] = dirt;
        }
    }
}

namespace {

cairo_surface_t *load_png(const std::string &path)
{
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

// City 2's own disaster art: data/disaster-art/<name>.png for the Classic
// look and <name>-svga.png for SVGA (Spooky uses SVGA). Sheets are one row
// of square cells as tall as the sheet (barricades 16, the lion 24). Null
// when the file is missing; the map just goes without.
cairo_surface_t *disaster_sheet(const char *name)
{
    struct Cache {
        cairo_surface_t *image[2][2] = {{nullptr, nullptr}, {nullptr, nullptr}};
        bool tried[2][2] = {{false, false}, {false, false}};
        ~Cache()
        {
            for (auto &row : image) {
                for (cairo_surface_t *surface : row) {
                    if (surface != nullptr) {
                        cairo_surface_destroy(surface);
                    }
                }
            }
        }
    };
    static Cache cache;
    const int which = std::string(name) == "lion" ? 0 : 1;
    const int look = tile_atlas().look() >= 1 ? 1 : 0;
    if (!cache.tried[which][look]) {
        cache.tried[which][look] = true;
        cache.image[which][look] =
            load_png(ui_data_file(std::string("disaster-art/") + name + (look == 1 ? "-svga.png" : ".png")));
    }
    return cache.image[which][look];
}

// One cell of a disaster sheet with its top-left at (x, y) in 16px map
// pixels. At dusk it takes the same tint as the engine's sprites.
void paint_disaster_cell(cairo_t *cr, cairo_surface_t *sheet, int cell, double x, double y, double sprite_to_source,
                         bool dusk, double dusk_r, double dusk_g, double dusk_b)
{
    if (sheet == nullptr) {
        return;
    }
    const double size = cairo_image_surface_get_height(sheet);
    cairo_save(cr);
    cairo_translate(cr, x * sprite_to_source, y * sprite_to_source);
    cairo_scale(cr, sprite_to_source, sprite_to_source);
    cairo_rectangle(cr, 0, 0, size, size);
    cairo_clip(cr);
    cairo_set_source_surface(cr, sheet, -size * cell, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
    cairo_paint(cr);
    if (dusk) {
        cairo_set_operator(cr, CAIRO_OPERATOR_MULTIPLY);
        cairo_set_source_rgb(cr, dusk_r, dusk_g, dusk_b);
        cairo_mask_surface(cr, sheet, -size * cell, 0);
    }
    cairo_restore(cr);
}

// How the 8px overview snaps a special sheet: like the atlas the sheet
// belongs to (SVGA sheets keep their tan dirt ramp, Spooky snaps to VGA).
enum class SpecialSnap { None, Vga, Svga };

cairo_surface_t *special_sprite(int index, SpecialSnap &snap)
{
    snap = SpecialSnap::None;
    struct Slot {
        cairo_surface_t *image = nullptr;
        bool tried = false;
    };
    struct Cache {
        // One slot per table row (special_buildings.cpp asserts it fits).
        Slot slots[kSpecialSpriteSlots];
        Slot svga[kSpecialSpriteSlots];
        Slot spooky[kSpecialSpriteSlots];
        ~Cache()
        {
            for (Slot &slot : slots) {
                if (slot.image != nullptr) {
                    cairo_surface_destroy(slot.image);
                }
            }
            for (Slot &slot : svga) {
                if (slot.image != nullptr) {
                    cairo_surface_destroy(slot.image);
                }
            }
            for (Slot &slot : spooky) {
                if (slot.image != nullptr) {
                    cairo_surface_destroy(slot.image);
                }
            }
        }
    };
    static Cache cache;
    if (index < 0 || index >= kSpecialSpriteSlots || index >= special_building_count()) {
        return nullptr;
    }
    const char *name = special_building(index).map_sprite;
    const int look = tile_atlas().look();
    // Each look has its own sheet. A missing file falls through: Spooky
    // tries SVGA, then the classic sheet. The classic sheet is not snapped.
    auto take = [](Slot &slot, const std::string &path) {
        if (!slot.tried) {
            slot.tried = true;
            slot.image = load_png(path);
        }
        return slot.image;
    };
    if (look == 2) {
        cairo_surface_t *night = take(cache.spooky[index], ui_data_file(std::string("special-tiles/") + name + "-spooky.png"));
        if (night != nullptr) {
            snap = SpecialSnap::Vga;
            return night;
        }
    }
    if (look >= 1) {
        cairo_surface_t *day = take(cache.svga[index], ui_data_file(std::string("special-tiles/") + name + "-svga.png"));
        if (day != nullptr) {
            snap = SpecialSnap::Svga;
            return day;
        }
    }
    Slot &slot = cache.slots[index];
    if (slot.tried) {
        return slot.image;
    }
    slot.tried = true;
    slot.image = load_png(ui_data_file(std::string("special-tiles/") + special_building(index).map_sprite + ".png"));
    return slot.image;
}

// The Data Center's night glass: data/special-tiles/datacenter-glass.png
// (opaque where the facade glass is, the same in every look) as 3x3 cells
// of window-mask rows. Empty when the file is missing.
const std::vector<std::uint16_t> &datacenter_glass_sheet()
{
    static std::vector<std::uint16_t> sheet;
    static bool tried = false;
    if (tried) {
        return sheet;
    }
    tried = true;
    const int index = special_datacenter_index();
    if (index < 0) {
        return sheet;
    }
    const SpecialBuilding &building = special_building(index);
    const int span = building.width;
    cairo_surface_t *mask = load_png(ui_data_file("special-tiles/datacenter-glass.png"));
    if (mask == nullptr) {
        return sheet;
    }
    cairo_surface_flush(mask);
    const int kt = night_light::kTile;
    if (cairo_image_surface_get_width(mask) == span * kt && cairo_image_surface_get_height(mask) == span * kt &&
        cairo_image_surface_get_format(mask) == CAIRO_FORMAT_ARGB32) {
        sheet.assign(static_cast<std::size_t>(span * span * kt), 0);
        const unsigned char *data = cairo_image_surface_get_data(mask);
        const int stride = cairo_image_surface_get_stride(mask);
        for (int y = 0; y < span * kt; ++y) {
            const auto *row = reinterpret_cast<const std::uint32_t *>(data + y * stride);
            for (int x = 0; x < span * kt; ++x) {
                if ((row[x] >> 24) < 128) {
                    continue;
                }
                const int cell = (y / kt) * span + x / kt;
                std::uint16_t &bits = sheet[static_cast<std::size_t>(cell * kt + y % kt)];
                bits = static_cast<std::uint16_t>(bits | (1u << (x % kt)));
            }
        }
    }
    cairo_surface_destroy(mask);
    return sheet;
}

// The micro nuclear plant's night light: data/special-tiles/
// micro-nuclear-light.png (16x16; colour is the light, alpha its strength,
// the same in every look). No lamp when the file is missing.
const night_light::TileLamp &micro_nuclear_lamp()
{
    static night_light::TileLamp lamp;
    static bool tried = false;
    if (tried) {
        return lamp;
    }
    tried = true;
    cairo_surface_t *sheet = load_png(ui_data_file("special-tiles/micro-nuclear-light.png"));
    if (sheet == nullptr) {
        return lamp;
    }
    cairo_surface_flush(sheet);
    const int kt = night_light::kTile;
    if (cairo_image_surface_get_width(sheet) == kt && cairo_image_surface_get_height(sheet) == kt &&
        cairo_image_surface_get_format(sheet) == CAIRO_FORMAT_ARGB32) {
        lamp.pixels.assign(static_cast<std::size_t>(kt * kt), 0);
        const unsigned char *data = cairo_image_surface_get_data(sheet);
        const int stride = cairo_image_surface_get_stride(sheet);
        for (int y = 0; y < kt; ++y) {
            const auto *row = reinterpret_cast<const std::uint32_t *>(data + y * stride);
            for (int x = 0; x < kt; ++x) {
                const std::uint32_t p = row[x];
                const std::uint32_t a = p >> 24;
                if (a == 0) {
                    continue;
                }
                // Cairo stores premultiplied colour; the lamp wants it straight.
                auto un = [a](std::uint32_t c) { return std::min<std::uint32_t>(255, (c * 255 + a / 2) / a); };
                lamp.pixels[static_cast<std::size_t>(y * kt + x)] =
                    (a << 24) | (un((p >> 16) & 0xFF) << 16) | (un((p >> 8) & 0xFF) << 8) | un(p & 0xFF);
            }
        }
        lamp.tile = SPECIAL_MICRONUKE;
    }
    cairo_surface_destroy(sheet);
    return lamp;
}

int session_word(int x, int y, const void *ctx)
{
    const CitySession *session = static_cast<const CitySession *>(ctx);
    if (session == nullptr) {
        return -1;
    }
    return session->map_value(x, y);
}

// 0 when this cell is not part of a special building. Otherwise a key that
// changes when the building identity or the cell inside it changes.
int special_display_key(const CitySession *session, int x, int y, int &index, int &cell)
{
    index = -1;
    cell = 0;
    if (session == nullptr) {
        return 0;
    }
    const int raw = session->map_value(x, y);
    const int tile = raw & LOMASK;
    if (tile != kSpecialBodyTile && special_index_for_tile(tile) < 0) {
        return 0;
    }
    if (!special_sheet_at(x, y, session_word, session, index, cell)) {
        return 39990;
    }
    return 40000 + index * 32 + cell;
}

} // namespace

void MapView::blit_special(int x, int y, int index, int cell)
{
    const int px = source_px();
    unsigned char *data = cairo_image_surface_get_data(map_pixels_);
    const int stride = cairo_image_surface_get_stride(map_pixels_);
    unsigned char *dest = data + y * px * stride + x * px * 4;
    auto fill = [&](std::uint32_t color) {
        for (int row = 0; row < px; ++row) {
            auto *out = reinterpret_cast<std::uint32_t *>(dest + row * stride);
            for (int col = 0; col < px; ++col) {
                out[col] = color;
            }
        }
    };
    SpecialSnap snap = SpecialSnap::None;
    cairo_surface_t *sprite = special_sprite(index, snap);
    const int cols = index >= 0 && index < special_building_count() && special_building(index).width > 0
                         ? special_building(index).width
                         : 3;
    const int col = cell % cols;
    const int row = cell / cols;
    if (sprite == nullptr || col < 0 || row < 0) {
        const std::uint32_t fallback[3] = {
            (255u << 24) | (48u << 16) | (140u << 8) | 40u,
            (255u << 24) | (48u << 16) | (160u << 8) | 32u,
            (255u << 24) | (90u << 16) | (110u << 8) | 150u,
        };
        fill(index >= 0 && index < 3 ? fallback[index] : ((255u << 24) | (40u << 16) | (60u << 8) | 70u));
        return;
    }
    cairo_surface_flush(sprite);
    const int sw = cairo_image_surface_get_width(sprite);
    const int sh = cairo_image_surface_get_height(sprite);
    const int sx = col * TileAtlas::kSize;
    const int sy = row * TileAtlas::kSize;
    if (sx + TileAtlas::kSize > sw || sy + TileAtlas::kSize > sh) {
        fill((255u << 24) | (40u << 16) | (60u << 8) | 70u);
        return;
    }
    const int sstride = cairo_image_surface_get_stride(sprite);
    const unsigned char *src = cairo_image_surface_get_data(sprite);
    if (px == TileAtlas::kOverview) {
        auto word_at = [&](int rx, int ry) {
            const unsigned char *p = src + (sy + ry) * sstride + (sx + rx) * 4;
            return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
                   (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
        };
        auto mix = [](std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d, int shift) {
            const unsigned sum = ((a >> shift) & 255u) + ((b >> shift) & 255u) + ((c >> shift) & 255u) + ((d >> shift) & 255u);
            return (sum + 2u) / 4u;
        };
        for (int r = 0; r < TileAtlas::kOverview; ++r) {
            auto *out = reinterpret_cast<std::uint32_t *>(dest + r * stride);
            for (int c = 0; c < TileAtlas::kOverview; ++c) {
                const std::uint32_t p00 = word_at(c * 2, r * 2);
                const std::uint32_t p10 = word_at(c * 2 + 1, r * 2);
                const std::uint32_t p01 = word_at(c * 2, r * 2 + 1);
                const std::uint32_t p11 = word_at(c * 2 + 1, r * 2 + 1);
                const unsigned red = mix(p00, p10, p01, p11, 16);
                const unsigned green = mix(p00, p10, p01, p11, 8);
                const unsigned blue = mix(p00, p10, p01, p11, 0);
                const unsigned alpha = mix(p00, p10, p01, p11, 24);
                if (snap == SpecialSnap::Svga) {
                    out[c] = snap_svga_rgb(red, green, blue);
                } else if (snap == SpecialSnap::Vga) {
                    out[c] = snap_vga_rgb(red, green, blue);
                } else {
                    out[c] = (alpha << 24) | (red << 16) | (green << 8) | blue;
                }
            }
        }
        return;
    }
    for (int r = 0; r < TileAtlas::kSize; ++r) {
        std::memcpy(dest + r * stride, src + (sy + r) * sstride + sx * 4, static_cast<std::size_t>(TileAtlas::kSize * 4));
    }
}

void MapView::sync()
{
    ensure_surface();
    TileAtlas &atlas = tile_atlas();
    if (atlas.loaded() != atlas_was_loaded_ || atlas.look_generation() != look_generation_) {
        atlas_was_loaded_ = atlas.loaded();
        look_generation_ = atlas.look_generation();
        class_all_ = true;
        std::fill(cached_raw_.begin(), cached_raw_.end(), -1);
        std::fill(cached_shown_.begin(), cached_shown_.end(), -1);
    }
    int dirty = 0;
    std::vector<int> grown;
    const int world_w = CitySession::kWorldW;
    const int world_h = CitySession::kWorldH;
    for (int y = 0; y < world_h; ++y) {
        for (int x = 0; x < world_w; ++x) {
            const int raw = session_ != nullptr ? session_->map_value(x, y) : 0;
            const std::size_t index = static_cast<std::size_t>(y * world_w + x);
            int special_index = -1;
            int special_cell = 0;
            const int special_key = special_display_key(session_, x, y, special_index, special_cell);
            if (special_cell_.size() == cached_raw_.size()) {
                special_cell_[index] = special_key != 0 ? 1 : 0;
            }
            if (special_key != 0) {
                const bool bolt = special_center_needs_bolt(raw, special_index, special_cell, blink_on_);
                const int shown_key = bolt ? special_key + 50000 : special_key;
                if (cached_raw_[index] == raw && cached_shown_[index] == shown_key) {
                    continue;
                }
                if (bolt && atlas.loaded() && LIGHTNINGBOLT < atlas.count()) {
                    blit_tile(x, y, LIGHTNINGBOLT, atlas);
                } else {
                    blit_special(x, y, special_index, special_cell);
                }
                cached_raw_[index] = raw;
                cached_shown_[index] = shown_key;
                ++dirty;
                if (!class_all_) {
                    class_dirty_.push_back(static_cast<int>(index));
                }
                if (!night_stale_) {
                    night_dirty_.push_back(static_cast<int>(index));
                }
                continue;
            }
            const int shown = display_tile(raw, atlas);
            if (cached_raw_[index] == raw && cached_shown_[index] == shown) {
                continue;
            }
            if (cached_raw_[index] >= 0 && (raw & ZONEBIT) != 0 && (cached_raw_[index] & ZONEBIT) != 0 &&
                (raw & LOMASK) != (cached_raw_[index] & LOMASK) && (raw & LOMASK) >= RESBASE &&
                (raw & LOMASK) < PORTBASE) {
                grown.push_back(static_cast<int>(index));
            }
            blit_tile(x, y, shown, atlas);
            cached_raw_[index] = raw;
            cached_shown_[index] = shown;
            ++dirty;
            if (!class_all_) {
                class_dirty_.push_back(static_cast<int>(index));
            }
            if (!night_stale_) {
                night_dirty_.push_back(static_cast<int>(index));
            }
        }
    }
    last_sync_blits_ = dirty;
    if (!grown.empty() && dirty < 160) {
        // A zone centre changed building by itself (growth or decline).
        // Not on a load or a look change, where every tile is new.
        for (int index : grown) {
            start_rise(index % world_w - 1, index / world_w - 1, 3, 3, true);
        }
    }
    if (dirty > 0 || class_all_) {
        refresh_pixel_classes();
    }
    if (night_dirty_.size() > static_cast<std::size_t>(world_w * world_h)) {
        // Night was held off a long while; retint the whole map once.
        night_dirty_.clear();
        night_stale_ = true;
    }
    if (dirty > 0) {
        cairo_surface_mark_dirty(map_pixels_);
        light_dirty_ = true;
        if (dirty > 256) {
            // A loaded or newly generated city: light it on this frame
            // instead of waiting out the rebuild throttle.
            light_built_ = {};
        }
    }
}

void MapView::follow_local_pointer(double x, double y, bool drag)
{
    int tx = 0;
    int ty = 0;
    if (!tile_at(x, y, tx, ty)) {
        clear_hover();
        return;
    }
    set_hover_tile(tx, ty);
    if (!drag || !dragging_) {
        return;
    }
    if (tx == last_x_ && ty == last_y_) {
        return;
    }
    if (on_tool_drag) {
        on_tool_drag(last_x_, last_y_, tx, ty);
    }
    last_x_ = tx;
    last_y_ = ty;
}

bool MapView::press(double x, double y)
{
    int tx = 0;
    int ty = 0;
    if (!tile_at(x, y, tx, ty)) {
        return false;
    }
    dragging_ = true;
    drag_moved_ = false;
    press_local_x_ = x;
    press_local_y_ = y;
    edge_holding_ = false;
    origin_x_ = tx;
    origin_y_ = ty;
    last_x_ = tx;
    last_y_ = ty;
    set_hover_tile(tx, ty);
    if (on_tool_down) {
        on_tool_down(tx, ty);
    }
    return true;
}

void MapView::release()
{
    dragging_ = false;
    drag_moved_ = false;
    edge_holding_ = false;
    origin_x_ = -1;
    origin_y_ = -1;
}

void MapView::motion(double x, double y, bool button_down)
{
    if (dragging_ && !button_down) {
        dragging_ = false;
        drag_moved_ = false;
        edge_holding_ = false;
    }
    if (dragging_) {
        const double dx = x - press_local_x_;
        const double dy = y - press_local_y_;
        if (dx * dx + dy * dy > 16.0) {
            drag_moved_ = true;
        }
    }
    follow_local_pointer(x, y, dragging_ && button_down);
}

void MapView::leave()
{
    if (!dragging_) {
        clear_hover();
    }
}

bool MapView::edge_scroll(double view_x, double view_y, int view_w, int view_h, int elapsed_ms, double &dx, double &dy)
{
    dx = 0;
    dy = 0;
    if (!dragging_ || !drag_moved_ || view_w < 1 || view_h < 1) {
        edge_holding_ = false;
        return false;
    }
    const int margin = 28;
    int sx = 0;
    int sy = 0;
    if (view_x < margin) {
        sx = -1;
    } else if (view_x > view_w - margin) {
        sx = 1;
    }
    if (view_y < margin) {
        sy = -1;
    } else if (view_y > view_h - margin) {
        sy = 1;
    }
    if (sx == 0 && sy == 0) {
        edge_holding_ = false;
        return false;
    }
    const auto now = std::chrono::steady_clock::now();
    if (!edge_holding_) {
        edge_holding_ = true;
        edge_hold_start_ = now;
        return false;
    }
    if (now - edge_hold_start_ < std::chrono::milliseconds(300)) {
        return false;
    }
    double seconds = elapsed_ms / 1000.0;
    if (seconds < 0.001) {
        seconds = 0.001;
    }
    if (seconds > 0.1) {
        seconds = 0.1;
    }
    auto depth = [](double into) {
        if (into < 0.0) {
            into = 0.0;
        }
        if (into > 1.0) {
            into = 1.0;
        }
        return into;
    };
    const double tile = static_cast<double>(std::max(tile_size_, 8));
    // About 12 tiles a second at the inner edge of the margin, faster as
    // the pointer sits deeper in it.
    if (sx != 0) {
        const double into = sx < 0 ? (margin - view_x) / margin : (view_x - (view_w - margin)) / margin;
        dx = sx * tile * (12.0 * (0.45 + 1.3 * depth(into))) * seconds;
    }
    if (sy != 0) {
        const double into = sy < 0 ? (margin - view_y) / margin : (view_y - (view_h - margin)) / margin;
        dy = sy * tile * (12.0 * (0.45 + 1.3 * depth(into))) * seconds;
    }
    return dx != 0.0 || dy != 0.0;
}

namespace {

void vga_rgb(cairo_t *cr, int r, int g, int b)
{
    cairo_set_source_rgb(cr, r / 255.0, g / 255.0, b / 255.0);
}

void corner_arm(cairo_t *cr, double x, double y, double dx, double dy, double len)
{
    cairo_move_to(cr, x, y);
    cairo_line_to(cr, x + dx * len, y);
    cairo_move_to(cr, x, y);
    cairo_line_to(cr, x, y + dy * len);
}

void paint_corners(cairo_t *cr, double x, double y, double w, double h, double len, int kind)
{
    // kind 1 solid, 2 cross, 3 hatch, 4 broken. A dark halo sits under the ink.
    const double inset = 1.0;
    cairo_set_line_width(cr, 3.0);
    vga_rgb(cr, 0, 0, 0);
    auto arms = [&](double shrink) {
        const double x0 = x + shrink;
        const double y0 = y + shrink;
        const double x1 = x + w - shrink;
        const double y1 = y + h - shrink;
        const double arm = std::max(2.0, len - shrink);
        if (kind == 2) {
            const double s = arm * 0.7;
            cairo_move_to(cr, x0, y0);
            cairo_line_to(cr, x0 + s, y0 + s);
            cairo_move_to(cr, x0 + s, y0);
            cairo_line_to(cr, x0, y0 + s);
            cairo_move_to(cr, x1, y0);
            cairo_line_to(cr, x1 - s, y0 + s);
            cairo_move_to(cr, x1 - s, y0);
            cairo_line_to(cr, x1, y0 + s);
            cairo_move_to(cr, x0, y1);
            cairo_line_to(cr, x0 + s, y1 - s);
            cairo_move_to(cr, x0 + s, y1);
            cairo_line_to(cr, x0, y1 - s);
            cairo_move_to(cr, x1, y1);
            cairo_line_to(cr, x1 - s, y1 - s);
            cairo_move_to(cr, x1 - s, y1);
            cairo_line_to(cr, x1, y1 - s);
            return;
        }
        if (kind == 3) {
            const double s = arm;
            cairo_move_to(cr, x0, y0 + s);
            cairo_line_to(cr, x0 + s, y0);
            cairo_move_to(cr, x1, y0 + s);
            cairo_line_to(cr, x1 - s, y0);
            cairo_move_to(cr, x0, y1 - s);
            cairo_line_to(cr, x0 + s, y1);
            cairo_move_to(cr, x1, y1 - s);
            cairo_line_to(cr, x1 - s, y1);
            return;
        }
        if (kind == 4) {
            const double gap = std::max(2.0, arm * 0.45);
            cairo_move_to(cr, x0, y0 + gap);
            cairo_line_to(cr, x0, y0 + arm);
            cairo_move_to(cr, x0 + gap, y0);
            cairo_line_to(cr, x0 + arm, y0);
            cairo_move_to(cr, x1, y0 + gap);
            cairo_line_to(cr, x1, y0 + arm);
            cairo_move_to(cr, x1 - gap, y0);
            cairo_line_to(cr, x1 - arm, y0);
            cairo_move_to(cr, x0, y1 - gap);
            cairo_line_to(cr, x0, y1 - arm);
            cairo_move_to(cr, x0 + gap, y1);
            cairo_line_to(cr, x0 + arm, y1);
            cairo_move_to(cr, x1, y1 - gap);
            cairo_line_to(cr, x1, y1 - arm);
            cairo_move_to(cr, x1 - gap, y1);
            cairo_line_to(cr, x1 - arm, y1);
            return;
        }
        corner_arm(cr, x0, y0, 1, 1, arm);
        corner_arm(cr, x1, y0, -1, 1, arm);
        corner_arm(cr, x0, y1, 1, -1, arm);
        corner_arm(cr, x1, y1, -1, -1, arm);
    };
    arms(0.0);
    cairo_stroke(cr);
    cairo_set_line_width(cr, 1.5);
    if (kind == 5) {
        vga_rgb(cr, 255, 255, 255);
    } else if (kind == 1) {
        vga_rgb(cr, 0, 170, 0);
    } else if (kind == 2) {
        vga_rgb(cr, 255, 190, 0);
    } else if (kind == 4) {
        vga_rgb(cr, 255, 255, 85);
    } else {
        vga_rgb(cr, 170, 0, 0);
    }
    arms(inset);
    cairo_stroke(cr);
}

void paint_tag(cairo_t *cr, double x, double y, double w, double h, const std::string &text, int tile, int slot,
               bool slate, double min_px)
{
    if (text.empty()) {
        return;
    }
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    const double font_px = std::max(min_px, tile * 0.7);
    cairo_set_font_size(cr, font_px);
    cairo_text_extents_t extents;
    cairo_text_extents(cr, text.c_str(), &extents);
    // A raised (handheld) tag gets padding to match; the desktop keeps 3px.
    const double pad = min_px > 11.0 ? std::max(3.0, font_px * 0.22) : 3.0;
    const double tw = extents.width + pad * 2;
    const double th = extents.height + pad * 2;
    double tx = w > tw ? x + (w - tw) / 2.0 : x;
    const int stack = slot < 0 ? 0 : slot;
    double ty = y - (stack + 1) * (th + 2.0);
    if (ty < 0.0) {
        ty = y + h + 2.0 + stack * (th + 2.0);
    }
    const double cx = x + w * 0.5;
    const double cy = y + h * 0.5;
    if (cx >= tx && cx <= tx + tw && cy >= ty && cy <= ty + th) {
        tx = cx + std::max(8.0, w * 0.55);
    }
    if (slate) {
        // Slate chip: dark plate, gold edge, cream text. Same place and size.
        cairo_save(cr);
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
        cairo_set_source_rgba(cr, 0, 0, 0, 0.45);
        cairo_rectangle(cr, tx + 1.5, ty + 2.0, tw, th);
        cairo_fill(cr);
        cairo_set_source_rgb(cr, 0x1E / 255.0, 0x26 / 255.0, 0x31 / 255.0);
        cairo_rectangle(cr, tx, ty, tw, th);
        cairo_fill(cr);
        cairo_set_source_rgb(cr, 0xF2 / 255.0, 0xC1 / 255.0, 0x4E / 255.0);
        cairo_set_line_width(cr, 1.0);
        cairo_rectangle(cr, tx + 0.5, ty + 0.5, tw - 1.0, th - 1.0);
        cairo_stroke(cr);
        cairo_set_source_rgb(cr, 0xF6 / 255.0, 0xF0 / 255.0, 0xE4 / 255.0);
        cairo_move_to(cr, tx + pad - extents.x_bearing, ty + pad - extents.y_bearing);
        cairo_show_text(cr, text.c_str());
        cairo_restore(cr);
        return;
    }
    vga_rgb(cr, 0, 0, 0);
    cairo_rectangle(cr, tx + 1.0, ty + 1.0, tw, th);
    cairo_fill(cr);
    vga_rgb(cr, 255, 255, 85);
    cairo_rectangle(cr, tx, ty, tw, th);
    cairo_fill(cr);
    vga_rgb(cr, 0, 0, 0);
    cairo_set_line_width(cr, 1.0);
    cairo_rectangle(cr, tx + 0.5, ty + 0.5, tw - 1.0, th - 1.0);
    cairo_stroke(cr);
    cairo_move_to(cr, tx + pad - extents.x_bearing, ty + pad - extents.y_bearing);
    cairo_show_text(cr, text.c_str());
}

bool bayer_keep(int x, int y)
{
    static const int bayer[4][4] = {
        {0, 8, 2, 10},
        {12, 4, 14, 6},
        {3, 11, 1, 9},
        {15, 7, 13, 5},
    };
    return bayer[y & 3][x & 3] < 8;
}

void paint_special_ghost(cairo_t *cr, int index, double x, double y, double w, double h, bool blocked)
{
    if (index < 0 || w < 2.0 || h < 2.0) {
        return;
    }
    SpecialSnap snap = SpecialSnap::None;
    cairo_surface_t *sprite = special_sprite(index, snap);
    if (sprite == nullptr) {
        return;
    }
    cairo_surface_flush(sprite);
    const int sw = cairo_image_surface_get_width(sprite);
    const int sh = cairo_image_surface_get_height(sprite);
    const int stride = cairo_image_surface_get_stride(sprite);
    const unsigned char *src = cairo_image_surface_get_data(sprite);
    if (src == nullptr || sw < 1 || sh < 1) {
        return;
    }
    const int pixels_w = std::max(1, static_cast<int>(w));
    const int pixels_h = std::max(1, static_cast<int>(h));
    for (int py = 0; py < pixels_h; ++py) {
        for (int px = 0; px < pixels_w; ++px) {
            if (!bayer_keep(px, py)) {
                continue;
            }
            const int sx = std::min(sw - 1, px * sw / pixels_w);
            const int sy = std::min(sh - 1, py * sh / pixels_h);
            const unsigned char *p = src + sy * stride + sx * 4;
            if (p[3] < 16) {
                continue;
            }
            int red = p[2];
            int green = p[1];
            int blue = p[0];
            if (blocked) {
                red = (red + 170) / 2;
                green = green / 3;
                blue = blue / 3;
            }
            cairo_set_source_rgb(cr, red / 255.0, green / 255.0, blue / 255.0);
            cairo_rectangle(cr, x + px, y + py, 1.0, 1.0);
            cairo_fill(cr);
        }
    }
}

} // namespace

void MapView::draw_footprint(cairo_t *cr) const
{
    if (cue_.active || cue_.zone || cue_.receipt) {
        cairo_save(cr);
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
        const double tile = static_cast<double>(std::max(tile_size_, 1));
        // A click's receipt over the footprint the pointer still covers:
        // only the newest tag (the receipt) is drawn. Two tags there used to
        // stack into each other ("Ca|Can't build on water").
        const bool receipt_shown = cue_.receipt && cue_.rwidth > 0 && cue_.rheight > 0 && !cue_.receipt_text.empty();
        const bool overlap = receipt_shown && cue_.active && cue_.left < cue_.rleft + cue_.rwidth + 2 &&
                             cue_.rleft < cue_.left + cue_.width + 2 && cue_.top < cue_.rtop + cue_.rheight + 2 &&
                             cue_.rtop < cue_.top + cue_.height + 2;
        const std::string live_tag = overlap ? std::string() : cue_.tag;
        if (cue_.zone && cue_.zwidth > 0 && cue_.zheight > 0) {
            const double x = cue_.zleft * tile;
            const double y = cue_.ztop * tile;
            const double w = cue_.zwidth * tile;
            const double h = cue_.zheight * tile;
            paint_corners(cr, x, y, w, h, std::min(8.0, tile * 0.45), 5);
        }
        if (cue_.active && cue_.mark >= 1 && cue_.width > 0 && cue_.height > 0) {
            const double x = cue_.left * tile;
            const double y = cue_.top * tile;
            const double w = cue_.width * tile;
            const double h = cue_.height * tile;
            // Right after a successful click the pointer still covers the
            // new building; its receipt speaks for it, not a red ghost.
            const bool just_built = overlap && cue_.receipt_ok;
            if (cue_.sprite >= 0) {
                paint_special_ghost(cr, cue_.sprite, x, y, w, h, cue_.mark >= 3);
            } else if (!just_built) {
                draw_ghost(cr, x, y, w, h);
            }
            if (cue_.outline) {
                cairo_rectangle(cr, x + 1.0, y + 1.0, w - 2.0, h - 2.0);
                cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
                cairo_set_line_width(cr, 3.0);
                cairo_stroke(cr);
                cairo_rectangle(cr, x + 2.0, y + 2.0, w - 4.0, h - 4.0);
                cairo_set_source_rgb(cr, 1.0, 1.0, 0.33);
                cairo_set_line_width(cr, 1.0);
                cairo_stroke(cr);
            }
            paint_corners(cr, x, y, w, h, std::min(10.0, tile * 0.55), cue_.mark);
            paint_tag(cr, x, y, w, h, live_tag, tile_size_, 0, slate_tags_, tag_min_px_);
        }
        if (cue_.receipt && cue_.rwidth > 0 && cue_.rheight > 0) {
            const double x = cue_.rleft * tile;
            const double y = cue_.rtop * tile;
            const double w = cue_.rwidth * tile;
            const double h = cue_.rheight * tile;
            paint_corners(cr, x, y, w, h, std::min(10.0, tile * 0.6), cue_.receipt_ok ? 1 : 3);
            const int receipt_slot = cue_.active && !live_tag.empty() ? 1 : 0;
            paint_tag(cr, x, y, w, h, cue_.receipt_text, tile_size_, receipt_slot, slate_tags_, tag_min_px_);
        }
        cairo_restore(cr);
        if (cue_.active) {
            return;
        }
    }
    if (!hover_valid_) {
        return;
    }
    ToolFootprint foot = tool_footprint(engine_tool_);
    // Query has no building footprint. At the overview zoom the crosshair is
    // a whole tile, so the cell under it still needs an outline before A.
    if (tile_size_ <= kMapTileOverview && !foot.placeable) {
        foot.placeable = true;
        foot.width = 1;
        foot.height = 1;
        foot.cursor_to_left = 0;
        foot.cursor_to_top = 0;
    }
    if (!foot.placeable || foot.width < 1 || foot.height < 1) {
        return;
    }
    const double x = static_cast<double>(hover_x_ - foot.cursor_to_left) * tile_size_;
    const double y = static_cast<double>(hover_y_ - foot.cursor_to_top) * tile_size_;
    const double w = static_cast<double>(foot.width) * tile_size_;
    const double h = static_cast<double>(foot.height) * tile_size_;
    if (w < 2.0 || h < 2.0) {
        return;
    }
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    if (tile_size_ <= kMapTileOverview) {
        cairo_rectangle(cr, x, y, w, h);
        cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.90);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);
        cairo_rectangle(cr, x + 1.0, y + 1.0, w - 2.0, h - 2.0);
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.95);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);
    } else {
        // Deck scale (16px and up) gets a dark band and a thicker white edge
        // so the tile under the cursor reads at arm's length.
        const bool deck_scale = tile_size_ >= 16;
        const double halo = deck_scale ? 5.0 : 3.0;
        const double ink = deck_scale ? 2.5 : 1.5;
        cairo_rectangle(cr, x + 1.0, y + 1.0, w - 2.0, h - 2.0);
        cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.92);
        cairo_set_line_width(cr, halo);
        cairo_stroke(cr);
        cairo_rectangle(cr, x + 2.0, y + 2.0, w - 4.0, h - 4.0);
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.96);
        cairo_set_line_width(cr, ink);
        cairo_stroke(cr);
    }
    cairo_restore(cr);
}

void MapView::draw(cairo_t *cr)
{
    ui_tail_ = false;
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    if (map_pixels_ == nullptr) {
        sync();
    }
    if (map_pixels_ == nullptr) {
        return;
    }

    cairo_save(cr);
    cairo_translate(cr, shake_x_, shake_y_);
    // 8px draws the prefiltered half atlas 1:1. Larger steps scale the 16px
    // atlas by an integer. Sprite positions are in 16px map pixels.
    const int source = source_px();
    const double scale = static_cast<double>(tile_size_) / static_cast<double>(source);
    const double sprite_to_source = static_cast<double>(source) / static_cast<double>(TileAtlas::kSize);
    const bool dusk = night_ && !night_hold_;
    if (dusk) {
        refresh_night_base();
    }
    cairo_save(cr);
    cairo_scale(cr, scale, scale);
    cairo_set_source_surface(cr, dusk && night_pixels_ != nullptr ? night_pixels_ : map_pixels_, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
    cairo_paint(cr);
    const bool sunlit = !night_hold_ && !dusk && source == TileAtlas::kSize;
    bool shaded = false;
    if (sunlit && look_fx::ambient_allowed(effects_)) {
        // Shimmer and shore foam, under the boats and the other sprites.
        // The layer carries the sun shadows too, so a frame pays for one
        // paint instead of two.
        shaded = draw_water(cr, visible_tiles(cr, source, 0));
    }
    if (sunlit && !shaded) {
        // Sun shadows: still, so Calm and Off keep them; none at night.
        draw_shadows(cr);
    }

    if (session_ != nullptr) {
        for (const auto &dot : session_->sprites()) {
            cairo_surface_t *image = sprite_frame(dot.type, dot.frame);
            if (image == nullptr) {
                continue;
            }
            cairo_save(cr);
            cairo_translate(cr, (dot.x + dot.x_offset) * sprite_to_source, (dot.y + dot.y_offset) * sprite_to_source);
            cairo_scale(cr, sprite_to_source, sprite_to_source);
            cairo_set_source_surface(cr, image, 0, 0);
            cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
            cairo_paint(cr);
            cairo_restore(cr);
        }
    }
    if (dusk) {
        tint_sprites(cr, sprite_to_source);
    }
    draw_city_events(cr, sprite_to_source, dusk);
    if (dusk) {
        draw_night(cr, source);
    }
    fx_animated_ = false;
    fx_drawn_step_ = fx_ms_ / kFxStepMs;
    if (!night_hold_ && !dusk && tile_size_ > kMapTileOverview && look_fx::ambient_allowed(effects_)) {
        draw_clouds(cr, visible_tiles(cr, source, 0));
    }
    if (!night_hold_ && tile_size_ > kMapTileOverview) {
        // Effects sit on the city, not on a data map, and not at the
        // overview zoom where a tile is a few pixels.
        const TileSpan span = visible_tiles(cr, source, 2);
        draw_fire(cr, source, span, dusk);
    }
    cairo_restore(cr);
    draw_rises(cr);
    draw_dust(cr);
    draw_footprint(cr);
    cairo_restore(cr);
}

void MapView::set_night(bool on)
{
    if (on == night_) {
        return;
    }
    night_ = on;
    light_dirty_ = true;
    light_built_ = {};
    night_stale_ = true;
    night_dirty_.clear();
}

namespace {

// The dusk tint for red, green and blue. Night
// multiplies the day art by it (the same as Cairo's MULTIPLY with an
// opaque colour), once per changed tile instead of every frame.
constexpr double kDuskR = 0.30;
constexpr double kDuskG = 0.36;
constexpr double kDuskB = 0.58;

} // namespace

void MapView::tint_tile(int x, int y)
{
    const int px = source_px();
    const unsigned char *src = cairo_image_surface_get_data(map_pixels_);
    const int sstride = cairo_image_surface_get_stride(map_pixels_);
    unsigned char *dst = cairo_image_surface_get_data(night_pixels_);
    const int dstride = cairo_image_surface_get_stride(night_pixels_);
    const std::uint32_t kr = static_cast<std::uint32_t>(kDuskR * 256.0 + 0.5);
    const std::uint32_t kg = static_cast<std::uint32_t>(kDuskG * 256.0 + 0.5);
    const std::uint32_t kb = static_cast<std::uint32_t>(kDuskB * 256.0 + 0.5);
    for (int row = 0; row < px; ++row) {
        const auto *in = reinterpret_cast<const std::uint32_t *>(src + (y * px + row) * sstride) + x * px;
        auto *out = reinterpret_cast<std::uint32_t *>(dst + (y * px + row) * dstride) + x * px;
        for (int col = 0; col < px; ++col) {
            const std::uint32_t p = in[col];
            const std::uint32_t r = (((p >> 16) & 0xFF) * kr) >> 8;
            const std::uint32_t g = (((p >> 8) & 0xFF) * kg) >> 8;
            const std::uint32_t b = ((p & 0xFF) * kb) >> 8;
            out[col] = (p & 0xFF000000u) | (r << 16) | (g << 8) | b;
        }
    }
}

void MapView::refresh_night_base()
{
    if (map_pixels_ == nullptr) {
        return;
    }
    const int width = cairo_image_surface_get_width(map_pixels_);
    const int height = cairo_image_surface_get_height(map_pixels_);
    if (night_pixels_ == nullptr || cairo_image_surface_get_width(night_pixels_) != width ||
        cairo_image_surface_get_height(night_pixels_) != height) {
        if (night_pixels_ != nullptr) {
            cairo_surface_destroy(night_pixels_);
        }
        night_pixels_ = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
        night_stale_ = true;
    }
    if (!night_stale_ && night_dirty_.empty()) {
        return;
    }
    cairo_surface_flush(map_pixels_);
    cairo_surface_flush(night_pixels_);
    const int world_w = CitySession::kWorldW;
    const int world_h = CitySession::kWorldH;
    if (night_stale_) {
        for (int y = 0; y < world_h; ++y) {
            for (int x = 0; x < world_w; ++x) {
                tint_tile(x, y);
            }
        }
        night_stale_ = false;
    } else {
        for (const int index : night_dirty_) {
            tint_tile(index % world_w, index / world_w);
        }
    }
    night_dirty_.clear();
    cairo_surface_mark_dirty(night_pixels_);
}

void MapView::draw_city_events(cairo_t *cr, double sprite_to_source, bool dusk)
{
    // Autonomous Zone barricades on the border, then the escaped lion. Both
    // are City 2's own art, drawn like the engine's sprites.
    if (session_ == nullptr) {
        return;
    }
    const CityEvents &events = session_->events_state();
    if (events.zoneCount > 0) {
        // The zone itself: a faint red wash, and a hatched band just inside
        // its border, under the barricades.
        cairo_save(cr);
        cairo_scale(cr, sprite_to_source, sprite_to_source);
        cairo_set_source_rgba(cr, 0.86, 0.22, 0.10, 0.13);
        for (int x = 0; x < CitySession::kWorldW; ++x) {
            for (int y = 0; y < CitySession::kWorldH; ++y) {
                if (events.zoneTile[x][y] != 0) {
                    cairo_rectangle(cr, x * 16.0, y * 16.0, 16.0, 16.0);
                }
            }
        }
        cairo_fill(cr);
        static const int kStep[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
        for (int x = 0; x < CitySession::kWorldW; ++x) {
            for (int y = 0; y < CitySession::kWorldH; ++y) {
                if (events.zoneTile[x][y] == 0) {
                    continue;
                }
                for (int side = 0; side < 4; ++side) {
                    const int nx = x + kStep[side][0];
                    const int ny = y + kStep[side][1];
                    if (nx < 0 || ny < 0 || nx >= CitySession::kWorldW || ny >= CitySession::kWorldH ||
                        events.zoneTile[nx][ny] != 0) {
                        continue;
                    }
                    // A 4px band along this edge, inside the tile.
                    const double bx = x * 16.0 + (side == 1 ? 12.0 : 0.0);
                    const double by = y * 16.0 + (side == 2 ? 12.0 : 0.0);
                    const double bw = (side == 0 || side == 2) ? 16.0 : 4.0;
                    const double bh = (side == 0 || side == 2) ? 4.0 : 16.0;
                    cairo_save(cr);
                    cairo_rectangle(cr, bx, by, bw, bh);
                    cairo_clip(cr);
                    cairo_set_source_rgba(cr, 0.80, 0.12, 0.06, 0.45);
                    cairo_set_line_width(cr, 1.5);
                    for (double d = -16.0; d < 20.0; d += 4.0) {
                        cairo_move_to(cr, bx + d, by + bh);
                        cairo_line_to(cr, bx + d + bh, by);
                        cairo_move_to(cr, bx + d, by + 16.0);
                        cairo_line_to(cr, bx + d + 16.0, by);
                    }
                    cairo_stroke(cr);
                    cairo_restore(cr);
                }
            }
        }
        cairo_restore(cr);
    }
    cairo_surface_t *fence = disaster_sheet("barricade");
    for (const city_events::Barricade &piece : session_->barricades()) {
        const double left = piece.x * 16.0;
        const double top = piece.y * 16.0;
        // Three kinds of barricade (sawhorse, concrete, cones), picked by
        // place so the line does not repeat but stays put frame to frame.
        // Cells 0-2 run along a horizontal edge (rows 3-12 of the cell),
        // 3-5 along a vertical one (columns 3-12), 6 is the corner block.
        // Each piece sits inside its tile, flush against the named edge.
        const int kind = ((piece.x * 7 + piece.y * 13 + piece.side * 5) & 0x7fff) % 3;
        double cell_x = left;
        double cell_y = top;
        int cell = kind;
        switch (piece.side) {
        case 0:
            cell_y = top - 3.0;
            break;
        case 2:
            cell_y = top + 16.0 - 13.0;
            break;
        case 1:
            cell = 3 + kind;
            cell_x = left + 16.0 - 13.0;
            break;
        case 3:
            cell = 3 + kind;
            cell_x = left - 3.0;
            break;
        default:
            // Corner posts: 4 NW, 5 NE, 6 SE, 7 SW.
            cell = 6;
            cell_x = (piece.side == 5 || piece.side == 6) ? left + 16.0 - 13.0 : left - 3.0;
            cell_y = (piece.side == 6 || piece.side == 7) ? top + 16.0 - 13.0 : top - 3.0;
            break;
        }
        paint_disaster_cell(cr, fence, cell, cell_x, cell_y, sprite_to_source, dusk, kDuskR, kDuskG, kDuskB);
    }
    const city_events::LionView cat = session_->lion();
    if (cat.active) {
        // 24px cells centred on the lion, lifted a little so its paws are
        // on the tile it walks through.
        const int cell = (cat.dir & 3) * 2 + (cat.frame & 1);
        paint_disaster_cell(cr, disaster_sheet("lion"), cell, cat.x - 12.0, cat.y - 14.0, sprite_to_source, dusk,
                            kDuskR, kDuskG, kDuskB);
    }
}

void MapView::tint_sprites(cairo_t *cr, double sprite_to_source)
{
    // Cars, trains, planes and boats are drawn over the tinted map, so they
    // take the same dusk tint, masked by their own shapes.
    if (session_ == nullptr) {
        return;
    }
    for (const auto &dot : session_->sprites()) {
        cairo_surface_t *image = sprite_frame(dot.type, dot.frame);
        if (image == nullptr) {
            continue;
        }
        cairo_save(cr);
        cairo_translate(cr, (dot.x + dot.x_offset) * sprite_to_source, (dot.y + dot.y_offset) * sprite_to_source);
        cairo_scale(cr, sprite_to_source, sprite_to_source);
        cairo_set_operator(cr, CAIRO_OPERATOR_MULTIPLY);
        cairo_set_source_rgb(cr, kDuskR, kDuskG, kDuskB);
        cairo_mask_surface(cr, image, 0, 0);
        cairo_restore(cr);
    }
}

void MapView::draw_night(cairo_t *cr, int source)
{
    const int world_w = CitySession::kWorldW;
    const int world_h = CitySession::kWorldH;
    const int lw = world_w * night_light::kPx;
    const int lh = world_h * night_light::kPx;
    if (light_pixels_ == nullptr) {
        light_pixels_ = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, lw, lh);
        light_dirty_ = true;
        light_built_ = {};
    }
    if (window_pixels_ == nullptr) {
        window_pixels_ = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, world_w * night_light::kTile,
                                                    world_h * night_light::kTile);
        lamp_pixels_ = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, world_w * night_light::kTile,
                                                  world_h * night_light::kTile);
        window_keys_.clear();
        light_dirty_ = true;
        light_built_ = {};
    }
    // Window masks come from the sheet now drawn: Classic, SVGA and Spooky
    // each put their windows in different places.
    const TileAtlas &atlas = tile_atlas();
    if (atlas.loaded() != window_masks_loaded_ || atlas.look_generation() != window_masks_generation_) {
        window_masks_loaded_ = atlas.loaded();
        window_masks_generation_ = atlas.look_generation();
        const int tiles = atlas.loaded() ? atlas.count() : 0;
        std::vector<std::uint32_t> sheet(static_cast<std::size_t>(tiles) * TileAtlas::kSize * TileAtlas::kSize, 0);
        for (int t = 0; t < tiles; ++t) {
            if (night_light::wants_windows(t)) {
                atlas.blit(t, reinterpret_cast<unsigned char *>(&sheet[static_cast<std::size_t>(t) * 256]),
                           TileAtlas::kSize * 4);
            }
        }
        night_light::window_masks(sheet.data(), tiles, window_masks_);
        window_keys_.clear();
        light_dirty_ = true;
        light_built_ = {};
    }
    const auto now = std::chrono::steady_clock::now();
    // A running city changes a few tiles every slice. Rebuilding the glow
    // twice a second is plenty for lights and keeps the frame cheap; the
    // window layer only repaints the tiles that changed.
    const int art_rows = world_h * night_light::kTile;
    bool rebuilt = false;
    if (light_dirty_ && static_cast<int>(cached_raw_.size()) == world_w * world_h &&
        (light_built_.time_since_epoch().count() == 0 || now - light_built_ >= std::chrono::milliseconds(500))) {
        const bool at_once = light_built_.time_since_epoch().count() == 0;
        // Every glass pixel of a powered Data Center lights.
        const int datacenter = special_datacenter_index();
        if (datacenter >= 0) {
            const SpecialBuilding &building = special_building(datacenter);
            night_light::stamp_glass(cached_raw_, world_w, world_h, building.center_tile, kSpecialBodyTile,
                                     building.width, building.anchor_x, datacenter_glass_sheet(), glass_cells_);
        }
        cairo_surface_flush(light_pixels_);
        night_light::build(cached_raw_, world_w, world_h, cairo_image_surface_get_data(light_pixels_),
                           cairo_image_surface_get_stride(light_pixels_), &window_masks_, &glass_cells_,
                           &micro_nuclear_lamp());
        cairo_surface_mark_dirty(light_pixels_);
        cairo_surface_flush(window_pixels_);
        night_light::build_windows(cached_raw_, world_w, world_h, window_masks_,
                                   cairo_image_surface_get_data(window_pixels_),
                                   cairo_image_surface_get_stride(window_pixels_), &window_keys_, &glass_cells_,
                                   &micro_nuclear_lamp());
        cairo_surface_mark_dirty(window_pixels_);
        // A new or loaded city lights in one go; a running city's small
        // changes are flattened a quarter of the map per frame, starting
        // next frame, so no frame carries the whole rebuild.
        flatten_row_ = at_once ? -2 : 0;
        rebuilt = !at_once;
        light_dirty_ = false;
        light_built_ = now;
    }
    if (flatten_row_ != -1 && !rebuilt) {
        const int from = flatten_row_ < 0 ? 0 : flatten_row_;
        const int to = flatten_row_ < 0 ? art_rows : std::min(art_rows, from + art_rows / 4);
        cairo_surface_flush(lamp_pixels_);
        night_light::flatten(cairo_image_surface_get_data(light_pixels_), cairo_image_surface_get_stride(light_pixels_),
                             cairo_image_surface_get_data(window_pixels_),
                             cairo_image_surface_get_stride(window_pixels_), world_w, world_h,
                             cairo_image_surface_get_data(lamp_pixels_), cairo_image_surface_get_stride(lamp_pixels_),
                             from, to);
        cairo_surface_mark_dirty(lamp_pixels_);
        flatten_row_ = to >= art_rows ? -1 : to;
    }
    cairo_save(cr);
    cairo_rectangle(cr, 0, 0, world_w * source, world_h * source);
    cairo_clip(cr);
    // The map under this is already the dusk-tinted copy; the lights add
    // on top.
    cairo_set_operator(cr, CAIRO_OPERATOR_ADD);
    // One light pixel per art pixel: crisp windows like the tiles, and
    // the glow already smoothed. The half-size overview averages them
    // instead of dropping every other one.
    const double art = static_cast<double>(source) / night_light::kTile;
    cairo_scale(cr, art, art);
    cairo_set_source_surface(cr, lamp_pixels_, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), source >= night_light::kTile ? CAIRO_FILTER_NEAREST
                                                                                : CAIRO_FILTER_GOOD);
    cairo_paint(cr);
    cairo_restore(cr);
}

// ---------------------------------------------------------------------------
// Look-and-feel effects drawn over the tiles. Nothing here touches the tile
// sheets; it is all drawn in code each frame from the map words.

void MapView::set_effects(int effects)
{
    effects_ = look_fx::clamp_effects(effects);
}

void MapView::advance_fx(int elapsed_ms, bool running, int speed)
{
    if (!running || !look_fx::motion_allowed(effects_) || elapsed_ms <= 0) {
        return;
    }
    const int step = std::min(elapsed_ms, 500);
    // Wraps well before int overflow; every phase below is periodic in it.
    fx_ms_ = (fx_ms_ + step) % (kFxStepMs * 360000);
    cloud_ms_ = std::fmod(cloud_ms_ + step * look_fx::cloud_speed_factor(speed), kFxStepMs * 360000.0);
}

bool MapView::fx_frame_due() const
{
    return fx_animated_ && look_fx::motion_allowed(effects_) && fx_ms_ / kFxStepMs != fx_drawn_step_;
}

bool MapView::classic_fx() const
{
    return tile_atlas().look() == 0;
}

MapView::TileSpan MapView::visible_tiles(cairo_t *cr, int source, int pad) const
{
    double x1 = 0;
    double y1 = 0;
    double x2 = 0;
    double y2 = 0;
    cairo_clip_extents(cr, &x1, &y1, &x2, &y2);
    TileSpan span;
    const int px = std::max(1, source);
    span.x0 = std::max(0, static_cast<int>(std::floor(x1 / px)) - pad);
    span.y0 = std::max(0, static_cast<int>(std::floor(y1 / px)) - pad);
    span.x1 = std::min(CitySession::kWorldW, static_cast<int>(std::ceil(x2 / px)) + pad);
    span.y1 = std::min(CitySession::kWorldH, static_cast<int>(std::ceil(y2 / px)) + pad);
    return span;
}

void MapView::draw_fire(cairo_t *cr, int source, const TileSpan &span, bool dusk)
{
    if (cached_raw_.empty()) {
        return;
    }
    struct Fire {
        int x;
        int y;
    };
    std::vector<Fire> fires;
    for (int y = span.y0; y < span.y1; ++y) {
        for (int x = span.x0; x < span.x1; ++x) {
            const int raw = cached_raw_[static_cast<std::size_t>(y * CitySession::kWorldW + x)];
            const int tile = raw < 0 ? -1 : (raw & LOMASK);
            if (tile >= FIREBASE && tile <= LASTFIRE) {
                fires.push_back(Fire{x, y});
            }
        }
    }
    if (fires.empty()) {
        return;
    }
    const bool moving = look_fx::motion_allowed(effects_);
    const bool classic = classic_fx();
    const double px = static_cast<double>(source);
    const int step = fx_ms_ / kFxStepMs;
    fx_animated_ = fx_animated_ || moving;
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    if (!dusk) {
        // Heat glow on the ground around a fire. Night lighting already
        // lights fires, so the day glow steps aside at dusk. The fire
        // tiles themselves are left out so the flames keep their art.
        cairo_save(cr);
        double cx1 = 0;
        double cy1 = 0;
        double cx2 = 0;
        double cy2 = 0;
        cairo_clip_extents(cr, &cx1, &cy1, &cx2, &cy2);
        cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
        cairo_rectangle(cr, cx1, cy1, cx2 - cx1, cy2 - cy1);
        for (const Fire &fire : fires) {
            cairo_rectangle(cr, fire.x * px, fire.y * px, px, px);
        }
        cairo_clip(cr);
        cairo_set_fill_rule(cr, CAIRO_FILL_RULE_WINDING);
        if (classic) {
            // Opaque orange specks thinning with distance: two dithered rings.
            for (const Fire &fire : fires) {
                const double cx = (fire.x + 0.5) * px;
                const double cy = (fire.y + 0.5) * px;
                const double flick = moving ? look_fx::hash01(static_cast<std::uint32_t>(fire.x * 977 + fire.y * 131 + step)) : 0.5;
                cairo_new_path(cr);
                cairo_arc(cr, cx, cy, px * (1.25 + 0.15 * flick), 0, 2 * look_fx::kPi);
                look_fx::fill_fx(cr, 1.0, 125.0 / 255.0, 0.0, 2.0 / 16.0, true);
                cairo_new_path(cr);
                cairo_arc(cr, cx, cy, px * 0.95, 0, 2 * look_fx::kPi);
                look_fx::fill_fx(cr, 1.0, 190.0 / 255.0, 0.0, 2.0 / 16.0, true);
            }
        } else {
            cairo_set_operator(cr, CAIRO_OPERATOR_ADD);
            for (const Fire &fire : fires) {
                const double cx = (fire.x + 0.5) * px;
                const double cy = (fire.y + 0.5) * px;
                const double flick =
                    moving ? 0.78 + 0.22 * look_fx::hash01(static_cast<std::uint32_t>(fire.x * 977 + fire.y * 131 + step))
                           : 0.9;
                const double radius = px * 2.4;
                cairo_pattern_t *glow = cairo_pattern_create_radial(cx, cy, px * 0.2, cx, cy, radius);
                cairo_pattern_add_color_stop_rgba(glow, 0.0, 1.0, 0.45, 0.08, 0.55 * flick);
                cairo_pattern_add_color_stop_rgba(glow, 0.4, 1.0, 0.36, 0.05, 0.26 * flick);
                cairo_pattern_add_color_stop_rgba(glow, 1.0, 1.0, 0.30, 0.0, 0.0);
                cairo_set_source(cr, glow);
                cairo_rectangle(cr, cx - radius, cy - radius, radius * 2, radius * 2);
                cairo_fill(cr);
                cairo_pattern_destroy(glow);
            }
        }
        cairo_restore(cr);
    }
    if (moving) {
        // Smoke plumes drifting up and to the right, and a few embers.
        // Each puff's phase is a hash of its tile, so nothing is stored.
        const int per_fire = fires.size() > 80 ? 2 : (fires.size() > 30 ? 3 : 5);
        for (const Fire &fire : fires) {
            const double cx = (fire.x + 0.5) * px;
            const double cy = (fire.y + 0.3) * px;
            for (int k = 0; k < per_fire; ++k) {
                const std::uint32_t seed = static_cast<std::uint32_t>(fire.x * 7919 + fire.y * 131 + k * 31337);
                const double sd = look_fx::hash01(seed);
                const double period = 1500.0 + 700.0 * look_fx::hash01(seed ^ 0x55u);
                const double age = std::fmod(fx_ms_ / period + look_fx::hash01(seed ^ 0xa7u), 1.0);
                const double drift = std::sin(age * 6.0 + sd * 9.0) * px * 0.18;
                const double x = cx + age * px * 1.2 * (0.6 + sd) + drift;
                const double y = cy - age * px * 3.6 * (0.7 + sd * 0.6);
                const double r = std::max(1.0, px * (0.15 + age * 0.45));
                const double alpha = 0.95 * std::pow(1.0 - age, 0.8) * std::min(1.0, age / 0.1);
                const double grey = (34.0 + 64.0 * age) / 255.0;
                // A hard-edged disc on whole pixels, like the tile art.
                cairo_new_path(cr);
                cairo_arc(cr, std::round(x), std::round(y), std::round(r), 0, 2 * look_fx::kPi);
                if (classic) {
                    const double vga = age < 0.45 ? 81.0 / 255.0 : 130.0 / 255.0;
                    look_fx::fill_fx(cr, vga, vga, vga, alpha, true);
                } else {
                    look_fx::fill_fx(cr, grey, grey, grey + 0.015, alpha, false);
                }
                if (age < 0.35 && sd > 0.55) {
                    const double ex = std::round(x + sd * px * 0.25);
                    const double ey = std::round(y + px * 0.15);
                    const double es = std::max(1.0, std::round(px / 16.0));
                    cairo_new_path(cr);
                    cairo_rectangle(cr, ex, ey, es, es);
                    cairo_set_source_rgb(cr, 1.0, classic ? 190.0 / 255.0 : 0.80, classic ? 0.0 : 0.24);
                    cairo_fill(cr);
                }
            }
        }
    }
    cairo_restore(cr);
}

namespace {

int luma(std::uint32_t rgb)
{
    return static_cast<int>(((rgb >> 16) & 255u) * 3 + ((rgb >> 8) & 255u) * 6 + (rgb & 255u));
}

// Buildings cast a shadow; open ground, empty zone lots and explosions do not.
bool casts_shadow(int tile)
{
    if (tile >= RESBASE && tile <= LASTZONE) {
        return !(tile <= RESBASE + 8 || (tile >= COMBASE && tile <= COMBASE + 8) ||
                 (tile >= INDBASE && tile <= INDBASE + 8));
    }
    return (tile >= RADAR0 && tile < TINYEXP) || (tile >= COALSMOKE1 && tile < VBRDG0);
}

bool is_water_tile(int raw)
{
    const int tile = raw < 0 ? -1 : (raw & LOMASK);
    return tile >= WATER_LOW && tile <= WATER_HIGH;
}

// One cycle of a sine in 256 steps, -1..1.
const float *wave_table()
{
    static float table[256];
    static bool built = false;
    if (!built) {
        for (int i = 0; i < 256; ++i) {
            table[i] = static_cast<float>(std::sin(i * 2.0 * look_fx::kPi / 256.0));
        }
        built = true;
    }
    return table;
}

} // namespace

void MapView::learn_fx_palette()
{
    TileAtlas &atlas = tile_atlas();
    if (fx_palette_generation_ == atlas.look_generation() && !water_rgb_.empty()) {
        return;
    }
    fx_palette_generation_ = atlas.look_generation();
    water_rgb_.clear();
    water_lift_.clear();
    if (!atlas.loaded()) {
        return;
    }
    // Every colour of the open-water and channel tiles is a water colour.
    std::uint32_t pixels[TileAtlas::kSize * TileAtlas::kSize];
    for (int tile : {static_cast<int>(RIVER), static_cast<int>(CHANNEL)}) {
        atlas.blit(tile, reinterpret_cast<unsigned char *>(pixels), TileAtlas::kSize * 4);
        for (std::uint32_t pixel : pixels) {
            const std::uint32_t rgb = pixel & 0xffffffu;
            if (std::find(water_rgb_.begin(), water_rgb_.end(), rgb) == water_rgb_.end() && water_rgb_.size() < 31) {
                water_rgb_.push_back(rgb);
            }
        }
    }
    std::sort(water_rgb_.begin(), water_rgb_.end(), [](std::uint32_t a, std::uint32_t b) { return luma(a) < luma(b); });
    const std::size_t count = water_rgb_.size();
    const std::size_t lift = std::max<std::size_t>(1, count / 4);
    for (std::size_t i = 0; i < count; ++i) {
        water_lift_.push_back(water_rgb_[std::min(count - 1, i + lift)]);
    }
    // Foam: a pale blue-white, on the VGA palette for Classic.
    foam_rgb_ = atlas.look() == 0 ? (snap_vga_rgb(190, 225, 245) & 0xffffffu) : 0xc4e2f4u;
}

void MapView::refresh_pixel_classes()
{
    if (map_pixels_ == nullptr || source_px() != TileAtlas::kSize || cached_raw_.empty()) {
        class_dirty_.clear();
        return;
    }
    learn_fx_palette();
    const int world_w = CitySession::kWorldW;
    const int world_h = CitySession::kWorldH;
    const int kTile = TileAtlas::kSize;
    const int art_w = world_w * kTile;
    const std::size_t want = static_cast<std::size_t>(art_w) * static_cast<std::size_t>(world_h * kTile);
    if (pix_class_.size() != want) {
        pix_class_.assign(want, 0);
        class_all_ = true;
    }
    std::vector<int> tiles;
    if (class_all_) {
        tiles.resize(static_cast<std::size_t>(world_w * world_h));
        for (std::size_t i = 0; i < tiles.size(); ++i) {
            tiles[i] = static_cast<int>(i);
        }
    } else {
        tiles.swap(class_dirty_);
    }
    class_dirty_.clear();
    class_all_ = false;
    if (tiles.empty()) {
        return;
    }
    fx_cache_step_ = -1;
    cairo_surface_flush(map_pixels_);
    const unsigned char *data = cairo_image_surface_get_data(map_pixels_);
    const int stride = cairo_image_surface_get_stride(map_pixels_);
    auto pixel = [&](int x, int y) {
        return *reinterpret_cast<const std::uint32_t *>(data + y * stride + x * 4) & 0xffffffu;
    };
    for (int index : tiles) {
        const int tx = index % world_w;
        const int ty = index / world_w;
        const bool water = is_water_tile(cached_raw_[static_cast<std::size_t>(index)]);
        const int raw_tile = cached_raw_[static_cast<std::size_t>(index)] < 0
                                 ? -1
                                 : (cached_raw_[static_cast<std::size_t>(index)] & LOMASK);
        const bool caster = special_cell_[static_cast<std::size_t>(index)] != 0 || casts_shadow(raw_tile);
        for (int r = 0; r < kTile; ++r) {
            const int y = ty * kTile + r;
            std::uint8_t *row = pix_class_.data() + static_cast<std::size_t>(y) * art_w;
            for (int c = 0; c < kTile; ++c) {
                const int x = tx * kTile + c;
                // A building casts from its whole cell: the tile sheets share
                // too many colours between roofs and ground to cut a
                // building out pixel by pixel, and a block shadow reads
                // cleanly at every zoom.
                std::uint8_t value = caster ? 0x80u : 0u;
                const std::uint32_t rgb = pixel(x, y);
                if (water) {
                    for (std::size_t i = 0; i < water_rgb_.size(); ++i) {
                        if (water_rgb_[i] == rgb) {
                            value = static_cast<std::uint8_t>(value | (i + 1));
                            break;
                        }
                    }
                }
                row[x] = value;
            }
        }
    }
    // Shore: water with a non-water pixel beside it. A changed tile can
    // change its neighbours' shore, so redo a one-tile ring too.
    const int art_h = world_h * kTile;
    auto is_water = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= art_w || y >= art_h) {
            return true;
        }
        return (pix_class_[static_cast<std::size_t>(y) * art_w + x] & 0x1fu) != 0;
    };
    std::vector<std::uint8_t> done(static_cast<std::size_t>(world_w * world_h), 0);
    for (int index : tiles) {
        const int tx0 = index % world_w;
        const int ty0 = index / world_w;
        for (int ty = std::max(0, ty0 - 1); ty <= std::min(world_h - 1, ty0 + 1); ++ty) {
            for (int tx = std::max(0, tx0 - 1); tx <= std::min(world_w - 1, tx0 + 1); ++tx) {
                std::uint8_t &mark = done[static_cast<std::size_t>(ty * world_w + tx)];
                if (mark != 0) {
                    continue;
                }
                mark = 1;
                for (int y = ty * kTile; y < (ty + 1) * kTile; ++y) {
                    std::uint8_t *row = pix_class_.data() + static_cast<std::size_t>(y) * art_w;
                    for (int x = tx * kTile; x < (tx + 1) * kTile; ++x) {
                        row[x] = static_cast<std::uint8_t>(row[x] & ~0x40u);
                        if ((row[x] & 0x1fu) != 0 &&
                            (!is_water(x - 1, y) || !is_water(x + 1, y) || !is_water(x, y - 1) || !is_water(x, y + 1))) {
                            row[x] = static_cast<std::uint8_t>(row[x] | 0x40u);
                        }
                    }
                }
            }
        }
    }
    refresh_shade(tiles);
}

bool MapView::draw_water(cairo_t *cr, const TileSpan &span)
{
    if (pix_class_.empty() || water_rgb_.empty() || span.x1 <= span.x0 || span.y1 <= span.y0) {
        return false;
    }
    const int kTile = TileAtlas::kSize;
    const int art_w = CitySession::kWorldW * kTile;
    const int w = (span.x1 - span.x0) * kTile;
    const int h = (span.y1 - span.y0) * kTile;
    const int step = fx_ms_ / kFxStepMs;
    if (fx_pixels_ == nullptr || cairo_image_surface_get_width(fx_pixels_) < w ||
        cairo_image_surface_get_height(fx_pixels_) < h) {
        if (fx_pixels_ != nullptr) {
            cairo_surface_destroy(fx_pixels_);
        }
        fx_pixels_ = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w + 4 * kTile, h + 4 * kTile);
        fx_cache_step_ = -1;
    }
    bool any = false;
    const bool fresh = fx_cache_step_ == step && fx_cache_span_.x0 == span.x0 && fx_cache_span_.y0 == span.y0 &&
                       fx_cache_span_.x1 == span.x1 && fx_cache_span_.y1 == span.y1;
    if (!fresh) {
        cairo_surface_flush(fx_pixels_);
        unsigned char *out = cairo_image_surface_get_data(fx_pixels_);
        const int ostride = cairo_image_surface_get_stride(fx_pixels_);
        // Start from the sun shadows for these tiles (or clear), then lay
        // the water step over them.
        const bool with_shade = shade_pixels_ != nullptr && cairo_image_surface_get_width(shade_pixels_) == art_w;
        const unsigned char *shade = with_shade ? cairo_image_surface_get_data(shade_pixels_) : nullptr;
        const int sstride = with_shade ? cairo_image_surface_get_stride(shade_pixels_) : 0;
        for (int y = 0; y < h; ++y) {
            if (with_shade) {
                std::memcpy(out + y * ostride,
                            shade + static_cast<std::size_t>(y + span.y0 * kTile) * sstride + span.x0 * kTile * 4,
                            static_cast<std::size_t>(w) * 4);
            } else {
                std::memset(out + y * ostride, 0, static_cast<std::size_t>(w) * 4);
            }
        }
        const float *table = wave_table();
        const int crest = static_cast<int>(water_rgb_.size()) - 1;
        // Two slow waves crossing; a band where they add up gets the next
        // lighter water colour, the very top a rare glint. Shore pixels
        // get a dotted foam line that crawls along the coast.
        const int phase_a = (step * 18) & 255;
        const int phase_b = (step * 11) & 255;
        for (int ty = span.y0; ty < span.y1; ++ty) {
            for (int tx = span.x0; tx < span.x1; ++tx) {
                if (!is_water_tile(cached_raw_[static_cast<std::size_t>(ty * CitySession::kWorldW + tx)])) {
                    continue;
                }
                any = true;
                for (int y = ty * kTile; y < (ty + 1) * kTile; ++y) {
                    const std::uint8_t *row = pix_class_.data() + static_cast<std::size_t>(y) * art_w;
                    auto *dest = reinterpret_cast<std::uint32_t *>(out + (y - span.y0 * kTile) * ostride);
                    for (int x = tx * kTile; x < (tx + 1) * kTile; ++x) {
                        const std::uint8_t cls = row[x];
                        const int idx = (cls & 0x1f) - 1;
                        if (idx < 0) {
                            continue;
                        }
                        const int ox = x - span.x0 * kTile;
                        if ((cls & 0x40u) != 0 && ((x + y + step) % 5) == 0) {
                            dest[ox] = 0xff000000u | foam_rgb_;
                            continue;
                        }
                        const float wave = 0.6f * table[(x * 9 + y * 14 - phase_a) & 255] +
                                           0.4f * table[(x * 3 - y * 8 + phase_b) & 255];
                        if (wave > 0.86f && look_fx::hash01(static_cast<std::uint32_t>(x * 73856093 ^ y * 19349663)) > 0.82) {
                            dest[ox] = 0xff000000u | water_rgb_[static_cast<std::size_t>(crest)];
                        } else if (wave > 0.55f) {
                            dest[ox] = 0xff000000u | water_lift_[static_cast<std::size_t>(idx)];
                        }
                    }
                }
            }
        }
        cairo_surface_mark_dirty(fx_pixels_);
        fx_cache_step_ = step;
        fx_cache_span_ = span;
        fx_water_seen_ = any;
    }
    if (fx_water_seen_) {
        fx_animated_ = true;
    }
    cairo_save(cr);
    cairo_rectangle(cr, span.x0 * kTile, span.y0 * kTile, w, h);
    cairo_clip(cr);
    cairo_set_source_surface(cr, fx_pixels_, span.x0 * kTile, span.y0 * kTile);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
    cairo_paint(cr);
    cairo_restore(cr);
    return true;
}

void MapView::refresh_shade(const std::vector<int> &tiles)
{
    const int world_w = CitySession::kWorldW;
    const int world_h = CitySession::kWorldH;
    const int kTile = TileAtlas::kSize;
    const int art_w = world_w * kTile;
    const int art_h = world_h * kTile;
    if (pix_class_.size() != static_cast<std::size_t>(art_w) * static_cast<std::size_t>(art_h)) {
        return;
    }
    bool all = false;
    if (shade_pixels_ == nullptr || cairo_image_surface_get_width(shade_pixels_) != art_w ||
        cairo_image_surface_get_height(shade_pixels_) != art_h) {
        if (shade_pixels_ != nullptr) {
            cairo_surface_destroy(shade_pixels_);
        }
        shade_pixels_ = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, art_w, art_h);
        all = true;
    }
    const bool classic = classic_fx();
    cairo_surface_flush(shade_pixels_);
    unsigned char *data = cairo_image_surface_get_data(shade_pixels_);
    const int stride = cairo_image_surface_get_stride(shade_pixels_);
    auto solid = [&](int x, int y) {
        return x >= 0 && y >= 0 && (pix_class_[static_cast<std::size_t>(y) * art_w + x] & 0x80u) != 0;
    };
    // A changed tile throws shadow into the tiles right of and below it.
    std::vector<std::uint8_t> todo(static_cast<std::size_t>(world_w * world_h), all ? 1 : 0);
    if (!all) {
        for (int index : tiles) {
            const int tx = index % world_w;
            const int ty = index / world_w;
            for (int dy = 0; dy <= 1; ++dy) {
                for (int dx = 0; dx <= 1; ++dx) {
                    if (tx + dx < world_w && ty + dy < world_h) {
                        todo[static_cast<std::size_t>((ty + dy) * world_w + tx + dx)] = 1;
                    }
                }
            }
        }
    }
    // A soft shadow cast down and to the right: a 4px offset with a little
    // spread around it, and a tight contact shade at 2px. Only open ground
    // receives it. Classic gets an ordered dither of black instead of alpha.
    struct Tap {
        int dx;
        int dy;
        float weight;
    };
    static const Tap kTaps[] = {{4, 4, 0.36f}, {3, 4, 0.07f}, {4, 3, 0.07f}, {5, 4, 0.07f}, {4, 5, 0.07f},
                                {3, 3, 0.06f}, {5, 5, 0.06f}, {2, 2, 0.18f}};
    for (int ty = 0; ty < world_h; ++ty) {
        for (int tx = 0; tx < world_w; ++tx) {
            if (todo[static_cast<std::size_t>(ty * world_w + tx)] == 0) {
                continue;
            }
            for (int y = ty * kTile; y < (ty + 1) * kTile; ++y) {
                auto *out = reinterpret_cast<std::uint32_t *>(data + y * stride);
                for (int x = tx * kTile; x < (tx + 1) * kTile; ++x) {
                    float a = 0.0f;
                    if (!solid(x, y)) {
                        for (const Tap &tap : kTaps) {
                            if (solid(x - tap.dx, y - tap.dy)) {
                                a += tap.weight;
                            }
                        }
                    }
                    a = std::min(a, 0.66f);
                    if (a <= 0.0f) {
                        out[x] = 0;
                    } else if (classic) {
                        out[x] = look_fx::dither_on(x, y, a * 0.6) ? 0xff000000u : 0u;
                    } else {
                        const std::uint32_t alpha = static_cast<std::uint32_t>(a * 255.0f + 0.5f);
                        // Premultiplied cool shade (12, 14, 34).
                        out[x] = (alpha << 24) | ((12u * alpha / 255u) << 16) | ((14u * alpha / 255u) << 8) |
                                 (34u * alpha / 255u);
                    }
                }
            }
        }
    }
    cairo_surface_mark_dirty(shade_pixels_);
    // The water layer starts from these shadows.
    fx_cache_step_ = -1;
}

void MapView::draw_shadows(cairo_t *cr)
{
    if (shade_pixels_ == nullptr) {
        return;
    }
    cairo_save(cr);
    cairo_set_source_surface(cr, shade_pixels_, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
    cairo_paint(cr);
    cairo_restore(cr);
}

namespace {

constexpr int kCloudTex = 256;
// One cloud texel covers this many 16px-art pixels: the pattern repeats
// every 96 tiles, wider than the map is tall.
constexpr double kCloudTexel = 6.0;

// Tileable value noise: a few octaves on lattices that divide the texture.
void build_cloud_cover(std::vector<float> &cover)
{
    cover.assign(static_cast<std::size_t>(kCloudTex * kCloudTex), 0.0f);
    auto lattice = [](int cells, int ix, int iy, int salt) {
        ix = ((ix % cells) + cells) % cells;
        iy = ((iy % cells) + cells) % cells;
        return static_cast<float>(look_fx::hash01(static_cast<std::uint32_t>(ix * 92821 + iy * 68917 + salt * 7919)));
    };
    const int octaves[] = {4, 8, 16};
    const float weights[] = {0.6f, 0.28f, 0.12f};
    for (int o = 0; o < 3; ++o) {
        const int cells = octaves[o];
        const float size = static_cast<float>(kCloudTex) / static_cast<float>(cells);
        for (int y = 0; y < kCloudTex; ++y) {
            for (int x = 0; x < kCloudTex; ++x) {
                const float fx = x / size;
                const float fy = y / size;
                const int ix = static_cast<int>(fx);
                const int iy = static_cast<int>(fy);
                float tx = fx - ix;
                float ty = fy - iy;
                tx = tx * tx * (3.0f - 2.0f * tx);
                ty = ty * ty * (3.0f - 2.0f * ty);
                const float a = lattice(cells, ix, iy, o);
                const float b = lattice(cells, ix + 1, iy, o);
                const float c = lattice(cells, ix, iy + 1, o);
                const float d = lattice(cells, ix + 1, iy + 1, o);
                const float v = (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * ty;
                cover[static_cast<std::size_t>(y * kCloudTex + x)] += v * weights[o];
            }
        }
    }
    // Keep roughly a third of the sky cloudy, with soft edges.
    for (float &v : cover) {
        const float t = std::max(0.0f, std::min(1.0f, (v - 0.50f) / 0.10f));
        v = t * t * (3.0f - 2.0f * t);
    }
}

} // namespace

void MapView::draw_clouds(cairo_t *cr, const TileSpan &span)
{
    if (span.x1 <= span.x0 || span.y1 <= span.y0) {
        return;
    }
    fx_animated_ = true;
    const int source = source_px();
    const bool classic = classic_fx();
    const int key = source * 2 + (classic ? 1 : 0);
    // The cover repeats every kCloudTex texels; one repeat at art resolution
    // is built once (per zoom source and look) and then only slid, so a
    // frame pays for one plain repeating paint.
    const int period = static_cast<int>(kCloudTex * kCloudTexel) * source / TileAtlas::kSize;
    if (cloud_pixels_ == nullptr || cloud_cache_key_ != key) {
        if (cloud_cover_.empty()) {
            build_cloud_cover(cloud_cover_);
        }
        if (cloud_pixels_ != nullptr) {
            cairo_surface_destroy(cloud_pixels_);
        }
        cloud_pixels_ = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, period, period);
        cloud_cache_key_ = key;
        unsigned char *data = cairo_image_surface_get_data(cloud_pixels_);
        const int stride = cairo_image_surface_get_stride(cloud_pixels_);
        const double texel = static_cast<double>(period) / kCloudTex;
        auto cover_at = [&](double u, double v) {
            const double fu = std::floor(u);
            const double fv = std::floor(v);
            const int iu = static_cast<int>(fu) & (kCloudTex - 1);
            const int iv = static_cast<int>(fv) & (kCloudTex - 1);
            const int ju = (iu + 1) & (kCloudTex - 1);
            const int jv = (iv + 1) & (kCloudTex - 1);
            const float tu = static_cast<float>(u - fu);
            const float tv = static_cast<float>(v - fv);
            const float *row0 = cloud_cover_.data() + iv * kCloudTex;
            const float *row1 = cloud_cover_.data() + jv * kCloudTex;
            const float top = row0[iu] + (row0[ju] - row0[iu]) * tu;
            const float bot = row1[iu] + (row1[ju] - row1[iu]) * tu;
            return top + (bot - top) * tv;
        };
        for (int y = 0; y < period; ++y) {
            auto *row = reinterpret_cast<std::uint32_t *>(data + y * stride);
            const double v = (y + 0.5) / texel - 0.5;
            for (int x = 0; x < period; ++x) {
                const float c = cover_at((x + 0.5) / texel - 0.5, v);
                if (classic) {
                    // A sparse black dither inside the cloud outline. The
                    // repeat is a multiple of 4, so the dither tiles too.
                    row[x] = c > 0.5f && look_fx::dither_on(x, y, 0.25) ? 0xff000000u : 0u;
                } else {
                    // A cool shade, deepest in the middle of a cloud.
                    const std::uint32_t a = static_cast<std::uint32_t>(c * 0.32f * 255.0f + 0.5f);
                    row[x] = a == 0 ? 0u
                                    : (a << 24) | ((15u * a / 255u) << 16) | ((20u * a / 255u) << 8) | (51u * a / 255u);
                }
            }
        }
        cairo_surface_mark_dirty(cloud_pixels_);
    }
    // Drift east-south-east at about 9 art pixels a second at Medium (scaled
    // by the city speed on the cloud clock), in whole pixels (whole dither
    // cells on Classic, so it never crawls). The offset still moves only on
    // ambient steps, so the frame budget is unchanged.
    const double drift = std::floor(cloud_ms_ / kFxStepMs) * kFxStepMs * 0.009 * source / TileAtlas::kSize;
    const int snap = classic ? 4 : 1;
    const int ox = (static_cast<int>(drift) / snap * snap) % period;
    const int oy = (static_cast<int>(drift * 0.35) / snap * snap) % period;
    cairo_save(cr);
    cairo_rectangle(cr, span.x0 * source, span.y0 * source, (span.x1 - span.x0) * source, (span.y1 - span.y0) * source);
    cairo_clip(cr);
    cairo_set_source_surface(cr, cloud_pixels_, ox, oy);
    cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_REPEAT);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
    cairo_paint(cr);
    cairo_restore(cr);
}

namespace {

constexpr int kPulseMs = 160;
// Brackets 0-90 ms, rise 90-360 ms, settle and glint 360-450 ms.
constexpr int kRiseMs = 450;

// First tile and footprint of the building a tool lays, for the ghost.
// Zones and plants are laid row by row from their base tile.
bool ghost_tiles(int tool, int &base, int &size)
{
    switch (tool) {
    case TOOL_RESIDENTIAL:
        base = RESBASE;
        size = 3;
        return true;
    case TOOL_COMMERCIAL:
        base = COMBASE;
        size = 3;
        return true;
    case TOOL_INDUSTRIAL:
        base = INDBASE;
        size = 3;
        return true;
    case TOOL_FIRESTATION:
        base = FIRESTBASE;
        size = 3;
        return true;
    case TOOL_POLICESTATION:
        base = POLICESTBASE;
        size = 3;
        return true;
    case TOOL_STADIUM:
        base = STADIUMBASE;
        size = 4;
        return true;
    case TOOL_SEAPORT:
        base = PORTBASE;
        size = 4;
        return true;
    case TOOL_COALPOWER:
        base = COALBASE;
        size = 4;
        return true;
    case TOOL_NUCLEARPOWER:
        base = NUCLEARBASE;
        size = 4;
        return true;
    case TOOL_AIRPORT:
        base = AIRPORTBASE;
        size = 6;
        return true;
    default:
        return false;
    }
}

} // namespace

void MapView::advance_ui(int elapsed_ms)
{
    for (Rise &rise : rises_) {
        rise.ms += std::max(0, elapsed_ms);
    }
    const std::size_t before = rises_.size();
    rises_.erase(std::remove_if(rises_.begin(), rises_.end(), [](const Rise &rise) { return rise.ms >= kRiseMs; }),
                 rises_.end());
    if (rises_.size() != before) {
        ui_tail_ = true;
    }
    if (pulse_ms_ >= 0) {
        pulse_ms_ += std::max(0, elapsed_ms);
        if (pulse_ms_ >= kPulseMs) {
            pulse_ms_ = -1;
            ui_tail_ = true;
        }
    }
}

bool MapView::ui_animating() const
{
    return pulse_ms_ >= 0 || !rises_.empty() || ui_tail_;
}

void MapView::draw_ghost(cairo_t *cr, double x, double y, double w, double h) const
{
    const int mark = cue_.mark;
    const bool blocked = mark >= 3;
    const bool classic = classic_fx();
    TileAtlas &atlas = tile_atlas();
    int base = 0;
    int size = 0;
    const bool tiles = atlas.loaded() && ghost_tiles(engine_tool_, base, size) && size == cue_.width &&
                       size == cue_.height && base + size * size <= atlas.count();
    cairo_save(cr);
    if (tiles) {
        if (ghost_pixels_ == nullptr || ghost_tool_ != engine_tool_ || ghost_generation_ != atlas.look_generation()) {
            if (ghost_pixels_ != nullptr) {
                cairo_surface_destroy(ghost_pixels_);
            }
            const int k = TileAtlas::kSize;
            ghost_pixels_ = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size * k, size * k);
            cairo_surface_flush(ghost_pixels_);
            unsigned char *data = cairo_image_surface_get_data(ghost_pixels_);
            const int stride = cairo_image_surface_get_stride(ghost_pixels_);
            for (int row = 0; row < size; ++row) {
                for (int col = 0; col < size; ++col) {
                    atlas.blit(base + row * size + col, data + row * k * stride + col * k * 4, stride);
                }
            }
            cairo_surface_mark_dirty(ghost_pixels_);
            ghost_tool_ = engine_tool_;
            ghost_generation_ = atlas.look_generation();
        }
        // The building it will lay, see-through: blended in SVGA, a 50%
        // dither in Classic. A blocked spot shows it washed red.
        cairo_save(cr);
        cairo_rectangle(cr, x, y, w, h);
        cairo_clip(cr);
        {
            cairo_matrix_t matrix;
            cairo_matrix_init_scale(&matrix, cairo_image_surface_get_width(ghost_pixels_) / w,
                                    cairo_image_surface_get_height(ghost_pixels_) / h);
            cairo_matrix_translate(&matrix, -x, -y);
            cairo_pattern_t *art = cairo_pattern_create_for_surface(ghost_pixels_);
            cairo_pattern_set_filter(art, CAIRO_FILTER_NEAREST);
            cairo_pattern_set_matrix(art, &matrix);
            cairo_set_source(cr, art);
            if (classic) {
                look_fx::dither_paint(cr, blocked ? 6.0 / 16.0 : 8.0 / 16.0);
            } else {
                cairo_paint_with_alpha(cr, blocked ? 0.45 : 0.6);
            }
            cairo_pattern_destroy(art);
        }
        if (blocked) {
            cairo_rectangle(cr, x, y, w, h);
            look_fx::fill_fx(cr, 0.85, 0.12, 0.08, classic ? 4.0 / 16.0 : 0.30, classic);
        }
        cairo_restore(cr);
    }
    // Glow around the footprint: green ready, amber caution, red blocked.
    double r = 110 / 255.0;
    double g = 1.0;
    double b = 140 / 255.0;
    if (mark == 2) {
        r = 1.0;
        g = 220 / 255.0;
        b = 90 / 255.0;
    } else if (blocked) {
        r = 1.0;
        g = 70 / 255.0;
        b = 60 / 255.0;
    }
    cairo_set_line_width(cr, 1.0);
    if (classic) {
        cairo_rectangle(cr, x - 2.0, y - 2.0, w + 4.0, h + 4.0);
        cairo_rectangle(cr, x, y, w, h);
        cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
        look_fx::fill_fx(cr, r, g, b, 0.5, true);
        cairo_set_fill_rule(cr, CAIRO_FILL_RULE_WINDING);
    } else {
        const double alphas[3] = {50 / 255.0, 90 / 255.0, 140 / 255.0};
        for (int i = 3; i >= 1; --i) {
            cairo_rectangle(cr, x - i + 0.5, y - i + 0.5, w + 2 * i - 1, h + 2 * i - 1);
            cairo_set_source_rgba(cr, r, g, b, alphas[3 - i]);
            cairo_stroke(cr);
        }
    }
    cairo_rectangle(cr, x + 0.5, y + 0.5, w - 1, h - 1);
    cairo_set_source_rgb(cr, r, g, b);
    cairo_stroke(cr);
    const double tile = static_cast<double>(std::max(tile_size_, 1));
    if (!classic && tile >= 16) {
        // Faint cell lines inside a multi-tile footprint.
        for (double gx = x + tile; gx < x + w - 1; gx += tile) {
            cairo_move_to(cr, gx + 0.5, y + 2);
            cairo_line_to(cr, gx + 0.5, y + h - 2);
        }
        for (double gy = y + tile; gy < y + h - 1; gy += tile) {
            cairo_move_to(cr, x + 2, gy + 0.5);
            cairo_line_to(cr, x + w - 2, gy + 0.5);
        }
        cairo_set_source_rgba(cr, r, g, b, 70 / 255.0);
        cairo_stroke(cr);
    }
    if (blocked) {
        // Hatch across a blocked footprint.
        cairo_save(cr);
        cairo_rectangle(cr, x, y, w, h);
        cairo_clip(cr);
        const double gap = classic ? 8.0 : 6.0;
        for (double k = -h; k < w; k += gap) {
            cairo_move_to(cr, x + k, y + h);
            cairo_line_to(cr, x + k + h, y);
        }
        if (classic) {
            cairo_set_source_rgb(cr, 1.0, 85 / 255.0, 85 / 255.0);
        } else {
            cairo_set_source_rgba(cr, 1.0, 60 / 255.0, 50 / 255.0, 90 / 255.0);
        }
        cairo_stroke(cr);
        cairo_restore(cr);
    }
    if (pulse_ms_ >= 0 && !blocked) {
        // Snap pulse: a white ring that grows and fades.
        const double p = std::min(1.0, pulse_ms_ / static_cast<double>(kPulseMs));
        const double e = std::round(2.0 + 8.0 * look_fx::ease_out_cubic(p));
        cairo_set_line_width(cr, 2.0);
        cairo_rectangle(cr, x - e, y - e, w + 2 * e, h + 2 * e);
        if (classic) {
            if (p < 0.6) {
                cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
                cairo_stroke(cr);
            }
        } else {
            cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.86 * (1.0 - p));
            cairo_stroke(cr);
        }
        cairo_new_path(cr);
    }
    cairo_new_path(cr);
    cairo_restore(cr);
}

void MapView::start_rise(int left, int top, int width, int height, bool growth)
{
    if (!look_fx::motion_allowed(effects_) || width < 1 || height < 1) {
        return;
    }
    for (Rise &rise : rises_) {
        if (rise.left == left && rise.top == top) {
            rise = Rise{left, top, width, height, 0, growth};
            return;
        }
    }
    // A handful at once; a burst of growth should not become a light show.
    const auto grown = std::count_if(rises_.begin(), rises_.end(), [](const Rise &rise) { return rise.growth; });
    if (rises_.size() >= 6 || (growth && grown >= 2)) {
        return;
    }
    rises_.push_back(Rise{left, top, width, height, 0, growth});
}

void MapView::draw_rises(cairo_t *cr) const
{
    if (rises_.empty() || map_pixels_ == nullptr) {
        return;
    }
    TileAtlas &atlas = tile_atlas();
    if (atlas.loaded() && (dirt_pixels_ == nullptr || dirt_generation_ != atlas.look_generation())) {
        if (dirt_pixels_ != nullptr) {
            cairo_surface_destroy(dirt_pixels_);
        }
        dirt_pixels_ = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, TileAtlas::kSize, TileAtlas::kSize);
        cairo_surface_flush(dirt_pixels_);
        atlas.blit(DIRT, cairo_image_surface_get_data(dirt_pixels_), cairo_image_surface_get_stride(dirt_pixels_));
        cairo_surface_mark_dirty(dirt_pixels_);
        dirt_generation_ = atlas.look_generation();
    }
    const bool classic = classic_fx();
    const double tile = static_cast<double>(std::max(tile_size_, 1));
    const double art = tile / TileAtlas::kSize;
    const double unit = std::max(1.0, std::round(art));
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    for (const Rise &rise : rises_) {
        const double x = rise.left * tile;
        const double y = rise.top * tile;
        const double w = rise.width * tile;
        const double h = rise.height * tile;
        const int ms = rise.ms;
        if (ms < 360 && dirt_pixels_ != nullptr) {
            // Not topped out yet: bare lot over the part still to rise.
            const double u = ms < 90 ? 0.0 : look_fx::ease_out_cubic((ms - 90) / 270.0);
            const double cover = std::round(h * (1.0 - u));
            if (cover > 0) {
                cairo_save(cr);
                cairo_rectangle(cr, x, y, w, cover);
                cairo_clip(cr);
                cairo_pattern_t *dirt = cairo_pattern_create_for_surface(dirt_pixels_);
                cairo_pattern_set_extend(dirt, CAIRO_EXTEND_REPEAT);
                cairo_pattern_set_filter(dirt, CAIRO_FILTER_NEAREST);
                cairo_matrix_t matrix;
                cairo_matrix_init_scale(&matrix, 1.0 / art, 1.0 / art);
                cairo_matrix_translate(&matrix, -x, -y);
                cairo_pattern_set_matrix(dirt, &matrix);
                cairo_set_source(cr, dirt);
                cairo_paint(cr);
                cairo_pattern_destroy(dirt);
                if (ms >= 90) {
                    // Scaffold over the rising edge: posts, planks, braces.
                    const double top_edge = y + cover;
                    const double band = std::min(cover, std::max(6.0 * unit, h * 0.35));
                    const double sy = top_edge - band;
                    if (classic) {
                        cairo_set_source_rgb(cr, 170 / 255.0, 85 / 255.0, 0.0);
                    } else {
                        cairo_set_source_rgb(cr, 150 / 255.0, 105 / 255.0, 60 / 255.0);
                    }
                    cairo_set_line_width(cr, unit);
                    for (double px = x + 2 * unit; px < x + w; px += 8 * unit) {
                        cairo_move_to(cr, px + unit / 2, sy);
                        cairo_line_to(cr, px + unit / 2, top_edge);
                    }
                    for (double py = top_edge - unit / 2; py > sy; py -= 10 * unit) {
                        cairo_move_to(cr, x, py);
                        cairo_line_to(cr, x + w, py);
                    }
                    cairo_stroke(cr);
                    if (classic) {
                        cairo_set_source_rgb(cr, 130 / 255.0, 130 / 255.0, 130 / 255.0);
                    } else {
                        cairo_set_source_rgb(cr, 120 / 255.0, 85 / 255.0, 45 / 255.0);
                    }
                    for (double px = x; px < x + w; px += 10 * unit) {
                        cairo_move_to(cr, px, top_edge);
                        cairo_line_to(cr, px + std::min(band, 10 * unit), top_edge - std::min(band, 10 * unit));
                    }
                    cairo_stroke(cr);
                }
                cairo_restore(cr);
            }
        }
        if (!rise.growth && ms < 120) {
            // Placement brackets snapping in at the corners.
            const double len = std::round(std::min(10.0 * unit, tile * 0.6) * look_fx::ease_out_cubic(ms / 90.0));
            cairo_set_source_rgb(cr, 1.0, classic ? 1.0 : 230 / 255.0, classic ? 85 / 255.0 : 120 / 255.0);
            cairo_set_line_width(cr, 2.0);
            const double xs[2] = {x + 1, x + w - 1};
            const double ys[2] = {y + 1, y + h - 1};
            for (int i = 0; i < 2; ++i) {
                for (int j = 0; j < 2; ++j) {
                    const double sx = i == 0 ? 1.0 : -1.0;
                    const double sy = j == 0 ? 1.0 : -1.0;
                    cairo_move_to(cr, xs[i] + sx * len, ys[j]);
                    cairo_line_to(cr, xs[i], ys[j]);
                    cairo_line_to(cr, xs[i], ys[j] + sy * len);
                }
            }
            cairo_stroke(cr);
        }
        if (ms < 360) {
            // Dust kicked out from the base.
            const double radius = (0.45 + 0.5 * ms / 360.0) * std::max(w, h) * 0.5;
            const double cx = x + w / 2;
            const double cy = y + h * 0.62;
            const int specks = rise.growth ? 12 : 26;
            for (int i = 0; i < specks; ++i) {
                const double angle = look_fx::hash01(static_cast<std::uint32_t>(i * 977 + rise.left * 31 + rise.top)) * 2 * look_fx::kPi;
                const double reach = 0.7 + 0.5 * look_fx::hash01(static_cast<std::uint32_t>(i * 131 + 7));
                const double sx = std::round(cx + std::cos(angle) * radius * reach);
                const double sy = std::round(cy + std::sin(angle) * radius * reach * 0.6 - ms / 180.0 * unit);
                const double size = (ms < 240 ? 3.0 : 2.0) * unit * 0.5 + 1.0;
                if (i % 2 == 0) {
                    cairo_set_source_rgb(cr, 1.0, classic ? 1.0 : 219 / 255.0, classic ? 85 / 255.0 : 140 / 255.0);
                } else {
                    cairo_set_source_rgb(cr, classic ? 170 / 255.0 : 184 / 255.0, classic ? 85 / 255.0 : 140 / 255.0,
                                         classic ? 0.0 : 71 / 255.0);
                }
                cairo_rectangle(cr, sx, sy, std::round(size), std::round(size));
                cairo_fill(cr);
            }
        }
        if (ms >= 360) {
            // Three little glints on the finished roof.
            cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
            cairo_set_line_width(cr, unit);
            const double arm = 3.0 * unit;
            const double gx[3] = {x + 2 * unit, x + w - 4 * unit, x + w - 8 * unit};
            const double gy[3] = {y + unit, y + 4 * unit, y + h - 6 * unit};
            for (int i = 0; i < 3; ++i) {
                cairo_move_to(cr, gx[i] - arm, gy[i] + unit / 2);
                cairo_line_to(cr, gx[i] + arm, gy[i] + unit / 2);
                cairo_move_to(cr, gx[i] + unit / 2, gy[i] - arm);
                cairo_line_to(cr, gx[i] + unit / 2, gy[i] + arm);
            }
            cairo_stroke(cr);
        }
    }
    cairo_restore(cr);
}
