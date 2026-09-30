/* tm-eval.h - evaluating formulas, and the function library's interface
 *
 * Copyright (C) 2026 The timemachine authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The evaluator does not know what a sheet is.  It asks a callback for the
 * value of a cell, and another for the samples a simulation left behind,
 * so that the formula engine can be driven by anything -- the sheet, the
 * terminal front-end, a simulation.
 */

#pragma once

#include "tm-formula.h"
#include "tm-rng.h"
#include "tm-stats.h"

G_BEGIN_DECLS

typedef struct _TmEvalContext TmEvalContext;

struct _TmEvalContext {
  /* The value of a cell, or NULL for an empty one.  The pointer is
   * borrowed, and good for as long as the evaluation that asked. */
  const TmValue *(*cell) (gpointer data, int row, int col);
  /* The samples the last simulation kept for a cell: one per iteration,
   * NaN where an iteration gave no number, or -- if sorted -- only the
   * numbers, in ascending order.  NULL if there are none. */
  const double  *(*samples) (gpointer data, int row, int col, gboolean sorted, int *n);
  gpointer data;

  TmRng   *rng;
  /* Set when a random function has run, so that a cell can learn whether
   * its value is a draw from a distribution or a plain fact. */
  gboolean random;
  int row, col;          /* the cell whose formula this is */
};

/* An argument, as a function sees it: a value already worked out, or --
 * for a reference or a range -- where the values are, so that SUM(A1:A9)
 * can skip text the way Excel's does while SUM("3") counts it. */
typedef struct {
  gboolean  is_range;    /* a reference or a range: see range */
  gboolean  is_ref;      /* a single-cell reference */
  gboolean  missing;     /* left out: F(a,,c) */
  TmRange   range;
  TmValue   value;       /* when !is_range; owned */
} TmArg;

typedef TmValue (*TmFunctionImpl) (TmEvalContext *ctx, TmArg *args, int n);
/* For IF, IFERROR and CHOOSE, which must not evaluate what they do not
 * use -- above all not draw random numbers for a branch not taken. */
typedef TmValue (*TmLazyImpl) (TmEvalContext *ctx, TmNode **args, int n);

enum {
  TM_FN_RANDOM = 1 << 0,   /* draws from the generator */
  TM_FN_SIM    = 1 << 1    /* reads the last simulation's samples */
};

struct _TmFunction {
  const char     *name;
  int             min_args;
  int             max_args;      /* -1: as many as given */
  TmFunctionImpl  impl;
  TmLazyImpl      lazy;
  guint           flags;
  const char     *category;
  const char     *syntax;
  const char     *help;
};

TmValue tm_eval (TmEvalContext *ctx, const TmNode *node);

const TmFunction *tm_function_lookup (const char *name);
/* Every function, sorted by name, for Help > Functions. */
const TmFunction *const *tm_function_list (int *n);

/* ---- For the function library ----------------------------------------- */

/* A cell's value, never NULL: an empty cell is an empty value. */
const TmValue *tm_ctx_cell (TmEvalContext *ctx, int row, int col);

/* The argument as one value: a reference's cell, or the value itself.  A
 * range of more than one cell is #VALUE!.  The result is a copy. */
TmValue  tm_arg_scalar (TmEvalContext *ctx, const TmArg *arg);

/* The argument as a number, or FALSE with *err set to the error to
 * return. */
gboolean tm_arg_number (TmEvalContext *ctx, const TmArg *arg, double *out, TmValue *err);
gboolean tm_arg_bool   (TmEvalContext *ctx, const TmArg *arg, gboolean *out, TmValue *err);
char    *tm_arg_text   (TmEvalContext *ctx, const TmArg *arg, TmValue *err);

/* The numbers in the arguments, the way SUM and AVERAGE collect them:
 * numbers in cells, but not text or logical values there; anything typed
 * straight in that can be a number.  An error anywhere is the answer. */
gboolean tm_args_numbers (TmEvalContext *ctx, TmArg *args, int n, GArray *out, TmValue *err);

/* Every cell of a range argument, row by row, as borrowed values; a plain
 * value is a range of one.  Free the array (not the values) with
 * g_free. */
const TmValue **tm_arg_cells (TmEvalContext *ctx, const TmArg *arg, int *n);

/* Two ranges of numbers taken in pairs, as SLOPE and CORREL want them:
 * a pair is kept only when both halves are numbers.  #N/A if the ranges
 * differ in size. */
gboolean tm_arg_pairs (TmEvalContext *ctx, const TmArg *ys, const TmArg *xs,
                       GArray *y_out, GArray *x_out, TmValue *err);

/* COUNTIF-style criteria: ">=10", "<>0", "5", "apple". */
typedef struct {
  TmOp    op;
  TmValue value;
} TmCriteria;
void     tm_criteria_parse (const TmValue *criteria, TmCriteria *out);
gboolean tm_criteria_match (const TmCriteria *criteria, const TmValue *value);
void     tm_criteria_clear (TmCriteria *criteria);

/* The libraries, one table each, in tm-fn-*.c. */
extern const TmFunction tm_fn_core[];
extern const int        tm_fn_core_count;
extern const TmFunction tm_fn_stats[];
extern const int        tm_fn_stats_count;
extern const TmFunction tm_fn_random[];
extern const int        tm_fn_random_count;
extern const TmFunction tm_fn_forecast[];
extern const int        tm_fn_forecast_count;
extern const TmFunction tm_fn_sim[];
extern const int        tm_fn_sim_count;

G_END_DECLS
