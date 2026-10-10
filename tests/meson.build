# SPDX-License-Identifier: Apache-2.0
test_inc = include_directories('../src')

foreach t : ['detect', 'power', 'leds', 'policy', 'image', 'render', 'display',
             'backlight']
  exe = executable('test_' + t, 'test_' + t + '.cpp',
    include_directories : test_inc,
    cpp_args : ['-DASSETS_SRC="' + meson.project_source_root() / 'assets' + '"'],
    link_with : core,
    dependencies : [gtest, png, m])
  test(t, exe, workdir : meson.current_build_dir(), protocol : 'gtest')
endforeach
