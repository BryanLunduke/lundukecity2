// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

#pragma once

#include <string>

// Scenario difficulty tag shown on the picker. Display only: it does not
// change the simulator, starting funds, tax tables, or the deadline.
//
// The tag is derived from the catalog goal sentence and the deadline in
// years:
//
//   Hard   — five years or fewer, or a six-to-ten-year goal that is a
//            survival clock (the sentence starts with "Keep " or contains
//            "despite"). A short disaster or "keep the city a Metropolis
//            until…" goal lands here.
//   Medium — other six-to-ten-year tasks (bring crime or traffic down,
//            raise a score with a mid-length clock), or a long (>10 year)
//            survival/keep goal.
//   Easy   — more than ten years, and a growth or task goal rather than a
//            keep/despite clock. Dullsville's thirty-year run to a
//            Metropolis is the typical case.
//
// Deadline years alone would treat every five-year scenario the same.
// The keep/despite test is what separates a short survival clock from a
// mid-length cleanup task, without reading the scenario's name.

enum class ScenarioDifficulty { Easy, Medium, Hard };

inline bool scenario_goal_is_survival(const char *goal)
{
    if (goal == nullptr || goal[0] == '\0') {
        return false;
    }
    const std::string text(goal);
    if (text.compare(0, 5, "Keep ") == 0) {
        return true;
    }
    return text.find("despite") != std::string::npos;
}

inline ScenarioDifficulty scenario_difficulty_from(int years, const char *goal)
{
    // No deadline is a tutorial or an open-ended task, not a short clock.
    if (years <= 0) {
        return ScenarioDifficulty::Easy;
    }
    const bool survival = scenario_goal_is_survival(goal);
    if (years <= 5) {
        return ScenarioDifficulty::Hard;
    }
    if (years <= 10) {
        return survival ? ScenarioDifficulty::Hard : ScenarioDifficulty::Medium;
    }
    return survival ? ScenarioDifficulty::Medium : ScenarioDifficulty::Easy;
}

inline const char *scenario_difficulty_tag(ScenarioDifficulty difficulty)
{
    switch (difficulty) {
    case ScenarioDifficulty::Easy:
        return "Easy";
    case ScenarioDifficulty::Medium:
        return "Medium";
    case ScenarioDifficulty::Hard:
        return "Hard";
    }
    return "Medium";
}

inline const char *scenario_difficulty_tag(int years, const char *goal)
{
    return scenario_difficulty_tag(scenario_difficulty_from(years, goal));
}

inline std::string scenario_years_label(int years)
{
    if (years <= 0) {
        return "No deadline";
    }
    if (years == 1) {
        return "1 year";
    }
    return std::to_string(years) + " years";
}
