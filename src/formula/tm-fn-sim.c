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
  int k, hits = 0, seen = 0;
  const double *x = samples_of (ctx, &args[0], FALSE, &k, &err);

  if (x == NULL)
    return err;
  crit = tm_arg_scalar (ctx, &args[1]);
  if (crit.type == TM_VALUE_ERROR)
    return crit;
  tm_criteria_parse (&crit, &c);
  tm_value_clear (&crit);
  /* The futures of a TRUE/FALSE cell are kept as 1 and 0. */
  if (c.value.type == TM_VALUE_BOOL)
    c.value = tm_value_number (c.value.as.boolean ? 1 : 0);
  for (int i = 0; i < k; i++)
    {
      /* Numbers, and futures that came out as text: SIM.PROB(A30, "rain"). */
      const char *label = isnan (x[i]) && ctx->sample_label != NULL
                          ? ctx->sample_label (ctx->data, x[i]) : NULL;
      TmValue v;

      if (label != NULL)
        v = (TmValue) { TM_VALUE_TEXT, { .text = (char *) label } };
      else if (isnan (x[i]))
        continue;
      else
        v = tm_value_number (x[i]);
      seen++;
      hits += tm_criteria_match (&c, &v);
    }
  tm_criteria_clear (&c);
  if (seen == 0)
    return tm_value_error (TM_ERR_NA);
  return tm_value_number ((double) hits / seen);
}

/* The commonest outcome: for a cell whose futures are text -- a state, a
 * winner, a region -- the likeliest of them. */
static TmValue
fn_mode (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  int k, best = 0;
  const double *x = samples_of (ctx, &args[0], FALSE, &k, &err);
  GHashTable *counts;
  double winner = NAN;

  if (x == NULL)
    return err;
  counts = g_hash_table_new_full (g_int64_hash, g_int64_equal, g_free, NULL);
  for (int i = 0; i < k; i++)
    {
      union { double d; gint64 u; } bits = { x[i] };
      int c;

      if (isnan (x[i]) && (ctx->sample_label == NULL || ctx->sample_label (ctx->data, x[i]) == NULL))
        continue;
      c = GPOINTER_TO_INT (g_hash_table_lookup (counts, &bits.u)) + 1;
      g_hash_table_replace (counts, g_memdup2 (&bits.u, sizeof bits.u), GINT_TO_POINTER (c));
      /* Ties go to the first to reach the count, so the answer does not
       * depend on the order of the table. */
      if (c > best)
        {
          best = c;
          winner = x[i];
        }
    }
  g_hash_table_destroy (counts);
  if (best == 0)
    return tm_value_error (TM_ERR_NA);
  if (isnan (winner))
    return tm_value_text (ctx->sample_label (ctx->data, winner));
  return tm_value_number (winner);
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
 * care whether the relation is a straight line. */
static TmValue
fn_correl (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  int na, nb, k = 0;
  const double *a = samples_of (ctx, &args[0], FALSE, &na, &err);
  const double *b;
  double *x, *y, *rx, *ry, rho;

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
  tm_ranks (x, k, rx);
  tm_ranks (y, k, ry);
  rho = tm_correlation (rx, ry, k);
  g_free (x);
  g_free (y);
  g_free (rx);
  g_free (ry);
  if (isnan (rho))
    return tm_value_error (TM_ERR_DIV0);
  return tm_value_number (rho);
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

/* ---- Scoring the simulation against what happened ----------------------- */

/* The continuous ranked probability score of the cell's futures, once the
 * outcome is known: how far the whole forecast distribution was from it.
 * In the cell's units; for a forecast that was a single number, the
 * absolute error.  Averaged over many forecasts, it ranks methods. */
static TmValue
fn_sim_crps (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  int k;
  double y;
  gboolean fair = FALSE;
  const double *x = samples_of (ctx, &args[0], TRUE, &k, &err);

  if (x == NULL)
    return err;
  ARG_NUM (1, y);
  if (HAS_ARG (2))
    ARG_BOOL (2, fair);
  return tm_value_number (tm_crps_sorted (x, k, y, fair));
}

/* Where the outcome fell among the futures, from 0 to 1: the probability
 * integral transform.  Over many forecasts these should be spread evenly;
 * piled at the ends, the forecasts were too sure; in the middle, not sure
 * enough.  Ties count half. */
static TmValue
fn_sim_pit (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  int k, below = 0, equal = 0;
  double y;
  const double *x = samples_of (ctx, &args[0], TRUE, &k, &err);

  if (x == NULL)
    return err;
  ARG_NUM (1, y);
  for (int i = 0; i < k; i++)
    {
      below += x[i] < y;
      equal += x[i] == y;
    }
  return tm_value_number ((below + 0.5 * (1 + equal)) / (k + 1));
}

/* ---- Decisions ----------------------------------------------------------- */

/* The futures of several options, one value each: the options are cells
 * holding what each choice is worth, worked out in the same futures, so
 * that future i of one and of another are the same world.  Futures where
 * any option has no number are left out.  values[d * *s + i]. */
static double *
options_of (TmEvalContext *ctx, TmArg *args, int first, int n, int *d, int *s, TmValue *err)
{
  const double **x;
  double *out;
  int len = -1, k = 0;

  *d = n - first;
  if (*d < 1)
    {
      *err = tm_value_error (TM_ERR_VALUE);
      return NULL;
    }
  x = g_new (const double *, *d);
  for (int j = 0; j < *d; j++)
    {
      int m;

      x[j] = samples_of (ctx, &args[first + j], FALSE, &m, err);
      if (x[j] == NULL || (len >= 0 && m != len))
        {
          if (x[j] != NULL)
            *err = tm_value_error (TM_ERR_NA);
          g_free (x);
          return NULL;
        }
      len = m;
    }
  out = g_new (double, (gsize) *d * len);
  for (int i = 0; i < len; i++)
    {
      gboolean ok = TRUE;

      for (int j = 0; j < *d && ok; j++)
        ok = !isnan (x[j][i]);
      if (ok)
        {
          for (int j = 0; j < *d; j++)
            out[(gsize) j * len + k] = x[j][i];
          k++;
        }
    }
  /* Close the gaps: option j's futures start at j * k. */
  for (int j = 1; j < *d; j++)
    memmove (out + (gsize) j * k, out + (gsize) j * len, sizeof (double) * k);
  g_free (x);
  *s = k;
  if (k == 0)
    {
      g_free (out);
      *err = tm_value_error (TM_ERR_DIV0);
      return NULL;
    }
  return out;
}

/* The expected value of perfect information: how much better, on average,
 * one could do by choosing after seeing the future than by choosing the
 * best option now.  The most any study, survey or wait could be worth. */
static TmValue
fn_sim_evpi (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  int d, s;
  double *v = options_of (ctx, args, 0, n, &d, &s, &err), best_mean = -INFINITY, mean_best = 0;

  if (v == NULL)
    return err;
  for (int j = 0; j < d; j++)
    {
      double m = 0;
      for (int i = 0; i < s; i++)
        m += v[(gsize) j * s + i] / s;
      best_mean = MAX (best_mean, m);
    }
  for (int i = 0; i < s; i++)
    {
      double best = -INFINITY;
      for (int j = 0; j < d; j++)
        best = MAX (best, v[(gsize) j * s + i]);
      mean_best += best / s;
    }
  g_free (v);
  return tm_value_number (MAX (0.0, mean_best - best_mean));
}

/* The chance that option k turns out the best of them; ties share. */
static TmValue
fn_sim_pbest (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  int d, s, k;
  double dk, *v, wins = 0;

  ARG_NUM (0, dk);
  if ((v = options_of (ctx, args, 1, n, &d, &s, &err)) == NULL)
    return err;
  k = (int) floor (dk);
  if (dk < 1 || dk > d)
    {
      g_free (v);
      return tm_value_error (TM_ERR_REF);
    }
  k--;
  for (int i = 0; i < s; i++)
    {
      double best = -INFINITY;
      int ties = 0;

      for (int j = 0; j < d; j++)
        best = MAX (best, v[(gsize) j * s + i]);
      for (int j = 0; j < d; j++)
        ties += v[(gsize) j * s + i] == best;
      if (v[(gsize) k * s + i] == best)
        wins += 1.0 / ties;
    }
  g_free (v);
  return tm_value_number (wins / s);
}

/* The certainty equivalent under exponential utility: the sure amount
 * someone with this risk tolerance would take instead of the gamble.  A
 * tolerance about a sixth of equity, as Howard suggested, is a start; the
 * larger it is, the nearer the mean. */
static TmValue
fn_sim_ce (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  int k;
  double r, top = -INFINITY, s = 0;
  const double *x = samples_of (ctx, &args[0], TRUE, &k, &err);

  if (x == NULL)
    return err;
  ARG_NUM (1, r);
  if (!(r > 0))
    return tm_value_error (TM_ERR_NUM);
  /* Log-sum-exp, from the worst future, which dominates. */
  for (int i = 0; i < k; i++)
    top = MAX (top, -x[i] / r);
  for (int i = 0; i < k; i++)
    s += exp (-x[i] / r - top);
  return tm_value_number (-r * (top + log (s / k)));
}

typedef struct {
  double key;
  int    i;
} Order;

static int
by_key (const void *pa, const void *pb)
{
  const Order *a = pa, *b = pb;

  if (a->key != b->key)
    return a->key < b->key ? -1 : 1;
  return a->i - b->i;
}

/* The futures sorted by an input, cut into bins of equal count: for what
 * the rest of the model does, given the input. */
static Order *
binned_by (const double *input, int s, int *bins)
{
  Order *o = g_new (Order, s);

  for (int i = 0; i < s; i++)
    {
      o[i].key = input[i];
      o[i].i = i;
    }
  qsort (o, s, sizeof *o, by_key);
  if (*bins < 1)
    *bins = MAX (2, (int) floor (sqrt (s) / 2));
  *bins = MIN (*bins, s);
  return o;
}

/* The expected value of partial perfect information about one input:
 * what learning just it before choosing would be worth.  Strong and
 * Oakley's method, from the futures already simulated. */
static TmValue
fn_sim_evppi (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  int d, s, bins = 0;
  double *v = options_of (ctx, args, 0, n, &d, &s, &err), *means, best_mean = -INFINITY, sum = 0;
  Order *o;

  if (v == NULL)
    return err;
  if (d < 2)
    {
      g_free (v);
      return tm_value_error (TM_ERR_VALUE);
    }
  /* Option 0 in v is the input. */
  o = binned_by (v, s, &bins);
  means = g_new0 (double, d);
  for (int b = 0; b < bins; b++)
    {
      int lo = (int) ((gint64) b * s / bins), hi = (int) ((gint64) (b + 1) * s / bins);
      double best = -INFINITY;

      for (int j = 1; j < d; j++)
        {
          double m = 0;
          for (int i = lo; i < hi; i++)
            m += v[(gsize) j * s + o[i].i];
          best = MAX (best, m / (hi - lo));
          means[j] += m / s;
        }
      sum += best * (hi - lo) / s;
    }
  for (int j = 1; j < d; j++)
    best_mean = MAX (best_mean, means[j]);
  g_free (means);
  g_free (o);
  g_free (v);
  return tm_value_number (MAX (0.0, sum - best_mean));
}

/* The share of the output's variance the input explains on its own -- the
 * first-order Sobol index -- from the futures already simulated, by
 * averaging the output over bins of the input and taking off what the
 * bins' own noise would give. */
static TmValue
fn_sim_sobol (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  int d, s, bins = 0;
  double *v, mean = 0, total = 0, between = 0, within = 0;
  Order *o;

  if (HAS_ARG (2))
    {
      double db;
      ARG_NUM (2, db);
      if (db < 2)
        return tm_value_error (TM_ERR_NUM);
      bins = (int) MIN (db, 1e6);
    }
  if ((v = options_of (ctx, args, 0, 2, &d, &s, &err)) == NULL)
    return err;
  for (int i = 0; i < s; i++)
    mean += v[s + i] / s;
  for (int i = 0; i < s; i++)
    total += (v[s + i] - mean) * (v[s + i] - mean) / s;
  o = binned_by (v, s, &bins);
  for (int b = 0; b < bins; b++)
    {
      int lo = (int) ((gint64) b * s / bins), hi = (int) ((gint64) (b + 1) * s / bins);
      double m = 0;

      for (int i = lo; i < hi; i++)
        m += v[s + o[i].i] / (hi - lo);
      between += (hi - lo) * (m - mean) * (m - mean) / s;
      for (int i = lo; i < hi; i++)
        within += (v[s + o[i].i] - m) * (v[s + o[i].i] - m);
    }
  g_free (o);
  g_free (v);
  if (!(total > 0))
    return tm_value_error (TM_ERR_DIV0);
  within /= MAX (1, s - bins);
  return tm_value_number (CLAMP ((between - (bins - 1) * within / s) / total, 0.0, 1.0));
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
  FN ("SIM.MODE", 1, 1, fn_mode, SIM, S, "SIM.MODE(cell)", "The commonest outcome: for text futures, such as a state or a winner, the likeliest."),
  FN ("SIM.CORREL", 2, 2, fn_correl, SIM, S, "SIM.CORREL(input, output)", "Rank correlation across the futures: how much one drives the other."),
  FN ("SIM.COUNT", 1, 1, fn_count, SIM, S, "SIM.COUNT(cell)", "How many futures gave the cell a number."),
  FN ("SIM.SAMPLE", 2, 2, fn_sample, SIM, S, "SIM.SAMPLE(cell, i)", "The cell's value in the i'th future."),
  FN ("SIM.CRPS", 2, 3, fn_sim_crps, SIM, S, "SIM.CRPS(cell, observed, [fair])", "How far the simulated forecast was from what happened: lower is better."),
  FN ("SIM.PIT", 2, 2, fn_sim_pit, SIM, S, "SIM.PIT(cell, observed)", "Where what happened fell among the futures, 0 to 1: even over many, if calibrated."),
  FN ("SIM.EVPI", 1, -1, fn_sim_evpi, SIM, S, "SIM.EVPI(option1, option2, ...)", "What knowing the future before choosing between the options would be worth."),
  FN ("SIM.PBEST", 2, -1, fn_sim_pbest, SIM, S, "SIM.PBEST(k, option1, option2, ...)", "The chance that option k turns out the best."),
  FN ("SIM.CE", 2, 2, fn_sim_ce, SIM, S, "SIM.CE(cell, risk_tolerance)", "The sure amount worth as much as the gamble, to someone this averse to risk."),
  FN ("SIM.EVPPI", 3, -1, fn_sim_evppi, SIM, S, "SIM.EVPPI(input, option1, option2, ...)", "What learning just this input before choosing would be worth."),
  FN ("SIM.SOBOL", 2, 3, fn_sim_sobol, SIM, S, "SIM.SOBOL(input, output, [bins])", "The share of the output's variance the input explains on its own."),
};
const int tm_fn_sim_count = G_N_ELEMENTS (tm_fn_sim);
