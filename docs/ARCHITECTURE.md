# Architecture

What the layers are, what each may know about, and why the simulator is
built the way it is.

## The layers

```
    ui/        TmWindow -> TmGrid, TmChart            GTK 4, Cairo, Pango
      |
      v
    io/        tm-file: .tm and CSV, sample export
      |
      v
    model/     TmSheet (cells, recalculation, simulation) -> TmSim (samples)
      |
      v
    formula/   TmNode (parser), tm_eval, the function library
      |
      v
    util/      TmRef/TmRange, TmValue, TmRng (random numbers)
```

Dependencies point downwards. `formula/` does not know what a sheet is: it
asks callbacks for cell values and for a simulation's samples. Nothing
below `ui/` includes GTK. The core builds as its own static library
(`libtmcore`), which both `timemachine` and `timemachine-calc` link. That
is how the engine is checked on a machine with no display.

## Recalculation

A cell asked for its value works it out, asking the cells it names for
theirs. Each value is computed once per *generation*, and a new
recalculation is a new generation. So there is no dependency graph to
maintain. The order cells were typed in does not matter. A cell met again
while it is still being computed is a cycle, and gets `#CIRC!`.
Evaluation runs row by row, which keeps the recursion shallow for models
laid out the usual way: time across, cause above effect.

A cell remembers whether its value was drawn at random, directly by a
function flagged `TM_FN_RANDOM` or through a cell it depends on. That flag
is how the grid knows what to tint and the simulator knows what to keep.

## Simulation

`tm_sheet_simulate` works like this:

1. Seeds the generator with the sheet's seed.
2. Evaluates the whole sheet once, which shows which formula cells are
   random.
3. Decides which cells to keep: every random formula cell, plus every cell
   a `SIM.*` function names. If that would cost more than 32 million
   doubles, only the `SIM.*` ones.
4. Runs the remaining iterations, each a new generation that evaluates
   only the kept cells. What they depend on is computed on demand.
5. Swaps the new results in and recalculates once more, so that the
   `SIM.*` functions see them.

A sample is the cell's number, TRUE as 1 and FALSE as 0 (so the mean of
`=B14<0` is a probability), or NaN. Sorted copies are made lazily for
percentiles.

The same seed gives the same futures, because the whole run draws from
one xoshiro256** stream in a fixed order. The cost is that editing one
uncertain cell can change the draws of the cells after it. Per-cell
streams would fix that; see ROADMAP.md.

## Values and errors

A value is empty, a number, text, a boolean or an error. Errors are
values: they propagate through arithmetic and can be caught with
`IFERROR`. The coercions are Excel's. An empty cell is 0 in arithmetic
and "" in text. Text is a number only if the whole of it is one. A
non-finite result is `#NUM!`.
