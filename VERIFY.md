# PR #13 verification: Spooky Look (look=2)

- PR head: da120c46a046d5e04889b69d2dd1962d7fa24069 (branch cursor/spooky-look-8076), fresh clone in `repo/`
- Master baseline: 0d34900, fresh clone in `master/`
- Date: Thu Oct 22 2026, ~12:56–13:25 CDT. Displays: Xvfb :984/:985 (mine). SteamDeck and LUNDUKE_CITY2_SIZE unset for builds and tests.
- Nothing merged, pushed, committed, stashed or commented.

## Summary

| # | Item | Result |
|---|------|--------|
| 1 | Linux / ASan+UBSan / Windows (Wine) builds and tests, packaging, clean tree | PASS |
| 2 | Spooky Look item by mouse, keyboard, touch, gamepad, Deck mode; check mark; map and minimap at 8/16/32 | PASS |
| 3 | Persistence, fresh profile default, missing-sheet fallback, restore | PASS |
| 4 | Classic and SVGA pixel-identical to master; Spooky differs from SVGA only on the listed tiles | PASS |
| 5 | Tokyo and Dullsville graveyards and haunted houses; Big Park sprite | PASS (Big Park **does** use the spooky sprite) |
| 6 | Regression: build, zoom, save/load, quit | PASS |

Bugs: none found (no high, medium or low severity). Notes are at the end.

## 1. Builds and tests
- Linux debug (`-Dwarning_level=2`): compiled OK (BX=0); `meson test` Ok 32 / Fail 0 / Skipped 1.
- ASan+UBSan: compiled OK (BA=0); Ok 32 / Fail 0 / Skipped 1. No `runtime error`, AddressSanitizer or LeakSanitizer lines.
- Windows (`packaging/windows/build-windows.sh`, tests under Wine): Ok 27 / Fail 0 / Skipped 1, EXIT=0.
  - It made `build-windows/dist/lunduke-city-2-windows-x86_64.zip` (13,691,342 bytes).
  - The zip contains `tiles-spooky.png` and `special-tiles/big-park-spooky.png`. The new PNG is also in the meson `install_data` list.
- The one skip is `round11` on every platform: "LUNDUKE_CITY_FULL_DIR not set" (exit 77). It is gated by an env var and also skipped before this PR.
- New tests pass: `spooky_atlas`, `svga_atlas`, `round16`, `menu_model`.
- Warnings: `rg -i warning` finds 0 matches in every build, test and Windows log.
- Master 0d34900 build: BM=0.
- `git status --short` is empty after all tests. `--ignored` shows only build-x/, build-asan/ and build-windows/.

## 2. The menu item
- Options now lists Classic Look, SVGA Look and Spooky Look as a radio group. The mnemonic is `y` ("Spook_y Look").
- Mouse: clicking Spooky moves the check to Spooky (shots/m-opt1.png and best/options-menu-spooky-checked.png). Clicking Classic or SVGA moves it back.
- Keyboard: alt+o then k, c and y each set the matching look and check mark (shots/kb-*), and the minimap changes each time.
- Touch: tap Options, then tap Spooky. The map then matches the Spooky reference exactly (0 differing pixels).
- Gamepad: Start, D-pad right to Options, then up:
  - One press wraps to Spooky (the last item).
  - Three presses reach Classic.
  - A applies the selection. Classic and Spooky both matched their references; the only difference was 232 pixels, which is the gamepad cursor.
- Deck mode (SteamDeck=1): went Spooky, then SVGA, then Classic. The radio check moved correctly each time (best/deck-check-moves-3-looks.png).
  - On a fresh terrain-only map, Spooky equals SVGA (0 px), because none of the 29 tiles are present. Classic differs from both by 343k px, as expected.
- 8/16/32px in Tokyo (shots/z{8,16,32}-{c,k,y}):
  - Spooky vs SVGA: about 23–26k map pixels and about 780 minimap pixels differ.
  - Classic vs SVGA: about 650–750k pixels differ.
  - All three zooms switch both the map and the minimap.

## 3. Persistence (window 1011x601, no size override)
- Fresh profile: no settings file; Classic is checked (best/persist-fresh-profile-classic.png).
- Picked Spooky by mouse: settings.txt has `look=2` right away, and still after Ctrl+Q.
- Relaunch: Spooky is checked and Spooky is drawn (best/persist-relaunch-spooky-checked.png).
- Sheet missing (assets copy with `images/micropolisEngine/tiles-spooky.png` moved out):
  - The Classic map is drawn and Classic is checked.
  - Clicking Spooky again still draws Classic.
  - `look=2` is still stored after Ctrl+Q (best/fallback-missing-spooky-sheet-classic.png).
- Sheet restored: relaunch draws Spooky with Spooky checked, and `look=2` is kept.

## 4. Pixel comparisons (fixed time seed, 1280x800, 16px)
| Scene | Master vs PR, Classic | Master vs PR, SVGA |
|---|---|---|
| Detroit.cty | 0 px | 0 px |
| Tokyo scenario | 0 px | 0 px |

- Atlases: tiles.png and tiles-svga.png are byte-identical to master (same sha256).
- Spooky vs SVGA, per tile: the atlas diff gives exactly the 29 listed tiles (40-43, 252, 254, 256, 297-305, 324-332, 840-843).
- Spooky vs SVGA, rendered: every differing 16px map cell was checked (scratch/tilecheck.py). A cell passes if its SVGA pixels equal SVGA atlas tile T, its Spooky pixels equal Spooky atlas tile T, and T is one of the 29.
  - Detroit: 93 cells match and 1 is unexplained.
  - Tokyo: 229 cells match and 19 are unexplained.
  - All unexplained cells sit under the translucent overlays: the scenario banner, or the right and bottom scrollbars. No other tile changed.
  - Tile IDs seen: 40-43, 252/254/256, 297-305, 324-332, 840-843.

## 5. Visuals
- Tokyo:
  - Graveyard parks show tombstones, crosses and mausoleums (the old fountains).
  - Haunted houses are dark 3x3 residential blocks, and small houses are haunted too.
  - No wrong tiles or seams at 8/16/32 (best/tokyo-*px-spooky.png, best/tokyo-16px-haunted-houses-svga-vs-spooky.png).
- Dullsville (`LUNDUKE_CITY2_DEMO=city`) at 8/16/32: the haunted 3x3 house renders cleanly with no seams (best/dullsville-*).
  - The other demo-shot differences are cars and the blinking power indicator. They are animation timing between two separate runs.
- Big Park:
  - It was placed with the Specials (star) tool → Big Park, $100.
  - **In Spooky it uses the spooky sprite** (graveyard: tombstones, crosses, dark pond). At 8px it is VGA-snapped.
  - Classic and SVGA keep the normal park (best/bigpark-8px-classic-svga-spooky.png, best/bigpark-32px-svga-vs-spooky.png, best/bigpark-48px-spooky.png).

## 6. Regression
- Built a road strip next to the Big Park.
- Zoom stepped 16→32→48→32→8 with Ctrl+= and Ctrl+-.
- Save City As → `spk.cty` (29,338 bytes plus `.json`); the status bar showed "City saved."
- Started a New City, then loaded spk.cty: the park, road and funds ($19,838) came back.
- Ctrl+Q exited cleanly with no stray process.

## Notes (not bugs)
- With a 1280x800 size override, settings are not saved (by design), so persistence was tested without it.
- `round11` is skipped on every platform (env-gated, as before).
- The spooky Big Park sprite is shown at the same scale as the classic one: a 48px sprite stretched at 32px and above, so it looks blocky. This is the same as the classic park.
