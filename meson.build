# timemachine - a spreadsheet for making predictions about the future
# Copyright (C) 2026 The timemachine authors
# SPDX-License-Identifier: GPL-3.0-or-later

project('timemachine', 'c',
  version: '0.1.0-dev',
  license: 'GPL-3.0-or-later',
  meson_version: '>= 1.3',
  default_options: [
    'c_std=gnu11,c11',
    'warning_level=2',
    'buildtype=debugoptimized',
  ],
)

cc = meson.get_compiler('c')
gnome = import('gnome')

add_project_arguments(
  '-DTM_VERSION="' + meson.project_version() + '"',
  '-DG_LOG_DOMAIN="timemachine"',
  '-DTM_PREFIX="' + get_option('prefix') + '"',
  '-DTM_SAMPLES_DIR="' + get_option('prefix') / get_option('datadir') / 'timemachine' / 'samples' + '"',
  '-DTM_SOURCE_SAMPLES_DIR="' + meson.project_source_root() / 'samples' + '"',
  language: 'c',
)

extra_warnings = [
  '-Wmissing-prototypes',
  '-Wstrict-prototypes',
  '-Wold-style-definition',
  '-Wpointer-arith',
  '-Wshadow',
  '-Wwrite-strings',
  '-Wformat=2',
  '-Wundef',
  '-Wvla',
  '-Winit-self',
  '-Wno-unused-parameter',
]
add_project_arguments(cc.get_supported_arguments(extra_warnings), language: 'c')

glib_dep       = dependency('glib-2.0',    version: '>= 2.76')
gobject_dep    = dependency('gobject-2.0', version: '>= 2.76')
gio_dep        = dependency('gio-2.0',     version: '>= 2.76')
cairo_dep      = dependency('cairo',       version: '>= 1.16')
pango_dep      = dependency('pango',       version: '>= 1.50')
pangocairo_dep = dependency('pangocairo',  version: '>= 1.50')
gtk_dep        = dependency('gtk4',        version: '>= 4.10')
m_dep          = cc.find_library('m', required: false)

# The engine -- values, formulas, the sheet and the simulator -- needs
# GLib and the maths library and nothing else.  It does not need GTK, and
# must not: the terminal front-end and the smoke test run it with no
# display at all.
core_deps = [glib_dep, gobject_dep, gio_dep, m_dep]
app_deps  = core_deps + [cairo_dep, pango_dep, pangocairo_dep, gtk_dep]

subdir('data')
subdir('src')

# The example models, for File > Open Example.
install_subdir('samples', install_dir: get_option('datadir') / 'timemachine')

summary({
    'prefix' : get_option('prefix'),
    'GTK'    : gtk_dep.version(),
    'Pango'  : pango_dep.version(),
  }, section: 'timemachine ' + meson.project_version())
