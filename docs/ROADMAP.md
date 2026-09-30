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
- Terminal front-end and smoke test; CI on Linux.

## Next

- **Undo and redo.**
- **Number formats** (`0.0%`, `#,##0`) so that draws are not shown to ten
  decimal places.
- **Tornado chart** in the forecast panel: each input's rank correlation
  with the selected output, as bars.
- **Per-cell random streams**, so that editing one input does not change
  every other input's draws. This gives *common random numbers*: two
  versions of a model can then be compared futures-for-futures.
- **Latin hypercube sampling**, for smaller simulation error at the same
  number of futures.
- **Correlated inputs**, through the Iman–Conover rank reordering.
- **More forecasting**: damped trend (the most robust automatic method),
  simple exponential smoothing, the naive, seasonal naive and drift
  benchmarks, AR(1) mean reversion, logistic and Bass diffusion curves,
  and block bootstrap of a series.
- **More judgment**: Laplace's rule of succession, Beta-binomial base
  rates, reference-class uplift, geometric mean of odds, Gott's rule,
  metalog distributions from three quantiles, and calibration tables.
- **Convergence**: stopping automatically once the chosen statistics are
  known to a tolerance.
- **In-cell editing and point mode** (clicking cells while typing a
  formula inserts their references).
- Named ranges; more than one sheet.
- macOS and Windows builds.
