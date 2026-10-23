project('xplat-hello', 'cpp', default_options: ['cpp_std=c++17', 'warning_level=2'])
core = static_library('core', 'core/game.cpp')
executable('headless', 'linux/headless.cpp', link_with: core)
sdl3 = dependency('sdl3', required: get_option('sdl'))
if sdl3.found()
  executable('xplat-sdl', 'sdl/main.cpp', link_with: core, dependencies: sdl3)
endif
gtkmm = dependency('gtkmm-3.0', required: get_option('gtk'))
if gtkmm.found()
  executable('xplat-gtk', 'gtk/main.cpp', link_with: core, dependencies: gtkmm)
endif
