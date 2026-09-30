# Roadmap

What exists, and what comes next, roughly in order.

## Done

- Engine: values, a parser with Excel's precedence, on-demand
  recalculation with cycle detection, and over 120 functions.
- Random inputs: uniform, normal, lognormal, triangular, PERT, beta,
  gamma, exponential, Poisson, binomial, Bernoulli, Student's t, 90%
  ranges (normal and lognormal), discrete, bootstrap and GBM.
- Monte Carlo simulation: reproducible seeds, and `SIM.MEAN`, `SIM.STDEV`,
  `SIM.SE`, `SIM.PERCENTILE`, `SIM.PROB`, `SIM.TAILMEAN`, `SIM.CORREL` and
  the rest.
- Forecasting: linear regression with prediction intervals, exponential
  growth, and Holt–Winters exponential smoothing with automatic season
  detection and intervals. `RAND.ETS` and `RAND.LINEAR` bring a forecast's
  error into the simulation.
- Judgment: `BAYES`, `EXTREMIZE`, `BRIER`, `LOGSCORE`.
- Window: grid, formula bar, fill, copy and paste, column widths,
  histogram and fan chart, `.tm` and CSV files, sample export, and
  headless screenshots.
- Undo and redo, grouped by operation.
- Number formats (Excel's codes, with sections), a Format menu and
  toolbar buttons.
- A tornado chart of what drives the selected cell.
- Per-cell random streams: editing one cell does not change the others'
  draws, and runs with the same seed share their futures (common random
  numbers).
- More judgment: `LAPLACE`, `POOL.ODDS`, `REFCLASS`, and `RAND.METALOG`
  from three percentiles.
- A prompt to save changes on closing.
- Latin hypercube sampling, with an inverse distribution function for
  every distribution; `BETA.INV`, `GAMMA.INV`, `T.INV` and their kin as
  spreadsheet functions.
- In-cell editing, and point mode: clicking cells while typing a formula
  inserts their references.
- Sample paths drawn through the fan chart.
- Terminal front-end and smoke test; CI on Linux.

## Next

- **Correlated inputs**, through the Iman–Conover rank reordering.
- **More forecasting**: damped trend (the most robust automatic method),
  simple exponential smoothing, the naive, seasonal naive and drift
  benchmarks, AR(1) mean reversion, logistic and Bass diffusion curves,
  and block bootstrap of a series.
- **More judgment**: Beta-binomial base rates, Gott's rule, and
  calibration tables.
- **Convergence**: stopping automatically once the chosen statistics are
  known to a tolerance.
- Named ranges; more than one sheet.
- macOS and Windows builds.
