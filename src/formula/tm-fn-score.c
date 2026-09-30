/* tm-fn-score.c - keeping score of forecasts
 *
 * Copyright (C) 2026 The timemachine authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A forecasting method is only as good as its record.  These put a number
 * on a record, whatever made the forecasts: a column of what was forecast
 * beside a column of what happened.  All of them are lower-is-better but
 * COVERAGE, which should come out near the confidence the intervals
 * claimed.  BRIER and LOGSCORE, for probabilities, are among the Judgment
 * functions; SIM.CRPS and SIM.PIT score a simulated cell.
 */

#include "tm-fn-private.h"

#include <stdlib.h>
#include <string.h>

#define S "Scoring"

/* The continuous ranked probability score of a forecast given as a range
 * of samples -- an ensemble, a set of scenarios -- once y happened. */
static TmValue
fn_crps (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double y, score;
  gboolean fair = FALSE;
  GArray *a;

  ARG_NUM (1, y);
  if (HAS_ARG (2))
    ARG_BOOL (2, fair);
  if ((a = collect (ctx, args, 1, &err)) == NULL)
    return err;
  if (a->len == 0)
    {
      g_array_free (a, TRUE);
      return tm_value_error (TM_ERR_DIV0);
    }
  qsort (a->data, a->len, sizeof (double), tm_compare_doubles);
  score = tm_crps_sorted ((double *) a->data, a->len, y, fair);
  g_array_free (a, TRUE);
  return tm_value_number (score);
}

/* The same for a normal forecast, in closed form (Gneiting and Raftery). */
static TmValue
fn_crps_normal (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double y, mu, sd, z;

  ARG_NUM (0, y);
  ARG_NUM (1, mu);
  ARG_NUM (2, sd);
  if (sd < 0)
    return tm_value_error (TM_ERR_NUM);
  if (sd == 0)
    return tm_value_number (fabs (y - mu));
  z = (y - mu) / sd;
  return tm_value_number (sd * (z * (2 * tm_norm_cdf (z) - 1)
                                + 2 * exp (-z * z / 2) / sqrt (2 * G_PI) - 1 / sqrt (G_PI)));
}

/* Columns of the same length, side by side; a row is used only when all
 * have a number in it. */
static gboolean
columns (TmEvalContext *ctx, TmArg *args, int k, double **cols, int *m, TmValue *err)
{
  const TmArg *list[4];

  for (int j = 0; j < k; j++)
    list[j] = &args[j];
  if (!tm_arg_columns (ctx, list, k, cols, m, err))
    return FALSE;
  if (*m == 0)
    {
      for (int j = 0; j < k; j++)
        g_free (cols[j]);
      *err = tm_value_error (TM_ERR_DIV0);
      return FALSE;
    }
  return TRUE;
}

static void
free_columns (double **cols, int k)
{
  for (int j = 0; j < k; j++)
    g_free (cols[j]);
}

/* Winkler's interval score for central intervals meant to hold 1 - alpha
 * of outcomes: the width, plus 2/alpha times any miss.  It rewards
 * intervals that are narrow and right, and punishes the overconfident. */
static TmValue
fn_interval_score (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double alpha, *c[3], s = 0;
  int m;

  OPT_NUM (3, alpha, 0.1);
  if (!(alpha > 0 && alpha < 1))
    return tm_value_error (TM_ERR_NUM);
  if (!columns (ctx, args, 3, c, &m, &err))
    return err;
  for (int i = 0; i < m; i++)
    {
      double lo = c[0][i], hi = c[1][i], y = c[2][i];

      s += hi - lo;
      if (y < lo)
        s += 2 / alpha * (lo - y);
      if (y > hi)
        s += 2 / alpha * (y - hi);
    }
  free_columns (c, 3);
  return tm_value_number (s / m);
}

/* How often the outcomes fell inside their intervals: near the
 * confidence the intervals claimed, if they were honest. */
static TmValue
fn_coverage (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double *c[3];
  int m, in = 0;

  if (!columns (ctx, args, 3, c, &m, &err))
    return err;
  for (int i = 0; i < m; i++)
    in += c[0][i] <= c[2][i] && c[2][i] <= c[1][i];
  free_columns (c, 3);
  return tm_value_number ((double) in / m);
}

/* The quantile (pinball) loss of forecasts of the tau'th percentile. */
static TmValue
fn_pinball (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double tau, *c[2], s = 0;
  int m;

  ARG_NUM (2, tau);
  if (!(tau > 0 && tau < 1))
    return tm_value_error (TM_ERR_NUM);
  if (!columns (ctx, args, 2, c, &m, &err))
    return err;
  for (int i = 0; i < m; i++)
    {
      double u = c[1][i] - c[0][i];
      s += u * (tau - (u < 0));
    }
  free_columns (c, 2);
  return tm_value_number (s / m);
}

/* Hyndman and Koehler's mean absolute scaled error: the forecasts' mean
 * absolute error over the naive forecast's on the training data -- the
 * value a season before.  Below 1 beats the naive method. */
static TmValue
fn_mase (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double season, *c[2], e = 0, scale = 0, *y;
  GArray *train;
  int m, k;

  OPT_NUM (3, season, 1);
  if (season < 1 || season > 1e6)
    return tm_value_error (TM_ERR_NUM);
  k = (int) season;
  if ((train = collect (ctx, &args[2], 1, &err)) == NULL)
    return err;
  if ((int) train->len <= k)
    {
      g_array_free (train, TRUE);
      return tm_value_error (TM_ERR_DIV0);
    }
  y = (double *) train->data;
  for (guint i = k; i < train->len; i++)
    scale += fabs (y[i] - y[i - k]) / (train->len - k);
  g_array_free (train, TRUE);
  if (!(scale > 0))
    return tm_value_error (TM_ERR_DIV0);
  if (!columns (ctx, args, 2, c, &m, &err))
    return err;
  for (int i = 0; i < m; i++)
    e += fabs (c[0][i] - c[1][i]) / m;
  free_columns (c, 2);
  return tm_value_number (e / scale);
}

/* Cooke's calibration score for an expert who gave 5th, 50th and 95th
 * percentiles for questions whose answers are now known: the chance that
 * a well-calibrated expert would miss the right shares -- 5%, 45%, 45%,
 * 5% of answers in the four bins -- by as much as this one did.  Near 1
 * is well calibrated; below 0.05, badly. */
static TmValue
fn_expert_calibration (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double *c[4], count[4] = { 0 }, p[4] = { 0.05, 0.45, 0.45, 0.05 }, info = 0;
  int m;

  if (!columns (ctx, args, 4, c, &m, &err))
    return err;
  for (int i = 0; i < m; i++)
    {
      double y = c[3][i];
      int bin = y <= c[0][i] ? 0 : y <= c[1][i] ? 1 : y <= c[2][i] ? 2 : 3;
      count[bin]++;
    }
  free_columns (c, 4);
  for (int b = 0; b < 4; b++)
    if (count[b] > 0)
      info += count[b] / m * log (count[b] / m / p[b]);
  /* 2 m I is chi-squared with three degrees of freedom. */
  return tm_value_number (1 - tm_gamma_p (1.5, m * info));
}

const TmFunction tm_fn_score[] = {
  FN ("CRPS", 2, 3, fn_crps, 0, S, "CRPS(samples, observed, [fair])", "How far a forecast given as samples was from what happened: lower is better."),
  FN ("CRPS.NORMAL", 3, 3, fn_crps_normal, 0, S, "CRPS.NORMAL(observed, mean, sd)", "The CRPS of a normal forecast, in closed form."),
  FN ("INTERVAL.SCORE", 3, 4, fn_interval_score, 0, S, "INTERVAL.SCORE(lowers, uppers, observed, [alpha])", "Winkler's score: interval width plus 2/alpha times each miss; lower is better."),
  FN ("COVERAGE", 3, 3, fn_coverage, 0, S, "COVERAGE(lowers, uppers, observed)", "The share of outcomes inside their intervals."),
  FN ("PINBALL", 3, 3, fn_pinball, 0, S, "PINBALL(quantiles, observed, tau)", "The quantile loss of forecasts of the tau'th percentile."),
  FN ("MASE", 3, 4, fn_mase, 0, S, "MASE(forecasts, actuals, training, [season])", "Mean absolute error scaled by the naive forecast's: below 1 beats it."),
  FN ("EXPERT.CALIBRATION", 4, 4, fn_expert_calibration, 0, S, "EXPERT.CALIBRATION(p5s, p50s, p95s, actuals)", "Cooke's calibration score of an expert's past percentiles: near 1 is good."),
};
const int tm_fn_score_count = G_N_ELEMENTS (tm_fn_score);
