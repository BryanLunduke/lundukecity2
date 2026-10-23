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

inline constexpr const char *kUiFont = "Sans 13";
inline constexpr const char *kBodyFont = "Sans 14";
inline constexpr const char *kTitleFont = "Sans Bold 18";
inline constexpr const char *kMenuFont = "Sans 13";

} // namespace theme
