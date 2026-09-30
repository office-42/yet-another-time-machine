/* tm-fn-sim.c - what the simulation found
 *
 * Copyright (C) 2026 The timemachine authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * After a simulation every uncertain cell has not one value but thousands,
 * one per possible future.  These functions put what those futures have in
 * common back into the sheet: =SIM.PERCENTILE(B9, 0.9) is the profit that
 * nine futures in ten stay under, =SIM.PROB(B9, "<0") the chance of a
 * loss.  Before the first simulation they are #N/A.
 */

#include "tm-fn-private.h"

#include <string.h>

#define S "Simulation"

/* The samples of the cell the first argument names. */
static const double *
samples_of (TmEvalContext *ctx, const TmArg *arg, gboolean sorted, int *n, TmValue *err)
{
  const double *x;

  if (!arg->is_range || arg->range.row0 != arg->range.row1
      || arg->range.col0 != arg->range.col1)
    {
      *err = tm_value_error (TM_ERR_VALUE);
      return NULL;
    }
  x = ctx->samples != NULL
      ? ctx->samples (ctx->data, arg->range.row0, arg->range.col0, sorted, n)
      : NULL;
  if (x == NULL || *n == 0)
    {
      *err = tm_value_error (TM_ERR_NA);
      return NULL;
    }
  return x;
}

static TmValue
moment (TmEvalContext *ctx, TmArg *args, int which)
{
  TmValue err;
  int n;
  const double *x = samples_of (ctx, &args[0], TRUE, &n, &err);
  double mean = 0, ss = 0;

  if (x == NULL)
    return err;
  for (int i = 0; i < n; i++)
    mean += x[i];
  mean /= n;
  if (which == 0)
    return tm_value_number (mean);
  if (n < 2)
    return tm_value_error (TM_ERR_DIV0);
  for (int i = 0; i < n; i++)
    ss += (x[i] - mean) * (x[i] - mean);
  if (which == 1)
    return tm_value_number (sqrt (ss / (n - 1)));
  return tm_value_number (sqrt (ss / (n - 1)) / sqrt (n));   /* standard error */
}

static TmValue fn_mean  (TmEvalContext *c, TmArg *a, int n) { return moment (c, a, 0); }
static TmValue fn_stdev (TmEvalContext *c, TmArg *a, int n) { return moment (c, a, 1); }
static TmValue fn_se    (TmEvalContext *c, TmArg *a, int n) { return moment (c, a, 2); }

static TmValue
quantile (TmEvalContext *ctx, TmArg *args, double p)
{
  TmValue err;
  int n;
  const double *x = samples_of (ctx, &args[0], TRUE, &n, &err);

  if (x == NULL)
    return err;
  return tm_value_number (tm_percentile_sorted (x, n, p));
}

static TmValue fn_min    (TmEvalContext *c, TmArg *a, int n) { return quantile (c, a, 0); }
static TmValue fn_max    (TmEvalContext *c, TmArg *a, int n) { return quantile (c, a, 1); }
static TmValue fn_median (TmEvalContext *c, TmArg *a, int n) { return quantile (c, a, 0.5); }

static TmValue
fn_percentile (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double p;

  ARG_NUM (1, p);
  if (p < 0 || p > 1)
    return tm_value_error (TM_ERR_NUM);
  return quantile (ctx, args, p);
}

static TmValue
fn_count (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  int k;
  const double *x = samples_of (ctx, &args[0], TRUE, &k, &err);

  if (x == NULL)
    return err;
  return tm_value_number (k);
}

/* The fraction of futures that meet the criteria: SIM.PROB(B9, "<0"). */
static TmValue
fn_prob (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err, crit;
  TmCriteria c;
  int k, hits = 0;
  const double *x = samples_of (ctx, &args[0], TRUE, &k, &err);

  if (x == NULL)
    return err;
  crit = tm_arg_scalar (ctx, &args[1]);
  if (crit.type == TM_VALUE_ERROR)
    return crit;
  tm_criteria_parse (&crit, &c);
  tm_value_clear (&crit);
  for (int i = 0; i < k; i++)
    {
      TmValue v = tm_value_number (x[i]);
      hits += tm_criteria_match (&c, &v);
    }
  tm_criteria_clear (&c);
  return tm_value_number ((double) hits / k);
}

/* The mean of the worst fraction p of futures -- expected shortfall, or
 * conditional value at risk: not how bad the 5% case is, but how bad it
 * is on average once there. */
static TmValue
fn_tailmean (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  int k, m;
  double p, s = 0;
  const double *x = samples_of (ctx, &args[0], TRUE, &k, &err);

  if (x == NULL)
    return err;
  OPT_NUM (1, p, 0.05);
  if (p <= 0 || p > 1)
    return tm_value_error (TM_ERR_NUM);
  m = MAX (1, (int) floor (p * k));
  for (int i = 0; i < m; i++)
    s += x[i];
  return tm_value_number (s / m);
}

/* How strongly two cells move together across the futures, by rank
 * (Spearman), which is what a tornado chart ranks inputs by: it does not
 * care whether the relation is a straight line.  Ties share their mean
 * rank.  The engine runs on one thread, so the sort's key can be a
 * static. */
static const double *rank_keys;

static int
compare_by_key (const void *a, const void *b)
{
  double x = rank_keys[*(const int *) a], y = rank_keys[*(const int *) b];
  return x < y ? -1 : x > y;
}

static void
rank_of (const double *x, int n, double *rank)
{
  int *order = g_new (int, n);

  for (int i = 0; i < n; i++)
    order[i] = i;
  rank_keys = x;
  qsort (order, (size_t) n, sizeof (int), compare_by_key);
  for (int i = 0; i < n;)
    {
      int j = i;
      while (j + 1 < n && x[order[j + 1]] == x[order[i]])
        j++;
      for (int k = i; k <= j; k++)
        rank[order[k]] = (i + j) / 2.0;
      i = j + 1;
    }
  g_free (order);
}

static TmValue
fn_correl (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  int na, nb, k = 0;
  const double *a = samples_of (ctx, &args[0], FALSE, &na, &err);
  const double *b;
  double *x, *y, *rx, *ry, mx = 0, my = 0, sxy = 0, sxx = 0, syy = 0;

  if (a == NULL)
    return err;
  b = samples_of (ctx, &args[1], FALSE, &nb, &err);
  if (b == NULL)
    return err;
  if (na != nb)
    return tm_value_error (TM_ERR_NA);

  x = g_new (double, na);
  y = g_new (double, na);
  for (int i = 0; i < na; i++)
    if (!isnan (a[i]) && !isnan (b[i]))
      {
        x[k] = a[i];
        y[k] = b[i];
        k++;
      }
  if (k < 2)
    {
      g_free (x);
      g_free (y);
      return tm_value_error (TM_ERR_DIV0);
    }
  rx = g_new (double, k);
  ry = g_new (double, k);
  rank_of (x, k, rx);
  rank_of (y, k, ry);
  for (int i = 0; i < k; i++)
    {
      mx += rx[i];
      my += ry[i];
    }
  mx /= k;
  my /= k;
  for (int i = 0; i < k; i++)
    {
      sxy += (rx[i] - mx) * (ry[i] - my);
      sxx += (rx[i] - mx) * (rx[i] - mx);
      syy += (ry[i] - my) * (ry[i] - my);
    }
  g_free (x);
  g_free (y);
  g_free (rx);
  g_free (ry);
  if (sxx == 0 || syy == 0)
    return tm_value_error (TM_ERR_DIV0);
  return tm_value_number (sxy / sqrt (sxx * syy));
}

static TmValue
fn_sample (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  int k;
  double i;
  const double *x = samples_of (ctx, &args[0], FALSE, &k, &err);

  if (x == NULL)
    return err;
  ARG_NUM (1, i);
  i = floor (i);
  if (i < 1 || i > k)
    return tm_value_error (TM_ERR_REF);
  if (isnan (x[(int) i - 1]))
    return tm_value_error (TM_ERR_NA);
  return tm_value_number (x[(int) i - 1]);
}

#define SIM TM_FN_SIM

const TmFunction tm_fn_sim[] = {
  FN ("SIM.MEAN", 1, 1, fn_mean, SIM, S, "SIM.MEAN(cell)", "The cell's average over all simulated futures."),
  FN ("SIM.STDEV", 1, 1, fn_stdev, SIM, S, "SIM.STDEV(cell)", "How widely the cell varies across the futures."),
  FN ("SIM.SE", 1, 1, fn_se, SIM, S, "SIM.SE(cell)", "The standard error of SIM.MEAN: how far the simulation's own noise might move it."),
  FN ("SIM.MEDIAN", 1, 1, fn_median, SIM, S, "SIM.MEDIAN(cell)", "The middle future."),
  FN ("SIM.MIN", 1, 1, fn_min, SIM, S, "SIM.MIN(cell)", "The lowest value any future gave."),
  FN ("SIM.MAX", 1, 1, fn_max, SIM, S, "SIM.MAX(cell)", "The highest value any future gave."),
  FN ("SIM.PERCENTILE", 2, 2, fn_percentile, SIM, S, "SIM.PERCENTILE(cell, p)", "The value a fraction p of futures stay under."),
  FN ("SIM.PROB", 2, 2, fn_prob, SIM, S, "SIM.PROB(cell, criteria)", "The chance the cell meets the criteria, such as \"<0\"."),
  FN ("SIM.TAILMEAN", 1, 2, fn_tailmean, SIM, S, "SIM.TAILMEAN(cell, [p])", "The average of the worst p of futures (expected shortfall)."),
  FN ("SIM.CORREL", 2, 2, fn_correl, SIM, S, "SIM.CORREL(input, output)", "Rank correlation across the futures: how much one drives the other."),
  FN ("SIM.COUNT", 1, 1, fn_count, SIM, S, "SIM.COUNT(cell)", "How many futures gave the cell a number."),
  FN ("SIM.SAMPLE", 2, 2, fn_sample, SIM, S, "SIM.SAMPLE(cell, i)", "The cell's value in the i'th future."),
};
const int tm_fn_sim_count = G_N_ELEMENTS (tm_fn_sim);
