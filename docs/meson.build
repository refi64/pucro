# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

mrkd = find_program('mrkd', required : get_option('man'))
gzip = find_program('gzip', required : get_option('man'))

if mrkd.found() and gzip.found()
  foreach page : ['pucro.conf.5', 'pucrod.service.8']
    md = page + '.md'
    man_section = page.split('.')[-1]
    roff_target = custom_target(page,
        command : [mrkd, '@INPUT@', '@OUTPUT@'],
        input : [md], output : [page])

    gz = page + '.gz'
    custom_target(gz,
      input : ['../build-aux/compress.sh', roff_target],
      output : gz,
      command : ['@INPUT0@', gzip, '@INPUT1@', '@OUTPUT@'],
      install : true,
      install_dir : get_option('datadir') / 'man' / ('man' + man_section))

    html = page + '.html'
    custom_target(html,
        command : [mrkd, '-index', '@INPUT1@', '-f', 'html', '@INPUT0@', '@OUTPUT@'],
        input : [md, 'index.ini'], output : [html],
        build_by_default : true)
  endforeach
endif
