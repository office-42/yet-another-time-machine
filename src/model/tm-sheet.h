/* tm-sheet.h - a sheet of cells, recalculated and simulated
 *
 * Copyright (C) 2026 The timemachine authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The sheet keeps what the user typed into each cell, the formula parsed
 * from it, and the value it came to.  Recalculation is on demand: a cell
 * asked for its value works it out, asking the cells it names for theirs,
 * once per generation -- so the order cells are typed in does not matter,
 * and a cell that names itself, however indirectly, is caught.
 */

#pragma once

#include "tm-value.h"
#include "tm-sim.h"

G_BEGIN_DECLS

typedef struct _TmSheet TmSheet;

TmSheet *tm_sheet_new   (void);
void     tm_sheet_free  (TmSheet *sheet);
void     tm_sheet_clear (TmSheet *sheet);

/* Sets a cell to what a user typed: "=formula", a number, text.  NULL or
 * "" empties it.  The sheet is not recalculated; call tm_sheet_recalc. */
void     tm_sheet_set_input (TmSheet *sheet, int row, int col, const char *input);
/* What was typed, or NULL for an empty cell. */
const char *tm_sheet_get_input (TmSheet *sheet, int row, int col);

/* Works out every formula again.  A random cell's draw depends only on
 * where the cell is and on which draw this is, so recalculating after an
 * edit leaves every other uncertain cell showing what it showed. */
void     tm_sheet_recalc (TmSheet *sheet);
/* Draw Again: recalculates with the next draw from every random cell. */
void     tm_sheet_redraw (TmSheet *sheet);
/* Seeds the draws ordinary recalculation makes (a simulation has a seed
 * of its own), so that a script's output can be the same every run. */
void     tm_sheet_seed_draws (TmSheet *sheet, guint64 seed);

/* The value, never NULL: an empty cell's is empty. */
const TmValue *tm_sheet_get_value (TmSheet *sheet, int row, int col);
/* The value as the grid shows it. */
char    *tm_sheet_get_display (TmSheet *sheet, int row, int col);
gboolean tm_sheet_is_formula (TmSheet *sheet, int row, int col);
/* Whether the cell's value was drawn at random, directly or through a
 * cell it depends on: whether it is uncertain. */
gboolean tm_sheet_is_random  (TmSheet *sheet, int row, int col);
/* Whether the cell's own formula draws at random: an input to the model,
 * as opposed to a cell that is uncertain because its inputs are. */
gboolean tm_sheet_is_source  (TmSheet *sheet, int row, int col);

/* A cell's number format ("0.0%", "#,##0"), or NULL for General.  Formats
 * belong to the cell, not its contents: an empty cell can have one. */
const char *tm_sheet_get_format (TmSheet *sheet, int row, int col);
void     tm_sheet_set_format   (TmSheet *sheet, int row, int col, const char *format);
void     tm_sheet_format_range (TmSheet *sheet, const TmRange *range, const char *format);

/* The smallest range holding every non-empty cell; FALSE if there are
 * none. */
gboolean tm_sheet_used_range (TmSheet *sheet, TmRange *out);

/* Calls func for every non-empty cell, row by row. */
typedef void (*TmCellFunc) (int row, int col, const char *input, gpointer data);
void     tm_sheet_foreach (TmSheet *sheet, TmCellFunc func, gpointer data);
/* ... and for every cell with a format, row by row, with the format. */
void     tm_sheet_foreach_format (TmSheet *sheet, TmCellFunc func, gpointer data);

/* Copies the cells of src so that its top left lands at (row, col), each
 * formula's relative references moved with it. */
void     tm_sheet_copy_range (TmSheet *sheet, const TmRange *src, int row, int col);
/* Fill Down and Fill Right: the first row (or column) of range copied
 * into the rest of it. */
void     tm_sheet_fill_down  (TmSheet *sheet, const TmRange *range);
void     tm_sheet_fill_right (TmSheet *sheet, const TmRange *range);
void     tm_sheet_clear_range (TmSheet *sheet, const TmRange *range);

/* ---- Undo ------------------------------------------------------------- */

/* Changes between begin and end are undone and redone as one; a change
 * made outside such a pair is one on its own. */
void     tm_sheet_begin_undo (TmSheet *sheet);
void     tm_sheet_end_undo   (TmSheet *sheet);
gboolean tm_sheet_undo       (TmSheet *sheet);
gboolean tm_sheet_redo       (TmSheet *sheet);
gboolean tm_sheet_can_undo   (TmSheet *sheet);
gboolean tm_sheet_can_redo   (TmSheet *sheet);
/* After loading a file, there is nothing to undo. */
void     tm_sheet_forget_undo (TmSheet *sheet);

/* Column widths in pixels, for the grid and the file. */
#define TM_DEFAULT_COL_WIDTH 80
int      tm_sheet_col_width     (TmSheet *sheet, int col);
void     tm_sheet_set_col_width (TmSheet *sheet, int col, int width);

/* ---- Simulation ------------------------------------------------------- */

/* The settings a simulation runs with, kept with the sheet and saved in
 * its file. */
/* How the futures are drawn.  Plain Monte Carlo draws every input
 * independently each time.  Latin hypercube sampling splits each input's
 * range into as many equally likely strata as there are futures and draws
 * from every stratum exactly once, so that the futures cover each input
 * evenly; results settle with far fewer of them. */
typedef enum {
  TM_SAMPLING_MONTE_CARLO,
  TM_SAMPLING_LATIN_HYPERCUBE
} TmSampling;

TmSampling tm_sheet_sampling     (TmSheet *sheet);
void       tm_sheet_set_sampling (TmSheet *sheet, TmSampling sampling);

int      tm_sheet_iterations     (TmSheet *sheet);
void     tm_sheet_set_iterations (TmSheet *sheet, int iterations);
guint64  tm_sheet_seed           (TmSheet *sheet);
void     tm_sheet_set_seed       (TmSheet *sheet, guint64 seed);

/* Called every so often while a simulation runs; returning FALSE stops
 * it. */
typedef gboolean (*TmSimProgress) (int done, int total, gpointer data);

/* Runs the sheet iterations times with fresh draws each time, keeping the
 * samples of every uncertain cell and of every cell a SIM.* function
 * names, then recalculates so that those functions see the new results.
 * The same seed gives the same futures.  Returns FALSE if stopped. */
gboolean tm_sheet_simulate (TmSheet *sheet, TmSimProgress progress, gpointer data);

/* The last simulation, or NULL.  Owned by the sheet. */
TmSim   *tm_sheet_get_sim (TmSheet *sheet);

/* Whether the sheet has changed since it was last saved. */
gboolean tm_sheet_modified     (TmSheet *sheet);
void     tm_sheet_set_modified (TmSheet *sheet, gboolean modified);

G_END_DECLS
