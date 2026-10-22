lunduke_city_src = files(
  'main.cpp',
  'assets.cpp',
  'chart.cpp',
  'city_glance.cpp',
  'city_session.cpp',
  'map_view.cpp',
  'messages.cpp',
  'overlay_map.cpp',
  'shell.cpp',
  'special_buildings.cpp',
  'shell_actions.cpp',
  'shell_draw.cpp',
  'shell_uitest.cpp',
  'scenario_preview.cpp',
  'side_widgets.cpp',
  'sound_player.cpp',
  'sprite_art.cpp',
  'tile_atlas.cpp',
  'tool_palette.cpp',
  'tools.cpp',
)

lunduke_city_link_args = []
lunduke_city_win_subsystem = 'console'
if host_machine.system() == 'windows'
  # Icon, UTF-8 code page / DPI manifest, and version info.
  windows = import('windows')
  lunduke_city_src += windows.compile_resources(
    '../data/windows/lunduke-city-2.rc',
    depend_files: files('../data/windows/lunduke-city-2.ico', '../data/windows/lunduke-city-2.manifest'),
  )
  # The bundled MSYS2 DLLs come from a newer GCC than the cross compiler;
  # keep this exe's own C++ runtime static so the two never mix.
  lunduke_city_link_args += ['-static-libgcc', '-static-libstdc++', '-lshell32']
  lunduke_city_win_subsystem = 'windows'
endif

# Pango uses fontconfig, but --as-needed will not link it unless this
# binary calls FcFini itself. Sniper and Windows may lack the .pc file;
# the leak suppressions in tests/lsan.supp still cover that case.
fontconfig_dep = dependency('fontconfig', required: false)
lunduke_city_deps = [sdl3_dep, cairo_dep, pangocairo_dep, micropolis_dep, thread_dep]
# Install prefix data directory. The macro is CITY_DATA_DIR, not a
# LUNDUKE_CITY_* string, and the value is the prefix rather than the
# source tree (a source path must not land in the release binary).
city_data_dir = get_option('prefix') / get_option('datadir') / 'lunduke-city-2' / 'micropolis-assets'
city_ui_dir = get_option('prefix') / get_option('datadir') / 'lunduke-city-2'
lunduke_city_cpp_args = [
  '-DLUNDUKE_CITY_PACKAGE_VERSION="' + lunduke_package_version + '"',
  '-DCITY_DATA_DIR="' + city_data_dir + '"',
  '-DCITY_UI_DIR="' + city_ui_dir + '"',
]
if fontconfig_dep.found()
  lunduke_city_deps += fontconfig_dep
  lunduke_city_cpp_args += '-DLUNDUKE_HAS_FONTCONFIG=1'
endif

# The review self-test (LUNDUKE_CITY2_REVIEW) is a developer check. Auto
# leaves it out of release builds so it is not in the shipped binary.
review_opt = get_option('review_checks')
lunduke_review_checks_on = review_opt.enabled() or (
  review_opt.auto() and get_option('buildtype') != 'release')
if lunduke_review_checks_on
  lunduke_city_src += files('shell_review.cpp')
  lunduke_city_cpp_args += '-DCITY_REVIEW_CHECKS=1'
endif

lunduke_city_exe = executable(
  'lunduke-city-2',
  lunduke_city_src,
  link_args: lunduke_city_link_args,
  win_subsystem: lunduke_city_win_subsystem,
  dependencies: lunduke_city_deps,
  cpp_args: lunduke_city_cpp_args,
  install: true,
)
