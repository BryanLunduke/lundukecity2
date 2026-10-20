lunduke_city_src = files(
  'main.cpp',
  'assets.cpp',
  'chart.cpp',
  'city_session.cpp',
  'map_view.cpp',
  'messages.cpp',
  'overlay_map.cpp',
  'shell.cpp',
  'shell_actions.cpp',
  'shell_draw.cpp',
  'side_widgets.cpp',
  'sound_player.cpp',
  'sprite_art.cpp',
  'tile_atlas.cpp',
  'tool_palette.cpp',
  'tools.cpp',
)

lunduke_city_exe = executable(
  'lunduke-city-2',
  lunduke_city_src,
  dependencies: [sdl3_dep, cairomm_dep, pangocairo_dep, micropolis_dep, thread_dep],
  cpp_args: [
    '-DLUNDUKE_CITY_PACKAGE_VERSION="' + lunduke_package_version + '"',
  ],
  install: true,
)
