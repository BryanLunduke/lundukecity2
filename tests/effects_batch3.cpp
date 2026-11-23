// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

// Batch 3, effects and sound: graph solo, the synthesized interface cues,
// the weather, brownout, park, welcome and disaster helpers, and proof that
// none of it touches the simulation (same seed, same city, with or without
// the effect code running alongside).

#include "city_session.hpp"
#include "effects_fx.hpp"
#include "graph_solo.hpp"
#include "look_fx.hpp"
#include "ui_sounds.hpp"

#include "micropolis.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char *what)
{
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

void test_graph_solo()
{
    check(graph_solo_mask(0x7f, 2) == (1 << 2), "click a series from all: only that one");
    check(graph_solo_mask(1 << 2, 2) == 0x7f, "click the soloed series again: all back");
    check(graph_solo_mask(1 << 2, 5) == (1 << 5), "click another while soloed: it takes over");
    check(graph_solo_mask(0x55, 0) == 1, "click from a partial mask solos");
    check(graph_solo_mask(0x7f, -1) == 0x7f && graph_solo_mask(0x7f, 7) == 0x7f, "out-of-range series change nothing");
}

void test_sounds()
{
    constexpr int rate = 22050;
    std::set<std::string> keys;
    for (int cue = 0; cue < ui_sounds::CueCount; ++cue) {
        keys.insert(ui_sounds::cue_key(cue));
        const std::vector<std::int16_t> a = ui_sounds::cue_samples(cue, rate);
        const std::vector<std::int16_t> b = ui_sounds::cue_samples(cue, rate);
        check(a == b, "a cue renders the same every time");
        check(!a.empty() && a.size() <= static_cast<std::size_t>(rate * 0.6), "a cue is short (under 0.6 s)");
        int peak = 0;
        double energy = 0;
        for (std::int16_t s : a) {
            peak = std::max(peak, std::abs(static_cast<int>(s)));
            energy += static_cast<double>(s) * s;
        }
        check(peak > 2000, "a cue is audible");
        check(peak < 32000, "a cue does not clip");
        check(std::sqrt(energy / static_cast<double>(a.size())) > 400.0, "a cue has body");
        check(std::abs(a.front()) < 2500 && std::abs(a.back()) < 800, "a cue starts and ends without a click");
    }
    check(keys.size() == static_cast<std::size_t>(ui_sounds::CueCount), "every cue has its own mixer key");
    check(ui_sounds::cue_for_stroke(true, false, "") == ui_sounds::Place, "a good stroke places");
    check(ui_sounds::cue_for_stroke(true, true, "") == ui_sounds::Bulldoze, "a good bulldoze bulldozes");
    check(ui_sounds::cue_for_stroke(false, false, "Not enough funds") == ui_sounds::NoFunds, "no funds says so");
    check(ui_sounds::cue_for_stroke(false, true, "Can't build on water") == ui_sounds::Error, "other refusals are errors");
    check(ui_sounds::replaced_engine_sound("UhUh") && ui_sounds::replaced_engine_sound("Sorry") &&
              !ui_sounds::replaced_engine_sound("Explosion-High"),
          "only the refusal clips are replaced");
}

void test_weather()
{
    using namespace effects_fx;
    int clear = 0;
    int total = 0;
    bool snow_in_summer = false;
    bool snow_in_winter = false;
    for (int year = 1900; year < 2000; ++year) {
        for (int month = 0; month < 12; ++month) {
            const int w = weather_for(year, month, weather_salt("Dullsville"));
            check(w == weather_for(year, month, weather_salt("Dullsville")), "weather is a function of the date");
            ++total;
            clear += w == Clear ? 1 : 0;
            if (season_of(month) == 2 && w == Snow) {
                snow_in_summer = true;
            }
            if (season_of(month) == 0 && w == Snow) {
                snow_in_winter = true;
            }
        }
    }
    check(!snow_in_summer, "never snow in summer");
    check(snow_in_winter, "snow does fall in winter");
    check(clear > total * 0.35 && clear < total * 0.75, "about half the months are clear");
    check(weather_for(1950, 3, weather_salt("A")) == weather_for(1950, 3, weather_salt("A")), "salt is stable");
    check(weather_salt("Hamburg") != weather_salt("Tokyo"), "cities differ");
    check(weather_particles(Clear, 1280, 720, 1.0) == 0, "clear sky has no particles");
    check(weather_particles(Storm, 1280, 720, 1.0) <= 340 && weather_particles(Rain, 1280, 720, 1.0) <= 230, "particle caps");
    double peak = 0;
    for (int ms = 0; ms < 60000; ms += 7) {
        peak = std::max(peak, storm_flash(ms, 99));
    }
    check(peak <= kFlashPeak + 1e-9 && peak > 0.02, "lightning stays gentle but happens");
    check(weather_fade(0) <= 0.01 && weather_fade(5000) >= 0.99, "weather fades in");
    for (int i = 0; i < 200; ++i) {
        const Drop d = weather_drop(Rain, i, 1234.0, 960, 540, 1.0);
        check(d.alpha >= 0 && d.alpha <= 0.7, "rain alpha is faint");
    }
}

void test_power_and_parks()
{
    using namespace effects_fx;
    check(brownout_dim(3, 4, 1000, false) == kBrownoutBase, "still dim without motion");
    int dips = 0;
    int last = -10000;
    int min_gap = 100000;
    for (int ms = 0; ms < 60000; ms += 10) {
        const double d = brownout_dim(7, 9, ms, true);
        check(d >= 0.0 && d <= kBrownoutDip + 1e-9, "dim within bounds");
        if (d >= kBrownoutDip * 0.79 && d > kBrownoutBase + 1e-9) {
            if (ms - last > 150) {
                if (last >= 0) {
                    min_gap = std::min(min_gap, ms - last);
                }
                ++dips;
            }
            last = ms;
        }
    }
    check(dips > 20 && dips < 200, "flicker now and then, not constantly");
    check(min_gap >= 1000, "a zone dips no faster than about once a second");
    int same = 0;
    for (int ms = 0; ms < 3000; ms += 10) {
        same += brownout_dim(1, 1, ms, true) == brownout_dim(2, 1, ms, true) ? 1 : 0;
    }
    check(same < 300, "neighbours do not flicker in step");
    bool left = false, right = false;
    for (int ms = 0; ms < 20000; ms += 50) {
        const int dx = sway_dx(10, 10, ms, 1.0);
        check(dx >= -1 && dx <= 1, "sway is one pixel at most");
        left = left || dx < 0;
        right = right || dx > 0;
        const int dy = bob_dy(10, 10, ms);
        check(dy == 0 || dy == 1, "bob is 0 or 1 px");
    }
    check(left && right, "the canopy leans both ways");
    for (int k = 0; k < kSprayDrops; ++k) {
        for (int ms = 0; ms < 3000; ms += 37) {
            double dx, dy, a;
            spray_drop(k, ms, 4.0, dx, dy, a);
            check(std::fabs(dx) <= 4.5 && dy <= 4.0 && dy >= -8.0 && a >= 0 && a <= 1.0 + 1e-9, "spray stays near the jet");
        }
    }
}

void test_ceremony()
{
    using namespace effects_fx;
    check(confetto(0, -1, 100).alpha == 0 && confetto(0, kCeremonyMs, 100).alpha == 0, "no confetti outside the ceremony");
    for (int i = 0; i < kConfettiCount; ++i) {
        for (int ms = 0; ms < kCeremonyMs; ms += 50) {
            const Confetto c = confetto(i, ms, 100.0);
            check(std::fabs(c.x) < 400 && c.y > -400 && c.y < 400, "confetti stays near the card");
            check(c.color >= 0 && c.color < 5 && c.alpha >= 0 && c.alpha <= 1.0, "confetto colour and fade");
        }
        check(confetto(i, kCeremonyMs - 20, 100).alpha < 0.2, "confetti has faded by the end");
    }
    check(ceremony_for_milestone(4, 3, 9) && !ceremony_for_milestone(2, 3, 9) && !ceremony_for_milestone(10, 3, 9), "only the big milestones get the ceremony");
    check(ceremony_scale(0) <= 1.0 && std::fabs(ceremony_scale(5000) - 1.0) < 1e-9, "the card settles at full size");
    for (int ms = 0; ms < 600; ms += 10) {
        const double s = ceremony_scale(ms);
        check(s > 0.5 && s < 1.15, "the card pop stays modest");
    }
}

void test_welcome_and_disasters()
{
    using namespace effects_fx;
    for (int k = 0; k < kWelcomeClouds; ++k) {
        double lo = 1e9, hi = -1e9;
        for (int ms = 0; ms < 600000; ms += 997) {
            const double x = welcome_cloud_x(k, ms, 1712);
            lo = std::min(lo, x);
            hi = std::max(hi, x);
        }
        check(lo >= -261 && hi <= 1712 + 261, "clouds wrap off both edges");
        check(welcome_cloud_x(k, 5000, 1712) > welcome_cloud_x(k, 0, 1712) || welcome_cloud_x(k, 5000, 1712) < -200,
              "clouds drift right");
    }
    for (int k = 0; k < 6; ++k) {
        double on = 0;
        int n = 0;
        for (int ms = 0; ms < 20000; ms += 20) {
            const double a = welcome_light(k, ms);
            check(a >= 0 && a <= 1.0, "light alpha");
            on += a > 0 ? 1 : 0;
            ++n;
        }
        check(on / n > 0.15 && on / n < 0.45, "warning lights blink slowly");
    }
    for (int k = 0; k < 40; ++k) {
        double x, y, a;
        welcome_glint(k, 7777, 0, 470, 330, 680, x, y, a);
        check(x >= 0 && x <= 330 && y >= 470 && y <= 1150 && a >= 0 && a <= 1, "glint inside its box");
    }
    for (int k = 0; k < kTornadoDebris; ++k) {
        for (int ms = 0; ms < 5000; ms += 100) {
            double dx, dy, lift;
            tornado_debris(k, ms, dx, dy, lift);
            check(std::fabs(dx) <= 17.1 && std::fabs(dy) <= 7.2 && lift >= -1 && lift <= 24, "debris circles the funnel");
        }
    }
    int red = 0, blue = 0;
    for (int ms = 0; ms < 2400; ms += 10) {
        (riot_light_red(5, 6, ms) ? red : blue) += 1;
    }
    check(red > 80 && blue > 80, "police lights alternate");
    check(puff_age(3, 12345.0, 900.0) >= 0 && puff_age(3, 12345.0, 900.0) < 1.0, "puff age wraps in 0..1");
    check(shake_allowed(true, look_fx::EffectsFull) && shake_allowed(true, look_fx::EffectsCalm), "shake on by default");
    check(!shake_allowed(false, look_fx::EffectsFull), "the Screen shake switch turns it off");
    check(!shake_allowed(true, look_fx::EffectsOff), "reduced motion turns it off");
}

// The state of a run, enough to notice any change in the simulation.
struct Snapshot {
    std::uint64_t map = 1469598103934665603ull;
    std::int64_t funds = 0;
    std::int64_t population = 0;
    long long time = 0;
    std::size_t sprites = 0;
    long long sprite_sum = 0;
    bool operator==(const Snapshot &o) const
    {
        return map == o.map && funds == o.funds && population == o.population && time == o.time && sprites == o.sprites &&
               sprite_sum == o.sprite_sum;
    }
};

Snapshot snapshot(const CitySession &session)
{
    Snapshot s;
    for (int y = 0; y < CitySession::kWorldH; ++y) {
        for (int x = 0; x < CitySession::kWorldW; ++x) {
            s.map = (s.map ^ static_cast<std::uint64_t>(session.map_value(x, y) & 0xffff)) * 1099511628211ull;
        }
    }
    s.funds = session.funds();
    s.population = session.population();
    s.time = session.city_time();
    s.sprites = session.sprites().size();
    for (const auto &dot : session.sprites()) {
        s.sprite_sum += static_cast<long long>(dot.type) * 100003 + dot.x * 31 + dot.y;
    }
    return s;
}

// Everything the screen would ask of effects_fx and ui_sounds in a frame.
volatile double sink = 0;
void run_effect_code(int tick)
{
    using namespace effects_fx;
    const int ms = tick * 100;
    for (int k = 0; k < 40; ++k) {
        sink = sink + weather_drop(Storm, k, ms, 1280, 720, 1.0).x + storm_flash(ms, 5) + weather_fade(ms);
        sink = sink + brownout_dim(k, tick, ms, true) + sway_dx(k, tick, ms, 1.7) + bob_dy(k, tick, ms);
        sink = sink + confetto(k % kConfettiCount, ms % kCeremonyMs, 120).x + welcome_light(k % 6, ms);
        double dx, dy, a;
        spray_drop(k % kSprayDrops, ms, 4, dx, dy, a);
        tornado_debris(k % kTornadoDebris, ms, dx, dy, a);
        sink = sink + dx + dy + a + (riot_light_red(k, tick, ms) ? 1 : 0);
    }
    if (tick % 50 == 0) {
        sink = sink + ui_sounds::cue_samples(tick % ui_sounds::CueCount, 22050).size();
    }
}

const char *tmp_base()
{
    const char *dir = std::getenv("TMPDIR");
    return dir && dir[0] ? dir : "/tmp";
}

// A city that is busy enough to matter, saved with its random state, so
// every run below starts from the very same engine state. (A fresh city or
// a scenario load seeds the engine's generator from the clock.)
std::string make_start_city()
{
    const std::string path = std::string(tmp_base()) + "/effects_batch3_start.cty";
    CitySession session;
    if (!session.load_scenario(SC_BOSTON)) {
        std::fprintf(stderr, "Boston did not load\n");
        std::exit(2);
    }
    session.set_speed(3);
    session.set_simulation_paused(false);
    for (int i = 0; i < 48; ++i) {
        session.tick();
        if (session.budget_pending()) {
            session.commit_pending_budget();
        }
        (void)session.take_budget_request();
        (void)session.take_scenario_outcome();
    }
    if (!session.save_city_as(path)) {
        std::fprintf(stderr, "could not save the start city\n");
        std::exit(2);
    }
    return path;
}

Snapshot run_city(const std::string &path, bool with_effects, int ticks)
{
    CitySession session;
    if (!session.load_city(path)) {
        std::fprintf(stderr, "start city did not load: %s\n", session.message().c_str());
        std::exit(2);
    }
    session.set_speed(3);
    session.set_simulation_paused(false);
    for (int i = 0; i < ticks; ++i) {
        if (i == ticks / 4) {
            session.disaster_tornado();
        }
        if (i == ticks / 2) {
            session.disaster_flood();
        }
        if (i == 3 * ticks / 4) {
            session.disaster_earthquake();
        }
        session.tick();
        if (session.budget_pending()) {
            session.commit_pending_budget();
        }
        (void)session.take_budget_request();
        (void)session.take_scenario_outcome();
        (void)session.take_earthquake();
        if (with_effects) {
            run_effect_code(i);
        }
    }
    return snapshot(session);
}

void test_sim_unchanged()
{
    const std::string start = make_start_city();
    const Snapshot plain = run_city(start, false, 600);
    const Snapshot again = run_city(start, false, 600);
    const Snapshot with_fx = run_city(start, true, 600);
    if (!(plain == again)) {
        std::fprintf(stderr, "map %llx/%llx funds %lld/%lld pop %lld/%lld time %lld/%lld sprites %zu/%zu sum %lld/%lld\n",
                     static_cast<unsigned long long>(plain.map), static_cast<unsigned long long>(again.map),
                     static_cast<long long>(plain.funds), static_cast<long long>(again.funds),
                     static_cast<long long>(plain.population), static_cast<long long>(again.population), plain.time,
                     again.time, plain.sprites, again.sprites, plain.sprite_sum, again.sprite_sum);
    }
    check(plain == again, "the same seed gives the same city");
    check(plain == with_fx, "effects and sounds running alongside leave the city unchanged");
    check(plain.time > 0, "the city ran");
}

} // namespace

int main()
{
    test_graph_solo();
    test_sounds();
    test_weather();
    test_power_and_parks();
    test_ceremony();
    test_welcome_and_disasters();
    test_sim_unchanged();
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("effects_batch3 ok\n");
    return 0;
}
