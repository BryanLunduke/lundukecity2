// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

#pragma once

#include <string>
#include <vector>

// The Inspect card's verdict for one tile: a status word and the reasons a
// zone is not growing. It only reads values the simulator already keeps
// (the same density, crime, pollution, land value and growth maps the
// classic query reports), so it changes nothing about the simulation.
namespace inspect_report {

// Trees: release nov24 #11 (they were "Open land", but need bulldozing first).
enum class Kind { Land, Residential, Commercial, Industrial, Building, Network, Park, Water, Hazard, Trees };

enum class Tone { Good, Neutral, Bad };

struct Facts {
    Kind kind = Kind::Land;
    // 1 on, 0 off, -1 not needed or not applicable.
    int power = -1;
    // True when a road (or rail) touches the zone edge.
    bool access = true;
    // False for power plants (coal, nuclear, micro nuclear): they work
    // without a road, so a missing one is not a problem to report.
    bool needs_road = true;
    // Part of a special building is gone. Landmarks and the Data Center
    // stop working until it is bulldozed and rebuilt; damage_effect says
    // what the city loses (empty when there is nothing to add).
    bool damaged = false;
    std::string damage_effect;
    // An income building with power and a road that earns nothing (no
    // residents in reach). Its badge says Idle instead of Working.
    bool idle = false;
    // 0..255 from the simulator maps.
    int density = 0;
    int land_value = 0;
    int crime = 0;
    int pollution = 0;
    int traffic = 0;
    // Rate-of-growth map: below 0 declining, 0 stable, above 100 fast.
    int growth = 0;
    // Demand for this zone type, -1..1 (positive means the city wants more).
    double demand = 0.0;
};

struct Report {
    std::string status;
    Tone tone = Tone::Neutral;
    std::vector<std::string> reasons;
    // The first thing a mayor can do, or a short line when the zone is fine.
    // Hover, the Inspect modal, and Evaluation Why? all show this wording.
    std::string next;
};

inline bool is_zone(Kind kind)
{
    return kind == Kind::Residential || kind == Kind::Commercial || kind == Kind::Industrial;
}

inline const char *zone_word(Kind kind)
{
    switch (kind) {
    case Kind::Residential:
        return "residential";
    case Kind::Commercial:
        return "commercial";
    case Kind::Industrial:
        return "industrial";
    default:
        return "";
    }
}

// The engine's bands (getDensity in tool.cpp): pollution and crime use the
// top two bits, land value uses 30/80/150.
inline int band_of(int value)
{
    if (value < 0) {
        value = 0;
    }
    if (value > 255) {
        value = 255;
    }
    return value >> 6;
}

inline int land_band(int value)
{
    if (value < 30) {
        return 0;
    }
    if (value < 80) {
        return 1;
    }
    if (value < 150) {
        return 2;
    }
    return 3;
}


inline void fill_next(Report &report, const Facts &facts)
{
    const bool zone = is_zone(facts.kind);
    if (facts.damaged) {
        report.next = "Bulldoze and rebuild it";
        return;
    }
    if (facts.power == 0) {
        report.next = "Connect a power line";
        return;
    }
    if (!facts.access && facts.needs_road && (zone || facts.kind == Kind::Building)) {
        report.next = "Build a road that touches it";
        return;
    }
    if (facts.idle) {
        report.next = "Bring homes within reach";
        return;
    }
    if (facts.kind == Kind::Hazard) {
        report.next = "Bulldoze it";
        return;
    }
    if (!report.reasons.empty()) {
        const std::string &reason = report.reasons.front();
        if (reason.find("Little ") == 0) {
            report.next = "Zone more of the other types, or wait for demand";
        } else if (reason.find("Heavy pollution") == 0) {
            report.next = "Keep industry and plants away from this block";
        } else if (reason.find("High crime") == 0) {
            report.next = "Add a police station nearby";
        } else if (reason.find("Low land value") == 0) {
            report.next = "Add parks or water nearby";
        } else if (reason.find("Heavy traffic") == 0) {
            report.next = "Add roads or rail to spread the traffic";
        } else {
            report.next = reason;
        }
        return;
    }
    if (zone) {
        if (report.status == "Growing" || report.status == "Booming") {
            report.next = "It's growing \u2014 keep power and roads connected";
        } else if (report.status == "Steady") {
            report.next = "Nothing is holding it back this month";
        } else if (report.status == "Declining") {
            report.next = "Check demand, crime, pollution, and traffic";
        } else {
            report.next = "Check power, roads, and demand";
        }
        return;
    }
    if (facts.kind == Kind::Building && report.status == "Working") {
        report.next = "It's working";
    }
}

// A city-wide complaint, in the same words Inspect uses on a tile.
inline const char *complaint_fact(int problem)
{
    switch (problem) {
    case 0:
        return "High crime: add police";
    case 1:
        return "Heavy pollution nearby";
    case 2:
        return "Little residential demand right now";
    case 4:
        return "Heavy traffic on nearby roads";
    default:
        return "";
    }
}

// A 0..255 simulator map value as the percent the Inspect bars and the
// hover card both show.
inline int map_percent(int value)
{
    if (value < 0) {
        value = 0;
    }
    if (value > 255) {
        value = 255;
    }
    return (value * 100 + 127) / 255;
}

inline Report make(const Facts &facts)
{
    Report report;
    const bool zone = is_zone(facts.kind);
    if (facts.damaged) {
        report.reasons.push_back("Damaged: bulldoze and rebuild it");
        if (!facts.damage_effect.empty()) {
            report.reasons.push_back(facts.damage_effect);
        }
    }
    if (facts.power == 0) {
        report.reasons.push_back("No power: connect a power line");
    }
    if (!facts.access && facts.needs_road && (zone || facts.kind == Kind::Building)) {
        report.reasons.push_back("No road touches it");
    }
    if (!zone) {
        if (facts.kind == Kind::Building) {
            report.status = report.reasons.empty() ? "Working" : "Needs attention";
            report.tone = report.reasons.empty() ? Tone::Good : Tone::Bad;
            if (report.reasons.empty() && facts.idle) {
                report.status = "Idle";
                report.tone = Tone::Neutral;
            }
        } else {
            report.tone = Tone::Neutral;
            switch (facts.kind) {
            case Kind::Network:
                report.status = "Network";
                break;
            case Kind::Park:
                report.status = "Park";
                report.tone = Tone::Good;
                break;
            case Kind::Water:
                // Release nov24 #11: not "Water" again under the name.
                report.status = "Bridge or power line only";
                break;
            case Kind::Trees:
                report.status = "Bulldoze to build";
                break;
            case Kind::Hazard:
                report.status = "Clear it";
                report.tone = Tone::Bad;
                break;
            default:
                report.status = "Open land";
                break;
            }
        }
        fill_next(report, facts);
        return report;
    }
    if (facts.growth > 100) {
        report.status = "Booming";
        report.tone = Tone::Good;
    } else if (facts.growth > 0) {
        report.status = "Growing";
        report.tone = Tone::Good;
    } else if (facts.growth < 0) {
        report.status = "Declining";
        report.tone = Tone::Bad;
    } else {
        report.status = "Not growing";
        report.tone = Tone::Neutral;
    }
    if (facts.power == 0 || !facts.access) {
        // Without power or a road a zone cannot grow at all.
        if (report.tone == Tone::Good) {
            report.status = "Stalled";
        }
        report.tone = Tone::Bad;
    }
    if (facts.demand <= 0.0) {
        report.reasons.push_back(std::string("Little ") + zone_word(facts.kind) + " demand right now");
    }
    if (facts.kind != Kind::Industrial && band_of(facts.pollution) >= 2) {
        report.reasons.push_back("Heavy pollution nearby");
    }
    if (band_of(facts.crime) >= 2) {
        report.reasons.push_back("High crime: add police");
    }
    if (facts.kind == Kind::Residential && land_band(facts.land_value) == 0) {
        report.reasons.push_back("Low land value: parks and water help");
    }
    if (band_of(facts.traffic) >= 2) {
        report.reasons.push_back("Heavy traffic on nearby roads");
    }
    if (report.status == "Not growing" && report.reasons.empty()) {
        // Nothing holds it back; it is simply not changing this month.
        report.status = "Steady";
    }
    fill_next(report, facts);
    return report;
}

// Where the Inspect card's bars go. Every row starts its bar after the
// widest label plus a gap and ends before the widest value ("100%", "Down")
// plus a gap, so all bars line up and none touches its text.
struct BarLayout {
    double bar_offset = 0.0; // from the left text edge
    double bar_width = 0.0;
};

inline BarLayout bar_layout(double inner, double widest_label, double widest_value, double gap)
{
    BarLayout layout;
    layout.bar_offset = widest_label + gap;
    layout.bar_width = inner - layout.bar_offset - widest_value - gap;
    if (layout.bar_width < gap) {
        layout.bar_width = gap;
    }
    return layout;
}

} // namespace inspect_report
