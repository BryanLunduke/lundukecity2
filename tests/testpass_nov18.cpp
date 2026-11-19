// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

// Regressions for the November 18 test pass: the New City preview seed,
// and the display fixes that followed it.

#include "city_seed.hpp"

#include <cstdio>
#include <string>

namespace {

int fail(int code, const std::string &message)
{
    std::fprintf(stderr, "%s\n", message.c_str());
    return code;
}

// An "auto" field previews the dialog's picked seed, and Generate builds
// that same seed instead of a new one from the clock.
int check_auto_seed()
{
    const int picked = roll_city_seed(1763520000u, 4242u);
    if (picked < 1000 || picked > 900999) {
        return fail(10, "rolled seed out of range");
    }
    if (preview_seed_for_field("auto", picked) != picked || preview_seed_for_field("", picked) != picked ||
        preview_seed_for_field(" Auto ", picked) != picked) {
        return fail(11, "auto preview does not use the picked seed");
    }
    if (preview_seed_for_field("1234", picked) != 1234 || preview_seed_for_field("0", picked) != 0) {
        return fail(12, "explicit seed preview changed");
    }
    int seed = 0;
    bool set = false;
    if (!take_city_seed("auto", seed, set)) {
        return fail(13, "auto seed rejected");
    }
    resolve_auto_seed(picked, seed, set);
    if (!set || seed != picked) {
        return fail(14, "Generate does not build the previewed seed");
    }
    if (!take_city_seed("0", seed, set)) {
        return fail(15, "seed 0 rejected");
    }
    resolve_auto_seed(picked, seed, set);
    if (!set || seed != 0) {
        return fail(16, "explicit seed 0 replaced");
    }
    return 0;
}

} // namespace

int main()
{
    if (const int r = check_auto_seed()) {
        return r;
    }
    std::puts("testpass_nov18 ok");
    return 0;
}
