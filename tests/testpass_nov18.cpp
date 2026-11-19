// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

// Regressions for the November 18 test pass: the New City preview seed,
// and the display fixes that followed it.

#include "city_seed.hpp"
#include "civic.hpp"

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

// The Year in Review's start-of-year funds survive a save, and a sidecar
// from before they were kept still loads with them unknown.
int check_start_funds()
{
    civic::Ledger ledger;
    ledger.have_start_funds = true;
    ledger.start_funds = 10179;
    const std::string text = civic::encode_ledger(ledger);
    civic::Ledger back;
    if (!civic::decode_ledger(text, back) || !back.have_start_funds || back.start_funds != 10179) {
        return fail(20, "start funds lost in the sidecar: " + text);
    }
    civic::Ledger old;
    if (!civic::decode_ledger("m1=1901.0,p=480.1.2.3.4.5.6.7", old) || old.have_start_funds ||
        old.prior_population != 480 || !old.hit[1].earned) {
        return fail(21, "older sidecar did not load the same");
    }
    civic::YearDigest digest;
    digest.opening_funds = 20000;
    digest.closing_funds = 10251;
    const civic::ReviewText review = civic::make_review_text(digest, 0, 0, 0, true, "Village", 0, 0);
    if (review.funds.find("Start of year $20,000") == std::string::npos ||
        review.funds.find("Lost $9,749") == std::string::npos) {
        return fail(22, "review funds text: " + review.funds);
    }
    return 0;
}

} // namespace

int main()
{
    if (const int r = check_auto_seed()) {
        return r;
    }
    if (const int r = check_start_funds()) {
        return r;
    }
    std::puts("testpass_nov18 ok");
    return 0;
}
