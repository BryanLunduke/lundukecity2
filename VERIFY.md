# Lunduke City 2 PR #8 verification

- PR: https://github.com/BryanLunduke/lundukecity2/pull/8
- Branch: cursor/special-buildings-19d2
- Head: 7331d43ba16ec81f4ff8778cf1eb0c9dbd654290
- Clone: fresh clone at /workspace/city2-pr8/repo, detached at that SHA. `git status` is clean, I made no commits and pushed nothing.
- Master baseline: cc710e2, built in a temporary worktree that has since been removed.
- Date: 2026-10-21 (CDT)

## Summary

The feature works on every platform I tested: native, Windows (Wine), and Steam Runtime sniper in Deck mode. Nothing crashed, and ASan/UBSan reported nothing.

There are two real problems, listed below as blockers. Neither is a crash.

1. **The economy can be exploited (design).** A Zoo or Library costs $100 and pays $100 every year. There is no upkeep, no placement limit, and it pays even with zero population. The engine harness showed it: 100 buildings cost $10,000 and paid $10,000/yr, taking funds from $10,000 to $60,000 after 5 Januaries with no city. In the live game the same thing happened: Zoo + Library on an empty map paid +$200 in January 1901. The PR matches the brief exactly, so this is the brief's numbers, not a code bug. It needs an editor decision, e.g. upkeep, needing population, a cap, or a higher price.
2. **The PD and FD badges read as "PO" and "FO".** The "D" glyph is drawn as a rounded rectangle (all four corners cut), so it looks like an O or 0 at every scale, including the 10x zoom. The editor asked for PD/FD.

## Results per item

| # | Item | Result |
|---|------|--------|
| 1 | Native build + tests | **PASS**: debug build has 169 targets and 0 warnings (warning_level=2). Tests: 28 OK, 0 Fail, 1 Skip (round11) of 29. |
| 2 | Windows cross build + Wine | **PASS**: `build-windows.sh` EXIT=0, 0 warnings, Wine tests 23 OK / 0 Fail / 1 Skip. The zip has share/lunduke-city-2/tool-icons (20 PNGs: 17 tools + zoo/bigpark/library) and special-tiles/{zoo,big-park,library}.png. All 23 files are byte-identical to data/. The Wine headless smoke run exits 0. In the live Wine run at 1280x800 the icons load, the dropdown works, water placement is refused and buildings place. |
| 3 | Sniper + Deck 1280x800 | **PASS**: SDK 3.0.20260928 (SDL 3.4.16, cairo 1.16.0, pango 1.46.2). Release build has 0 warnings. Tests 25 OK / 0 Fail / 2 Skip (argv_open, round11; release turns review_checks off, so there are 27 tests). Installed to a destdir and run with SteamDeck=1, without LUNDUKE_CITY_ASSETS. Toolbar icons match the source PNGs pixel-for-pixel, 17/17. On the map at 16 px tiles, Zoo and Big Park match special-tiles/*.png exactly. Library also matches exactly, apart from its outer 3 px, which the cursor frame covers. |
| 4 | Toolbar layout | **PASS (with nits)**: at 1280x800, 1920x1200, and UI scale 0.5 and 2 on both sizes, nothing is clipped or overlapping. Query moved to its own bottom row, the minimap and the RCI graph (with R/C/I labels) fit above the status bar, and the dropdown always opens to the right of the star and stays on screen. Nits: at 1280x800 with scale 2 the text grows but the palette shrinks to fit the height, so icons are smaller than at 1x and are scaled fractionally. At 0.5 the R/C/I letters stay large compared with the rest of the UI. In the dropdown the "$100" line sits tight against the name. |
| 5 | Dropdown input | **PASS**: **Mouse**: clicking the star opens it, hover moves the highlight, and clicking a row chooses. **Keyboard**: Down/Return chooses (cursor starts on the current choice), Esc closes. **Gamepad**: LB/RB onto Special opens it, RB away closes it, B closes, D-pad Up/Down moves, A chooses, A on the map builds. **Touch**: tapping the star opens it, tapping a row chooses, tapping the map places. Nit: when the dropdown is open, the Deck hint bar still says "A build B cancel LB/RB tools". |
| 6 | Placement | **PASS**: each building costs exactly $100 ($20,000 → $19,900 → $19,800 → $19,700; checked native, Wine and Deck). Placement on water is refused with "Bulldoze that area first." and money is unchanged. That wording is odd for water. Building again works, including on rubble and trees (auto-bulldoze). Placement is refused off the map and with less than $100 (unit test `special_buildings` cases 5–7; I did not reproduce the low-funds case in the UI). |
| 7 | Land value + income | **PASS (see blocker 1)**: in an engine harness with a road through the zoo, land value along the road is exactly +48 within ±6 tiles and +0 outside (164 vs 116 at 7 tiles, 92 vs 92 at 19 tiles). Note: in an empty city only "developed" cells (buildings and roads) get the bonus. The in-game Land value overlay is normalised to its peak, so it cannot show the +48 directly. **Budget before January** reads "Special buildings (not yet collected): +$200" with the note that these are estimates. **After January** it reads "Special buildings: +$200", and funds went $19,700 → $19,900. "Cash flow: $0" leaves this money out (minor inconsistency). Big Park pays $0. |
| 8 | Save/load, old files, bulldoze, fire, query | **PASS (with notes)**: **Save/load**: the round trip keeps all buildings, the road and the money; in the file there are 5 centres, all with ZONEBIT. **Old files**: a master-built save, a PR #7 save and the Hamburg scenario all load. **Bulldoze**: clicking an edge tile turns the whole 3x3 to rubble for $1. **Query**: works on the centre and on edge tiles ("Zoo — $100 a year, raises nearby land value. Cost $100"). **Disasters**: Earthquake causes no crash (live Deck run and ASan run). **Fire** (engine harness, 20 seeds): fire next to a building destroyed it fully in 4 runs and partly in 1. That run's query said "Damaged" and income and land bonus stopped. No stray BULLBIT outside the 3x3, no crash. **Edge case**: if the centre burns first, leftover body tiles can be orphaned. Query on them gives no special text, and they need one $1 bulldoze each. **Forward compatibility**: master cc710e2 refuses a PR #8 save that contains these buildings with "The map has 27 unknown tile values" (graceful, no crash). Older builds and other Micropolis-family games cannot open such saves. |
| 9 | ASan/UBSan | **PASS**: build has 0 warnings. Tests 28 OK / 0 Fail / 1 Skip. The live run under ASan covered: load the PR save, Query, dropdown by keyboard, gamepad and touch, place two more, run past January (income +$300), Earthquake, save, Ctrl+Q. There were no ASan or UBSan reports; leaks were only the suppressed fontconfig/GLX ones. |

## Icon clarity, judged against the editor's notes

The pixels are crisp: at 1x in Deck mode they match the 40x40 source PNGs exactly.

| Icon | Editor's note | Verdict |
|------|------|---------|
| Coal / Nuclear | Same plant, different stack | **Meets the note.** Both use the same small brown plant with orange windows. Coal has a tall black smokestack; Nuclear has a teal cooling tower with steam (plus a small dark base). At 1x they tell apart by stack colour and shape. The shared plant is small, so the icons look a bit sparse. |
| Police / Fire | PD and FD badges | **Fails as written.** The badges are good: a blue shield and a red one, with white letters. But the D is a rounded rectangle, so they read "PO" and "FO". It needs a D with a straight left side and corners cut only on the right. |
| Road / Rail | Horizontal, same width, no grass, no train | **Meets the note.** Both bands run rows 11–28, full width, on a transparent background. Road is dark asphalt with yellow dashes; rail is brown ties with grey rails. No grass and no train. Very clear. |
| Park | Tree that is not a circle, on grass | **Meets the note.** The canopy is blocky and lopsided, with a brown trunk on a green strip. It reads as a tree. |
| Power line | Old-style wooden pole | **Meets the note.** A brown pole with a crossarm and insulators. Easy to read, including on the yellow selected background. |
| Stadium | Football helmet | **Mostly meets it.** A blue helmet with a white stripe and facemask is clear at 3x. At 1x it's closer to "blue ball with lines", but still distinct from every other icon. |
| New: Special (star) | — | A gold star, clear. |
| New: Zoo / Big Park / Library (dropdown) | — | **Big Park**: two trees and a path, clear. **Zoo**: a hut and a grey elephant, OK. **Library**: a white columned building with a blue roof, but a large blue "F"-shaped banner in front reads as a letter F. Minor. |

The map sprites for Zoo, Big Park and Library (see buildings-3x.png) look good and are easy to recognise.

## Screenshots (full size)

All paths are under /workspace/city2-pr8/shots/.

**Requested:**
- compare-toolbar.png: master cc710e2 vs PR, each at 1x and 3x nearest. Also toolbar-{master,pr8}-{1x,3x}.png.
- deck1280-toolbar.png
- deck1280-dropdown.png
- deck1280-buildings.png and buildings-3x.png
- deck1280-query.png
- deck1280-budget.png (after January) and deck1280-budget-before-january.png
- w1920-dropdown.png

**Extra:**
- scale/ui-{1280x800,1920x1200}-s{0.5,2}-dropdown.png
- wine1280-placed.png
- master-rejects-pr8-save.png
- native-*.png: query, water refused, bulldoze, reload, land-value overlay
- work/: intermediate shots

## Logs

All logs are in /workspace/city2-pr8/logs/:
- build-x.log, test-x.log
- windows.log, wine-smoke.log
- sniper-*.log
- build-asan.log, test-asan.log
- asan-live.log.asan.*
- iconmatch-deck.log

The engine harness is scratch/exp/exp.cpp. Run it with `./exp lv|money|fire|fire2|orphan`.

## Housekeeping

I killed only my own exact PIDs: Xvfb :66 and :67, the app instances, and the gpg-agent the Windows script spawned. I also ran `wineserver -k` on this repo's prefix and removed the podman containers lc2pr8-build and lc2pr8-deck. The scratch/master worktree was removed with `git worktree remove`, and the /tmp unzip was deleted. I did not stash, push or comment, and did not touch lcos-* or other workspaces.

---

# VERIFY v2: PR #8 at 91c34d9 (final verification, 2026-10-21, America/Chicago)

Fresh clone: /workspace/city2-pr8/repo-v2, detached at 91c34d98825a009a65e1c92ee15128b525e50db2, `git status` clean.
Logs are in logs/v2/ and screenshots in shots-v2/.

**Verdict: NOT ready to merge as specified. Item 5 fails at UI scale 0.5.** Everything else passes. No merge was run.

| # | Check | Result |
|---|---|---|
| 1 | Linux meson build, tests, ASan | **PASS**: build-x and build-asan both have 0 warnings, and both test runs are 28 OK / 0 Fail / 1 Skip. **Live ASan run**: I loaded the save, bulldozed a Zoo, ran Earthquake (a Big Park burned), passed January twice and quit. There were no ASan or UBSan errors. LSan did report 154 KB in 24 allocations: four explosion sprite frames, obj7-1..4 (their PNG sizes add up exactly to the 3566-byte mime-data block). The cause is `sprite_art.cpp`'s static `std::vector<Frame>` cache, which never destroys its surfaces. That code dates from 54d112f, is already on master and is not touched by this PR. Bare launch/quit, quit-with-Discard, Earthquake alone and special-building bulldoze each run clean. **Pre-existing, not a PR #8 issue.** |
| 2 | Windows MinGW + Wine | **PASS**: EXIT=0, 0 warnings, Wine tests 23 OK / 0 Fail / 1 Skip. The zip has 20 tool-icons and 3 special-tiles, all byte-identical to data/. |
| 3 | Deck 1280x800 toolbar + dropdown | **PASS**: all 17 toolbar sprites match their PNGs exactly at 40px. Helmet is a side view, coal has smoke, nuclear has steam, PD is a blue shield, FD is a red Maltese cross, and the road's yellow line is centred. Query sits alone on the bottom row. The star dropdown shows Zoo, Big Park and Library at $100 with "A Select  B Close". Nit: the PD/FD letters overlap the gold rim, so contrast is low at 1x. |
| 4 | Gameplay | **PASS**. **Placing**: Zoo, Big Park and Library each charge $100. **Water**: "Can't build on water." and no money is taken. **Budget**: no special-buildings line, and funds stay flat through Jan 1901 and Jan 1902 ($19,600 → $19,600, then $19,599 → $19,599 after a $1 bulldoze). Engine harness: 100 buildings over 6 years, income 0. **Land value**: harness `lv` shows +48 within 6 tiles and 0 beyond. **Burned footprint**: harness orphan2 burned the centre, a corner and the far corner for all 3 types. One `bulldoze_special_at` click on any leftover cleared every body tile for $1. The UI bulldozer goes through `bulldoze_special_at` (city_session.cpp:969), and a corner click in the UI turned the whole 3x3 to rubble for $1. A live Earthquake fire burned one Big Park fully to rubble. **Save/load**: Ctrl+S, then a relaunch with the file brings back all 4 buildings, $19,600 and Feb 1901 (deck1280-reload.png). |
| 5 | UI scales 0.5 / 1 / 1.5 / 2 | **FAIL at 0.5**. At 1, 1.5 and 2 (both 1280x800 and 1920x1200) all 17 sprites match exactly at 40px. At 1.5 and 2 the buttons grow and the icons stay 40px, never smaller. **At 0.5 the toolbar cell is 24px and the icons are smoothly downscaled to about 16px** (scale0.5-toolbar-4x.png). This is deliberate in code: ui_scale.hpp `toolbar_cell` only applies the 40px floor `if (scale >= 1.0f)`, and the comment says "At UI scale 1 and above". The R/C/I labels scale correctly at every scale (demand_label_px = scaled_px(14)). Note: at 1280x800 a requested scale of 2 is fitted down to the largest scale where the chrome fits, so the 1.5 and 2 shots look the same there. |

Screenshots (shots-v2/): deck1280-toolbar.png, toolbar-3x.png, deck1280-dropdown.png, deck1280-buildings.png, deck1280-budget.png, deck1280-reload.png, scale-968-{0.5,1,1.5,2}.png (1280x800), scale-969-{0.5,1,1.5,2}.png (1920x1200), scale0.5-toolbar-4x.png.
Harness: scratch/exp2/ (exp lv|money|fire2, orphan2), built against repo-v2/build-x.
