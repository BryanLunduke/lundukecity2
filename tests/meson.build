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
  cpp_args: ['-DLUNDUKE_CITY_ASSET_DIR="' + lunduke_asset_dir + '"'],
)

test('engine_fixes', engine_fixes)

features = executable(
  'features',
  'features.cpp',
  '../src/assets.cpp',
  '../src/city_session.cpp',
  '../src/city_events.cpp',
  '../src/placement_hint.cpp',
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

seattle_scenario = executable(
  'seattle_scenario',
  'seattle_scenario.cpp',
  '../src/assets.cpp',
  '../src/city_session.cpp',
  '../src/city_events.cpp',
  '../src/placement_hint.cpp',
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

test(
  'seattle_scenario',
  seattle_scenario,
  env: [
    'SDL_AUDIODRIVER=dummy',
    'SDL_AUDIO_DRIVER=dummy',
  ],
)

sf1962_scenario = executable(
  'sf1962_scenario',
  'sf1962_scenario.cpp',
  '../src/assets.cpp',
  '../src/city_session.cpp',
  '../src/city_events.cpp',
  '../src/placement_hint.cpp',
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

test(
  'sf1962_scenario',
  sf1962_scenario,
  env: [
    'SDL_AUDIODRIVER=dummy',
    'SDL_AUDIO_DRIVER=dummy',
  ],
)

portland_scenario = executable(
  'portland_scenario',
  'portland_scenario.cpp',
  '../src/assets.cpp',
  '../src/city_session.cpp',
  '../src/city_events.cpp',
  '../src/placement_hint.cpp',
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

test(
  'portland_scenario',
  portland_scenario,
  env: [
    'SDL_AUDIODRIVER=dummy',
    'SDL_AUDIO_DRIVER=dummy',
  ],
)

sandiego_scenario = executable(
  'sandiego_scenario',
  'sandiego_scenario.cpp',
  '../src/assets.cpp',
  '../src/city_session.cpp',
  '../src/city_events.cpp',
  '../src/placement_hint.cpp',
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

test(
  'sandiego_scenario',
  sandiego_scenario,
  env: [
    'SDL_AUDIODRIVER=dummy',
    'SDL_AUDIO_DRIVER=dummy',
  ],
)

crime_load = executable(
  'crime_load',
  'crime_load.cpp',
  '../src/assets.cpp',
  '../src/city_session.cpp',
  '../src/city_events.cpp',
  '../src/placement_hint.cpp',
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

test(
  'crime_load',
  crime_load,
  env: [
    'SDL_AUDIODRIVER=dummy',
    'SDL_AUDIO_DRIVER=dummy',
  ],
)

load_score = executable(
  'load_score',
  'load_score.cpp',
  '../src/assets.cpp',
  '../src/city_session.cpp',
  '../src/city_events.cpp',
  '../src/placement_hint.cpp',
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

test(
  'load_score',
  load_score,
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
    '../src/city_events.cpp',
  '../src/placement_hint.cpp',
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
    '../src/city_events.cpp',
  '../src/placement_hint.cpp',
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
  '../src/city_events.cpp',
  '../src/placement_hint.cpp',
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
  '../src/city_events.cpp',
  '../src/placement_hint.cpp',
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
  '../src/city_events.cpp',
  '../src/placement_hint.cpp',
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
    '../src/city_events.cpp',
  '../src/placement_hint.cpp',
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
  '../src/city_events.cpp',
  '../src/placement_hint.cpp',
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
    '../src/city_events.cpp',
  '../src/placement_hint.cpp',
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

round17 = executable(
  'round17',
  'round17.cpp',
  include_directories: include_directories('../src'),
)

test('round17', round17)

svga_look = executable(
  'svga_look',
  'svga_look.cpp',
  '../src/assets.cpp',
  '../src/tile_atlas.cpp',
  include_directories: include_directories('../src'),
  dependencies: [cairo_dep],
  cpp_args: [
    '-DLUNDUKE_CITY_ASSET_DIR="' + lunduke_asset_dir + '"',
  ],
)

test('svga_look', svga_look)

python = find_program('python3', required: true)
# Previews and the report go to the build tree. The script must not rewrite
# the committed sheets or leave bytecode beside the source. Pillow is absent
# on the CI image; atlas_test.py exits 77 then, and Meson skips the test.
# A hand run of the atlas script still reports the missing library.
atlas_test = meson.project_source_root() / 'tests' / 'atlas_test.py'
test(
  'svga_atlas',
  python,
  args: [
    atlas_test,
    meson.project_source_root() / 'art' / 'svga' / 'make_atlas.py',
    '--out', meson.project_build_root() / 'svga-atlas',
  ],
  env: ['PYTHONDONTWRITEBYTECODE=1'],
  timeout: 180,
)

test(
  'spooky_atlas',
  python,
  args: [
    atlas_test,
    meson.project_source_root() / 'art' / 'spooky' / 'make_atlas.py',
    '--out', meson.project_build_root() / 'spooky-atlas',
  ],
  env: ['PYTHONDONTWRITEBYTECODE=1'],
  timeout: 180,
)

# packaging/steam: the Steam Input layouts send what factory_pad expects.
test(
  'steam_input',
  python,
  args: [meson.project_source_root() / 'tests' / 'steam_input_test.py', meson.project_source_root()],
  env: ['PYTHONDONTWRITEBYTECODE=1'],
)

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

# The rolling Funds readout, its floating change, and the count-up text.
funds_tick = executable(
  'funds_tick',
  'funds_tick.cpp',
  include_directories: include_directories('../src'),
)

test('funds_tick', funds_tick)

# Original 24px toolbar sprites, found beside the source tree.
tool_icons = executable(
  'tool_icons',
  'tool_icons.cpp',
  '../src/assets.cpp',
  include_directories: include_directories('../src'),
  dependencies: [cairo_dep],
)

test('tool_icons', tool_icons)

welcome_asset = executable(
  'welcome_asset',
  'welcome_asset.cpp',
  '../src/assets.cpp',
  include_directories: include_directories('../src'),
  dependencies: [cairo_dep],
)

test('welcome_asset', welcome_asset)

special_buildings_test = executable(
  'special_buildings',
  'special_buildings.cpp',
  '../src/assets.cpp',
  '../src/city_session.cpp',
  '../src/city_events.cpp',
  '../src/placement_hint.cpp',
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

landmarks_test = executable(
  'landmarks',
  'landmarks.cpp',
  '../src/special_buildings.cpp',
  include_directories: include_directories('../src'),
  dependencies: [micropolis_dep],
)

test('landmarks', landmarks_test, timeout: 120)

new_specials_test = executable(
  'new_specials',
  'new_specials.cpp',
  '../src/placement_hint.cpp',
  '../src/special_buildings.cpp',
  include_directories: include_directories('../src'),
  dependencies: [micropolis_dep],
  cpp_args: ['-DLUNDUKE_CITY_ASSET_DIR="' + lunduke_asset_dir + '"'],
)

test('new_specials', new_specials_test, timeout: 120)

# C1: the save-format marker and the guard that older builds refuse.
save_format_test = executable(
  'save_format',
  'save_format.cpp',
  '../src/city_glance.cpp',
  include_directories: include_directories('../src'),
  dependencies: [micropolis_dep],
  cpp_args: ['-DLUNDUKE_CITY_ASSET_DIR="' + lunduke_asset_dir + '"'],
)

test('save_format', save_format_test, timeout: 120)

# Landmarks that use electricity take power, flash, and count for the score.
landmark_power_test = executable(
  'landmark_power',
  'landmark_power.cpp',
  '../src/placement_hint.cpp',
  '../src/special_buildings.cpp',
  include_directories: include_directories('../src'),
  dependencies: [micropolis_dep],
  cpp_args: ['-DLUNDUKE_CITY_ASSET_DIR="' + lunduke_asset_dir + '"'],
)

test('landmark_power', landmark_power_test, timeout: 120)

# Data Center (I+C when powered, no pollution) and micro nuclear (1/9 power,
# no meltdown): prices, capacity, save/load, glance, night glass.
datacenter_micro_test = executable(
  'datacenter_micro',
  'datacenter_micro.cpp',
  '../src/city_glance.cpp',
  '../src/night_light.cpp',
  '../src/placement_hint.cpp',
  '../src/special_buildings.cpp',
  '../src/tools.cpp',
  include_directories: include_directories('../src'),
  dependencies: [micropolis_dep],
  cpp_args: ['-DLUNDUKE_CITY_ASSET_DIR="' + lunduke_asset_dir + '"'],
)

test('datacenter_micro', datacenter_micro_test, timeout: 300)

# Thumbnails painted from the scenario files themselves.
scenario_preview_test = executable(
  'scenario_preview',
  'scenario_preview.cpp',
  '../src/scenario_preview.cpp',
  '../src/tile_atlas.cpp',
  '../src/assets.cpp',
  '../src/city_session.cpp',
  '../src/city_events.cpp',
  '../src/placement_hint.cpp',
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

editorial = executable(
  'editorial',
  'editorial.cpp',
  '../src/placement_hint.cpp',
  '../src/special_buildings.cpp',
  include_directories: include_directories('../src'),
  dependencies: [micropolis_dep],
)

test('editorial', editorial)

# Review fixes that do not need a window. Built on Windows too.
round16 = executable(
  'round16',
  'round16.cpp',
  '../src/assets.cpp',
  '../src/city_glance.cpp',
  '../src/city_session.cpp',
  '../src/city_events.cpp',
  '../src/placement_hint.cpp',
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

# The Inspect outline covers the whole building footprint. Built on
# Windows too.
footprint = executable(
  'footprint',
  'footprint.cpp',
  '../src/assets.cpp',
  '../src/city_glance.cpp',
  '../src/city_session.cpp',
  '../src/city_events.cpp',
  '../src/placement_hint.cpp',
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
  ],
)

test(
  'footprint',
  footprint,
  env: [
    'SDL_AUDIODRIVER=dummy',
    'SDL_AUDIO_DRIVER=dummy',
  ],
  timeout: 120,
)

# Slate skin, grouped toolbar, Inspect card, smooth heat map, night light
# layer and the handheld layout switch. Pure helpers; built on Windows too.
polish = executable(
  'polish',
  'polish.cpp',
  '../src/night_light.cpp',
  include_directories: include_directories('../src'),
  dependencies: [micropolis_dep, cairo_dep],
  cpp_args: ['-DLUNDUKE_CITY_ASSET_DIR="' + lunduke_asset_dir + '"'],
)

test('polish', polish)

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
presentation = executable(
  'presentation',
  'presentation.cpp',
  '../src/assets.cpp',
  '../src/city_session.cpp',
  '../src/city_events.cpp',
  '../src/placement_hint.cpp',
  '../src/special_buildings.cpp',
  '../src/messages.cpp',
  '../src/sound_player.cpp',
  '../src/tools.cpp',
  '../src/tile_atlas.cpp',
  include_directories: include_directories('../src'),
  dependencies: [micropolis_dep, thread_dep, sdl3_dep, cairo_dep],
  cpp_args: [
    '-DLUNDUKE_CITY_ASSET_DIR="' + lunduke_asset_dir + '"',
    '-DLUNDUKE_CITY_PACKAGE_VERSION="' + lunduke_package_version + '"',
  ],
)

test(
  'presentation',
  presentation,
  env: ['SDL_AUDIODRIVER=dummy', 'SDL_AUDIO_DRIVER=dummy'],
  timeout: 120,
)

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

# Compare decoded pixels so the single icon set stays reproducible. Like the
# atlas tests, skip on builders without Pillow and never rewrite source art.
test(
  'tool_icons_generator',
  python,
  args: [
    atlas_test,
    meson.project_source_root() / 'data' / 'tool-icons' / 'make_icons.py',
    '--check',
  ],
  env: ['PYTHONDONTWRITEBYTECODE=1'],
)

# Same check for the Data Center / Micro Nuclear map sheets and the Data
# Center night-glass mask.
test(
  'dcmicro_sprites_generator',
  python,
  args: [
    atlas_test,
    meson.project_source_root() / 'art' / 'dcmicro' / 'make_dcmicro.py',
    '--check',
  ],
  env: ['PYTHONDONTWRITEBYTECODE=1'],
)

# November playtest notes: fountain Inspect, menu highlight, Inspect bars
# and the Year in Review wording. Built on Windows too.
# Power plants need no road; their Inspect card does not ask for one.
plant_inspect = executable(
  'plant_inspect',
  'plant_inspect.cpp',
  '../src/assets.cpp',
  '../src/city_glance.cpp',
  '../src/city_session.cpp',
  '../src/city_events.cpp',
  '../src/placement_hint.cpp',
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
  ],
)

test(
  'plant_inspect',
  plant_inspect,
  env: [
    'SDL_AUDIODRIVER=dummy',
    'SDL_AUDIO_DRIVER=dummy',
  ],
  timeout: 120,
)

playtest_notes = executable(
  'playtest_notes',
  'playtest_notes.cpp',
  '../src/assets.cpp',
  '../src/city_glance.cpp',
  '../src/city_session.cpp',
  '../src/city_events.cpp',
  '../src/placement_hint.cpp',
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
  ],
)

test(
  'playtest_notes',
  playtest_notes,
  env: [
    'SDL_AUDIODRIVER=dummy',
    'SDL_AUDIO_DRIVER=dummy',
  ],
  timeout: 120,
)

engraved_minimap = executable(
  'engraved_minimap',
  'engraved_minimap.cpp',
  '../src/engraved_map.cpp',
  include_directories: include_directories('../src'),
  dependencies: [cairo_dep, pangocairo_dep, micropolis_dep],
)

test('engraved_minimap', engraved_minimap)

# November 18 review fixes: tax warning, crime average, blackouts, Bern,
# Year in Review after a load, evaluation after a load, New City funds.
review_nov18 = executable(
  'review_nov18',
  'review_nov18.cpp',
  '../src/assets.cpp',
  '../src/city_glance.cpp',
  '../src/city_session.cpp',
  '../src/city_events.cpp',
  '../src/placement_hint.cpp',
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
  'review_nov18',
  review_nov18,
  env: [
    'SDL_AUDIODRIVER=dummy',
    'SDL_AUDIO_DRIVER=dummy',
  ],
  timeout: 300,
)

# Second November 18 review (nov18b): default tax, Inspect notes, the
# Mayor's House key and hint, click phrases.
review_nov18b = executable(
  'review_nov18b',
  'review_nov18b.cpp',
  '../src/assets.cpp',
  '../src/city_session.cpp',
  '../src/city_events.cpp',
  '../src/placement_hint.cpp',
  '../src/messages.cpp',
  '../src/sound_player.cpp',
  '../src/special_buildings.cpp',
  '../src/tools.cpp',
  include_directories: include_directories('../src'),
  dependencies: [micropolis_dep, thread_dep, sdl3_dep],
  cpp_args: [
    '-DLUNDUKE_CITY_ASSET_DIR="' + lunduke_asset_dir + '"',
    '-DLUNDUKE_CITY_PACKAGE_VERSION="' + lunduke_package_version + '"',
  ],
)

test(
  'review_nov18b',
  review_nov18b,
  env: [
    'SDL_AUDIODRIVER=dummy',
    'SDL_AUDIO_DRIVER=dummy',
  ],
  timeout: 300,
)

# November 18 test pass: New City preview seed and display fixes.
testpass_nov18 = executable(
  'testpass_nov18',
  'testpass_nov18.cpp',
  include_directories: include_directories('../src'),
)

test('testpass_nov18', testpass_nov18)

newcity_preview = executable(
  'newcity_preview',
  'newcity_preview.cpp',
  '../src/assets.cpp',
  '../src/city_session.cpp',
  '../src/city_events.cpp',
  '../src/placement_hint.cpp',
  '../src/messages.cpp',
  '../src/sound_player.cpp',
  '../src/special_buildings.cpp',
  '../src/tools.cpp',
  include_directories: include_directories('../src'),
  dependencies: [micropolis_dep, thread_dep, sdl3_dep],
  cpp_args: [
    '-DLUNDUKE_CITY_ASSET_DIR="' + lunduke_asset_dir + '"',
    '-DLUNDUKE_CITY_PACKAGE_VERSION="' + lunduke_package_version + '"',
  ],
)

test(
  'newcity_preview',
  newcity_preview,
  env: [
    'SDL_AUDIODRIVER=dummy',
    'SDL_AUDIO_DRIVER=dummy',
  ],
)

# November 19 review: balance and playability fixes.
review_nov19 = executable(
  'review_nov19',
  'review_nov19.cpp',
  '../src/assets.cpp',
  '../src/city_session.cpp',
  '../src/city_events.cpp',
  '../src/placement_hint.cpp',
  '../src/messages.cpp',
  '../src/sound_player.cpp',
  '../src/special_buildings.cpp',
  '../src/tools.cpp',
  include_directories: include_directories('../src'),
  dependencies: [micropolis_dep, thread_dep, sdl3_dep],
  cpp_args: [
    '-DLUNDUKE_CITY_ASSET_DIR="' + lunduke_asset_dir + '"',
    '-DLUNDUKE_CITY_PACKAGE_VERSION="' + lunduke_package_version + '"',
  ],
)

test(
  'review_nov19',
  review_nov19,
  env: [
    'SDL_AUDIODRIVER=dummy',
    'SDL_AUDIO_DRIVER=dummy',
  ],
  timeout: 300,
)

# Terrain editor: brush, shore and forest edges, and painted new cities.
terrain_editor = executable(
  'terrain_editor',
  'terrain_editor.cpp',
  '../src/assets.cpp',
  '../src/city_session.cpp',
  '../src/city_events.cpp',
  '../src/placement_hint.cpp',
  '../src/messages.cpp',
  '../src/sound_player.cpp',
  '../src/special_buildings.cpp',
  '../src/terrain_paint.cpp',
  '../src/tools.cpp',
  include_directories: include_directories('../src'),
  dependencies: [micropolis_dep, thread_dep, sdl3_dep],
  cpp_args: [
    '-DLUNDUKE_CITY_ASSET_DIR="' + lunduke_asset_dir + '"',
    '-DLUNDUKE_CITY_PACKAGE_VERSION="' + lunduke_package_version + '"',
  ],
)

test(
  'terrain_editor',
  terrain_editor,
  env: [
    'SDL_AUDIODRIVER=dummy',
    'SDL_AUDIO_DRIVER=dummy',
  ],
)

city_events_test = executable(
  'city_events',
  'city_events.cpp',
  '../src/assets.cpp',
  '../src/city_session.cpp',
  '../src/city_events.cpp',
  '../src/placement_hint.cpp',
  '../src/messages.cpp',
  '../src/sound_player.cpp',
  '../src/special_buildings.cpp',
  '../src/terrain_paint.cpp',
  '../src/tools.cpp',
  include_directories: include_directories('../src'),
  dependencies: [micropolis_dep, thread_dep, sdl3_dep],
  cpp_args: [
    '-DLUNDUKE_CITY_ASSET_DIR="' + lunduke_asset_dir + '"',
    '-DLUNDUKE_CITY_PACKAGE_VERSION="' + lunduke_package_version + '"',
    '-DLUNDUKE_CITY_SOURCE_DIR="' + meson.project_source_root() + '"',
  ],
)

test(
  'city_events',
  city_events_test,
  args: [meson.current_build_dir()],
  env: [
    'SDL_AUDIODRIVER=dummy',
    'SDL_AUDIO_DRIVER=dummy',
  ],
)
