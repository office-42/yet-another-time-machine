Time Machine ("timemachine", one word, is the binary) is a spreadsheet for
making predictions about the future: Monte Carlo simulation, exponential
smoothing and calibrated judgment in the shape of Excel.  It is built on the
same principles and stack as office42-spreadsheet: a small, honest C codebase
on GTK 4, Pango and Cairo, built with Meson.  docs/METHODS.md explains the
forecasting methods, docs/ARCHITECTURE.md the layers, and docs/ROADMAP.md
lists what is left.

Instructions for AI agents:
- Do not add any unit tests.
- Exercise changes through the running program (`--screenshot` renders the
  window headlessly) or the timemachine-calc terminal front-end, and say in
  the commit message what you did to check.
- Run `sh build-aux/smoke-test.sh builddir/src/timemachine-calc` before
  pushing; build with `meson setup builddir --werror`, as CI does.
- The engine (src/util, src/formula, src/model, src/io) must never include
  GTK.
- A simulation must be reproducible: the same seed gives the same futures.
