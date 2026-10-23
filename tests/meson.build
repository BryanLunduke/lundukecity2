# A few tests drive POSIX-only behaviour (symlinks, ownership, rlimits,
# fork/exec and the X window tree). They are not built for Windows.
posix_host = host_machine.system() != 'windows'

engine_smoke = executable(
  'engine_smoke',
  'engine_smoke.cpp',
  dependencies: [micropolis_dep],
)

test('engine_smoke', engine_smoke)

engine_fixes = executable(
  'engine_fixes',
  'engine_fixes.cpp',
  dependencies: [micropolis_dep],
)

test('engine_fixes', engine_fixes)

features = executable(
  'features',
  'features.cpp',
  '../src/assets.cpp',
  '../src/city_session.cpp',
  '../src/special_buildings.cpp',
  '../src/messages.cpp',
  '../src/sound_player.cpp',
  '../src/tools.cpp',
  include_directories: include_directories('../src'),
  dependencies: [micropolis_dep, thread_dep, sdl3_dep],
  cpp_args: [
    '-DLUNDUKE_CITY_ASSET_DIR="' + lunduke_asset_dir + '"',
    '-DLUNDUKE_CITY_PACKAGE_VERSION="' + lunduke_package_version + '"',
    '-DLUNDUKE_CITY_TEST_HOOKS',
  ],
)

# Dummy audio: opening the default ALSA device leaks inside libasound when
# the machine has no sound card. started==0 is an accepted features result.
test(
  'features',
  features,
  env: [
    'SDL_AUDIODRIVER=dummy',
    'SDL_AUDIO_DRIVER=dummy',
  ],
)

if posix_host
  regressions = executable(
    'regressions',
    'regressions.cpp',
    '../src/assets.cpp',
    '../src/city_session.cpp',
    '../src/special_buildings.cpp',
    '../src/messages.cpp',
    '../src/sound_player.cpp',
    '../src/tools.cpp',
    include_directories: include_directories('../src'),
    dependencies: [micropolis_dep, thread_dep, sdl3_dep],
    cpp_args: [
      '-DLUNDUKE_CITY_ASSET_DIR="' + lunduke_asset_dir + '"',
      '-DLUNDUKE_CITY_PACKAGE_VERSION="' + lunduke_package_version + '"',
      '-DLUNDUKE_CITY_TEST_HOOKS',
    ],
  )

  test('regressions', regressions)
endif

if posix_host
  round3 = executable(
    'round3',
    'round3.cpp',
    '../src/assets.cpp',
    '../src/city_session.cpp',
    '../src/special_buildings.cpp',
    '../src/messages.cpp',
    '../src/sound_player.cpp',
    '../src/tools.cpp',
    include_directories: include_directories('../src'),
    dependencies: [micropolis_dep, thread_dep, sdl3_dep],
    cpp_args: [
      '-DLUNDUKE_CITY_ASSET_DIR="' + lunduke_asset_dir + '"',
      '-DLUNDUKE_CITY_PACKAGE_VERSION="' + lunduke_package_version + '"',
      '-DLUNDUKE_CITY_TEST_HOOKS',
    ],
  )

  test('round3', round3, timeout: 120)
endif



round4 = executable(
  'round4',
  'round4.cpp',
  '../src/assets.cpp',
  '../src/city_session.cpp',
  '../src/special_buildings.cpp',
  '../src/messages.cpp',
  '../src/sound_player.cpp',
  '../src/tools.cpp',
  include_directories: include_directories('../src'),
  dependencies: [micropolis_dep, thread_dep, sdl3_dep],
  cpp_args: [
    '-DLUNDUKE_CITY_ASSET_DIR="' + lunduke_asset_dir + '"',
    '-DLUNDUKE_CITY_PACKAGE_VERSION="' + lunduke_package_version + '"',
    '-DLUNDUKE_CITY_TEST_HOOKS',
  ],
)

test('round4', round4, timeout: 180)

round5 = executable(
  'round5',
  'round5.cpp',
  '../src/assets.cpp',
  '../src/city_session.cpp',
  '../src/special_buildings.cpp',
  '../src/messages.cpp',
  '../src/sound_player.cpp',
  '../src/tools.cpp',
  include_directories: include_directories('../src'),
  dependencies: [micropolis_dep, thread_dep, sdl3_dep],
  cpp_args: [
    '-DLUNDUKE_CITY_ASSET_DIR="' + lunduke_asset_dir + '"',
    '-DLUNDUKE_CITY_PACKAGE_VERSION="' + lunduke_package_version + '"',
    '-DLUNDUKE_CITY_TEST_HOOKS',
  ],
)

test('round5', round5, timeout: 180)



round6 = executable(
  'round6',
  'round6.cpp',
  '../src/assets.cpp',
  '../src/city_session.cpp',
  '../src/special_buildings.cpp',
  '../src/messages.cpp',
  '../src/sound_player.cpp',
  '../src/tools.cpp',
  include_directories: include_directories('../src'),
  dependencies: [micropolis_dep, thread_dep, sdl3_dep],
  cpp_args: [
    '-DLUNDUKE_CITY_ASSET_DIR="' + lunduke_asset_dir + '"',
    '-DLUNDUKE_CITY_PACKAGE_VERSION="' + lunduke_package_version + '"',
    '-DLUNDUKE_CITY_TEST_HOOKS',
  ],
)

test('round6', round6, timeout: 180)




# Reads pixels from the window. Other GUI tests on the same X server cover
# those pixels, so this one runs by itself.

round7 = executable(
  'round7',
  'round7.cpp',
  include_directories: include_directories('../src'),
  cpp_args: [
    '-DLUNDUKE_CITY_SOURCE_DIR="' + meson.project_source_root() + '"',
  ],
)

test('round7', round7)

production_binary = executable(
  'production_binary',
  'production_binary.cpp',
)

test(
  'production_binary',
  production_binary,
  args: [lunduke_city_exe.full_path()],
  depends: lunduke_city_exe,
)

if posix_host
  argv_open = executable(
    'argv_open',
    'argv_open.cpp',
    '../src/assets.cpp',
    '../src/city_session.cpp',
    '../src/special_buildings.cpp',
    '../src/messages.cpp',
    '../src/sound_player.cpp',
    '../src/tools.cpp',
    include_directories: include_directories('../src'),
    dependencies: [micropolis_dep, thread_dep, sdl3_dep],
    cpp_args: [
      '-DLUNDUKE_CITY_ASSET_DIR="' + lunduke_asset_dir + '"',
      '-DLUNDUKE_CITY_PACKAGE_VERSION="' + lunduke_package_version + '"',
    ],
  )

  # This test starts the real binary and reads the window tree. It does not
  # share the display with the other GUI tests.
  argv_env = environment()
  argv_env.set('SDL_VIDEODRIVER', 'x11')
  # Empty overrides a Deck flag or a size inherited from the caller. The
  # test also unsets both before it launches the city. Meson 1.3 has no
  # environment.unset().
  argv_env.set('SteamDeck', '')
  argv_env.set('LUNDUKE_CITY2_SIZE', '')
  test(
    'argv_open',
    argv_open,
    args: [lunduke_city_exe.full_path()],
    depends: lunduke_city_exe,
    timeout: 120,
    is_parallel: false,
    env: argv_env,
  )
endif



round8 = executable(
  'round8',
  'round8.cpp',
  '../src/assets.cpp',
  '../src/city_session.cpp',
  '../src/special_buildings.cpp',
  '../src/messages.cpp',
  '../src/sound_player.cpp',
  '../src/tools.cpp',
  include_directories: include_directories('../src'),
  dependencies: [micropolis_dep, thread_dep, sdl3_dep],
  cpp_args: [
    '-DLUNDUKE_CITY_ASSET_DIR="' + lunduke_asset_dir + '"',
    '-DLUNDUKE_CITY_PACKAGE_VERSION="' + lunduke_package_version + '"',
    '-DLUNDUKE_CITY_TEST_HOOKS',
  ],
)

test('round8', round8)



round9 = executable(
  'round9',
  'round9.cpp',
  include_directories: include_directories('../src'),
  dependencies: [micropolis_dep, thread_dep, sdl3_dep],
  cpp_args: [
    '-DLUNDUKE_CITY_ASSET_DIR="' + lunduke_asset_dir + '"',
    '-DLUNDUKE_CITY_PACKAGE_VERSION="' + lunduke_package_version + '"',
    '-DLUNDUKE_CITY_TEST_HOOKS',
  ],
)

test('round9', round9)



round10 = executable(
  'round10',
  'round10.cpp',
  include_directories: include_directories('../src'),
  dependencies: [micropolis_dep, thread_dep, sdl3_dep],
  cpp_args: [
    '-DLUNDUKE_CITY_ASSET_DIR="' + lunduke_asset_dir + '"',
    '-DLUNDUKE_CITY_PACKAGE_VERSION="' + lunduke_package_version + '"',
    '-DLUNDUKE_CITY_TEST_HOOKS',
  ],
)

test('round10', round10)

# Round 11 (C-1): a short write on a full disk reports ENOSPC. Needs a full
# filesystem: set LUNDUKE_CITY_FULL_DIR to a directory on one, else skips.
round11 = executable(
  'round11',
  'round11.cpp',
  include_directories: include_directories('../src'),
  dependencies: [micropolis_dep, thread_dep, sdl3_dep],
)

test('round11', round11)

# Round 11 (C-2): the main window's minimum size fits 1280x720 and
# GDK_SCALE=2 on 1366x768.


# Round 12: Help menu, Save As shortcut, empty-city evaluation, load reasons,
# full map redraw (dirt map), Budget fits 1280x720 and 1366x768.


# Round 13 (C-1, C-2): reject damaged city files with a reason; clamp
# window positions to the work area.
round13 = executable(
  'round13',
  'round13.cpp',
  include_directories: include_directories('../src'),
  dependencies: [micropolis_dep, thread_dep],
  cpp_args: [
    '-DLUNDUKE_CITY_ASSET_DIR="' + lunduke_asset_dir + '"',
  ],
)

test('round13', round13)

round14 = executable(
  'round14',
  'round14.cpp',
  include_directories: include_directories('../src'),
  dependencies: [micropolis_dep, thread_dep, sdl3_dep],
)

test('round14', round14)

# Round 15 (C-1..C-3): autosave/recovery helpers, save through a symlink,
# group kept, stale save temps removed.
if posix_host
  round15 = executable(
    'round15',
    'round15.cpp',
    '../src/assets.cpp',
    '../src/city_session.cpp',
    '../src/special_buildings.cpp',
    '../src/messages.cpp',
    '../src/sound_player.cpp',
    '../src/tools.cpp',
    include_directories: include_directories('../src'),
    dependencies: [micropolis_dep, thread_dep, sdl3_dep],
    cpp_args: [
      '-DLUNDUKE_CITY_ASSET_DIR="' + lunduke_asset_dir + '"',
      '-DLUNDUKE_CITY_PACKAGE_VERSION="' + lunduke_package_version + '"',
      '-DLUNDUKE_CITY_TEST_HOOKS',
    ],
  )

  test('round15', round15)
endif

# Round 15 window checks: autosave timer, SIGTERM recovery, the recovery
# prompt (Not Now / Discard-Cancel / Recover), Open Recent.


# 0.9.5-2 C-1: outside opens held while a City dialog is up; Save City As
# bound to its city.


# 0.9.5-2 C-2: a second launch (plain, or with a .cty) forwards to the
# running City and exits; it does not stay behind as a hidden process.

# Menu model: 35 actionable rows, accelerators, and keyboard navigation.
# The gtkmm window tests are gone with the toolkit. Single-instance D-Bus
# forwarding (second_launch) is not part of the SDL shell.
menu_model = executable(
  'menu_model',
  'menu_model.cpp',
  include_directories: include_directories('../src'),
)

test('menu_model', menu_model)

# Evaluation / Budget wording restored from 0.9.5, and the query em dash.
dialog_copy = executable(
  'dialog_copy',
  'dialog_copy.cpp',
  include_directories: include_directories('../src'),
)

test('dialog_copy', dialog_copy)

# Unique hotspot ids, and on-screen keyboard Up/Down by row.
hotspots = executable(
  'hotspots',
  'hotspots.cpp',
  include_directories: include_directories('../src'),
)

test('hotspots', hotspots)

# Original 24px toolbar sprites, found beside the source tree.
tool_icons = executable(
  'tool_icons',
  'tool_icons.cpp',
  '../src/assets.cpp',
  include_directories: include_directories('../src'),
  dependencies: [cairo_dep],
)

test('tool_icons', tool_icons)

special_buildings_test = executable(
  'special_buildings',
  'special_buildings.cpp',
  '../src/assets.cpp',
  '../src/city_session.cpp',
  '../src/special_buildings.cpp',
  '../src/messages.cpp',
  '../src/sound_player.cpp',
  '../src/tools.cpp',
  include_directories: include_directories('../src'),
  dependencies: [micropolis_dep, thread_dep, sdl3_dep],
  cpp_args: [
    '-DLUNDUKE_CITY_ASSET_DIR="' + lunduke_asset_dir + '"',
    '-DLUNDUKE_CITY_PACKAGE_VERSION="' + lunduke_package_version + '"',
  ],
)

test('special_buildings', special_buildings_test, timeout: 120)

# Thumbnails painted from the scenario files themselves.
scenario_preview_test = executable(
  'scenario_preview',
  'scenario_preview.cpp',
  '../src/scenario_preview.cpp',
  '../src/tile_atlas.cpp',
  '../src/assets.cpp',
  '../src/city_session.cpp',
  '../src/special_buildings.cpp',
  '../src/messages.cpp',
  '../src/sound_player.cpp',
  '../src/tools.cpp',
  include_directories: include_directories('../src'),
  dependencies: [micropolis_dep, thread_dep, sdl3_dep, cairo_dep],
  cpp_args: [
    '-DLUNDUKE_CITY_ASSET_DIR="' + lunduke_asset_dir + '"',
    '-DLUNDUKE_CITY_PACKAGE_VERSION="' + lunduke_package_version + '"',
  ],
)

test('scenario_preview', scenario_preview_test, timeout: 120)

review = executable(
  'review',
  'review.cpp',
  '../src/assets.cpp',
  include_directories: include_directories('../src'),
)

test('review', review)

# Review fixes that do not need a window. Built on Windows too.
round16 = executable(
  'round16',
  'round16.cpp',
  '../src/assets.cpp',
  '../src/city_glance.cpp',
  '../src/city_session.cpp',
  '../src/messages.cpp',
  '../src/overlay_map.cpp',
  '../src/sound_player.cpp',
  '../src/special_buildings.cpp',
  '../src/sprite_art.cpp',
  '../src/tools.cpp',
  include_directories: include_directories('../src'),
  dependencies: [micropolis_dep, thread_dep, sdl3_dep, cairo_dep],
  cpp_args: [
    '-DLUNDUKE_CITY_ASSET_DIR="' + lunduke_asset_dir + '"',
    '-DLUNDUKE_CITY_PACKAGE_VERSION="' + lunduke_package_version + '"',
    '-DLUNDUKE_CITY_TEST_HOOKS',
  ],
)

test(
  'round16',
  round16,
  env: [
    'SDL_AUDIODRIVER=dummy',
    'SDL_AUDIO_DRIVER=dummy',
  ],
  timeout: 180,
)

# Headless checks for the numbered review fixes. The binary drives itself.
# Release builds omit the self-test, so this test is not registered there.
if lunduke_review_checks_on
  review_env = environment()
  review_env.set('SDL_VIDEODRIVER', 'dummy')
  review_env.set('SDL_AUDIODRIVER', 'dummy')
  review_env.set('LUNDUKE_CITY2_REVIEW', '1')
  review_env.set('LUNDUKE_CITY2_SIZE', '1280x800')
  # The review build clears this itself. An empty value also keeps a Deck
  # flag in the parent environment from forcing fullscreen.
  review_env.set('SteamDeck', '')
  test(
    'review_ui',
    lunduke_city_exe,
    env: review_env,
    timeout: 180,
    is_parallel: false,
  )
endif

# Menu hit-testing, gamepad drag-build, and budget slider keys. Headless.
test(
  'ui_input',
  lunduke_city_exe,
  env: [
    'SDL_VIDEODRIVER=dummy',
    'SDL_AUDIODRIVER=dummy',
    'LUNDUKE_CITY2_UI_TEST=1',
  ],
  timeout: 180,
  is_parallel: false,
)

# Headless SDL: start, load Dullsville, tick, draw a frame, cycle a tool
# from a virtual gamepad, accept a synthetic tap.
test(
  'sdl_smoke',
  lunduke_city_exe,
  env: [
    'SDL_VIDEODRIVER=dummy',
    'SDL_AUDIODRIVER=dummy',
    'LUNDUKE_CITY2_SMOKE=1',
  ],
  timeout: 120,
  is_parallel: false,
)
