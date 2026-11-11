// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#pragma once

#include <string>

// Short notices for Micropolis message numbers (text.h MessageNumber).
// Wording is original to Lunduke City; the engine itself only sends numbers.
std::string message_for_number(int number);

// One-line query readout. tile_category is the 1-based index from
// doShowZoneStatus (29 means clear land; the engine's dirt path reports that).
// The five scores are stri.202 category ids plus one (population 1–4,
// land value 5–8, crime 9–12, pollution 13–16, growth 17–20).
std::string zone_status_text(int tile_category, int population, int land_value,
                             int crime, int pollution, int growth);

// Disaster notices get a stronger bar and a red edge flash. Earthquake and
// explosion also shake. Routine demands ("More roads needed") do not.
bool notice_is_disaster(const std::string &text);
bool notice_shakes(const std::string &text);

// 0 general, 1 disaster, 2 money, 3 people, 4 power, 5 road. Message-list marks.
int notice_mark(const std::string &text);
