// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#pragma once

// Clearlooks-Phenix colours, sampled for the LCOS desktop theme. The SDL
// shell paints these directly so Deck and Windows match LCOS.

namespace theme {

inline constexpr double kWindowR = 0xED / 255.0;
inline constexpr double kWindowG = 0xEC / 255.0;
inline constexpr double kWindowB = 0xEB / 255.0;

inline constexpr double kMenuTopR = 0xE8 / 255.0;
inline constexpr double kMenuTopG = 0xE8 / 255.0;
inline constexpr double kMenuTopB = 0xE7 / 255.0;
inline constexpr double kMenuBotR = 0xD4 / 255.0;
inline constexpr double kMenuBotG = 0xD4 / 255.0;
inline constexpr double kMenuBotB = 0xD3 / 255.0;

inline constexpr double kSelectR = 0x4A / 255.0;
inline constexpr double kSelectG = 0x7A / 255.0;
inline constexpr double kSelectB = 0xBB / 255.0;
inline constexpr double kSelectHiR = 0x8C / 255.0;
inline constexpr double kSelectHiG = 0xB0 / 255.0;
inline constexpr double kSelectHiB = 0xD8 / 255.0;
inline constexpr double kSelectEdgeR = 0x3C / 255.0;
inline constexpr double kSelectEdgeG = 0x5F / 255.0;
inline constexpr double kSelectEdgeB = 0x8A / 255.0;

inline constexpr double kMenuBgR = 1.0;
inline constexpr double kMenuBgG = 1.0;
inline constexpr double kMenuBgB = 1.0;
inline constexpr double kMenuFrameR = 0x8E / 255.0;
inline constexpr double kMenuFrameG = 0x8E / 255.0;
inline constexpr double kMenuFrameB = 0x8E / 255.0;
inline constexpr double kHoverR = 0xC1 / 255.0;
inline constexpr double kHoverG = 0xD6 / 255.0;
inline constexpr double kHoverB = 0xF0 / 255.0;

inline constexpr double kTextR = 0.05;
inline constexpr double kTextG = 0.05;
inline constexpr double kTextB = 0.05;
inline constexpr double kAccelR = 0.33;
inline constexpr double kAccelG = 0.33;
inline constexpr double kAccelB = 0.33;
inline constexpr double kDisabledR = 0.50;
inline constexpr double kDisabledG = 0.50;
inline constexpr double kDisabledB = 0.50;

inline constexpr double kTooltipR = 0xF5 / 255.0;
inline constexpr double kTooltipG = 0xF5 / 255.0;
inline constexpr double kTooltipB = 0xB5 / 255.0;

inline constexpr double kButtonHiR = 0xF7 / 255.0;
inline constexpr double kButtonHiG = 0xF7 / 255.0;
inline constexpr double kButtonHiB = 0xF6 / 255.0;
inline constexpr double kButtonLoR = 0xE0 / 255.0;
inline constexpr double kButtonLoG = 0xE0 / 255.0;
inline constexpr double kButtonLoB = 0xDF / 255.0;
inline constexpr double kButtonEdgeR = 0x9A / 255.0;
inline constexpr double kButtonEdgeG = 0x9A / 255.0;
inline constexpr double kButtonEdgeB = 0x9A / 255.0;

inline constexpr double kFocusR = 0xE6 / 255.0;
inline constexpr double kFocusG = 0x6A / 255.0;
inline constexpr double kFocusB = 0x00 / 255.0;

// Spooky swaps the blue selection chrome for a restrained orange. The focus
// ring is already orange in every look. Classic and SVGA stay on the blues.
inline int &chrome_look()
{
    static int look = 0;
    return look;
}

inline void set_chrome_look(int look)
{
    chrome_look() = look == 2 ? 2 : 0;
}

inline double chrome_select_r()
{
    return chrome_look() == 2 ? 176.0 / 255.0 : kSelectR;
}
inline double chrome_select_g()
{
    return chrome_look() == 2 ? 96.0 / 255.0 : kSelectG;
}
inline double chrome_select_b()
{
    return chrome_look() == 2 ? 32.0 / 255.0 : kSelectB;
}
inline double chrome_select_hi_r()
{
    return chrome_look() == 2 ? 220.0 / 255.0 : kSelectHiR;
}
inline double chrome_select_hi_g()
{
    return chrome_look() == 2 ? 156.0 / 255.0 : kSelectHiG;
}
inline double chrome_select_hi_b()
{
    return chrome_look() == 2 ? 88.0 / 255.0 : kSelectHiB;
}
inline double chrome_select_edge_r()
{
    return chrome_look() == 2 ? 120.0 / 255.0 : kSelectEdgeR;
}
inline double chrome_select_edge_g()
{
    return chrome_look() == 2 ? 64.0 / 255.0 : kSelectEdgeG;
}
inline double chrome_select_edge_b()
{
    return chrome_look() == 2 ? 24.0 / 255.0 : kSelectEdgeB;
}
inline double chrome_hover_r()
{
    return chrome_look() == 2 ? 236.0 / 255.0 : kHoverR;
}
inline double chrome_hover_g()
{
    return chrome_look() == 2 ? 210.0 / 255.0 : kHoverG;
}
inline double chrome_hover_b()
{
    return chrome_look() == 2 ? 180.0 / 255.0 : kHoverB;
}

inline constexpr const char *kUiFont = "Sans 13";
inline constexpr const char *kBodyFont = "Sans 14";
inline constexpr const char *kTitleFont = "Sans Bold 18";
inline constexpr const char *kMenuFont = "Sans 13";

} // namespace theme
