// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

// Regressions for the November 18 review fixes that are not covered by the
// special-building, scenario, and Data Center tests: the tax warning, the
// crime average after the special-building pass, the blackout message, the
// Bern goal, the Year in Review milestones after a load, the evaluation after
// a save and load, New City funds, and How to play.

#include "city_session.hpp"
#include "civic.hpp"
#include "controls_page.hpp"
#include "dialog_copy.hpp"
#include "overlay_map.hpp"
#include "special_buildings.hpp"

#include "micropolis.h"
#include "text.h"

#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace {

int fail(int code, const std::string &message)
{
    std::fprintf(stderr, "%s\n", message.c_str());
    return code;
}

struct Heard {
    std::vector<int> messages;
    int won = 0;
    int lost = 0;
};

void listen(Micropolis &sim, Heard &heard)
{
    sim.callbackData = &heard;
    sim.callbackHook = [](Micropolis *, void *data, const char *name, const char *, va_list args) {
        Heard *out = static_cast<Heard *>(data);
        if (name == nullptr) {
            return;
        }
        const std::string what(name);
        if (what == "winGame") {
            ++out->won;
        } else if (what == "loseGame") {
            ++out->lost;
        } else if (what == "update") {
            const char *kind = va_arg(args, const char *);
            if (kind != nullptr && std::string(kind) == "message") {
                out->messages.push_back(va_arg(args, int));
            }
        }
    };
}

bool heard_message(const Heard &heard, int number)
{
    for (int message : heard.messages) {
        if (message == number) {
            return true;
        }
    }
    return false;
}

void crime_everywhere(Micropolis *sim)
{
    for (int x = 0; x < WORLD_W; x += sim->crimeRateMap.MAP_BLOCKSIZE) {
        for (int y = 0; y < WORLD_H; y += sim->crimeRateMap.MAP_BLOCKSIZE) {
            if (sim->landValueMap.worldGet(x, y) > 0) {
                sim->crimeRateMap.worldSet(x, y, 200);
            }
        }
    }
}

int check_tax_warning()
{
    // The warning follows the engine's tax table: growth slows where the
    // push turns negative and people leave from -40.
    const int slows[3] = {8, 7, 6};
    const int leaves[3] = {9, 8, 7};
    for (int level = 0; level < 3; ++level) {
        if (CitySession::tax_growth_effect(7 - level, level) != 0 ||
            CitySession::tax_growth_effect(leaves[level], level) != -40) {
            return fail(1, "the tax table moved");
        }
        const TaxWarning fine = tax_warning(slows[level] - 1, level);
        const TaxWarning slow = tax_warning(slows[level], level);
        const TaxWarning leave = tax_warning(leaves[level], level);
        if (fine.level != 0 || !fine.note.empty() || slow.level != 1 || leave.level != 2 ||
            slow.slows_from != slows[level] || slow.leaves_from != leaves[level] ||
            tax_warning(20, level).level != 2 || tax_warning(0, level).level != 0) {
            std::fprintf(stderr, "level %d: %d %d %d from %d/%d\n", level, fine.level, slow.level, leave.level,
                         slow.slows_from, slow.leaves_from);
            return fail(2, "the tax warning does not match the tax table");
        }
    }
    if (tax_warning(7, CitySession::kLevelHard).note !=
        "Too high for Hard: people and businesses move away. 5% or lower keeps the city growing.") {
        std::fprintf(stderr, "%s\n", tax_warning(7, CitySession::kLevelHard).note.c_str());
        return fail(3, "Hard at 7% should say people leave");
    }
    if (tax_warning(8, CitySession::kLevelEasy).note != "Growth slows at this rate on Easy. At 9% people start to leave.") {
        std::fprintf(stderr, "%s\n", tax_warning(8, CitySession::kLevelEasy).note.c_str());
        return fail(4, "Easy at 8% should say growth slows");
    }
    return 0;
}

int check_crime_average()
{
    // A casino or prison changes the crime map after the scan. The average
    // the evaluation reads must include that change.
    Micropolis sim;
    sim.resourceDir = LUNDUKE_CITY_ASSET_DIR "/res";
    if (!sim.loadScenario(SC_DETROIT)) {
        return fail(10, "could not load Detroit");
    }
    sim.enableDisasters = false;
    sim.specialCrimeFn = crime_everywhere;
    sim.simPaused = false;
    sim.setSpeed(3);
    sim.crimeAverage = 0;
    for (int i = 0; i < 2000 && sim.crimeAverage == 0; ++i) {
        sim.simTick();
    }
    if (sim.crimeAverage != 200) {
        std::fprintf(stderr, "crime average %d\n", sim.crimeAverage);
        return fail(11, "the crime average left out the special-building pass");
    }
    return 0;
}

int check_blackout_message()
{
    // Unpowered zones with no plant at all are not a blackout.
    Micropolis sim;
    sim.generateSomeCity(4);
    sim.setAutoGoto(false);
    Heard heard;
    listen(sim, heard);
    sim.cityTime = 32;
    sim.unpoweredZoneCount = 5;
    sim.poweredZoneCount = 0;
    sim.coalPowerPop = 0;
    sim.nuclearPowerPop = 0;
    sim.microNuclearPop = 0;
    sim.sendMessages();
    if (heard_message(heard, MESSAGE_BLACKOUTS_REPORTED)) {
        return fail(20, "blackouts were reported with no power plant");
    }
    heard.messages.clear();
    sim.coalPowerPop = 1;
    sim.sendMessages();
    if (!heard_message(heard, MESSAGE_BLACKOUTS_REPORTED)) {
        return fail(21, "a plant with mostly unpowered zones did not report blackouts");
    }
    return 0;
}

int check_bern()
{
    Micropolis sim;
    sim.resourceDir = LUNDUKE_CITY_ASSET_DIR "/res";
    if (!sim.loadScenario(SC_BERN)) {
        return fail(30, "could not load Bern");
    }
    sim.setAutoGoto(false);
    Heard heard;
    listen(sim, heard);
    sim.censusSnapshotValid = true;
    sim.snapResPop = 4000; // 80,000 residents
    sim.snapComPop = 0;
    sim.snapIndPop = 0;
    sim.snapDataCenterPop = 0;
    sim.trafficAverage = 10;
    sim.doScenarioScore(SC_BERN);
    if (heard.lost != 1 || heard.won != 0) {
        return fail(31, "Bern was won with traffic down but the people gone");
    }
    sim.snapResPop = 4500; // 90,000
    sim.trafficAverage = BERN_TRAFFIC_GOAL;
    sim.doScenarioScore(SC_BERN);
    if (heard.lost != 2 || heard.won != 0) {
        return fail(32, "Bern was won with traffic at 40");
    }
    sim.trafficAverage = BERN_TRAFFIC_GOAL - 1;
    sim.doScenarioScore(SC_BERN);
    if (heard.won != 1) {
        return fail(33, "Bern was lost with 90,000 people and traffic under 40");
    }
    // Data Center jobs do not count toward the 90,000.
    sim.snapResPop = 4000;
    sim.snapComPop = 2 * 30;
    sim.snapIndPop = 2 * 30;
    sim.snapDataCenterPop = 2 * 30;
    sim.doScenarioScore(SC_BERN);
    if (heard.won != 1 || heard.lost != 3) {
        return fail(34, "Data Centers counted toward Bern's population");
    }
    return 0;
}

int check_ledger()
{
    civic::Ledger ledger;
    civic::note_population(ledger, 60000, 1965, 0, false);
    civic::note_powered(ledger, true, 1965, 0, false);
    if (!ledger.hit[civic::Capital].earned || !ledger.hit[civic::Capital].before_start ||
        civic::milestone_date(ledger.hit[civic::Capital]).rfind("By ", 0) != 0) {
        return fail(40, "a milestone already met at load is not marked as before the start");
    }
    civic::Ledger back;
    if (!civic::decode_ledger(civic::encode_ledger(ledger), back) || !back.hit[civic::Capital].before_start ||
        !back.hit[civic::Powered].before_start || back.hit[civic::Capital].year != 1965) {
        return fail(41, "the before-start mark did not survive a save");
    }
    const std::vector<civic::DigestNotice> notes;
    civic::YearDigest digest =
        civic::make_year_digest(19900, 18443, true, false, 60000, ledger, nullptr, "Crime", 31, 0, 0, false, 1965, notes);
    const civic::ReviewText text = civic::make_review_text(digest, 45, 16, 39, false, "Capital", 305, 31);
    if (text.highlights.find("capital") != std::string::npos || text.highlights.find("power") != std::string::npos) {
        std::fprintf(stderr, "%s\n", text.highlights.c_str());
        return fail(42, "the Year in Review listed milestones the city had when it was loaded");
    }
    civic::Ledger played;
    civic::note_population(played, 60000, 1965, 4, true);
    if (played.hit[civic::Capital].before_start) {
        return fail(43, "a milestone earned in play is marked as before the start");
    }
    return 0;
}

int check_text()
{
    if (CitySession::starting_funds(CitySession::kLevelEasy) != 20000 ||
        CitySession::starting_funds(CitySession::kLevelMedium) != 10000 ||
        CitySession::starting_funds(CitySession::kLevelHard) != 5000) {
        return fail(50, "New City shows the wrong starting funds");
    }
    const std::string help = how_to_play_text();
    for (const char *needle : {"Special buildings", "Casinos", "Prison", "Data Center", "Micro Nuclear", "amber"}) {
        if (help.find(needle) == std::string::npos) {
            return fail(51, std::string("How to play does not mention ") + needle);
        }
    }
    const std::string zones = overlay_legend(CitySession::MapLayer::Zones);
    if (zones.find("purple") == std::string::npos) {
        std::fprintf(stderr, "%s\n", zones.c_str());
        return fail(52, "the Zones legend does not name the commercial colour");
    }
    return 0;
}

int check_casino_card()
{
    // A casino opens without a road but earns nothing without one, so its
    // card asks for a road instead of reading "No road needed".
    CitySession city;
    CitySession::NewCitySpec spec;
    spec.name = "Card Town";
    spec.seed = 12;
    spec.seed_was_set = true;
    spec.island = CitySession::kTerrainOff;
    spec.rivers = CitySession::kTerrainOff;
    spec.lakes = CitySession::kTerrainOff;
    spec.trees = CitySession::kTerrainOff;
    city.new_city(spec);
    city.set_auto_bulldoze(true);
    int casino = -1;
    for (int i = 0; i < special_building_count(); ++i) {
        if (std::string(special_building(i).name) == "Casino") {
            casino = i;
        }
    }
    if (casino < 0 || !city.place_special(casino, 60, 50)) {
        return fail(70, "could not place a casino");
    }
    const CitySession::InspectFacts facts = city.inspect_facts(60, 50);
    if (!facts.valid || !facts.facts.needs_road || city.inspect_tile(60, 50).access != "No direct road") {
        std::fprintf(stderr, "access '%s'\n", city.inspect_tile(60, 50).access.c_str());
        return fail(71, "a casino with no road does not ask for one");
    }
    return 0;
}

int check_eval_after_load()
{
    // The Evaluation's change-since-last-year survives save and load, so a
    // recovered or reopened city does not show dashes until next January.
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "lunduke-review-nov18";
    std::filesystem::create_directories(dir);
    const std::string path = (dir / "eval.cty").string();
    std::filesystem::remove(path);
    CitySession session;
    session.new_city("Eval Town", 11);
    session.set_speed(3);
    for (int i = 0; i < 20000 && !session.evaluation().deltas_known; ++i) {
        session.tick();
        if (session.budget_pending()) {
            session.commit_pending_budget();
        }
        (void)session.take_budget_request();
        if ((i & 63) == 0) {
            session.update_evaluation();
        }
    }
    session.update_evaluation();
    if (!session.evaluation().deltas_known) {
        return fail(60, "a year passed without evaluation deltas");
    }
    const int score_delta = session.evaluation().score_delta;
    if (!session.save_city_as(path)) {
        return fail(61, "could not save");
    }
    CitySession back;
    const bool loaded = back.load_city(path);
    back.update_evaluation();
    if (!loaded || !back.evaluation().deltas_known || back.evaluation().score_delta != score_delta) {
        std::fprintf(stderr, "known %d delta %d vs %d\n", back.evaluation().deltas_known ? 1 : 0,
                     back.evaluation().score_delta, score_delta);
        return fail(62, "the evaluation lost its yearly change after a load");
    }
    std::filesystem::remove_all(dir);
    return 0;
}

} // namespace

int main()
{
    if (const int r = check_tax_warning()) {
        return r;
    }
    if (const int r = check_crime_average()) {
        return r;
    }
    if (const int r = check_blackout_message()) {
        return r;
    }
    if (const int r = check_bern()) {
        return r;
    }
    if (const int r = check_ledger()) {
        return r;
    }
    if (const int r = check_text()) {
        return r;
    }
    if (const int r = check_casino_card()) {
        return r;
    }
    if (const int r = check_eval_after_load()) {
        return r;
    }
    std::puts("review_nov18 ok");
    return 0;
}
