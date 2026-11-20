# Developer tool: rebuilds data/scenarios/seattle.cty (not installed).
#   meson compile -C build seattle_map
#   build/tools/seattle_map data/scenarios/seattle.cty
seattle_map = executable(
  'seattle_map',
  'seattle_map.cpp',
  '../src/terrain_paint.cpp',
  include_directories: include_directories('../src'),
  dependencies: [micropolis_dep],
  build_by_default: false,
  install: false,
)
