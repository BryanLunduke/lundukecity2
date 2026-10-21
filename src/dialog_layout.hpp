#pragma once
// Dialog rectangles shared by the shell and the unit tests. Sizes are in
// pixels; the shell passes already-scaled values.

#include <algorithm>

struct DialogRect {
    double x = 0;
    double y = 0;
    double w = 0;
    double h = 0;
};

// Center a panel. It never grows past the window minus the margin, so a
// 1024x640 screen still shows the whole dialog.
inline DialogRect place_dialog(double win_w, double win_h, double want_w, double want_h, double margin)
{
    const double avail_w = std::max(1.0, win_w - 2.0 * margin);
    const double avail_h = std::max(1.0, win_h - 2.0 * margin);
    DialogRect rect;
    rect.w = std::min(std::max(1.0, want_w), avail_w);
    rect.h = std::min(std::max(1.0, want_h), avail_h);
    rect.x = std::max(0.0, (win_w - rect.w) / 2.0);
    rect.y = std::max(0.0, (win_h - rect.h) / 2.0);
    return rect;
}

// Message dialogs stay between min_w and max_w when the window allows it.
inline double message_width(double win_w, double margin, double min_w, double max_w)
{
    const double avail = std::max(1.0, win_w - 2.0 * margin);
    if (avail <= min_w) {
        return avail;
    }
    return std::min(max_w, std::max(min_w, std::min(avail, max_w)));
}

inline double button_width(double text_px, double pad, double min_w)
{
    return std::max(min_w, text_px + pad);
}

// Keep a popup next to a map click, and push it back inside the window.
inline DialogRect place_near(double win_w, double win_h, double want_w, double want_h, double anchor_x, double anchor_y,
                             double margin)
{
    DialogRect rect = place_dialog(win_w, win_h, want_w, want_h, margin);
    rect.x = anchor_x + margin;
    rect.y = anchor_y + margin;
    if (rect.x + rect.w > win_w - margin) {
        rect.x = anchor_x - margin - rect.w;
    }
    if (rect.y + rect.h > win_h - margin) {
        rect.y = anchor_y - margin - rect.h;
    }
    rect.x = std::max(margin, std::min(rect.x, std::max(margin, win_w - margin - rect.w)));
    rect.y = std::max(margin, std::min(rect.y, std::max(margin, win_h - margin - rect.h)));
    return rect;
}
