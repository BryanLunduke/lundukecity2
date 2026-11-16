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

// The editor's wording, verbatim (including "Electronic arts" in the third
// paragraph).
inline std::string about_text()
{
    return std::string("Lunduke City version ") + kReleaseTrack +
           ".\n"
           "\nOriginal game design by Will Wright, first published by Maxis.\n"
           "\nSome artwork and engine based on GPL'd release of Micropolis, by Electronic arts.\n"
           "\nNot affiliated with Electronic Arts.\n"
           "\nLunduke City 2 is copyright Bryan Lunduke and is available under the GPL.";
}
