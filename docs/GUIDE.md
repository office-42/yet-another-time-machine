# User guide

## The window

- **Toolbar.** **▶ Simulate** (F5) runs the model through as many futures
  as **Futures** says, starting from **Seed**; the same seed always gives
  the same futures. **Draw again** (F9) makes every uncertain cell draw a
  new value. Unlike Excel, typing does not re-roll the draws: each
  uncertain cell keeps its draw until you press F9. The **%**, **1,000**,
  **.0+** and **.0−** buttons format the selection as a percentage, with
  thousands separated, or with more or fewer decimals. The sampling menu
  chooses between plain **Monte Carlo** and **Latin hypercube**
  sampling, which covers each input's range evenly and so settles with
  fewer futures.
- **Formula bar.** The name box on the left shows the selected cell. Type
  a cell or range there (`B14`, `B11:AA11`) to go to it. The long box
  shows what the cell holds, and you can edit it there.
- **Grid.** Cells whose value is a random draw, directly or through the
  cells they depend on, are tinted lavender and have a small mark in
  their top right corner.
- **Forecast panel** (right):
  - one uncertain cell selected: a histogram of its simulated futures,
    marked at P5, P50 and P95, with the mean as a triangle and any futures
    below zero drawn in red;
  - a row or column selected: a fan chart of the 90% and 50% bands and
    the median, a dozen single futures drawn faintly through them, and
    cells that are plain values (the history) drawn as a line leading
    into it;
  - a cell whose futures are words (a state, a winner, a country): a bar
    for each, likeliest first;
  - two columns headed *lat* and *lon* (with a column of values and one
    of names if you like), or two rows named so: a map. Places are
    coloured by their values; uncertain ones are drawn as the cloud of
    their simulated positions, or, for a track across time, as a hundred
    faint tracks and the median one. The maps and located pictures
    loaded as data are drawn underneath, and the view widens to a picture
    the places lie on;
  - any other block: a heat map of the cells' values, or, if they are
    uncertain, their simulated means — for a block of 1s and 0s, the
    chance each is 1.

  Under the chart are the mean, standard deviation, standard error of the
  mean, minimum, maximum, P5, P25, median, P75, P95 and the chance the
  cell is below zero, in the cell's own number format.

  **What drives it** is a tornado chart. It ranks the model's inputs
  (the cells whose own formulas draw at random) by how strongly the
  selected cell moves with each of them across the futures, measured as
  Spearman's rank correlation. Bars to the right push the result up and
  bars to the left pull it down. The longest bars mark the inputs worth
  measuring better.

## Typing

| Key | Does |
|---|---|
| any character | starts editing the cell with it |
| F2, double click | edits what the cell holds |
| Enter / Shift+Enter | commits and moves down / up |
| Tab / Shift+Tab | commits and moves right / left |
| Esc | abandons the edit |
| arrows, Page Up/Down, Home | move; with Shift, extend the selection |
| Ctrl+arrows | jump to the edge of the filled block |
| Ctrl+Home | go to A1 |
| Ctrl+A | select the used range |
| Delete | clear the selection |
| Ctrl+Z / Ctrl+Y (or Ctrl+Shift+Z) | undo / redo |
| Ctrl+C / Ctrl+X / Ctrl+V | copy / cut / paste |
| Ctrl+D / Ctrl+R | fill down / fill right from the first row / column |
| F5 | simulate |
| F9 | draw again |
| F1 | the list of functions |
| Ctrl+N, Ctrl+O, Ctrl+S, Ctrl+Shift+S, Ctrl+Q | new, open, save, save as, quit |

What you type appears in the cell as well as in the formula bar. While
you type a formula, wherever a reference could go (after `=`, `(`, `,` or
an operator), clicking a cell or dragging over a range puts its address
into the formula, marked on the grid with a dashed border. Clicking again
replaces it; typing anything else keeps it.

A formula starts with `=`. A value starting with `'` is kept as text. You
can type `12%` for 0.12, and you can use `12%` inside formulas too.
Copying and filling move relative references, and a `$` keeps one fixed,
as in Excel: `=$B$4*C10`. Pasting a copied cell over a larger selection
repeats it across the whole selection.

Clicking a column or row header while pointing puts the used part of
that column or row into the formula.

Cutting and pasting moves cells: formulas anywhere that name them follow
them, as in Excel. Numbers can be typed as they are written — `1,500`,
`$9.99`, `€20` — and the cell takes a matching format.

Drag the line between two column headers to resize a column; double-click
it to put the column back to its default width.

While a simulation runs, the menus and shortcuts wait; only **Stop**
works.

## Number formats

**Format** sets how numbers are shown. The format belongs to the cell,
not to what is in it. The codes are Excel's:

| Code | 1234.567 shows as |
|---|---|
| `0.00` | 1234.57 |
| `#,##0` | 1,235 |
| `$#,##0` | $1,235 |
| `0.0%` | 123456.7% (0.1234 shows as 12.3%) |
| `0.00E+00` | 1.23E+03 |
| `0.0 "days"` | 1234.6 days |
| `#,##0;(#,##0);"-"` | 1,235, negatives in brackets, zero as a dash |

Typing `12%` into a cell with no format of its own formats it as a
percentage. Copying and filling carry formats along. A number too wide
for its column shows as `###`.

## The formula language

Operators, loosest first: comparison `= <> < > <= >=`, `&`, `+ -`,
`* /`, `^` (left to right, as in Excel: `2^3^2` is 64), unary minus
(tighter than `^`: `-2^2` is 4), and `%`. References are `A1`, `$A$1` and
`A1:C9`. Errors are values: `#DIV/0!`, `#VALUE!`, `#REF!`, `#NAME?`,
`#NUM!`, `#N/A`, and `#CIRC!` for a cell that depends on itself.

**Help › Functions** (F1) lists every function with its syntax. The
families are:

- **Random** (`RAND.*`): distributions an uncertain cell draws from.
- **Processes**: Markov chains (`RAND.MARKOV`, `MARKOV.PROB`,
  `MARKOV.STEADY`, `MARKOV.ESTIMATE`), mean reversion (`RAND.AR1`),
  `RAND.GBM` and `RAND.SPLITNORMAL`.
- **Sports**: `POISSON.MATCH`, `POISSON.SCORE`, `MATCH.XG`, `ELO.EXPECT`,
  `ELO.UPDATE`.
- **Markets**: `DRIFT`, `VOLATILITY`, `GBM.PROB`, `GBM.PERCENTILE`,
  `BLACKSCHOLES`, `DRAWDOWN`.
- **Simulation** (`SIM.*`): statistics over the last simulation's futures.
- **Forecasting**: regression, exponential smoothing, damped trend, AR(1),
  the naive and drift benchmarks, and growth curves (`LOGISTIC`,
  `GOMPERTZ`, `BASS`).
- **Judgment**: `BAYES`, `EXTREMIZE`, `POOL.ODDS`, `LAPLACE`, `REFCLASS`,
  `BRIER`, `BRIER.SKILL`, `LOGSCORE`.
- **Learning**, from a table of past cases — a row each, a column per
  feature, a column of outcomes, and the new case as a row of features:
  `KNN.FORECAST`, `KNN.PERCENTILE` and `RAND.KNN` (the k nearest, each
  feature in its own standard deviations; kernel 0 counts them alike, 1
  tricube, 2 inverse distance), `FORECAST.MLR`, `FORECAST.MLR.CONFINT`,
  `RAND.MLR` and `MLR.COEF` (an optional ridge penalty tames collinear
  features), `LOGIT.PROB` (Firth's bias-reduced logistic regression),
  `POISSON.REG` and `RAND.POISSON.REG` (counts, with exposures),
  `QUANTILE.REG`, and `CONFORMAL.CONFINT` (an interval from past errors).
- **Updating**: `RAND.PROPORTION`, `RAND.RATE`, `RAND.NEXT`, `SHRINK`,
  `SHRINK.RATE`.
- **Survival**: `WEIBULL.FIT` (with `failed` flags, 0 for units still
  going), `RAND.WEIBULL` (optionally given the age reached),
  `KAPLAN.MEIER`, `RAND.SURVIVAL`.
- **Scoring**: `CRPS`, `CRPS.NORMAL`, `INTERVAL.SCORE`, `COVERAGE`,
  `PINBALL`, `MASE`, `EXPERT.CALIBRATION`, `CALIBRATE`; and among the
  simulation functions `SIM.CRPS` and `SIM.PIT`, which score a simulated
  cell once the outcome is known.
- **Decisions** (simulation functions over option cells worked out in the
  same futures): `SIM.PBEST`, `SIM.EVPI`, `SIM.EVPPI`, `SIM.CE`,
  `SIM.SOBOL`; `SIM.MODE` for the commonest outcome.
- **Forecasting**, besides the above: `FORECAST.THETA`, `CROSTON` for
  intermittent demand, `CHANGEPOINT`, `FORECAST.ANALOG` and
  `RAND.ANALOG` (the method of analogues on a series).
- **Distributions from data**: `RAND.KDE`, `KDE.DIST`, `KDE.INV`,
  `RAND.EMPIRICAL`, and for extremes `TAIL.PROB` and `RETURN.LEVEL`
  (a generalised Pareto tail). `RAND.COPULA` gives correlated uniforms to
  feed any inverse distribution; `ERLANG.C` answers queues; `RAND.LINDY`
  is Gott's rule.
- **Images**, **Geography** and **Maps**: see below.
- **Statistics**, **Distributions**, **Maths**, **Logic**, **Text**,
  **Lookup** and **Finance**: the ordinary spreadsheet functions,
  behaving as Excel's do.

## Pictures and maps

**Data › Import Picture…** loads a PNG, JPEG or any picture gdk-pixbuf
reads; **Data › Import Map (GeoJSON)…** loads regions, lines and points.
Each gets a name (the file's, until you change it), and **Data ›
Pictures and Maps…** lists them, sets a picture's bounds — *west south
east north*, in degrees — and removes them.

Positions on a picture are fractions: *u* across from 0 at the left to 1
at the right, *v* down from 0 at the top. A picture with bounds can also
be read by latitude and longitude. Channels are `gray`, `red`, `green`,
`blue`, `alpha`, `hue`, `saturation`, `value`, `exg` (excess green, 2g −
r − b of the chromatic coordinates: plants) and `vari` (the visible
atmospherically resistant index).

| Function | Answers |
|---|---|
| `IMAGE.AT(pic, u, v, [channel])` | a channel at a point, interpolated |
| `IMAGE.PIXEL(pic, x, y, [channel])` | one pixel, counted from 1 |
| `IMAGE.GEO(pic, lat, lon, [channel])`, `IMAGE.XY(pic, lat, lon, "u"\|"v")` | a located picture read by place, and where a place falls on it |
| `IMAGE.MEAN(pic, [channel], [u0, v0, u1, v1])` | a region's mean |
| `IMAGE.FRACTION(pic, channel, criteria, [region])` | the share of pixels meeting criteria: `">0.1"` of `exg` is green cover |
| `IMAGE.OTSU(pic, [channel])` | the level that best splits the picture in two |
| `IMAGE.LEGEND(pic, u, v, colours, values, [tolerance])` | the value a colour-coded map shows, through its legend; `#N/A` for a colour not in it |
| `IMAGE.MOTION(before, after, "dx"\|"dy", [max_shift])` | how far the scene moved between frames, as a fraction of the picture |
| `MAP.REGION(map, lat, lon, [property])` | the region a place is in, `#N/A` if none |
| `MAP.NEAREST`, `MAP.DISTANCE`, `MAP.CONTAINS` | the region nearest, the distance to a region (0 inside), whether it contains a place |
| `MAP.AREA`, `MAP.CENTROID`, `MAP.PROPERTY`, `MAP.COUNT`, `MAP.FEATURE` | a region's area in km², its middle, any property, and listing them |
| `GEO.DISTANCE`, `GEO.BEARING`, `GEO.DESTINATION` | great-circle geometry, in km and degrees |
| `GEO.NEAREST`, `GEO.WITHIN` | the nearest station, and how many are near |
| `GEO.IDW`, `GEO.KRIGE`, `GEO.KRIGE.SD`, `RAND.KRIGE` | interpolation between stations, kriging's error, and a draw from it |
| `RAND.SPREAD(fuel, row, col, p, steps, r, c, [wind_from], [wind_speed])` | 1 if a spread from (row, col) through a grid of fuel reaches (r, c) |

Regions are named by their `name` property (or `NAME`, `ADMIN` and the
like) unless a property is given. `RAND.SPREAD`'s cells all see the same
spread within one future, so a block of them is one fire; select it after
simulating for the chance each cell burns.

Each `RAND.KRIGE` cell draws on its own: right for one place, too
optimistic for a total over many, whose errors are correlated.

## Files

**.tm** files are the program's own. They are text, one line per cell,
tab-separated:

```
timemachine 1
iterations	10000
seed	1
sampling	latin
width	A	230
format	B4	#,##0
cell	A4	Market size (units a year)
cell	B4	=RAND.LOGCI(80000,300000)
```

A tab, a newline, a carriage return or a backslash inside a cell is
written `\t`, `\n`, `\r` or `\\`; spaces at the end of a cell are kept.
Lines the program does not understand are skipped, so newer files still
open in older versions. Pictures and maps are lines of their own, with a
path relative to the file:

```
image	radar_1	pictures/radar-1.png	4	57	14	62
map	world	maps/world.geojson
```

**CSV** files open as data, one cell per field. Saving to a name ending
in `.csv` writes the values: text as the grid shows it, numbers in full.
**File › Export Samples** writes one column per uncertain cell and one
row per future.

## The terminal front-end

`timemachine-calc [FILE]` loads the file, if there is one, then reads
commands from standard input:

| Command | Does |
|---|---|
| `A1 = 10`, `B1 = =A1*2` | set a cell to what follows the `=` |
| `B1` | print `name <tab> value <tab> input` |
| `dump` | print the used range as a table |
| `simulate [N]` | run the simulation, N futures if given |
| `stats B1` | mean, sd, se and percentiles of B1's futures; for words, how often each |
| `histogram B1 [bins]` | the futures drawn as a histogram in text |
| `fan B1:M1` | P5, P25, P50, P75 and P95 cell by cell |
| `iterations N`, `seed N` | simulation settings |
| `sampling latin`, `sampling random` | Latin hypercube or plain Monte Carlo |
| `format A1:B9 0.0%` | set a number format; no code means General |
| `undo`, `redo` | undo and redo |
| `redraw` | Draw again: the next draw from every random cell |
| `draws N` | seed the draws that ordinary recalculation makes |
| `filldown A1:A9`, `fillright A1:F1` | fill |
| `copy A1:B2 D1`, `clear A1:B2` | copy (moving references) and clear |
| `move A1:B2 D1` | cut and paste: formulas that name the cells follow them |
| `image FILE [NAME [W S E N]]` | load a picture, with its bounds |
| `map FILE [NAME]` | load a GeoJSON map |
| `sources` | list the pictures and maps |
| `load FILE`, `save FILE`, `export FILE` | files; `export` writes the samples |
| `functions` | every function with its syntax and a line of help |
| `recalc` | work every formula out again, keeping the draws |

## Running headless

The window can be driven and photographed from a script. This is how the
pictures in the README were made:

```sh
timemachine samples/launch.tm --simulate --select B14 --size 1280x760 --screenshot launch.png
```

With no display, run it under `xvfb-run` with `GSK_RENDERER=cairo`.
