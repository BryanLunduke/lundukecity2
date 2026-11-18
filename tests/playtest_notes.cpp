// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

// The November playtest notes: park fountains read as parks in Inspect, a
// pointer-opened menu highlights nothing until hovered, the Inspect bars
// start after the widest label, and the Year in Review reads plainly.

#include "city_session.hpp"
#include "civic.hpp"
#include "inspect_report.hpp"
#include "menu_model.hpp"

#include "micropolis.h"

#include "tile_kind.hpp"

#include <cstdio>
#include <string>

namespace {

int fail(int code, const char *message)
{
    std::fprintf(stderr, "%s\n", message);
    return code;
}

bool kind_is(int tile, const char *want)
{
    const std::string got = tile_kind::name(tile);
    if (got != want) {
        std::fprintf(stderr, "tile %d reads '%s', want '%s'\n", tile, got.c_str(), want);
        return false;
    }
    return true;
}

} // namespace

int main()
{
    // Note 1: the fountain tiles sat inside the old "Nuclear Plant" range
    // (NUCLEARBASE up to INDBASE2), along with the bridge and radar tiles.
    for (int t = FOUNTAIN; t < FOUNTAIN + 4; ++t) {
        if (!kind_is(t, "Park")) {
            return fail(10, "a fountain tile is not a park");
        }
    }
    for (int t = WOODS2; t <= WOODS5; ++t) {
        if (!kind_is(t, "Park")) {
            return fail(11, "a park-tool tree is not a park");
        }
    }
    if (!kind_is(NUCLEARBASE, "Nuclear Plant") || !kind_is(LASTZONE, "Nuclear Plant") ||
        !kind_is(NUKESWIRL1, "Nuclear Plant") || !kind_is(HBRDG0, "Road") ||
        !kind_is(VBRDG3, "Road") || !kind_is(RADAR0, "Airport") || !kind_is(HOSPITALBASE, "Hospital") ||
        !kind_is(CHURCHBASE, "Church") || !kind_is(CHURCH1BASE, "Church") || !kind_is(WOODS, "Trees") ||
        !kind_is(DIRT, "Clear") || !kind_is(RIVER, "Water") || !kind_is(RUBBLE, "Rubble") ||
        !kind_is(FIRE, "Fire") || !kind_is(COALSMOKE1, "Coal Plant") || !kind_is(FOOTBALLGAME1, "Stadium") ||
        !kind_is(INDBASE2, "Industrial") || !kind_is(TINYEXP, "Rubble")) {
        return fail(12, "a tile range reads as the wrong kind");
    }

    // A real park: lay parks until one comes up as a fountain, then Inspect it.
    {
        CitySession city;
        city.new_city("Fountains", 7);
        city.set_disasters(false);
        bool found = false;
        for (int y = 4; y < CitySession::kWorldH - 4 && !found; ++y) {
            for (int x = 4; x < CitySession::kWorldW - 4 && !found; ++x) {
                if ((city.map_value(x, y) & LOMASK) != DIRT) {
                    continue;
                }
                city.use_tool(TOOL_PARK, x, y);
                const int tile = city.map_value(x, y) & LOMASK;
                if (tile < FOUNTAIN || tile >= FOUNTAIN + 4) {
                    continue;
                }
                found = true;
                const CitySession::InspectFacts facts = city.inspect_facts(x, y);
                const inspect_report::Report report = inspect_report::make(facts.facts);
                if (facts.kind_name != "Park" || report.status != "Park") {
                    std::fprintf(stderr, "fountain reads '%s' / '%s'\n", facts.kind_name.c_str(), report.status.c_str());
                    return fail(13, "a fountain does not inspect as a park");
                }
            }
        }
        if (!found) {
            return fail(14, "no park came up as a fountain");
        }
    }

    // Note 2: a click opens the menu with nothing highlighted; keys bring the
    // cursor back; keyboard opens still show the first row.
    {
        MenuNav menu;
        menu.open_by_pointer(2);
        const int first2 = menu.cursor;
        for (int i = 0; i < menu_table::column_count(2); ++i) {
            if (menu.shows_cursor_at(i)) {
                return fail(20, "a pointer-opened menu highlights a row");
            }
        }
        menu.handle_key(AccelKey::Down, 0, 0);
        if (!menu.shows_cursor_at(first2)) {
            return fail(21, "Down in a pointer-opened menu should show the first row");
        }
        menu.close();
        menu.open_by_pointer(1);
        const int first1 = menu.cursor;
        menu.handle_key(AccelKey::Return, 0, 0);
        if (menu.activated != Action::None || !menu.open() || !menu.shows_cursor_at(first1)) {
            return fail(22, "Return in a pointer-opened menu acted on an unseen row");
        }
        menu.close();
        menu.open_by_pointer(1);
        menu.handle_key(AccelKey::Up, 0, 0);
        if (menu.shows_cursor_at(first1) || !menu.cursor_shown) {
            return fail(23, "Up in a pointer-opened menu should show the last row");
        }
        menu.close();
        menu.open_first();
        if (!menu.cursor_shown || !menu.shows_cursor_at(menu.cursor)) {
            return fail(24, "a keyboard-opened menu should show its first row");
        }
        menu.close();
        menu.open_by_pointer(0);
        menu.handle_key(AccelKey::Right, 0, 0);
        if (!menu.cursor_shown) {
            return fail(25, "moving across menus with keys should show the cursor");
        }
    }

    // Note 3: the bars start after the widest label plus the gap, the same
    // for every row, at desktop and handheld scale.
    for (double scale : {1.0, 1.5, 2.0}) {
        const double label = 74.0 * scale; // "Land value"
        const double value = 34.0 * scale; // "100%"
        const double gap = 12.0 * scale;
        const inspect_report::BarLayout layout = inspect_report::bar_layout(300.0 * scale, label, value, gap);
        if (layout.bar_offset < label + gap - 0.01 || layout.bar_offset + layout.bar_width + gap + value > 300.0 * scale + 0.01 ||
            layout.bar_width <= 0.0) {
            return fail(30, "the bars overlap their labels or values");
        }
    }

    // Note 4: the Year in Review in plain words.
    {
        civic::Ledger ledger;
        civic::award(ledger, civic::Town, 1955, 0, nullptr);
        civic::award(ledger, civic::City, 1955, 0, nullptr);
        civic::award(ledger, civic::Capital, 1955, 3, nullptr);
        civic::award(ledger, civic::Powered, 1955, 1, nullptr);
        ledger.have_prior = true;
        ledger.prior_population = 68000;
        std::vector<civic::DigestNotice> notes;
        civic::DigestNotice reached;
        reached.text = "The population has reached 50,000.";
        reached.year = 1955;
        reached.month = 3;
        notes.push_back(reached);
        civic::DigestNotice boom;
        boom.text = "An explosion has been detected.";
        boom.year = 1955;
        boom.month = 6;
        for (int i = 0; i < 4; ++i) {
            notes.push_back(boom);
        }
        civic::YearDigest digest = civic::make_year_digest(19900, 18443, true, false, 69620, ledger, nullptr, "Crime",
                                                           31, 0, 0, false, 1956, notes);
        digest.opening_funds = 19900;
        digest.closing_funds = 18443;
        const civic::ReviewText text = civic::make_review_text(digest, 45, 16, 39, false, "Capital", 305, 31);
        if (text.funds != "Start of year $19,900\nEnd of year $18,443\nLost $1,457 this year") {
            std::fprintf(stderr, "%s\n", text.funds.c_str());
            return fail(40, "funds should read start, end and the loss");
        }
        if (text.people != "Population 69,620\nUp 1,620 since last year\nHomes 45% \u00B7 Shops 16%\nIndustry 39%") {
            std::fprintf(stderr, "%s\n", text.people.c_str());
            return fail(41, "people should read population, change and the mix");
        }
        if (text.standing != "City size: Capital\nScore 305 out of 1,000\n31% approve of you") {
            std::fprintf(stderr, "%s\n", text.standing.c_str());
            return fail(42, "standing should read size, score and approval");
        }
        if (text.highlights.find("Milestone:") != std::string::npos || text.highlights.find("1955") != std::string::npos ||
            text.highlights.find("Grew into a capital (50,000 people)") == std::string::npos ||
            text.highlights.find("The first homes got power") == std::string::npos ||
            text.highlights.find("reached 50,000") != std::string::npos ||
            text.highlights.find("Grew into a town") != std::string::npos ||
            text.highlights.find("Grew into a city") != std::string::npos ||
            text.highlights.find("explosion") != std::string::npos) {
            std::fprintf(stderr, "%s\n", text.highlights.c_str());
            return fail(43, "highlights should be plain, dateless, good news only and not repeat the milestone");
        }
        if (text.challenges.find("Top complaint: Crime (31% of citizens)") != 0 ||
            text.challenges.find("An explosion has been detected (4 times)") == std::string::npos) {
            std::fprintf(stderr, "%s\n", text.challenges.c_str());
            return fail(44, "challenges should name the top complaint plainly, then the disasters");
        }
        civic::YearDigest first = civic::make_year_digest(500, 500, true, false, 10, civic::Ledger{}, nullptr, nullptr,
                                                          -1, -1, 0, false, 1901, {});
        const civic::ReviewText fresh = civic::make_review_text(first, 0, 0, 0, true, "Village", 0, 0);
        if (fresh.people != "Population 10" || fresh.funds.find("Broke even") == std::string::npos ||
            fresh.challenges != "No big complaints this year.") {
            std::fprintf(stderr, "%s | %s | %s\n", fresh.people.c_str(), fresh.funds.c_str(), fresh.challenges.c_str());
            return fail(45, "a first year should omit the change and read plainly");
        }
        // A collapse with no one left to poll (the review's monster year):
        // the blackout, the monster and the demand nag are challenges, and
        // Challenges never says "No big complaints".
        {
            civic::Ledger prior;
            prior.have_prior = true;
            prior.prior_population = 640;
            civic::award(prior, civic::Balanced, 1901, 6, nullptr);
            std::vector<civic::DigestNotice> bad;
            for (const char *line : {"Blackouts reported. Check power.", "A monster has been sighted.",
                                     "More commercial zones needed."}) {
                civic::DigestNotice n;
                n.text = line;
                n.year = 1901;
                n.month = 4;
                bad.push_back(n);
            }
            civic::YearDigest crash = civic::make_year_digest(15000, 15100, true, false, 0, prior, nullptr, nullptr, -1, -1,
                                                              0, false, 1902, bad);
            const civic::ReviewText fell = civic::make_review_text(crash, 0, 0, 0, true, "Village", 0, 0);
            if (fell.highlights.find("Blackouts") != std::string::npos ||
                fell.highlights.find("monster") != std::string::npos ||
                fell.highlights.find("zones needed") != std::string::npos ||
                fell.challenges.find("No big complaints") != std::string::npos ||
                fell.challenges.find("Blackouts reported") == std::string::npos ||
                fell.challenges.find("A monster has been sighted") == std::string::npos ||
                fell.challenges.find("More commercial zones needed") == std::string::npos ||
                fell.challenges.find("Population down 640") == std::string::npos) {
                std::fprintf(stderr, "H: %s\nC: %s\n", fell.highlights.c_str(), fell.challenges.c_str());
                return fail(46, "bad news must sit under Challenges, never Highlights");
            }
        }
    }
    std::printf("playtest notes ok\n");
    return 0;
}
