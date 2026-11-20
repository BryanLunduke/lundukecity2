// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

// The rolling Funds readout: it rolls to a new balance, floats the change,
// merges a quick run of spending into one figure, glints on income, and
// snaps for a new city or with motion off. Also the Year in Review
// count-up text.

#include "funds_tick.hpp"
#include "look_fx.hpp"

#include <cstdio>

namespace {

int fail(int code, const char *message)
{
    std::fprintf(stderr, "%s\n", message);
    return code;
}

} // namespace

int main()
{
    FundsTicker tick;
    tick.observe(20000, 1, true);
    if (tick.shown() != 20000 || tick.active()) {
        return fail(1, "first sight should snap");
    }
    tick.observe(19900, 1, true);
    if (tick.shown() != 20000 || tick.trend() != -1 || tick.floats.size() != 1 || tick.floats[0].delta != -100) {
        return fail(2, "spending should start a roll down and a -100 float");
    }
    tick.advance(100);
    if (tick.shown() >= 20000 || tick.shown() <= 19900) {
        return fail(3, "mid-roll value should sit between the two balances");
    }
    tick.observe(19890, 1, true);
    if (tick.floats.size() != 1 || tick.floats[0].delta != -110) {
        return fail(4, "a quick second spend should join the float");
    }
    tick.advance(FundsTicker::kRollMs);
    if (tick.shown() != 19890 || tick.trend() != 0 || tick.glint() != 0.0) {
        return fail(5, "roll should land on the balance without a glint for spending");
    }
    tick.observe(21890, 1, true);
    tick.advance(FundsTicker::kRollMs);
    if (tick.glint() <= 0.0 || tick.floats.back().delta != 2000) {
        return fail(6, "income should glint when it lands");
    }
    tick.advance(FundsTicker::kFloatMs + FundsTicker::kGlintMs);
    if (tick.active()) {
        return fail(7, "everything should settle");
    }
    tick.observe(5000, 2, true);
    if (tick.shown() != 5000 || tick.active()) {
        return fail(8, "a new city should snap");
    }
    tick.observe(4000, 2, false);
    if (tick.shown() != 4000 || tick.active()) {
        return fail(9, "motion off should snap");
    }
    for (int i = 0; i < 6; ++i) {
        tick.observe(4000 - (i + 1) * 10 * ((i % 2) ? -1 : 1), 2, true);
        tick.advance(FundsTicker::kMergeMs);
    }
    if (tick.floats.size() > FundsTicker::kMaxFloats) {
        return fail(10, "floats should be capped");
    }
    if (look_fx::count_up("Start of year $1,240", 0.5) != "Start of year $620" ||
        look_fx::count_up("End of year $12,000 (est)", 0.25) != "End of year $3,000 (est)" ||
        look_fx::count_up("Since 2009: 40 homes", 0.5) != "Since 2009: 20 homes" ||
        look_fx::count_up("Homes 9% \u00b7 Shops 3%", 0.0) != "Homes 0% \u00b7 Shops 0%" ||
        look_fx::count_up("$1,240", 1.0) != "$1,240") {
        return fail(11, "count-up text");
    }
    std::puts("funds_tick ok");
    return 0;
}
