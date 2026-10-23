# PR #12 verification: SVGA Look (cursor/svga-look-8ed6 @ f496ba8)

Fresh clone at /workspace/city2-pr12/repo, detached at f496ba865983400b2949a421930075a9fba5f109.
Master reference clone at /workspace/city2-pr12/master (75d26ef).
Tests ran in a clean env (SteamDeck and LUNDUKE_CITY2_SIZE unset). Builds used -j2.
Nothing was committed, pushed, stashed, merged or commented.

## Results

| # | Check | Result |
|---|-------|--------|
| 1 | Builds and tests: Linux, ASan+UBSan, Windows/Wine | **FAIL**: all builds are clean with 0 warnings, but `svga_atlas` fails in all three: Linux 30 OK/1 FAIL/1 skip, ASan 30/1 FAIL/1, Wine 25/1 FAIL/1, and the Windows packaging exits 1 with no zip |
| 2 | Toggle via mouse, keyboard (Alt+O C/K), touch, gamepad (Start, Right, Up, A) and Deck mode | PASS: the check mark moves, tiles switch instantly at 8/16/32/48/64px and the minimap updates. Only the map and minimap pixels change |
| 3 | Persistence | PASS: a fresh profile starts on Classic. SVGA persists across relaunch (look=1), and so does Classic (look=0) |
| 4 | Fallback when tiles-svga.png is missing | PASS: stays on Classic with no crash, and picking SVGA leaves Classic checked. See the Low bug below |
| 5 | Classic unchanged vs master 75d26ef | PASS: the same city (fixed seed) has 0 differing pixels in the whole window at 8, 16 and 32px. The classic atlas (16px, 8px overview and averages) is byte-identical for all 960 tiles |
| 6 | SVGA visual check (Dullsville, Hamburg, Tokyo and Boston) | PASS: no wrong tiles or seams found. Fire, explosion, rubble, radiation, no-power blink, stadium, airport and nuclear tiles render fine. PD/FD/C/I/R are legible. Notes below |
| 7 | Quick regression | PASS: touch build (road drag 6 tiles = $60, R zone $100, road tap $10; funds 20,000 to 19,830), pinch zoom, two-finger pan with no accidental build, Ctrl+wheel and LT zoom, gamepad RB tool + A build (rail $21), bridge refusal on water, Save As, New City, Load, Budget, power overlay and Ctrl+Q quit |

## Bugs

- **High: the `svga_atlas` test needs `svga-refs/warcraft.png`, which is not in the repo.** A fresh clone fails with "missing Warcraft reference". That breaks ctest on Linux, ASan and Wine, and makes the Windows packaging script exit 1, so no dist zip is produced. With a dummy reference in a scratch copy, make_atlas reports RESULT: PASS, so the committed atlas itself is fine. Fix options: commit the reference, or skip the test (ctest SKIP) when the reference is absent.
- **Low: the stored preference is overwritten when the SVGA sheet is missing.** With look=1 stored and the sheet missing, the game correctly falls back to Classic. But clicking SVGA, or quitting, then saves look=0, so the user's SVGA choice is lost once the sheet comes back.
- **Low: the test writes into the source tree.** It rewrites tracked files (previews/warcraft-dirt-compare.png, verify-report.txt) and leaves an untracked art/svga/__pycache__. The fresh clone now shows only `?? art/svga/__pycache__/`.
- Cosmetic notes, not bugs:
  - In SVGA the shoreline has a thin dark-red rim. Classic has a tan rim there.
  - The coal plant's lightning symbol has lower contrast than in Classic.
  - Load/Save dialog file thumbnails use a flat palette in both looks. Scenario previews do follow the look.

## Best screenshots (shots/best/)

- Tokyo, same view (1280x800, 16px, paused, same game time):
  - tokyo-classic.png
  - tokyo-svga.png
  - tokyo-side-by-side.png
- Tokyo at three zooms: tokyo-{8,16,32}px-{classic,svga}.png
- Dullsville at 32px: dullsville-32px-classic.png, dullsville-32px-svga.png
- options-menu-svga-checked.png
- deck-options-svga-checked.png, deck-tokyo-svga.png
- persist-svga-restored-after-relaunch.png
- fallback-missing-svga-stays-classic.png
- classic-16px-master-75d26ef.png, classic-16px-pr12.png (0 pixels differ)
- hamburg-fires-svga-32px.png
- boston-nuclear-airport-svga-32px.png, boston-meltdown-svga-32px.png
- regress-touch-build-svga.png, regress-saved-city-reloaded-svga.png

---

# v2 re-verification: 6df2dc233eaa80b2a4f18851f57c320785d47800 (Oct 22, 2026, ~10:40–11:15 AM CDT)

Fresh clone at /workspace/city2-pr12/v2, detached at 6df2dc2. Clean env (SteamDeck, LUNDUKE_CITY2_SIZE unset), -j2.
The fix commit touches make_atlas.py, tests/meson.build, .gitignore, shell*.cpp and tile_atlas.*. It changes no PNGs.
Nothing was merged, pushed, committed, stashed or commented.

| # | Check | Result |
|---|-------|--------|
| 1 | Builds and tests | **PASS**: Linux 31 OK/0 fail/1 skip, ASan+UBSan 31/0/1, Wine 26/0/1. The skip is `round11` (exit 77). There are 0 warnings in all three build logs. Windows packaging EXIT=0 and produced `v2/build-windows/dist/lunduke-city-2-windows-x86_64.zip` (13.5 MB). The OK counts are one higher than the expected 30/30/25 because the previously failing `svga_atlas` now passes; total test counts are unchanged. |
| 2 | git status after tests | **PASS**: `git status --short` is empty. `--ignored` lists only build-x/, build-asan/ and build-windows/. There are no `__pycache__` or `.pyc` files in the source tree. The test writes its report and previews to `build-x/svga-atlas/`. The committed tiles-svga.png is checked in place: "OK committed tiles-svga.png … hash f52025ef8319", and "SKIP warcraft comparison" because the reference is not in the tree. |
| 3 | Missing sheet keeps the stored choice | **PASS**, using a copy at `scratch/assets-v2` via LUNDUKE_CITY_ASSETS, with no size override so settings save: (a) fresh profile, picked SVGA by mouse, look=1, quit; (b) moved tiles-svga.png out of the copy, relaunched: Classic shown and checked; picked SVGA: still Classic, look=1; Ctrl+Q quit rewrote settings.txt (mtime 10:50:55 AM CDT) still with look=1; (c) restored the sheet (same sha256), relaunched: SVGA shown and checked; clicked Classic: look=0. |
| 4 | Smoke test | **PASS**: the mouse toggle works both ways. In Deck mode (SteamDeck=1), Start → Options → Up → A selects SVGA: the check moves and the map and minimap switch; switching back to Classic leaves the map area pixel-identical to before. With the fixed seed, Detroit at 16px Classic shows 0 differing pixels in the whole window between master 75d26ef and v2. The fixed-seed Tokyo SVGA shot has 0 differing pixels between f496ba8 and v2. tiles-svga.png sha256 01221530…c1c3 and tiles.png sha256 e80506a7…c174 are identical to f496ba8. |

## v2 bugs

None blocking. Minor nits:
- The verify-report "previews:" list still names `previews/warcraft-dirt-compare.png` even though that comparison is skipped and the file is not written.
- (Pre-existing, unchanged) The Save/Load dialog file thumbnails use the same flat palette in both looks.

## v2 screenshots (shots/best/v2/)
- mouse-toggle-svga.png
- deck-options-svga-checked.png
- missing-sheet-classic-shown.png
- sheet-restored-svga-back.png
- classic-16px-master.png, classic-16px-v2.png (0 px differ)
- tokyo-16px-svga-v2.png (0 px differ vs f496ba8)
