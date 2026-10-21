#pragma once
// Whether a frame needs a new cairo picture. Idle play (paused, no pointer,
// no blink) must not upload a fresh frame.

namespace redraw {

struct Reasons {
    bool forced = false;
    bool sim_running = false;
    bool quake = false;
    bool navigating = false;
    bool blink = false;
    bool stamp_changed = false;
};

inline bool needed(const Reasons &reasons)
{
    return reasons.forced || reasons.sim_running || reasons.quake || reasons.navigating || reasons.blink ||
           reasons.stamp_changed;
}

} // namespace redraw
