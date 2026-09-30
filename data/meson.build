# SPDX-License-Identifier: GPL-3.0-or-later

tm_resources = gnome.compile_resources(
  'tm-resources',
  'timemachine.gresource.xml',
  source_dir: meson.current_source_dir(),
  c_name: 'tm',
)

install_data('net.office42.timemachine.desktop',
  install_dir: get_option('datadir') / 'applications')
install_data('net.office42.timemachine.svg',
  install_dir: get_option('datadir') / 'icons/hicolor/scalable/apps')
