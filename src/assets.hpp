// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#pragma once

#include <string>
#include <vector>

// Places the game looks for images/ and res/. Packaged builds search the
// install tree and LUNDUKE_CITY_ASSETS. They do not search the process's
// current directory or a source-tree path.
std::vector<std::string> asset_candidates();

// Directory that contains images/ and res/ from the vendored Micropolis assets.
// Empty if the tree cannot be found.
std::string asset_root();

// asset_root() + "/" + relative, or empty if the root or file is missing.
std::string asset_file(const std::string &relative);

// Shown when tiles.png cannot be found. Names every path that was searched.
std::string asset_missing_text(const std::vector<std::string> &searched);
