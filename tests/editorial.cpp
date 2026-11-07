// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#include "civic.hpp"
#include "placement_hint.hpp"
#include "theme.hpp"

#include "micropolis.h"

#include <cstdio>
#include <string>

namespace {

int fail(const char *message)
{
    std::fprintf(stderr, "%s\n", message);
    return 1;
}

int word_at(const int *words, int x, int y)
{
    if (x < 0 || y < 0 || x >= 120 || y >= 100) {
        return 0;
    }
    return words[y * 120 + x];
}

} // namespace

int main()
{
    int land[120 * 100] = {};
    auto at = [&](int x, int y) { return word_at(land, x, y); };
    const PlaceForecast road = forecast_tile(TOOL_ROAD, 4, 4, -1, 20000, true, at);
    if (road.mark != PlaceMark::Ready || road.cost != 10 || road.tag != "Road $10") {
        return fail("a road on dirt should read Road $10");
    }
    land[4 * 120 + 4] = RIVER;
    const PlaceForecast open_water = forecast_tile(TOOL_ROAD, 4, 4, -1, 20000, true, at);
    if (open_water.mark != PlaceMark::Blocked || open_water.tag != "No bridge anchor") {
        return fail("water without an anchor should say so");
    }
    land[4 * 120 + 5] = ROADS | BULLBIT | BURNBIT;
    const PlaceForecast bridge = forecast_tile(TOOL_ROAD, 4, 4, -1, 20000, true, at);
    if (bridge.mark != PlaceMark::Caution || bridge.cost != 50 || bridge.tag != "Water: bridge cost") {
        std::fprintf(stderr, "bridge mark %d cost %d tag %s\n", static_cast<int>(bridge.mark), bridge.cost,
                     bridge.tag.c_str());
        return fail("a bridge should be marked with its extra cost");
    }
    const PlaceForecast poor = forecast_tile(TOOL_ROAD, 4, 4, -1, 20, true, at);
    if (poor.mark != PlaceMark::Short || poor.tag != "Need $50") {
        return fail("a bridge the city cannot afford should say what it needs");
    }
    land[4 * 120 + 4] = 0;
    land[4 * 120 + 5] = 0;
    land[8 * 120 + 8] = WOODS | BULLBIT;
    const PlaceForecast zone = forecast_tile(TOOL_RESIDENTIAL, 9, 9, -1, 20000, false, at);
    if (zone.mark != PlaceMark::Blocked || zone.tag != "Bulldoze first" || zone.width != 3) {
        return fail("a zone on trees without auto-bulldoze should say bulldoze first");
    }
    const PlaceForecast cleared = forecast_tile(TOOL_RESIDENTIAL, 9, 9, -1, 20000, true, at);
    if (cleared.mark != PlaceMark::Ready || cleared.cost != 101) {
        std::fprintf(stderr, "cleared cost %d tag %s\n", cleared.cost, cleared.tag.c_str());
        return fail("auto-bulldoze should add a dollar to the zone");
    }
    const StrokeForecast line = forecast_stroke(TOOL_ROAD, 2, 6, 7, 6, -1, 20000, true, at);
    if (line.tiles != 6 || line.cost != 60 || line.summary != "6 tiles \u00b7 $60") {
        std::fprintf(stderr, "stroke tiles %d cost %d summary %s\n", line.tiles, line.cost, line.summary.c_str());
        return fail("a six-tile road should project $60");
    }
    if (short_build_reason("Bulldoze that area first.") != "Bulldoze first") {
        return fail("the receipt should shorten the bulldoze sentence");
    }

    civic::Ledger ledger;
    const auto first = civic::note_population(ledger, 2000, 1901, 0, true);
    if (first.size() != 1 || first[0] != civic::Town || !ledger.hit[civic::Town].earned) {
        return fail("2,000 people should earn the town milestone once");
    }
    if (!civic::note_population(ledger, 2500, 1901, 1, true).empty()) {
        return fail("the town milestone was awarded twice");
    }
    const auto city = civic::note_population(ledger, 10000, 1904, 5, true);
    if (city.size() != 1 || city[0] != civic::City) {
        return fail("10,000 people should earn the city milestone");
    }
    if (civic::note_powered(ledger, true, 1901, 2, false).size() != 0 || !ledger.hit[civic::Powered].earned) {
        return fail("a silent catch-up should record power without a card");
    }
    civic::Ledger fresh;
    if (civic::note_balanced(fresh, 0, 1902, 0).size() != 1 || civic::note_balanced(fresh, -1, 1903, 0).size() != 0) {
        return fail("only a non-negative year earns the balanced milestone");
    }
    int votes[civic::kProblems] = {10, 4, 3, 2, 20, 1, 0};
    civic::remember_year(fresh, votes, 1500);
    votes[4] = 12;
    if (civic::note_traffic(fresh, votes[4], 1903, 0).size() != 1) {
        return fail("a lower traffic complaint should be noticed");
    }
    const std::string encoded = civic::encode_ledger(fresh);
    civic::Ledger decoded;
    if (!civic::decode_ledger(encoded, decoded) || !decoded.hit[civic::Balanced].earned || !decoded.have_prior ||
        decoded.prior_population != 1500 || decoded.prior_votes[4] != 20) {
        return fail("the ledger did not round-trip");
    }
    const civic::Advice crime = civic::advice_for(0);
    if (std::string(crime.body).find("does not prove") == std::string::npos || crime.layer != 3 || crime.alt_layer != 7) {
        return fail("crime advice should explain uncertainty and offer both maps");
    }
    const civic::YearCard estimated =
        civic::make_year_card(20000, 18000, true, true, 0, civic::Ledger{}, nullptr, nullptr, -1, -1, 0, false);
    if (estimated.opening_label.find("estimate") == std::string::npos ||
        estimated.closing_label.find("estimate") == std::string::npos) {
        return fail("estimated figures must say they are estimates");
    }
    int now_votes[civic::kProblems] = {21, 4, 3, 2, 12, 1, 0};
    const civic::YearCard collected =
        civic::make_year_card(20000, 21400, true, false, 1800, fresh, now_votes, "Crime", 21, 0, 5, true);
    if (collected.closing_label.find("when collected") == std::string::npos ||
        collected.population.find("1,500") == std::string::npos || collected.improvement.find("not a proven cause") == std::string::npos ||
        collected.problem.find("Crime") == std::string::npos) {
        std::fprintf(stderr, "close '%s' pop '%s' imp '%s'\n", collected.closing_label.c_str(),
                     collected.population.c_str(), collected.improvement.c_str());
        return fail("the year card should separate collected results from the poll");
    }
    if (collected.population.find("\u2192") == std::string::npos) {
        return fail("population change should show both years");
    }

    theme::set_chrome_look(0);
    if (theme::enhanced_chrome()) {
        return fail("Classic chrome must stay on the existing materials");
    }
    theme::set_chrome_look(1);
    if (!theme::enhanced_chrome() || theme::chrome_select_r() != 0.0 || theme::chrome_select_b() != 170.0 / 255.0) {
        return fail("SVGA chrome should use VGA blue");
    }
    theme::set_chrome_look(2);
    if (!theme::enhanced_chrome() || theme::chrome_select_r() != 170.0 / 255.0 || theme::chrome_select_g() != 85.0 / 255.0) {
        return fail("Spooky chrome should use VGA orange");
    }
    theme::set_chrome_look(0);
    return 0;
}
