// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

// C1: the save-format marker. A city with Data Center or micro nuclear
// tiles carries a guarded corner tile so builds before the marker refuse
// it; this build reads it back unchanged. A newer format is refused.

#include "city_glance.hpp"
#include "city_tail.hpp"

#include "micropolis.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

int fail(int code, const std::string &message)
{
    std::fprintf(stderr, "%s\n", message.c_str());
    return code;
}

std::vector<unsigned char> slurp(const std::string &path)
{
    std::ifstream in(path, std::ios::binary);
    return std::vector<unsigned char>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void spit(const std::string &path, const std::vector<unsigned char> &bytes)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

int corner_raw(const std::vector<unsigned char> &bytes)
{
    // The map starts at 3120 bytes; tiles are big-endian shorts.
    return (static_cast<int>(bytes[3120]) << 8) | static_cast<int>(bytes[3121]);
}

} // namespace

int main()
{
    const std::filesystem::path dir = std::filesystem::temp_directory_path();
    const std::string plain_path = (dir / "lunduke-format-plain.cty").string();
    const std::string dc_path = (dir / "lunduke-format-dc.cty").string();
    const std::string newer_path = (dir / "lunduke-format-newer.cty").string();

    Micropolis sim;
    sim.generateSomeCity(7);
    const unsigned short corner = sim.map[0][0];

    // A city without the new tiles: a marker, no guard, corner untouched.
    if (!sim.saveCityAs(plain_path.c_str())) {
        return fail(1, "could not save a plain city");
    }
    std::vector<unsigned char> plain = slurp(plain_path);
    unsigned version = 0;
    unsigned flags = 0;
    unsigned saved_corner = 0;
    if (!city_format_marker(plain.data(), plain.size(), version, flags, saved_corner) ||
        version != static_cast<unsigned>(Micropolis::kCityFormatVersion) || (flags & 1u) != 0) {
        return fail(2, "a plain save should end with an unguarded format marker");
    }
    if (corner_raw(plain) != corner) {
        return fail(3, "a plain save changed the corner tile");
    }
    if (classify_city_bytes(plain.data(), plain.size()) != CityFileKind::Complete) {
        return fail(4, "a save with the marker should classify as complete");
    }

    // A micro nuclear plant: the corner is guarded in the file only.
    sim.map[20][20] = SPECIAL_MICRONUKE | ZONEBIT | CONDBIT;
    if (!sim.saveCityAs(dc_path.c_str())) {
        return fail(5, "could not save a city with a micro nuclear plant");
    }
    if (sim.map[0][0] != corner) {
        return fail(6, "saving changed the live corner tile");
    }
    std::vector<unsigned char> guarded = slurp(dc_path);
    if (!city_format_marker(guarded.data(), guarded.size(), version, flags, saved_corner) || (flags & 1u) == 0 ||
        saved_corner != corner || corner_raw(guarded) != static_cast<int>(Micropolis::kCityFormatGuardTile)) {
        return fail(7, "a save with the new tiles should guard the corner");
    }
    if (classify_city_bytes(guarded.data(), guarded.size()) != CityFileKind::Complete) {
        return fail(8, "a guarded save should classify as complete");
    }
    // What a build before the marker sees: the raw tiles, with the guard.
    {
        std::vector<short> tiles(WORLD_W * WORLD_H);
        std::vector<short> misc(MISC_HISTORY_LENGTH / 2);
        for (int i = 0; i < WORLD_W * WORLD_H; ++i) {
            tiles[static_cast<std::size_t>(i)] =
                static_cast<short>((guarded[3120 + 2 * i] << 8) | guarded[3121 + 2 * i]);
        }
        const std::string old_reason =
            Micropolis::checkCityContents(misc.data(), tiles.data(), WORLD_W * WORLD_H, false, false, false, 0);
        if (old_reason.find("unknown tile") == std::string::npos) {
            return fail(9, "an older build's tile check should refuse the guarded file: " + old_reason);
        }
    }
    Micropolis back;
    if (!back.loadCity(dc_path.c_str())) {
        return fail(10, "could not load the guarded save: " + back.loadErrorDetail);
    }
    if (back.map[0][0] != corner || (back.map[20][20] & LOMASK) != SPECIAL_MICRONUKE) {
        return fail(11, "the guarded save did not load back unchanged");
    }
    const CityGlance glance = city_glance(dc_path);
    if (glance.summary.find("Not a valid") != std::string::npos || glance.summary.find("newer") != std::string::npos) {
        return fail(12, "the file list should read the guarded save: " + glance.summary);
    }

    // A newer format is refused with a reason, before the city changes.
    std::vector<unsigned char> newer = guarded;
    newer[newer.size() - 8] = static_cast<unsigned char>(Micropolis::kCityFormatVersion + 1);
    spit(newer_path, newer);
    Micropolis future;
    if (future.loadCity(newer_path.c_str()) || future.loadErrorDetail.find("newer version") == std::string::npos) {
        return fail(13, "a newer save format should be refused with a reason");
    }
    if (city_glance(newer_path).summary.find("newer") == std::string::npos) {
        return fail(14, "the file list should say the save is from a newer build");
    }

    // Older saves (no marker) still load.
    std::vector<unsigned char> legacy(plain.begin(), plain.end() - static_cast<long>(kCityFormatBytes));
    spit(newer_path, legacy);
    Micropolis old_city;
    if (!old_city.loadCity(newer_path.c_str()) || old_city.map[0][0] != corner) {
        return fail(15, "a save from before the marker should still load");
    }
    if (classify_city_bytes(legacy.data(), legacy.size()) != CityFileKind::Complete) {
        return fail(16, "a save from before the marker should still classify as complete");
    }

    std::remove(plain_path.c_str());
    std::remove(dc_path.c_str());
    std::remove(newer_path.c_str());
    std::puts("save_format ok");
    return 0;
}
