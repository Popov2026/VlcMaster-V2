# HomeRun Player — version Linux (GTK 4 + libVLC 3).
#   meson setup build && meson compile -C build && meson test -C build
project('homerun-player', 'c',
  version : '3.7',
  meson_version : '>= 0.60',
  default_options : ['c_std=gnu11', 'warning_level=2', 'buildtype=release'])

app_version = get_option('app_version')
if app_version == ''
  app_version = meson.project_version()
endif
add_project_arguments('-DAPP_VERSION="@0@"'.format(app_version), language : 'c')

cc = meson.get_compiler('c')
add_project_arguments(cc.get_supported_arguments(['-Wno-missing-field-initializers']), language : 'c')

gtk  = dependency('gtk4', version : '>= 4.14')
vlc  = dependency('libvlc', version : '>= 3.0')
curl = dependency('libcurl')

# logique sans interface (playlist, guide, JSON…), testée par tests/test_core.c
core = static_library('hrcore', 'src/core.c', 'src/json.c')

executable('homerun-player',
  'src/main.c', 'src/video.c', 'src/net.c', 'src/config.c', 'src/i18n.c', 'src/ui.c',
  'src/tracks.c', 'src/guide_ui.c', 'src/rec.c', 'src/tuner_ui.c', 'src/settings_ui.c', 'src/update.c',
  link_with : core,
  dependencies : [gtk, vlc, curl],
  install : true)

test_core = executable('test_core', 'tests/test_core.c',
  link_with : core,
  include_directories : include_directories('src'))
test('core', test_core)

datadir = get_option('datadir')
install_data('data/fr.popov.HomeRunPlayer.desktop', install_dir : datadir / 'applications')
install_data('data/fr.popov.HomeRunPlayer.svg', install_dir : datadir / 'icons' / 'hicolor' / 'scalable' / 'apps')
install_data('data/fr.popov.HomeRunPlayer.metainfo.xml', install_dir : datadir / 'metainfo')
