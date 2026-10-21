// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#include "list_scroll.hpp"

#include <iostream>
#include <string>

namespace {

int fail(const std::string &message)
{
    std::cerr << message << "\n";
    return 1;
}

} // namespace

int main()
{
    // C1: a selection past the first screen scrolls into view, and the
    // offset never walks off the end of a short list.
    int scroll = 0;
    list_reveal(scroll, 30, 10, 40);
    if (scroll != 21) {
        return fail("selecting row 30 of 40 with 10 visible should scroll to 21, got " + std::to_string(scroll));
    }
    list_reveal(scroll, 0, 10, 40);
    if (scroll != 0) {
        return fail("selecting the first row should scroll back to 0");
    }
    list_reveal(scroll, 3, 10, 40);
    if (scroll != 0) {
        return fail("a row already on screen should not scroll");
    }
    scroll = 100;
    list_reveal(scroll, 2, 15, 8);
    if (scroll != 0) {
        return fail("a list shorter than the window should pin scroll at 0, got " + std::to_string(scroll));
    }
    if (list_max_scroll(40, 10) != 30 || list_max_scroll(4, 10) != 0) {
        return fail("max scroll");
    }
    if (list_index_from_track(0, 0, 100, 40) != 0 || list_index_from_track(100, 0, 100, 40) != 39) {
        return fail("track ends should map to the first and last row");
    }
    if (list_index_from_track(50, 0, 100, 11) != 5) {
        return fail("mid track should map to the middle row");
    }
    double ty = 0;
    double th = 0;
    list_thumb(21, 40, 10, 10, 200, ty, th);
    if (th >= 200.0 || ty <= 10.0) {
        return fail("thumb should sit below the top of a scrolled track");
    }
    list_thumb(0, 4, 10, 10, 200, ty, th);
    if (ty != 10.0 || th != 200.0) {
        return fail("a short list fills the track");
    }
    std::cout << "review ok\n";
    return 0;
}
