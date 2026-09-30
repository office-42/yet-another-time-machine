# Architecture

What the layers are, what each may know about, and why the simulator is
built the way it is.

## The layers

```
    ui/        TmWindow -> TmGrid, TmChart, tm-mapview    GTK 4, Cairo, Pango
      |
      v
    io/        tm-file: .tm and CSV, sample export
               tm-sources: pictures (gdk-pixbuf) and GeoJSON maps
      |
      v
    model/     TmSheet (cells, sources, recalculation, simulation)
               -> TmSim (samples)
      |
      v
    formula/   TmNode (parser), tm_eval, the function library
      |
      v
    util/      TmRef/TmRange, TmValue, TmRng (random numbers),
               number formats, shared statistics, linear algebra,
               TmImage, TmMap and spherical geometry, a small JSON reader
```

gdk-pixbuf, which decodes pictures, is below the line: it is not GTK, and
the terminal front-end reads pictures too.

Dependencies point downwards. `formula/` does not know what a sheet is: it
asks callbacks for cell values and for a simulation's samples. Nothing
below `ui/` includes GTK. The core builds as its own static library
(`libtmcore`), which both `timemachine` and `timemachine-calc` link. That
is how the engine is checked on a machine with no display.

## Recalculation

A cell asked for its value works it out, asking the cells it names for
theirs. Each value is computed once per *generation*, and a new
recalculation is a new generation. A cell met again while it is still
being computed is a cycle, and gets `#CIRC!`.

Recalculation works the cells out in dependency order: a depth-first walk
over the references in the formulas, with a stack of its own rather than
the C stack, puts every formula after the cells it names. Each cell then
finds its precedents already done, so a chain ten thousand cells long is
one call deep, not ten thousand. The order is kept until a cell changes;
ranges named by many cells are looked through once. A cycle is cut where
it is met, and its cells come out `#CIRC!`.

A cell remembers whether its value was drawn at random, directly by a
function flagged `TM_FN_RANDOM` or through a cell it depends on. That flag
is how the grid knows what to tint and the simulator knows what to keep.

## Simulation

`tm_sheet_simulate` works like this:

1. Seeds the generator with the sheet's seed.
2. Evaluates the whole sheet once, which shows which formula cells are
   random.
3. Decides which cells to keep: every random formula cell, plus every cell
   a `SIM.*` function names (any of its arguments). If that would cost
   more than 32 million doubles, the `SIM.*` ones and then the model's
   outputs — random cells no formula names — while there is room, and
   the window says so.
4. Runs the remaining iterations, each a new generation that works out,
   in dependency order, every cell that could be random. The rest keep
   the value the first iteration gave them. "Could be random" is decided
   from the formulas before the run: the cell draws, or something it
   names does, however indirectly; a cycle counts as could-be, since an
   `IF` may steer round it. It is not decided from what the first
   iteration happened to do, because an `IF` may take a random branch in
   another future. This is what keeps a model with a large history (the
   Markov estimates in the weather example, the expected goals in the
   football one) as fast as one without.
5. Swaps the new results in and recalculates once more, so that the
   `SIM.*` functions see them.

A sample is the cell's number, TRUE as 1 and FALSE as 0 (so the mean of
`=B14<0` is a probability), or NaN. Text is kept too: a quiet NaN whose
spare bits number the text in the simulation's list, so that the numeric
statistics skip it while `SIM.PROB` and `SIM.MODE` can count it. Sorted
copies are made lazily for percentiles.

Some draws must agree between cells within a future. A forecast's months
share their errors; every cell of a fire's grid sees the same fire; the
inputs named in one copula matrix share their normals. For these the
evaluation context offers a *shared stream*: a generator seeded from the
seed, the future and a key the function makes from the numbers that
identify what is shared (the fitted series, the fuel grid, the matrix).
Every cell that asks with the same key in the same future gets the same
draws, whatever order they are worked out in.

Functions that fit a model to a table — regression, logistic, Poisson,
quantile — are worked out once per future with the same table. The fits
are kept in a small cache keyed by the numbers they were made from, so
only the first of ten thousand pays for the fit. Functions over pictures
keep what they measured (a motion estimate, a fire) until the next
generation.

While a simulation runs, the window disables every action but Stop, so
that nothing changes the sheet under it.

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
