# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

project('pucro', 'c', version : '0.1')

global_systemd_dep = dependency('systemd', required : true)

deps = [
  dependency('libconfuse', required : true),
  dependency('libevdev', required : true),
  dependency('libinput', required : true),
  dependency('libsystemd', required : true),
  dependency('libudev', required : true),
]

add_project_arguments('-D_GNU_SOURCE', language : 'c')
add_project_arguments('-DSYSCONFDIR="/@0@"'.format(get_option('sysconfdir')),
                      language : 'c')

global_conf_data = configuration_data()
global_conf_data.set('version', meson.project_version())
global_conf_data.set('prefix', get_option('prefix'))
global_conf_data.set('libexecdir', get_option('libexecdir'))

executable('pucrod', [
    'src/config.c',
    'src/dispatch.c',
    'src/input.c',
    'src/pucro.c',
    'src/seat.c',
    'src/utils.c',
  ],
  dependencies : deps,
  install : true,
  install_dir : get_option('libexecdir') / 'pucro')

subdir('data')

if not get_option('man').disabled()
  subdir('docs')
endif

if get_option('selinux')
  subdir('selinux')
endif
