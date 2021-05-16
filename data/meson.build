# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

install_data('pucro.conf', install_dir : get_option('sysconfdir'))

pucrod_service = configure_file(input : 'pucrod.service.in', output : 'pucrod.service',
                               configuration : global_conf_data)

systemd_system_unit_dir = global_systemd_dep.get_pkgconfig_variable(
    'systemd_system_unit_dir')
install_data(pucrod_service, install_dir : systemd_system_unit_dir)
