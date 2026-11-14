// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#pragma once

// Slider focus is a double ring. The dark outer stroke stays visible on the
// light dialog body. The inner stroke stays visible on the coloured fill.
// The standard blue fill and orange inner stroke are shared by all tile looks.

struct Rgb8 {
    int r = 0;
    int g = 0;
    int b = 0;
};

struct SliderFocusRing {
    Rgb8 outer;
    Rgb8 inner;
};

inline Rgb8 dialog_body_rgb()
{
    return Rgb8{0xED, 0xEC, 0xEB};
}

inline Rgb8 slider_fill_rgb()
{
    return Rgb8{0x4A, 0x7A, 0xBB};
}

inline SliderFocusRing slider_focus_ring()
{
    return SliderFocusRing{Rgb8{0, 0, 0}, Rgb8{0xE6, 0x6A, 0x00}};
}
