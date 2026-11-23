// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

#pragma once

// Pure timing and placement for City 2's cosmetic effects: the milestone
// ceremony, weather, brownout flicker, park and harbour motion, and the
// disaster particles. Everything here is a function of a clock and a tile
// position through look_fx::hash32. Nothing reads or advances the
// simulator's random numbers, so a replay with the same seed plays out the
// same whatever is drawn on top of it.

#include "look_fx.hpp"

#include <cmath>
#include <cstdint>

namespace effects_fx {

// ---- Milestone ceremony -------------------------------------------------

inline constexpr int kCeremonyMs = 1800;
inline constexpr int kConfettiCount = 28;
// Gold, red, blue, green, white: the slate palette's accents.
inline constexpr double kConfettiRgb[5][3] = {
    {0.96, 0.78, 0.22}, {0.89, 0.27, 0.22}, {0.27, 0.53, 0.93}, {0.33, 0.78, 0.38}, {0.98, 0.97, 0.92}};

struct Confetto {
    double x = 0;
    double y = 0;
    int color = 0;
    bool tall = false;
    double alpha = 0;
};

// One piece of the burst, relative to the launch point, in units of
// `spread` (about a third of the card width). ms is time since the
// ceremony began; before 0 or after kCeremonyMs nothing is shown.
inline Confetto confetto(int index, int ms, double spread)
{
    Confetto piece;
    if (ms < 0 || ms >= kCeremonyMs) {
        return piece;
    }
    const std::uint32_t seed = static_cast<std::uint32_t>(index) * 2654435761u + 0x51ed27u;
    // Launch between 30 and 150 degrees above the horizon.
    const double angle = (210.0 + 120.0 * look_fx::hash01(seed)) * look_fx::kPi / 180.0;
    const double speed = spread * (1.5 + 1.3 * look_fx::hash01(seed ^ 0x9e37u));
    const double t = ms / 1000.0;
    // Air drag: the burst opens fast and then drifts.
    const double drag = (1.0 - std::exp(-2.2 * t)) / 2.2;
    piece.x = std::cos(angle) * speed * drag + std::sin(t * 7.0 + index) * spread * 0.05;
    piece.y = std::sin(angle) * speed * drag + 0.5 * spread * 1.4 * t * t;
    piece.color = static_cast<int>(look_fx::hash32(seed ^ 0x77u) % 5u);
    piece.tall = ((ms / 90 + index) & 1) != 0;
    piece.alpha = ms < 1250 ? 1.0 : std::fmax(0.0, 1.0 - (ms - 1250) / 550.0);
    return piece;
}

// The card's pop: in from 86%, a touch past full size, then settled.
inline double ceremony_scale(int ms)
{
    if (ms < 0 || ms >= kCeremonyMs) {
        return 1.0;
    }
    if (ms < 220) {
        return 0.86 + 0.19 * look_fx::ease_out_cubic(ms / 220.0);
    }
    if (ms < 420) {
        return 1.05 - 0.05 * look_fx::ease_in_out((ms - 220) / 200.0);
    }
    return 1.0;
}

// A light band sweeping across the title, 0..1 across the card, or -1.
inline double ceremony_shine(int ms)
{
    if (ms < 260 || ms > 980) {
        return -1.0;
    }
    return look_fx::ease_in_out((ms - 260) / 720.0);
}

// Population milestones (and a won scenario) get the ceremony; the
// smaller notes keep the plain card.
inline bool ceremony_for_milestone(int id, int town, int megalopolis)
{
    return id >= town && id <= megalopolis;
}

// ---- Weather ------------------------------------------------------------

enum Weather : int { Clear = 0, Rain = 1, Snow = 2, Storm = 3 };

// 0 winter (Dec-Feb), 1 spring, 2 summer, 3 autumn. month is 0..11.
inline int season_of(int month)
{
    month = ((month % 12) + 12) % 12;
    if (month == 11 || month <= 1) {
        return 0;
    }
    return month <= 4 ? 1 : (month <= 7 ? 2 : 3);
}

// The month's weather, from the date and a per-city salt (a hash of the
// city name). About half the months are clear, so the overlay stays light.
inline int weather_for(int year, int month, std::uint32_t salt)
{
    const std::uint32_t stamp = static_cast<std::uint32_t>(year * 12 + (((month % 12) + 12) % 12));
    const double roll = look_fx::hash01(stamp * 0x9e3779b1u ^ salt ^ 0x3c6ef372u);
    switch (season_of(month)) {
    case 0:
        return roll < 0.50 ? Snow : (roll < 0.56 ? Rain : Clear);
    case 1:
        return roll < 0.34 ? Rain : (roll < 0.44 ? Storm : Clear);
    case 2:
        return roll < 0.12 ? Rain : (roll < 0.30 ? Storm : Clear);
    default:
        return roll < 0.38 ? Rain : (roll < 0.44 ? Storm : Clear);
    }
}

inline const char *weather_name(int weather)
{
    switch (weather) {
    case Rain:
        return "Rain";
    case Snow:
        return "Snow";
    case Storm:
        return "Storm";
    default:
        return "Clear";
    }
}

inline std::uint32_t weather_salt(const char *name)
{
    std::uint32_t h = 2166136261u;
    for (const char *c = name; c != nullptr && *c != '\0'; ++c) {
        h = (h ^ static_cast<unsigned char>(*c)) * 16777619u;
    }
    return h;
}

// Particles for a w x h view. unit is one UI pixel (dp(1)).
inline int weather_particles(int weather, double w, double h, double unit)
{
    if (weather == Clear || w <= 0 || h <= 0) {
        return 0;
    }
    unit = unit < 1.0 ? 1.0 : unit;
    const double cell = (weather == Snow ? 34.0 : 26.0) * unit;
    double count = (w * h) / (cell * cell);
    if (weather == Storm) {
        count *= 1.5;
    }
    const double cap = weather == Snow ? 170.0 : (weather == Storm ? 340.0 : 230.0);
    return static_cast<int>(count < cap ? count : cap);
}

struct Drop {
    double x = 0;
    double y = 0;
    double alpha = 0;
};

// Rain streak i (its tail end) or snowflake i in the view at clock ms.
inline Drop weather_drop(int weather, int index, double ms, double w, double h, double unit)
{
    Drop drop;
    unit = unit < 1.0 ? 1.0 : unit;
    const std::uint32_t seed = static_cast<std::uint32_t>(index) * 0x85ebca6bu + 0x1234567u;
    const double hx = look_fx::hash01(seed);
    const double hy = look_fx::hash01(seed ^ 0xabcdu);
    const double hs = look_fx::hash01(seed ^ 0x5151u);
    if (weather == Snow) {
        const double speed = (26.0 + 22.0 * hs) * unit;
        const double span = h + 8.0 * unit;
        drop.y = std::fmod(hy * span + ms / 1000.0 * speed, span) - 4.0 * unit;
        const double sway = std::sin(ms / 900.0 + index * 1.7) * 7.0 * unit;
        drop.x = std::fmod(hx * w + sway + ms / 1000.0 * 6.0 * unit + w, w);
        drop.alpha = 0.55 + 0.35 * hs;
        return drop;
    }
    const double speed = (weather == Storm ? 620.0 : 460.0) * (0.85 + 0.3 * hs) * unit;
    const double len = 9.0 * unit;
    const double span = h + len * 2.0;
    drop.y = std::fmod(hy * span + ms / 1000.0 * speed, span) - len;
    // Wind from the left: streaks lean right as they fall.
    const double lean = weather == Storm ? 0.34 : 0.22;
    drop.x = std::fmod(hx * (w + h * lean) + drop.y * lean, w + h * lean) - h * lean * 0.5;
    drop.alpha = 0.40 + 0.25 * hs;
    return drop;
}

// The lean of a rain streak, x per y.
inline double rain_lean(int weather)
{
    return weather == Storm ? 0.34 : 0.22;
}

// A lightning flash during a storm: 0 most of the time, a soft double
// flicker every 7-11 s. Never more than kFlashPeak, so it reads as a flash
// without strobing the screen.
inline constexpr double kFlashPeak = 0.14;
inline double storm_flash(double ms, std::uint32_t salt)
{
    if (ms < 0) {
        return 0.0;
    }
    const double period = 9000.0;
    const int slot = static_cast<int>(ms / period);
    const double offset = 1500.0 + 5000.0 * look_fx::hash01(static_cast<std::uint32_t>(slot) * 0x27d4eb2du ^ salt);
    const double t = ms - slot * period - offset;
    if (t < 0 || t > 420.0) {
        return 0.0;
    }
    if (t < 70.0) {
        return kFlashPeak;
    }
    if (t < 130.0) {
        return 0.0;
    }
    return kFlashPeak * 0.7 * std::exp(-(t - 130.0) / 110.0);
}

// The overlay eases in when a month's weather begins.
inline double weather_fade(double ms_since_change)
{
    return look_fx::clamp01(ms_since_change / 1500.0);
}

// ---- Brownout -----------------------------------------------------------

// How much darker an unpowered building is drawn (0..1 black). A steady dim
// always; with motion, each zone also dips now and then, on its own clock,
// for about 100 ms (and sometimes twice), never in step with its
// neighbours and never faster than about once a second.
inline constexpr double kBrownoutBase = 0.16;
inline constexpr double kBrownoutDip = 0.38;
inline double brownout_dim(int x, int y, int ms, bool moving)
{
    if (!moving) {
        return kBrownoutBase;
    }
    const std::uint32_t seed = static_cast<std::uint32_t>(x * 7919 + y * 104729 + 17);
    const int period = 1300 + static_cast<int>(1700.0 * look_fx::hash01(seed));
    const int shift = static_cast<int>(period * look_fx::hash01(seed ^ 0x6a09u));
    const int phase = ((ms + shift) % period + period) % period;
    if (phase < 90) {
        return kBrownoutDip;
    }
    if (phase < 150) {
        return 0.05;
    }
    if (phase >= 230 && phase < 290 && look_fx::hash01(seed ^ 0xbb67u) < 0.5) {
        return kBrownoutDip * 0.8;
    }
    return kBrownoutBase;
}

// ---- Parks, fountains, marinas -------------------------------------------

// Canopy shift in art pixels (-1, 0 or 1) for the tree tile at (x, y). A
// slow gust rolls across the map from the west; wind > 1 in a storm.
inline int sway_dx(int x, int y, int ms, double wind)
{
    const double wave = std::sin(2.0 * look_fx::kPi * (ms / 2600.0 - x * 0.09 - y * 0.04)) +
                        0.35 * (look_fx::hash01(static_cast<std::uint32_t>(x * 31 + y * 977 + ms / 400)) - 0.5);
    const double push = wave * wind;
    if (push > 0.72) {
        return 1;
    }
    if (push < -0.95) {
        return -1;
    }
    return 0;
}

// Boat bob in art pixels (0 or 1) for basin cell (x, y): each berth on
// its own slow swell.
inline int bob_dy(int x, int y, int ms)
{
    const std::uint32_t seed = static_cast<std::uint32_t>(x * 613 + y * 1709 + 3);
    const int period = 1300 + static_cast<int>(500.0 * look_fx::hash01(seed));
    const int phase = ((ms + static_cast<int>(period * look_fx::hash01(seed ^ 0x1fu))) % period + period) % period;
    return phase < period / 2 ? 0 : 1;
}

// Fountain spray: droplet k of kSprayDrops around a jet at the origin, in
// art pixels. alpha 0 hides it this frame.
inline constexpr int kSprayDrops = 8;
inline void spray_drop(int k, int ms, double radius, double &dx, double &dy, double &alpha)
{
    const double life = 700.0;
    const double t = std::fmod(ms + k * (life / kSprayDrops) + 37.0 * k, life) / life;
    const double angle = 2.0 * look_fx::kPi * look_fx::hash01(static_cast<std::uint32_t>(k * 97 + static_cast<int>((ms + k * (life / kSprayDrops)) / life)));
    // Up from the jet and out, falling back into the basin.
    const double out = radius * t;
    dx = std::cos(angle) * out;
    dy = std::sin(angle) * out * 0.6 - radius * 1.1 * std::sin(look_fx::kPi * t);
    alpha = t < 0.85 ? 1.0 : (1.0 - t) / 0.15;
}

// ---- Disasters ----------------------------------------------------------

// Debris piece k circling a tornado's foot, in map pixels from the foot.
// A flattened ellipse, for the 3/4 view, with pieces lifted to different
// heights.
inline constexpr int kTornadoDebris = 14;
inline void tornado_debris(int k, int ms, double &dx, double &dy, double &lift)
{
    const double h = look_fx::hash01(static_cast<std::uint32_t>(k * 7477 + 11));
    const double spin = ms * (0.0075 + 0.004 * h) + k * (2.0 * look_fx::kPi / kTornadoDebris);
    const double radius = 7.0 + 10.0 * look_fx::hash01(static_cast<std::uint32_t>(k * 331 + 5));
    dx = std::cos(spin) * radius;
    dy = std::sin(spin) * radius * 0.42;
    lift = 2.0 + 18.0 * h + 3.0 * std::sin(ms / 260.0 + k);
}

// A puff's age 0..1 on its own loop (period ms), offset by its seed.
inline double puff_age(std::uint32_t seed, double ms, double period)
{
    return std::fmod(ms / period + look_fx::hash01(seed), 1.0);
}

// Police light colour for the riot: true is red, false blue, swapping
// every 240 ms with each car on its own beat.
inline bool riot_light_red(int x, int y, int ms)
{
    return (((ms + static_cast<int>(look_fx::hash32(static_cast<std::uint32_t>(x * 53 + y * 97)) % 240u)) / 240) & 1) == 0;
}

// Screen shake obeys its own switch and the reduced-motion Effects choice.
inline bool shake_allowed(bool shake_setting, int effects)
{
    return shake_setting && look_fx::motion_allowed(effects);
}

} // namespace effects_fx
