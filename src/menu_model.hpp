// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#pragma once

// Menu model for the SDL shell. The 0.9.5-2 gtkmm bar has 35 actionable
// rows (8 System, 11 Options, 6 Disasters, 9 Windows, 1 Help), 4 separators
// and 5 titles. Hidden zoom accelerators (Ctrl+plus, Ctrl+keypad plus,
// Ctrl+keypad minus) are extra bindings, not extra rows. Together with the
// two hidden zoom-in bindings that is the "37" figure in the migration note.

#include <cctype>
#include <string>
#include <vector>

enum class AccelKey {
    None = 0,
    N, O, S, Q, P,
    D1, D2, D3,
    Equal, Plus, Minus, KpAdd, KpSub,
    F10, Up, Down, Left, Right, Return, Escape, Space,
    Letter
};

enum class AccelMod {
    None = 0,
    Ctrl = 1,
    Shift = 2,
    Alt = 4
};

inline int operator|(AccelMod a, AccelMod b)
{
    return static_cast<int>(a) | static_cast<int>(b);
}

enum class Action {
    None = 0,
    NewCity, Load, Recent, Save, SaveAs, Scenario, Rename, Quit,
    AutoBudget, AutoBulldoze, DisastersEnabled, AutoGoto, Mute,
    SetPause, Slow, Medium, Fast, TogglePause,
    ZoomIn, ZoomOut,
    Fire, Flood, Tornado, Earthquake, Monster, Meltdown,
    Budget, Graphs, Evaluation,
    Power, Water, Pollution, Crime, LandValue, Traffic,
    About,
    RecentIndex
};

enum class ItemKind { Normal, Check, Radio, Separator, Submenu };

struct MenuEntry {
    const char *label;
    const char *accel_text;
    ItemKind kind;
    Action action;
    AccelKey key;
    int mods;
    int radio_group;
};

struct MenuColumn {
    const char *title;
    const MenuEntry *items;
    int count;
};

inline char mnemonic_of(const char *label)
{
    if (label == nullptr) {
        return 0;
    }
    for (const char *p = label; *p != '\0'; ++p) {
        if (*p == '_' && p[1] != '\0' && p[1] != '_') {
            return static_cast<char>(std::tolower(static_cast<unsigned char>(p[1])));
        }
    }
    return 0;
}

inline std::string strip_mnemonic(const char *label)
{
    std::string out;
    if (label == nullptr) {
        return out;
    }
    for (const char *p = label; *p != '\0'; ++p) {
        if (*p == '_' && p[1] != '\0') {
            continue;
        }
        out.push_back(*p);
    }
    return out;
}

namespace menu_table {

inline constexpr MenuEntry kSystem[] = {
    {"_New City", "Ctrl+N", ItemKind::Normal, Action::NewCity, AccelKey::N, static_cast<int>(AccelMod::Ctrl), 0},
    {"_Load City...", "Ctrl+O", ItemKind::Normal, Action::Load, AccelKey::O, static_cast<int>(AccelMod::Ctrl), 0},
    {"Open _Recent", nullptr, ItemKind::Submenu, Action::Recent, AccelKey::None, 0, 0},
    {"Save _City", "Ctrl+S", ItemKind::Normal, Action::Save, AccelKey::S, static_cast<int>(AccelMod::Ctrl), 0},
    {"Save City _As...", "Ctrl+Shift+S", ItemKind::Normal, Action::SaveAs, AccelKey::S,
     static_cast<int>(AccelMod::Ctrl) | static_cast<int>(AccelMod::Shift), 0},
    {"Play _Scenario…", nullptr, ItemKind::Normal, Action::Scenario, AccelKey::None, 0, 0},
    {"_Rename City…", nullptr, ItemKind::Normal, Action::Rename, AccelKey::None, 0, 0},
    {nullptr, nullptr, ItemKind::Separator, Action::None, AccelKey::None, 0, 0},
    {"_Quit", "Ctrl+Q", ItemKind::Normal, Action::Quit, AccelKey::Q, static_cast<int>(AccelMod::Ctrl), 0},
};

inline constexpr MenuEntry kOptions[] = {
    {"Auto _budget", nullptr, ItemKind::Check, Action::AutoBudget, AccelKey::None, 0, 0},
    {"Auto b_ulldoze", nullptr, ItemKind::Check, Action::AutoBulldoze, AccelKey::None, 0, 0},
    {"Enable _disasters", nullptr, ItemKind::Check, Action::DisastersEnabled, AccelKey::None, 0, 0},
    {"Auto-_goto", nullptr, ItemKind::Check, Action::AutoGoto, AccelKey::None, 0, 0},
    {"_Mute sound", nullptr, ItemKind::Check, Action::Mute, AccelKey::None, 0, 0},
    {nullptr, nullptr, ItemKind::Separator, Action::None, AccelKey::None, 0, 0},
    {"_Pause", "Ctrl+P", ItemKind::Radio, Action::SetPause, AccelKey::None, 0, 1},
    {"Slo_w", "Ctrl+1", ItemKind::Radio, Action::Slow, AccelKey::D1, static_cast<int>(AccelMod::Ctrl), 1},
    {"M_edium", "Ctrl+2", ItemKind::Radio, Action::Medium, AccelKey::D2, static_cast<int>(AccelMod::Ctrl), 1},
    {"_Fast", "Ctrl+3", ItemKind::Radio, Action::Fast, AccelKey::D3, static_cast<int>(AccelMod::Ctrl), 1},
    {nullptr, nullptr, ItemKind::Separator, Action::None, AccelKey::None, 0, 0},
    {"Zoom _in", "Ctrl+=", ItemKind::Normal, Action::ZoomIn, AccelKey::Equal, static_cast<int>(AccelMod::Ctrl), 0},
    {"Zoom _out", "Ctrl+-", ItemKind::Normal, Action::ZoomOut, AccelKey::Minus, static_cast<int>(AccelMod::Ctrl), 0},
};

inline constexpr MenuEntry kDisasters[] = {
    {"_Fire…", nullptr, ItemKind::Normal, Action::Fire, AccelKey::None, 0, 0},
    {"F_lood…", nullptr, ItemKind::Normal, Action::Flood, AccelKey::None, 0, 0},
    {"_Tornado…", nullptr, ItemKind::Normal, Action::Tornado, AccelKey::None, 0, 0},
    {"_Earthquake…", nullptr, ItemKind::Normal, Action::Earthquake, AccelKey::None, 0, 0},
    {"M_onster…", nullptr, ItemKind::Normal, Action::Monster, AccelKey::None, 0, 0},
    {"_Meltdown…", nullptr, ItemKind::Normal, Action::Meltdown, AccelKey::None, 0, 0},
};

inline constexpr MenuEntry kWindows[] = {
    {"_Budget", nullptr, ItemKind::Normal, Action::Budget, AccelKey::None, 0, 0},
    {"_Graphs", nullptr, ItemKind::Normal, Action::Graphs, AccelKey::None, 0, 0},
    {"_Evaluation", nullptr, ItemKind::Normal, Action::Evaluation, AccelKey::None, 0, 0},
    {nullptr, nullptr, ItemKind::Separator, Action::None, AccelKey::None, 0, 0},
    {"_Power", nullptr, ItemKind::Normal, Action::Power, AccelKey::None, 0, 0},
    {"_Water", nullptr, ItemKind::Normal, Action::Water, AccelKey::None, 0, 0},
    {"P_ollution", nullptr, ItemKind::Normal, Action::Pollution, AccelKey::None, 0, 0},
    {"_Crime", nullptr, ItemKind::Normal, Action::Crime, AccelKey::None, 0, 0},
    {"_Land value", nullptr, ItemKind::Normal, Action::LandValue, AccelKey::None, 0, 0},
    {"_Traffic", nullptr, ItemKind::Normal, Action::Traffic, AccelKey::None, 0, 0},
};

inline constexpr MenuEntry kHelp[] = {
    {"_About Lunduke City 2", nullptr, ItemKind::Normal, Action::About, AccelKey::None, 0, 0},
};

inline constexpr int kColumnCount = 5;

inline int column_count(int index)
{
    switch (index) {
    case 0:
        return static_cast<int>(sizeof(kSystem) / sizeof(kSystem[0]));
    case 1:
        return static_cast<int>(sizeof(kOptions) / sizeof(kOptions[0]));
    case 2:
        return static_cast<int>(sizeof(kDisasters) / sizeof(kDisasters[0]));
    case 3:
        return static_cast<int>(sizeof(kWindows) / sizeof(kWindows[0]));
    case 4:
        return static_cast<int>(sizeof(kHelp) / sizeof(kHelp[0]));
    default:
        return 0;
    }
}

inline const MenuEntry *column_items(int index)
{
    switch (index) {
    case 0:
        return kSystem;
    case 1:
        return kOptions;
    case 2:
        return kDisasters;
    case 3:
        return kWindows;
    case 4:
        return kHelp;
    default:
        return nullptr;
    }
}

inline const char *column_title(int index)
{
    switch (index) {
    case 0:
        return "_System";
    case 1:
        return "_Options";
    case 2:
        return "_Disasters";
    case 3:
        return "_Windows";
    case 4:
        return "_Help";
    default:
        return "";
    }
}

inline int actionable_count()
{
    int n = 0;
    for (int c = 0; c < kColumnCount; ++c) {
        const int count = column_count(c);
        const MenuEntry *items = column_items(c);
        for (int i = 0; i < count; ++i) {
            if (items[i].kind != ItemKind::Separator) {
                ++n;
            }
        }
    }
    return n;
}

inline Action hidden_zoom_accel(AccelKey key, int mods)
{
    if (mods != static_cast<int>(AccelMod::Ctrl)) {
        return Action::None;
    }
    if (key == AccelKey::Plus || key == AccelKey::KpAdd) {
        return Action::ZoomIn;
    }
    if (key == AccelKey::KpSub) {
        return Action::ZoomOut;
    }
    return Action::None;
}

inline Action accel_action(AccelKey key, int mods)
{
    if ((mods & static_cast<int>(AccelMod::Alt)) != 0) {
        return Action::None;
    }
    if (key == AccelKey::P && mods == static_cast<int>(AccelMod::Ctrl)) {
        return Action::TogglePause;
    }
    const Action hidden = hidden_zoom_accel(key, mods);
    if (hidden != Action::None) {
        return hidden;
    }
    for (int c = 0; c < kColumnCount; ++c) {
        const int count = column_count(c);
        const MenuEntry *items = column_items(c);
        for (int i = 0; i < count; ++i) {
            if (items[i].key == AccelKey::None || items[i].kind == ItemKind::Separator) {
                continue;
            }
            if (items[i].key == key && items[i].mods == mods) {
                return items[i].action;
            }
        }
    }
    return Action::None;
}

} // namespace menu_table

// Keyboard and pointer state for the menu bar. Drawing lives in the shell.
class MenuNav {
public:
    int open_menu = -1;
    int cursor = 0;
    bool alt_shown = false;
    bool recent_open = false;
    int recent_cursor = 0;
    int recent_count = 0;
    Action activated = Action::None;
    int activated_recent = -1;

    void close()
    {
        open_menu = -1;
        recent_open = false;
        alt_shown = false;
        activated = Action::None;
        activated_recent = -1;
    }

    bool open() const { return open_menu >= 0; }

    void open_first()
    {
        open_menu = 0;
        cursor = first_enabled(0);
        recent_open = false;
        alt_shown = true;
    }

    bool handle_key(AccelKey key, int mods, char letter)
    {
        activated = Action::None;
        activated_recent = -1;
        const bool alt = (mods & static_cast<int>(AccelMod::Alt)) != 0;
        const bool ctrl = (mods & static_cast<int>(AccelMod::Ctrl)) != 0;
        if (!open() && !alt) {
            return false;
        }
        if (!open() && alt && !ctrl && letter != 0) {
            for (int c = 0; c < menu_table::kColumnCount; ++c) {
                if (mnemonic_of(menu_table::column_title(c)) == letter) {
                    open_menu = c;
                    cursor = first_enabled(c);
                    alt_shown = true;
                    recent_open = false;
                    return true;
                }
            }
            return false;
        }
        if (!open()) {
            return false;
        }
        if (key == AccelKey::Escape) {
            if (recent_open) {
                recent_open = false;
                return true;
            }
            close();
            return true;
        }
        if (key == AccelKey::Left) {
            if (recent_open) {
                recent_open = false;
                return true;
            }
            open_menu = (open_menu + menu_table::kColumnCount - 1) % menu_table::kColumnCount;
            cursor = first_enabled(open_menu);
            return true;
        }
        if (key == AccelKey::Right) {
            if (item_is(ItemKind::Submenu) && !recent_open) {
                recent_open = true;
                recent_cursor = 0;
                return true;
            }
            recent_open = false;
            open_menu = (open_menu + 1) % menu_table::kColumnCount;
            cursor = first_enabled(open_menu);
            return true;
        }
        if (key == AccelKey::Up) {
            if (recent_open) {
                if (recent_count > 0) {
                    recent_cursor = (recent_cursor + recent_count - 1) % recent_count;
                }
                return true;
            }
            cursor = step(open_menu, cursor, -1);
            return true;
        }
        if (key == AccelKey::Down) {
            if (recent_open) {
                if (recent_count > 0) {
                    recent_cursor = (recent_cursor + 1) % recent_count;
                }
                return true;
            }
            cursor = step(open_menu, cursor, +1);
            return true;
        }
        if (key == AccelKey::Return) {
            activate_current();
            return true;
        }
        if (!ctrl && letter != 0) {
            const MenuEntry *items = menu_table::column_items(open_menu);
            const int count = menu_table::column_count(open_menu);
            for (int i = 0; i < count; ++i) {
                if (items[i].kind == ItemKind::Separator) {
                    continue;
                }
                if (mnemonic_of(items[i].label) == letter) {
                    cursor = i;
                    activate_current();
                    return true;
                }
            }
        }
        return true;
    }

    void activate_current()
    {
        if (recent_open) {
            if (recent_count > 0) {
                activated = Action::RecentIndex;
                activated_recent = recent_cursor;
                close();
                activated = Action::RecentIndex;
                activated_recent = recent_cursor;
            }
            return;
        }
        const MenuEntry *items = menu_table::column_items(open_menu);
        if (items == nullptr) {
            return;
        }
        const MenuEntry &item = items[cursor];
        if (item.kind == ItemKind::Separator) {
            return;
        }
        if (item.kind == ItemKind::Submenu) {
            recent_open = true;
            recent_cursor = 0;
            if (recent_count == 0) {
                activated = Action::Recent;
                close();
                activated = Action::Recent;
            }
            return;
        }
        activated = item.action;
        close();
        activated = item.action;
    }

private:
    bool item_is(ItemKind kind) const
    {
        const MenuEntry *items = menu_table::column_items(open_menu);
        if (items == nullptr) {
            return false;
        }
        return items[cursor].kind == kind;
    }

    static int first_enabled(int column)
    {
        const int count = menu_table::column_count(column);
        const MenuEntry *items = menu_table::column_items(column);
        for (int i = 0; i < count; ++i) {
            if (items[i].kind != ItemKind::Separator) {
                return i;
            }
        }
        return 0;
    }

    static int step(int column, int from, int dir)
    {
        const int count = menu_table::column_count(column);
        const MenuEntry *items = menu_table::column_items(column);
        if (count <= 0) {
            return 0;
        }
        int i = from;
        for (int n = 0; n < count; ++n) {
            i = (i + dir + count) % count;
            if (items[i].kind != ItemKind::Separator) {
                return i;
            }
        }
        return from;
    }
};
