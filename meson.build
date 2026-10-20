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
cairomm_dep = dependency('cairomm-1.0')
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
