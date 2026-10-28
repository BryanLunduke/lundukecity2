// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#pragma once

#include <string>

// Lunduke City 2. The Meson project version and the in-app version are 2.0.0.
// The binary installs beside the original Lunduke City.
#ifndef LUNDUKE_CITY_PACKAGE_VERSION
#define LUNDUKE_CITY_PACKAGE_VERSION "2.0.0"
#endif

inline constexpr const char kPackageVersion[] = LUNDUKE_CITY_PACKAGE_VERSION;
inline constexpr const char kReleaseTrack[] = "2.0.0";
inline constexpr const char kProductName[] = "Lunduke City 2";

inline std::string about_text()
{
    return std::string("Version ") + kPackageVersion +
           "\nA city-building game for LCOS, Steam Deck, and Windows.\n"
           "\nOriginal design by Will Wright. First published by Maxis.\n"
           "Micropolis engine by Don Hopkins.\n"
           "GPL release from Electronic Arts.\n"
           "Map tiles, sprites, and sounds from that release.\n"
           "Hard fork of Lunduke City.\n"
           "The Lunduke Journal - https://lunduke.com\n"
           "\nIndependent work. Not affiliated with Electronic Arts.\n"
           "GPL-3.0-or-later. See NOTICE and COPYING.";
}
