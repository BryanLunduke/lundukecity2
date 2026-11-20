// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

// Regressions for the second November 18 review (nov18b) that the
// special-building tests do not already cover.

#include "bindings.hpp"
#include "city_session.hpp"
#include "dialog_copy.hpp"
#include "inspect_report.hpp"
#include "special_buildings.hpp"
#include "tools.hpp"

#include "micropolis.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

int fail(int code, const std::string &message)
{
    std::fprintf(stderr, "%s\n", message.c_str());
    return code;
}

CitySession::NewCitySpec flat_spec(const char *name, int level)
{
    CitySession::NewCitySpec spec;
    spec.name = name;
    spec.seed = 12;
    spec.seed_was_set = true;
    spec.difficulty = level;
    spec.island = CitySession::kTerrainOff;
    spec.rivers = CitySession::kTerrainOff;
    spec.lakes = CitySession::kTerrainOff;
    spec.trees = CitySession::kTerrainOff;
    return spec;
}

// #4: a new city opens at 7/6/5% and its Budget shows no tax warning.
int check_default_tax()
{
    const int expect[] = {7, 6, 5};
    for (int level = CitySession::kLevelEasy; level <= CitySession::kLevelHard; ++level) {
        CitySession city;
        city.new_city(flat_spec("Tax Town", level));
        if (city.tax() != expect[level] || CitySession::default_tax(level) != expect[level]) {
            std::fprintf(stderr, "level %d tax %d\n", level, city.tax());
            return fail(1, "a new city did not open at its level's default tax");
        }
        if (tax_warning(city.tax(), city.difficulty()).level != 0) {
            std::fprintf(stderr, "level %d note '%s'\n", level, tax_warning(city.tax(), level).note.c_str());
            return fail(2, "a new city's default tax shows a Budget warning");
        }
        if (tax_warning(city.tax() + 1, level).level == 0) {
            return fail(3, "the default tax is not the highest rate without a warning");
        }
    }
    return 0;
}

int index_named(const char *name)
{
    for (int i = 0; i < special_building_count(); ++i) {
        if (std::string(special_building(i).name) == name) {
            return i;
        }
    }
    return -1;
}

void run_ticks(CitySession &city, int ticks)
{
    city.set_simulation_paused(false);
    for (int i = 0; i < ticks; ++i) {
        city.tick();
        if (city.budget_pending()) {
            city.commit_pending_budget();
        }
        (void)city.take_budget_request();
    }
}

// A powered casino on a road, with nobody living anywhere: the Inspect
// card says Idle and gives one note.
int check_idle_card()
{
    CitySession city;
    city.new_city(flat_spec("Idle Town", CitySession::kLevelEasy));
    city.set_auto_bulldoze(true);
    city.set_disasters(false);
    city.use_tool(TOOL_COALPOWER, 50, 50);
    city.drag_tool(TOOL_WIRE, 51, 53, 55, 53);
    city.drag_tool(TOOL_ROAD, 54, 57, 58, 57);
    const int casino = index_named("Casino");
    if (casino < 0 || !city.place_special(casino, 56, 55)) {
        return fail(10, "could not place the casino for the idle check");
    }
    run_ticks(city, 400);
    const CitySession::InspectFacts facts = city.inspect_facts(56, 55);
    const inspect_report::Report report = inspect_report::make(facts.facts);
    if (facts.facts.power != 1 || !facts.facts.access) {
        std::fprintf(stderr, "power %d access %d\n", facts.facts.power, static_cast<int>(facts.facts.access));
        return fail(11, "the idle-check casino has no power or no road");
    }
    if (report.status != "Idle" || facts.notes.size() != 1 ||
        facts.notes[0] != "No income: no residents within 16 tiles") {
        std::fprintf(stderr, "status '%s' notes %zu\n", report.status.c_str(), facts.notes.size());
        for (const std::string &note : facts.notes) {
            std::fprintf(stderr, "  note '%s'\n", note.c_str());
        }
        return fail(12, "an income building with no residents is not Idle with one note");
    }
    return 0;
}

bool has_note(const std::vector<std::string> &notes, const std::string &want)
{
    for (const std::string &note : notes) {
        if (note == want) {
            return true;
        }
    }
    return false;
}

void dump_notes(const char *what, const std::vector<std::string> &notes)
{
    std::fprintf(stderr, "%s:\n", what);
    for (const std::string &note : notes) {
        std::fprintf(stderr, "  '%s'\n", note.c_str());
    }
}

// #8: the landmarks without income say what they do (or why they don't).
int check_landmark_notes()
{
    CitySession city;
    city.new_city(flat_spec("Note Town", CitySession::kLevelEasy));
    city.set_auto_bulldoze(true);
    city.set_disasters(false);
    city.use_tool(TOOL_COALPOWER, 50, 50);
    city.drag_tool(TOOL_WIRE, 51, 53, 55, 53);
    city.drag_tool(TOOL_ROAD, 54, 58, 62, 58);
    if (!city.place_special(index_named("Library"), 56, 55) || !city.place_special(index_named("Prison"), 59, 55) ||
        !city.place_special(index_named("Big Park"), 70, 70) || !city.place_special(index_named("Zoo"), 80, 70) ||
        !city.place_special(index_named("Fountain"), 90, 70)) {
        return fail(20, "could not place the landmarks for the note check");
    }
    run_ticks(city, 400);
    const std::vector<std::string> library = city.inspect_facts(56, 55).notes;
    if (!has_note(library, "Land value +19% within 6 tiles") || !has_note(library, "Crime \u22129% within 8 tiles")) {
        dump_notes("library", library);
        return fail(21, "the powered library card does not list its land value and crime effects");
    }
    const std::vector<std::string> prison = city.inspect_facts(59, 55).notes;
    if (!has_note(prison, "Crime \u221216% within 8 tiles") || !has_note(prison, "Takes 15% off crime across the city") ||
        !has_note(prison, "Land value \u22126% within 6 tiles")) {
        dump_notes("prison", prison);
        return fail(22, "the working prison card does not list its effects");
    }
    const std::vector<std::string> park = city.inspect_facts(70, 70).notes;
    if (park.size() != 1 || park[0] != "Land value +19% within 6 tiles") {
        dump_notes("big park", park);
        return fail(23, "the Big Park card does not give its one effect line");
    }
    const std::vector<std::string> zoo = city.inspect_facts(80, 70).notes;
    if (zoo.size() != 1 || zoo[0] != "No effect without power") {
        dump_notes("zoo", zoo);
        return fail(24, "the unpowered zoo card does not say it does nothing");
    }
    const std::vector<std::string> fountain = city.inspect_facts(90, 70).notes;
    if (!has_note(fountain, "Land value +6% within 4 tiles") || !has_note(fountain, "Fountains close together add less")) {
        dump_notes("fountain", fountain);
        return fail(25, "the fountain card does not give its effect lines");
    }
    // No road: the prison says what it is missing.
    for (int x = 54; x <= 62; ++x) {
        city.use_tool(TOOL_BULLDOZER, x, 58);
    }
    const std::vector<std::string> cut_off = city.inspect_facts(59, 55).notes;
    if (cut_off.size() != 1 || cut_off[0] != "No effect until it has power and a road") {
        dump_notes("prison without a road", cut_off);
        return fail(26, "the prison with no road does not say why it does nothing");
    }
    return 0;
}

// #9: tiles past the atlas get a civic colour, not the salmon fallback.
int check_minimap_colours()
{
    const int civic[] = {kSpecialBodyTile, SPECIAL_ZOO, SPECIAL_PARK, SPECIAL_LIBRARY, SPECIAL_MAYOR, SPECIAL_CASINO,
                         SPECIAL_AMUSEMENT};
    for (const int tile : civic) {
        double r = 0;
        double g = 0;
        double b = 0;
        if (!special_minimap_rgb(tile, r, g, b) || r > 0.6 || g > 0.6 || b > 0.6 || (r == 0.80 && g == 0.50)) {
            std::fprintf(stderr, "tile %d -> %d %.2f %.2f %.2f\n", tile, static_cast<int>(special_minimap_rgb(tile, r, g, b)), r,
                         g, b);
            return fail(30, "a special tile has no civic minimap colour");
        }
    }
    double r = 0;
    double g = 0;
    double b = 0;
    if (special_minimap_rgb(DIRT, r, g, b) || special_minimap_rgb(SPECIAL_PRISON, r, g, b)) {
        return fail(31, "dirt or the prison centre left the atlas colour");
    }
    return 0;
}

// #11: the Mayor's House has a factory key and its hint does not say the
// cap twice.
int check_mayor_key_and_hint()
{
    const int act = key_act_from_id("mayor");
    if (act < KeyTool0 || factory_key(act) != "shift+m" || chord_label(factory_key(act)) != "Shift+M") {
        return fail(40, "the Mayor's House has no Shift+M factory key");
    }
    for (int i = 0; i < KeyActCount; ++i) {
        if (i != act && factory_key(i) == factory_key(act)) {
            return fail(41, "Shift+M is already another action's factory key");
        }
    }
    const ControlBinds binds = default_binds();
    if (key_act_for_token(binds, "shift+m") != act || key_act_for_token(binds, "m") == act) {
        return fail(42, "Shift+M does not pick the Mayor's House");
    }
    for (int i = 0; i < kToolCount; ++i) {
        const ToolDef *tool = tool_by_index(i);
        if (tool != nullptr && std::string(tool->name) == "Mayor's House" &&
            std::string(tool->hint).find("One per city") != std::string::npos) {
            return fail(43, "the Mayor's House hint still says 'One per city'");
        }
    }
    return 0;
}

// #12: player text uses US spelling.
int check_spelling()
{
    for (int i = 0; i < special_building_count(); ++i) {
        if (std::string(special_building(i).effect).find("neighbour") != std::string::npos) {
            return fail(50, "a building description says 'neighbourhood'");
        }
    }
    return 0;
}

// #13: the mouse and pad phrases start with the toolbar name.
int check_click_phrases()
{
    for (int i = 0; i < kToolCount; ++i) {
        const ToolDef *tool = tool_by_index(i);
        if (tool == nullptr || tool->engine_id == TOOL_QUERY) {
            continue;
        }
        const std::string name = std::string(tool->name) + ": ";
        const std::string click = tool_click_text(tool);
        const std::string press = tool_press_text(tool, "A");
        if (click.compare(0, name.size(), name) != 0 || press.compare(0, name.size(), name) != 0) {
            std::fprintf(stderr, "tool '%s': click '%s' press '%s'\n", tool->name, click.c_str(), press.c_str());
            return fail(60, "a click or press phrase does not use the toolbar name");
        }
    }
    return 0;
}

} // namespace

int main()
{
    if (const int r = check_default_tax()) {
        return r;
    }
    if (const int r = check_idle_card()) {
        return r;
    }
    if (const int r = check_landmark_notes()) {
        return r;
    }
    if (const int r = check_minimap_colours()) {
        return r;
    }
    if (const int r = check_mayor_key_and_hint()) {
        return r;
    }
    if (const int r = check_spelling()) {
        return r;
    }
    if (const int r = check_click_phrases()) {
        return r;
    }
    std::puts("review_nov18b ok");
    return 0;
}
