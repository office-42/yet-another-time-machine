/* tm-fn-forecast.c - extrapolating from the past, and keeping score
 *
 * Copyright (C) 2026 The timemachine authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Three families.  Regression fits a line (or an exponential) through what
 * has happened and reads off where it goes next.  Exponential smoothing --
 * Holt's linear trend, and Holt and Winters' seasonal method -- weighs
 * recent history more than old, which is what most real series deserve;
 * FORECAST.ETS is Excel's name for it.  And the judgmental forecaster's
 * tools: Bayes' rule for updating a probability on evidence, and the
 * Brier and logarithmic scores for finding out, afterwards, how good the
 * probabilities were.
 *
 * RAND.LINEAR and RAND.ETS are the bridge to simulation: the statistical
 * forecast plus a draw from its prediction error, so that the uncertainty
 * of an extrapolation can flow into a Monte Carlo model like any other.
 */

#include "tm-fn-private.h"

#include <stdlib.h>
#include <string.h>

#define F "Forecasting"
#define J "Judgment"

/* ---- Regression ------------------------------------------------------- */

typedef struct {
  guint  n;
  double slope, intercept;
  double mean_x, sxx, sse, r2;
} LineFit;

static gboolean
fit_line (const double *y, const double *x, guint n, LineFit *fit, TmValue *err)
{
  double my = 0, mx = 0, sxy = 0, sxx = 0, syy = 0;

  if (n < 2)
    {
      *err = tm_value_error (TM_ERR_DIV0);
      return FALSE;
    }
  for (guint i = 0; i < n; i++)
    {
      my += y[i];
      mx += x[i];
    }
  my /= n;
  mx /= n;
  for (guint i = 0; i < n; i++)
    {
      sxy += (x[i] - mx) * (y[i] - my);
      sxx += (x[i] - mx) * (x[i] - mx);
      syy += (y[i] - my) * (y[i] - my);
    }
  if (sxx == 0)
    {
      *err = tm_value_error (TM_ERR_DIV0);
      return FALSE;
    }
  fit->n = n;
  fit->slope = sxy / sxx;
  fit->intercept = my - fit->slope * mx;
  fit->mean_x = mx;
  fit->sxx = sxx;
  fit->sse = MAX (0.0, syy - fit->slope * sxy);
  fit->r2 = syy == 0 ? 1 : (sxy * sxy) / (sxx * syy);
  return TRUE;
}

/* known_y and known_x as pairs, fitted; log_y fits ln(y) for GROWTH. */
static gboolean
fit_args (TmEvalContext *ctx, TmArg *ys, TmArg *xs, gboolean log_y, LineFit *fit, TmValue *err)
{
  GArray *y = g_array_new (FALSE, FALSE, sizeof (double));
  GArray *x = g_array_new (FALSE, FALSE, sizeof (double));
  gboolean ok;

  ok = tm_arg_pairs (ctx, ys, xs, y, x, err);
  if (ok && log_y)
    for (guint i = 0; i < y->len && ok; i++)
      {
        double *v = &g_array_index (y, double, i);
        if (*v <= 0)
          {
            *err = tm_value_error (TM_ERR_NUM);
            ok = FALSE;
          }
        else
          *v = log (*v);
      }
  if (ok)
    ok = fit_line ((double *) y->data, (double *) x->data, y->len, fit, err);
  g_array_free (y, TRUE);
  g_array_free (x, TRUE);
  return ok;
}

static TmValue
fn_slope (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  LineFit fit;

  if (!fit_args (ctx, &args[0], &args[1], FALSE, &fit, &err))
    return err;
  return tm_value_number (fit.slope);
}

static TmValue
fn_intercept (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  LineFit fit;

  if (!fit_args (ctx, &args[0], &args[1], FALSE, &fit, &err))
    return err;
  return tm_value_number (fit.intercept);
}

static TmValue
fn_rsq (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  LineFit fit;

  if (!fit_args (ctx, &args[0], &args[1], FALSE, &fit, &err))
    return err;
  return tm_value_number (fit.r2);
}

static TmValue
fn_steyx (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  LineFit fit;

  if (!fit_args (ctx, &args[0], &args[1], FALSE, &fit, &err))
    return err;
  if (fit.n < 3)
    return tm_value_error (TM_ERR_DIV0);
  return tm_value_number (sqrt (fit.sse / (fit.n - 2)));
}

static TmValue
fn_forecast_linear (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  LineFit fit;
  double x;

  ARG_NUM (0, x);
  if (!fit_args (ctx, &args[1], &args[2], FALSE, &fit, &err))
    return err;
  return tm_value_number (fit.intercept + fit.slope * x);
}

/* TREND and GROWTH in Excel's argument order, for one new x. */
static TmValue
fn_trend (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  LineFit fit;
  double x;

  ARG_NUM (2, x);
  if (!fit_args (ctx, &args[0], &args[1], FALSE, &fit, &err))
    return err;
  return tm_value_number (fit.intercept + fit.slope * x);
}

static TmValue
fn_growth (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  LineFit fit;
  double x;

  ARG_NUM (2, x);
  if (!fit_args (ctx, &args[0], &args[1], TRUE, &fit, &err))
    return err;
  return tm_value_number (exp (fit.intercept + fit.slope * x));
}

/* The line's forecast plus a draw from its prediction error: Student's t
 * with n - 2 degrees of freedom, scaled by the standard error of a new
 * observation at x, s sqrt(1 + 1/n + (x - mean)^2 / Sxx). */
static TmValue
fn_rand_linear (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  LineFit fit;
  double x, s, se, u;

  ARG_NUM (0, x);
  if (!fit_args (ctx, &args[1], &args[2], FALSE, &fit, &err))
    return err;
  if (fit.n < 3)
    return tm_value_error (TM_ERR_DIV0);
  s = sqrt (fit.sse / (fit.n - 2));
  se = s * sqrt (1 + 1.0 / fit.n + (x - fit.mean_x) * (x - fit.mean_x) / fit.sxx);
  if (stratified (ctx, &u))
    return tm_value_number (fit.intercept + fit.slope * x + se * tm_t_inv (u, fit.n - 2));
  return tm_value_number (fit.intercept + fit.slope * x
                          + se * tm_rng_student_t (ctx->rng, fit.n - 2));
}

static TmValue
fn_forecast_linear_confint (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  LineFit fit;
  double x, conf, s, se;

  ARG_NUM (0, x);
  OPT_NUM (3, conf, 0.95);
  if (conf <= 0 || conf >= 1)
    return tm_value_error (TM_ERR_NUM);
  if (!fit_args (ctx, &args[1], &args[2], FALSE, &fit, &err))
    return err;
  if (fit.n < 3)
    return tm_value_error (TM_ERR_DIV0);
  s = sqrt (fit.sse / (fit.n - 2));
  se = s * sqrt (1 + 1.0 / fit.n + (x - fit.mean_x) * (x - fit.mean_x) / fit.sxx);
  return tm_value_number (se * tm_t_inv (0.5 + conf / 2, fit.n - 2));
}

/* ---- Exponential smoothing -------------------------------------------- */

typedef struct {
  int    period;          /* 0: no seasonality */
  double alpha, beta, gamma;
  double level, trend;
  double *season;         /* the last period's seasonal indices */
  double sigma2;          /* one-step error variance */
  double t_last, step;    /* where the timeline ends, and its spacing */
  int    n;
} EtsFit;

/* One pass of Holt-Winters' additive method (Holt's alone when period is
 * 0) over y, returning the sum of squared one-step errors and leaving the
 * final state in fit. */
static double
ets_pass (const double *y, int n, int period, double alpha, double beta, double gamma,
          EtsFit *fit, double *season_work)
{
  double level, trend, sse = 0;
  int start, count = 0;

  if (period > 0)
    {
      double m1 = 0, m2 = 0;

      for (int i = 0; i < period; i++)
        m1 += y[i];
      m1 /= period;
      if (n >= 2 * period)
        {
          for (int i = period; i < 2 * period; i++)
            m2 += y[i];
          m2 /= period;
          trend = (m2 - m1) / period;
        }
      else
        trend = 0;
      for (int i = 0; i < period; i++)
        season_work[i] = y[i] - (m1 + trend * (i - (period - 1) / 2.0));
      /* Level and trend as they stand at the end of the first season. */
      level = m1 + trend * (period - 1) / 2.0;
      start = period;
    }
  else
    {
      level = y[0];
      trend = n > 1 ? y[1] - y[0] : 0;
      start = 1;
    }

  /* Holt and Winters' component form: season_work[t % period] holds the
   * index from a season ago until it is replaced by this season's. */
  for (int t = start; t < n; t++)
    {
      double s = period > 0 ? season_work[t % period] : 0;
      double old_level = level, old_trend = trend;
      double e = y[t] - (old_level + old_trend + s);

      sse += e * e;
      count++;
      level = alpha * (y[t] - s) + (1 - alpha) * (old_level + old_trend);
      trend = beta * (level - old_level) + (1 - beta) * old_trend;
      if (period > 0)
        season_work[t % period] = gamma * (y[t] - old_level - old_trend) + (1 - gamma) * s;
    }

  if (fit != NULL)
    {
      fit->alpha = alpha;
      fit->beta = beta;
      fit->gamma = gamma;
      fit->level = level;
      fit->trend = trend;
      fit->sigma2 = count > 0 ? sse / count : 0;
      fit->n = n;
      if (period > 0)
        {
          /* Rotate the indices so that season[0] belongs to the step
           * after the last observation. */
          for (int i = 0; i < period; i++)
            fit->season[i] = season_work[(n + i) % period];
        }
    }
  return sse;
}

/* The lag, from 2 up to half the series, at which the series (less its
 * straight-line trend) is most like itself; 0 if none is much like it. */
static int
detect_period (const double *y, int n)
{
  double *d = g_new (double, n);
  double mt = (n - 1) / 2.0, my = 0, sty = 0, stt = 0, slope, var = 0;
  double best = 0.3;
  int period = 0;

  for (int i = 0; i < n; i++)
    my += y[i];
  my /= n;
  for (int i = 0; i < n; i++)
    {
      sty += (i - mt) * (y[i] - my);
      stt += (i - mt) * (i - mt);
    }
  slope = stt > 0 ? sty / stt : 0;
  for (int i = 0; i < n; i++)
    {
      d[i] = y[i] - my - slope * (i - mt);
      var += d[i] * d[i];
    }
  if (var > 0)
    for (int lag = 2; lag <= n / 2; lag++)
      {
        double c = 0;

        for (int i = lag; i < n; i++)
          c += d[i] * d[i - lag];
        c /= var;
        if (c > best)
          {
            best = c;
            period = lag;
          }
      }
  g_free (d);
  return period;
}

static void
ets_optimise (const double *y, int n, int period, EtsFit *fit)
{
  static const double grid[] = { 0.02, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 0.98 };
  double *work = g_new0 (double, MAX (period, 1));
  double best = INFINITY, ba = 0.5, bb = 0.1, bg = 0.1;
  int ng = period > 0 ? (int) G_N_ELEMENTS (grid) : 1;

  /* A coarse grid first, so as not to be caught in a local minimum... */
  for (guint i = 0; i < G_N_ELEMENTS (grid); i++)
    for (guint j = 0; j < G_N_ELEMENTS (grid); j++)
      for (int k = 0; k < ng; k++)
        {
          double g = period > 0 ? grid[k] : 0;
          double sse = ets_pass (y, n, period, grid[i], grid[j] * 0.5, g, NULL, work);

          if (sse < best)
            {
              best = sse;
              ba = grid[i];
              bb = grid[j] * 0.5;
              bg = g;
            }
        }

  /* ...then a pattern search about the best point, halving the step. */
  for (double step = 0.05; step > 1e-4; step /= 2)
    {
      gboolean moved = TRUE;

      while (moved)
        {
          moved = FALSE;
          for (int dim = 0; dim < (period > 0 ? 3 : 2); dim++)
            for (int sign = -1; sign <= 1; sign += 2)
              {
                double a = ba, b = bb, g = bg, sse;

                if (dim == 0) a += sign * step;
                if (dim == 1) b += sign * step;
                if (dim == 2) g += sign * step;
                if (a <= 0.001 || a >= 0.999 || b < 0 || b >= 0.999 || g < 0 || g >= 0.999)
                  continue;
                sse = ets_pass (y, n, period, a, b, g, NULL, work);
                if (sse < best - 1e-12)
                  {
                    best = sse;
                    ba = a;
                    bb = b;
                    bg = g;
                    moved = TRUE;
                  }
              }
        }
    }

  fit->period = period;
  fit->season = g_new0 (double, MAX (period, 1));
  ets_pass (y, n, period, ba, bb, bg, fit, work);
  g_free (work);
}

/* Fitting is a search over thousands of parameter sets, and RAND.ETS asks
 * for the same fit every iteration of a simulation; the answers are kept,
 * keyed by the data they came from. */
static GHashTable *ets_cache;

static void
ets_fit_free (gpointer p)
{
  EtsFit *fit = p;

  g_free (fit->season);
  g_free (fit);
}

/* FNV-1a over the data's bytes, mixed at the end: a key the cache can
 * trust (a clash would need two series among a few hundred to agree in
 * 64 bits) at a fraction of a cryptographic hash's cost. */
static char *
ets_key (const double *t, const double *y, int n, int season_arg)
{
  guint64 h = 0xcbf29ce484222325ULL;
  const guchar *parts[2] = { (const guchar *) t, (const guchar *) y };

  for (int k = 0; k < 2; k++)
    for (gsize i = 0; i < sizeof (double) * (gsize) n; i++)
      h = (h ^ parts[k][i]) * 0x100000001b3ULL;
  h ^= (guint64) season_arg * 0x9e3779b97f4a7c15ULL;
  h = (h ^ (h >> 31)) * 0xbf58476d1ce4e5b9ULL;
  return g_strdup_printf ("%016" G_GINT64_MODIFIER "x:%d", h ^ (h >> 29), n);
}

typedef struct {
  double t;
  double y;
} Point;

static int
compare_points (const void *a, const void *b)
{
  const Point *p = a, *q = b;
  return p->t < q->t ? -1 : p->t > q->t;
}

/* Fits values against timeline.  seasonality: missing or 1 finds the
 * period itself, 0 turns seasonality off, 2 or more is the period. */
static const EtsFit *
ets_fit (TmEvalContext *ctx, TmArg *values, TmArg *timeline, TmArg *season_arg,
         TmValue *err)
{
  GArray *y = g_array_new (FALSE, FALSE, sizeof (double));
  GArray *t = g_array_new (FALSE, FALSE, sizeof (double));
  double season = 1;
  int n, period;
  Point *pts;
  double *ys, *ts;
  char *key;
  EtsFit *fit;

  if (season_arg != NULL && !season_arg->missing)
    {
      if (!tm_arg_number (ctx, season_arg, &season, err))
        goto fail;
      season = floor (season);
      if (season < 0)
        {
          *err = tm_value_error (TM_ERR_NUM);
          goto fail;
        }
    }
  if (!tm_arg_pairs (ctx, values, timeline, y, t, err))
    goto fail;
  n = (int) y->len;
  if (n < 3)
    {
      *err = tm_value_error (TM_ERR_NUM);
      goto fail;
    }

  pts = g_new (Point, n);
  for (int i = 0; i < n; i++)
    {
      pts[i].t = g_array_index (t, double, i);
      pts[i].y = g_array_index (y, double, i);
    }
  qsort (pts, (size_t) n, sizeof (Point), compare_points);
  ys = g_new (double, n);
  ts = g_new (double, n);
  for (int i = 0; i < n; i++)
    {
      ys[i] = pts[i].y;
      ts[i] = pts[i].t;
    }
  g_free (pts);

  /* The timeline must step evenly, as Excel's does. */
  {
    double step = ts[1] - ts[0];

    for (int i = 1; i < n; i++)
      if (step <= 0 || fabs ((ts[i] - ts[i - 1]) - step) > 1e-9 * MAX (1.0, fabs (step)))
        {
          g_free (ys);
          g_free (ts);
          *err = tm_value_error (TM_ERR_NUM);
          goto fail;
        }
  }

  if (ets_cache == NULL)
    ets_cache = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, ets_fit_free);
  key = ets_key (ts, ys, n, (int) season);
  fit = g_hash_table_lookup (ets_cache, key);
  if (fit == NULL)
    {
      if (season == 1)
        period = detect_period (ys, n);
      else if (season == 0)
        period = 0;
      else
        period = (int) season;
      if (period > 0 && n < period + 2)
        period = 0;

      fit = g_new0 (EtsFit, 1);
      ets_optimise (ys, n, period, fit);
      fit->t_last = ts[n - 1];
      fit->step = ts[1] - ts[0];
      if (g_hash_table_size (ets_cache) > 256)
        g_hash_table_remove_all (ets_cache);
      g_hash_table_insert (ets_cache, key, fit);
    }
  else
    g_free (key);

  g_free (ys);
  g_free (ts);
  g_array_free (y, TRUE);
  g_array_free (t, TRUE);
  return fit;

fail:
  g_array_free (y, TRUE);
  g_array_free (t, TRUE);
  return NULL;
}

/* The point forecast h steps past the end, and its standard error: the
 * variance of Hyndman and Athanasopoulos' ETS(A,A,N) and ETS(A,A,A),
 * with the smoothing parameters turned into their error-correction form. */
static void
ets_forecast (const EtsFit *fit, double h, double *mean, double *se)
{
  int hi = MAX (1, (int) ceil (h - 1e-9));
  double season = 0;
  double a = fit->alpha, b = fit->alpha * fit->beta, g = fit->gamma * (1 - fit->alpha);
  double v;

  if (fit->period > 0)
    season = fit->season[(hi - 1) % fit->period];
  *mean = fit->level + h * fit->trend + season;

  v = 1 + (hi - 1) * (a * a + a * b * hi + b * b * hi * (2 * hi - 1) / 6.0);
  if (fit->period > 0)
    {
      int k = (hi - 1) / fit->period;
      v += g * k * (2 * a + g + b * fit->period * (k + 1));
    }
  *se = sqrt (fit->sigma2 * v);
}

static gboolean
ets_steps (TmEvalContext *ctx, TmArg *target, const EtsFit *fit, double *h, TmValue *err)
{
  double x;

  if (!tm_arg_number (ctx, target, &x, err))
    return FALSE;
  *h = (x - fit->t_last) / fit->step;
  if (*h <= 0)
    {
      *err = tm_value_error (TM_ERR_NUM);
      return FALSE;
    }
  return TRUE;
}

static TmValue
fn_forecast_ets (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  const EtsFit *fit = ets_fit (ctx, &args[1], &args[2], n > 3 ? &args[3] : NULL, &err);
  double h, mean, se;

  if (fit == NULL)
    return err;
  if (!ets_steps (ctx, &args[0], fit, &h, &err))
    return err;
  ets_forecast (fit, h, &mean, &se);
  return tm_value_number (mean);
}

static TmValue
fn_forecast_ets_confint (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  const EtsFit *fit;
  double h, mean, se, conf;

  OPT_NUM (3, conf, 0.95);
  if (conf <= 0 || conf >= 1)
    return tm_value_error (TM_ERR_NUM);
  fit = ets_fit (ctx, &args[1], &args[2], n > 4 ? &args[4] : NULL, &err);
  if (fit == NULL)
    return err;
  if (!ets_steps (ctx, &args[0], fit, &h, &err))
    return err;
  ets_forecast (fit, h, &mean, &se);
  return tm_value_number (se * tm_norm_inv (0.5 + conf / 2));
}

static TmValue
fn_forecast_ets_seasonality (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  const EtsFit *fit = ets_fit (ctx, &args[0], &args[1], NULL, &err);

  if (fit == NULL)
    return err;
  return tm_value_number (fit->period);
}

static TmValue
fn_rand_ets (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  const EtsFit *fit = ets_fit (ctx, &args[1], &args[2], n > 3 ? &args[3] : NULL, &err);
  double h, mean, se;

  if (fit == NULL)
    return err;
  if (!ets_steps (ctx, &args[0], fit, &h, &err))
    return err;
  ets_forecast (fit, h, &mean, &se);
  return tm_value_number (mean + se * draw_normal (ctx));
}

static TmValue
fn_cagr (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double start, end, periods;

  ARG_NUM (0, start);
  ARG_NUM (1, end);
  ARG_NUM (2, periods);
  if (start <= 0 || end < 0 || periods <= 0)
    return tm_value_error (TM_ERR_NUM);
  return tm_value_number (pow (end / start, 1 / periods) - 1);
}

/* ---- Judgment --------------------------------------------------------- */

/* Probabilities and what happened (1 or 0, TRUE or FALSE), in pairs. */
static gboolean
prob_pairs (TmEvalContext *ctx, TmArg *args, GArray *p, GArray *o, TmValue *err)
{
  int np, no;
  const TmValue **pc = tm_arg_cells (ctx, &args[0], &np);
  const TmValue **oc = tm_arg_cells (ctx, &args[1], &no);
  gboolean ok = TRUE;

  if (np != no)
    {
      *err = tm_value_error (TM_ERR_NA);
      ok = FALSE;
    }
  for (int i = 0; ok && i < np; i++)
    {
      double pv, ov;
      TmErrorCode code;

      if (pc[i]->type == TM_VALUE_EMPTY || oc[i]->type == TM_VALUE_EMPTY)
        continue;             /* a question not yet resolved */
      if (!tm_value_to_number (pc[i], &pv, &code) || !tm_value_to_number (oc[i], &ov, &code))
        {
          *err = tm_value_error (code);
          ok = FALSE;
          break;
        }
      if (pv < 0 || pv > 1 || (ov != 0 && ov != 1))
        {
          *err = tm_value_error (TM_ERR_NUM);
          ok = FALSE;
          break;
        }
      g_array_append_val (p, pv);
      g_array_append_val (o, ov);
    }
  g_free (pc);
  g_free (oc);
  if (ok && p->len == 0)
    {
      *err = tm_value_error (TM_ERR_DIV0);
      ok = FALSE;
    }
  return ok;
}

static TmValue
score (TmEvalContext *ctx, TmArg *args, gboolean log_score)
{
  TmValue err;
  GArray *p = g_array_new (FALSE, FALSE, sizeof (double));
  GArray *o = g_array_new (FALSE, FALSE, sizeof (double));
  double total = 0;
  TmValue r;

  if (!prob_pairs (ctx, args, p, o, &err))
    r = err;
  else
    {
      for (guint i = 0; i < p->len; i++)
        {
          double pv = g_array_index (p, double, i), ov = g_array_index (o, double, i);

          if (log_score)
            {
              /* A probability of exactly 0 for what happened is an
               * infinite penalty; it is clipped to a merely huge one. */
              double q = ov == 1 ? pv : 1 - pv;
              total += -log (MAX (q, 1e-15));
            }
          else
            total += (pv - ov) * (pv - ov);
        }
      r = tm_value_number (total / p->len);
    }
  g_array_free (p, TRUE);
  g_array_free (o, TRUE);
  return r;
}

static TmValue fn_brier    (TmEvalContext *c, TmArg *a, int n) { return score (c, a, FALSE); }
static TmValue fn_logscore (TmEvalContext *c, TmArg *a, int n) { return score (c, a, TRUE); }

/* Bayes' rule: the chance of the hypothesis after seeing evidence that
 * would turn up with probability p_if_true were it true and p_if_false
 * were it not. */
static TmValue
fn_bayes (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double prior, pt, pf, num, den;

  ARG_NUM (0, prior);
  ARG_NUM (1, pt);
  ARG_NUM (2, pf);
  if (prior < 0 || prior > 1 || pt < 0 || pt > 1 || pf < 0 || pf > 1)
    return tm_value_error (TM_ERR_NUM);
  num = prior * pt;
  den = num + (1 - prior) * pf;
  if (den == 0)
    return tm_value_error (TM_ERR_DIV0);
  return tm_value_number (num / den);
}

/* Pushes a probability away from one half: p^a / (p^a + (1 - p)^a).  An
 * average of forecasters' probabilities is under-confident, because each
 * knows only part of what the crowd knows; the Good Judgment Project
 * found an a of about 2.5 put it right. */
static TmValue
fn_extremize (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double p, a, x, y;

  ARG_NUM (0, p);
  OPT_NUM (1, a, 2.5);
  if (p < 0 || p > 1 || a <= 0)
    return tm_value_error (TM_ERR_NUM);
  x = pow (p, a);
  y = pow (1 - p, a);
  return tm_value_number (x / (x + y));
}

/* Laplace's rule of succession: after s successes in n tries, the chance
 * of success next time is (s + 1) / (n + 2) -- which, unlike s / n, is
 * neither certain after one success nor impossible before the first. */
static TmValue
fn_laplace (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double s, trials;

  ARG_NUM (0, s);
  ARG_NUM (1, trials);
  if (s < 0 || trials < s)
    return tm_value_error (TM_ERR_NUM);
  return tm_value_number ((s + 1) / (trials + 2));
}

/* The forecasters' probabilities pooled by the (weighted) geometric mean
 * of their odds: an average of log-odds, which, unlike the plain average,
 * lets a confident forecaster's 1% count for what it says. */
static TmValue
fn_pool_odds (TmEvalContext *ctx, TmArg *args, int n)
{
  int np, nw = 0;
  const TmValue **p = tm_arg_cells (ctx, &args[0], &np);
  const TmValue **w = HAS_ARG (1) ? tm_arg_cells (ctx, &args[1], &nw) : NULL;
  double sum = 0, total = 0;
  TmValue r;

  if (w != NULL && nw != np)
    r = tm_value_error (TM_ERR_NA);
  else
    {
      r = tm_value_error (TM_ERR_DIV0);
      for (int i = 0; i < np; i++)
        {
          double q, weight = 1;

          if (p[i]->type == TM_VALUE_ERROR)
            {
              r = tm_value_copy (p[i]);
              total = -1;
              break;
            }
          if (p[i]->type != TM_VALUE_NUMBER)
            continue;
          q = p[i]->as.number;
          if (q < 0 || q > 1)
            {
              r = tm_value_error (TM_ERR_NUM);
              total = -1;
              break;
            }
          if (w != NULL)
            {
              if (w[i]->type != TM_VALUE_NUMBER || w[i]->as.number < 0)
                continue;
              weight = w[i]->as.number;
            }
          q = CLAMP (q, 1e-6, 1 - 1e-6);
          sum += weight * log (q / (1 - q));
          total += weight;
        }
      if (total > 0)
        r = tm_value_number (1 / (1 + exp (-sum / total)));
    }
  g_free (p);
  g_free (w);
  return r;
}

/* Reference-class forecasting, after Kahneman and Tversky's outside view
 * and Flyvbjerg's practice: the estimate scaled by the ratio of actual to
 * estimated that projects like it reached with probability p. */
static TmValue
fn_refclass (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  GArray *a;
  double estimate, p, q;

  ARG_NUM (1, estimate);
  OPT_NUM (2, p, 0.8);
  if (p < 0 || p > 1)
    return tm_value_error (TM_ERR_NUM);
  a = collect (ctx, args, 1, &err);
  if (a == NULL)
    return err;
  if (a->len == 0)
    {
      g_array_free (a, TRUE);
      return tm_value_error (TM_ERR_NUM);
    }
  qsort (a->data, a->len, sizeof (double), tm_compare_doubles);
  q = tm_percentile_sorted ((double *) a->data, (int) a->len, p);
  g_array_free (a, TRUE);
  return tm_value_number (estimate * q);
}

const TmFunction tm_fn_forecast[] = {
  FN ("SLOPE", 2, 2, fn_slope, 0, F, "SLOPE(known_y, known_x)", "The slope of the least-squares line."),
  FN ("INTERCEPT", 2, 2, fn_intercept, 0, F, "INTERCEPT(known_y, known_x)", "Where the least-squares line crosses x = 0."),
  FN ("RSQ", 2, 2, fn_rsq, 0, F, "RSQ(known_y, known_x)", "How much of y the line explains, from 0 to 1."),
  FN ("STEYX", 2, 2, fn_steyx, 0, F, "STEYX(known_y, known_x)", "The standard error of the line's predictions."),
  FN ("FORECAST", 3, 3, fn_forecast_linear, 0, F, "FORECAST(x, known_y, known_x)", "The least-squares line, read at x."),
  FN ("FORECAST.LINEAR", 3, 3, fn_forecast_linear, 0, F, "FORECAST.LINEAR(x, known_y, known_x)", "The least-squares line, read at x."),
  FN ("FORECAST.LINEAR.CONFINT", 3, 4, fn_forecast_linear_confint, 0, F, "FORECAST.LINEAR.CONFINT(x, known_y, known_x, [confidence])", "Half the width of the line's prediction interval at x."),
  FN ("TREND", 3, 3, fn_trend, 0, F, "TREND(known_y, known_x, new_x)", "The least-squares line, read at new_x."),
  FN ("GROWTH", 3, 3, fn_growth, 0, F, "GROWTH(known_y, known_x, new_x)", "The least-squares exponential curve, read at new_x."),
  FN ("FORECAST.ETS", 3, 4, fn_forecast_ets, 0, F, "FORECAST.ETS(target, values, timeline, [seasonality])", "Exponential smoothing (Holt-Winters), read at target."),
  FN ("FORECAST.ETS.CONFINT", 3, 5, fn_forecast_ets_confint, 0, F, "FORECAST.ETS.CONFINT(target, values, timeline, [confidence], [seasonality])", "Half the width of the smoothing forecast's interval."),
  FN ("FORECAST.ETS.SEASONALITY", 2, 2, fn_forecast_ets_seasonality, 0, F, "FORECAST.ETS.SEASONALITY(values, timeline)", "The length of the season the smoothing found."),
  FN ("RAND.LINEAR", 3, 3, fn_rand_linear, TM_FN_RANDOM, F, "RAND.LINEAR(x, known_y, known_x)", "The line's forecast at x, plus a draw from its error."),
  FN ("RAND.ETS", 3, 4, fn_rand_ets, TM_FN_RANDOM, F, "RAND.ETS(target, values, timeline, [seasonality])", "The smoothing forecast, plus a draw from its error."),
  FN ("CAGR", 3, 3, fn_cagr, 0, F, "CAGR(start, end, periods)", "The compound growth rate per period."),

  FN ("BRIER", 2, 2, fn_brier, 0, J, "BRIER(probabilities, outcomes)", "Mean squared error of probability forecasts: 0 perfect, 0.25 a coin."),
  FN ("LOGSCORE", 2, 2, fn_logscore, 0, J, "LOGSCORE(probabilities, outcomes)", "Mean negative log likelihood of what happened: lower is better."),
  FN ("BAYES", 3, 3, fn_bayes, 0, J, "BAYES(prior, p_if_true, p_if_false)", "The probability after seeing the evidence."),
  FN ("EXTREMIZE", 1, 2, fn_extremize, 0, J, "EXTREMIZE(p, [a])", "A crowd's average probability, made bolder."),
  FN ("LAPLACE", 2, 2, fn_laplace, 0, J, "LAPLACE(successes, trials)", "The chance of success next time: (s + 1) / (n + 2)."),
  FN ("POOL.ODDS", 1, 2, fn_pool_odds, 0, J, "POOL.ODDS(probabilities, [weights])", "Forecasters pooled by the geometric mean of their odds."),
  FN ("REFCLASS", 2, 3, fn_refclass, 0, J, "REFCLASS(ratios, estimate, [p])", "The estimate scaled by past actual/estimate ratios at p (80%)."),
};
const int tm_fn_forecast_count = G_N_ELEMENTS (tm_fn_forecast);
