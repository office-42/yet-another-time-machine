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
    util/      TmRef/TmRange, TmValue, TmRng (random numbers),
               number formats, shared statistics
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
   only the kept cells. What they depend on is computed on demand, except
   for cells that cannot be random. Those keep the value the first
   iteration gave them. "Cannot be random" is decided from the formulas
   before the run: the cell does not draw, and neither does anything it
   names, however indirectly. It is not decided from what the first
   iteration happened to do, because an `IF` may take a random branch in
   another future. This is what keeps a model with a large history (the
   Markov estimates in the weather example, the expected goals in the
   football one) as fast as one without.
5. Swaps the new results in and recalculates once more, so that the
   `SIM.*` functions see them.

A sample is the cell's number, TRUE as 1 and FALSE as 0 (so the mean of
`=B14<0` is a probability), or NaN. Sorted copies are made lazily for
percentiles.

Every random cell has a random stream of its own. The stream is a
xoshiro256** generator seeded from a hash of three things: the seed, the
cell's position, and the number of the draw (the future being simulated,
or how many times **Draw again** has been pressed). Nothing else goes
into it. This has three consequences:

- The same seed gives the same futures, whatever order cells are worked
  out in.
- Editing one cell does not change any other cell's draws. Typing no
  longer re-rolls the whole sheet, as it does in Excel; only F9 does.
- Two versions of a model, run with the same seed, see the same futures
  (*common random numbers*). The difference between their results is
  then the change, not the luck of the draw.

A formula that draws more than once, such as
`=RAND.NORMAL(0,1)+RAND.NORMAL(0,1)`, takes its draws one after another
from its cell's stream.

Under Latin hypercube sampling each draw in a cell's formula (its *slot*)
gets a shuffle of the numbers 0 to N−1, seeded from the seed, the cell
and the slot. In future i, the draw takes the stratified uniform
(perm[i] + U)/N, with U from the cell's stream, and passes it through
the distribution's inverse distribution function. So every distribution
has one: in closed form where there is one (normal, lognormal,
triangular, exponential, metalog), and otherwise by Newton's method kept
inside a bracket by bisection, on the incomplete beta and gamma functions
(PERT, beta, gamma, Student's t) or by stepping along the distribution
function (Poisson, binomial). Under plain Monte Carlo the faster
rejection samplers are used instead.

## Undo

Every change to a cell's contents or format goes through one function,
which records what the cell held before. Changes are grouped: one paste,
one fill or one clear is undone as a single step, however many cells it
touched. Undoing a group applies it backwards and records its inverse
for redo.

## Values and errors

A value is empty, a number, text, a boolean or an error. Errors are
values: they propagate through arithmetic and can be caught with
`IFERROR`. The coercions are Excel's. An empty cell is 0 in arithmetic
and "" in text. Text is a number only if the whole of it is one. A
non-finite result is `#NUM!`.
