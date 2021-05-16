# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

make = find_program('make', required : true)
bzip2 = find_program('bzip2', required : true)
python3 = import('python').find_installation('python3', required : true)

selinux_makefile = get_option('selinux_makefile')
if selinux_makefile == 'auto'
  selinux_makefile = get_option('prefix') / get_option('datadir') \
                        / 'selinux' / 'devel' / 'Makefile'
endif

pucro_fc = configure_file(input : 'pucro.fc.in', output : 'pucro.fc',
                          configuration : global_conf_data)
pucro_te = configure_file(input : 'pucro.te.in', output : 'pucro.te',
                          configuration : global_conf_data)

pucro_pp = custom_target('pucro.pp',
  input : ['build_policy.py', pucro_te, pucro_fc],
  output : 'pucro.pp',
  command : [python3.path(), '@INPUT0@',
      '--make', make,
      '--makefile', selinux_makefile,
      '--te', '@INPUT1@',
      '--fc', '@INPUT2@',
      '--pp', '@OUTPUT@'])

custom_target('pucro.pp.bz2',
  input : ['../build-aux/compress.sh', pucro_pp],
  output : 'pucro.pp.bz2',
  command : ['@INPUT0@', bzip2, '@INPUT1@', '@OUTPUT@'],
  install : true,
  install_dir : get_option('datadir') / 'selinux' / 'packages')
