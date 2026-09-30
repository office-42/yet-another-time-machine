# Time Machine

**Time Machine** (the binary is `timemachine`) is a spreadsheet for making
predictions about the future. It looks and works like Excel, and every
number in it can be a *range* instead of a single guess. Press **F5** and
it lives through ten thousand possible futures. For any cell it then shows
what could happen: the most likely outcome, the 90% range, the chance of a
loss, and which inputs matter most.

It is written from scratch in C on GTK 4, Pango and Cairo, with a Meson
build — the same stack and the same principles as its sister project
[office42-spreadsheet](https://github.com/office-42/office42-spreadsheet):
a small, honest codebase that does one thing well.

![Time Machine showing a product-launch model: inputs in lavender-tinted cells drawn from PERT, lognormal, triangular and Bernoulli distributions; the profit cell selected; and on the right a forecast panel with a histogram of 10,000 simulated profits — the loss-making futures in red — marked at P5, P50 and P95, with the mean, standard deviation, percentiles and a 27.7% chance of a loss](docs/images/launch.png)

## How it predicts

Most spreadsheets give one answer to "what will profit be next year?",
and that answer is almost always wrong. Time Machine is built on the
methods that forecasters, risk analysts and superforecasters use instead.
[docs/METHODS.md](docs/METHODS.md) explains each one;
[docs/RESEARCH.md](docs/RESEARCH.md) has the formulas and sources.

- **Monte Carlo simulation.** An uncertain cell holds a distribution:
  `=RAND.PERT(80,100,150)` or `=RAND.LOGCI(20000,80000)`. The sheet is
  recalculated with fresh random draws thousands of times, and every cell
  that depends on the uncertain ones gets a distribution of its own.
  `=SIM.PROB(B14,"<0")` is then the chance of a loss.
- **Calibrated estimates.** Following Hubbard's *How to Measure Anything*,
  you give a range you are 90% sure of rather than a single number
  (`RAND.CI`, `RAND.LOGCI`). You can also give a three-point estimate
  (`RAND.PERT`, `RAND.TRIANGULAR`).
- **Time-series forecasting.** `FORECAST.ETS` uses exponential smoothing
  (Holt–Winters), finds the length of the season by itself, and gives
  prediction intervals. `FORECAST.LINEAR`, `TREND` and `GROWTH` fit
  regression lines. `RAND.ETS` and `RAND.LINEAR` draw from a forecast's
  error, so an extrapolation's uncertainty flows into the simulation.
- **Stochastic processes.** Random walks and geometric Brownian motion
  (`RAND.GBM`), and bootstrap resampling of history (`RAND.BOOTSTRAP`).
- **Judgment and keeping score.** Bayes' rule (`BAYES`), extremizing a
  crowd's average probability (`EXTREMIZE`), and scoring forecasts
  against what happened (`BRIER`, `LOGSCORE`).
- **Sensitivity.** `SIM.CORREL` shows how much each input drives an
  output. `SIM.TAILMEAN` gives the average of the worst futures
  (expected shortfall).

Select one uncertain cell to see a histogram of its futures. Select a row
or column of them — a quantity month by month — to see a fan chart that
grows out of the history before it:

![A fan chart of retirement savings over 25 years: a median line rising from 100k to about 790k, inside a 50% band and a wider 90% band that spreads to over 2M](docs/images/retirement.png)

## Examples

**File › Open Example** has five models. Each one shows a different method:

| Example | Method |
|---|---|
| Product launch | Monte Carlo with PERT, lognormal, triangular and Bernoulli inputs. Also shows the chance of a loss, expected shortfall, what drives profit, and the *flaw of averages* (the plan built from average inputs is not the average outcome). |
| Sales forecast | Holt–Winters exponential smoothing on three years of seasonal sales, with the forecast drawn as a fan out of the history. |
| Retirement savings | A 25-year geometric random walk of market returns: the chance of reaching a goal, compared with the straight-line plan. |
| Project schedule | Three-point (PERT) estimates, and the *merge bias* that makes plans built from averages run late on average. |
| Forecasting tournament | Brier and log scores for two forecasters and their crowd, extremizing, and Bayes' rule checked against a simulation. |

## Building

You need a C compiler, Meson, Ninja, and the development files for
GTK 4.10 or later.

```sh
# Debian / Ubuntu
sudo apt install gcc meson ninja-build libgtk-4-dev
# Fedora
sudo dnf install gcc meson ninja-build gtk4-devel

meson setup builddir
meson compile -C builddir
./builddir/src/timemachine samples/launch.tm
```

`meson install -C builddir` installs the program, its desktop entry, its
icon and the examples.

## The terminal front-end

`timemachine-calc` runs the same engine without a window. It reads
commands on standard input:

```sh
$ ./builddir/src/timemachine-calc <<'EOF'
A1 = =RAND.PERT(800,1000,1500)
A2 = =RAND.NORMAL(20,2)
A3 = =A1*A2-15000
simulate
stats A3
EOF
simulated 10000 iterations, seed 1, 3 cells kept
A3	iterations=10000 valid=10000 mean=5943.19 sd=3264.49 se=32.6
A3	min=-2668.12 p5=993.744 p10=1941.97 p25=3570.39 p50=5714.05 p75=8020.12 p90=10369.3 p95=11748.7 max=19786.9
```

It also has `histogram`, `fan`, `dump`, `load`, `save`, `export` (all
samples to CSV), `filldown`, `fillright`, `copy` and `functions`.
[docs/GUIDE.md](docs/GUIDE.md) is the user guide.
`sh build-aux/smoke-test.sh builddir/src/timemachine-calc` checks the
engine end to end. It compares simulated means with what theory says.

## Files

A model is saved as a `.tm` file. It is plain text, one line per cell,
so it can be read, diffed and kept in version control:

```
timemachine 1
iterations	10000
seed	1
cell	A4	Market size (units a year)
cell	B4	=RAND.LOGCI(80000,300000)
```

CSV files open as data and save as values. **File › Export Samples**
writes every simulated future of every uncertain cell to CSV, for
analysis elsewhere.

## Status

This is an early version. The engine, the simulator and the forecasting
functions are done and checked. The window covers the essentials: a grid,
a formula bar, fill, copy and paste, and the forecast panel.
[docs/ROADMAP.md](docs/ROADMAP.md) lists what comes next: undo, number
formats, Latin hypercube sampling, correlated inputs, a tornado chart and
more forecasting methods.

## License

GPL-3.0-or-later. See [LICENSE](LICENSE).
