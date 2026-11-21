#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Bryan Lunduke
# See COPYING and NOTICE.

"""Checks the Steam Input files in packaging/steam against the game.

The layouts emulate an Xbox pad, so each physical control has to send the
pad button the game's factory binding expects (src/bindings.hpp,
factory_pad), the sticks and triggers have to stay analog, and the Deck
right trackpad has to be the mouse. The Action Manifest has to list files
that are there. Standard library only.

    python3 tests/steam_input_test.py SOURCE_ROOT
"""

import re
import sys
from pathlib import Path


def tokens(text):
    i = 0
    while i < len(text):
        c = text[i]
        if c.isspace():
            i += 1
        elif text.startswith("//", i):
            i = text.find("\n", i)
            i = len(text) if i < 0 else i
        elif c in "{}":
            yield c
            i += 1
        elif c == '"':
            j = text.find('"', i + 1)
            if j < 0:
                raise ValueError("unterminated string")
            yield ("s", text[i + 1:j])
            i = j + 1
        else:
            raise ValueError(f"unexpected {c!r} at offset {i}")


def parse(text):
    """VDF as nested lists of (key, value); keys may repeat."""
    stream = list(tokens(text))
    pos = 0

    def block():
        nonlocal pos
        items = []
        while pos < len(stream):
            tok = stream[pos]
            if tok == "}":
                pos += 1
                return items
            if tok == "{" or tok[0] != "s":
                raise ValueError("expected a key")
            key = tok[1]
            pos += 1
            if pos >= len(stream):
                raise ValueError(f"{key} has no value")
            nxt = stream[pos]
            if nxt == "{":
                pos += 1
                items.append((key, block()))
            elif nxt == "}":
                raise ValueError(f"{key} has no value")
            else:
                items.append((key, nxt[1]))
                pos += 1
        return items

    top = block()
    if pos != len(stream):
        raise ValueError("unbalanced braces")
    return top


def get(items, key):
    for k, v in items:
        if k == key:
            return v
    return None


def every(items, key):
    return [v for k, v in items if k == key]


# Physical control on the four_buttons / switches groups -> the Xbox button
# the game must see for its factory binding.
FACTORY_BUTTON = {
    "kPadSouth": "A", "kPadEast": "B", "kPadWest": "X", "kPadNorth": "Y",
    "kPadBack": "SELECT", "kPadStart": "START",
    "kPadLeftStick": "JOYSTICK_LEFT", "kPadRightStick": "JOYSTICK_RIGHT",
    "kPadLeftShoulder": "SHOULDER_LEFT", "kPadRightShoulder": "SHOULDER_RIGHT",
}
PHYSICAL = {
    ("four_buttons", "button_a"): "A", ("four_buttons", "button_b"): "B",
    ("four_buttons", "button_x"): "X", ("four_buttons", "button_y"): "Y",
    ("switches", "button_menu"): "SELECT", ("switches", "button_escape"): "START",
    ("switches", "left_bumper"): "SHOULDER_LEFT", ("switches", "right_bumper"): "SHOULDER_RIGHT",
}
CONTROLLER_TYPES = {
    "controller_neptune", "controller_steamcontroller_gordon", "controller_xbox360", "controller_xboxone",
    "controller_xboxelite", "controller_ps3", "controller_ps4", "controller_ps5", "controller_ps5_edge",
    "controller_switch_pro", "controller_switch2_pro", "controller_switch_joycon_left",
    "controller_switch_joycon_right", "controller_switch_joycon_pair", "controller_mobile_touch",
    "controller_generic",
}


def binding_of(inp):
    acts = get(inp, "activators") or []
    out = []
    for _, act in acts:
        for b in every(get(act, "bindings") or [], "binding"):
            out.append(b.split(",")[0].strip())
    return out


def check_layout(path, deck, problems):
    root = parse(path.read_text())
    cm = get(root, "controller_mappings")
    if cm is None:
        problems.append(f"{path.name}: no controller_mappings")
        return
    if get(cm, "version") != "3":
        problems.append(f"{path.name}: version is not 3")
    loc = get(get(cm, "localization") or [], "english") or []
    for key in ("title", "description"):
        token = get(cm, key) or ""
        if token.startswith("#") and get(loc, token[1:]) is None:
            problems.append(f"{path.name}: {key} token {token} is not localized")
    groups = {}
    for g in every(cm, "group"):
        groups[get(g, "id")] = g
    preset = get(cm, "preset")
    sources = {}
    for gid, source in (get(preset, "group_source_bindings") or []) if preset else []:
        if gid not in groups:
            problems.append(f"{path.name}: preset uses missing group {gid}")
            continue
        sources[source.split()[0]] = groups[gid]
    seen = set()
    for g in groups.values():
        mode = get(g, "mode")
        for name, inp in get(g, "inputs") or []:
            want = PHYSICAL.get((mode, name))
            if want is None:
                continue
            got = binding_of(inp)
            if got != [f"xinput_button {want}"]:
                problems.append(f"{path.name}: {mode}/{name} sends {got}, want xinput_button {want}")
            seen.add(want)
    for button in FACTORY_BUTTON.values():
        if button in ("JOYSTICK_LEFT", "JOYSTICK_RIGHT"):
            continue
        if button not in seen:
            problems.append(f"{path.name}: nothing sends {button}")

    def analog(source, mode, setting, value, click):
        g = sources.get(source)
        if g is None or get(g, "mode") != mode:
            problems.append(f"{path.name}: {source} is not a {mode} group")
            return
        if get(get(g, "settings") or [], setting) != value:
            problems.append(f"{path.name}: {source} {setting} is not {value}")
        if click:
            inp = get(get(g, "inputs") or [], "click") or get(get(g, "inputs") or [], "edge") or []
            if binding_of(inp) != [f"xinput_button {click}"]:
                problems.append(f"{path.name}: {source} does not send {click}")

    analog("joystick", "joystick_move", "output_joystick", "0", "JOYSTICK_LEFT")
    analog("right_joystick", "joystick_move", "output_joystick", "1", "JOYSTICK_RIGHT")
    analog("left_trigger", "trigger", "output_trigger", "1", "TRIGGER_LEFT")
    analog("right_trigger", "trigger", "output_trigger", "2", "TRIGGER_RIGHT")
    dpad = sources.get("dpad")
    if dpad is None:
        problems.append(f"{path.name}: no dpad group")
    else:
        for name, button in (("dpad_north", "DPAD_UP"), ("dpad_south", "DPAD_DOWN"),
                             ("dpad_east", "DPAD_RIGHT"), ("dpad_west", "DPAD_LEFT")):
            if binding_of(get(get(dpad, "inputs") or [], name) or []) != [f"xinput_button {button}"]:
                problems.append(f"{path.name}: D-pad {name} does not send {button}")
    if deck:
        pad = sources.get("right_trackpad")
        if pad is None or get(pad, "mode") != "absolute_mouse" or \
                binding_of(get(get(pad, "inputs") or [], "click") or []) != ["mouse_button LEFT"]:
            problems.append(f"{path.name}: the right trackpad is not the mouse with a left click")
    if get(cm, "controller_type") not in CONTROLLER_TYPES:
        problems.append(f"{path.name}: unknown controller_type")


def main():
    root = Path(sys.argv[1] if len(sys.argv) > 1 else Path(__file__).resolve().parents[1])
    problems = []
    # The game's factory pad buttons must be the ones the layouts send.
    header = (root / "src" / "bindings.hpp").read_text()
    body = header[header.index("inline int factory_pad(int act)"):]
    body = body[:body.index("\n}\n")]
    factory = dict(re.findall(r"case (Pad\w+):\s*return (kPad\w+);", body))
    expected = {
        "PadConfirm": "kPadSouth", "PadCancel": "kPadEast", "PadQuery": "kPadWest", "PadPause": "kPadNorth",
        "PadBack": "kPadBack", "PadMenu": "kPadStart", "PadPrevTool": "kPadLeftShoulder",
        "PadNextTool": "kPadRightShoulder", "PadOverlay": "kPadLeftStick", "PadCenter": "kPadRightStick",
    }
    if factory != expected:
        problems.append(f"factory_pad changed ({factory}); update packaging/steam and this test")
    steam = root / "packaging" / "steam"
    manifest = parse((steam / "steam_input_manifest.vdf").read_text())
    am = get(manifest, "Action Manifest")
    if am is None:
        problems.append("steam_input_manifest.vdf: no Action Manifest")
        am = []
    listed = set()
    for controller, entries in get(am, "configurations") or []:
        if controller not in CONTROLLER_TYPES:
            problems.append(f"manifest: unknown controller type {controller}")
        for _, entry in entries:
            rel = get(entry, "path")
            if not rel or not (steam / rel).is_file():
                problems.append(f"manifest: {controller} lists missing {rel}")
            else:
                listed.add(rel)
    if "controller_neptune" not in dict(get(am, "configurations") or []):
        problems.append("manifest: no Steam Deck layout")
    sets = get(am, "actions") or []
    if not sets or any(get(s, "legacy_set") != "1" for _, s in sets):
        problems.append("manifest: action sets must be legacy (the game does not use the Steam Input API)")
    for rel in sorted(listed):
        check_layout(steam / rel, rel.endswith("_deck.vdf"), problems)
    for p in problems:
        print(p)
    if problems:
        return 1
    print(f"steam input ok ({len(listed)} layouts)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
