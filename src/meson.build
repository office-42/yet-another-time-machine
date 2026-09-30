# SPDX-License-Identifier: GPL-3.0-or-later

tm_inc = include_directories('util', 'formula', 'model', 'io', 'ui')

# Everything below the user interface: values, the random generator,
# formulas, the sheet, the simulator and the file formats.  Its own
# library, linking without GTK, so that nothing graphical can reach in.
tm_core_sources = files(
  'util/tm-types.c',
  'util/tm-rng.c',
  'util/tm-value.c',
  'util/tm-stats.c',
  'util/tm-numfmt.c',
  'formula/tm-formula.c',
  'formula/tm-eval.c',
  'formula/tm-fn-core.c',
  'formula/tm-fn-stats.c',
  'formula/tm-fn-random.c',
  'formula/tm-fn-forecast.c',
  'formula/tm-fn-sim.c',
  'formula/tm-fn-domains.c',
  'model/tm-sim.c',
  'model/tm-sheet.c',
  'io/tm-file.c',
)

tm_core = static_library('tmcore',
  tm_core_sources,
  dependencies: core_deps,
  include_directories: tm_inc,
  install: false,
)

tm_core_dep = declare_dependency(
  link_with: tm_core,
  include_directories: tm_inc,
  dependencies: core_deps,
)

# The engine driven from a terminal: how it is checked without a window,
# and how the smoke test checks it on a machine with no display.
executable('timemachine-calc',
  'calc-main.c',
  dependencies: [tm_core_dep],
  install: true,
)

tm_ui_sources = files(
  'ui/tm-grid.c',
  'ui/tm-chart.c',
  'ui/tm-window.c',
  'ui/tm-application.c',
  'main.c',
)

executable('timemachine',
  tm_ui_sources,
  tm_resources,
  dependencies: [tm_core_dep] + app_deps,
  include_directories: tm_inc,
  install: true,
  win_subsystem: 'windows',
)
