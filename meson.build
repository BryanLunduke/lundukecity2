project(
  'lunduke-city-2',
  'cpp',
  version: '2.0.0',
  default_options: ['cpp_std=c++17', 'warning_level=2'],
  meson_version: '>= 0.60',
)

# Package version shown in About and the desktop package. Side by side with
# the original Lunduke City package.
lunduke_package_version = '2.0.0'

sdl3_dep = dependency('sdl3', version: '>= 3.2.0')
# Plain cairo. The Steam Linux Runtime 3.0 (sniper) ships cairo and pango
# but not cairomm or sigc++.
cairo_dep = dependency('cairo')
pangocairo_dep = dependency('pangocairo')
thread_dep = dependency('threads')
lunduke_asset_dir = meson.project_source_root() / 'third_party' / 'micropolis-assets'

subdir('third_party/micropolis')
subdir('src')
subdir('tests')

install_data(
  'data/lunduke-city-2.desktop',
  install_dir: get_option('datadir') / 'applications',
)
install_data(
  'data/com.lunduke.LundukeCity2.metainfo.xml',
  install_dir: get_option('datadir') / 'metainfo',
)
appstreamcli = find_program('appstreamcli', required: false)
if appstreamcli.found()
  test(
    'metainfo',
    appstreamcli,
    args: ['validate', '--no-net', files('data/com.lunduke.LundukeCity2.metainfo.xml')],
  )
endif
install_data(
  'data/lunduke-city-2.xml',
  install_dir: get_option('datadir') / 'mime' / 'packages',
)
install_data(
  'data/lunduke-city.svg',
  rename: 'lunduke-city-2.svg',
  install_dir: get_option('datadir') / 'icons' / 'hicolor' / 'scalable' / 'apps',
)
foreach size : ['16', '32', '48', '64', '128', '256']
  install_data(
    'data/icons/hicolor/' + size + 'x' + size + '/apps/lunduke-city.png',
    rename: 'lunduke-city-2.png',
    install_dir: get_option('datadir') / 'icons' / 'hicolor' / (size + 'x' + size) / 'apps',
  )
endforeach
install_subdir(
  'third_party/micropolis-assets',
  install_dir: get_option('datadir') / 'lunduke-city-2',
)
install_data(
  'NOTICE',
  'COPYING',
  install_dir: get_option('datadir') / 'doc' / 'lunduke-city-2',
)

# Every test looks up tiles.png. The default setup points at the source
# tree so a build directory not named "build" still finds the assets.
# LeakSanitizer runs also load tests/lsan.supp (fontconfig / pango / Mesa).
lunduke_test_env = ['LUNDUKE_CITY_ASSETS=' + lunduke_asset_dir]
lunduke_sanitize = get_option('b_sanitize')
if lunduke_sanitize.contains('address') or lunduke_sanitize.contains('leak')
  lunduke_test_env += ['LSAN_OPTIONS=suppressions=' + meson.project_source_root() / 'tests' / 'lsan.supp']
endif
add_test_setup('default', env: lunduke_test_env, is_default: true)
