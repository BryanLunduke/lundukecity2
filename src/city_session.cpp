// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#include <cstdint>
#include "city_session.hpp"

#include "assets.hpp"
#include "atomic_write.hpp"
#include "city_tail.hpp"
#include "messages.hpp"
#include "scenario_sidecar.hpp"
#include "special_buildings.hpp"

#include "micropolis.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <sys/stat.h>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <memory>
#include <sstream>

namespace {

// Thousands separators for a count such as population or score. No currency
// symbol: a city of 17,940 people is not $17,940.
std::string grouped_count(std::int64_t value)
{
    const bool neg = value < 0;
    std::uint64_t mag = static_cast<std::uint64_t>(neg ? -value : value);
    std::string digits = std::to_string(mag);
    std::string grouped;
    int count = 0;
    for (int i = static_cast<int>(digits.size()) - 1; i >= 0; --i) {
        if (count > 0 && count % 3 == 0) {
            grouped.push_back(',');
        }
        grouped.push_back(digits[static_cast<std::size_t>(i)]);
        ++count;
    }
    std::reverse(grouped.begin(), grouped.end());
    if (neg) {
        grouped.insert(grouped.begin(), '-');
    }
    return grouped;
}

// Funds, taxes, and department costs. The sign sits in front of the dollar.
std::string with_commas(std::int64_t value)
{
    const bool neg = value < 0;
    return (neg ? "-$" : "$") + grouped_count(neg ? -value : value);
}

const char *kMonths[] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
};

int percent_of_fraction(float fraction)
{
    int n = static_cast<int>(fraction * 100.0f + 0.5f);
    if (n < 0) {
        n = 0;
    }
    if (n > 100) {
        n = 100;
    }
    return n;
}

Quad live_population_count(const Micropolis &sim)
{
    // Phase 0 clears the live counts. The snapshot is the last finished scan.
    // The counters are wide. A 16-bit history sample can be saturated; this
    // count is the one the status line and Evaluation show.
    const Quad res = sim.censusSnapshotValid ? sim.snapResPop : sim.resPop;
    const Quad com = sim.censusSnapshotValid ? sim.snapComPop : sim.comPop;
    const Quad ind = sim.censusSnapshotValid ? sim.snapIndPop : sim.indPop;
    return (res + (com + ind) * 8) * 20;
}

CityClass live_city_class(Quad population)
{
    CityClass city_class = CC_VILLAGE;
    if (population > 2000) {
        city_class = CC_TOWN;
    }
    if (population > 10000) {
        city_class = CC_CITY;
    }
    if (population > 50000) {
        city_class = CC_CAPITAL;
    }
    if (population > 100000) {
        city_class = CC_METROPOLIS;
    }
    if (population > 500000) {
        city_class = CC_MEGALOPOLIS;
    }
    return city_class;
}

const char *city_class_name(CityClass city_class)
{
    switch (city_class) {
    case CC_VILLAGE:
        return "Village";
    case CC_TOWN:
        return "Town";
    case CC_CITY:
        return "City";
    case CC_CAPITAL:
        return "Capital";
    case CC_METROPOLIS:
        return "Metropolis";
    case CC_MEGALOPOLIS:
        return "Megalopolis";
    default:
        return "Settlement";
    }
}

struct ScenarioEntry {
    CitySession::ScenarioDef def;
    const char *filename;
};

// Names, years, funds, and filenames are the engine's loadScenario() table.
// Goals are the classic Micropolis win conditions (Metropolis, traffic
// below 80, crime below 60, city score above 500) and the scoreWaitTable
// deadlines, in years.
const ScenarioEntry kScenarios[] = {
    {{SC_DULLSVILLE, "Dullsville", 1900, "A quiet city that needs growth.",
      "Turn this city into a Metropolis (more than 100,000 people) within 30 years.", 30},
     "snro.111"},
    {{SC_SAN_FRANCISCO, "San Francisco", 1906, "An earthquake strikes the city.",
      "Keep the city a Metropolis (over 100,000) until 1911 despite the quake.", 5},
     "snro.222"},
    {{SC_HAMBURG, "Hamburg", 1944, "Fire spreads through the city.",
      "Keep the city a Metropolis (over 100,000) until 1949 despite the fire.", 5},
     "snro.333"},
    {{SC_BERN, "Bern", 1965, "Traffic is choking the streets.",
      "Bring average traffic below 80 within 10 years.", 10},
     "snro.444"},
    {{SC_TOKYO, "Tokyo", 1957, "A monster is approaching.",
      "Keep the city score above 500 until 1962.", 5},
     "snro.555"},
    {{SC_DETROIT, "Detroit", 1972, "Crime is out of control.",
      "Bring average crime below 60 within 10 years.", 10},
     "snro.666"},
    {{SC_BOSTON, "Boston", 2010, "A nuclear meltdown is coming.",
      "Keep the city score above 500 until 2015.", 5},
     "snro.777"},
    {{SC_RIO, "Rio de Janeiro", 2047, "Flooding threatens the city.",
      "Keep the city score above 500 until 2057.", 10},
     "snro.888"},
};

static_assert(sizeof(kScenarios) / sizeof(kScenarios[0]) == CitySession::kScenarioCount,
              "scenario catalog size");
static_assert(SC_DULLSVILLE == 1 && SC_RIO == 8, "scenario ids");
static_assert(static_cast<int>(LEVEL_EASY) == CitySession::kLevelEasy, "easy level");
static_assert(static_cast<int>(LEVEL_MEDIUM) == CitySession::kLevelMedium, "medium level");
static_assert(static_cast<int>(LEVEL_HARD) == CitySession::kLevelHard, "hard level");
static_assert(static_cast<int>(HISTORY_TYPE_RES) == static_cast<int>(CitySession::HistorySeries::Residential),
              "residential history");
static_assert(static_cast<int>(HISTORY_TYPE_COM) == static_cast<int>(CitySession::HistorySeries::Commercial),
              "commercial history");
static_assert(static_cast<int>(HISTORY_TYPE_IND) == static_cast<int>(CitySession::HistorySeries::Industrial),
              "industrial history");
static_assert(static_cast<int>(HISTORY_TYPE_MONEY) == static_cast<int>(CitySession::HistorySeries::CashFlow),
              "cash-flow history");
static_assert(static_cast<int>(HISTORY_TYPE_CRIME) == static_cast<int>(CitySession::HistorySeries::Crime),
              "crime history");
static_assert(static_cast<int>(HISTORY_TYPE_POLLUTION) == static_cast<int>(CitySession::HistorySeries::Pollution),
              "pollution history");
static_assert(static_cast<int>(HISTORY_SCALE_SHORT) == static_cast<int>(CitySession::HistoryScale::Short),
              "short history");
static_assert(static_cast<int>(HISTORY_SCALE_LONG) == static_cast<int>(CitySession::HistoryScale::Long),
              "long history");
static_assert(HISTORY_COUNT == CitySession::kHistoryPoints, "history length");
static_assert(CVP_CRIME == 0 && CVP_FIRE == 6 && CVP_NUMPROBLEMS == 7, "problem ids");

} // namespace

const CitySession::ScenarioDef &CitySession::scenario_def(int index)
{
    if (index < 0 || index >= kScenarioCount) {
        return kScenarios[0].def;
    }
    return kScenarios[index].def;
}

// Held out of the header so the UI translation units do not include the engine.
struct CitySession::Engine {
    Micropolis sim;
};

#ifdef LUNDUKE_CITY_TEST_HOOKS
int hostile_review_session_probe(CitySession &session, int op)
{
    Micropolis &sim = session.engine_->sim;
    if (op == 1) {
        sim.budgetAwaitingAccept = true;
        return 0;
    }
    if (op == 2) {
        // A finished census, and the next simulator step is phase 0.
        sim.resPop = 40;
        sim.comPop = 2;
        sim.indPop = 1;
        sim.censusSnapshotValid = false;
        sim.liveCensusComplete = true;
        sim.phaseCycle = 0;
        sim.setSpeed(3);
        return 0;
    }
    if (op == 3) {
        sim.roadFund = 1000;
        sim.roadSpend = 1000;
        sim.roadPercent = 1.0f;
        sim.fireFund = 0;
        sim.policeFund = 0;
        sim.updateFundEffects();
        return static_cast<int>(sim.roadEffect);
    }
    if (op == 4) {
        return static_cast<int>(sim.roadEffect);
    }
    if (op == 5) {
        sim.doWinGame();
        return session.speed();
    }
    if (op == 6) {
        sim.doLoseGame();
        return session.speed();
    }
    if (op == 8) {
        sim.autoBudget = false;
        sim.taxFund = 2000;
        sim.roadFund = 400;
        sim.policeFund = 0;
        sim.fireFund = 0;
        sim.roadPercent = 1.0f;
        sim.policePercent = 0.0f;
        sim.firePercent = 0.0f;
        sim.setFunds(8000);
        sim.budgetAnchorFunds = 8000;
        sim.budgetAnchorValid = true;
        sim.doBudgetNow(false);
        return sim.budgetAwaitingAccept ? static_cast<int>(sim.totalFunds) : -1;
    }
    if (op == 9) {
        sim.autoBudget = false;
        sim.taxFund = 0;
        sim.roadFund = 5000;
        sim.policeFund = 0;
        sim.fireFund = 0;
        sim.roadPercent = 1.0f;
        sim.policePercent = 0.0f;
        sim.firePercent = 0.0f;
        sim.setFunds(100);
        sim.doBudgetNow(false);
        sim.roadPercent = 1.0f;
        return sim.budgetAwaitingAccept ? 0 : 1;
    }
    if (op == 10) {
        sim.map[10][10] = RIVER;
        sim.map[9][10] = ROADS | BULLBIT | BURNBIT;
        sim.map[11][10] = ROADS | BULLBIT | BURNBIT;
        return 0;
    }
    if (op == 11) {
        sim.map[20][20] = RIVER;
        sim.map[19][20] = DIRT;
        sim.map[21][20] = DIRT;
        sim.map[20][19] = DIRT;
        sim.map[20][21] = DIRT;
        return 0;
    }
    if (op == 12) {
        // Rail on both banks, and a clear land tile for the dry-land price.
        // The other axis is dirt so the span uses the banks that were set.
        sim.map[10][12] = RIVER;
        sim.map[9][12] = HRAIL | BULLBIT | BURNBIT;
        sim.map[11][12] = HRAIL | BULLBIT | BURNBIT;
        sim.map[10][11] = DIRT;
        sim.map[10][13] = DIRT;
        sim.map[30][16] = RIVER;
        sim.map[30][15] = VRAIL | BULLBIT | BURNBIT;
        sim.map[30][17] = VRAIL | BULLBIT | BURNBIT;
        sim.map[29][16] = DIRT;
        sim.map[31][16] = DIRT;
        sim.map[5][5] = DIRT;
        return 0;
    }
    if (op == 13) {
        // Conductive tiles the wire tool will anchor to. Horizontal banks
        // must not be HPOWER; vertical banks must not be VPOWER.
        sim.map[10][14] = RIVER;
        sim.map[9][14] = LHPOWER | CONDBIT | BURNBIT | BULLBIT;
        sim.map[11][14] = LHPOWER | CONDBIT | BURNBIT | BULLBIT;
        sim.map[10][13] = DIRT;
        sim.map[10][15] = DIRT;
        sim.map[14][10] = RIVER;
        sim.map[14][9] = HPOWER | CONDBIT | BURNBIT | BULLBIT;
        sim.map[14][11] = HPOWER | CONDBIT | BURNBIT | BULLBIT;
        sim.map[13][10] = DIRT;
        sim.map[15][10] = DIRT;
        sim.map[6][6] = DIRT;
        return 0;
    }
    if (op == 14) {
        sim.autoBudget = false;
        sim.taxFund = 0;
        sim.roadFund = 5000;
        sim.policeFund = 0;
        sim.fireFund = 4000;
        sim.roadPercent = 1.0f;
        sim.policePercent = 1.0f;
        sim.firePercent = 1.0f;
        sim.setFunds(100);
        sim.doBudgetNow(false);
        return sim.budgetAwaitingAccept ? static_cast<int>(sim.totalFunds) : -1;
    }
    if (op == 15) {
        sim.budgetAwaitingAccept = false;
        sim.taxFund = 2000;
        sim.taxReceiptKnown = true;
        return static_cast<int>(sim.taxFund);
    }
    if (op == 20) {
        // The book before the first January must use projectBudget, the
        // same function collectTax uses, on the finished census.
        const bool snap = sim.censusSnapshotValid;
        const short roads = snap ? sim.snapRoadTotal : sim.roadTotal;
        const short rails = snap ? sim.snapRailTotal : sim.railTotal;
        const short police = snap ? sim.snapPolicePop : sim.policeStationPop;
        const short fire = snap ? sim.snapFirePop : sim.fireStationPop;
        const Quad res = snap ? sim.snapResPop : sim.resPop;
        const Quad com = snap ? sim.snapComPop : sim.comPop;
        const Quad ind = snap ? sim.snapIndPop : sim.indPop;
        const float normalized = static_cast<float>(res) / 8.0f;
        const Quad pop = static_cast<Quad>(normalized + static_cast<float>(com) + static_cast<float>(ind));
        const Micropolis::BudgetBases bases =
            sim.projectBudget(roads, rails, police, fire, pop, sim.landValueAverage, sim.cityTax);
        const CitySession::BudgetBook book = session.budget();
        if (!book.estimates || book.taxes_known || book.road_need != static_cast<std::int64_t>(bases.road) ||
            book.police_need != static_cast<std::int64_t>(bases.police) || book.fire_need != static_cast<std::int64_t>(bases.fire) ||
            book.taxes != static_cast<std::int64_t>(bases.tax)) {
            return 0;
        }
        if (!snap || sim.roadTotal != sim.snapRoadTotal || sim.railTotal != sim.snapRailTotal) {
            return 0;
        }
        const Micropolis::BudgetBases january = sim.projectBudget(
            sim.roadTotal, sim.railTotal, sim.policeStationPop, sim.fireStationPop, pop, sim.landValueAverage,
            sim.cityTax);
        if (book.road_need != static_cast<std::int64_t>(january.road)) {
            return 0;
        }
        // 2 when the census has a real road bill. 1 when the formula
        // matches and the map simply has no roads yet.
        return book.road_need > 0 ? 2 : 1;
    }
    if (op == 21) {
        // The next park roll is a fountain. initWillStuff() reseeds from
        // the clock, so a search for that 1-in-5 tile is not deterministic.
        sim.primeRandom(1);
        return 0;
    }
    if (op == 16) {
        // Taxes in hand are what make the road cut affordable. Cash alone
        // would cut the same request much harder.
        sim.autoBudget = false;
        sim.taxFund = 2000;
        sim.taxReceiptKnown = true;
        sim.roadFund = 5000;
        sim.policeFund = 0;
        sim.fireFund = 1000;
        sim.roadPercent = 1.0f;
        sim.policePercent = 1.0f;
        sim.firePercent = 1.0f;
        sim.setFunds(100);
        sim.doBudgetNow(false);
        return sim.budgetAwaitingAccept ? static_cast<int>(sim.totalFunds) : -1;
    }
    sim.cityAssessedValue = 424242;
    const bool ran = session.note_evaluation_month(session.game_month_index() + 50);
    if (ran || sim.cityAssessedValue != 424242) {
        std::fprintf(stderr, "ran %d assessed %ld\n", ran ? 1 : 0, static_cast<long>(sim.cityAssessedValue));
        return 1;
    }
    return 0;
}
#endif

CitySession::CitySession()
    : engine_(new Engine)
{
    engine_->sim.callbackHook = [](Micropolis * /*sim*/, void *data, const char *name,
                                   const char *params, va_list args) {
        auto *self = static_cast<CitySession *>(data);
        if (self == nullptr) {
            return;
        }
        self->on_callback(name, params, args);
    };
    engine_->sim.callbackData = this;
    engine_->sim.specialLandFn = boost_special_land_value;
    engine_->sim.setEnableSound(sound_enabled_);
}

CitySession::~CitySession()
{
    engine_->sim.callbackHook = nullptr;
    engine_->sim.callbackData = nullptr;
    engine_->sim.specialLandFn = nullptr;
    delete engine_;
}

void CitySession::set_listener(Listener listener)
{
    listener_ = std::move(listener);
}

void CitySession::notify()
{
    if (listener_) {
        listener_();
    }
}

void CitySession::post_message(std::string text)
{
    message_ = std::move(text);
    ++message_serial_;
    notify();
}

void CitySession::mark_dirty()
{
    if (ready_) {
        dirty_ = true;
    }
}

bool CitySession::name_is_usable(const std::string &name)
{
    return acceptable_city_name(name);
}

void CitySession::on_callback(const char *name, const char *params, va_list args)
{
    struct CallbackGuard {
        bool &flag;
        bool outer;
        explicit CallbackGuard(bool &flag) : flag(flag), outer(!flag) { flag = true; }
        ~CallbackGuard()
        {
            if (outer) {
                flag = false;
            }
        }
    } guard(in_callback_);

    const std::string which = name != nullptr ? name : "";

    auto take_string = [&]() -> const char * {
        const char *s = va_arg(args, char *);
        return s != nullptr ? s : "";
    };
    auto take_int = [&]() { return va_arg(args, int); };

    if (which == "update" && params != nullptr && params[0] == 's') {
        const std::string kind = take_string();
        if (kind == "message") {
            int number = 0;
            int mx = 0;
            int my = 0;
            bool has_place = false;
            const char *p = params + 1;
            if (*p == 'd') {
                number = take_int();
                ++p;
            }
            if (*p == 'd') {
                mx = take_int();
                ++p;
                has_place = true;
            }
            if (*p == 'd') {
                my = take_int();
                ++p;
            }
            if (*p == 'd') {
                (void)take_int();
                ++p;
            }
            if (*p == 'b') {
                (void)take_int();
            }
            const std::string text = message_for_number(number);
            post_message(text);
            remember_notice(text, mx, my, has_place);
        }
        return;
    }

    if (which == "showZoneStatus") {
        int category = 0;
        int s0 = 0, s1 = 0, s2 = 0, s3 = 0, s4 = 0;
        const char *p = params != nullptr ? params : "";
        if (*p == 'd') {
            category = take_int();
            ++p;
        }
        if (*p == 'd') {
            s0 = take_int();
            ++p;
        }
        if (*p == 'd') {
            s1 = take_int();
            ++p;
        }
        if (*p == 'd') {
            s2 = take_int();
            ++p;
        }
        if (*p == 'd') {
            s3 = take_int();
            ++p;
        }
        if (*p == 'd') {
            s4 = take_int();
            ++p;
        }
        int qx = 0;
        int qy = 0;
        if (*p == 'd') {
            qx = take_int();
            ++p;
        }
        if (*p == 'd') {
            qy = take_int();
        }
        const std::string special = special_query_at(&engine_->sim, qx, qy);
        message_ = special.empty() ? zone_status_text(category, s0, s1, s2, s3, s4) : special;
        query_x_ = qx;
        query_y_ = qy;
        query_has_tile_ = Micropolis::testBounds(qx, qy);
        if (query_has_tile_) {
            const int raw = engine_->sim.map[qx][qy];
            const bool powered = (raw & PWRBIT) != 0;
            message_ += ", Power: ";
            message_ += powered ? "on" : "off";
            message_ += ", Pollution level: " + std::to_string(layer_value(MapLayer::Pollution, qx, qy));
            message_ += ", Crime level: " + std::to_string(layer_value(MapLayer::Crime, qx, qy));
        }
        ++query_serial_;
        notify();
        return;
    }

    if (which == "makeSound") {
        const char *channel = (params != nullptr && params[0] == 's') ? take_string() : "";
        const char *sound = (params != nullptr && params[0] == 's' && params[1] == 's') ? take_string() : "";
        (void)channel;
        if (sound[0] != '\0') {
            sounds_.emplace_back(sound);
        }
        return;
    }

    if (which == "showBudgetAndWait") {
        budget_requested_ = true;
        notify();
        return;
    }

    if (which == "autoGoto") {
        int x = 0;
        int y = 0;
        const char *p = params != nullptr ? params : "";
        if (*p == 'd') {
            x = take_int();
            ++p;
        }
        if (*p == 'd') {
            y = take_int();
        }
        if (Micropolis::testBounds(x, y)) {
            goto_x_ = x;
            goto_y_ = y;
            goto_pending_ = true;
            notify();
        }
        return;
    }

    if (which == "startEarthquake") {
        quake_strength_ = (params != nullptr && params[0] == 'd') ? take_int() : 0;
        if (quake_strength_ < 1) {
            quake_strength_ = 1;
        }
        notify();
        return;
    }

    if (which == "winGame" || which == "loseGame") {
        // The window is told after the clock is already 0. Remember the
        // speed that was running, once, so Keep playing and the next
        // scenario can put it back.
        if (!outcome_paused_) {
            speed_before_outcome_ = speed_;
        }
        outcome_paused_ = true;
        scenario_outcome_ = which == "winGame" ? 1 : -1;
        scenario_result_ = scenario_outcome_;
        speed_ = 0;
        engine_->sim.setSpeed(0);
        mark_dirty();
        notify();
        return;
    }

    if (which == "didntLoadCity" || which == "didntSaveCity") {
        const char *msg = (params != nullptr && params[0] == 's') ? take_string() : "";
        std::string text = std::string(which == "didntLoadCity" ? "Could not load " : "Could not save ") + msg;
        if (which == "didntSaveCity" && !engine_->sim.saveErrorDetail.empty()) {
            text += ": ";
            text += engine_->sim.saveErrorDetail;
        }
        post_message(std::move(text));
    }
}

void CitySession::new_city(const std::string &name, int seed)
{
    NewCitySpec spec;
    spec.name = name;
    spec.seed = seed;
    // The historical call treats 0 as "choose from the clock".
    spec.seed_was_set = seed != 0;
    new_city(spec);
}

void CitySession::new_city(const NewCitySpec &spec)
{
    if (!spec.name.empty() && !name_is_usable(spec.name)) {
        post_message("Enter a name that is not only spaces.");
        return;
    }
    outcome_paused_ = false;
    ready_ = false;
    Micropolis &sim = engine_->sim;
    // A budget window left open must not charge this city when it closes.
    sim.budgetAwaitingAccept = false;
    // Micropolis::init() (called from the constructor) already ran simInit().
    // simInit() is private and reallocates history buffers, so a new city
    // sets the terrain knobs, lets generateSomeCity() rebuild the map, then
    // applies the difficulty funds. generateMap() reads these fields directly.
    sim.terrainCreateIsland = spec.island;
    sim.terrainCurveLevel = spec.rivers;
    sim.terrainLakeLevel = spec.lakes;
    sim.terrainTreeLevel = spec.trees;
    sim.setCityTax(7);
    sim.setAutoBudget(true);
    sim.setAutoBulldoze(true);
    sim.setEnableDisasters(true);
    const std::string city = spec.name.empty() ? "New City" : spec.name;
    sim.setCleanCityName(city);
    sim.setSpeed(static_cast<short>(speed_));
    sim.setPasses(1);
    // An explicit seed, including 0, is passed through. Clock/auto is only
    // the unset case (seed 0 and seed_was_set false).
    const int used_seed = (spec.seed == 0 && !spec.seed_was_set)
                              ? static_cast<int>(std::time(nullptr))
                              : spec.seed;
    sim.generateSomeCity(used_seed);
    int level = spec.difficulty;
    if (level < kLevelEasy || level > kLevelHard) {
        level = kLevelEasy;
    }
    sim.setGameLevelFunds(static_cast<GameLevel>(level));
    // generateSomeCity() already ran setValves at the previous city's
    // level. Store this city's level before the next even sim cycle.
    if (sim.miscHist != nullptr && sim.gameLevel >= LEVEL_FIRST && sim.gameLevel <= LEVEL_LAST) {
        sim.miscHist[15] = static_cast<short>(sim.gameLevel);
    }
    sim.setSpeed(static_cast<short>(speed_));
    sim.setEnableSound(sound_enabled_);
    // A new city starts with disasters on. A loaded city reads miscHist[64]:
    // 1 is on, 2 is off, and every other value (including 0) stays on.
    sim.setAutoGoto(true);
    // The last city's road, police, and fire rates must not bill this map.
    sim.budgetAwaitingAccept = false;
    sim.initFundingLevel();
    sim.updateFundEffects();
    sim.evalPreviewValid = false;
    save_path_.clear();
    message_.clear();
    dirty_ = false;
    mark_clean_time();
    fresh_generated_ = true;
    // simInit() leaves cityTime at 50, which is January 1901. generateSomeCity()
    // sets cityTime back to 0. Refresh the calendar from that counter so the
    // birth month cannot stay on the pre-generation date.
    sim.updateDate();
    {
        const Quad time = sim.cityTime < 0 ? static_cast<Quad>(0) : sim.cityTime;
        int month = static_cast<int>((time % 48) >> 2);
        if (month < 0 || month > 11) {
            month = 0;
        }
        birth_month_ = month;
        const int year = static_cast<int>(time / 48) + static_cast<int>(sim.startingYear);
        birth_year_ = year > 0 ? year : static_cast<int>(sim.startingYear);
    }
    eval_month_ = -1;
    eval_month_pending_ = false;
    clear_notices();
    clear_scenario_clock();
    arm_eval_baseline();
    ready_ = true;
    notify();
}

void CitySession::rename_city(const std::string &name)
{
    std::string clean;
    clean.reserve(name.size());
    bool pending_space = false;
    for (unsigned char ch : name) {
        if (ch == ' ' || ch == '\t') {
            if (!clean.empty()) {
                pending_space = true;
            }
            continue;
        }
        if (ch < 32) {
            continue;
        }
        if (pending_space) {
            clean.push_back(' ');
            pending_space = false;
        }
        clean.push_back(static_cast<char>(ch));
        if (clean.size() >= 48) {
            break;
        }
    }
    if (clean.empty()) {
        post_message("Enter a name that is not only spaces.");
        return;
    }
    const std::string before = engine_->sim.cityName;
    engine_->sim.setCleanCityName(clean);
    if (engine_->sim.cityName == before) {
        return;
    }
    dirty_ = true;
    notify();
}

int CitySession::difficulty() const
{
    return static_cast<int>(engine_->sim.gameLevel);
}

std::int64_t CitySession::funds() const
{
    return static_cast<std::int64_t>(engine_->sim.totalFunds);
}

int CitySession::generated_seed() const
{
    return engine_->sim.generatedCitySeed;
}

std::string CitySession::load_problem(const std::string &path)
{
    if (path.empty()) {
        return "The file has no local path.";
    }
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        const int err = errno;
        if (err == ENOENT || err == ENOTDIR) {
            return "The file was not found. It may have been moved or deleted.";
        }
        if (err == EACCES) {
            return "You do not have permission to open this file.";
        }
        return std::string("The file could not be opened: ") + std::strerror(err) + ".";
    }
    if (S_ISDIR(st.st_mode)) {
        return "That is a folder, not a city file.";
    }
    if (!S_ISREG(st.st_mode)) {
        return "That is not a regular file.";
    }
    FILE *f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) {
        const int err = errno;
        if (err == EACCES || err == EPERM) {
            return "You do not have permission to open this file.";
        }
        return std::string("The file could not be opened: ") + std::strerror(err) + ".";
    }
    std::fclose(f);
    // Micropolis city files are at least 27,120 bytes (history + map).
    if (st.st_size == 0) {
        return "The file is empty.";
    }
    if (st.st_size < 27120) {
        return "The file is too short to be a city file (" + std::to_string(static_cast<long long>(st.st_size)) +
               " bytes; a city file is at least 27,120 bytes). It may be damaged or a different kind of file.";
    }
    if (classify_city_path(path) == CityFileKind::AllZero) {
        return "The file is all zeros and is not a city. It may be damaged.";
    }
    return std::string();
}

bool CitySession::load_city(const std::string &path)
{
    message_.clear();
    Micropolis &sim = engine_->sim;
    last_load_error_.clear();
    const std::string problem = load_problem(path);
    if (!problem.empty() || !sim.loadCity(path.c_str())) {
        if (!problem.empty()) {
            last_load_error_ = problem;
        } else if (!sim.loadErrorDetail.empty()) {
            last_load_error_ = "It is not a city file, or it is damaged: " + sim.loadErrorDetail;
        } else {
            last_load_error_ = "The file could not be read as a city. It may be damaged.";
        }
        // The dialog shows the full reason. The message line stays a short
        // sentence so a damaged file does not paste its whole explanation there.
        message_ = "Could not load that city file.";
        notify();
        return false;
    }
    int loaded_speed = static_cast<int>(sim.simSpeed);
    if (loaded_speed < 0) {
        loaded_speed = 0;
    }
    if (loaded_speed > 3) {
        loaded_speed = 3;
    }
    outcome_paused_ = false;
    speed_ = loaded_speed;
    sound_enabled_ = sim.enableSound;
    sim.budgetAwaitingAccept = false;
    sim.evalPreviewValid = false;
    save_path_ = path;
    ready_ = true;
    dirty_ = false;
    fresh_generated_ = false;
    mark_clean_time();
    eval_month_ = -1;
    eval_month_pending_ = false;
    clear_notices();
    clear_scenario_clock();
    read_scenario_sidecar(path);
    recount_population_from_map();
    refresh_loaded_meters();
    arm_eval_baseline();
    if (classify_city_path(path) == CityFileKind::Incomplete) {
        post_message("Loaded a saved city. Part of the City 2 data at the end of the file is missing.");
    } else {
        post_message("Loaded a saved city.");
    }
    return true;
}

bool CitySession::load_scenario(int id)
{
    const ScenarioEntry *entry = nullptr;
    for (const auto &candidate : kScenarios) {
        if (candidate.def.id == id) {
            entry = &candidate;
            break;
        }
    }
    if (entry == nullptr) {
        message_ = "Could not start that scenario.";
        notify();
        return false;
    }

    const std::string root = asset_root();
    const std::string dir = root.empty() ? std::string() : root + "/res";
    const std::string file = dir.empty() ? std::string() : dir + "/" + entry->filename;
    std::ifstream in(file, std::ios::binary);
    in.seekg(0, std::ios::end);
    const auto bytes = in.good() ? static_cast<long>(in.tellg()) : -1L;
    if (bytes != 27120) {
        if (root.empty()) {
            message_ = "Could not start that scenario. The game data files were not found.";
        } else {
            message_ = "Could not start that scenario.\n" + file + " is missing or the wrong size.";
        }
        notify();
        return false;
    }

    Micropolis &sim = engine_->sim;
    // loadScenario() reads snro.* from resourceDir via loadFileDir().
    // A failed second open must keep the city that is already loaded.
    const std::string previous_name = sim.cityName;
    sim.resourceDir = dir;
    if (!sim.loadScenario(static_cast<Scenario>(id))) {
        message_ = "Could not start that scenario.";
        notify();
        return false;
    }
    if (sim.cityName.empty()) {
        sim.setCleanCityName(previous_name);
    }
    sim.budgetAwaitingAccept = false;
    sim.evalPreviewValid = false;
    // loadScenario() starts at Fast. A win or loss has paused this
    // session for the announcement; the next city runs at the speed
    // from before that pause. Any other session speed is kept.
    if (outcome_paused_) {
        speed_ = std::max(0, std::min(3, speed_before_outcome_));
        outcome_paused_ = false;
    }
    sim.setSpeed(static_cast<short>(speed_));
    sim.setEnableSound(sound_enabled_);
    save_path_.clear();
    ready_ = true;
    dirty_ = false;
    fresh_generated_ = false;
    mark_clean_time();
    eval_month_ = -1;
    eval_month_pending_ = false;
    clear_notices();
    clear_scenario_clock();
    recount_population_from_map();
    refresh_loaded_meters();
    arm_eval_baseline();
    post_message(std::string("Playing ") + entry->def.name + ".");
    return true;
}

bool CitySession::save_city_as(const std::string &path)
{
    message_.clear();
    // saveCityAs reports failure through the callback hook, including the
    // operating-system reason captured on the failing call.
    if (!engine_->sim.saveCityAs(path.c_str())) {
        if (message_.rfind("Could not save", 0) != 0) {
            std::string text = "Could not save " + path;
            if (!engine_->sim.saveErrorDetail.empty()) {
                text += ": " + engine_->sim.saveErrorDetail;
            }
            post_message(std::move(text));
        }
        return false;
    }
    save_path_ = path;
    dirty_ = false;
    fresh_generated_ = false;
    mark_clean_time();
    if (!write_scenario_sidecar(path) && engine_->sim.scoreType != SC_NONE) {
        post_message("The city was saved, but the scenario timer could not be stored. Loading this file ends the scenario.");
        return true;
    }
    post_message("City saved.");
    return true;
}

bool CitySession::write_recovery(const std::string &path)
{
    if (!ready_ || path.empty()) {
        return false;
    }
    // saveFile, not saveCityAs: no "saved" callback, no engine file name
    // change, and a waiting tax year stays waiting (commitPending false).
    if (!engine_->sim.saveFile(path.c_str(), false)) {
        return false;
    }
    write_scenario_sidecar(path);
    return true;
}

bool CitySession::load_recovery(const std::string &recovery_path, const std::string &origin_path)
{
    if (!load_city(recovery_path)) {
        return false;
    }
    save_path_ = origin_path;
    engine_->sim.cityFileName = origin_path;
    // Recovered work is unsaved until the player saves it.
    dirty_ = true;
    post_message("Recovered an unsaved city. Save it to keep it.");
    return true;
}

std::string CitySession::change_fingerprint() const
{
    const Micropolis &sim = engine_->sim;
    return std::to_string(static_cast<long long>(sim.cityTime)) + ":" + std::to_string(map_serial()) + ":" +
           std::to_string(static_cast<long long>(sim.totalFunds)) + ":" + std::to_string(dirty_ ? 1 : 0) + ":" +
           std::to_string(budget_pending() ? 1 : 0) + ":" + sim.cityName + ":" +
           std::to_string(static_cast<int>(sim.cityTax));
}

void CitySession::tick()
{
    if (!ready_ || simulation_paused_) {
        return;
    }
    // simTick() steps the simulation and does not cycle animated map
    // tiles. Traffic, fountains, and smokestacks advance on the same
    // tick while the city is running. Pause (speed 0) leaves them still.
    engine_->sim.simTick();
    if (speed_ != 0) {
        engine_->sim.animateTiles();
    }
}

void CitySession::use_tool(int engine_tool, int tile_x, int tile_y)
{
    if (!ready_) {
        return;
    }
    if (engine_tool == kSpecialEngineId) {
        return;
    }
    if (engine_tool == TOOL_BULLDOZER && bulldoze_special_at(&engine_->sim, tile_x, tile_y)) {
        dirty_ = true;
        notify();
        return;
    }
    const ToolResult result = engine_->sim.toolDown(static_cast<EditingTool>(engine_tool),
                                                    static_cast<short>(tile_x), static_cast<short>(tile_y));
    if (result == TOOLRESULT_OK && engine_tool != TOOL_QUERY) {
        dirty_ = true;
    }
    notify();
}

bool CitySession::place_special(int index, int tile_x, int tile_y)
{
    if (!ready_) {
        return false;
    }
    std::string error;
    if (!place_special_building(&engine_->sim, index, tile_x, tile_y, error)) {
        if (!error.empty()) {
            post_message(error);
        }
        return false;
    }
    dirty_ = true;
    notify();
    return true;
}

void CitySession::drag_tool(int engine_tool, int from_x, int from_y, int to_x, int to_y)
{
    if (!ready_ || engine_tool == kSpecialEngineId) {
        return;
    }
    const bool placed = engine_->sim.toolDrag(static_cast<EditingTool>(engine_tool),
                                              static_cast<short>(from_x), static_cast<short>(from_y),
                                              static_cast<short>(to_x), static_cast<short>(to_y));
    if (placed) {
        dirty_ = true;
    }
    notify();
}

void CitySession::set_speed(int speed)
{
    speed = std::max(0, std::min(3, speed));
    // A running speed is the player's choice. Pause leaves an announcement
    // pause in place so the next scenario can still restore the old speed.
    if (speed > 0) {
        outcome_paused_ = false;
    }
    if (speed_ == speed && (!ready_ || engine_->sim.simSpeed == speed)) {
        return;
    }
    speed_ = speed;
    if (ready_) {
        engine_->sim.setSpeed(static_cast<short>(speed_));
        // Speed is a preference, stored with the window, not in the city.
        // The launch city is created paused and then started; that is the
        // same as New City, which is created while the clock is already
        // running. A saved, loaded, or scenario city still counts it.
        if (!fresh_generated_) {
            dirty_ = true;
        }
    }
}

int CitySession::speed() const
{
    return speed_;
}

void CitySession::set_simulation_paused(bool paused)
{
    simulation_paused_ = paused;
}

bool CitySession::simulation_paused() const
{
    return simulation_paused_;
}

int CitySession::game_month_index() const
{
    const Micropolis &sim = engine_->sim;
    int year = static_cast<int>(sim.cityYear);
    if (year <= 0) {
        year = static_cast<int>(sim.startingYear);
    }
    int month = static_cast<int>(sim.cityMonth);
    if (month < 0) {
        month = 0;
    }
    if (month > 11) {
        month = 11;
    }
    return year * 12 + month;
}

void CitySession::set_auto_budget(bool on)
{
    if (engine_->sim.autoBudget == on) {
        return;
    }
    engine_->sim.setAutoBudget(on);
    mark_dirty();
}

bool CitySession::auto_budget() const
{
    return engine_->sim.autoBudget;
}

void CitySession::set_auto_bulldoze(bool on)
{
    if (engine_->sim.autoBulldoze == on) {
        return;
    }
    engine_->sim.setAutoBulldoze(on);
    mark_dirty();
}

bool CitySession::auto_bulldoze() const
{
    return engine_->sim.autoBulldoze;
}

void CitySession::set_disasters(bool on)
{
    if (engine_->sim.enableDisasters == on) {
        return;
    }
    engine_->sim.setEnableDisasters(on);
    mark_dirty();
}

bool CitySession::disasters() const
{
    return engine_->sim.enableDisasters;
}

void CitySession::set_auto_goto(bool on)
{
    if (engine_->sim.autoGoto == on) {
        return;
    }
    engine_->sim.setAutoGoto(on);
    mark_dirty();
}

bool CitySession::auto_goto() const
{
    return engine_->sim.autoGoto;
}

void CitySession::set_tax(int percent)
{
    if (percent < 0) {
        percent = 0;
    }
    if (percent > 20) {
        percent = 20;
    }
    if (engine_->sim.cityTax == percent) {
        return;
    }
    engine_->sim.setCityTax(static_cast<short>(percent));
    if (engine_->sim.budgetAwaitingAccept) {
        engine_->sim.recomputeTaxFund();
    }
    mark_dirty();
}

int CitySession::tax() const
{
    return engine_->sim.cityTax;
}

void CitySession::set_service_funding(int kind, int percent)
{
    if (percent < 0) {
        percent = 0;
    }
    if (percent > 100) {
        percent = 100;
    }
    const float fraction = static_cast<float>(percent) / 100.0f;
    Micropolis &sim = engine_->sim;
    float *slot = &sim.roadPercent;
    if (kind == 1) {
        slot = &sim.policePercent;
    } else if (kind == 2) {
        slot = &sim.firePercent;
    }
    // A scale event that rounds to a non-zero percent already on screen
    // is not a new request. Zero is different: choosing 0% is the player
    // asking for nothing, and the cut sentence has to go away (round 4).
    // The budget window drops a scale event that rounds to the percent
    // already showing, including 0, before it gets here.
    const int showing = percent_of_fraction(*slot);
    if (showing == percent && percent != 0) {
        return;
    }
    if (kind == 1) {
        sim.policeFundingTouched = true;
    } else if (kind == 2) {
        sim.fireFundingTouched = true;
    } else {
        sim.roadFundingTouched = true;
    }
    // Effects wait until the budget window commits. Moving a slider used
    // to drop road and coverage before any money moved.
    *slot = fraction;
    mark_dirty();
}

void CitySession::set_road_funding(int percent)
{
    set_service_funding(0, percent);
}

void CitySession::set_police_funding(int percent)
{
    set_service_funding(1, percent);
}

void CitySession::set_fire_funding(int percent)
{
    set_service_funding(2, percent);
}

void CitySession::restore_budget_rates(int tax_percent, int road_percent, int police_percent, int fire_percent)
{
    set_tax(tax_percent);
    set_road_funding(road_percent);
    set_police_funding(police_percent);
    set_fire_funding(fire_percent);
    // Restoring the opening rates is not the player asking for a new share.
    engine_->sim.roadFundingTouched = false;
    engine_->sim.policeFundingTouched = false;
    engine_->sim.fireFundingTouched = false;
}

namespace {

int percent_of(float fraction)
{
    int n = static_cast<int>(fraction * 100.0f + 0.5f);
    if (n < 0) {
        n = 0;
    }
    if (n > 100) {
        n = 100;
    }
    return n;
}

std::int64_t funded(Quad need, int percent)
{
    return static_cast<std::int64_t>(need) * percent / 100;
}

} // namespace

CitySession::BudgetBook CitySession::budget() const
{
    const Micropolis &sim = engine_->sim;
    BudgetBook book;
    book.taxes = static_cast<std::int64_t>(sim.taxFund);
    book.tax_percent = sim.cityTax;
    book.road_percent = percent_of(sim.roadPercent);
    book.police_percent = percent_of(sim.policePercent);
    book.fire_percent = percent_of(sim.firePercent);
    book.road_need = static_cast<std::int64_t>(sim.roadFund);
    book.police_need = static_cast<std::int64_t>(sim.policeFund);
    book.fire_need = static_cast<std::int64_t>(sim.fireFund);
    book.taxes_known = sim.taxReceiptKnown;

    // A new city, a scenario, and a classic file have no January receipt.
    // roadFund stays 0 until collectTax, which is not the same as a
    // department that costs nothing. Project the bill from the finished
    // census with the same formula January will use. totalPop is left at
    // 1 by doSimInit, so the tax uses the census population setValves
    // would write, not that placeholder.
    if (!book.taxes_known && !sim.budgetAwaitingAccept) {
        const bool snap = sim.censusSnapshotValid;
        const short roads = snap ? sim.snapRoadTotal : sim.roadTotal;
        const short rails = snap ? sim.snapRailTotal : sim.railTotal;
        const short police = snap ? sim.snapPolicePop : sim.policeStationPop;
        const short fire = snap ? sim.snapFirePop : sim.fireStationPop;
        const Quad res = snap ? sim.snapResPop : sim.resPop;
        const Quad com = snap ? sim.snapComPop : sim.comPop;
        const Quad ind = snap ? sim.snapIndPop : sim.indPop;
        const float normalized = static_cast<float>(res) / 8.0f;
        const Quad pop = static_cast<Quad>(normalized + static_cast<float>(com) + static_cast<float>(ind));
        const Micropolis::BudgetBases bases =
            sim.projectBudget(roads, rails, police, fire, pop, sim.landValueAverage, sim.cityTax);
        book.taxes = static_cast<std::int64_t>(bases.tax);
        book.road_need = static_cast<std::int64_t>(bases.road);
        book.police_need = static_cast<std::int64_t>(bases.police);
        book.fire_need = static_cast<std::int64_t>(bases.fire);
        book.estimates = true;
    }

    if (sim.budgetAwaitingAccept) {
        const Micropolis::BudgetCharge charge = sim.budgetCharge();
        book.road_spent = static_cast<std::int64_t>(charge.roadTaken);
        book.police_spent = static_cast<std::int64_t>(charge.policeTaken);
        book.fire_spent = static_cast<std::int64_t>(charge.fireTaken);
        book.cash_flow = static_cast<std::int64_t>(charge.posted);
        book.previous_funds = sim.budgetAnchorValid ? static_cast<std::int64_t>(sim.budgetAnchorFunds)
                                                    : static_cast<std::int64_t>(sim.totalFunds);
        book.funds = static_cast<std::int64_t>(sim.totalFunds) + book.cash_flow;
        // budgetCharge spends taxFund + cash, roads first, then fire, then
        // police. The sentence names that same money. A later department
        // that gets nothing because an earlier one used the pool says so.
        const std::int64_t taxes = static_cast<std::int64_t>(sim.taxFund);
        const std::int64_t cash = static_cast<std::int64_t>(sim.totalFunds);
        // Roads are paid first. Once one department is short, every later
        // short department was left short because that earlier one took
        // the taxes and cash that remained.
        bool earlier_was_cut = false;
        auto cut = [&](float requested, bool touched, float slider, float applied, Quad fund) -> std::string {
            if (fund <= 0) {
                return {};
            }
            // An untouched slider still shows the affordable share. The
            // sentence uses the request from before that scaling. After
            // the player moves the slider, that position is the request.
            const int want = percent_of(touched ? slider : requested);
            const int got = percent_of(applied);
            if (want <= got) {
                return {};
            }
            std::string note;
            if (earlier_was_cut) {
                note = "cut to " + std::to_string(got) +
                       "% because an earlier department took the rest of taxes (" + with_commas(taxes) +
                       ") plus the " + with_commas(cash) + " on hand";
            } else {
                note = "cut to " + std::to_string(got) + "% because taxes (" + with_commas(taxes) +
                       ") plus the " + with_commas(cash) + " on hand cannot cover " + std::to_string(want) +
                       "%";
            }
            earlier_was_cut = true;
            return note;
        };
        book.road_note = cut(sim.roadPercentRequested, sim.roadFundingTouched, sim.roadPercent, charge.roadPercent,
                             sim.roadFund);
        book.fire_note = cut(sim.firePercentRequested, sim.fireFundingTouched, sim.firePercent, charge.firePercent,
                             sim.fireFund);
        book.police_note =
            cut(sim.policePercentRequested, sim.policeFundingTouched, sim.policePercent, charge.policePercent,
                sim.policeFund);
        return book;
    }

    book.funds = static_cast<std::int64_t>(sim.totalFunds);
    book.previous_funds = book.funds;
    // Needs are the stored January bill, or the estimate filled in above.
    // Spending the stored fund here would print $0 for a city that has
    // not collected a tax year yet.
    book.road_spent = funded(static_cast<Quad>(book.road_need), book.road_percent);
    book.police_spent = funded(static_cast<Quad>(book.police_need), book.police_percent);
    book.fire_spent = funded(static_cast<Quad>(book.fire_need), book.fire_percent);
    book.cash_flow = book.taxes - book.road_spent - book.police_spent - book.fire_spent;
    return book;
}

void CitySession::set_sound_enabled(bool on)
{
    if (sound_enabled_ == on && engine_->sim.enableSound == on) {
        return;
    }
    sound_enabled_ = on;
    engine_->sim.setEnableSound(on);
    mark_dirty();
}

bool CitySession::sound_enabled() const
{
    return sound_enabled_;
}

void CitySession::accept_preferences()
{
    dirty_ = false;
}

std::vector<std::string> CitySession::take_sounds()
{
    std::vector<std::string> sounds;
    sounds.swap(sounds_);
    return sounds;
}

bool CitySession::take_budget_request()
{
    const bool requested = budget_requested_;
    budget_requested_ = false;
    return requested;
}

void CitySession::keep_budget_request()
{
    budget_requested_ = true;
}

void CitySession::commit_pending_budget()
{
    if (engine_->sim.budgetAwaitingAccept) {
        dirty_ = true;
    }
    engine_->sim.commitBudgetPayment();
}

bool CitySession::budget_pending() const
{
    return engine_->sim.budgetAwaitingAccept;
}

void CitySession::discard_pending_budget()
{
    engine_->sim.budgetAwaitingAccept = false;
}

void CitySession::finish_budget_edit()
{
    if (engine_->sim.budgetAwaitingAccept) {
        commit_pending_budget();
        return;
    }
    engine_->sim.applyFundingLevels();
}

bool CitySession::census_ready() const
{
    return engine_->sim.liveCensusComplete;
}

bool CitySession::note_evaluation_month(int month_index)
{
    if (month_index != eval_month_) {
        eval_month_pending_ = true;
    }
    // Phase 0 publishes the new month and clears the census in the same
    // tick. Wait until the scan has filled the counts again.
    if (in_callback_ || !census_ready()) {
        return false;
    }
    if (!eval_month_pending_) {
        return false;
    }
    eval_month_ = month_index;
    eval_month_pending_ = false;
    engine_->sim.cityEvaluationPreview();
    return true;
}

std::int64_t CitySession::stored_assessed_value() const
{
    return static_cast<std::int64_t>(engine_->sim.cityAssessedValue);
}

void CitySession::mark_clean_time()
{
    saved_city_time_ = engine_ ? static_cast<long long>(engine_->sim.cityTime) : 0;
}

long long CitySession::city_time() const
{
    return static_cast<long long>(engine_->sim.cityTime);
}

bool CitySession::time_advanced_since_save() const
{
    // Years of growth, taxes, and auto-budget are unsaved work too.
    return ready_ && static_cast<long long>(engine_->sim.cityTime) != saved_city_time_;
}

bool CitySession::needs_save_prompt() const
{
    if (dirty_ || budget_pending()) {
        return true;
    }
    if (!time_advanced_since_save()) {
        return false;
    }
    // The clock moving inside the month a city was generated is not a
    // change. A saved city, a loaded city, and a scenario still prompt.
    if (fresh_generated_ && engine_ != nullptr) {
        int month = static_cast<int>(engine_->sim.cityMonth);
        if (month < 0 || month > 11) {
            month = 0;
        }
        const long year = static_cast<long>(engine_->sim.cityYear);
        const int shown = static_cast<int>(year > 0 ? year : engine_->sim.startingYear);
        if (month == birth_month_ && shown == birth_year_) {
            return false;
        }
    }
    return true;
}

bool CitySession::take_view_target(int &tile_x, int &tile_y)
{
    if (!goto_pending_) {
        return false;
    }
    goto_pending_ = false;
    tile_x = goto_x_;
    tile_y = goto_y_;
    return true;
}

int CitySession::take_earthquake()
{
    const int strength = quake_strength_;
    quake_strength_ = 0;
    return strength;
}

void CitySession::place_sprite(int type, int tile_x, int tile_y)
{
    if (!ready_) {
        return;
    }
    if (type <= SPRITE_NOTUSED || type >= SPRITE_COUNT) {
        return;
    }
    engine_->sim.makeSprite(type, tile_x << 4, tile_y << 4);
}

bool CitySession::stamp_neighborhood(int &origin_x, int &origin_y)
{
    origin_x = 0;
    origin_y = 0;
    if (!ready_) {
        return false;
    }
    Micropolis &sim = engine_->sim;
    constexpr int kWide = 20;
    constexpr int kTall = 14;
    auto open_land = [](int raw) {
        const int tile = raw & LOMASK;
        return tile == DIRT || (tile >= TREEBASE && tile <= WOODS5) ||
               (tile >= RUBBLE && tile <= LASTRUBBLE);
    };
    bool found = false;
    for (int y = 2; y < kWorldH - kTall && !found; ++y) {
        for (int x = 2; x < kWorldW - kWide; ++x) {
            bool clear = true;
            for (int dy = 0; dy < kTall && clear; ++dy) {
                for (int dx = 0; dx < kWide; ++dx) {
                    if (!open_land(sim.map[x + dx][y + dy])) {
                        clear = false;
                        break;
                    }
                }
            }
            if (clear) {
                origin_x = x;
                origin_y = y;
                found = true;
            }
        }
    }
    if (!found) {
        return false;
    }
    for (int dy = 0; dy < kTall; ++dy) {
        for (int dx = 0; dx < kWide; ++dx) {
            if ((sim.map[origin_x + dx][origin_y + dy] & LOMASK) != DIRT) {
                sim.doTool(TOOL_BULLDOZER, static_cast<short>(origin_x + dx),
                           static_cast<short>(origin_y + dy));
            }
        }
    }

    const int ox = origin_x;
    const int oy = origin_y;
    auto put = [&](EditingTool tool, int x, int y) {
        sim.doTool(tool, static_cast<short>(x), static_cast<short>(y));
    };

    // Click is the building center. 4x4 coal occupies center-1 .. center+2.
    put(TOOL_COALPOWER, static_cast<short>(ox + 3), static_cast<short>(oy + 3));
    put(TOOL_RESIDENTIAL, static_cast<short>(ox + 9), static_cast<short>(oy + 3));
    put(TOOL_COMMERCIAL, static_cast<short>(ox + 15), static_cast<short>(oy + 3));
    put(TOOL_INDUSTRIAL, static_cast<short>(ox + 3), static_cast<short>(oy + 9));
    put(TOOL_FIRESTATION, static_cast<short>(ox + 9), static_cast<short>(oy + 9));
    put(TOOL_POLICESTATION, static_cast<short>(ox + 15), static_cast<short>(oy + 9));
    put(TOOL_PARK, static_cast<short>(ox + 13), static_cast<short>(oy + 8));

    for (int y = oy; y < oy + kTall; ++y) {
        for (int x = ox; x < ox + kWide; ++x) {
            const bool grid = (x - ox) % 6 == 0 || (y - oy) % 6 == 0;
            if (grid && (sim.map[x][y] & LOMASK) == DIRT) {
                put(TOOL_ROAD, x, y);
            }
        }
    }
    for (int x = ox + 1; x < ox + kWide - 1; ++x) {
        put(TOOL_WIRE, x, oy + 6);
    }
    notify();
    return true;
}

void CitySession::disaster_fire()
{
    engine_->sim.makeFire();
    dirty_ = true;
    notify();
}

void CitySession::disaster_flood()
{
    engine_->sim.makeFlood();
    dirty_ = true;
    notify();
}

void CitySession::disaster_tornado()
{
    engine_->sim.makeTornado();
    dirty_ = true;
    notify();
}

void CitySession::disaster_earthquake()
{
    engine_->sim.makeEarthquake();
    dirty_ = true;
    notify();
}

void CitySession::disaster_monster()
{
    engine_->sim.makeMonster();
    dirty_ = true;
    notify();
}

void CitySession::disaster_meltdown()
{
    engine_->sim.makeMeltdown();
    dirty_ = true;
    notify();
}

std::string CitySession::city_name() const
{
    if (engine_->sim.cityName.empty()) {
        return "New City";
    }
    return engine_->sim.cityName;
}

std::string CitySession::funds_text() const
{
    return "Funds: " + with_commas(static_cast<std::int64_t>(engine_->sim.totalFunds));
}

std::string CitySession::date_text() const
{
    int month = static_cast<int>(engine_->sim.cityMonth);
    if (month < 0 || month > 11) {
        month = 0;
    }
    const long year = static_cast<long>(engine_->sim.cityYear);
    return std::string(kMonths[month]) + " " + std::to_string(year > 0 ? year : engine_->sim.startingYear);
}

std::string CitySession::message() const
{
    return message_;
}

std::string CitySession::evaluation_text()
{
    update_evaluation();
    const Evaluation report = evaluation();
    std::ostringstream out;
    out << report.category << "\n"
        << "Population: " << report.population << "\n"
        << "Score: " << report.score << "\n"
        << "Yes: " << report.yes_percent << "%";
    return out.str();
}

int CitySession::history_value(HistorySeries series, HistoryScale scale, int index) const
{
    return engine_->sim.getHistory(static_cast<int>(series), static_cast<int>(scale), index);
}

int CitySession::crime_average() const
{
    return static_cast<int>(engine_->sim.crimeAverage);
}

int CitySession::pollution_average() const
{
    return static_cast<int>(engine_->sim.pollutionAverage);
}

std::int64_t CitySession::cash_flow_history(HistoryScale scale, int index) const
{
    return static_cast<std::int64_t>(engine_->sim.cashFlowHistory(static_cast<int>(scale), index));
}

bool CitySession::cash_flow_history_exact(HistoryScale scale, int index) const
{
    return engine_->sim.cashFlowHistoryExact(static_cast<int>(scale), index);
}

int CitySession::take_scenario_outcome()
{
    const int outcome = scenario_outcome_;
    scenario_outcome_ = 0;
    return outcome;
}

int CitySession::scenario_id() const
{
    const int id = static_cast<int>(engine_->sim.scenario);
    if (id < SC_DULLSVILLE || id > SC_RIO) {
        return 0;
    }
    return id;
}

int CitySession::scenario_years_left() const
{
    const Micropolis &sim = engine_->sim;
    if (scenario_id() == 0 || sim.scoreType == SC_NONE) {
        return -1;
    }
    // scoreWait counts simulation cycles. A year is 48 cycles. Whole years
    // are rounded down so a partial year still shows in scenario_clock_text.
    if (sim.scoreWait <= 0) {
        return 0;
    }
    return static_cast<int>(sim.scoreWait) / 48;
}

std::string CitySession::scenario_clock_text() const
{
    const Micropolis &sim = engine_->sim;
    if (scenario_id() == 0 || sim.scoreType == SC_NONE) {
        return {};
    }
    if (sim.scoreWait <= 0) {
        return "Time is up";
    }
    const int cycles = static_cast<int>(sim.scoreWait);
    const int years = cycles / 48;
    const int months = (cycles % 48) / 4;
    if (months <= 0) {
        if (years == 1) {
            return "1 year left";
        }
        return std::to_string(years) + " years left";
    }
    std::string text = std::to_string(years) + (years == 1 ? " year, " : " years, ");
    text += std::to_string(months) + (months == 1 ? " month left" : " months left");
    return text;
}

std::string CitySession::scenario_progress() const
{
    const int id = scenario_id();
    if (id == 0 || scenario_years_left() < 0) {
        return {};
    }
    const ScenarioDef &def = scenario_def(id - SC_DULLSVILLE);
    const std::string time = scenario_clock_text();
    if (time.empty()) {
        return def.goal;
    }
    return time + ". " + def.goal;
}

std::string CitySession::scenario_meter() const
{
    const int id = scenario_id();
    if (id == 0 || scenario_years_left() < 0) {
        return {};
    }
    const Micropolis &sim = engine_->sim;
    const std::int64_t people = static_cast<std::int64_t>(live_population_count(sim));
    if (id == SC_DETROIT) {
        return "Crime " + std::to_string(static_cast<int>(sim.crimeAverage)) + " → goal < 60";
    }
    if (id == SC_BERN) {
        return "Traffic " + std::to_string(static_cast<int>(sim.trafficAverage)) + " → goal < 80";
    }
    if (id == SC_TOKYO || id == SC_BOSTON || id == SC_RIO) {
        // Judged on the score at the deadline, after the disaster. A city
        // that already sits above 500 has not won yet.
        const ScenarioDef &def = scenario_def(id - SC_DULLSVILLE);
        const int deadline = def.year + def.years;
        return "Score " + grouped_count(static_cast<std::int64_t>(sim.cityScore)) + ", keep it above 500 until " +
               std::to_string(deadline);
    }
    return "Population " + grouped_count(people > 0 ? people : 0) + " → 100,000";
}

std::string CitySession::scenario_outcome_text(bool won) const
{
    const std::string name = city_name();
    std::string text = won ? "You did it! " + name + " met the goal." : "Time's up. " + name + " missed the goal.";
    const int id = scenario_id();
    if (id != 0) {
        const ScenarioDef &def = scenario_def(id - SC_DULLSVILLE);
        text += " ";
        text += def.goal;
    }
    const std::string meter = scenario_meter();
    if (!meter.empty()) {
        text += " ";
        text += meter;
        text += ".";
    } else {
        const Evaluation report = evaluation();
        text += " Population " + grouped_count(report.population) + ", score " + grouped_count(report.score) + ".";
    }
    return text;
}

void CitySession::resume_after_outcome()
{
    if (!outcome_paused_) {
        return;
    }
    const int resume = speed_before_outcome_;
    outcome_paused_ = false;
    set_speed(resume);
}

void CitySession::stay_paused_after_outcome()
{
    // The announcement is over and the player wants the clock to stay
    // stopped. The remembered speed remains so the next scenario can
    // still start at the speed from before the win or loss.
    if (!outcome_paused_) {
        return;
    }
    speed_ = 0;
    if (ready_) {
        engine_->sim.setSpeed(0);
    }
}

void CitySession::update_evaluation()
{
    // Preview fills problems and opinion for the window. Population,
    // migration, and score stay on the tax-year pass. Skip the pass while
    // phase 0 has cleared the census, and while an engine callback is
    // still on the stack.
    if (in_callback_ || !census_ready()) {
        eval_month_pending_ = true;
        return;
    }
    engine_->sim.cityEvaluationPreview();
}

CitySession::Evaluation CitySession::evaluation() const
{
    const Micropolis &sim = engine_->sim;
    Evaluation report;
    report.score = sim.cityScore;
    report.score_delta = sim.cityScoreDelta;
    const bool year_passed = ready_ && sim.cityTime >= eval_anchor_time_ + 48;
    report.deltas_known = eval_deltas_ready_ || year_passed;
    report.yes_percent = sim.evalPreviewValid ? sim.evalPreviewYes : sim.cityYes;
    if (report.yes_percent < 0) {
        report.yes_percent = 0;
    }
    if (report.yes_percent > 100) {
        report.yes_percent = 100;
    }
    const Quad live_population = live_population_count(sim);
    report.population = live_population < 0 ? 0 : static_cast<std::int64_t>(live_population);
    // cityPopDelta is the last tax year. A mid-year preview does not replace it.
    report.migration = static_cast<std::int64_t>(sim.cityPopDelta);
    if (!report.deltas_known) {
        report.score_delta = 0;
        report.migration = 0;
    }
    report.assessed_value = static_cast<std::int64_t>(sim.evalPreviewValid ? sim.evalPreviewAssessed : sim.cityAssessedValue);
    report.category = city_class_name(live_city_class(live_population < 0 ? 0 : live_population));
    report.year = static_cast<int>(sim.cityYear > 0 ? sim.cityYear : sim.startingYear);
    switch (sim.gameLevel) {
    case LEVEL_MEDIUM:
        report.difficulty = "Medium";
        break;
    case LEVEL_HARD:
        report.difficulty = "Hard";
        break;
    case LEVEL_EASY:
    default:
        report.difficulty = "Easy";
        break;
    }

    static const char *kProblems[] = {
        "Crime", "Pollution", "Housing", "Taxes", "Traffic", "Unemployment", "Fire",
    };
    for (int i = 0; i < CVP_PROBLEM_COMPLAINTS; ++i) {
        const int which = sim.evalPreviewValid ? sim.evalPreviewOrder[i] : sim.problemOrder[i];
        if (which < 0 || which >= CVP_NUMPROBLEMS) {
            break;
        }
        Problem problem;
        problem.name = kProblems[which];
        const int votes = sim.evalPreviewValid ? sim.evalPreviewVotes[which] : sim.problemVotes[which];
        problem.votes = votes;
        if (problem.votes < 0) {
            problem.votes = 0;
        }
        report.problems.push_back(problem);
    }
    if (report.population <= 0) {
        report.no_residents = true;
        report.problems.clear();
        report.yes_percent = 0;
    }
    return report;
}

std::string CitySession::budget_text() const
{
    const BudgetBook book = budget();
    std::ostringstream out;
    out << "Road funding: " << book.road_percent << "%\n"
        << "Police funding: " << book.police_percent << "%\n"
        << "Fire funding: " << book.fire_percent << "%\n"
        << (engine_->sim.autoBudget ? "Auto budget is on." : "Auto budget is off.");
    return out.str();
}

namespace {

double clamp_unit(float value)
{
    if (value < -1.0f) {
        return -1.0;
    }
    if (value > 1.0f) {
        return 1.0;
    }
    return value;
}

} // namespace

double CitySession::res_demand()
{
    float residential = 0;
    float commercial = 0;
    float industrial = 0;
    engine_->sim.getDemands(&residential, &commercial, &industrial);
    return clamp_unit(residential);
}

double CitySession::com_demand()
{
    float residential = 0;
    float commercial = 0;
    float industrial = 0;
    engine_->sim.getDemands(&residential, &commercial, &industrial);
    return clamp_unit(commercial);
}

double CitySession::ind_demand()
{
    float residential = 0;
    float commercial = 0;
    float industrial = 0;
    engine_->sim.getDemands(&residential, &commercial, &industrial);
    return clamp_unit(industrial);
}

int CitySession::map_value(int x, int y) const
{
    if (!ready_ || !Micropolis::testBounds(x, y) || engine_->sim.map[x] == nullptr) {
        return 0;
    }
    return engine_->sim.map[x][y];
}

int CitySession::layer_value(MapLayer layer, int x, int y) const
{
    if (!ready_ || !Micropolis::testBounds(x, y)) {
        return 0;
    }
    Micropolis &sim = engine_->sim;
    const int raw = sim.map[x][y];
    const int tile = raw & LOMASK;
    const bool water = (tile >= RIVER && tile <= WATER_HIGH) || (tile >= FLOOD && tile <= LASTFLOOD);
    switch (layer) {
    case MapLayer::Water:
        return water ? 1 : 0;
    case MapLayer::Power:
        if (water || tile <= LASTFIRE) {
            return 0;
        }
        if ((raw & ZONEBIT) != 0) {
            return (raw & PWRBIT) != 0 ? 3 : 2;
        }
        if ((raw & CONDBIT) != 0) {
            return 4;
        }
        return 0;
    case MapLayer::Pollution:
        return sim.getPollutionDensity(x / 2, y / 2);
    case MapLayer::Crime:
        return sim.getCrimeRate(x / 2, y / 2);
    case MapLayer::LandValue:
        return sim.getLandValue(x / 2, y / 2);
    case MapLayer::Traffic:
        return sim.getTrafficDensity(x / 2, y / 2);
    case MapLayer::Police:
        return sim.policeStationEffectMap.worldGet(x, y);
    case MapLayer::Fire:
        return sim.fireStationEffectMap.worldGet(x, y);
    case MapLayer::Population:
        return sim.populationDensityMap.worldGet(x, y);
    case MapLayer::Growth:
        return sim.rateOfGrowthMap.worldGet(x, y);
    case MapLayer::Zones: {
        if (water) {
            return 8;
        }
        if (tile >= RESBASE && tile < COMBASE) {
            return 1;
        }
        if (tile >= COMBASE && tile < INDBASE) {
            return 2;
        }
        if (tile >= INDBASE && tile < PORTBASE) {
            return 3;
        }
        if ((tile >= ROADBASE && tile <= BRWXXX7) || tile == ROADVPOWERH) {
            return 4;
        }
        if (tile >= RAILBASE && tile <= LASTRAIL) {
            return 5;
        }
        return 0;
    }
    case MapLayer::Residential:
        return (tile >= RESBASE && tile < COMBASE) ? 1 : 0;
    case MapLayer::Commercial:
        return (tile >= COMBASE && tile < INDBASE) ? 1 : 0;
    case MapLayer::Industrial:
        return (tile >= INDBASE && tile < PORTBASE) ? 1 : 0;
    case MapLayer::Transportation:
        if ((tile >= ROADBASE && tile <= BRWXXX7) || tile == ROADVPOWERH) {
            return 1;
        }
        if (tile >= RAILBASE && tile <= LASTRAIL) {
            return 2;
        }
        return 0;
    }
    return 0;
}

bool CitySession::query_tile(int &x, int &y) const
{
    if (!query_has_tile_) {
        return false;
    }
    x = query_x_;
    y = query_y_;
    return true;
}

void CitySession::clear_notices()
{
    notices_.clear();
}

void CitySession::remember_notice(std::string text, int tile_x, int tile_y, bool has_place)
{
    if (text.empty()) {
        return;
    }
    // Dragging a blocked tile used to append the same line on every sample.
    if (text == "Cannot build here." && !notices_.empty() && notices_.front().text == text) {
        notices_.front().tile_x = tile_x;
        notices_.front().tile_y = tile_y;
        notices_.front().has_place = has_place && Micropolis::testBounds(tile_x, tile_y);
        return;
    }
    CityNotice notice;
    notice.text = std::move(text);
    notice.tile_x = tile_x;
    notice.tile_y = tile_y;
    notice.has_place = has_place && Micropolis::testBounds(tile_x, tile_y);
    notice.month = static_cast<int>(engine_->sim.cityMonth);
    if (notice.month < 0 || notice.month > 11) {
        notice.month = 0;
    }
    const long year = static_cast<long>(engine_->sim.cityYear);
    notice.year = static_cast<int>(year > 0 ? year : engine_->sim.startingYear);
    notices_.insert(notices_.begin(), std::move(notice));
    if (notices_.size() > 50) {
        notices_.resize(50);
    }
}

void CitySession::recount_population_from_map()
{
    if (engine_ == nullptr || !ready_) {
        return;
    }
    Micropolis &sim = engine_->sim;
    Quad res = 0;
    Quad com = 0;
    Quad ind = 0;
    for (int x = 0; x < kWorldW; ++x) {
        for (int y = 0; y < kWorldH; ++y) {
            const unsigned short raw = sim.map[x][y];
            if ((raw & ZONEBIT) == 0) {
                continue;
            }
            const int tile = raw & LOMASK;
            // Same splits as doZone: houses below the hospital, then
            // commercial, then industry through the last industrial tile.
            // Hospitals and churches are zone centres and are not people.
            if (tile == FREEZ) {
                int homes = 0;
                for (int nx = x - 1; nx <= x + 1; ++nx) {
                    for (int ny = y - 1; ny <= y + 1; ++ny) {
                        if (nx < 0 || ny < 0 || nx >= kWorldW || ny >= kWorldH) {
                            continue;
                        }
                        const int house = sim.map[nx][ny] & LOMASK;
                        if (house >= LHTHR && house <= HHTHR) {
                            ++homes;
                        }
                    }
                }
                res += homes;
            } else if (tile < HOSPITAL) {
                res += ((tile - RZB) / 9) % 4 * 8 + 16;
            } else if (tile >= COMBASE && tile < INDBASE) {
                if (tile != COMCLR) {
                    com += ((tile - CZB) / 9) % 5 + 1;
                }
            } else if (tile >= INDBASE && tile <= PORTBASE) {
                if (tile != INDCLR) {
                    ind += ((tile - IZB) / 9) % 4 + 1;
                }
            }
        }
    }
    const Quad people = (res + (com + ind) * 8) * 20;
    const Quad saved = (sim.resPop + (sim.comPop + sim.indPop) * 8) * 20;
    // An empty generated map can still carry a wide census that is not on
    // the tiles. A city with zones always shows the tiles, so a stale
    // stored "Town" does not sit on screen until the next census.
    if (people <= 0 && saved > 0) {
        sim.cityPop = saved;
        sim.cityClass = live_city_class(saved);
        return;
    }
    sim.resPop = res;
    sim.comPop = com;
    sim.indPop = ind;
    sim.snapResPop = res;
    sim.snapComPop = com;
    sim.snapIndPop = ind;
    sim.censusSnapshotValid = true;
    sim.liveCensusComplete = true;
    sim.cityPop = people < 0 ? 0 : people;
    sim.cityClass = live_city_class(people < 0 ? 0 : people);
}

void CitySession::refresh_loaded_meters()
{
    if (engine_ == nullptr || !ready_) {
        return;
    }
    Micropolis &sim = engine_->sim;
    if (live_population_count(sim) <= 0) {
        return;
    }
    // doSimInit now scans density before crime. This pass refreshes the
    // score and the traffic average the scenario meter reads, which otherwise
    // stay on the file until the next evaluation.
    sim.cityEvaluation();
}

void CitySession::arm_eval_baseline()
{
    Micropolis &sim = engine_->sim;
    eval_deltas_ready_ = false;
    const Quad time = sim.cityTime < 0 ? static_cast<Quad>(0) : sim.cityTime;
    eval_anchor_time_ = static_cast<long long>(time);
    const Quad people = live_population_count(sim);
    sim.cityPop = people < 0 ? 0 : people;
    sim.cityPopDelta = 0;
    sim.cityScoreDelta = 0;
}

void CitySession::clear_scenario_clock()
{
    scenario_result_ = 0;
}

bool CitySession::write_scenario_sidecar(const std::string &path) const
{
    if (path.empty()) {
        return false;
    }
    const Micropolis &sim = engine_->sim;
    ScenarioClock clock;
    clock.scenario = static_cast<int>(sim.scenario);
    clock.score_wait = static_cast<int>(sim.scoreWait);
    clock.score_type = static_cast<int>(sim.scoreType);
    clock.outcome = scenario_result_;
    clock.month = static_cast<int>(sim.cityMonth);
    const long year = static_cast<long>(sim.cityYear);
    clock.year = static_cast<int>(year > 0 ? year : sim.startingYear);
    return write_text_atomic(scenario_sidecar_path(path), scenario_sidecar_text(clock));
}

void CitySession::read_scenario_sidecar(const std::string &path)
{
    std::ifstream in(scenario_sidecar_path(path));
    if (!in) {
        return;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    ScenarioClock clock;
    if (!scenario_sidecar_parse(buffer.str(), clock)) {
        return;
    }
    Micropolis &sim = engine_->sim;
    if (clock.scenario >= SC_DULLSVILLE && clock.scenario <= SC_RIO) {
        sim.scenario = static_cast<Scenario>(clock.scenario);
    }
    scenario_result_ = clock.outcome;
    if (clock.outcome != 0) {
        sim.scoreType = SC_NONE;
        sim.scoreWait = 0;
        return;
    }
    if (clock.scenario != 0) {
        const int type = clock.score_type != 0 ? clock.score_type : clock.scenario;
        sim.scoreType = static_cast<Scenario>(type);
        sim.scoreWait = static_cast<short>(std::max(0, clock.score_wait));
    }
}

bool CitySession::preview_terrain(int seed, int island, int rivers, int lakes, int trees,
                                 std::vector<unsigned short> &tiles) const
{
    Micropolis sim;
    sim.terrainCreateIsland = island;
    sim.terrainCurveLevel = rivers;
    sim.terrainLakeLevel = lakes;
    sim.terrainTreeLevel = trees;
    sim.generateSomeCity(seed);
    tiles.resize(static_cast<std::size_t>(kWorldW * kWorldH));
    for (int y = 0; y < kWorldH; ++y) {
        for (int x = 0; x < kWorldW; ++x) {
            tiles[static_cast<std::size_t>(y * kWorldW + x)] = sim.map[x][y];
        }
    }
    return true;
}

std::int64_t CitySession::plot_cash_flow(HistoryScale scale, int index) const
{
    const std::int64_t stored = cash_flow_history(scale, index);
    if (scale == HistoryScale::Short && index == 0 && stored == 0) {
        return budget().cash_flow;
    }
    return stored;
}

unsigned CitySession::map_serial() const
{
    return static_cast<unsigned>(engine_->sim.mapSerial);
}

std::vector<CitySession::SpriteDot> CitySession::sprites() const
{
    std::vector<SpriteDot> dots;
    if (!ready_) {
        return dots;
    }
    int guard = 0;
    // Deactivated sprites used to sit on this list forever. Keep a cycle
    // guard, but do not stop after a handful of nodes or a later train,
    // plane, ship, or monster is never drawn.
    for (SimSprite *sprite = engine_->sim.spriteList; sprite != nullptr && guard < 4096;
         sprite = sprite->next, ++guard) {
        if (sprite->frame == 0 || sprite->type == SPRITE_NOTUSED) {
            continue;
        }
        SpriteDot dot;
        dot.type = sprite->type;
        dot.frame = sprite->frame;
        dot.x = sprite->x;
        dot.y = sprite->y;
        dot.x_offset = sprite->xOffset;
        dot.y_offset = sprite->yOffset;
        dot.width = sprite->width;
        dot.height = sprite->height;
        dot.tile_x = sprite->x >> 4;
        dot.tile_y = sprite->y >> 4;
        dots.push_back(dot);
    }
    return dots;
}
