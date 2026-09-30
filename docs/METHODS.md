# How the time machine predicts

Nobody can see the future, but some ways of guessing at it do much better
than others. This page covers the ones Time Machine is built on: what each
is for, how to use it here, and the traps in each. The formulas and
sources behind them are in [RESEARCH.md](RESEARCH.md),
[GENERAL-RESEARCH.md](GENERAL-RESEARCH.md) and
[SPATIAL-RESEARCH.md](SPATIAL-RESEARCH.md).

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

**Latin hypercube sampling** (the toolbar's sampling menu) gets more
from each future. It splits every input's range into as many equally
likely slices as there are futures and draws from each slice exactly
once, pairing the slices of different inputs at random. The futures then
cover every input evenly instead of by luck. With 1,000 futures, a
normal input's simulated mean lands within a few hundredths of the truth,
where plain Monte Carlo would be off by about half a unit. `SIM.SE`
assumes plain Monte Carlo, so under Latin hypercube it overstates the
error. @RISK samples this way by default.

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
  everything else. The months of one future hang together, as real ones
  do: every `RAND.ETS` cell on the same series shares that future's
  innovations, step h's error being its own innovation plus each earlier
  one carried forward by the smoothing (Hyndman and Athanasopoulos'
  coefficients α + αβ·j + γ at the season's lags). A year's total is then
  as uncertain as the model says — three times wider, in the sales
  example, than months drawn on their own. `RAND.LINEAR` shares the
  line's own uncertainty (its height, its slope and the scatter's scale)
  across the cells drawing from it, and draws only the scatter about the
  line afresh for each.
- **More benchmarks.** `FORECAST.THETA` is the Theta method, winner of the
  M3 competition — simple exponential smoothing plus half the series'
  straight-line trend, as Hyndman and Billah showed it to be. `CROSTON`
  forecasts intermittent demand (spare parts, rare incidents) with
  Syntetos and Boylan's correction. `CHANGEPOINT` finds where the series'
  current level began (binary segmentation with a penalty of 3 ln n
  noise variances), so that a forecast can use the present regime only.
- **The method of analogues** (`FORECAST.ANALOG`, `RAND.ANALOG`): find the
  stretches of history most like the latest few values, and see how the
  series moved in the steps after each. Lorenz's idea for the weather; it
  needs no model at all.

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

## 6. Weather

Weather forecasters use two ideas that work far beyond weather.

- **Persistence as a chain.** Whether it rains tomorrow depends mostly on
  whether it rains today. A *Markov chain* says exactly that and no more:
  a table of the chance of each state tomorrow given each state today.
  - `MARKOV.ESTIMATE(from, to, history, [prior])` counts it from a column
    of past states; the prior adds pseudo-counts so that a transition
    never seen is not treated as impossible.
  - `RAND.MARKOV(today, states, matrix)` draws tomorrow.
  - `MARKOV.PROB(from, to, states, matrix, days)` is the exact chance
    that many days on.
  - `MARKOV.STEADY` is the long-run share, which the forecast decays
    towards: the forecast approaches *climatology*.

  The same chain serves for customers moving between plans, machines
  between working and broken, or economies between growth and recession.
- **Anomalies revert.** Temperature is the seasonal normal plus an
  anomaly, and a warm spell fades day by day.
  `RAND.AR1(yesterday, 0, phi, sigma)` carries a fraction *phi* of
  yesterday's anomaly over and adds a fresh shock.
  `FORECAST.AR1(steps, history)` fits *phi* and gives the fading
  forecast; `FORECAST.AR1.CONFINT` gives its widening interval.
- **Skill, not score.** A 20% chance of rain in a dry climate is easy to
  get right. `BRIER.SKILL(probabilities, outcomes)` compares your Brier
  score with always forecasting the base rate (climatology): 0 is no
  better, 1 is perfect, and negative is worse than knowing nothing.

## 7. Sport

- **Goals are Poisson.** In football, hockey and other low-scoring games,
  goals arrive roughly at random at a rate that depends on the two
  sides. With expected goals of 1.5 against 1.1,
  `POISSON.MATCH(1.5, 1.1, "home")` gives the home win's chance, 46%.
  `POISSON.SCORE` gives exact scores. The optional *rho* (around −0.05 to
  −0.1) is Dixon and Coles' correction for the low scores Poisson gets
  slightly wrong.
- **Expected goals from results.**
  `MATCH.XG(home, away, home_teams, away_teams, home_goals, away_goals)`
  multiplies the league's average home score by the home side's
  attacking strength and the away side's defensive weakness, each a ratio
  to the average and shrunk towards it by one game's worth of average
  results. `side` 2 gives the away side's figure. Use more than a handful
  of games: the football example adds last season's.
- **Seasons, not matches.** Simulate every remaining match with
  `RAND.POISSON` goals, add up the points, and `SIM.MEAN` of "is top" is
  a title chance. That is how the bookmakers' outright odds are made.
- **Elo.** `ELO.EXPECT(rating, opponent, [home_advantage])` gives the
  expected score. `ELO.UPDATE(rating, expected, actual, k)` moves the
  rating by *k* times the surprise. Chess, football and tennis ratings
  all work this way.

## 8. Markets

- **Random walks.** Prices are modelled as geometric Brownian motion:
  log returns are normal, with a drift and a volatility.
  `DRIFT(prices, 52)` and `VOLATILITY(prices, 52)` estimate both from
  weekly prices. Then `GBM.PROB`, `GBM.PERCENTILE` and `RAND.GBM` answer
  "where in a year?".
- **The drift is barely known.** Two years of data pin the volatility down
  well but the drift only to about ±20% a year: the standard error of a
  drift is the volatility divided by the square root of the years. Treat
  the drift as a guess, or make it an uncertain input of its own.
- **Fat tails.** Real returns have more extreme weeks than a normal
  distribution allows. Resampling the history's own returns
  (`RAND.BOOTSTRAP`) keeps them, as does `RAND.STUDENT`.
- **Risk.** Value at risk is today's price minus `SIM.PERCENTILE(end, 5%)`.
  `SIM.TAILMEAN` gives the expected shortfall beyond it. `DRAWDOWN(path)`
  is the worst fall along the way, and its distribution across the
  futures is often what an investor actually feels.
- **The market's own forecast.** `BLACKSCHOLES(price, strike, rate,
  volatility, years)` is what an option costs if the volatility is as
  given. Read the other way round, an option's price reveals the
  volatility the market expects.

## 9. Growth, benchmarks and trends

- **Benchmarks first.** A method earns its keep only if it beats the
  simple ones:
  - carrying the last value forward;
  - the value a season ago (`FORECAST.SNAIVE`);
  - the last value plus the average step so far (`FORECAST.DRIFT`).

  Forecasting competitions have shown, again and again, how often they
  win.
- **Damped trend** (`FORECAST.DAMPED`). Holt's trend, fading by *phi* a
  period so that the forecast levels off instead of running on for ever.
  It was the most reliable automatic method in the M-competitions.
- **S-curves.** Anything that spreads through a population (a product, a
  technology, an epidemic) grows exponentially at first and then meets a
  ceiling:
  - `LOGISTIC(t, capacity, rate, midpoint)` is symmetric;
  - `GOMPERTZ` is lopsided;
  - `BASS(t, p, q, market)` separates adopters who come on their own
    (*p*, about 0.03) from those who follow others (*q*, about 0.4).

  Fitted before the midpoint, the ceiling is barely known, so make it an
  uncertain input.
- **A lopsided risk** (`RAND.SPLITNORMAL(mode, sd_below, sd_above)`) is a
  bell with a different spread on each side. The Bank of England draws its
  inflation fan charts this way.

## 10. Learning from past cases

The most general forecasting method there is: find cases like this one,
and see how they turned out. Lay the past cases out as a table — a row
each, a column per thing known about it (numbers; 1 and 0 for yes and
no), a column of outcomes — and give the new case as a row of the same
features. Every function below reads that layout.

- **Analogues, or k nearest neighbours** (`KNN.FORECAST`,
  `KNN.PERCENTILE`, `RAND.KNN`). Each feature is measured in its own
  standard deviations, so that none dominates by its units; the k closest
  cases (√n by default) are weighted equally, by the tricube of their
  distance, or by its inverse. Their outcomes are the forecast
  distribution: no functional form, interactions for free, but no
  extrapolation beyond what has happened.
- **Multiple regression** (`FORECAST.MLR`, `FORECAST.MLR.CONFINT`,
  `RAND.MLR`, `MLR.COEF`). Least squares by Householder QR, on
  standardised features. The interval is t·s·√(1 + h₀), h₀ the new
  case's leverage, which grows away from the data; the draw is Student's
  t, and the coefficients' uncertainty is shared by every cell drawing
  from the same fit. A ridge penalty keeps collinear features in check.
- **Chances of events** (`LOGIT.PROB`): logistic regression with Firth's
  correction, which keeps the estimates finite when the features split the
  cases perfectly — common with few of them — and removes most of the
  small-sample bias. The answer averages over the coefficients'
  uncertainty (MacKay's probit approximation), which pulls it a little
  towards a half.
- **Counts** (`POISSON.REG`, `RAND.POISSON.REG`), with exposures (days,
  visits, kilometres). The draw adds the rate's uncertainty and, where the
  past counts scattered more than Poisson's, a negative binomial.
- **Percentiles directly** (`QUANTILE.REG`): Koenker's quantile
  regression, by Hunter and Lange's iteratively reweighted least squares.
  Where the spread grows with a feature — novel projects miss by more —
  the P90 grows faster than the mean.
- **Honest intervals from any forecaster** (`CONFORMAL.CONFINT`): from how
  far past forecasts missed, the ⌈(n+1)(1−α)⌉-th smallest miss covers the
  next one with probability at least 1−α, whatever made the forecasts —
  a model, a spreadsheet, a person — provided the past misses and the next
  are alike. It needs 1/α − 1 misses at least; it is `#NUM!` with fewer.
  The misses must be out of sample: a model's errors on the data it was
  fitted to are too small.

## 11. Updating on evidence

- **Proportions** (`RAND.PROPORTION(successes, trials)`): the success rate
  after a record, from a uniform prior by Bayes' rule — a beta
  distribution with mean (s + 1)/(n + 2), Laplace's. With future trials,
  how many succeed: the beta-binomial, the rate's uncertainty and the
  trials' luck together. An A/B test's "chance B is better" is
  `SIM.PROB` of the difference of two such draws.
- **Rates** (`RAND.RATE(events, exposure)`): events per unit of exposure,
  from Jeffreys' prior; with a future exposure, the negative binomial
  count.
- **Partial pooling** (`SHRINK`, `SHRINK.RATE`). One school's results,
  one store's growth, one surgeon's record — estimates of the same kind —
  are pulled towards their common mean by as much as their own noise,
  against the real spread between them, warrants (DerSimonian and Laird's
  estimate of that spread; an empirical-Bayes beta prior for rates). Small
  samples move a lot, large ones hardly.
- **The next value** (`RAND.NEXT(data)`): x̄ + s·√(1 + 1/n)·t, the honest
  replacement for resampling when there are few data, since the mean
  itself is uncertain.
- **Distributions from data**: a kernel density estimate (`RAND.KDE`,
  `KDE.DIST`, `KDE.INV`, Silverman's bandwidth, an optional lower bound by
  reflection), and the data's own percentiles joined up
  (`RAND.EMPIRICAL`). For extremes, peaks over a threshold fitted with a
  generalised Pareto tail by probability-weighted moments: `TAIL.PROB` is
  the chance of exceeding a level beyond anything seen, `RETURN.LEVEL`
  the 100-year flood.
- **Lifetimes** (`WEIBULL.FIT`, `RAND.WEIBULL`, `KAPLAN.MEIER`,
  `RAND.SURVIVAL`). Some units are still running; ignoring them biases
  every estimate short. The Weibull is fitted by maximum likelihood with
  them counted as lasting at least so long; Kaplan–Meier's curve needs no
  shape at all. `RAND.WEIBULL(shape, scale, age)` draws when a unit that
  has lasted `age` will fail.
- **With nothing to go on** (`RAND.LINDY(age)`): Gott's rule. Something
  seen at a random moment of its life has even odds of lasting as long
  again, and a chance 1/(1 + x) of lasting x times as long.
- **Queues** (`ERLANG.C`): the chance a caller waits, and the mean wait,
  for so many servers — call centres, clinics, repair crews.

## 12. Inputs that move together

Two costs that rise with the same inflation, sales of products that share
a market: treating them as independent makes totals far too certain.
`RAND.COPULA(correlations, index)` returns a uniform correlated with the
others drawn from the same matrix (a Gaussian copula); feed it to any
inverse — `NORM.INV`, `BETA.INV`, `LOGNORM.INV`, `PERCENTILE` of history
— and each input keeps its own distribution while the correlations hold.
The matrix gives the correlations of the underlying normals; a rank
correlation ρₛ corresponds to 2 sin(πρₛ/6).

## 13. Scoring any forecast

A method is only as good as its record, and these put a number on a
record whatever made it:

- **CRPS** (`CRPS(samples, observed)`, `SIM.CRPS(cell, observed)`,
  `CRPS.NORMAL`): the continuous ranked probability score, the distance
  between a forecast distribution and what happened, in the outcome's
  units — for a single number, the absolute error. Lower is better, and
  it rewards being both right and sure.
- **Calibration** (`SIM.PIT`): where the outcome fell among the futures.
  Over many forecasts these should be spread evenly from 0 to 1; piled at
  the ends, the forecasts were too sure. `COVERAGE` counts how often
  outcomes fell inside their intervals, `INTERVAL.SCORE` is Winkler's
  score (width plus 2/α times each miss), `PINBALL` scores percentile
  forecasts, and `MASE` scales errors by the naive forecast's.
- **Recalibration** (`CALIBRATE(p, past_probabilities, past_outcomes)`):
  a probability corrected by the forecaster's record — Platt's logistic
  scaling (a slope above 1 means the forecaster was too timid) or
  isotonic regression for long records.
- **Experts** (`EXPERT.CALIBRATION`): Cooke's calibration score for
  someone's past 5th, 50th and 95th percentiles.

## 14. Deciding

The point of a forecast is a better decision. Work out what each option
is worth in each future — cells computed from the same uncertain inputs —
and after F5:

- `SIM.PBEST(k, option1, option2, …)` is the chance option k turns out
  best.
- `SIM.EVPI(option1, option2, …)` is the expected value of perfect
  information: how much better one would do choosing after seeing the
  future. It bounds what any study, survey or delay could be worth.
  `SIM.EVPPI(input, options…)` is the same for learning one input only
  (Strong and Oakley's method, from the futures already simulated).
- `SIM.CE(cell, risk_tolerance)` is the certainty equivalent under
  exponential utility: the sure amount someone that averse to risk would
  take instead.
- `SIM.SOBOL(input, output)` is the share of the output's variance the
  input explains on its own — a first-order sensitivity index that, unlike
  a correlation, sees non-linear effects.
- For a stock, a capacity or a staffing level, the best quantity is the
  demand's `SIM.PERCENTILE` at cᵤ/(cᵤ + cₒ), the cost of too little over
  the costs of both errors (the newsvendor).

## 15. Pictures and maps

Much of what is known about the future is in pictures: radar and
satellite images, photographs of a field, land-cover maps. And much of
what is predicted happens somewhere.

- **Reading pictures.** Pixels are read by position or by latitude and
  longitude (`IMAGE.AT`, `IMAGE.GEO`), bilinearly interpolated, in any of
  the channels: brightness (Rec. 709), hue, saturation, and the vegetation
  indices excess green (2g − r − b of the chromatic coordinates) and
  VARI. `IMAGE.FRACTION` measures cover — plants, cloud, water — and
  `IMAGE.OTSU` finds the threshold that best splits a picture in two.
- **Colour-coded maps.** Radar, land cover, a published heat map: the
  colours stand for values, and `IMAGE.LEGEND` reads them back through
  the legend (the nearest colour within a tolerance). The wildfire
  example reads fuel off a land-cover picture this way.
- **Motion** (`IMAGE.MOTION`): block matching between two frames —
  every shift within reach is tried, the one with the least mean squared
  difference kept and refined to a fraction of a pixel by a parabola. It
  is the heart of radar nowcasting: extrapolate the rain along its motion
  (backwards, semi-Lagrangian: the rain here in an hour is the rain
  upstream now), with uncertainty in the motion and in growth and decay.
  Reflectivity turns into a rain rate by Marshall and Palmer's Z = 200 R¹·⁶.
- **Places.** Great-circle distances (haversine), bearings and
  destinations on a sphere of 6,371 km. `MAP.REGION` finds the region a
  place is in by ray casting (holes and islands included), `MAP.AREA`
  measures regions on the sphere, `MAP.DISTANCE` how far a place is from
  one.
- **Between stations.** Inverse distance weighting (`GEO.IDW`) is simple;
  ordinary kriging (`GEO.KRIGE`, with an exponential variogram whose
  range defaults to a third of the stations' spread) also gives its own
  error (`GEO.KRIGE.SD`), and `RAND.KRIGE` draws from it.
- **Tracks and cones.** The hurricane example turns the National
  Hurricane Center's cone — circles holding two thirds of five years'
  track errors — into a random walk whose spread matches the cone at each
  hour (σ = r / 1.4823 each way), then asks the world map which
  countries each simulated track crosses.
- **Spread** (`RAND.SPREAD`): a stochastic cellular automaton over a grid
  of fuel, each burning cell igniting its neighbours with a chance scaled
  by their fuel and by the wind (Alexandridis and colleagues' factor).
  Every cell of the grid sees the same spread within a future, so the
  block is one fire; simulated, it is the chance each cell burns.

## 16. Reading the results

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
- **Words.** A cell whose futures are words is shown as bars;
  `SIM.PROB(cell, "rain")` is the chance of one and `SIM.MODE` the
  likeliest.
- **Big models.** Every uncertain cell's futures are kept while they fit
  in 32 million numbers. Past that, the simulator keeps the cells `SIM.*`
  formulas name and the model's outputs (uncertain cells nothing else
  names), and says so.
