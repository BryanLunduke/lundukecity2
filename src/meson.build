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
  'shell_uitest.cpp',
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
  lunduke_city_link_args += ['-static-libgcc', '-static-libstdc++']
  lunduke_city_win_subsystem = 'windows'
endif

lunduke_city_exe = executable(
  'lunduke-city-2',
  lunduke_city_src,
  link_args: lunduke_city_link_args,
  win_subsystem: lunduke_city_win_subsystem,
  dependencies: [sdl3_dep, cairo_dep, pangocairo_dep, micropolis_dep, thread_dep],
  cpp_args: [
    '-DLUNDUKE_CITY_PACKAGE_VERSION="' + lunduke_package_version + '"',
  ],
  install: true,
)
