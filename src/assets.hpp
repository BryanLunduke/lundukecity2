// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#pragma once

#include <string>
#include <vector>

// Places the game looks for images/ and res/. Search order is the
// LUNDUKE_CITY_ASSETS override, the configured install directory, then
// exe-relative install and source-tree layouts (share/ next to bin/, and
// third_party/ above the executable). The process's current directory is
// never searched, and the lookup does not require a directory named build.
std::vector<std::string> asset_candidates();

// Directory that contains images/ and res/ from the vendored Micropolis assets.
// Empty if the tree cannot be found.
std::string asset_root();

// asset_root() + "/" + relative, or empty if the root or file is missing.
std::string asset_file(const std::string &relative);

// Original UI art (toolbar icons). Search order is the install directory
// share/lunduke-city-2, then exe-relative share/ and data/.
std::string ui_data_file(const std::string &relative);

// Shown when tiles.png cannot be found. Names every path that was searched.
std::string asset_missing_text(const std::vector<std::string> &searched);
