# How the time machine predicts

Nobody can see the future, but some ways of guessing at it do much better
than others. This page covers the ones Time Machine is built on: what each
is for, how to use it here, and the traps in each. The formulas and
sources behind them are in [RESEARCH.md](RESEARCH.md).

The rule under all of it: **a forecast is a distribution, not a number.**
"Profit next year will be 190,000" is almost certainly wrong. "Profit
will be between −150,000 and 740,000 nine times in ten, with a 28% chance
of a loss" can be right, can be checked, and is far more use for a
decision.

## 1. Monte Carlo simulation

**What it is.** Replace each uncertain input with a distribution. Draw a
value from every one, work the model out, and write down the result. Do
that ten thousand times. The results trace out the distribution of every
output: its likely range, its tails, and the chance it crosses a line you
care about.

**How to use it here.**

1. Put a `RAND.*` function in each uncertain input cell. These cells are
   tinted lavender, so you can tell what is guessed from what is known.
2. Build the rest of the model as usual, with ordinary formulas.
3. Press **F5**. Select any tinted cell to see its histogram, or a row of
   them to see a fan chart.
4. To bring results back into the sheet, use `SIM.MEAN`, `SIM.PERCENTILE`,
   `SIM.PROB`, `SIM.TAILMEAN` and `SIM.CORREL`.

| To say… | Write |
|---|---|
| "between 80k and 300k, 90% sure; can't go below zero" | `=RAND.LOGCI(80000,300000)` |
| "between 10 and 30, 90% sure; symmetric" | `=RAND.CI(10,30)` |
| "at least 5%, most likely 12%, at most 25%" | `=RAND.PERT(5%,12%,25%)` |
| "somewhere from 39 to 55, likeliest 49" | `=RAND.TRIANGULAR(39,49,55)` |
| "a 30% chance it happens" | `=RAND.BERNOULLI(30%)` (1 or 0) |
| "about 4 a month, at random" | `=RAND.POISSON(4)` |
| "one of these, this likely" | `=RAND.DISCRETE(A1:A3,B1:B3)` |
| "like one of the months we've had" | `=RAND.BOOTSTRAP(B4:B39)` |
| "P10 is 10, P50 is 20, P90 is 60" (skewed) | `=RAND.METALOG(10,20,60)` |

**Comparing two versions of a model.** Each uncertain cell draws from a
stream of its own, fixed by the seed and the cell's position. So running
a model, changing a decision, and running it again with the same seed
puts both versions through the *same* futures (common random numbers).
The difference between them is then the effect of the decision, not the
luck of the draw.

**How many futures?** The error in a simulated mean shrinks as one over
the square root of the number of futures. Four times as many halves it.
`SIM.SE` reports that error. With 10,000 futures, a probability of 1% is
known to about ±0.1%, but the extreme tails need many more. The seed
makes a run repeatable: the same seed gives the same futures.

**Traps.**
- *The flaw of averages.* A plan built from average inputs is not the
  average outcome whenever the model has a maximum, a minimum, a capacity
  or a threshold in it. The launch example's row 22 and the project
  example's merge bias both show this.
- *Ranges that are too narrow.* People asked for a 90% range give one
  that holds the truth half the time or less. Widen yours, and check
  them against outcomes (section 5).
- *Precision is not accuracy.* A million futures of a wrong model are
  precisely wrong.
- *Hidden dependence.* The simulator draws each input independently. If
  two costs rise and fall together, model the common cause explicitly: one
  cell that both depend on.

## 2. Estimates from people

The inputs to most models of the future are judgments. Some ways of
writing a judgment down are better than others.

- **A 90% range** (`RAND.CI`, `RAND.LOGCI`). "I'm 90% sure it's between
  lo and hi" is easier to give honestly than a single number, and it is
  easy to check later. Use the lognormal form for anything that cannot go
  below zero and could turn out much larger than expected: sizes,
  durations, prices, costs.
- **Three points** (`RAND.PERT`, `RAND.TRIANGULAR`): minimum, most likely,
  maximum. PERT's mean, (min + 4·likely + max)/6, leans on the most likely
  value less than a triangle's mean does. Project planners have used it
  since the 1950s.
- **Three percentiles** (`RAND.METALOG(p10, p50, p90)`): Keelin's metalog
  distribution. It fits the three quantiles exactly and can lean either
  way, with no hard minimum or maximum, which experts are poor at giving
  anyway. A triple that no distribution fits gives `#NUM!`.
- **The outside view first.** Before you estimate how long *this* project
  will take, look at how long projects *like* it took (a reference class).
  Start from that base rate and adjust for what is special about this one.
  `REFCLASS(ratios, estimate, 80%)` does Flyvbjerg's version: it scales
  your estimate by the ratio of actual to estimated that 80% of past
  projects stayed within.
- **Small numbers** (`LAPLACE(successes, trials)`). After 0 failures in 10
  launches, the chance of failure next time is not 0. Laplace's rule of
  succession says 1/12.

## 3. Extrapolating from history

When there is a history and the future is likely to resemble it:

- **Exponential smoothing** (`FORECAST.ETS`). Weighs recent observations
  more than old ones, tracks a level, a trend and, if there is one, a
  season. Its seasonality argument works like this: left out or 1 finds
  the season length by itself, 0 turns seasonality off, and 12 means
  monthly data with a yearly pattern.
  `FORECAST.ETS.CONFINT` gives the half-width of the 95% prediction
  interval, and `FORECAST.ETS.SEASONALITY` gives the season it found. This
  is Holt–Winters' additive method; the smoothing parameters are fitted by
  least squares on one-step errors.
- **Regression** (`FORECAST.LINEAR`, `TREND`, `SLOPE`, `RSQ`,
  `FORECAST.LINEAR.CONFINT`). A straight line through the history. Good
  for a steady trend, and dangerous far past the data.
- **Exponential growth** (`GROWTH`, `CAGR`). Every S-curve looks
  exponential early on, so be wary of extrapolating early growth.
- **Forecast plus its error** (`RAND.ETS`, `RAND.LINEAR`). The statistical
  forecast plus a random draw from its prediction error. Use them in a
  simulation so that the uncertainty of the extrapolation counts alongside
  everything else.

## 4. Random processes

For quantities that wander over time, lay the periods out across columns
and let each column depend on the one before:

- **Random walk** (for example, savings plus a normal shock each year):
  `=B11*(1+C10)+$B$4`, with `C10` an uncertain return.
- **Geometric Brownian motion**, the random walk of prices, where changes
  are proportional: `=EXP(RAND.NORMAL(mu-sigma^2/2, sigma))-1` as a
  period's return, or `RAND.GBM(start, drift, volatility, time)` for a
  single draw at any time ahead.
- **Bootstrap**: draw each period's change from the changes in the
  history, with `RAND.BOOTSTRAP`. No distribution has to be assumed, but
  the future can never be wilder than the past was.

Select the whole row after simulating to see it as a fan chart.

## 5. Judgment, updating and keeping score

The Good Judgment Project found that ordinary people could forecast world
events better than intelligence analysts. They did it by breaking
questions down, starting from base rates, updating often in small steps,
and keeping score.

- **Bayes' rule** (`BAYES(prior, p_if_true, p_if_false)`). How much a
  piece of evidence should move a probability. A bad status report that
  turns up 80% of the time when a project is slipping and 20% of the time
  when it isn't moves a 30% chance of slipping to 63%.
- **Pooling** (`POOL.ODDS(probabilities, [weights])`). Combine
  forecasters by the geometric mean of their odds, not the average of
  their probabilities. Then a confident forecaster's 2% counts for what it
  says instead of being averaged away.
- **Extremizing** (`EXTREMIZE(p, a)`). The average of several
  forecasters' probabilities is too timid, because each of them knows
  only part of what the group knows. Pushing it away from 50% (with *a*
  around 2 to 2.5) scored better in the Good Judgment tournaments.
- **Brier score** (`BRIER`). The mean squared error of probability
  forecasts: 0 is perfect, 0.25 is what always saying 50% scores.
  **Log score** (`LOGSCORE`) punishes confident misses much harder. Keep
  a column of your forecasts and a column of what happened, and score
  them.

## 6. Reading the results

- **Median and the 90% range** (P5 to P95) are the honest summary of a
  forecast. The mean is pulled by the tails.
- **P(< 0)** in the forecast panel is the chance the cell ends up
  negative: a loss, a shortfall, a deficit.
- **Expected shortfall** (`SIM.TAILMEAN(cell, 5%)`) tells you how bad the
  worst 5% of futures are on average, not just where they start.
- **What drives the result.** The forecast panel's tornado chart ranks
  the inputs by the rank correlation between each of them and the selected
  cell across the futures. `SIM.CORREL(input, output)` puts the same
  figure in a cell. The inputs with the largest values are the ones worth
  measuring better. Values smaller than about ±0.02 are noise.
- **Stale results.** The panel says so when the sheet has changed since
  the last simulation. Press F5 again.
