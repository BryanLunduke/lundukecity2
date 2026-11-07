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
    if (look == 1 || look == 2) {
        chrome_look() = look;
        return;
    }
    chrome_look() = 0;
}

// SVGA and Spooky share one flat material. Classic keeps Clearlooks.
inline bool enhanced_chrome()
{
    return chrome_look() == 1 || chrome_look() == 2;
}

// VGA entries. SVGA stays blue. Spooky stays orange. Classic is unchanged.
inline double chrome_select_r()
{
    if (chrome_look() == 1) {
        return 0.0 / 255.0;
    }
    if (chrome_look() == 2) {
        return 170.0 / 255.0;
    }
    return kSelectR;
}
inline double chrome_select_g()
{
    if (chrome_look() == 1) {
        return 0.0 / 255.0;
    }
    if (chrome_look() == 2) {
        return 85.0 / 255.0;
    }
    return kSelectG;
}
inline double chrome_select_b()
{
    if (chrome_look() == 1) {
        return 170.0 / 255.0;
    }
    if (chrome_look() == 2) {
        return 0.0 / 255.0;
    }
    return kSelectB;
}
inline double chrome_select_hi_r()
{
    if (chrome_look() == 1) {
        return 85.0 / 255.0;
    }
    if (chrome_look() == 2) {
        return 255.0 / 255.0;
    }
    return kSelectHiR;
}
inline double chrome_select_hi_g()
{
    if (chrome_look() == 1) {
        return 85.0 / 255.0;
    }
    if (chrome_look() == 2) {
        return 190.0 / 255.0;
    }
    return kSelectHiG;
}
inline double chrome_select_hi_b()
{
    if (chrome_look() == 1) {
        return 255.0 / 255.0;
    }
    if (chrome_look() == 2) {
        return 0.0 / 255.0;
    }
    return kSelectHiB;
}
inline double chrome_select_edge_r()
{
    if (chrome_look() == 1) {
        return 0.0 / 255.0;
    }
    if (chrome_look() == 2) {
        return 113.0 / 255.0;
    }
    return kSelectEdgeR;
}
inline double chrome_select_edge_g()
{
    if (chrome_look() == 1) {
        return 0.0 / 255.0;
    }
    if (chrome_look() == 2) {
        return 56.0 / 255.0;
    }
    return kSelectEdgeG;
}
inline double chrome_select_edge_b()
{
    if (chrome_look() == 1) {
        return 113.0 / 255.0;
    }
    if (chrome_look() == 2) {
        return 0.0 / 255.0;
    }
    return kSelectEdgeB;
}
inline double chrome_hover_r()
{
    if (chrome_look() == 1) {
        return 85.0 / 255.0;
    }
    if (chrome_look() == 2) {
        return 255.0 / 255.0;
    }
    return kHoverR;
}
inline double chrome_hover_g()
{
    if (chrome_look() == 1) {
        return 85.0 / 255.0;
    }
    if (chrome_look() == 2) {
        return 190.0 / 255.0;
    }
    return kHoverG;
}
inline double chrome_hover_b()
{
    if (chrome_look() == 1) {
        return 255.0 / 255.0;
    }
    if (chrome_look() == 2) {
        return 0.0 / 255.0;
    }
    return kHoverB;
}

// Flat body, one highlight, one dark edge. All three are VGA table entries.
inline void chrome_body(double &r, double &g, double &b)
{
    r = 182.0 / 255.0;
    g = 182.0 / 255.0;
    b = 182.0 / 255.0;
}
inline void chrome_highlight(double &r, double &g, double &b)
{
    r = 227.0 / 255.0;
    g = 227.0 / 255.0;
    b = 227.0 / 255.0;
}
inline void chrome_shadow(double &r, double &g, double &b)
{
    r = 85.0 / 255.0;
    g = 85.0 / 255.0;
    b = 85.0 / 255.0;
}

// Goal banner and the message line. Classic and SVGA keep the pale tooltip
// yellow. Spooky uses the orange title-bar family, snapped to exact VGA
// entries: dark brown (65, 32, 0), orange (255, 190, 0), edge (170, 85, 0).
inline void banner_fill(double &r, double &g, double &b)
{
    if (chrome_look() == 2) {
        r = 65.0 / 255.0;
        g = 32.0 / 255.0;
        b = 0.0;
        return;
    }
    if (chrome_look() == 1) {
        r = 255.0 / 255.0;
        g = 255.0 / 255.0;
        b = 85.0 / 255.0;
        return;
    }
    r = kTooltipR;
    g = kTooltipG;
    b = kTooltipB;
}

inline void banner_ink(double &r, double &g, double &b)
{
    if (chrome_look() == 2) {
        r = 255.0 / 255.0;
        g = 190.0 / 255.0;
        b = 0.0;
        return;
    }
    if (chrome_look() == 1) {
        r = 0.0;
        g = 0.0;
        b = 0.0;
        return;
    }
    r = kTextR;
    g = kTextG;
    b = kTextB;
}

inline void banner_edge(double &r, double &g, double &b)
{
    if (chrome_look() == 2) {
        r = 170.0 / 255.0;
        g = 85.0 / 255.0;
        b = 0.0;
        return;
    }
    if (chrome_look() == 1) {
        r = 113.0 / 255.0;
        g = 113.0 / 255.0;
        b = 0.0;
        return;
    }
    r = kMenuFrameR;
    g = kMenuFrameG;
    b = kMenuFrameB;
}

inline constexpr const char *kUiFont = "Sans 13";
inline constexpr const char *kBodyFont = "Sans 14";
inline constexpr const char *kTitleFont = "Sans Bold 18";
inline constexpr const char *kMenuFont = "Sans 13";

} // namespace theme
