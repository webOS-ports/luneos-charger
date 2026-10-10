# SPDX-License-Identifier: Apache-2.0
project('luneos-charger', 'c',
  version : '2.0.0',
  license : 'Apache-2.0',
  meson_version : '>= 0.60',
  default_options : ['c_std=gnu11', 'warning_level=3', 'buildtype=release'])

cc = meson.get_compiler('c')
add_project_arguments(cc.get_supported_arguments([
  '-Wshadow', '-Wstrict-prototypes', '-Wmissing-prototypes', '-Wformat=2',
  '-Wconversion', '-Wno-sign-conversion', '-Wpointer-arith', '-Wvla',
  '-Wcast-align', '-Wwrite-strings', '-Wundef',
]), language : 'c')

asset_dir = get_option('prefix') / get_option('datadir') / 'luneos-charger'

png = dependency('libpng', version : '>= 1.6')
m = cc.find_library('m', required : false)

core_src = files(
  'src/backlight.c', 'src/detect.c', 'src/display.c', 'src/display_drm.c',
  'src/display_fbdev.c', 'src/image.c', 'src/input.c', 'src/leds.c',
  'src/policy.c', 'src/power.c', 'src/render.c', 'src/util.c',
)

core = static_library('charger-core', core_src,
  dependencies : [png, m])

executable('luneos-charger', 'src/main.c',
  c_args : ['-DASSET_DIR="' + asset_dir + '"'],
  link_with : core,
  dependencies : [png, m],
  install : true,
  install_dir : get_option('sbindir'))

# Renders the screen to a PNG for any panel size; not installed.
executable('luneos-charger-preview', 'tools/preview.c',
  c_args : ['-DASSETS_SRC="' + meson.project_source_root() / 'assets' + '"'],
  include_directories : include_directories('src'),
  link_with : core,
  dependencies : [png, m],
  install : false)

install_data('assets/logo.png', 'assets/text.png', 'assets/text.idx',
  install_dir : asset_dir)

if get_option('tests').allowed()
  add_languages('cpp', required : get_option('tests'))
  gtest = dependency('gtest', main : true, required : get_option('tests'))
  if gtest.found()
    subdir('tests')
  endif
endif
