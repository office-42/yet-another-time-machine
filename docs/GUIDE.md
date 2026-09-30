# User guide

## The window

- **Toolbar.** **▶ Simulate** (F5) runs the model through as many futures
  as **Futures** says, starting from **Seed**; the same seed always gives
  the same futures. **Draw again** (F9) recalculates, so every uncertain
  cell draws a new value, just as Excel's F9 re-rolls `RAND()`.
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
    the median, with cells that are plain values (the history) drawn as a
    line leading into it.

  Under the chart are the mean, standard deviation, standard error of the
  mean, minimum, maximum, P5, P25, median, P75, P95 and the chance the
  cell is below zero.

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
| Ctrl+C / Ctrl+X / Ctrl+V | copy / cut / paste |
| Ctrl+D / Ctrl+R | fill down / fill right from the first row / column |
| F5 | simulate |
| F9 | draw again |
| F1 | the list of functions |
| Ctrl+N, Ctrl+O, Ctrl+S, Ctrl+Shift+S, Ctrl+Q | new, open, save, save as, quit |

A formula starts with `=`. A value starting with `'` is kept as text. You
can type `12%` for 0.12, and you can use `12%` inside formulas too.
Copying and filling move relative references, and a `$` keeps one fixed,
as in Excel: `=$B$4*C10`. Pasting a copied cell over a larger selection
repeats it across the whole selection.

Drag the line between two column headers to resize a column; double-click
it to put the column back to its default width.

## The formula language

Operators, loosest first: comparison `= <> < > <= >=`, `&`, `+ -`,
`* /`, `^` (left to right, as in Excel: `2^3^2` is 64), unary minus
(tighter than `^`: `-2^2` is 4), and `%`. References are `A1`, `$A$1` and
`A1:C9`. Errors are values: `#DIV/0!`, `#VALUE!`, `#REF!`, `#NAME?`,
`#NUM!`, `#N/A`, and `#CIRC!` for a cell that depends on itself.

**Help › Functions** (F1) lists every function with its syntax. The
families are:

- **Random** (`RAND.*`): distributions an uncertain cell draws from.
- **Processes**: `RAND.GBM`.
- **Simulation** (`SIM.*`): statistics over the last simulation's futures.
- **Forecasting**: regression, exponential smoothing, growth.
- **Judgment**: `BAYES`, `EXTREMIZE`, `BRIER`, `LOGSCORE`.
- **Statistics**, **Distributions**, **Maths**, **Logic**, **Text**,
  **Lookup** and **Finance**: the ordinary spreadsheet functions,
  behaving as Excel's do.

## Files

**.tm** files are the program's own. They are text, one line per cell,
tab-separated:

```
timemachine 1
iterations	10000
seed	1
width	A	230
cell	A4	Market size (units a year)
cell	B4	=RAND.LOGCI(80000,300000)
```

A tab, a newline or a backslash inside a cell is written `\t`, `\n` or
`\\`. Lines the program does not understand are skipped, so newer files
still open in older versions.

**CSV** files open as data, one cell per field. Saving to a name ending
in `.csv` writes the values as the grid shows them.
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
| `stats B1` | mean, sd, se and percentiles of B1's futures |
| `histogram B1 [bins]` | the futures drawn as a histogram in text |
| `fan B1:M1` | P5, P25, P50, P75 and P95 cell by cell |
| `iterations N`, `seed N` | simulation settings |
| `draws N` | seed the draws that ordinary recalculation makes |
| `filldown A1:A9`, `fillright A1:F1` | fill |
| `copy A1:B2 D1`, `clear A1:B2` | copy (moving references) and clear |
| `load FILE`, `save FILE`, `export FILE` | files; `export` writes the samples |
| `functions` | every function with its syntax and a line of help |
| `recalc` | draw again |

## Running headless

The window can be driven and photographed from a script. This is how the
pictures in the README were made:

```sh
timemachine samples/launch.tm --simulate --select B14 --size 1280x760 --screenshot launch.png
```

With no display, run it under `xvfb-run` with `GSK_RENDERER=cairo`.
