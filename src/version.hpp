// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
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

// The editor's approved wording (Nov 2026), verbatim apart from the line
// breaks: "Lunduke City 2, version ...", the copyright line, the
// Micropolis/Electronic Arts credit and modified-version notice, the Will
// Wright/Maxis credit with "Not affiliated with or endorsed by Electronic
// Arts", the GPL v3-or-later/NOTICE/NO WARRANTY line, and the source URL.
// ascii swaps the copyright sign and the en dash for (C) and -, for a font
// that lacks them.
inline std::string about_text(bool ascii = false)
{
    const std::string c = ascii ? "(C)" : "\u00A9";
    const std::string dash = ascii ? "-" : "\u2013";
    return std::string("Lunduke City 2, version ") + kPackageVersion +
           "\n"
           "Copyright " + c + " 2026 Bryan Lunduke.\n"
           "\nSimulation engine and classic artwork based on the GPL release of Micropolis, Copyright " + c +
           " 1989" + dash + "2007 Electronic Arts Inc. This is a modified version, not the original program.\n"
           "\nOriginal game design by Will Wright, first published by Maxis. Not affiliated with or endorsed by "
           "Electronic Arts.\n"
           "\nFree software under the GNU GPL v3 or later, with additional terms (see NOTICE). There is NO "
           "WARRANTY, to the extent permitted by law.\n"
           "\nSource code: github.com/BryanLunduke/lundukecity2";
}
