// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

// A mid-game save comes back with the same tiles, score, clock, sidecar
// RNG / valves / phase, and sprites (trains, ships, planes, helicopters,
// monsters, tornados, and the rest), so a resumed city starts from the
// same simulator state the uninterrupted game had.

#include "city_session.hpp"
#include "city_tail.hpp"
#include "scenario_sidecar.hpp"

#include "micropolis.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace {

int fail(int code, const std::string &message)
{
    std::fprintf(stderr, "%s\n", message.c_str());
    return code;
}

void run_ticks(CitySession &session, long count)
{
    for (long i = 0; i < count; ++i) {
        session.tick();
        if (session.budget_pending()) {
            session.commit_pending_budget();
        }
        (void)session.take_budget_request();
        (void)session.take_scenario_outcome();
    }
}

void run_until(CitySession &session, long long time)
{
    for (long guard = 0; session.city_time() < time && guard < 20000000; ++guard) {
        run_ticks(session, 1);
    }
}

const char *tmp_base()
{
    const char *dir = std::getenv("TMPDIR");
    return dir && dir[0] ? dir : "/tmp";
}

bool file_has_magic(const std::string &path, const char magic[4])
{
    std::ifstream in(path, std::ios::binary);
    std::vector<unsigned char> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (data.size() < 4) {
        return false;
    }
    for (std::size_t i = 0; i + 4 <= data.size(); ++i) {
        if (data[i] == static_cast<unsigned char>(magic[0]) &&
            data[i + 1] == static_cast<unsigned char>(magic[1]) &&
            data[i + 2] == static_cast<unsigned char>(magic[2]) &&
            data[i + 3] == static_cast<unsigned char>(magic[3])) {
            return true;
        }
    }
    return false;
}

// Drop an LCP1 trailer so the file looks like a pre-sprite save. LCV1, when
// present, stays at the end.
bool strip_lcp1(const std::string &src, const std::string &dst)
{
    std::ifstream in(src, std::ios::binary);
    std::vector<unsigned char> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (data.size() < kClassicCityBytes) {
        return false;
    }
    unsigned version = 0, flags = 0, corner = 0;
    const bool marked = city_format_marker(data.data(), data.size(), version, flags, corner);
    const std::size_t body = marked ? data.size() - kCityFormatBytes : data.size();
    std::size_t cut_at = body;
    std::size_t cut_len = 0;
    for (std::size_t i = kClassicCityBytes; i + 8 <= body; ++i) {
        if (data[i] == 'L' && data[i + 1] == 'C' && data[i + 2] == 'P' && data[i + 3] == '1') {
            const std::size_t payload =
                static_cast<std::size_t>(data[i + 6]) | (static_cast<std::size_t>(data[i + 7]) << 8);
            if (i + 8 + payload <= body) {
                cut_at = i;
                cut_len = 8 + payload;
                break;
            }
        }
    }
    if (cut_len == 0) {
        return false;
    }
    std::vector<unsigned char> out;
    out.insert(out.end(), data.begin(), data.begin() + static_cast<std::ptrdiff_t>(cut_at));
    out.insert(out.end(), data.begin() + static_cast<std::ptrdiff_t>(cut_at + cut_len),
               data.begin() + static_cast<std::ptrdiff_t>(body));
    if (marked) {
        out.insert(out.end(), data.begin() + static_cast<std::ptrdiff_t>(body), data.end());
    }
    if (classify_city_bytes(out.data(), out.size()) != CityFileKind::Complete) {
        return false;
    }
    std::ofstream file(dst, std::ios::binary | std::ios::trunc);
    return file.write(reinterpret_cast<const char *>(out.data()), static_cast<std::streamsize>(out.size())).good();
}

int check_reload(CitySession &session, const std::string &path, const char *label)
{
    if (!session.save_city_as(path)) {
        return fail(2, std::string(label) + " did not save");
    }
    if (classify_city_path(path) != CityFileKind::Complete) {
        return fail(6, std::string(label) + " save was not a complete city file");
    }
    const unsigned tiles = session.tile_sample();
    const int score = session.evaluation().score;
    const long long time = session.city_time();
    const int wait = session.disaster_wait();
    const int event = session.disaster_event();
    const unsigned sprites = session.sprite_signature();
    const std::size_t sprite_n = session.sprites().size();

    std::ifstream in(scenario_sidecar_path(path));
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (text.find("\"rng\"") == std::string::npos || text.find("\"sim_cycle\"") == std::string::npos ||
        text.find("\"valves\"") == std::string::npos || text.find("\"phase\"") == std::string::npos) {
        return fail(3, "the sidecar did not store rng, sim_cycle, valves, and phase");
    }

    CitySession loaded;
    if (!loaded.load_city(path)) {
        return fail(4, std::string(label) + " did not reload: " + loaded.message());
    }
    if (loaded.city_time() != time || loaded.evaluation().score != score || loaded.tile_sample() != tiles ||
        loaded.disaster_wait() != wait || loaded.disaster_event() != event) {
        return fail(5, std::string(label) + " reloaded city did not match the save (score " +
                           std::to_string(loaded.evaluation().score) + " vs " + std::to_string(score) +
                           ", tiles " + (loaded.tile_sample() == tiles ? "ok" : "differ") + ")");
    }
    if (loaded.sprite_signature() != sprites || loaded.sprites().size() != sprite_n) {
        return fail(7, std::string(label) + " sprites did not match after reload (count " +
                           std::to_string(loaded.sprites().size()) + " vs " + std::to_string(sprite_n) + ")");
    }
    if (sprite_n > 0 && !file_has_magic(path, "LCP1")) {
        return fail(8, std::string(label) + " had sprites but the save had no LCP1 trailer");
    }
    return 0;
}

} // namespace

int main()
{
    CitySession session;
    if (!session.load_scenario(SC_BOSTON)) {
        return fail(1, "Boston did not load: " + session.message());
    }
    session.set_speed(3);
    session.set_simulation_paused(false);
    const long long start = session.city_time();
    run_until(session, start + 12 * 4);

    const std::string path = std::string(tmp_base()) + "/reload_determinism_boston.cty";
    if (int err = check_reload(session, path, "Boston month 12")) {
        return err;
    }

    // A disaster sprite must come back too, not only the everyday traffic.
    session.disaster_tornado();
    run_ticks(session, 8);
    bool saw_tornado = false;
    for (const auto &dot : session.sprites()) {
        if (dot.type == SPRITE_TORNADO) {
            saw_tornado = true;
            break;
        }
    }
    if (!saw_tornado) {
        return fail(9, "a tornado did not appear after disaster_tornado");
    }
    const std::string tornado_path = std::string(tmp_base()) + "/reload_determinism_tornado.cty";
    if (int err = check_reload(session, tornado_path, "Boston tornado")) {
        return err;
    }

    // Older saves without LCP1 still load; sprites are empty, as before.
    const std::string old_path = std::string(tmp_base()) + "/reload_determinism_old.cty";
    if (!strip_lcp1(tornado_path, old_path)) {
        return fail(10, "could not write an old-style save without LCP1");
    }
    if (file_has_magic(old_path, "LCP1")) {
        return fail(11, "the stripped save still had an LCP1 trailer");
    }
    CitySession old_load;
    if (!old_load.load_city(old_path)) {
        return fail(12, "an older save without LCP1 did not load: " + old_load.message());
    }
    // doSimInit may still spawn a new train or ship, as it did before the
    // trailer existed. The tornado from the later save must not come back.
    bool restored_tornado = false;
    for (const auto &dot : old_load.sprites()) {
        if (dot.type == SPRITE_TORNADO) {
            restored_tornado = true;
            break;
        }
    }
    if (restored_tornado) {
        return fail(13, "an older save without LCP1 restored a tornado");
    }
    return 0;
}
