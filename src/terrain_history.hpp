// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

#pragma once

#include <cstddef>
#include <utility>
#include <vector>

// Undo and redo for the terrain editor, one step per brush stroke. A stroke
// begins when the button (or pad confirm) goes down; the first paint in it
// that changes the map records the map as it was before. Clicks that change
// nothing record nothing. The oldest steps fall off past kMaxSteps.
class TerrainHistory {
public:
    using Tiles = std::vector<unsigned short>;

    static constexpr std::size_t kMaxSteps = 32;

    void clear()
    {
        undo_.clear();
        redo_.clear();
        recorded_ = false;
    }

    // A new stroke: its first change is a new undo step.
    void begin_stroke() { recorded_ = false; }

    // True when the next change would start a step, so the caller should
    // keep a copy of the map before painting.
    bool wants_snapshot() const { return !recorded_; }

    // The map before the stroke's first change. Clears redo.
    void record(Tiles before)
    {
        push(undo_, std::move(before));
        redo_.clear();
        recorded_ = true;
    }

    // Puts `work` back as it was before the last stroke. False with
    // nothing to undo.
    bool undo(Tiles &work) { return swap_step(undo_, redo_, work); }

    // Lays the last undone stroke again. False with nothing to redo.
    bool redo(Tiles &work) { return swap_step(redo_, undo_, work); }

    bool can_undo() const { return !undo_.empty(); }
    bool can_redo() const { return !redo_.empty(); }
    std::size_t undo_steps() const { return undo_.size(); }
    std::size_t redo_steps() const { return redo_.size(); }

private:
    static void push(std::vector<Tiles> &stack, Tiles tiles)
    {
        stack.push_back(std::move(tiles));
        if (stack.size() > kMaxSteps) {
            stack.erase(stack.begin());
        }
    }

    bool swap_step(std::vector<Tiles> &from, std::vector<Tiles> &to, Tiles &work)
    {
        if (from.empty()) {
            return false;
        }
        push(to, std::move(work));
        work = std::move(from.back());
        from.pop_back();
        // Painting after an undo (even mid-drag) is a new step.
        recorded_ = false;
        return true;
    }

    std::vector<Tiles> undo_;
    std::vector<Tiles> redo_;
    bool recorded_ = false;
};
