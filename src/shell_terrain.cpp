// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

// The terrain editor: New City > Edit Terrain. It looks like the game
// (map, minimap, zoom, both skins) with terrain pieces and a brush size in
// the toolbar, and the city name with Cancel and Map Complete in the top
// bar. It paints a map of its own; Map Complete hands that map back to the
// New City page, and Generate builds the city on it.

#include "shell_state.hpp"

#include "city_seed.hpp"
#include "terrain_paint.hpp"
#include "text_paint.hpp"
#include "theme.hpp"
#include "tile_atlas.hpp"

#include "side_widgets.hpp"
#include "tool_palette.hpp"

#include "micropolis.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>

namespace {

struct Ink {
    double r;
    double g;
    double b;
};

constexpr Ink kSlateBand{0x1E / 255.0, 0x26 / 255.0, 0x31 / 255.0};
constexpr Ink kSlateBandLow{0x19 / 255.0, 0x20 / 255.0, 0x2A / 255.0};
constexpr Ink kSlateDeep{0x15 / 255.0, 0x1B / 255.0, 0x23 / 255.0};
constexpr Ink kSlatePanel{0x29 / 255.0, 0x32 / 255.0, 0x3F / 255.0};
constexpr Ink kSlateEdge{0x3A / 255.0, 0x46 / 255.0, 0x58 / 255.0};
constexpr Ink kSlateGold{0xF2 / 255.0, 0xC1 / 255.0, 0x4E / 255.0};
constexpr Ink kSlateGoldDim{0xC9 / 255.0, 0xA6 / 255.0, 0x46 / 255.0};
constexpr Ink kSlateInk{0xEE / 255.0, 0xE8 / 255.0, 0xDA / 255.0};
constexpr Ink kSlateDim{0x9A / 255.0, 0xA6 / 255.0, 0xB8 / 255.0};
constexpr Ink kClassicInk{theme::kTextR, theme::kTextG, theme::kTextB};

Ink hex(unsigned c)
{
    return Ink{((c >> 16) & 255) / 255.0, ((c >> 8) & 255) / 255.0, (c & 255) / 255.0};
}

void rect(cairo_t *cr, double x, double y, double w, double h, const Ink &ink, double alpha = 1.0)
{
    cairo_set_source_rgba(cr, ink.r, ink.g, ink.b, alpha);
    cairo_rectangle(cr, x, y, w, h);
    cairo_fill(cr);
}

void rounded_path(cairo_t *cr, double x, double y, double w, double h, double r)
{
    r = std::min(r, std::min(w, h) / 2.0);
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -look_fx::kPi / 2.0, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, look_fx::kPi / 2.0);
    cairo_arc(cr, x + r, y + h - r, r, look_fx::kPi / 2.0, look_fx::kPi);
    cairo_arc(cr, x + r, y + r, r, look_fx::kPi, 1.5 * look_fx::kPi);
    cairo_close_path(cr);
}

void text(cairo_t *cr, const char *font, const std::string &line, double x, double y, const Ink &ink)
{
    paint_text(cr, font, line, x, y, ink.r, ink.g, ink.b, -1);
}

std::string fit(cairo_t *cr, const char *font, std::string line, int room)
{
    if (room <= 0) {
        return {};
    }
    if (measure_text(cr, font, line, -1).width <= room) {
        return line;
    }
    const std::string dots = "\u2026";
    while (!line.empty() && measure_text(cr, font, line + dots, -1).width > room) {
        while (!line.empty() && (static_cast<unsigned char>(line.back()) & 0xC0) == 0x80) {
            line.pop_back();
        }
        if (!line.empty()) {
            line.pop_back();
        }
    }
    return line.empty() ? std::string() : line + dots;
}

// Toolbar art for a piece, built from the map's own tiles in the look that
// is drawn now: 2x2 tiles, 32px. Water and forest are a little pond and a
// grove made of the generator's edge tiles. No new artwork.
constexpr int kIconTiles = 2;
constexpr int kIconPx = TileAtlas::kSize * kIconTiles;

cairo_surface_t *piece_icon(int piece)
{
    struct Cache {
        cairo_surface_t *image[terrain_paint::kPieceCount] = {};
        int generation = -1;
        ~Cache()
        {
            for (cairo_surface_t *surface : image) {
                if (surface != nullptr) {
                    cairo_surface_destroy(surface);
                }
            }
        }
    };
    static Cache cache;
    if (piece < 0 || piece >= terrain_paint::kPieceCount) {
        return nullptr;
    }
    TileAtlas &atlas = tile_atlas();
    if (!atlas.loaded()) {
        return nullptr;
    }
    if (cache.generation != atlas.look_generation()) {
        for (cairo_surface_t *&surface : cache.image) {
            if (surface != nullptr) {
                cairo_surface_destroy(surface);
                surface = nullptr;
            }
        }
        cache.generation = atlas.look_generation();
    }
    if (cache.image[piece] != nullptr) {
        return cache.image[piece];
    }
    // Top-left, top-right, bottom-left, bottom-right.
    static const int kTiles[terrain_paint::kPieceCount][4] = {
        {DIRT, DIRT, DIRT, DIRT},
        {19, 7, 15, 11},
        {CHANNEL, CHANNEL, CHANNEL, CHANNEL},
        {36, 30, 34, 32},
    };
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, kIconPx, kIconPx);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surface);
        return nullptr;
    }
    cairo_surface_flush(surface);
    unsigned char *data = cairo_image_surface_get_data(surface);
    const int stride = cairo_image_surface_get_stride(surface);
    for (int i = 0; i < 4; ++i) {
        const int col = i % kIconTiles;
        const int row = i / kIconTiles;
        atlas.blit(kTiles[piece][i], data + row * TileAtlas::kSize * stride + col * TileAtlas::kSize * 4, stride);
    }
    cairo_surface_mark_dirty(surface);
    cache.image[piece] = surface;
    return surface;
}

void paint_piece_icon(cairo_t *cr, int piece, double x, double y, double size)
{
    cairo_surface_t *icon = piece_icon(piece);
    if (icon == nullptr) {
        // No atlas: the minimap colour of the piece's tile.
        double r = 0;
        double g = 0;
        double b = 0;
        minimap_tile_rgb(terrain_paint::piece_tile(static_cast<terrain_paint::Piece>(piece)), r, g, b);
        rect(cr, x, y, size, size, Ink{r, g, b});
        return;
    }
    const double scale = icon_pixel_scale(kIconPx, static_cast<int>(size));
    const double drawn = kIconPx * scale;
    cairo_save(cr);
    cairo_rectangle(cr, x, y, size, size);
    cairo_clip(cr);
    cairo_translate(cr, std::round(x + (size - drawn) / 2.0), std::round(y + (size - drawn) / 2.0));
    cairo_scale(cr, scale, scale);
    cairo_set_source_surface(cr, icon, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
    cairo_paint(cr);
    cairo_restore(cr);
}

// Brush size art: one square, or a 3x3 grid of them, in the toolbar ink.
void paint_brush_icon(cairo_t *cr, int size, double x, double y, double face, const Ink &ink)
{
    const double box = std::round(face * 0.62);
    const double ox = std::round(x + (face - box) / 2.0);
    const double oy = std::round(y + (face - box) / 2.0);
    const int n = size >= terrain_paint::kBrushLarge ? 3 : 1;
    const double gap = std::max(1.0, std::round(box / 14.0));
    const double cell = std::floor((box - gap * (3 - 1)) / 3.0);
    const double grid = cell * 3 + gap * 2;
    const double gx = ox + std::round((box - grid) / 2.0);
    const double gy = oy + std::round((box - grid) / 2.0);
    // A faint 3x3 frame behind the 1x1, so the two read as sizes.
    cairo_set_source_rgba(cr, ink.r, ink.g, ink.b, 0.28);
    cairo_set_line_width(cr, 1.0);
    cairo_rectangle(cr, gx + 0.5, gy + 0.5, grid - 1.0, grid - 1.0);
    cairo_stroke(cr);
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            const bool on = n == 3 || (row == 1 && col == 1);
            if (!on) {
                continue;
            }
            rect(cr, gx + col * (cell + gap), gy + row * (cell + gap), cell, cell, ink);
        }
    }
}

} // namespace

bool Shell::new_city_land(std::array<int, 5> &land) const
{
    int seed = 0;
    bool set = false;
    if (!take_city_seed(field_b_, seed, set)) {
        return false;
    }
    resolve_auto_seed(auto_seed_, seed, set);
    land = {seed, new_island_, new_rivers_, new_lakes_, new_trees_};
    return true;
}

const std::vector<unsigned short> *Shell::pending_terrain_for_page() const
{
    std::array<int, 5> land{};
    if (pending_terrain_.empty() || !new_city_land(land) || land != pending_land_) {
        return nullptr;
    }
    return &pending_terrain_;
}

bool Shell::confirm_drop_terrain(int id)
{
    if (pending_terrain_.empty()) {
        return true;
    }
    if (terrain_confirm_id_ != id) {
        terrain_confirm_id_ = id;
        dialog_error_ = id == kNewTerrain ? "New terrain replaces your edited map. Press it again to go ahead."
                                          : "This changes the land and drops your edited map. Choose it again to go ahead.";
        dialog_note_ok_ = false;
        return false;
    }
    terrain_confirm_id_ = 0;
    pending_terrain_.clear();
    dialog_error_.clear();
    terrain_key_ = 0;
    return true;
}

void Shell::open_terrain_editor()
{
    if (dialog_ != Dialog::NewCity || terrain_edit_) {
        return;
    }
    const bool name_ok = CitySession::name_is_usable(field_a_);
    std::array<int, 5> land{};
    const bool seed_ok = new_city_land(land);
    if (!name_ok || !seed_ok) {
        dialog_error_ = new_city_field_error(name_ok, seed_ok);
        return;
    }
    CitySession::NewCitySpec spec;
    spec.name = field_a_;
    spec.seed = land[0];
    spec.seed_was_set = true;
    spec.difficulty = new_level_;
    spec.island = land[1];
    spec.rivers = land[2];
    spec.lakes = land[3];
    spec.trees = land[4];
    if (const std::vector<unsigned short> *painted = pending_terrain_for_page()) {
        // Back in to keep painting the map Map Complete kept.
        spec.terrain = *painted;
    }
    terrain_session_ = std::make_unique<CitySession>();
    terrain_session_->set_sound_enabled(false);
    terrain_session_->new_city(spec);
    terrain_work_ = terrain_session_->terrain_tiles();
    terrain_opened_ = terrain_work_;
    terrain_history_.clear();
    terrain_land_ = land;
    terrain_changed_ = 0;
    terrain_confirm_id_ = 0;
    terrain_saved_tile_ = map_.tile_size();
    terrain_saved_scroll_x_ = scroll_x_;
    terrain_saved_scroll_y_ = scroll_y_;
    terrain_saved_overlay_ = overlay_on_;
    overlay_on_ = false;
    terrain_edit_ = true;
    close_dialog();
    reset_touch_state();
    end_mini_drag();
    map_.release();
    pointer_on_map_ = false;
    key_cursor_ = false;
    map_.set_session(terrain_session_.get());
    minimap_.set_session(terrain_session_.get());
    overlay_.set_session(terrain_session_.get());
    demand_.set_session(terrain_session_.get());
    // No city tool: the brush footprint comes from refresh_place_cue.
    map_.set_tool(TOOL_QUERY);
    focus_id_ = 0;
    hover_id_ = 0;
    set_paused_for_dialog();
    layout();
    center_on_fraction(0.5, 0.5);
}

void Shell::close_terrain_editor(bool apply)
{
    if (!terrain_edit_) {
        return;
    }
    if (apply) {
        pending_terrain_ = terrain_work_;
        pending_land_ = terrain_land_;
        // Rebuild the settings preview from the painted map.
        terrain_key_ = 0;
    }
    terrain_edit_ = false;
    reset_touch_state();
    end_mini_drag();
    map_.release();
    pointer_on_map_ = false;
    pointer_down_ = false;
    key_cursor_ = false;
    map_.set_session(&session_);
    minimap_.set_session(&session_);
    overlay_.set_session(&session_);
    demand_.set_session(&session_);
    terrain_session_.reset();
    terrain_work_.clear();
    terrain_opened_.clear();
    terrain_history_.clear();
    overlay_on_ = terrain_saved_overlay_;
    select_tool(tool_index_);
    map_.set_tile_size(terrain_saved_tile_);
    layout();
    scroll_x_ = terrain_saved_scroll_x_;
    scroll_y_ = terrain_saved_scroll_y_;
    clamp_scroll();
    // Back to the land page as it was, with the preview showing the result.
    // open_dialog() would reset every field, so the page is put back here.
    menu_.close();
    dialog_ = Dialog::NewCity;
    new_page_ = 1;
    dialog_error_.clear();
    dialog_anim_ms_ = -1;
    hover_id_ = 0;
    focus_id_ = apply ? kGenerate : kEditTerrain;
    arm_modal_grace();
    set_paused_for_dialog();
}

void Shell::cancel_terrain_editor()
{
    if (!terrain_edit_) {
        return;
    }
    if (terrain_changed_ > 0) {
        map_.release();
        pointer_on_map_ = false;
        open_dialog(Dialog::TerrainDiscard);
        return;
    }
    close_terrain_editor(false);
}

void Shell::complete_terrain_editor()
{
    if (!terrain_edit_) {
        return;
    }
    if (terrain_paint::land_percent(terrain_work_) < terrain_paint::kLowLandPercent) {
        map_.release();
        pointer_on_map_ = false;
        open_dialog(Dialog::TerrainLowLand);
        return;
    }
    close_terrain_editor(true);
}

void Shell::paint_terrain_line(int x0, int y0, int x1, int y1)
{
    if (!terrain_edit_ || terrain_session_ == nullptr) {
        return;
    }
    const auto piece = static_cast<terrain_paint::Piece>(terrain_piece_);
    const int steps = std::max(std::abs(x1 - x0), std::abs(y1 - y0));
    // The stroke's first change records the map before it, for undo.
    const bool first = terrain_history_.wants_snapshot();
    std::vector<unsigned short> before;
    if (first) {
        before = terrain_work_;
    }
    int changed = 0;
    for (int i = 0; i <= steps; ++i) {
        const int tx = steps == 0 ? x1 : x0 + (x1 - x0) * i / steps;
        const int ty = steps == 0 ? y1 : y0 + (y1 - y0) * i / steps;
        changed += terrain_paint::paint(terrain_work_, CitySession::kWorldW, CitySession::kWorldH, tx, ty, piece,
                                        terrain_brush_);
    }
    if (changed > 0) {
        if (first) {
            terrain_history_.record(std::move(before));
        }
        terrain_changed_ += changed;
        terrain_session_->set_terrain_tiles(terrain_work_);
    }
}

bool Shell::step_terrain_history(bool forward)
{
    if (!terrain_edit_ || terrain_session_ == nullptr) {
        return false;
    }
    if (!(forward ? terrain_history_.redo(terrain_work_) : terrain_history_.undo(terrain_work_))) {
        return false;
    }
    terrain_session_->set_terrain_tiles(terrain_work_);
    // Back at the map the editor opened with: Cancel has nothing to lose.
    terrain_changed_ = terrain_work_ == terrain_opened_ ? 0 : std::max(1, terrain_changed_);
    return true;
}

void Shell::select_terrain_piece(int index)
{
    if (index < 0 || index >= terrain_paint::kPieceCount) {
        return;
    }
    terrain_piece_ = index;
}

void Shell::set_terrain_brush(int size)
{
    terrain_brush_ = size >= terrain_paint::kBrushLarge ? terrain_paint::kBrushLarge : terrain_paint::kBrushSmall;
}

bool Shell::on_terrain_hotspot(int id)
{
    if (!terrain_edit_ || dialog_ != Dialog::None) {
        return false;
    }
    if (id >= kTerrainPiece && id < kTerrainPiece + terrain_paint::kPieceCount) {
        select_terrain_piece(id - kTerrainPiece);
        return true;
    }
    if (id == kTerrainBrush || id == kTerrainBrush + 1) {
        set_terrain_brush(id == kTerrainBrush ? terrain_paint::kBrushSmall : terrain_paint::kBrushLarge);
        return true;
    }
    if (id == kTerrainComplete) {
        complete_terrain_editor();
        return true;
    }
    if (id == kTerrainCancel) {
        cancel_terrain_editor();
        return true;
    }
    if (id == kTerrainUndo || id == kTerrainRedo) {
        // The Undo / Redo chips: the same stroke step as Ctrl+Z / Ctrl+Y and
        // the pad's X / L3, so a Deck or touch player can take a stroke back.
        step_terrain_history(id == kTerrainRedo);
        return true;
    }
    // Nothing else in the editor's chrome does anything (there is no menu).
    return id != kMapId && id != kMinimapId && id != kHudZoomIn && id != kHudZoomOut && id != kHudCenter;
}


void Shell::draw_terrain_palette(cairo_t *cr)
{
    // Same tray, cells and section headers as the tool palette: TERRAIN,
    // then BRUSH SIZE. The Classic skin gets small headers too, since the
    // two groups are not the game's own tool groups.
    const int columns = std::max(2, palette_.columns());
    const int cell = std::max(24, cell_);
    const double pad = 4;
    const double header = dp(18);
    const double w = pad * 2 + columns * cell;
    const int piece_rows = (terrain_paint::kPieceCount + columns - 1) / columns;
    const double h = pad * 2 + header * 2 + (piece_rows + 1) * cell + dp(4);
    const double x0 = side_x_;
    const double y0 = pal_y_;
    const int under = gamepad_nav_ ? hit(cursor_x_, cursor_y_) : hover_id_;
    cairo_save(cr);
    if (slate_) {
        rect(cr, x0, y0, w, h, hex(0x1B222C));
    } else {
        rect(cr, x0, y0, w, h, hex(0x5E5242));
        rect(cr, x0 + 1, y0 + 1, w - 2, h - 2, hex(0xEDECEB));
    }
    const double font = std::max(8.0, std::min(header * 0.58, cell * 0.26));
    auto section = [&](const char *title, double top) {
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, font);
        cairo_text_extents_t extents;
        cairo_text_extents(cr, title, &extents);
        const double baseline = top + header * 0.5 + font * 0.36;
        const Ink ink = slate_ ? kSlateGoldDim : hex(0x5E5242);
        cairo_set_source_rgb(cr, ink.r, ink.g, ink.b);
        cairo_move_to(cr, x0 + pad + 2 - extents.x_bearing, baseline);
        cairo_show_text(cr, title);
        const double rule_x = x0 + pad + 2 + extents.x_advance + 5;
        const double rule_end = x0 + w - pad - 2;
        if (rule_x < rule_end) {
            rect(cr, std::round(rule_x), std::round(baseline - font * 0.36), rule_end - rule_x, 1,
                 slate_ ? kSlateEdge : hex(0x9E907A));
        }
    };
    // One button face in the tool palette's style for each skin.
    auto face = [&](double cx, double cy, int id, bool chosen, auto &&paint_inside) {
        const bool hot = under == id || focus_id_ == id;
        const bool down = hot && pointer_down_ && hover_id_ == id;
        if (slate_) {
            const double inset = std::max(2.0, std::round(cell / 22.0));
            const double x = cx + inset;
            const double y = cy + inset;
            const double bs = cell - inset * 2;
            const double radius = std::max(3.0, bs * 0.14);
            if (chosen) {
                for (int ring = 3; ring >= 1; --ring) {
                    rounded_path(cr, x - ring * 1.5, y - ring * 1.5, bs + ring * 3.0, bs + ring * 3.0, radius + ring * 1.5);
                    const Ink glow = kSlateGold;
                    cairo_set_source_rgba(cr, glow.r, glow.g, glow.b, 0.10 + 0.08 * (3 - ring));
                    cairo_fill(cr);
                }
            }
            rounded_path(cr, x, y, bs, bs, radius);
            const Ink fill_ink = hex(chosen ? 0x3E4A5E : (hot ? 0x354154 : 0x29323F));
            cairo_set_source_rgb(cr, fill_ink.r, fill_ink.g, fill_ink.b);
            cairo_fill_preserve(cr);
            cairo_set_line_width(cr, chosen ? 2.0 : 1.0);
            const Ink edge = hex(chosen ? 0xF2C14E : (hot ? 0x8C98AA : 0x3C4859));
            cairo_set_source_rgb(cr, edge.r, edge.g, edge.b);
            cairo_stroke(cr);
            const double shift = down ? 1.0 : 0.0;
            const double inner = bs >= 30.0 ? 3.0 : 2.0;
            paint_inside(std::round(x + inner + shift), std::round(y + inner + shift), bs - inner * 2, kSlateInk);
            if (gamepad_nav_ && under == id) {
                cairo_set_line_width(cr, 2.0);
                cairo_set_source_rgb(cr, 0xF6 / 255.0, 0xF0 / 255.0, 0xE4 / 255.0);
                rounded_path(cr, x + 2.5, y + 2.5, bs - 5, bs - 5, std::max(2.0, radius - 2));
                cairo_stroke(cr);
            }
        } else {
            const double x = cx + 1;
            const double y = cy + 1;
            const double bs = cell - 2;
            const double unit = std::max(1.0, std::round(bs / 46.0));
            const bool sunk = chosen || down;
            rect(cr, x, y, bs, bs, hex(hot && !sunk ? 0xB88C2E : 0x5E5242));
            rect(cr, x + unit, y + unit, bs - 2 * unit, bs - 2 * unit, hex(sunk ? 0xC9B892 : (hot ? 0xEBE3D5 : 0xDED4C2)));
            if (sunk) {
                rect(cr, x + unit, y + unit, bs - 2 * unit, 2 * unit, hex(0x8C634A));
                rect(cr, x + unit, y + unit, 2 * unit, bs - 2 * unit, hex(0x8C634A));
            } else {
                rect(cr, x + unit, y + unit, bs - 2 * unit, 2 * unit, hex(0xF6F0E4));
                rect(cr, x + unit, y + unit, 2 * unit, bs - 2 * unit, hex(0xF6F0E4));
                rect(cr, x + unit, y + bs - 3 * unit, bs - 2 * unit, 2 * unit, hex(0x9E907A));
                rect(cr, x + bs - 3 * unit, y + unit, 2 * unit, bs - 2 * unit, hex(0x9E907A));
            }
            const double shift = sunk ? unit : 0.0;
            paint_inside(x + 4 * unit + shift, y + 4 * unit + shift, bs - 8 * unit, hex(0x2E241C));
            if (sunk || hot) {
                rect(cr, x + 6 * unit, y + bs - (sunk ? 3 : 4) * unit, bs - 12 * unit, 2 * unit,
                     hex(sunk ? 0xB88C2E : 0xB59473));
            }
            if (gamepad_nav_ && under == id) {
                cairo_set_line_width(cr, unit);
                cairo_set_source_rgb(cr, 0x2E / 255.0, 0x24 / 255.0, 0x1C / 255.0);
                cairo_rectangle(cr, x + 2.5 * unit, y + 2.5 * unit, bs - 5 * unit, bs - 5 * unit);
                cairo_stroke(cr);
            }
        }
        push_hot(cx, cy, cell, cell, id);
    };
    double top = y0 + pad;
    section("TERRAIN", top);
    top += header;
    for (int i = 0; i < terrain_paint::kPieceCount; ++i) {
        const double cx = x0 + pad + (i % columns) * cell;
        const double cy = top + (i / columns) * cell;
        face(cx, cy, kTerrainPiece + i, terrain_piece_ == i, [&](double ix, double iy, double size, const Ink &) {
            cairo_save(cr);
            cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
            paint_piece_icon(cr, i, ix, iy, size);
            cairo_restore(cr);
        });
    }
    top += piece_rows * cell + dp(4);
    section("BRUSH SIZE", top);
    top += header;
    for (int b = 0; b < 2; ++b) {
        const int size = b == 0 ? terrain_paint::kBrushSmall : terrain_paint::kBrushLarge;
        const double cx = x0 + pad + b * cell;
        face(cx, top, kTerrainBrush + b, terrain_brush_ == size, [&](double ix, double iy, double box, const Ink &ink) {
            cairo_save(cr);
            cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
            paint_brush_icon(cr, size, ix, iy, box, ink);
            cairo_restore(cr);
        });
        // The size in words beside the art, as the toolbar has no labels.
        if (columns >= 3 && b == 1) {
            const std::string label = terrain_brush_ >= terrain_paint::kBrushLarge ? "3x3" : "1x1";
            const TextMetrics m = measure_text(cr, bold_font(), label, -1);
            const double lx = x0 + pad + 2 * cell + (cell - m.width) / 2.0;
            text(cr, bold_font(), label, lx, top + (cell - m.height) / 2.0, slate_ ? kSlateGold : kClassicInk);
        }
    }
    cairo_restore(cr);
}

void Shell::draw_terrain_band(cairo_t *cr)
{
    const double y = band_y_;
    const double h = band_h_;
    const double right_edge = win_w_ - dp(12);
    if (slate_) {
        cairo_pattern_t *grad = cairo_pattern_create_linear(0, y, 0, y + h);
        cairo_pattern_add_color_stop_rgb(grad, 0, kSlateBand.r, kSlateBand.g, kSlateBand.b);
        cairo_pattern_add_color_stop_rgb(grad, 1, kSlateBandLow.r, kSlateBandLow.g, kSlateBandLow.b);
        cairo_set_source(cr, grad);
        cairo_rectangle(cr, 0, y, win_w_, h);
        cairo_fill(cr);
        cairo_pattern_destroy(grad);
        rect(cr, 0, y + h - 1, win_w_, 1, kSlateGoldDim, 0.55);
    } else {
        cairo_pattern_t *grad = cairo_pattern_create_linear(0, y, 0, y + h);
        cairo_pattern_add_color_stop_rgb(grad, 0, theme::kMenuTopR, theme::kMenuTopG, theme::kMenuTopB);
        cairo_pattern_add_color_stop_rgb(grad, 1, theme::kMenuBotR, theme::kMenuBotG, theme::kMenuBotB);
        cairo_set_source(cr, grad);
        cairo_rectangle(cr, 0, y, win_w_, h);
        cairo_fill(cr);
        cairo_pattern_destroy(grad);
        rect(cr, 0, y + h - 1, win_w_, 1, Ink{theme::kMenuFrameR, theme::kMenuFrameG, theme::kMenuFrameB});
    }
    const Ink label_ink = slate_ ? kSlateDim : hex(0x5E5242);
    const Ink value_ink = slate_ ? kSlateInk : kClassicInk;
    // Buttons on the right: Cancel, then Map Complete as the main action.
    const double bh = dp(30);
    const double by = y + (h - bh) / 2.0;
    // Undo and Redo step one brush stroke (Ctrl+Z / Ctrl+Y); they used to
    // be keyboard-only (review nov25 #12).
    const char *labels[4] = {"Undo", "Redo", "Cancel", "Map Complete"};
    const int ids[4] = {kTerrainUndo, kTerrainRedo, kTerrainCancel, kTerrainComplete};
    const bool able[4] = {terrain_history_.can_undo(), terrain_history_.can_redo(), true, true};
    double widths[4] = {};
    double total = 0;
    for (int i = 0; i < 4; ++i) {
        widths[i] = std::max(static_cast<double>(dp(i < 2 ? 64 : 84)),
                             static_cast<double>(measure_text(cr, bold_font(), labels[i], -1).width + dp(28)));
        total += widths[i] + (i > 0 ? dp(8) : 0);
    }
    double bx = right_edge - total;
    const double buttons_left = bx;
    for (int i = 0; i < 4; ++i) {
        const int id = ids[i];
        const bool primary = id == kTerrainComplete;
        const bool hot = able[i] && dialog_ == Dialog::None &&
                         (hover_id_ == id || (gamepad_nav_ && hit(cursor_x_, cursor_y_) == id));
        const bool down = hot && pointer_down_ && hover_id_ == id;
        if (slate_) {
            rounded_path(cr, bx, by, widths[i], bh, dp(6));
            const Ink face = primary ? (down ? kSlateGoldDim : kSlateGold) : (hot ? kSlateEdge : kSlatePanel);
            cairo_set_source_rgb(cr, face.r, face.g, face.b);
            cairo_fill_preserve(cr);
            cairo_set_line_width(cr, 1.0);
            const Ink edge = primary ? kSlateGoldDim : (hot ? hex(0x8C98AA) : kSlateEdge);
            cairo_set_source_rgb(cr, edge.r, edge.g, edge.b);
            cairo_stroke(cr);
            const TextMetrics m = measure_text(cr, bold_font(), labels[i], -1);
            text(cr, bold_font(), labels[i], bx + (widths[i] - m.width) / 2.0, by + (bh - m.height) / 2.0,
                 primary ? kSlateDeep : (able[i] ? kSlateInk : kSlateDim));
            push_hot(bx, by, widths[i], bh, id);
        } else {
            button(cr, bx, by, widths[i], bh, labels[i], id, !able[i]);
            if (primary) {
                // The main action: a gold keyline like the chosen toolbar stone.
                cairo_set_source_rgb(cr, 0xB8 / 255.0, 0x8C / 255.0, 0x2E / 255.0);
                cairo_set_line_width(cr, 2.0);
                cairo_rectangle(cr, bx - 1, by - 1, widths[i] + 2, bh + 2);
                cairo_stroke(cr);
            }
        }
        bx += widths[i] + dp(8);
    }
    // Left: the city name, the piece being painted and the brush.
    const char *tags[3] = {"CITY", "PAINTING", "BRUSH"};
    const std::string values[3] = {field_a_,
                                   terrain_paint::piece_name(static_cast<terrain_paint::Piece>(terrain_piece_)),
                                   terrain_brush_ >= terrain_paint::kBrushLarge ? "3x3" : "1x1"};
    const double label_y = y + dp(6);
    const double value_y = y + dp(20);
    const double gap = dp(16);
    double x = dp(12);
    for (int k = 0; k < 3; ++k) {
        const double label_w = measure_text(cr, tag_font(), tags[k], -1).width;
        const double room = buttons_left - gap - x;
        if (room < dp(48)) {
            break;
        }
        const std::string shown = fit(cr, bold_font(), values[k], static_cast<int>(std::min(room, k == 0 ? static_cast<double>(dp(260)) : room)));
        const double value_w = measure_text(cr, bold_font(), shown, -1).width;
        if (k > 0) {
            rect(cr, x - gap, y + dp(9), 1, h - dp(18), slate_ ? kSlateEdge : hex(0x9E907A));
        }
        text(cr, tag_font(), tags[k], x, label_y, label_ink);
        text(cr, bold_font(), shown, x, value_y, k == 1 && slate_ ? kSlateGold : value_ink);
        x += std::max(label_w, value_w) + gap * 2;
    }
}

void Shell::draw_terrain_status(cairo_t *cr, const std::string &hint)
{
    const double y = win_h_ - status_h_;
    if (slate_) {
        rect(cr, 0, y, win_w_, status_h_, kSlateDeep);
        rect(cr, 0, y, win_w_, 1, kSlateEdge);
    } else {
        rect(cr, 0, y, win_w_, status_h_, Ink{theme::kMenuBotR, theme::kMenuBotG, theme::kMenuBotB});
    }
    const double text_y = y + (status_h_ - measure_text(cr, ui_font(), "Ag", -1).height) / 2.0;
    // Left: what is under the pointer in the toolbar, else what the brush lays.
    std::string left;
    const int under = gamepad_nav_ ? hit(cursor_x_, cursor_y_) : hover_id_;
    if (under >= kTerrainPiece && under < kTerrainPiece + terrain_paint::kPieceCount) {
        const auto piece = static_cast<terrain_paint::Piece>(under - kTerrainPiece);
        left = std::string(terrain_paint::piece_name(piece)) + ": " + terrain_paint::piece_hint(piece);
        if (!gamepad_nav_ && !deck_) {
            left += "  [" + std::to_string(under - kTerrainPiece + 1) + "]";
        }
    } else if (under == kTerrainBrush || under == kTerrainBrush + 1) {
        left = under == kTerrainBrush ? "Brush 1x1: paints one tile at a time."
                                      : "Brush 3x3: paints a 3x3 square, for large areas.";
        if (!gamepad_nav_ && !deck_) {
            left += "  [B]";
        }
    } else if (under == kTerrainComplete) {
        left = "Map Complete: keep this map and go back to the New City settings.";
    } else if (under == kTerrainCancel) {
        left = "Cancel: go back to the New City settings without these changes.";
    } else {
        const auto piece = static_cast<terrain_paint::Piece>(terrain_piece_);
        left = std::string("Painting ") + terrain_paint::piece_name(piece) + " with the " +
               (terrain_brush_ >= terrain_paint::kBrushLarge ? "3x3" : "1x1") + " brush.";
    }
    double x = dp(10);
    if (slate_) {
        const TextMetrics tag = measure_text(cr, tag_font(), "TERRAIN", -1);
        const double tag_w = tag.width + dp(14);
        const double tag_h = tag.height + dp(6);
        const double tag_y = y + (status_h_ - tag_h) / 2.0;
        rounded_path(cr, x, tag_y, tag_w, tag_h, dp(4));
        cairo_set_source_rgb(cr, kSlateGold.r, kSlateGold.g, kSlateGold.b);
        cairo_fill(cr);
        text(cr, tag_font(), "TERRAIN", x + dp(7), tag_y + dp(3), kSlateDeep);
        x += tag_w + dp(10);
    }
    double right = win_w_ - dp(10);
    const int left_w = measure_text(cr, ui_font(), left, -1).width;
    const int hint_room = static_cast<int>(right - x - std::min(left_w, dp(420)) - dp(24));
    std::string hint_line;
    if (hint_room >= dp(120)) {
        hint_line = hint;
        if (measure_text(cr, ui_font(), hint_line, -1).width > hint_room) {
            // A shorter form of the keyboard hint before an ellipsis.
            const std::vector<std::string> forms = terrain_hint_forms();
            if (hint == forms.front()) {
                for (const std::string &form : forms) {
                    hint_line = form;
                    if (measure_text(cr, ui_font(), form, -1).width <= hint_room) {
                        break;
                    }
                }
            }
            hint_line = fit(cr, ui_font(), hint_line, hint_room);
        }
    }
    if (!hint_line.empty()) {
        const int w = measure_text(cr, ui_font(), hint_line, -1).width;
        right -= w;
        text(cr, ui_font(), hint_line, right, text_y, slate_ ? kSlateDim : kClassicInk);
        right -= dp(24);
    }
    text(cr, ui_font(), fit(cr, ui_font(), left, static_cast<int>(right - x)), x, text_y, slate_ ? kSlateInk : kClassicInk);
}

void Shell::draw_terrain_hud(cairo_t *cr)
{
    // The Map editor on a handheld (Deck hardware notes, Nov 25): the same
    // pieces as the city HUD. The minimap and the zoom buttons as in play,
    // a title chip and the Cancel / Map Complete chips along the top, and
    // the terrain pieces and brush sizes in the quick bar at the bottom.
    draw_minimap_overlay(cr);
    const int under = gamepad_nav_ ? hit(cursor_x_, cursor_y_) : hover_id_;
    const bool live = dialog_ == Dialog::None;
    const double top = dp(10);
    const double chip_h = dp(52);
    const double right_limit = mini_x_ - dp(16);
    const std::string piece = terrain_paint::piece_name(static_cast<terrain_paint::Piece>(terrain_piece_));
    const std::string brush = std::string(terrain_brush_ >= terrain_paint::kBrushLarge ? "3x3" : "1x1") + " brush";
    // Undo / Redo chips first (review nov25 #12: undo was keyboard-only).
    const char *labels[4] = {"Undo", "Redo", "Cancel", "Map Complete"};
    const int ids[4] = {kTerrainUndo, kTerrainRedo, kTerrainCancel, kTerrainComplete};
    const bool able[4] = {terrain_history_.can_undo(), terrain_history_.can_redo(), true, true};
    double button_w[4] = {};
    for (int i = 0; i < 4; ++i) {
        button_w[i] = measure_text(cr, bold_font(), labels[i], -1).width + dp(32);
    }
    // Title chip: a TERRAIN tag, the piece being painted, the brush, and
    // the city's name when there is room for it.
    const TextMetrics tag = measure_text(cr, tag_font(), "TERRAIN", -1);
    const double tag_w = tag.width + dp(14);
    const double tag_h = tag.height + dp(6);
    const double pad = dp(12);
    const double sep = dp(12);
    const double piece_w = measure_text(cr, bold_font(), piece, -1).width;
    const double brush_w = measure_text(cr, ui_font(), brush, -1).width;
    const double buttons = button_w[0] + button_w[1] + button_w[2] + button_w[3] + dp(8) * 3;
    double x = dp(10);
    double title_w = pad + tag_w + sep + piece_w + sep + brush_w + pad;
    std::string city = field_a_;
    double city_w = city.empty() ? 0.0 : measure_text(cr, ui_font(), city, -1).width;
    const double city_room = right_limit - x - buttons - dp(16) - title_w - sep;
    if (city_w > 0.0 && city_room >= dp(60)) {
        if (city_w > city_room) {
            city = fit(cr, ui_font(), city, static_cast<int>(city_room));
            city_w = measure_text(cr, ui_font(), city, -1).width;
        }
        title_w += sep + city_w;
    } else {
        city.clear();
    }
    rounded_path(cr, x, top, title_w, chip_h, dp(10));
    cairo_set_source_rgba(cr, kSlateBand.r, kSlateBand.g, kSlateBand.b, 0.94);
    cairo_fill(cr);
    {
        double cx = x + pad;
        const double tag_y = top + (chip_h - tag_h) / 2.0;
        rounded_path(cr, cx, tag_y, tag_w, tag_h, dp(4));
        cairo_set_source_rgb(cr, kSlateGold.r, kSlateGold.g, kSlateGold.b);
        cairo_fill(cr);
        text(cr, tag_font(), "TERRAIN", cx + dp(7), tag_y + dp(3), kSlateDeep);
        cx += tag_w + sep;
        const double ty = top + (chip_h - measure_text(cr, bold_font(), "Ag", -1).height) / 2.0;
        text(cr, bold_font(), piece, cx, ty, kSlateGold);
        cx += piece_w + sep;
        text(cr, ui_font(), brush, cx, ty + dp(1), kSlateInk);
        cx += brush_w + sep;
        if (!city.empty()) {
            text(cr, ui_font(), city, cx, ty + dp(1), kSlateDim);
        }
    }
    x += title_w + dp(8);
    for (int i = 0; i < 4; ++i) {
        const int id = ids[i];
        const bool primary = id == kTerrainComplete;
        const bool hot = live && able[i] && under == id;
        const bool down = hot && pointer_down_ && hover_id_ == id;
        rounded_path(cr, x, top, button_w[i], chip_h, dp(10));
        const Ink face = primary ? (down ? kSlateGoldDim : kSlateGold) : (hot ? kSlateEdge : kSlateBand);
        cairo_set_source_rgba(cr, face.r, face.g, face.b, primary ? 1.0 : 0.94);
        cairo_fill_preserve(cr);
        const Ink edge = hot ? kSlateInk : (primary ? kSlateGoldDim : kSlateEdge);
        cairo_set_source_rgb(cr, edge.r, edge.g, edge.b);
        cairo_set_line_width(cr, hot ? 2.0 : 1.0);
        cairo_stroke(cr);
        const TextMetrics m = measure_text(cr, bold_font(), labels[i], -1);
        text(cr, bold_font(), labels[i], x + (button_w[i] - m.width) / 2.0, top + (chip_h - m.height) / 2.0,
             primary ? kSlateDeep : (able[i] ? kSlateInk : kSlateDim));
        push_hot(x, top, button_w[i], chip_h, id);
        x += button_w[i] + dp(8);
    }
    draw_hud_zoom_buttons(cr);
    // Quick bar: the terrain pieces, a rule, then the two brush sizes. The
    // chosen piece and the chosen brush are raised with a gold edge.
    const double slot = static_cast<double>(quick_h_);
    const double lift = dp(8);
    const double gap = dp(16);
    const int count = terrain_paint::kPieceCount + 2;
    const double bar_w = count * slot + gap + dp(16);
    const double bar_x = std::round((win_w_ - bar_w) / 2.0);
    rounded_path(cr, bar_x, quick_y_, bar_w, quick_h_ + dp(4), dp(12));
    cairo_set_source_rgba(cr, kSlateBand.r, kSlateBand.g, kSlateBand.b, 0.94);
    cairo_fill(cr);
    double sx = bar_x + dp(8);
    double chosen_x = -1.0;
    for (int i = 0; i < count; ++i) {
        const bool is_piece = i < terrain_paint::kPieceCount;
        if (i == terrain_paint::kPieceCount) {
            const double rx = std::round(sx + gap / 2.0) - 0.5;
            cairo_set_source_rgba(cr, kSlateDim.r, kSlateDim.g, kSlateDim.b, 0.55);
            cairo_set_line_width(cr, 1.0);
            cairo_move_to(cr, rx, quick_y_ + dp(8));
            cairo_line_to(cr, rx, quick_y_ + quick_h_ - dp(4));
            cairo_stroke(cr);
            sx += gap;
        }
        const int b = i - terrain_paint::kPieceCount;
        const int size = b == 0 ? terrain_paint::kBrushSmall : terrain_paint::kBrushLarge;
        const int id = is_piece ? kTerrainPiece + i : kTerrainBrush + b;
        const bool chosen = is_piece ? terrain_piece_ == i : terrain_brush_ == size;
        const bool hot = live && under == id;
        const double sy = quick_y_ + dp(4) - (chosen ? lift : 0);
        if (chosen) {
            rounded_path(cr, sx + 1, sy, slot - 2, slot - 2, dp(8));
            cairo_set_source_rgb(cr, kSlatePanel.r, kSlatePanel.g, kSlatePanel.b);
            cairo_fill_preserve(cr);
            cairo_set_source_rgb(cr, kSlateGold.r, kSlateGold.g, kSlateGold.b);
            cairo_set_line_width(cr, 2.0);
            cairo_stroke(cr);
            if (is_piece) {
                chosen_x = sx + slot / 2.0;
            }
        } else if (hot) {
            rounded_path(cr, sx + 1, sy, slot - 2, slot - 2, dp(8));
            cairo_set_source_rgb(cr, kSlateEdge.r, kSlateEdge.g, kSlateEdge.b);
            cairo_fill(cr);
        }
        const double icon = std::floor(std::max(16.0, slot - dp(16)));
        const double ix = std::floor(sx + (slot - icon) / 2.0);
        const double iy = std::floor(sy + (slot - 2 - icon) / 2.0);
        cairo_save(cr);
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
        if (is_piece) {
            paint_piece_icon(cr, i, ix, iy, icon);
        } else {
            paint_brush_icon(cr, size, ix, iy, icon, kSlateInk);
        }
        cairo_restore(cr);
        if (hot && chosen) {
            rounded_path(cr, sx + 3, sy + 2, slot - 6, slot - 6, dp(6));
            cairo_set_source_rgb(cr, kSlateInk.r, kSlateInk.g, kSlateInk.b);
            cairo_set_line_width(cr, 2.0);
            cairo_stroke(cr);
        }
        push_hot(sx, quick_y_ - lift, slot, quick_h_ + lift + dp(4), id);
        sx += slot;
    }
    // L1/R1 step the pieces: their glyphs at the bar's ends.
    const std::string prev = glyph_for(PadPrevTool);
    const std::string next = glyph_for(PadNextTool);
    if (gamepad_nav_ || deck_) {
        const std::string glyphs[2] = {prev, next};
        for (int k = 0; k < 2; ++k) {
            if (glyphs[k].empty()) {
                continue;
            }
            const TextMetrics m = measure_text(cr, tag_font(), glyphs[k], -1);
            const double kw = m.width + dp(12);
            const double ky = quick_y_ + (quick_h_ - m.height - dp(6)) / 2.0;
            const double kx = k == 0 ? bar_x - kw - dp(8) : bar_x + bar_w + dp(8);
            rounded_path(cr, kx, ky, kw, m.height + dp(6), dp(4));
            cairo_set_source_rgba(cr, kSlatePanel.r, kSlatePanel.g, kSlatePanel.b, 0.95);
            cairo_fill(cr);
            text(cr, tag_font(), glyphs[k], kx + dp(6), ky + dp(3), kSlateInk);
        }
    }
    // The chosen piece's name above its raised slot, like a tool's.
    if (chosen_x >= 0.0 && live) {
        const TextMetrics m = measure_text(cr, bold_font(), piece, -1);
        const double cw = m.width + dp(16);
        const double ch = m.height + dp(8);
        const double cx = std::max(static_cast<double>(dp(8)), std::min(chosen_x - cw / 2.0, win_w_ - cw - dp(8)));
        const double cy = quick_y_ - lift - ch - dp(6);
        rounded_path(cr, cx, cy, cw, ch, dp(6));
        cairo_set_source_rgba(cr, kSlateDeep.r, kSlateDeep.g, kSlateDeep.b, 0.92);
        cairo_fill_preserve(cr);
        cairo_set_source_rgb(cr, kSlateEdge.r, kSlateEdge.g, kSlateEdge.b);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);
        text(cr, bold_font(), piece, cx + dp(8), cy + dp(4), kSlateGold);
    }
    draw_pad_legend(cr, win_h_ - hint_h_, hint_h_);
}
