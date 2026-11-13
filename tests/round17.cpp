// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

// Headless checks for remapped controls, the autosave interval, save-file
// rename and delete, and one money sign.

#include "bindings.hpp"
#include "graph_legend.hpp"
#include "save_path.hpp"
#include "ui_scale.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#ifndef _WIN32
#include <unistd.h>
#endif

namespace {

int fail(const std::string &message)
{
    std::cerr << message << "\n";
    return 1;
}

} // namespace

int main()
{
    const ControlBinds factory = default_binds();
    if (factory.key[KeyPause] != "space" || factory.key[KeySave] != "ctrl+s" || factory.key[KeyTool0] != "b" ||
        factory.pad[PadConfirm] != kPadSouth || factory.pad[PadQuery] != kPadWest) {
        return fail("factory bindings drifted");
    }
    if (key_act_for_token(factory, "ctrl+plus") != KeyZoomIn || key_act_for_token(factory, "ctrl+kp_sub") != KeyZoomOut) {
        return fail("the extra zoom keys no longer follow the factory chords");
    }
    ControlBinds edited = factory;
    int conflict = -1;
    if (assign_key(edited, KeyPause, "b", conflict) || conflict != KeyTool0) {
        return fail("Pause was allowed to take the bulldozer key");
    }
    if (!assign_key(edited, KeyTool0, "x", conflict) || !assign_key(edited, KeyPause, "b", conflict)) {
        return fail("a freed key could not be assigned");
    }
    if (key_act_for_token(edited, "b") != KeyPause || key_act_for_token(edited, "space") != -1) {
        return fail("the pause chord did not move");
    }
    if (!token_is_shadowed(edited, "space")) {
        return fail("the old pause key is still live");
    }
    const std::string saved = binds_override_text(edited);
    const ControlBinds loaded = binds_from_text("speed=1\n" + saved + "bind_key_nope=z\n");
    if (loaded.key[KeyPause] != "b" || loaded.key[KeyTool0] != "x" || loaded.key[KeySave] != "ctrl+s") {
        return fail("bindings did not round-trip, or a factory key was dropped");
    }
    const ControlBinds untouched = binds_from_text("ui_scale=1\nlook=0\n");
    if (untouched.key[KeyPause] != "space" || untouched.pad[PadConfirm] != kPadSouth) {
        return fail("an old settings file did not keep the factory bindings");
    }
    if (chord_label("ctrl+shift+s") != "Ctrl+Shift+S" || chord_label("space") != "Space" || chord_label("f8") != "F8") {
        return fail("chord labels changed");
    }
    ControlBinds swapped = factory;
    swap_pad(swapped, PadPause, kPadSouth);
    if (swapped.pad[PadPause] != kPadSouth || swapped.pad[PadConfirm] != kPadNorth) {
        return fail("a taken pad button did not swap");
    }
    swap_key(swapped, KeyPause, "b");
    if (swapped.key[KeyPause] != "b" || swapped.key[KeyTool0] != "space") {
        return fail("a taken key did not swap");
    }
    swap_key(swapped, KeyPause, "");
    if (!swapped.key[KeyPause].empty()) {
        return fail("a key binding could not be cleared");
    }
    ControlBinds guarded = factory;
    if (swap_pad(guarded, PadConfirm, -1) || guarded.pad[PadConfirm] != kPadSouth) {
        return fail("Confirm was cleared");
    }
    guarded.pad[PadQuery] = -1;
    if (swap_pad(guarded, PadQuery, kPadSouth) || guarded.pad[PadConfirm] != kPadSouth) {
        return fail("a swap left Confirm with no button");
    }
    const ControlBinds kept = binds_from_text("bind_pad_confirm=\nbind_pad_cancel=\nbind_pad_back=\n");
    if (kept.pad[PadConfirm] != kPadSouth || kept.pad[PadCancel] != kPadEast || kept.pad[PadBack] != kPadBack) {
        return fail("empty Confirm, Cancel, or Back did not keep the factory button");
    }
    const ControlBinds stolen = binds_from_text("bind_pad_query=east\n");
    if (stolen.pad[PadCancel] != kPadEast || stolen.pad[PadQuery] == kPadEast) {
        return fail("taking Cancel's button left Cancel unbound");
    }
    const ControlBinds query_clear = binds_from_text("bind_pad_query=\n");
    if (query_clear.pad[PadQuery] != -1 || query_clear.pad[PadCancel] != kPadEast) {
        return fail("an empty Query line did not clear only Query");
    }
    if (binds_override_text(kept).find("bind_pad_cancel=") != std::string::npos ||
        binds_override_text(stolen).find("bind_pad_cancel=") != std::string::npos) {
        return fail("a required pad binding was written empty");
    }

    const std::string old_settings = "ui_scale=1.5\nwindow_w=1280\nwindow_h=800\nspeed=2\n";
    const ui_scale::StoredSettings parsed = ui_scale::settings_parse(old_settings);
    if (parsed.autosave != -1 || parsed.speed != 2 || !parsed.binds.empty() || !parsed.has_geometry) {
        return fail("an old settings.txt was not accepted");
    }
    ui_scale::StoredSettings written = parsed;
    written.autosave = 0;
    written.binds = "bind_key_pause=p\n";
    const std::string text = ui_scale::settings_text(written);
    const ui_scale::StoredSettings again = ui_scale::settings_parse(text);
    if (again.autosave != 0 || again.binds.find("bind_key_pause=p") == std::string::npos || again.speed != 2) {
        return fail("autosave and bindings were not stored");
    }
    if (ui_scale::settings_parse("autosave=60\n").autosave != 60) {
        return fail("the one-minute interval did not load");
    }

    if (graph_money(-1485) != "\u2212$1,485" || graph_money(21) != "$21") {
        return fail("money no longer uses one minus sign");
    }

    const auto dir = std::filesystem::temp_directory_path() / "lunduke-round17-saves";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    const auto file = dir / "Harbor.cty";
    {
        std::ofstream out(file);
        out << "city";
    }
    const SaveFileResult clash = rename_city_file(dir.string(), "Harbor.cty", "Harbor");
    if (!clash.ok) {
        return fail("renaming a file to itself failed");
    }
    {
        std::ofstream other(dir / "Other.cty");
        other << "city";
    }
    const SaveFileResult exists = rename_city_file(dir.string(), "Harbor.cty", "Other");
    if (exists.ok || exists.error.find("already exists") == std::string::npos) {
        return fail("rename did not notice an existing name");
    }
    const SaveFileResult bad = rename_city_file(dir.string(), "Harbor.cty", "a/b");
    if (bad.ok || bad.error.find("cannot contain") == std::string::npos) {
        return fail("rename accepted a slash");
    }
    {
        std::ofstream side(dir / "Harbor.cty.json");
        side << "{\"scenario\": 1}\n";
    }
    const SaveFileResult renamed = rename_city_file(dir.string(), "Harbor.cty", "Port");
    if (!renamed.ok || !std::filesystem::exists(dir / "Port.cty") || std::filesystem::exists(file)) {
        return fail("rename did not move the city file");
    }
    if (!std::filesystem::exists(dir / "Port.cty.json") || std::filesystem::exists(dir / "Harbor.cty.json")) {
        return fail("rename did not move the scenario sidecar");
    }
#ifndef _WIN32
    if (geteuid() != 0) {
        std::filesystem::permissions(dir, std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec, ec);
        const SaveFileResult blocked = delete_city_file(dir.string(), "Port.cty");
        std::filesystem::permissions(dir, std::filesystem::perms::owner_all, ec);
        if (blocked.ok || blocked.error.find("read-only") == std::string::npos) {
            return fail("delete succeeded in a read-only folder");
        }
    }
#endif
    const SaveFileResult missing = delete_city_file(dir.string(), "Nope.cty");
    if (missing.ok || missing.error.find("no longer there") == std::string::npos) {
        return fail("deleting a missing file did not say so");
    }
    const SaveFileResult removed = delete_city_file(dir.string(), "Port.cty");
    if (!removed.ok || std::filesystem::exists(dir / "Port.cty") || std::filesystem::exists(dir / "Port.cty.json")) {
        return fail("delete left the city file or its sidecar behind");
    }
    std::filesystem::remove_all(dir, ec);
    std::cout << "round17 ok\n";
    return 0;
}
