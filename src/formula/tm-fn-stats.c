/* tm-fn-stats.c - statistics, and the distributions' formulas
 *
 * Copyright (C) 2026 The timemachine authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Describing the past is the first step to guessing at the future: the
 * averages, spreads and percentiles of what has already happened, and
 * the distribution functions the random draws are the inverse of.
 */

#include "tm-fn-private.h"

#include <stdlib.h>

#define S "Statistics"
#define D "Distributions"

static double
mean_of (const double *x, guint n)
{
  double s = 0;

  for (guint i = 0; i < n; i++)
    s += x[i];
  return s / n;
}

/* Two passes: the mean first, then squares about it, which keeps the
 * cancellation of the one-pass formula out of the answer. */
static double
sumsq_of (const double *x, guint n)
{
  double m = mean_of (x, n), s = 0;

  for (guint i = 0; i < n; i++)
    s += (x[i] - m) * (x[i] - m);
  return s;
}

static TmValue
fn_average (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  GArray *a = collect (ctx, args, n, &err);
  TmValue r;

  if (a == NULL)
    return err;
  r = a->len == 0 ? tm_value_error (TM_ERR_DIV0)
                  : tm_value_number (mean_of ((double *) a->data, a->len));
  g_array_free (a, TRUE);
  return r;
}

static TmValue
extreme (TmEvalContext *ctx, TmArg *args, int n, gboolean max)
{
  TmValue err;
  GArray *a = collect (ctx, args, n, &err);
  double best = 0;

  if (a == NULL)
    return err;
  for (guint i = 0; i < a->len; i++)
    {
      double x = g_array_index (a, double, i);
      if (i == 0 || (max ? x > best : x < best))
        best = x;
    }
  g_array_free (a, TRUE);
  return tm_value_number (best);
}

static TmValue fn_min (TmEvalContext *c, TmArg *a, int n) { return extreme (c, a, n, FALSE); }
static TmValue fn_max (TmEvalContext *c, TmArg *a, int n) { return extreme (c, a, n, TRUE); }

static TmValue
fn_count (TmEvalContext *ctx, TmArg *args, int n)
{
  double count = 0;

  for (int i = 0; i < n; i++)
    {
      int k;
      const TmValue **cells = tm_arg_cells (ctx, &args[i], &k);

      for (int j = 0; j < k; j++)
        {
          const TmValue *v = cells[j];
          double d;
          TmErrorCode code;

          if (v->type == TM_VALUE_NUMBER)
            count++;
          else if (!args[i].is_range && v->type != TM_VALUE_ERROR
                   && tm_value_to_number (v, &d, &code) && v->type != TM_VALUE_EMPTY)
            count++;
        }
      g_free (cells);
    }
  return tm_value_number (count);
}

static TmValue
fn_counta (TmEvalContext *ctx, TmArg *args, int n)
{
  double count = 0;

  for (int i = 0; i < n; i++)
    {
      int k;
      const TmValue **cells = tm_arg_cells (ctx, &args[i], &k);

      for (int j = 0; j < k; j++)
        count += cells[j]->type != TM_VALUE_EMPTY;
      g_free (cells);
    }
  return tm_value_number (count);
}

/* The spread of the numbers; sample says whether to divide by n - 1. */
static TmValue
spread (TmEvalContext *ctx, TmArg *args, int n, gboolean sample, gboolean root)
{
  TmValue err;
  GArray *a = collect (ctx, args, n, &err);
  guint k;
  double v;

  if (a == NULL)
    return err;
  k = a->len;
  if (k < (sample ? 2u : 1u))
    {
      g_array_free (a, TRUE);
      return tm_value_error (TM_ERR_DIV0);
    }
  v = sumsq_of ((double *) a->data, k) / (sample ? k - 1 : k);
  g_array_free (a, TRUE);
  return tm_value_number (root ? sqrt (v) : v);
}

static TmValue fn_stdev_s (TmEvalContext *c, TmArg *a, int n) { return spread (c, a, n, TRUE, TRUE); }
static TmValue fn_stdev_p (TmEvalContext *c, TmArg *a, int n) { return spread (c, a, n, FALSE, TRUE); }
static TmValue fn_var_s   (TmEvalContext *c, TmArg *a, int n) { return spread (c, a, n, TRUE, FALSE); }
static TmValue fn_var_p   (TmEvalContext *c, TmArg *a, int n) { return spread (c, a, n, FALSE, FALSE); }

static TmValue
fn_median (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err, r;
  GArray *a = collect (ctx, args, n, &err);

  if (a == NULL)
    return err;
  if (a->len == 0)
    r = tm_value_error (TM_ERR_NUM);
  else
    {
      qsort (a->data, a->len, sizeof (double), tm_compare_doubles);
      r = tm_value_number (tm_percentile_sorted ((double *) a->data, (int) a->len, 0.5));
    }
  g_array_free (a, TRUE);
  return r;
}

static TmValue
fn_percentile (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err, r;
  GArray *a;
  double p;

  ARG_NUM (1, p);
  if (p < 0 || p > 1)
    return tm_value_error (TM_ERR_NUM);
  a = collect (ctx, args, 1, &err);
  if (a == NULL)
    return err;
  if (a->len == 0)
    r = tm_value_error (TM_ERR_NUM);
  else
    {
      qsort (a->data, a->len, sizeof (double), tm_compare_doubles);
      r = tm_value_number (tm_percentile_sorted ((double *) a->data, (int) a->len, p));
    }
  g_array_free (a, TRUE);
  return r;
}

static TmValue
fn_quartile (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err, r;
  GArray *a;
  double q;

  ARG_NUM (1, q);
  q = floor (q);
  if (q < 0 || q > 4)
    return tm_value_error (TM_ERR_NUM);
  a = collect (ctx, args, 1, &err);
  if (a == NULL)
    return err;
  if (a->len == 0)
    r = tm_value_error (TM_ERR_NUM);
  else
    {
      qsort (a->data, a->len, sizeof (double), tm_compare_doubles);
      r = tm_value_number (tm_percentile_sorted ((double *) a->data, (int) a->len, q / 4));
    }
  g_array_free (a, TRUE);
  return r;
}

static TmValue
large_small (TmEvalContext *ctx, TmArg *args, int n, gboolean large)
{
  TmValue err, r;
  GArray *a;
  double k;

  ARG_NUM (1, k);
  a = collect (ctx, args, 1, &err);
  if (a == NULL)
    return err;
  if (k < 1 || k > a->len)
    r = tm_value_error (TM_ERR_NUM);
  else
    {
      int i = (int) ceil (k) - 1;

      qsort (a->data, a->len, sizeof (double), tm_compare_doubles);
      r = tm_value_number (g_array_index (a, double, large ? (int) a->len - 1 - i : i));
    }
  g_array_free (a, TRUE);
  return r;
}

static TmValue fn_large (TmEvalContext *c, TmArg *a, int n) { return large_small (c, a, n, TRUE); }
static TmValue fn_small (TmEvalContext *c, TmArg *a, int n) { return large_small (c, a, n, FALSE); }

/* Pearson's r and the sample covariance over pairs. */
static TmValue
pair_stat (TmEvalContext *ctx, TmArg *args, int which)
{
  TmValue err;
  GArray *y = g_array_new (FALSE, FALSE, sizeof (double));
  GArray *x = g_array_new (FALSE, FALSE, sizeof (double));
  double my, mx, sxy = 0, sxx = 0, syy = 0;
  guint k;
  TmValue r;

  if (!tm_arg_pairs (ctx, &args[0], &args[1], y, x, &err))
    {
      g_array_free (y, TRUE);
      g_array_free (x, TRUE);
      return err;
    }
  k = y->len;
  if (k < 2)
    {
      g_array_free (y, TRUE);
      g_array_free (x, TRUE);
      return tm_value_error (TM_ERR_DIV0);
    }
  my = mean_of ((double *) y->data, k);
  mx = mean_of ((double *) x->data, k);
  for (guint i = 0; i < k; i++)
    {
      double dy = g_array_index (y, double, i) - my;
      double dx = g_array_index (x, double, i) - mx;
      sxy += dx * dy;
      sxx += dx * dx;
      syy += dy * dy;
    }
  if (which == 0)
    r = (sxx == 0 || syy == 0) ? tm_value_error (TM_ERR_DIV0)
                               : tm_value_number (sxy / sqrt (sxx * syy));
  else
    r = tm_value_number (sxy / (k - 1));
  g_array_free (y, TRUE);
  g_array_free (x, TRUE);
  return r;
}

static TmValue fn_correl (TmEvalContext *c, TmArg *a, int n) { return pair_stat (c, a, 0); }
static TmValue fn_covar  (TmEvalContext *c, TmArg *a, int n) { return pair_stat (c, a, 1); }

/* ---- Distribution functions ------------------------------------------- */

static TmValue
fn_norm_dist (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double x, mean, sd;
  gboolean cumulative;

  ARG_NUM (0, x);
  ARG_NUM (1, mean);
  ARG_NUM (2, sd);
  ARG_BOOL (3, cumulative);
  if (sd <= 0)
    return tm_value_error (TM_ERR_NUM);
  if (cumulative)
    return tm_value_number (tm_norm_cdf ((x - mean) / sd));
  return tm_value_number (exp (-0.5 * pow ((x - mean) / sd, 2)) / (sd * sqrt (2 * G_PI)));
}

static TmValue
fn_norm_s_dist (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double z;
  gboolean cumulative = TRUE;

  ARG_NUM (0, z);
  if (HAS_ARG (1))
    ARG_BOOL (1, cumulative);
  if (cumulative)
    return tm_value_number (tm_norm_cdf (z));
  return tm_value_number (exp (-0.5 * z * z) / sqrt (2 * G_PI));
}

static TmValue
fn_norm_inv (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double p, mean, sd;

  ARG_NUM (0, p);
  ARG_NUM (1, mean);
  ARG_NUM (2, sd);
  if (p <= 0 || p >= 1 || sd <= 0)
    return tm_value_error (TM_ERR_NUM);
  return tm_value_number (mean + sd * tm_norm_inv (p));
}

static TmValue
fn_norm_s_inv (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double p;

  ARG_NUM (0, p);
  if (p <= 0 || p >= 1)
    return tm_value_error (TM_ERR_NUM);
  return tm_value_number (tm_norm_inv (p));
}

static TmValue
fn_poisson_dist (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double x, mean;
  gboolean cumulative;

  ARG_NUM (0, x);
  ARG_NUM (1, mean);
  ARG_BOOL (2, cumulative);
  x = floor (x);
  if (x < 0 || mean < 0)
    return tm_value_error (TM_ERR_NUM);
  if (!cumulative)
    return tm_value_number (exp (x * log (mean) - mean - lgamma (x + 1)));
  {
    double term = exp (-mean), sum = term;
    for (int k = 1; k <= (int) x; k++)
      {
        term *= mean / k;
        sum += term;
      }
    return tm_value_number (MIN (sum, 1.0));
  }
}

static TmValue
fn_binom_dist (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double k, trials, p;
  gboolean cumulative;

  ARG_NUM (0, k);
  ARG_NUM (1, trials);
  ARG_NUM (2, p);
  ARG_BOOL (3, cumulative);
  k = floor (k);
  trials = floor (trials);
  if (k < 0 || k > trials || p < 0 || p > 1)
    return tm_value_error (TM_ERR_NUM);
  {
    double sum = 0;
    int from = cumulative ? 0 : (int) k;

    for (int j = from; j <= (int) k; j++)
      {
        double lc = lgamma (trials + 1) - lgamma (j + 1.0) - lgamma (trials - j + 1);
        double term;

        if (p == 0)
          term = j == 0 ? 1 : 0;
        else if (p == 1)
          term = j == trials ? 1 : 0;
        else
          term = exp (lc + j * log (p) + (trials - j) * log1p (-p));
        sum += term;
      }
    return tm_value_number (MIN (sum, 1.0));
  }
}

static TmValue
fn_expon_dist (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double x, lambda;
  gboolean cumulative;

  ARG_NUM (0, x);
  ARG_NUM (1, lambda);
  ARG_BOOL (2, cumulative);
  if (x < 0 || lambda <= 0)
    return tm_value_error (TM_ERR_NUM);
  return tm_value_number (cumulative ? 1 - exp (-lambda * x) : lambda * exp (-lambda * x));
}

static TmValue
fn_beta_dist (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double x, a, b, lo, hi;
  gboolean cumulative;

  ARG_NUM (0, x);
  ARG_NUM (1, a);
  ARG_NUM (2, b);
  ARG_BOOL (3, cumulative);
  OPT_NUM (4, lo, 0);
  OPT_NUM (5, hi, 1);
  if (a <= 0 || b <= 0 || hi <= lo || x < lo || x > hi)
    return tm_value_error (TM_ERR_NUM);
  x = (x - lo) / (hi - lo);
  if (cumulative)
    return tm_value_number (tm_beta_inc (a, b, x));
  if (x <= 0 || x >= 1)
    return tm_value_number (0);
  return tm_value_number (exp ((a - 1) * log (x) + (b - 1) * log1p (-x)
                               + lgamma (a + b) - lgamma (a) - lgamma (b)) / (hi - lo));
}

static TmValue
fn_beta_inv (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double p, a, b, lo, hi;

  ARG_NUM (0, p);
  ARG_NUM (1, a);
  ARG_NUM (2, b);
  OPT_NUM (3, lo, 0);
  OPT_NUM (4, hi, 1);
  if (p <= 0 || p > 1 || a <= 0 || b <= 0 || hi <= lo)
    return tm_value_error (TM_ERR_NUM);
  return tm_value_number (lo + (hi - lo) * tm_beta_inv (p, a, b));
}

static TmValue
fn_gamma_dist (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double x, a, scale;
  gboolean cumulative;

  ARG_NUM (0, x);
  ARG_NUM (1, a);
  ARG_NUM (2, scale);
  ARG_BOOL (3, cumulative);
  if (x < 0 || a <= 0 || scale <= 0)
    return tm_value_error (TM_ERR_NUM);
  if (cumulative)
    return tm_value_number (tm_gamma_p (a, x / scale));
  if (x == 0)
    return tm_value_number (a == 1 ? 1 / scale : 0);
  return tm_value_number (exp ((a - 1) * log (x / scale) - x / scale - lgamma (a)) / scale);
}

static TmValue
fn_gamma_inv (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double p, a, scale;

  ARG_NUM (0, p);
  ARG_NUM (1, a);
  ARG_NUM (2, scale);
  if (p < 0 || p >= 1 || a <= 0 || scale <= 0)
    return tm_value_error (TM_ERR_NUM);
  return tm_value_number (scale * tm_gamma_inv (p, a));
}

static TmValue
fn_t_dist (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double t, df;
  gboolean cumulative;

  ARG_NUM (0, t);
  ARG_NUM (1, df);
  ARG_BOOL (2, cumulative);
  if (df < 1)
    return tm_value_error (TM_ERR_NUM);
  if (cumulative)
    return tm_value_number (tm_t_cdf (t, df));
  return tm_value_number (exp (lgamma ((df + 1) / 2) - lgamma (df / 2) - 0.5 * log (df * G_PI)
                               - (df + 1) / 2 * log1p (t * t / df)));
}

static TmValue
fn_t_inv (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double p, df;

  ARG_NUM (0, p);
  ARG_NUM (1, df);
  if (p <= 0 || p >= 1 || df < 1)
    return tm_value_error (TM_ERR_NUM);
  return tm_value_number (tm_t_inv (p, df));
}

static TmValue
fn_t_inv_2t (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double p, df;

  ARG_NUM (0, p);
  ARG_NUM (1, df);
  if (p <= 0 || p > 1 || df < 1)
    return tm_value_error (TM_ERR_NUM);
  return tm_value_number (tm_t_inv (1 - p / 2, df));
}

static TmValue
fn_lognorm_dist (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double x, mu, sigma;
  gboolean cumulative;

  ARG_NUM (0, x);
  ARG_NUM (1, mu);
  ARG_NUM (2, sigma);
  ARG_BOOL (3, cumulative);
  if (x <= 0 || sigma <= 0)
    return tm_value_error (TM_ERR_NUM);
  if (cumulative)
    return tm_value_number (tm_norm_cdf ((log (x) - mu) / sigma));
  return tm_value_number (exp (-0.5 * pow ((log (x) - mu) / sigma, 2)) / (x * sigma * sqrt (2 * G_PI)));
}

static TmValue
fn_lognorm_inv (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double p, mu, sigma;

  ARG_NUM (0, p);
  ARG_NUM (1, mu);
  ARG_NUM (2, sigma);
  if (p <= 0 || p >= 1 || sigma <= 0)
    return tm_value_error (TM_ERR_NUM);
  return tm_value_number (exp (mu + sigma * tm_norm_inv (p)));
}

const TmFunction tm_fn_stats[] = {
  FN ("AVERAGE", 1, -1, fn_average, 0, S, "AVERAGE(number, ...)", "The arithmetic mean."),
  FN ("MIN", 1, -1, fn_min, 0, S, "MIN(number, ...)", "The smallest number."),
  FN ("MAX", 1, -1, fn_max, 0, S, "MAX(number, ...)", "The largest number."),
  FN ("COUNT", 1, -1, fn_count, 0, S, "COUNT(value, ...)", "How many numbers there are."),
  FN ("COUNTA", 1, -1, fn_counta, 0, S, "COUNTA(value, ...)", "How many cells are not empty."),
  FN ("MEDIAN", 1, -1, fn_median, 0, S, "MEDIAN(number, ...)", "The middle number."),
  FN ("STDEV", 1, -1, fn_stdev_s, 0, S, "STDEV(number, ...)", "Standard deviation of a sample."),
  FN ("STDEV.S", 1, -1, fn_stdev_s, 0, S, "STDEV.S(number, ...)", "Standard deviation of a sample."),
  FN ("STDEV.P", 1, -1, fn_stdev_p, 0, S, "STDEV.P(number, ...)", "Standard deviation of a whole population."),
  FN ("VAR", 1, -1, fn_var_s, 0, S, "VAR(number, ...)", "Variance of a sample."),
  FN ("VAR.S", 1, -1, fn_var_s, 0, S, "VAR.S(number, ...)", "Variance of a sample."),
  FN ("VAR.P", 1, -1, fn_var_p, 0, S, "VAR.P(number, ...)", "Variance of a whole population."),
  FN ("PERCENTILE", 2, 2, fn_percentile, 0, S, "PERCENTILE(range, p)", "The value below which a fraction p of the numbers lie."),
  FN ("PERCENTILE.INC", 2, 2, fn_percentile, 0, S, "PERCENTILE.INC(range, p)", "The value below which a fraction p of the numbers lie."),
  FN ("QUARTILE", 2, 2, fn_quartile, 0, S, "QUARTILE(range, quart)", "Minimum, quartiles and maximum: quart 0 to 4."),
  FN ("LARGE", 2, 2, fn_large, 0, S, "LARGE(range, k)", "The k'th largest number."),
  FN ("SMALL", 2, 2, fn_small, 0, S, "SMALL(range, k)", "The k'th smallest number."),
  FN ("CORREL", 2, 2, fn_correl, 0, S, "CORREL(range1, range2)", "Pearson's correlation coefficient."),
  FN ("COVARIANCE.S", 2, 2, fn_covar, 0, S, "COVARIANCE.S(range1, range2)", "The sample covariance."),

  FN ("NORM.DIST", 4, 4, fn_norm_dist, 0, D, "NORM.DIST(x, mean, sd, cumulative)", "The normal distribution's density or distribution function."),
  FN ("NORMDIST", 4, 4, fn_norm_dist, 0, D, "NORMDIST(x, mean, sd, cumulative)", "The normal distribution's density or distribution function."),
  FN ("NORM.S.DIST", 1, 2, fn_norm_s_dist, 0, D, "NORM.S.DIST(z, [cumulative])", "The standard normal distribution."),
  FN ("NORM.INV", 3, 3, fn_norm_inv, 0, D, "NORM.INV(p, mean, sd)", "The value with probability p below it."),
  FN ("NORMINV", 3, 3, fn_norm_inv, 0, D, "NORMINV(p, mean, sd)", "The value with probability p below it."),
  FN ("NORM.S.INV", 1, 1, fn_norm_s_inv, 0, D, "NORM.S.INV(p)", "The standard normal quantile."),
  FN ("POISSON.DIST", 3, 3, fn_poisson_dist, 0, D, "POISSON.DIST(x, mean, cumulative)", "The chance of x events when mean are expected."),
  FN ("BINOM.DIST", 4, 4, fn_binom_dist, 0, D, "BINOM.DIST(k, trials, p, cumulative)", "The chance of k successes in so many trials."),
  FN ("EXPON.DIST", 3, 3, fn_expon_dist, 0, D, "EXPON.DIST(x, rate, cumulative)", "The exponential distribution."),
  FN ("BETA.DIST", 4, 6, fn_beta_dist, 0, D, "BETA.DIST(x, alpha, beta, cumulative, [A], [B])", "The beta distribution."),
  FN ("BETA.INV", 3, 5, fn_beta_inv, 0, D, "BETA.INV(p, alpha, beta, [A], [B])", "The beta distribution's quantile."),
  FN ("GAMMA.DIST", 4, 4, fn_gamma_dist, 0, D, "GAMMA.DIST(x, shape, scale, cumulative)", "The gamma distribution."),
  FN ("GAMMA.INV", 3, 3, fn_gamma_inv, 0, D, "GAMMA.INV(p, shape, scale)", "The gamma distribution's quantile."),
  FN ("T.DIST", 3, 3, fn_t_dist, 0, D, "T.DIST(t, df, cumulative)", "Student's t distribution."),
  FN ("T.INV", 2, 2, fn_t_inv, 0, D, "T.INV(p, df)", "Student's t quantile."),
  FN ("T.INV.2T", 2, 2, fn_t_inv_2t, 0, D, "T.INV.2T(p, df)", "The t value with probability p outside plus or minus it."),
  FN ("LOGNORM.DIST", 4, 4, fn_lognorm_dist, 0, D, "LOGNORM.DIST(x, mu, sigma, cumulative)", "The lognormal distribution, by its log's mean and sd."),
  FN ("LOGNORM.INV", 3, 3, fn_lognorm_inv, 0, D, "LOGNORM.INV(p, mu, sigma)", "The lognormal distribution's quantile."),
};
const int tm_fn_stats_count = G_N_ELEMENTS (tm_fn_stats);
