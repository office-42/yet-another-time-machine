# Roadmap

What exists, and what comes next, roughly in order.

## Done

- Engine: values, a parser with Excel's precedence, recalculation with
  cycle detection, and 249 functions.
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
- Methods for particular futures:
  - weather: Markov chains, AR(1), `BRIER.SKILL`;
  - sport: Poisson match models with Dixon–Coles, expected goals from
    results, Elo;
  - markets: GBM fitted to prices, `BLACKSCHOLES`, `DRAWDOWN`;
  - general: the naive, seasonal-naive and drift benchmarks, damped
    trend, and logistic, Gompertz and Bass curves.

  Examples for weather, football and stock prices come with them.
- Cells that cannot be random are frozen during a simulation.
- Terminal front-end and smoke test; CI on Linux.
- Pictures and maps as data: PNG and JPEG pictures, located or not, read
  by position, place or colour legend, measured, and compared frame to
  frame; GeoJSON maps with regions, areas and distances; interpolation
  between stations (inverse distance, ordinary kriging with its error);
  a stochastic spread over a grid. The forecast panel draws maps, tracks,
  clouds of positions and heat maps. Examples: radar nowcast, wildfire,
  hurricane track, rain gauges, crop canopy.
- Learning from a table of past cases: nearest analogues, multiple,
  logistic, Poisson and quantile regression, conformal intervals.
- Updating: beta-binomial and gamma-Poisson draws, partial pooling,
  Student-t predictive draws, kernel density estimates, empirical
  distributions, generalised Pareto tails.
- Lifetimes: Weibull fits with censoring, Kaplan–Meier, conditional
  lifetimes, Gott's rule; queues (Erlang C).
- Correlated inputs through a Gaussian copula, and forecast errors shared
  across a series' months.
- Scoring (CRPS, PIT, interval and quantile scores, MASE, recalibration,
  Cooke's calibration) and decisions (chance each option is best, EVPI,
  EVPPI, certainty equivalents, Sobol indices).
- Text outcomes in simulations: `SIM.PROB` of a word, `SIM.MODE`, bar
  charts.
- Evaluation in dependency order, so long chains neither overflow the
  stack nor read as cycles; a sample budget that keeps the outputs.
- Cut and paste that moves references; numbers typed with currency signs
  and thousands separators.

## Next

- **Correlated inputs by rank**: Iman–Conover reordering, which keeps
  Latin hypercube strata.
- **More forecasting**: fitting logistic and Bass curves to data by
  nonlinear least squares, block bootstrap of a series, multiplicative
  Holt–Winters with model selection, ARIMA, Gaussian processes.
- **Joint spatial draws**: conditional simulation, so that kriged fields
  are consistent from place to place.
- **More domains**: ranking models for elections (polls to vote shares),
  agent-based spread on networks.
- **Stacking and pooling of models**: weights from past scores.
- **Convergence**: stopping automatically once the chosen statistics are
  known to a tolerance.
- Named ranges, lookups (`VLOOKUP`, `MATCH`), dates; inserting rows and
  columns; more than one sheet.
- Running the simulation off the main thread.
- macOS and Windows builds.
