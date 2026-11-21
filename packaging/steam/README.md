# Steam Input layouts

These files give Steam an official controller layout for Lunduke City 2
that matches the game's own pad controls. The game does **not** use the
Steam Input API and does not link the Steamworks SDK (its licence does not
mix with the GPL). Steam uses *gamepad emulation*: it turns any controller
(Steam Deck, Steam Controller, PlayStation, Switch Pro, generic pads) into
an Xbox-style pad, and the game reads that pad through SDL like any other.
The files are plain text data; nothing here is compiled into the game.

| File | What it is |
| --- | --- |
| `steam_input_manifest.vdf` | Action Manifest. Lists which layout each controller type loads. One legacy action set (`legacy_set 1`), no API actions. |
| `lunduke_city_2_deck.vdf` | Steam Deck layout (`controller_neptune`), also used for the Steam Controller. |
| `lunduke_city_2_gamepad.vdf` | Layout for Xbox, PlayStation, Switch Pro and generic pads. |

## What the layouts do

They mirror the factory pad buttons (`factory_pad` in `src/bindings.hpp`)
and the stick, trigger and D-pad handling in `src/shell.cpp`, which the
in-game Controls page also lists. Physical controls send the same Xbox
button, so a player's in-game rebinding still applies on top.

| Control | Sends | In game |
| --- | --- | --- |
| A | A | Confirm, build, or open the highlighted row |
| B | B | Cancel or close |
| X | X | Switch to Inspect |
| Y | Y | Pause or resume; hold to cycle slow, medium, fast |
| Menu (☰ / Start) | Start | Open the menu; in a window, OK |
| View (⧉ / Back) | Back | Cancel or close |
| L1 / R1 (LB / RB) | LB / RB | Previous / next tool; hold to jump a section |
| Left stick | left stick (analog) | Move the cursor |
| Left stick click | LS | Show or hide the map overlay |
| Right stick | right stick (analog) | Pan the map |
| Right stick click | RS | Center the view on the cursor |
| L2 / R2 | left / right trigger (analog) | Zoom out / in, one step per press |
| D-pad | D-pad | Step and repeat in lists and sliders, or pan |
| Deck right trackpad | mouse pointer, click = left click | Point and build like a mouse (right drag and right-click are on the mouse only) |
| Deck left trackpad (click) | D-pad | Same as the D-pad |

The back grips (L4, R4, L5, R5) and gyro are left unbound for players to
use. The touchscreen is not part of Steam Input; the game reads touch
itself.

`tests/steam_input_test.py` (run by `meson test` as `steam_input`) parses
the files and fails if a control no longer sends what `factory_pad`
expects, if the sticks or triggers stop being analog, if the Deck right
trackpad stops being the mouse, or if the manifest lists a missing file.
When the factory pad buttons change, change the layouts and the test with
them.

## Putting it on Steam (Steamworks partner site)

1. Ship the three `.vdf` files in the depot, in one folder, for every
   platform the same way, e.g. `steam_input/` beside the executable. The
   manifest's `path` entries are relative to the manifest.
2. Steamworks → App Admin → Application → **Steam Input**:
   - Opt-in: tick the controller types that should use Steam Input
     (PlayStation, Switch, generic; the Deck and Steam Controller always
     do). Leaving Xbox pads on plain XInput is fine: the game reads them
     directly.
   - **Steam Input Template**: pick *Custom Configuration (Bundled with
     game)* and enter the manifest path, e.g.
     `steam_input/steam_input_manifest.vdf`.
3. Save and publish the app changes as usual.

To try a layout before a depot build: in Big Picture turn on
Settings → System → Developer mode, then Developer → *Steam Input Layout
Dev Mode*. Open the game's controller settings and load the layout from
there, or point Steam at a local manifest. Valve's
`steam://dumpcontrollerconfig?appid=<appid>` writes the active layout out
for comparison. If Steam rewrites a layout when it is exported, commit the
exported text and keep the bindings above (the test will say if one moved).

### If the bundled manifest is not wanted

The built-in template *Gamepad with Mouse Trackpad* behaves the same on the
Deck, apart from the left trackpad. It needs no files, but its labels are
Valve's generic ones and players do not see what each button does in game.
A layout can also be published from Big Picture as the official
configuration (hosted on the Workshop) instead of shipping it in the depot.

References: Steamworks documentation, *Steam Input Gamepad Emulation – Best
Practices* and *Action Manifest Files*.
