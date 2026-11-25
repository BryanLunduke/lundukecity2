// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

#pragma once

#include "city_events.hpp"
#include "civic.hpp"
#include "inspect_report.hpp"
#include "save_view.hpp"

#include <cstdint>
#include <cstdarg>
#include <functional>
#include <string>
#include <utility>
#include <vector>

struct CityEvents;

// Thin bridge over the vendored Micropolis engine. The UI never talks to
// Tcl, and it does not include the engine header.
class CitySession {
public:
    static constexpr int kWorldW = 120;
    static constexpr int kWorldH = 100;

    struct SpriteDot {
        int type = 0;
        int frame = 0;
        int x = 0;
        int y = 0;
        int x_offset = 0;
        int y_offset = 0;
        int width = 0;
        int height = 0;
        int tile_x = 0;
        int tile_y = 0;
    };

    struct BudgetBook {
        std::int64_t taxes = 0;
        std::int64_t cash_flow = 0;
        std::int64_t funds = 0;
        std::int64_t previous_funds = 0;
        std::int64_t road_need = 0;
        std::int64_t police_need = 0;
        std::int64_t fire_need = 0;
        std::int64_t road_spent = 0;
        std::int64_t police_spent = 0;
        std::int64_t fire_spent = 0;
        // Casino, Amusement Park, Marina and other income buildings. In a
        // tax year: the amount this January added to cash_flow. Otherwise:
        // what they would pay next January at today's count (display only).
        std::int64_t attraction_income = 0;
        bool attraction_collected = false;
        int tax_percent = 0;
        int road_percent = 100;
        int police_percent = 100;
        int fire_percent = 100;
        // Set when Close will fund a department below the slider, because
        // tax plus cash cannot cover the request.
        std::string road_note;
        std::string police_note;
        std::string fire_note;
        // False until a January in this version has collected, or a save
        // carried an LCW1 receipt. The book then shows estimates.
        bool taxes_known = false;
        // True when the dollar amounts are projected from the current
        // census because no January has been collected yet.
        bool estimates = false;
    };

    // Separate map views. Power codes: 0 empty, 2 unpowered zone,
    // 3 powered zone, 4 conductive line. Water is 0 or 1. The density
    // layers return the engine's cluster value (0..255).
    enum class MapLayer {
        Power,
        Water,
        Pollution,
        Crime,
        LandValue,
        Traffic,
        Police,
        Fire,
        Population,
        Growth,
        Zones,
        Residential,
        Commercial,
        Industrial,
        Transportation,
    };

    using Listener = std::function<void()>;

    CitySession();
    ~CitySession();

    CitySession(const CitySession &) = delete;
    CitySession &operator=(const CitySession &) = delete;

    void set_listener(Listener listener);

    // Pre-made scenarios the engine loads from res/snro.* (Dullsville
    // through Rio de Janeiro), and Lunduke City 2's own maps in
    // data/scenarios (Seattle onwards). id matches the engine Scenario
    // ordinal.
    struct ScenarioDef {
        int id = 0;
        const char *name = "";
        int year = 0;
        const char *summary = "";
        // Classic win condition, including the deadline in years.
        const char *goal = "";
        int years = 0;
        // The name painted on the HUD. Empty or null means name is used.
        const char *city_name = nullptr;
    };

    static const char *scenario_city(const ScenarioDef &def)
    {
        return (def.city_name != nullptr && def.city_name[0] != '\0') ? def.city_name : def.name;
    }

    static constexpr int kScenarioCount = 13;
    // Seattle: the Autonomous Zone forms on the road block holding this tile
    // (Cal Anderson Park on Capitol Hill) and its least-policed neighbour.
    static constexpr int kSeattleZoneX = 75;
    static constexpr int kSeattleZoneY = 59;
    // San Francisco (1962): the Alcatraz escapees come ashore here, at
    // Fisherman's Wharf, the nearest shore to the island.
    static constexpr int kSf1962LandingX = 72;
    static constexpr int kSf1962LandingY = 22;
    // Portland: the riot breaks out around this tile, downtown by the
    // Justice Center and Lownsdale Square.
    static constexpr int kPortlandRiotX = 50;
    static constexpr int kPortlandRiotY = 63;
    // San Diego: the San Diego Zoo in Balboa Park (top-left corner of the
    // 3x3 landmark) that the lion gets out of.
    static constexpr int kSanDiegoZooLeft = 65;
    static constexpr int kSanDiegoZooTop = 46;
    static const ScenarioDef &scenario_def(int index);
    // Release nov24 #7: the scenario picker lists Learn to Play (the last
    // scenario_def) first, then the rest in their usual order. Row -> def index.
    static int picker_scenario(int row)
    {
        if (row <= 0) {
            return kScenarioCount - 1;
        }
        return row - 1 < kScenarioCount - 1 ? row - 1 : kScenarioCount - 1;
    }

    // Difficulty is the engine GameLevel: easy $20,000, medium $12,500,
    // hard $7,500. It also changes the tax and road-cost tables.
    static constexpr int kLevelEasy = 0;
    static constexpr int kLevelMedium = 1;
    static constexpr int kLevelHard = 2;
    // The funds a new city starts with (Micropolis::setGameLevelFunds).
    static constexpr int starting_funds(int level)
    {
        return level == kLevelHard ? 7500 : level == kLevelMedium ? 12500 : 20000;
    }

    // Terrain knobs are the engine fields used by generateMap().
    // -1 is the generator default. 0 turns that feature off.
    // Island 1 always builds an island (the only other value the engine reads).
    static constexpr int kTerrainDefault = -1;
    static constexpr int kTerrainOff = 0;
    static constexpr int kIslandAlways = 1;
    // Curve level above 0 makes rivers turn more often than the default rates.
    static constexpr int kRiversCurvy = 200;
    // Non-negative lake level is a lake count times two (20 => 10 lakes).
    static constexpr int kLakesMany = 20;
    // Non-negative tree level: splash count is level + 3 (197 => 200 splashes).
    static constexpr int kTreesWooded = 197;

    struct NewCitySpec {
        std::string name;
        int seed = 0;
        // False with seed 0 means "pick from the clock", which is what the
        // two-argument new_city() still does. True uses seed as written,
        // including 0.
        bool seed_was_set = false;
        int difficulty = kLevelEasy;
        int island = kTerrainDefault;
        int rivers = kTerrainDefault;
        int lakes = kTerrainDefault;
        int trees = kTerrainDefault;
        // Land painted in the terrain editor: kWorldW * kWorldH map words in
        // rows (y * kWorldW + x). Empty keeps the land the seed generates;
        // any other size is ignored. The seed and knobs still run the
        // generator first, so the city is set up exactly as a generated one.
        std::vector<unsigned short> terrain;
    };

    // seed 0 asks the engine path to draw a seed from the clock.
    // The two-argument form keeps the historical easy / default-terrain city.
    void new_city(const std::string &name, int seed = 0);
    void new_city(const NewCitySpec &spec);
    bool load_city(const std::string &path);
    // C-3 (round 12): why the last load_city() failed, in plain words
    // ("The file was not found.", "...too short...", etc.).
    const std::string &last_load_error() const { return last_load_error_; }
    // The reason a file cannot be loaded as a city, or "" if it looks loadable.
    static std::string load_problem(const std::string &path);
    bool save_city_as(const std::string &path);
    // C-1 (round 15): write the live city to a recovery file without
    // changing the save path, the unsaved-work state or a waiting tax year.
    // Returns false and leaves the city untouched when the write fails.
    bool write_recovery(const std::string &path);
    // Load a recovery file as unsaved work. origin_path (may be empty) is
    // the file the city came from; Save writes there, and the city counts
    // as unsaved until it is saved.
    bool load_recovery(const std::string &recovery_path, const std::string &origin_path);
    // Changes when the city changes in a way an autosave should capture.
    std::string change_fingerprint() const;
    bool load_scenario(int id);
    void rename_city(const std::string &name);
    // False when the name is empty after spaces, tabs, and controls are stripped.
    static bool name_is_usable(const std::string &name);

    int difficulty() const;
    // The engine's tax-table push on growth at this rate and level
    // (Micropolis::taxGrowthEffect): 0 at an effective 7%, negative above.
    static int tax_growth_effect(int tax, int level);
    // A new city's tax: 7% Easy, 6% Medium, 5% Hard. Each is the highest
    // rate whose growth effect is not negative on that level.
    static int default_tax(int level);
    std::int64_t funds() const;
    int generated_seed() const;

    void tick();
    void use_tool(int engine_tool, int tile_x, int tile_y);
    void drag_tool(int engine_tool, int from_x, int from_y, int to_x, int to_y);
    // index is a row in the special-building table. The click is the center tile.
    bool place_special(int index, int tile_x, int tile_y);
    std::string special_menu_detail(int index) const;
    std::string special_status_line(int index) const;
    // Residential census count (not people). The demo uses this so a
    // Mayor's House can be placed without waiting for growth.
    void set_census_residents(int residents);

    void set_speed(int speed);
    int speed() const;
    // Modal dialogs pause simTick and tile animation until they return.
    void set_simulation_paused(bool paused);
    bool simulation_paused() const;
    // year * 12 + month, used to refresh evaluation about once a month.
    int game_month_index() const;

    void set_auto_budget(bool on);
    bool auto_budget() const;
    void set_auto_bulldoze(bool on);
    bool auto_bulldoze() const;
    void set_disasters(bool on);
    bool disasters() const;
    void set_auto_goto(bool on);
    bool auto_goto() const;
    void set_tax(int percent);
    int tax() const;
    void set_road_funding(int percent);
    void set_police_funding(int percent);
    void set_fire_funding(int percent);
    // Put the sliders back to the rates captured when the book opened.
    // Does not collect a waiting tax year and does not close the book.
    void restore_budget_rates(int tax_percent, int road_percent, int police_percent, int fire_percent);
    BudgetBook budget() const;

    void set_sound_enabled(bool on);
    bool sound_enabled() const;
    // Startup restores saved preferences. That is not a city edit.
    void accept_preferences();
    std::vector<std::string> take_sounds();
    bool take_budget_request();
    // A modal dialog deferred the budget window. Ask for it again.
    void keep_budget_request();
    // Charge a tax year that was waiting on the budget window. A no-op
    // when nothing is waiting, including a second close.
    void commit_pending_budget();
    // Run January's tax pass on this city. An empty city still ends the
    // year waiting on the budget window.
    bool begin_tax_year();
    // True while a tax year is waiting for the budget window.
    bool budget_pending() const;
    // Drop a tax year without charging it. Used when the city is replaced.
    void discard_pending_budget();
    // Commit a waiting tax year, or apply funding effects when the book
    // was opened from the menu. Road effects stay put until this runs.
    void finish_budget_edit();
    // Live census is a finished scan (phase 9 or later, or just generated).
    bool census_ready() const;
    // Refresh the evaluation preview when the displayed month changes and
    // the census is complete. Returns false when the pass was skipped.
    bool note_evaluation_month(int month_index);
    // cityAssessedValue as stored on the engine, not the preview copy.
    std::int64_t stored_assessed_value() const;
    // Unsaved tools, budget, disasters, rename, or a tax year still open.
    // A generated city that has never been saved, still in the month it
    // was created, does not count the clock alone as a change.
    bool needs_save_prompt() const;
    bool dirty() const { return dirty_; }
    // Cancel of a budget preview puts the flag back. The rates are restored
    // by the caller; this only owns the unsaved bit.
    void set_dirty(bool on);
    // A stroke ends when the button lifts. "Cannot build here" during a
    // stroke that already placed a tile is the drag touching its own road.
    void end_tool_stroke();
    struct PlacementNote {
        bool ok = false;
        int tiles = 0;
        long long spent = 0;
        std::string reason;
        int tool = -1;
        int x = 0;
        int y = 0;
        // Set for a special so the receipt matches that building's footprint.
        // 0 keeps the generic tool footprint.
        int width = 0;
        int height = 0;
        int anchor_x = 0;
        int anchor_y = 0;
    };
    // One receipt for the stroke that just ended. False when nothing happened.
    bool take_placement_note(PlacementNote &out);
    int stroke_tiles() const { return stroke_tiles_; }
    long long stroke_spent() const { return stroke_spent_; }
    struct TileInspect {
        bool valid = false;
        std::string name;
        std::string power;
        std::string access;
        std::string condition;
        bool offer_power = false;
        bool zone = false;
        // Coal, nuclear or micro nuclear plant: no road needed.
        bool power_plant = false;
        // A building that works only with a road or rail beside it (zones,
        // service buildings, the Prison). False for plants, the Data
        // Center and the landmarks whose table row needs no road, and for
        // anything that is not a building (water, dirt, a road).
        bool needs_road = false;
        // Table row of the special building here, -1 for none, and whether
        // part of it has been destroyed (it stops working until rebuilt).
        int special = -1;
        bool damaged = false;
        int left = 0;
        int top = 0;
        int width = 1;
        int height = 1;
    };
    TileInspect inspect_tile(int x, int y) const;
    // The tiles a building covers, given its zone centre (the ZONEBIT
    // tile). False when (center_x, center_y) is not a zone centre.
    bool building_footprint(int center_x, int center_y, int &left, int &top, int &width, int &height) const;
    // Read-only facts for the Inspect card: the tile's kind, power, road
    // access and the simulator map values under it, plus the demand for
    // its zone type. kind_name is the card title.
    struct InspectFacts {
        bool valid = false;
        std::string kind_name;
        inspect_report::Facts facts;
        bool offer_power = false;
        int left = 0;
        int top = 0;
        int width = 1;
        int height = 1;
        // Short facts about the building itself, drawn under the chips
        // (Data Center: zone counts and no pollution; Micro Nuclear Plant:
        // its power and no meltdown). Empty for everything else.
        std::vector<std::string> notes;
    };
    InspectFacts inspect_facts(int x, int y);
    std::vector<std::string> special_inspect_notes(int x, int y) const;
    // Live head count, the same figure the Evaluation window shows.
    std::int64_t population() const;
    const civic::Ledger &ledger() const { return ledger_; }
    // announce is false while catching a city up, so an old city does not
    // celebrate milestones it had already reached.
    std::vector<int> note_civic_month(bool announce);
    std::vector<int> note_civic_year(std::int64_t cash_flow);
    void remember_civic_year();
    // A stroke that placed a tile must not keep "Cannot build here."
    void forget_blocked_build();
    bool time_advanced_since_save() const;
    long long city_time() const;
    // Tile the engine asked the view to center on (auto-goto).
    bool take_view_target(int &tile_x, int &tile_y);
    // Strength of a quake the engine just started, 0 if none is waiting.
    int take_earthquake();

    // Drop a mobile sprite on a tile. Used so the map can show the
    // engine's sprite list without waiting for the simulator to spawn one.
    void place_sprite(int type, int tile_x, int tile_y);

    // Lay a small powered neighborhood on clear land, for a visible demo.
    bool stamp_neighborhood(int &origin_x, int &origin_y);

    void disaster_fire();
    void disaster_flood();
    void disaster_tornado();
    void disaster_earthquake();
    void disaster_monster();
    void disaster_meltdown();

    // City 2's lasting disasters (city_events.hpp). start_event posts the
    // notice, or the reason it cannot start, and returns whether it did.
    // Menus, random rolls and scenarios all come through here.
    bool start_event(city_events::Kind kind);
    // A scenario file's name for one ("riot", "autonomous_zone",
    // "lion_escape", "prison_escape"). False for an unknown name.
    bool start_event_named(const std::string &key);
    // The zone on the road block holding tile (x, y) and its least-policed
    // neighbour, for scenarios.
    bool start_autonomous_zone_at(int x, int y);
    // The Seattle scenario's zone on Capitol Hill (the engine asks for it
    // a few weeks in). False outside Seattle or when no zone can form.
    bool start_seattle_zone();
    // The San Francisco (1962) escape from Alcatraz, coming ashore at the
    // Wharf. False outside that scenario or when it cannot start.
    bool start_sf1962_escape();
    // The Portland riot, downtown. False outside that scenario or when it
    // cannot start.
    bool start_portland_riot();
    // The San Diego lion, out of the Balboa Park zoo. False outside that
    // scenario or when it cannot start.
    bool start_san_diego_lion();
    // The disaster the running scenario raises when the engine asks for it
    // (the Seattle zone, the Alcatraz escape, ...). False when none starts.
    bool start_scenario_disaster();
    // Empty when it can start now.
    std::string event_unavailable(city_events::Kind kind) const;
    bool in_autonomous_zone(int x, int y) const;
    // True when the bulldozer may not touch (x, y): inside the zone, or a
    // building that reaches into it.
    bool bulldoze_blocked(int x, int y) const;
    city_events::LionView lion() const;
    std::vector<city_events::Barricade> barricades() const;
    std::vector<std::pair<int, int>> autonomous_zone_blocks() const;
    // Read-only view of the saved disaster state, for tests and the HUD.
    const CityEvents &events_state() const;
    // Live unpowered zones and fires, plus a short line for a running or
    // upcoming scenario event. Only reads the map and existing event state.
    struct CrisisSite {
        int x = 0;
        int y = 0;
    };
    struct CrisisReport {
        int unpowered = 0;
        int fires = 0;
        std::string event;
        std::string text;
        std::vector<CrisisSite> sites;
    };
    CrisisReport crisis_report() const;
    // Riot and prison crime now added (0 when none).
    int riot_crime() const;

    std::string city_name() const;
    std::string funds_text() const;
    std::string date_text() const;
    int city_year() const;
    int city_month() const;
    // January of a new year reviews the year that just finished.
    int review_year() const;
    struct CensusMix {
        int homes = 0;
        int shops = 0;
        int jobs = 0;
    };
    CensusMix census_mix() const;
    // DEMO=yearreview only. Sets the clock to the first January after 1900
    // and fills the real census, ledger, budget, and poll the cover reads.
    void prepare_review_demo();
    std::string message() const;
    // Increases once per engine notice, including a repeat of the same words.
    int message_serial() const { return message_serial_; }
    // Bumps once per query-tool report so the window can open feedback
    // even when the text matches the previous tile.
    int query_serial() const { return query_serial_; }
    std::string evaluation_text();
    std::string budget_text() const;

    // Engine history tables. Index 0 is the newest sample.
    // Short is 120 monthly samples (10 years). Long is 120 yearly samples.
    enum class HistorySeries {
        Residential = 0,
        Commercial,
        Industrial,
        CashFlow,
        Crime,
        Pollution,
    };
    enum class HistoryScale {
        Short = 0,
        Long = 1,
    };
    static constexpr int kHistoryPoints = 120;
    int history_value(HistorySeries series, HistoryScale scale, int index) const;
    // Live census averages (0..255). The history byte at the right edge of
    // a freshly loaded scenario is still 0 until the next monthly census.
    int crime_average() const;
    int pollution_average() const;
    // Dollars for one cash-flow sample. Index 0 is the newest.
    std::int64_t cash_flow_history(HistoryScale scale, int index) const;
    // The sample the graphs plot. A fresh short-scale point of zero uses
    // the budget's current cash-flow figure instead of the empty history.
    std::int64_t plot_cash_flow(HistoryScale scale, int index) const;
    // False when the sample was reconstructed from the capped history byte.
    bool cash_flow_history_exact(HistoryScale scale, int index) const;
    // 1 when the scenario was just won, -1 when it was just lost, then 0.
    int take_scenario_outcome();
    // Speed the city was running at when the last win or loss paused it.
    int outcome_resume_speed() const { return speed_before_outcome_; }
    // True after a win or loss until Keep playing, a new scenario, or a
    // chosen running speed consumes that pause.
    bool outcome_pause_pending() const { return outcome_paused_; }
    // Leave the announcement pause and run at outcome_resume_speed().
    void resume_after_outcome();
    // Leave the clock stopped. The remembered speed stays available for
    // the next scenario.
    void stay_paused_after_outcome();
    // Body of the win/loss dialog. Keep playing is what starts the clock.
    // Closing the window, or cancelling the scenario list, leaves it paused.
    std::string scenario_outcome_text(bool won) const;
    // 0 when this city is not one of the scenarios.
    int scenario_id() const;
    // The scripted disaster countdown (0 when none is armed).
    int disaster_event() const;
    int disaster_wait() const;
    // Whole years until the scenario is scored, rounded down. -1 when no
    // scenario is running. 0 when the deadline has already passed.
    int scenario_years_left() const;
    // "30 years left", "9 years, 2 months left", or "Time is up".
    // Empty when no scenario is running.
    std::string scenario_clock_text() const;
    // Goal plus the time still left. Empty when no scenario is running.
    std::string scenario_progress() const;
    // The number the scenario is chasing, with the live reading.
    std::string scenario_meter() const;
    // Narrow forms for a tight status band (200% UI): "8y 11m left" and
    // "Traffic 62 → <40 · 92,140 → ≥90,000".
    std::string scenario_clock_short() const;
    std::string scenario_meter_short() const;
    // Tight forms for a 960-pixel band: "18k→100k · 30y".
    std::string scenario_clock_compact() const;
    std::string scenario_meter_compact() const;

    // Learn to Play: the first incomplete teaching step (0..6), or 7 when won.
    int tutorial_step() const;
    // The player used Inspect or opened Budget (steps that are not on the map).
    void note_tutorial_inspect();
    void note_tutorial_budget();
    // Place the buildings a mid-scenario demo or test needs, up through the
    // given 0-based step (that step is left for the player).
    void setup_tutorial_through(int last_complete);
    // Teaching buildings plus a small extra grid so a win-shot can grow
    // to 2,000 people. Leaves Inspect and Budget marked complete.
    void setup_tutorial_growth();
    // Run the ordinary simulator for a number of ticks. Used by demos and
    // shots so power can reach new buildings. Does not change prices or
    // tables. Leaves the clock running.
    void pump_simulation(int ticks);
    // Mid-scenario demo: teaching buildings through Safety, plus a few
    // extra R and C lots so houses and shops can grow in the shot.
    void setup_tutorial_mid();
    // Grown house and shop zone counts (centers), for mid-shot readiness.
    int tutorial_grown_homes() const;
    int tutorial_grown_shops() const;
    bool tutorial_mid_ready() const;
    // Connect power lines to every zone in the teaching town, and add a
    // plant if the grid is still short, so a win shot has no unpowered
    // alert and no blackout news.
    bool ensure_tutorial_power();
    void ensure_tutorial_park();
    void refresh_tutorial(bool announce = true);


    // Public-opinion and score data from the last cityEvaluation().
    // update_evaluation() runs that pass; evaluation() only reads it.
    struct Problem {
        std::string name;
        int votes = 0;
        // CityVotingProblems ordinal: crime, pollution, housing, taxes,
        // traffic, unemployment, fire. -1 when the row is not one of those.
        int kind = -1;
    };
    struct Evaluation {
        int year = 0;
        int score = 0;
        int score_delta = 0;
        int yes_percent = 0;
        std::int64_t population = 0;
        std::int64_t migration = 0;
        std::int64_t assessed_value = 0;
        std::string category;
        std::string difficulty;
        std::vector<Problem> problems;
        // C-5 (round 12): nobody lives in the city yet, so there is no
        // poll and no complaints; the window says so instead of showing
        // the engine's start-up defaults.
        bool no_residents = false;
        // False until a full game year has passed since the city was
        // started or loaded. The window shows a dash instead of a stale
        // migration or score change from the scenario file.
        bool deltas_known = true;
    };
    void update_evaluation();
    Evaluation evaluation() const;

    // Demand in the range -1..1 (shortage to surplus is negative..positive).
    double res_demand();
    double com_demand();
    double ind_demand();

    // Raw map word (tile character in the low bits, flags above).
    int map_value(int x, int y) const;
    int layer_value(MapLayer layer, int x, int y) const;
    // The busiest traffic-map cell (read-only). False when every cell is quiet.
    bool worst_traffic(int &x, int &y, int &value) const;
    // NEWS line for a traffic spike, with a place the mayor can jump to.
    void note_traffic_spike(int tile_x, int tile_y);
    unsigned map_serial() const;
    // Cheap samples so the shell can skip a full repaint when the picture is still.
    unsigned tile_sample() const;
    unsigned sprite_sample() const;
    // Filled by the last query. False until the player queries a tile.
    bool query_tile(int &x, int &y) const;
    struct CityNotice {
        std::string text;
        int tile_x = 0;
        int tile_y = 0;
        int month = 0;
        int year = 1900;
        bool has_place = false;
    };
    const std::vector<CityNotice> &notices() const { return notices_; }
    void clear_notices();
    // The price of the building a bulldozer at (x, y) would clear, and its
    // name, or 0 for anything that is not a whole building (A5: the shell
    // asks before clearing one worth kBulldozeConfirmCost or more).
    int bulldoze_worth(int tile_x, int tile_y, std::string *name = nullptr) const;
    static constexpr int kBulldozeConfirmCost = 400;
    // A one-line message for the toast (not the NEWS log).
    void tell(std::string text) { post_message(std::move(text)); }
    // Puts back a NEWS log saved with a recovery file (newest first).
    void restore_notices(std::vector<CityNotice> notices);
    // The camera the next save stores in its sidecar, and the one the last
    // load found there (invalid for an older save or a new city).
    void set_view_for_save(const save_view::View &view) { save_view_ = view; }
    const save_view::View &loaded_view() const { return loaded_view_; }
    // Terrain only, on a throwaway simulator. Does not touch this city.
    bool preview_terrain(int seed, int island, int rivers, int lakes, int trees, std::vector<unsigned short> &tiles) const;
    // Terrain editor: the whole map in rows (y * kWorldW + x), and writing
    // one back. The editor's own session shows the painted land this way;
    // it never runs the simulation. A vector of another size is ignored.
    std::vector<unsigned short> terrain_tiles() const;
    void set_terrain_tiles(const std::vector<unsigned short> &tiles);
    std::vector<SpriteDot> sprites() const;
    // All live sprite fields, for reload tests. 0 when the list is empty.
    unsigned sprite_signature() const;

    const std::string &save_path() const { return save_path_; }
    void set_save_path(const std::string &path) { save_path_ = path; }

private:
    struct Engine;

    void on_callback(const char *name, const char *params, va_list args);
    void notify();

    Engine *engine_;
    Listener listener_;
    void set_service_funding(int kind, int percent);
    void post_message(std::string text);
    void mark_dirty();

    std::string message_;
    std::string last_load_error_;
    int message_serial_ = 0;
    int scenario_outcome_ = 0;
    int query_serial_ = 0;
    std::string save_path_;
    std::vector<std::string> sounds_;
    bool budget_requested_ = false;
    bool goto_pending_ = false;
    int goto_x_ = 0;
    int goto_y_ = 0;
    int quake_strength_ = 0;
    bool sound_enabled_ = true;
    bool ready_ = false;
    bool simulation_paused_ = false;
    int speed_ = 2;
    bool dirty_ = false;
    int tool_stroke_placed_ = 0;
    long long stroke_spent_ = 0;
    int stroke_tiles_ = 0;
    int stroke_tool_ = -1;
    int stroke_x_ = 0;
    int stroke_y_ = 0;
    std::string stroke_reason_;
    bool placement_waiting_ = false;
    PlacementNote placement_note_;
    civic::Ledger ledger_;
    bool ledger_from_file_ = false;
    void finish_stroke_attempt(long long before, int tool, int x, int y);
    void catch_up_ledger(bool announce);
    bool powered_homes() const;
    int calendar_year() const;
    int calendar_month() const;
    // True only after new_city(), until the city is saved, loaded, or a
    // scenario starts. birth_* is the month cityTime shows at that moment
    // (January 1900 for a generated city). Changing speed does not count
    // as an edit while this is set.
    bool fresh_generated_ = false;
    int birth_month_ = 0;
    int birth_year_ = 1900;
    long long saved_city_time_ = 0;
    void mark_clean_time();
    bool in_callback_ = false;
    // The engine asked for the scenario's own disaster (Seattle's zone,
    // the Alcatraz escape, ...); it is raised on the next event frame,
    // outside the simulation step.
    bool scenario_disaster_pending_ = false;
    bool eval_month_pending_ = false;
    int eval_month_ = -1;
    int speed_before_outcome_ = 2;
    bool outcome_paused_ = false;
    bool eval_deltas_ready_ = false;
    // The "evaluation" line of a loaded sidecar (score change, migration,
    // and the population the next year's change is measured from).
    struct LoadedEval {
        bool valid = false;
        long long score_delta = 0;
        long long migration = 0;
        long long population = 0;
    };
    LoadedEval loaded_eval_;
    // The city score and the simulator phase the sidecar kept (-1 when the
    // file has none, as in a classic save or one from an older release).
    int loaded_score_ = -1;
    int loaded_phase_ = -1;
    // The simulator's random generator and simCycle at save time. Restored
    // after load init so a resumed game follows the same rolls.
    bool loaded_rng_valid_ = false;
    unsigned long long loaded_rng_ = 0;
    int loaded_sim_cycle_ = -1;
    bool loaded_valves_valid_ = false;
    short loaded_res_valve_ = 0;
    short loaded_com_valve_ = 0;
    short loaded_ind_valve_ = 0;
    void restore_loaded_eval();
    save_view::View save_view_;
    save_view::View loaded_view_;
    long long eval_anchor_time_ = 0;
    // cityTime when this city was made, loaded or started. The traffic
    // average is not measured until the engine's January evaluation after
    // it; until then the Bern meter shows a dash, not the empty-map figure.
    long long traffic_anchor_time_ = 0;
    bool traffic_measured() const;
    int scenario_result_ = 0;
    bool tutorial_inspected_ = false;
    bool tutorial_budget_seen_ = false;
    int tutorial_step_ = 0;
    int tutorial_announced_ = -1;
    // Furthest step passed; steps never go back (review nov24 #3/#4).
    int tutorial_reached_ = 0;
    // Last clock and population a tick refreshed the tutorial at (#8).
    long long tutorial_tick_time_ = -1;
    long long tutorial_tick_people_ = -1;
    std::string tutorial_meter_;
    std::string tutorial_meter_short_;
    std::string tutorial_meter_compact_;
    // Road tiles at the last refresh, for the Step 1 hint (release nov24 #4).
    int tutorial_roads_ = 0;
    void reset_tutorial();

    bool query_has_tile_ = false;
    int query_x_ = 0;
    int query_y_ = 0;
    std::vector<CityNotice> notices_;
    void arm_eval_baseline();
    void recount_population_from_map();
    void refresh_loaded_meters();
    void clear_scenario_clock();
    void remember_notice(std::string text, int tile_x, int tile_y, bool has_place);
    void post_event_notes(const std::vector<city_events::Note> &notes, bool jump = false);
    void on_event_frame();
    // A refusal from the Autonomous Zone on the click just made.
    void refuse_in_zone();
    bool write_scenario_sidecar(const std::string &path) const;
    void read_scenario_sidecar(const std::string &path);

#ifdef LUNDUKE_CITY_TEST_HOOKS
    friend int hostile_review_session_probe(CitySession &session, int op);
    friend int round9_window_probe(class AppWindow &window, int op);
#endif
};

#ifdef LUNDUKE_CITY_TEST_HOOKS
// Test hook for the census month and a budget left open across New City.
int hostile_review_session_probe(CitySession &session, int op);
#endif
