/* tm-fn-private.h - shorthand for writing the function library
 *
 * Copyright (C) 2026 The timemachine authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "tm-eval.h"

#include <math.h>

/* Each takes argument i into var, or returns the argument's error from
 * the function; they expect a TmValue named err in scope. */
#define ARG_NUM(i, var) \
  do { if (!tm_arg_number (ctx, &args[i], &(var), &err)) return err; } while (0)
#define ARG_BOOL(i, var) \
  do { if (!tm_arg_bool (ctx, &args[i], &(var), &err)) return err; } while (0)

/* An optional argument: left out, or given. */
#define HAS_ARG(i) ((i) < n && !args[i].missing)
#define OPT_NUM(i, var, dflt) \
  do { if (HAS_ARG (i)) ARG_NUM (i, var); else (var) = (dflt); } while (0)

/* A table entry: name, arguments from min to max, the implementation,
 * flags, category, syntax and one line of help. */
#define FN(name, lo, hi, impl, flags, cat, syntax, help) \
  { name, lo, hi, impl, NULL, flags, cat, syntax, help }
#define LAZY(name, lo, hi, impl, flags, cat, syntax, help) \
  { name, lo, hi, NULL, impl, flags, cat, syntax, help }

/* The draws random functions are built from: a uniform, and a standard
 * normal.  Under Latin hypercube sampling both come from the cell's
 * stratified uniform, the normal through its inverse distribution
 * function, so that the strata carry through. */
static inline gboolean
stratified (TmEvalContext *ctx, double *u)
{
  return ctx->stratified != NULL && ctx->stratified (ctx->data, u);
}

static inline double
draw_uniform (TmEvalContext *ctx)
{
  double u;

  return stratified (ctx, &u) ? u : tm_rng_uniform (ctx->rng);
}

static inline double
draw_normal (TmEvalContext *ctx)
{
  double u;

  return stratified (ctx, &u) ? tm_norm_inv (u) : tm_rng_normal (ctx->rng);
}

static inline TmValue
num_or_error (double d)
{
  return tm_value_number (d);
}

/* The numbers an aggregate asks for, or its error. */
static inline GArray *
collect (TmEvalContext *ctx, TmArg *args, int n, TmValue *err)
{
  GArray *a = g_array_new (FALSE, FALSE, sizeof (double));

  if (!tm_args_numbers (ctx, args, n, a, err))
    {
      g_array_free (a, TRUE);
      return NULL;
    }
  return a;
}

