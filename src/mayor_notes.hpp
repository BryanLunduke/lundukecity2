// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

#pragma once

#include "civic.hpp"

#include <string>

// Mid-year mayor's notes: at most about one per quarter, and only when
// the evaluation's top complaint is real (at least 10% of the poll, and
// someone lives in the city). The copy is civic::advice_for.

inline constexpr int kMayorNoteGapMonths = 3;
inline constexpr int kMayorNoteMinVotes = 10;

inline int mayor_note_stamp(int year, int month)
{
    if (month < 0) {
        month = 0;
    }
    if (month > 11) {
        month = 11;
    }
    return year * 12 + month;
}

inline bool mayor_note_due(int stamp, int last_stamp)
{
    if (last_stamp < 0) {
        return true;
    }
    return stamp - last_stamp >= kMayorNoteGapMonths;
}

inline bool mayor_note_has_complaint(int kind, int votes, bool no_residents)
{
    if (no_residents || kind < 0 || kind >= civic::kProblems) {
        return false;
    }
    return votes >= kMayorNoteMinVotes;
}

inline std::string mayor_note_text(int kind)
{
    const civic::Advice advice = civic::advice_for(kind);
    std::string text = "Mayor's note: ";
    if (advice.title != nullptr && advice.title[0] != '\0') {
        text += advice.title;
    }
    if (advice.tip != nullptr && advice.tip[0] != '\0') {
        text += ". ";
        text += advice.tip;
    }
    return text;
}

inline bool mayor_note_is(const std::string &text)
{
    return text.compare(0, 14, "Mayor's note: ") == 0;
}
