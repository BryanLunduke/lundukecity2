// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

// Regressions for the November 18 test pass: the New City preview seed,
// and the display fixes that followed it.

#include "city_seed.hpp"
#include "civic.hpp"
#include "save_view.hpp"

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

// A save keeps the camera and the NEWS log in its sidecar strings, a
// quote or semicolon in a notice survives, and junk or an older sidecar
// (no keys) loads with the default view and no news.
int check_save_view()
{
    save_view::View view;
    view.valid = true;
    view.tile_size = 32;
    view.center_x = 0.45;
    view.center_y = 0.4612;
    save_view::View back;
    if (!save_view::decode_view(save_view::encode_view(view), back) || back.tile_size != 32 ||
        back.center_x < 0.4499 || back.center_x > 0.4501 || back.center_y < 0.4611 || back.center_y > 0.4613) {
        return fail(30, "view did not survive: " + save_view::encode_view(view));
    }
    save_view::View none;
    if (save_view::decode_view("", none) || none.valid || save_view::decode_view("t=0,x=1,y=2", none) ||
        save_view::decode_view("t=32,x=20000,y=5", none)) {
        return fail(31, "a missing or bad view was accepted");
    }
    std::vector<save_view::News> news(2);
    news[0].text = "Fire at \"Main\"; 50% lost | call 911";
    news[0].month = 4;
    news[0].year = 1903;
    news[0].tile_x = 12;
    news[0].tile_y = 34;
    news[0].has_place = true;
    news[1].text = "Blackouts reported. Check power.";
    news[1].month = 11;
    news[1].year = 1902;
    const std::string encoded = save_view::encode_news(news);
    if (encoded.find('"') != std::string::npos) {
        return fail(32, "a quote leaked into the sidecar string: " + encoded);
    }
    const std::vector<save_view::News> read = save_view::decode_news(encoded);
    if (read.size() != 2 || read[0].text != news[0].text || read[0].tile_x != 12 || read[0].tile_y != 34 ||
        !read[0].has_place || read[0].month != 4 || read[0].year != 1903 || read[1].text != news[1].text ||
        read[1].month != 11 || read[1].has_place) {
        return fail(33, "news did not survive: " + encoded);
    }
    if (!save_view::decode_news("").empty() || !save_view::decode_news("x|y|z").empty()) {
        return fail(34, "junk news was accepted");
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
    if (const int r = check_save_view()) {
        return r;
    }
    std::puts("testpass_nov18 ok");
    return 0;
}
