// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

#pragma once

// Include "micropolis.h" before this header (it has no include guard).

// What a map tile is, in the Inspect card's words. Every tile number
// 0..1023 has one answer, including the animation frames the engine keeps
// past the nuclear plant (draw bridges, radar dishes, park fountains,
// industrial smoke, coal chimneys, the stadium game, nuclear swirl) and the
// extra church art. Landmarks are named by the special-building table
// before this is asked.
//
// The order follows the engine's own query table (idArray in tool.cpp),
// but each animation range is folded into the building it belongs to.
namespace tile_kind {

inline const char *name(int tile)
{
    if (tile < 0 || tile >= TILE_COUNT) {
        return "Land";
    }
    if (tile == DIRT) {
        return "Clear";
    }
    if (tile >= WATER_LOW && tile <= WATER_HIGH) {
        return "Water";
    }
    if (tile >= WOODS2 && tile <= WOODS5) {
        // The park tool lays these four tree tiles.
        return "Park";
    }
    if (tile >= WOODS_LOW && tile <= WOODS_HIGH) {
        return "Trees";
    }
    if (tile >= RUBBLE && tile <= LASTRUBBLE) {
        return "Rubble";
    }
    if (tile >= FLOOD && tile <= LASTFLOOD) {
        return "Flood";
    }
    if (tile == RADTILE) {
        return "Radiation";
    }
    if (tile >= FIREBASE && tile <= LASTFIRE) {
        return "Fire";
    }
    if (tile >= ROADBASE && tile <= LASTROAD) {
        return "Road";
    }
    if (tile >= POWERBASE && tile <= LASTPOWER) {
        return "Power line";
    }
    if (tile >= RAILBASE && tile <= LASTRAIL) {
        return "Rail";
    }
    if (tile >= HOSPITALBASE && tile < CHURCHBASE) {
        return "Hospital";
    }
    if (tile >= CHURCHBASE && tile < COMBASE) {
        return "Church";
    }
    if (tile >= RESBASE && tile < HOSPITALBASE) {
        return "Residential";
    }
    if (tile >= COMBASE && tile < INDBASE) {
        return "Commercial";
    }
    if (tile >= INDBASE && tile < PORTBASE) {
        return "Industrial";
    }
    if (tile >= PORTBASE && tile < AIRPORTBASE) {
        return "Seaport";
    }
    if (tile >= AIRPORTBASE && tile < COALBASE) {
        return "Airport";
    }
    if (tile >= COALBASE && tile < FIRESTBASE) {
        return "Coal Plant";
    }
    if (tile >= FIRESTBASE && tile < POLICESTBASE) {
        return "Fire Station";
    }
    if (tile >= POLICESTBASE && tile < STADIUMBASE) {
        return "Police Station";
    }
    if (tile >= STADIUMBASE && tile < NUCLEARBASE) {
        return "Stadium";
    }
    if (tile >= NUCLEARBASE && tile <= LASTZONE) {
        return "Nuclear Plant";
    }
    // 827 is the no-power bolt, drawn over a zone centre but never stored.
    if (tile >= HBRDG0 && tile <= HBRDG3) {
        // A draw bridge raised for a boat is still the road.
        return "Road";
    }
    if (tile >= RADAR0 && tile < FOUNTAIN) {
        // The radar dish turns on the airport.
        return "Airport";
    }
    if (tile >= FOUNTAIN && tile < INDBASE2) {
        // The fountain the park tool sometimes lays, animated.
        return "Park";
    }
    if (tile >= INDBASE2 && tile < TINYEXP) {
        // Industrial building animation and smoke.
        return "Industrial";
    }
    if (tile >= TINYEXP && tile <= TINYEXPLAST) {
        return "Rubble";
    }
    if (tile >= COALSMOKE1 && tile < FOOTBALLGAME1) {
        return "Coal Plant";
    }
    if (tile >= FOOTBALLGAME1 && tile < VBRDG0) {
        return "Stadium";
    }
    if (tile >= VBRDG0 && tile <= VBRDG3) {
        return "Road";
    }
    if (tile >= NUKESWIRL1 && tile <= NUKESWIRL4) {
        return "Nuclear Plant";
    }
    if (tile >= CHURCH1BASE && tile < CHURCH7BASE + 9) {
        return "Church";
    }
    return "Land";
}

} // namespace tile_kind
