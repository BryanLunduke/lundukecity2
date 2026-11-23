// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

#pragma once

// City 2's own interface cues, synthesized at start-up: no sample files,
// no third-party audio. Each cue is a few square/triangle notes with a fast
// attack and a short decay, like a 16-bit console menu. They go through the
// same mixer, mute switch and Volume slider as the city sounds.

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace ui_sounds {

enum Cue : int { Place = 0, Bulldoze, Error, NoFunds, Toast, Milestone, CueCount };

// The mixer key for a cue. A ':' never appears in a sounds/ file name, so a
// key cannot collide with an engine clip.
inline const char *cue_key(int cue)
{
    switch (cue) {
    case Place:
        return "ui:place";
    case Bulldoze:
        return "ui:bulldoze";
    case Error:
        return "ui:error";
    case NoFunds:
        return "ui:nofunds";
    case Toast:
        return "ui:toast";
    case Milestone:
        return "ui:milestone";
    default:
        return "";
    }
}

inline constexpr double kTwoPi = 6.28318530717958647692;

struct Note {
    double freq;    // Hz; 0 is noise
    double start;   // seconds
    double length;  // seconds
    double level;   // 0..1
    int wave;       // 0 square (25% pulse), 1 triangle, 2 noise
    double bend;    // Hz per second
};

// A deterministic LCG: the noise is the same every run and never touches
// the simulator's random numbers.
inline std::uint32_t noise_step(std::uint32_t &state)
{
    state = state * 1664525u + 1013904223u;
    return state;
}

inline std::vector<std::int16_t> render(const std::vector<Note> &notes, int rate)
{
    if (rate < 8000) {
        rate = 8000;
    }
    double total = 0.0;
    for (const Note &note : notes) {
        total = std::fmax(total, note.start + note.length);
    }
    const std::size_t frames = static_cast<std::size_t>(total * rate) + 1;
    std::vector<double> mix(frames, 0.0);
    std::uint32_t seed = 0x5eed1234u;
    for (const Note &note : notes) {
        const std::size_t first = static_cast<std::size_t>(note.start * rate);
        const std::size_t count = static_cast<std::size_t>(note.length * rate);
        double phase = 0.0;
        double held = 0.0;
        for (std::size_t i = 0; i < count && first + i < frames; ++i) {
            const double t = static_cast<double>(i) / rate;
            // 4 ms attack, then an exponential decay to the note's end.
            const double attack = std::fmin(1.0, t / 0.004);
            const double decay = std::exp(-4.0 * t / std::fmax(0.01, note.length));
            const double tail = std::fmin(1.0, (note.length - t) / 0.006);
            const double env = attack * decay * std::fmax(0.0, tail);
            double value = 0.0;
            if (note.wave == 2) {
                // Sample-and-hold noise, re-rolled at about 6 kHz for grit.
                if (i % static_cast<std::size_t>(std::fmax(1.0, rate / 6000.0)) == 0) {
                    held = static_cast<double>(noise_step(seed) >> 16) / 32768.0 - 1.0;
                }
                value = held;
            } else {
                const double freq = note.freq + note.bend * t;
                phase += freq / rate;
                phase -= std::floor(phase);
                if (note.wave == 0) {
                    value = phase < 0.25 ? 1.0 : -0.33;
                } else {
                    value = phase < 0.5 ? (4.0 * phase - 1.0) : (3.0 - 4.0 * phase);
                }
            }
            mix[first + i] += value * env * note.level;
        }
    }
    std::vector<std::int16_t> out(frames);
    for (std::size_t i = 0; i < frames; ++i) {
        double v = mix[i];
        v = v > 1.0 ? 1.0 : (v < -1.0 ? -1.0 : v);
        out[i] = static_cast<std::int16_t>(std::lround(v * 0.55 * 32767.0));
    }
    return out;
}

inline std::vector<Note> notes_for(int cue)
{
    switch (cue) {
    case Place:
        // A soft two-note "tock-tick", up a fifth.
        return {{523.25, 0.0, 0.05, 0.55, 1, 0.0}, {783.99, 0.035, 0.06, 0.45, 1, 0.0}};
    case Bulldoze:
        // A low crunch: falling square plus a burst of noise.
        return {{180.0, 0.0, 0.16, 0.45, 0, -500.0}, {0.0, 0.0, 0.14, 0.35, 2, 0.0}};
    case Error:
        // Two low buzzes, the second lower: "uh-uh".
        return {{220.0, 0.0, 0.08, 0.40, 0, 0.0}, {174.61, 0.10, 0.10, 0.40, 0, 0.0}};
    case NoFunds:
        // A falling three-note "nope" on the triangle.
        return {{659.25, 0.0, 0.07, 0.5, 1, 0.0}, {523.25, 0.07, 0.07, 0.5, 1, 0.0}, {392.0, 0.14, 0.12, 0.5, 1, 0.0}};
    case Toast:
        // A gentle single bell: triangle with a soft octave.
        return {{880.0, 0.0, 0.18, 0.40, 1, 0.0}, {1760.0, 0.0, 0.10, 0.12, 1, 0.0}};
    case Milestone:
        // A short rising arpeggio, C E G C, with the top note held.
        return {{523.25, 0.00, 0.10, 0.45, 0, 0.0},
                {659.25, 0.08, 0.10, 0.45, 0, 0.0},
                {783.99, 0.16, 0.10, 0.45, 0, 0.0},
                {1046.50, 0.24, 0.34, 0.50, 1, 0.0},
                {523.25, 0.24, 0.34, 0.20, 1, 0.0}};
    default:
        return {};
    }
}

inline std::vector<std::int16_t> cue_samples(int cue, int rate)
{
    return render(notes_for(cue), rate);
}

// Which cue a finished stroke earns. reason is the refusal text the
// placement receipt shows ("Not enough funds", "Can't build on water").
inline int cue_for_stroke(bool ok, bool bulldozer, const std::string &reason)
{
    if (ok) {
        return bulldozer ? Bulldoze : Place;
    }
    if (reason.find("funds") != std::string::npos || reason.find("afford") != std::string::npos ||
        reason.find("money") != std::string::npos) {
        return NoFunds;
    }
    return Error;
}

// The engine's own interface clips ("UhUh", "Sorry") say the same thing as
// the Error and NoFunds cues; the shell plays only the cue.
inline bool replaced_engine_sound(const std::string &name)
{
    return name == "UhUh" || name == "Sorry" || name == "uhuh" || name == "sorry";
}

} // namespace ui_sounds
