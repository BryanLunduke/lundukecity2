// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#pragma once

#include <cstdint>
#include <string>

// Classic 16-pixel Micropolis map tiles, indexed the same way as the engine
// (low 10 bits of a map word). Loaded once from images/micropolisEngine/tiles.png.
class TileAtlas {
public:
    static constexpr int kSize = 16;
    // Half of kSize. Built once at load with a 2×2 box filter and drawn 1:1
    // for the overview zoom, so an 8px scale does not drop atlas pixels.
    static constexpr int kOverview = 8;

    bool loaded() const { return count_ > 0; }
    int count() const { return count_; }

    // Average color of one tile, 0..1. Out-of-range tiles report clear land.
    void average_color(int tile, double &r, double &g, double &b) const;

    // Copy one tile into a Cairo-style ARGB32 buffer (native endian, opaque).
    // dest points at the top-left pixel; stride is bytes per row.
    void blit(int tile, unsigned char *dest, int stride) const;
    // The same tile at kOverview, box-filtered from the 16px art at load.
    void blit_overview(int tile, unsigned char *dest, int stride) const;

    bool ensure_loaded();
    // Empty when the tiles loaded. Otherwise the dialog body (searched paths).
    const std::string &load_error() const { return error_; }
    // How many times the file was actually opened. A failure is not retried.
    int load_attempts() const { return attempts_; }

    // Classic is the default. SVGA is used only when its sheet loaded.
    // Switching does not reload the files. A missing sheet stays on Classic.
    bool use_svga() const { return use_svga_; }
    bool svga_available() const { return svga_ready_; }
    bool set_look(bool svga);
    // Bumps when the active sheet changes, so a cached map can redraw.
    int look_generation() const { return generation_; }
    // Drop the SVGA sheet. The next SVGA request stays on Classic.
    // The decoded pixels stay loaded; recall_svga() puts the sheet back.
    void forget_svga();
    bool recall_svga();

private:
    void bind_active();

    int count_ = 0;
    int columns_ = 16;
    int attempts_ = 0;
    int generation_ = 0;
    bool tried_ = false;
    bool svga_ready_ = false;
    bool use_svga_ = false;
    std::string error_;
    // Tightly packed opaque pixels, 16*16 per tile, native ARGB32.
    // Held as bytes so the header stays free of containers' noise in TUs.
    const std::uint32_t *pixels_ = nullptr;
    const std::uint32_t *overview_ = nullptr;
    const unsigned char *average_ = nullptr; // count * 3, RGB
};

TileAtlas &tile_atlas();
