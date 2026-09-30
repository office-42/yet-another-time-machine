/* tm-fn-learn.c - learning from past cases, whatever they are cases of
 *
 * Copyright (C) 2026 The timemachine authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Most predictions in most fields come down to one question: of the cases
 * seen before, the ones like this one -- how did they turn out?  These
 * functions answer it from a table, whatever the table is of: a row per
 * past case, a column per thing known about it, a column of how it ended.
 * Projects and their overruns, patients and their recoveries, fields and
 * their yields, matches and their scores.
 *
 *   KNN.* and RAND.KNN   the k most similar past cases, and their outcomes
 *   FORECAST.MLR ...     a straight line through many features at once
 *   LOGIT.PROB           the chance of an event, for yes-or-no outcomes
 *   POISSON.REG          an expected count, for how-many outcomes
 *   QUANTILE.REG         a percentile, where the spread varies with them
 *   CONFORMAL.CONFINT    an honest interval around any forecast at all
 *
 * and, from a single column of numbers: a smooth distribution (KDE), the
 * next value with the uncertainty of its mean (RAND.NEXT), rates and
 * proportions updated by Bayes' rule, estimates pulled towards their
 * group's, lifetimes, correlated inputs, and extremes.
 *
 * A random function that fits a model is worked out once per future, with
 * the same table each time; fits are kept, keyed by the numbers they were
 * made from, so that only the first of ten thousand pays for the fit.
 */

#include "tm-fn-private.h"
#include "tm-linalg.h"

#include <stdlib.h>
#include <string.h>

#define L "Learning"
#define U "Updating"
#define R "Random"
#define D "Distributions"
#define V "Survival"
#define F "Forecasting"
#define P "Processes"
#define RND TM_FN_RANDOM

/* ---- Reading a table --------------------------------------------------- */

static gboolean
as_number (const TmValue *v, double *out)
{
  if (v->type == TM_VALUE_NUMBER)
    *out = v->as.number;
  else if (v->type == TM_VALUE_BOOL)
    *out = v->as.boolean ? 1 : 0;
  else
    return FALSE;
  return TRUE;
}

static void
shape_of (const TmArg *arg, int *rows, int *cols)
{
  if (arg->is_range)
    {
      *rows = arg->range.row1 - arg->range.row0 + 1;
      *cols = arg->range.col1 - arg->range.col0 + 1;
    }
  else
    *rows = *cols = 1;
}

static gboolean
has_error (const TmValue **cells, int n, TmValue *err)
{
  for (int i = 0; i < n; i++)
    if (cells[i]->type == TM_VALUE_ERROR)
      {
        *err = tm_value_copy (cells[i]);
        return TRUE;
      }
  return FALSE;
}

typedef struct {
  int     n, p;     /* cases; features of each */
  double *y;        /* n outcomes */
  double *x;        /* n rows of p features */
  double *e;        /* n exposures, if asked for */
} Table;

static void
table_clear (Table *t)
{
  g_free (t->y);
  g_free (t->x);
  g_free (t->e);
  memset (t, 0, sizeof *t);
}

/* The past cases: ys a column (or row) of n outcomes; xs a row per case,
 * a column per feature -- or, beside a row of outcomes, the other way
 * round; es, if given, a range like ys of exposures.  A case missing any
 * number is left out. */
static gboolean
read_table (TmEvalContext *ctx, const TmArg *ys, const TmArg *xs, const TmArg *es,
            Table *t, TmValue *err)
{
  int ny, nx, ne = 0, yr, yc, xr, xc;
  const TmValue **yv = tm_arg_cells (ctx, ys, &ny);
  const TmValue **xv = tm_arg_cells (ctx, xs, &nx);
  const TmValue **ev = es != NULL ? tm_arg_cells (ctx, es, &ne) : NULL;
  gboolean by_row, ok = FALSE;

  memset (t, 0, sizeof *t);
  shape_of (ys, &yr, &yc);
  shape_of (xs, &xr, &xc);
  if (has_error (yv, ny, err) || has_error (xv, nx, err)
      || (ev != NULL && has_error (ev, ne, err)))
    goto out;
  *err = tm_value_error (TM_ERR_NA);
  if (ev != NULL && ne != ny)
    goto out;
  if (yr == 1 && yc > 1 && xc == ny)
    by_row = FALSE;
  else if (xr == ny)
    by_row = TRUE;
  else if (xc == ny)
    by_row = FALSE;
  else
    goto out;

  t->p = by_row ? xc : xr;
  t->y = g_new (double, MAX (ny, 1));
  t->x = g_new (double, MAX ((gsize) ny * t->p, 1));
  if (ev != NULL)
    t->e = g_new (double, MAX (ny, 1));
  for (int i = 0; i < ny; i++)
    {
      double *row = t->x + (gsize) t->n * t->p;
      gboolean keep = as_number (yv[i], &t->y[t->n]);

      for (int j = 0; keep && j < t->p; j++)
        keep = as_number (xv[by_row ? i * xc + j : j * xc + i], &row[j]);
      if (keep && ev != NULL)
        keep = as_number (ev[i], &t->e[t->n]);
      if (keep)
        t->n++;
    }
  ok = TRUE;

out:
  g_free (yv);
  g_free (xv);
  g_free (ev);
  if (!ok)
    table_clear (t);
  return ok;
}

/* The new case: its p features. */
static double *
read_case (TmEvalContext *ctx, const TmArg *arg, int p, TmValue *err)
{
  int n;
  const TmValue **cells = tm_arg_cells (ctx, arg, &n);
  double *x0 = NULL;

  if (!has_error (cells, n, err))
    {
      if (n != p)
        *err = tm_value_error (TM_ERR_NA);
      else
        {
          x0 = g_new (double, MAX (p, 1));
          for (int j = 0; j < p; j++)
            if (!as_number (cells[j], &x0[j]))
              {
                g_clear_pointer (&x0, g_free);
                *err = tm_value_error (TM_ERR_VALUE);
                break;
              }
        }
    }
  g_free (cells);
  return x0;
}

/* Each feature's mean and standard deviation, so that features measured
 * in different units count alike.  One that never varies gets sd 0, and
 * is left out: it cannot tell the cases apart. */
static void
feature_scales (const Table *t, double *mean, double *sd)
{
  for (int j = 0; j < t->p; j++)
    {
      double m = 0, ss = 0;

      for (int i = 0; i < t->n; i++)
        m += t->x[i * t->p + j];
      m /= MAX (t->n, 1);
      for (int i = 0; i < t->n; i++)
        ss += (t->x[i * t->p + j] - m) * (t->x[i * t->p + j] - m);
      mean[j] = m;
      sd[j] = t->n > 1 ? sqrt (ss / (t->n - 1)) : 0;
      if (!(sd[j] > 1e-12 * MAX (1.0, fabs (m))))
        sd[j] = 0;
    }
}

/* ---- Models, and keeping them ------------------------------------------- */

typedef enum { FIT_OLS, FIT_LOGIT, FIT_POISSON, FIT_QUANTILE } FitKind;

typedef struct {
  TmErrorCode error;     /* why there is no fit, or none */
  int     p, q;          /* features; coefficients, the intercept first */
  double *mean, *sd;     /* each feature's, to standardise with */
  double *beta;          /* q, on the standardised features */
  double *l;             /* q x q: R of the QR (OLS), or the Cholesky
                          * factor of the information (the others) */
  double  s2;            /* residual variance (OLS); dispersion (Poisson) */
  int     df;
  gboolean zero;         /* Poisson: no events at all */
} Model;

static void
model_free (Model *m)
{
  if (m == NULL)
    return;
  g_free (m->mean);
  g_free (m->sd);
  g_free (m->beta);
  g_free (m->l);
  g_free (m);
}

static Model *
model_new (const Table *t)
{
  Model *m = g_new0 (Model, 1);

  m->p = t->p;
  m->mean = g_new (double, MAX (t->p, 1));
  m->sd = g_new (double, MAX (t->p, 1));
  feature_scales (t, m->mean, m->sd);
  m->q = 1;
  for (int j = 0; j < t->p; j++)
    if (m->sd[j] > 0)
      m->q++;
  return m;
}

/* A case as the model sees it: 1, then each feature that varies,
 * standardised. */
static void
design_row (const Model *m, const double *x, double *z)
{
  int k = 1;

  z[0] = 1;
  for (int j = 0; j < m->p; j++)
    if (m->sd[j] > 0)
      z[k++] = (x[j] - m->mean[j]) / m->sd[j];
}

static double *
design (const Model *m, const Table *t)
{
  double *z = g_new (double, MAX ((gsize) t->n * m->q, 1));

  for (int i = 0; i < t->n; i++)
    design_row (m, t->x + (gsize) i * t->p, z + (gsize) i * m->q);
  return z;
}

static double
dot (const double *a, const double *b, int n)
{
  double s = 0;

  for (int i = 0; i < n; i++)
    s += a[i] * b[i];
  return s;
}

/* The information matrix, Z'WZ plus a ridge on all but the intercept,
 * factorised.  FALSE if it will not factorise. */
static gboolean
information (const double *z, const double *w, int n, int q, double ridge, double *l)
{
  memset (l, 0, sizeof (double) * q * q);
  for (int i = 0; i < n; i++)
    for (int a = 0; a < q; a++)
      for (int b = 0; b <= a; b++)
        l[a * q + b] += w[i] * z[i * q + a] * z[i * q + b];
  for (int a = 0; a < q; a++)
    {
      if (a > 0)
        l[a * q + a] += ridge;
      for (int b = 0; b < a; b++)
        l[b * q + a] = l[a * q + b];
    }
  return tm_cholesky (l, q);
}

/* A Newton step is kept to a sensible size, so that a fit on cases that
 * separate cleanly creeps rather than leaps. */
static double
clamp_step (double *step, int q)
{
  double big = 0;

  for (int j = 0; j < q; j++)
    big = MAX (big, fabs (step[j]));
  if (big > 3)
    for (int j = 0; j < q; j++)
      step[j] *= 3 / big;
  return MIN (big, 3);
}

static Model *
fit_ols (const Table *t, double ridge)
{
  Model *m = model_new (t);
  int n = t->n, q = m->q, rows = n + (ridge > 0 ? q - 1 : 0);
  double *z, *y;
  TmLsq fit;

  if (n <= q)
    {
      m->error = TM_ERR_DIV0;
      return m;
    }
  z = g_new0 (double, (gsize) rows * q);
  y = g_new0 (double, rows);
  for (int i = 0; i < n; i++)
    {
      design_row (m, t->x + (gsize) i * t->p, z + (gsize) i * q);
      y[i] = t->y[i];
    }
  /* Ridge regression as least squares with q - 1 made-up cases, each
   * pulling one coefficient towards zero. */
  for (int j = 1; ridge > 0 && j < q; j++)
    z[(gsize) (n + j - 1) * q + j] = sqrt (ridge);
  if (!tm_lsq_fit (z, y, rows, q, &fit))
    m->error = TM_ERR_NUM;
  else
    {
      double sse = 0;

      for (int i = 0; i < n; i++)
        {
          double e = y[i] - dot (z + (gsize) i * q, fit.beta, q);
          sse += e * e;
        }
      m->beta = fit.beta;
      m->l = fit.r;
      m->df = n - q;
      m->s2 = sse / m->df;
    }
  g_free (z);
  g_free (y);
  return m;
}

/* Logistic regression, with Firth's correction: his penalty keeps the
 * estimates finite even when the features split the events from the
 * non-events perfectly -- common with few cases -- and takes out most of
 * the bias small samples give maximum likelihood. */
static Model *
fit_logit (const Table *t, double ridge)
{
  Model *m = model_new (t);
  int n = t->n, q = m->q;
  double *z, *w, *score, *v, ybar = 0;

  if (n < 2)
    {
      m->error = TM_ERR_DIV0;
      return m;
    }
  for (int i = 0; i < n; i++)
    {
      if (t->y[i] < 0 || t->y[i] > 1)
        {
          m->error = TM_ERR_NUM;
          return m;
        }
      ybar += t->y[i] / n;
    }
  z = design (m, t);
  w = g_new (double, n);
  score = g_new (double, q);
  v = g_new (double, q);
  m->beta = g_new0 (double, q);
  m->l = g_new (double, q * q);
  ybar = CLAMP (ybar, 0.02, 0.98);
  m->beta[0] = log (ybar / (1 - ybar));

  for (int iter = 0; iter < 200; iter++)
    {
      for (int i = 0; i < n; i++)
        {
          double p = 1 / (1 + exp (-dot (z + i * q, m->beta, q)));
          w[i] = MAX (p * (1 - p), 1e-12);
        }
      if (!information (z, w, n, q, ridge, m->l))
        {
          m->error = TM_ERR_NUM;
          break;
        }
      for (int j = 0; j < q; j++)
        score[j] = j > 0 ? -ridge * m->beta[j] : 0;
      for (int i = 0; i < n; i++)
        {
          double p = 1 / (1 + exp (-dot (z + i * q, m->beta, q)));
          double h;

          /* The case's leverage, from the factor: w z'(Z'WZ)^-1 z. */
          memcpy (v, z + i * q, sizeof (double) * q);
          tm_forward_solve (m->l, q, v);
          h = w[i] * dot (v, v, q);
          for (int j = 0; j < q; j++)
            score[j] += (t->y[i] - p + h * (0.5 - p)) * z[i * q + j];
        }
      tm_cholesky_solve (m->l, q, score);
      if (clamp_step (score, q) < 1e-10)
        break;
      for (int j = 0; j < q; j++)
        m->beta[j] += score[j];
    }
  /* The information where the fit ended, for the uncertainty of a new
   * case's chance. */
  if (m->error == TM_ERR_NONE)
    {
      for (int i = 0; i < n; i++)
        {
          double p = 1 / (1 + exp (-dot (z + i * q, m->beta, q)));
          w[i] = MAX (p * (1 - p), 1e-12);
        }
      if (!information (z, w, n, q, ridge, m->l))
        m->error = TM_ERR_NUM;
    }
  g_free (z);
  g_free (w);
  g_free (score);
  g_free (v);
  return m;
}

/* Poisson regression: counts of events over exposures -- days, visits,
 * kilometres -- their rate a product of the features' effects. */
static Model *
fit_poisson (const Table *t, double ridge)
{
  Model *m = model_new (t);
  int n = t->n, q = m->q;
  double *z, *w, *score, sy = 0, st = 0, chi = 0;

  if (n < 1)
    {
      m->error = TM_ERR_DIV0;
      return m;
    }
  for (int i = 0; i < n; i++)
    {
      double e = t->e != NULL ? t->e[i] : 1;

      if (t->y[i] < 0 || !(e > 0))
        {
          m->error = TM_ERR_NUM;
          return m;
        }
      sy += t->y[i];
      st += e;
    }
  m->beta = g_new0 (double, q);
  m->l = g_new (double, q * q);
  if (sy == 0)
    {
      m->zero = TRUE;
      return m;
    }
  z = design (m, t);
  w = g_new (double, n);
  score = g_new (double, q);
  m->beta[0] = log (sy / st);

  for (int iter = 0; iter < 200; iter++)
    {
      for (int j = 0; j < q; j++)
        score[j] = j > 0 ? -ridge * m->beta[j] : 0;
      for (int i = 0; i < n; i++)
        {
          double mu = (t->e != NULL ? t->e[i] : 1) * exp (dot (z + i * q, m->beta, q));

          w[i] = MAX (mu, 1e-12);
          for (int j = 0; j < q; j++)
            score[j] += (t->y[i] - mu) * z[i * q + j];
        }
      if (!information (z, w, n, q, ridge, m->l))
        {
          m->error = TM_ERR_NUM;
          break;
        }
      tm_cholesky_solve (m->l, q, score);
      if (clamp_step (score, q) < 1e-10)
        break;
      for (int j = 0; j < q; j++)
        m->beta[j] += score[j];
    }
  if (m->error == TM_ERR_NONE)
    {
      for (int i = 0; i < n; i++)
        {
          double mu = (t->e != NULL ? t->e[i] : 1) * exp (dot (z + i * q, m->beta, q));

          w[i] = MAX (mu, 1e-12);
          chi += (t->y[i] - mu) * (t->y[i] - mu) / w[i];
        }
      if (!information (z, w, n, q, ridge, m->l))
        m->error = TM_ERR_NUM;
      /* Pearson's estimate of overdispersion: 1 if the counts are as
       * scattered as Poisson counts are. */
      m->s2 = n > q ? MAX (1.0, chi / (n - q)) : 1;
    }
  g_free (z);
  g_free (w);
  g_free (score);
  return m;
}

/* Linear quantile regression by Hunter and Lange's majorise-minimise
 * iteration: each step a weighted least squares fit whose weights are the
 * inverse of the residuals, which minimises Koenker's check loss in the
 * limit. */
static Model *
fit_quantile (const Table *t, double tau)
{
  Model *m = model_new (t);
  int n = t->n, q = m->q;
  double *z, *w, *rhs, eps, scale = 0, ybar = 0, last = INFINITY;
  TmLsq start;

  if (n <= q)
    {
      m->error = TM_ERR_DIV0;
      return m;
    }
  z = design (m, t);
  if (!tm_lsq_fit (z, t->y, n, q, &start))
    {
      m->error = TM_ERR_NUM;
      g_free (z);
      return m;
    }
  m->beta = start.beta;
  g_free (start.r);
  m->l = g_new (double, q * q);
  w = g_new (double, n);
  rhs = g_new (double, q);
  for (int i = 0; i < n; i++)
    ybar += t->y[i] / n;
  for (int i = 0; i < n; i++)
    scale += fabs (t->y[i] - ybar) / n;
  eps = 1e-6 * (scale > 0 ? scale : 1);

  for (int iter = 0; iter < 1000; iter++)
    {
      double loss = 0;

      memset (rhs, 0, sizeof (double) * q);
      for (int i = 0; i < n; i++)
        {
          double r = t->y[i] - dot (z + i * q, m->beta, q);

          loss += r * (tau - (r < 0));
          w[i] = 1 / (eps + fabs (r));
          for (int j = 0; j < q; j++)
            rhs[j] += (w[i] * t->y[i] + 2 * tau - 1) * z[i * q + j];
        }
      if (fabs (last - loss) <= 1e-10 * (fabs (loss) + 1e-300))
        break;
      last = loss;
      if (!information (z, w, n, q, 0, m->l))
        {
          m->error = TM_ERR_NUM;
          break;
        }
      tm_cholesky_solve (m->l, q, rhs);
      memcpy (m->beta, rhs, sizeof (double) * q);
    }
  g_free (z);
  g_free (w);
  g_free (rhs);
  return m;
}

/* The last few fits, by the numbers they were made from. */
typedef struct {
  double *key;
  gsize   len;
  Model  *model;
} Memo;

static Memo memo[8];
static int memo_next;

static const Model *
model_get (const Table *t, FitKind kind, double a)
{
  gsize len = 4 + t->n + (gsize) t->n * t->p + (t->e != NULL ? t->n : 0);
  double *key = g_new (double, len), *w = key;
  Model *m;

  *w++ = kind;
  *w++ = t->n;
  *w++ = t->p;
  *w++ = a;
  memcpy (w, t->y, sizeof (double) * t->n);
  w += t->n;
  memcpy (w, t->x, sizeof (double) * t->n * t->p);
  w += (gsize) t->n * t->p;
  if (t->e != NULL)
    memcpy (w, t->e, sizeof (double) * t->n);

  for (guint i = 0; i < G_N_ELEMENTS (memo); i++)
    if (memo[i].model != NULL && memo[i].len == len
        && memcmp (memo[i].key, key, sizeof (double) * len) == 0)
      {
        g_free (key);
        return memo[i].model;
      }

  switch (kind)
    {
    case FIT_OLS:      m = fit_ols (t, a); break;
    case FIT_LOGIT:    m = fit_logit (t, a); break;
    case FIT_POISSON:  m = fit_poisson (t, a); break;
    case FIT_QUANTILE: default: m = fit_quantile (t, a); break;
    }
  g_free (memo[memo_next].key);
  model_free (memo[memo_next].model);
  memo[memo_next].key = key;
  memo[memo_next].len = len;
  memo[memo_next].model = m;
  memo_next = (memo_next + 1) % G_N_ELEMENTS (memo);
  return m;
}

/* The model's linear predictor for a new case, and its variance under
 * the information matrix (not for OLS, whose l is R). */
static double
model_eta (const Model *m, const double *x0, double *z0, double *var)
{
  double eta;

  design_row (m, x0, z0);
  eta = dot (z0, m->beta, m->q);
  if (var != NULL)
    {
      double *v = g_memdup2 (z0, sizeof (double) * m->q);

      tm_forward_solve (m->l, m->q, v);
      *var = dot (v, v, m->q);
      g_free (v);
    }
  return eta;
}

/* Reads x0, known_y and known_X (args 0, 1, 2) and fits; NULL with *err
 * set if any of it fails.  *x0 is the new case, to free. */
static const Model *
fit_args (TmEvalContext *ctx, TmArg *args, const TmArg *exposures, FitKind kind,
          double a, double **x0, TmValue *err)
{
  Table t;
  const Model *m;

  if (!read_table (ctx, &args[1], &args[2], exposures, &t, err))
    return NULL;
  m = model_get (&t, kind, a);
  table_clear (&t);
  if (m->error != TM_ERR_NONE)
    {
      *err = tm_value_error (m->error);
      return NULL;
    }
  if (x0 != NULL && (*x0 = read_case (ctx, &args[0], m->p, err)) == NULL)
    return NULL;
  return m;
}

/* ---- Multiple regression ---------------------------------------------- */

/* FORECAST.MLR, its interval's half-width and a draw from it.  The draw is
 * from the Student t predictive a flat prior gives: it carries the
 * uncertainty of the line as well as the scatter about it. */
static TmValue
mlr (TmEvalContext *ctx, TmArg *args, int n, int mode)
{
  TmValue err;
  double ridge, conf = 0.95, *x0, *z0, yhat, h0, s;
  const Model *m;

  if (mode == 1)
    {
      OPT_NUM (3, conf, 0.95);
      OPT_NUM (4, ridge, 0);
    }
  else
    OPT_NUM (3, ridge, 0);
  if (ridge < 0 || conf <= 0 || conf >= 1)
    return tm_value_error (TM_ERR_NUM);
  if ((m = fit_args (ctx, args, NULL, FIT_OLS, ridge, &x0, &err)) == NULL)
    return err;
  z0 = g_new (double, m->q);
  yhat = model_eta (m, x0, z0, NULL);
  h0 = tm_lsq_leverage (&(TmLsq) { m->q, m->beta, m->l, 0 }, z0);
  s = sqrt (m->s2 * (1 + h0));
  if (mode < 2)
    {
      g_free (x0);
      g_free (z0);
    }
  switch (mode)
    {
    case 0:  return tm_value_number (yhat);
    case 1:  return tm_value_number (tm_t_inv ((1 + conf) / 2, m->df) * s);
    default:
      {
        /* The coefficients' error, R^-1 z, and the scatter's scale are
         * shared by every cell drawing from this fit in a future, as for
         * RAND.LINEAR; the scatter about the plane is the cell's own. */
        int q = m->q;
        double *key = g_new (double, q + 2), *delta = g_new (double, q), w, y = 0;
        TmRng rng;

        key[0] = 3;
        key[1] = m->s2;
        memcpy (key + 2, m->beta, sizeof (double) * q);
        shared_rng (ctx, key, q + 2, &rng);
        w = draw_scale (&rng, m->df);
        for (int k = 0; k < q; k++)
          delta[k] = tm_rng_normal (&rng);
        for (int k = q - 1; k >= 0; k--)
          {
            for (int j = k + 1; j < q; j++)
              delta[k] -= m->l[k * q + j] * delta[j];
            delta[k] /= m->l[k * q + k];
          }
        for (int k = 0; k < q; k++)
          y += z0[k] * (m->beta[k] + sqrt (m->s2) * w * delta[k]);
        y += sqrt (m->s2) * w * draw_normal (ctx);
        g_free (key);
        g_free (delta);
        g_free (x0);
        g_free (z0);
        return tm_value_number (y);
      }
    }
}

static TmValue fn_mlr         (TmEvalContext *c, TmArg *a, int n) { return mlr (c, a, n, 0); }
static TmValue fn_mlr_confint (TmEvalContext *c, TmArg *a, int n) { return mlr (c, a, n, 1); }
static TmValue fn_rand_mlr    (TmEvalContext *c, TmArg *a, int n) { return mlr (c, a, n, 2); }

/* A coefficient, in the features' own units: 0 the intercept, i the i'th
 * feature's -- how much the outcome moves when it goes up by one. */
static TmValue
fn_mlr_coef (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  Table t;
  double di, ridge;
  int i, k = 1;
  const Model *m;

  OPT_NUM (2, di, 0);
  OPT_NUM (3, ridge, 0);
  if (ridge < 0 || di < 0)
    return tm_value_error (TM_ERR_NUM);
  if (!read_table (ctx, &args[0], &args[1], NULL, &t, &err))
    return err;
  m = model_get (&t, FIT_OLS, ridge);
  table_clear (&t);
  if (m->error != TM_ERR_NONE)
    return tm_value_error (m->error);
  i = (int) di;
  if (i > m->p)
    return tm_value_error (TM_ERR_REF);
  if (i == 0)
    {
      double b0 = m->beta[0];

      for (int j = 0; j < m->p; j++)
        if (m->sd[j] > 0)
          b0 -= m->beta[k++] * m->mean[j] / m->sd[j];
      return tm_value_number (b0);
    }
  for (int j = 0; j < i - 1; j++)
    if (m->sd[j] > 0)
      k++;
  return tm_value_number (m->sd[i - 1] > 0 ? m->beta[k] / m->sd[i - 1] : 0);
}

/* ---- Events, counts and percentiles ------------------------------------ */

/* The chance of the event for the new case.  The coefficients are only
 * estimates; averaging the chance over their uncertainty, by MacKay's
 * probit approximation, pulls it a little towards a half. */
static TmValue
fn_logit_prob (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double ridge, *x0, *z0, eta, var;
  const Model *m;

  OPT_NUM (3, ridge, 0);
  if (ridge < 0)
    return tm_value_error (TM_ERR_NUM);
  if ((m = fit_args (ctx, args, NULL, FIT_LOGIT, ridge, &x0, &err)) == NULL)
    return err;
  z0 = g_new (double, m->q);
  eta = model_eta (m, x0, z0, &var);
  g_free (x0);
  g_free (z0);
  return tm_value_number (1 / (1 + exp (-eta / sqrt (1 + G_PI * var / 8))));
}

/* The expected count for the new case, over its exposure. */
static TmValue
fn_poisson_reg (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double t0, *x0, *z0, eta;
  const Model *m;

  OPT_NUM (4, t0, 1);
  if (!(t0 >= 0))
    return tm_value_error (TM_ERR_NUM);
  if ((m = fit_args (ctx, args, HAS_ARG (3) ? &args[3] : NULL, FIT_POISSON, 0, &x0, &err)) == NULL)
    return err;
  z0 = g_new (double, m->q);
  eta = model_eta (m, x0, z0, NULL);
  g_free (x0);
  g_free (z0);
  return tm_value_number (m->zero ? 0 : t0 * exp (eta));
}

/* A draw of the count: the rate's own uncertainty, and the scatter of
 * counts about it -- wider than Poisson's when the past counts were. */
static TmValue
fn_rand_poisson_reg (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double t0, *x0, *z0, eta, var, lambda, u;
  const Model *m;

  OPT_NUM (4, t0, 1);
  if (!(t0 >= 0))
    return tm_value_error (TM_ERR_NUM);
  if ((m = fit_args (ctx, args, HAS_ARG (3) ? &args[3] : NULL, FIT_POISSON, 0, &x0, &err)) == NULL)
    return err;
  z0 = g_new (double, m->q);
  eta = model_eta (m, x0, z0, &var);
  g_free (x0);
  g_free (z0);
  if (m->zero || t0 == 0)
    return tm_value_number (0);
  lambda = t0 * exp (eta + sqrt (var * m->s2) * draw_normal (ctx));
  u = draw_uniform (ctx);
  if (m->s2 > 1.0001)
    {
      /* Negative binomial, variance phi times the mean: a gamma-mixed
       * Poisson. */
      double shape = lambda / (m->s2 - 1);
      lambda = tm_gamma_inv (tm_rng_uniform (ctx->rng), shape) * (m->s2 - 1);
    }
  return tm_value_number (tm_poisson_inv (u, lambda));
}

static TmValue
fn_quantile_reg (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double tau, *x0, *z0, y;
  const Model *m;

  ARG_NUM (3, tau);
  if (!(tau > 0 && tau < 1))
    return tm_value_error (TM_ERR_NUM);
  if ((m = fit_args (ctx, args, NULL, FIT_QUANTILE, tau, &x0, &err)) == NULL)
    return err;
  z0 = g_new (double, m->q);
  y = model_eta (m, x0, z0, NULL);
  g_free (x0);
  g_free (z0);
  return tm_value_number (y);
}

/* Split conformal prediction: from how far past forecasts missed, the
 * half-width that covers the next with at least the confidence asked --
 * whatever made the forecasts, provided the past misses and the next are
 * alike.  It needs at least 1/(1 - confidence) - 1 of them. */
static TmValue
fn_conformal (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double conf;
  GArray *a;
  double *s;
  int k, m;
  TmValue r;

  OPT_NUM (1, conf, 0.95);
  if (!(conf > 0 && conf < 1))
    return tm_value_error (TM_ERR_NUM);
  if ((a = collect (ctx, args, 1, &err)) == NULL)
    return err;
  m = a->len;
  s = (double *) a->data;
  for (int i = 0; i < m; i++)
    s[i] = fabs (s[i]);
  qsort (s, m, sizeof (double), tm_compare_doubles);
  k = (int) ceil ((m + 1) * conf - 1e-9);
  r = m == 0 ? tm_value_error (TM_ERR_DIV0)
      : k > m ? tm_value_error (TM_ERR_NUM)
      : tm_value_number (s[MAX (k, 1) - 1]);
  g_array_free (a, TRUE);
  return r;
}

/* ---- Nearest neighbours ------------------------------------------------ */

typedef struct {
  double d, y, w;
  int    i;
} Neighbour;

static int
by_distance (const void *pa, const void *pb)
{
  const Neighbour *a = pa, *b = pb;

  if (a->d != b->d)
    return a->d < b->d ? -1 : 1;
  return a->i - b->i;
}

static int
by_outcome (const void *pa, const void *pb)
{
  const Neighbour *a = pa, *b = pb;

  if (a->y != b->y)
    return a->y < b->y ? -1 : 1;
  return a->i - b->i;
}

/* The analogue method: the k past cases nearest the new one, each feature
 * measured in its own standard deviations, and how they turned out.
 * kernel 0 counts them alike; 1 weights them by the tricube of their
 * distance, the nearest most; 2 by its inverse.  The neighbours come back
 * sorted by outcome, their weights summing to 1. */
static Neighbour *
neighbours (const Table *t, const double *x0, int k, int kernel, TmValue *err)
{
  Neighbour *nb = g_new (Neighbour, t->n);
  double *mean = g_new (double, MAX (t->p, 1)), *sd = g_new (double, MAX (t->p, 1));
  double h, total = 0;

  feature_scales (t, mean, sd);
  for (int i = 0; i < t->n; i++)
    {
      double d = 0;

      for (int j = 0; j < t->p; j++)
        if (sd[j] > 0)
          {
            double z = (t->x[i * t->p + j] - x0[j]) / sd[j];
            d += z * z;
          }
      nb[i].d = sqrt (d);
      nb[i].y = t->y[i];
      nb[i].i = i;
    }
  g_free (mean);
  g_free (sd);
  qsort (nb, t->n, sizeof *nb, by_distance);

  /* The tricube's reach: just past the k'th neighbour. */
  h = k < t->n ? nb[k].d : nb[k - 1].d * 1.0001;
  for (int i = 0; i < k; i++)
    {
      double w = 1;

      if (kernel == 1 && h > 0)
        {
          double r = nb[i].d / h;
          w = r < 1 ? pow (1 - r * r * r, 3) : 0;
        }
      else if (kernel == 2)
        w = 1 / (nb[i].d + 1e-9 * (h > 0 ? h : 1));
      nb[i].w = w;
      total += w;
    }
  for (int i = 0; i < k; i++)
    nb[i].w = total > 0 ? nb[i].w / total : 1.0 / k;
  qsort (nb, k, sizeof *nb, by_outcome);
  *err = tm_value_empty ();
  return nb;
}

/* KNN.FORECAST, KNN.PERCENTILE and RAND.KNN. */
static TmValue
knn (TmEvalContext *ctx, TmArg *args, int n, int mode)
{
  TmValue err, r;
  Table t;
  double p = 0.5, dk, kernel, *x0, acc = 0, target;
  int ka = mode == 1 ? 4 : 3, k;
  Neighbour *nb;

  if (mode == 1)
    {
      ARG_NUM (3, p);
      if (p < 0 || p > 1)
        return tm_value_error (TM_ERR_NUM);
    }
  OPT_NUM (ka + 1, kernel, 0);
  if (kernel != 0 && kernel != 1 && kernel != 2)
    return tm_value_error (TM_ERR_NUM);
  if (!read_table (ctx, &args[1], &args[2], NULL, &t, &err))
    return err;
  if (t.n == 0)
    {
      table_clear (&t);
      return tm_value_error (TM_ERR_DIV0);
    }
  if (HAS_ARG (ka))
    {
      if (!tm_arg_number (ctx, &args[ka], &dk, &err))
        {
          table_clear (&t);
          return err;
        }
    }
  else
    dk = MAX (1, floor (sqrt (t.n) + 0.5));
  if (dk < 1 || (x0 = read_case (ctx, &args[0], t.p, &err)) == NULL)
    {
      table_clear (&t);
      return dk < 1 ? tm_value_error (TM_ERR_NUM) : err;
    }
  k = (int) MIN (dk, t.n);
  nb = neighbours (&t, x0, k, (int) kernel, &err);

  if (mode == 0)
    {
      double mean = 0;
      for (int i = 0; i < k; i++)
        mean += nb[i].w * nb[i].y;
      r = tm_value_number (mean);
    }
  else
    {
      /* The weighted distribution of their outcomes: the least outcome
       * with at least the fraction asked at or below it. */
      target = mode == 1 ? p : draw_uniform (ctx);
      r = tm_value_number (nb[k - 1].y);
      for (int i = 0; i < k; i++)
        {
          acc += nb[i].w;
          if (mode == 1 ? acc >= target - 1e-12 : acc > target)
            {
              r = tm_value_number (nb[i].y);
              break;
            }
        }
    }
  g_free (nb);
  g_free (x0);
  table_clear (&t);
  return r;
}

static TmValue fn_knn            (TmEvalContext *c, TmArg *a, int n) { return knn (c, a, n, 0); }
static TmValue fn_knn_percentile (TmEvalContext *c, TmArg *a, int n) { return knn (c, a, n, 1); }
static TmValue fn_rand_knn       (TmEvalContext *c, TmArg *a, int n) { return knn (c, a, n, 2); }

/* ---- Distributions from data ------------------------------------------- */

static double *
sorted_numbers (TmEvalContext *ctx, TmArg *arg, int *n, TmValue *err)
{
  GArray *a = collect (ctx, arg, 1, err);

  if (a == NULL)
    return NULL;
  *n = a->len;
  qsort (a->data, a->len, sizeof (double), tm_compare_doubles);
  return (double *) g_array_free (a, FALSE);
}

static void
mean_sd (const double *x, int n, double *mean, double *sd)
{
  double m = 0, ss = 0;

  for (int i = 0; i < n; i++)
    m += x[i];
  m /= MAX (n, 1);
  for (int i = 0; i < n; i++)
    ss += (x[i] - m) * (x[i] - m);
  *mean = m;
  *sd = n > 1 ? sqrt (ss / (n - 1)) : 0;
}

/* Silverman's rule of thumb for the kernel's width: robust to a long
 * tail, though it smooths two humps into one. */
static double
silverman (const double *sorted, int n)
{
  double mean, sd, iqr, a;

  mean_sd (sorted, n, &mean, &sd);
  iqr = tm_percentile_sorted (sorted, n, 0.75) - tm_percentile_sorted (sorted, n, 0.25);
  a = MIN (sd, iqr / 1.34);
  if (!(a > 0))
    a = MAX (sd, iqr / 1.34);
  return 0.9 * a * pow (n, -0.2);
}

/* The data, sorted, and the kernel width: the one given, or Silverman's. */
static double *
kde_args (TmEvalContext *ctx, TmArg *args, int n, int data, int bw, int *count,
          double *h, TmValue *err)
{
  double *x = sorted_numbers (ctx, &args[data], count, err);

  if (x == NULL)
    return NULL;
  if (*count == 0)
    {
      g_free (x);
      *err = tm_value_error (TM_ERR_DIV0);
      return NULL;
    }
  if (HAS_ARG (bw))
    {
      if (!tm_arg_number (ctx, &args[bw], h, err) || !(*h > 0))
        {
          if (err->type != TM_VALUE_ERROR)
            *err = tm_value_error (TM_ERR_NUM);
          g_free (x);
          return NULL;
        }
    }
  else
    *h = silverman (x, *count);
  return x;
}

static double
kde_cdf (const double *x, int n, double h, double at)
{
  double s = 0;

  for (int i = 0; i < n; i++)
    s += h > 0 ? tm_norm_cdf ((at - x[i]) / h) : at >= x[i];
  return s / n;
}

/* KDE.DIST(x, data, [cumulative], [bandwidth]): the smooth density a
 * kernel density estimate puts on the data, or its distribution function. */
static TmValue
fn_kde_dist (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double at, h, s = 0, *x;
  gboolean cumulative = FALSE;
  int m;

  ARG_NUM (0, at);
  if (HAS_ARG (2))
    ARG_BOOL (2, cumulative);
  if ((x = kde_args (ctx, args, n, 1, 3, &m, &h, &err)) == NULL)
    return err;
  if (cumulative)
    s = kde_cdf (x, m, h, at);
  else if (h > 0)
    {
      for (int i = 0; i < m; i++)
        {
          double z = (at - x[i]) / h;
          s += exp (-z * z / 2);
        }
      s /= m * h * sqrt (2 * G_PI);
    }
  g_free (x);
  if (!cumulative && !(h > 0))
    return tm_value_error (TM_ERR_DIV0);
  return tm_value_number (s);
}

static TmValue
fn_kde_inv (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double p, h, lo, hi, *x;
  int m;

  ARG_NUM (0, p);
  if (!(p > 0 && p < 1))
    return tm_value_error (TM_ERR_NUM);
  if ((x = kde_args (ctx, args, n, 1, 2, &m, &h, &err)) == NULL)
    return err;
  lo = x[0] - 10 * h;
  hi = x[m - 1] + 10 * h;
  for (int i = 0; i < 200 && hi - lo > 1e-12 * MAX (1.0, fabs (hi)); i++)
    {
      double mid = (lo + hi) / 2;
      if (kde_cdf (x, m, h, mid) < p)
        lo = mid;
      else
        hi = mid;
    }
  g_free (x);
  return tm_value_number ((lo + hi) / 2);
}

/* A draw from the kernel density estimate -- the smoothed bootstrap: one
 * of the data, jittered by the kernel.  One uniform does both, the whole
 * part of u times n picking the datum and the fraction left the jitter, so
 * that Latin hypercube strata carry through.  Values below lower are
 * reflected back above it, for things that cannot be negative. */
static TmValue
fn_rand_kde (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double h, lower, u, f, v, *x;
  int m, i;

  if ((x = kde_args (ctx, args, n, 0, 1, &m, &h, &err)) == NULL)
    return err;
  if (HAS_ARG (2) && !tm_arg_number (ctx, &args[2], &lower, &err))
    {
      g_free (x);
      return err;
    }
  u = draw_uniform (ctx) * m;
  i = MIN (m - 1, (int) floor (u));
  f = CLAMP (u - i, 1e-12, 1 - 1e-12);
  v = x[i] + h * tm_norm_inv (f);
  if (HAS_ARG (2) && v < lower)
    v = 2 * lower - v;
  g_free (x);
  return tm_value_number (v);
}

/* The data's own distribution, joined up: its percentiles, interpolated
 * as PERCENTILE does, so that a draw can fall between the values seen but
 * never beyond them. */
static TmValue
fn_rand_empirical (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  int m;
  double *x = sorted_numbers (ctx, &args[0], &m, &err), v;

  if (x == NULL)
    return err;
  if (m == 0)
    {
      g_free (x);
      return tm_value_error (TM_ERR_DIV0);
    }
  v = tm_percentile_sorted (x, m, draw_uniform (ctx));
  g_free (x);
  return tm_value_number (v);
}

/* The next value from wherever the data came from, if it is roughly
 * normal: its mean and spread are known only from the data, so the next
 * value is Student's t about the mean, a little wider than the data. */
static TmValue
fn_rand_next (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  GArray *a = collect (ctx, args, 1, &err);
  double mean, sd, u;
  int m;

  if (a == NULL)
    return err;
  m = a->len;
  mean_sd ((double *) a->data, m, &mean, &sd);
  g_array_free (a, TRUE);
  if (m < 2)
    return tm_value_error (TM_ERR_DIV0);
  u = CLAMP (draw_uniform (ctx), 1e-12, 1 - 1e-12);
  return tm_value_number (mean + sd * sqrt (1 + 1.0 / m) * tm_t_inv (u, m - 1));
}

/* ---- Updating on evidence ---------------------------------------------- */

static gboolean
sum_arg (TmEvalContext *ctx, TmArg *arg, double *sum, int *count, TmValue *err)
{
  GArray *a = collect (ctx, arg, 1, err);

  if (a == NULL)
    return FALSE;
  *sum = 0;
  for (guint i = 0; i < a->len; i++)
    *sum += g_array_index (a, double, i);
  if (count != NULL)
    *count = a->len;
  g_array_free (a, TRUE);
  return TRUE;
}

/* A proportion learnt from successes in trials, by Bayes' rule from a
 * beta prior -- uniform unless told otherwise, which makes the mean
 * Laplace's (s + 1) / (n + 2).  With future trials, how many of those
 * succeed: the beta-binomial, the proportion's uncertainty and the
 * trials' luck together. */
static TmValue
fn_rand_proportion (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double s, t, m, a, b, u;

  if (!sum_arg (ctx, &args[0], &s, NULL, &err) || !sum_arg (ctx, &args[1], &t, NULL, &err))
    return err;
  OPT_NUM (3, a, 1);
  OPT_NUM (4, b, 1);
  if (s < 0 || t < s || !(a > 0) || !(b > 0))
    return tm_value_error (TM_ERR_NUM);
  a += s;
  b += t - s;
  u = CLAMP (draw_uniform (ctx), 1e-12, 1 - 1e-12);
  if (!HAS_ARG (2))
    return tm_value_number (tm_beta_inv (u, a, b));
  ARG_NUM (2, m);
  m = floor (m);
  if (m < 0)
    return tm_value_error (TM_ERR_NUM);
  if (m <= 2000)
    {
      /* The beta-binomial's own distribution function, term by term. */
      double pmf = exp (lgamma (a + b) - lgamma (b) + lgamma (m + b) - lgamma (a + m + b)), acc = 0;

      for (double k = 0; k < m; k++)
        {
          acc += pmf;
          if (acc >= u)
            return tm_value_number (k);
          pmf *= (m - k) / (k + 1) * (k + a) / (m - k - 1 + b);
        }
      return tm_value_number (m);
    }
  return tm_value_number (tm_binomial_inv (tm_rng_uniform (ctx->rng), m, tm_beta_inv (u, a, b)));
}

/* A rate learnt from events over exposure, by Bayes' rule from a gamma
 * prior (Jeffreys', shape a half and rate 0, unless told otherwise).  With
 * a future exposure, how many events it brings: the negative binomial. */
static TmValue
fn_rand_rate (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double y, t, t0, a, b, u;

  if (!sum_arg (ctx, &args[0], &y, NULL, &err) || !sum_arg (ctx, &args[1], &t, NULL, &err))
    return err;
  OPT_NUM (3, a, 0.5);
  OPT_NUM (4, b, 0);
  if (y < 0 || t < 0 || !(a > 0) || b < 0 || !(t + b > 0))
    return tm_value_error (TM_ERR_NUM);
  a += y;
  b += t;
  u = CLAMP (draw_uniform (ctx), 1e-12, 1 - 1e-12);
  if (!HAS_ARG (2))
    return tm_value_number (tm_gamma_inv (u, a) / b);
  ARG_NUM (2, t0);
  if (t0 < 0)
    return tm_value_number (TM_ERR_NUM);
  if (t0 == 0)
    return tm_value_number (0);
  {
    double q = b / (b + t0), mean = a * t0 / b;

    if (a * log (q) > -600 && mean < 1e5)
      {
        double pmf = exp (a * log (q)), acc = 0;

        for (double k = 0; k < 1e7; k++)
          {
            acc += pmf;
            if (acc >= u || pmf < 1e-300 * (k > mean))
              return tm_value_number (k);
            pmf *= (a + k) / (k + 1) * (1 - q);
          }
      }
    return tm_value_number (tm_poisson_inv (tm_rng_uniform (ctx->rng), tm_gamma_inv (u, a) / b * t0));
  }
}

/* Partial pooling: one estimate among many of the same kind -- a school's
 * results, a store's growth, a surgeon's record -- pulled towards their
 * common mean by as much as its own noise, against the real spread between
 * them, warrants.  DerSimonian and Laird's estimate of that spread. */
static TmValue
fn_shrink (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  const TmArg *cols_args[2] = { &args[2], &args[3] };
  double *cols[2], y, se, sw = 0, sw2 = 0, swy = 0, qstat = 0, tau2, num = 0, den = 0, mu, b;
  int k;

  ARG_NUM (0, y);
  ARG_NUM (1, se);
  if (!(se >= 0))
    return tm_value_error (TM_ERR_NUM);
  if (!tm_arg_columns (ctx, cols_args, 2, cols, &k, &err))
    return err;
  for (int i = 0; i < k; i++)
    if (!(cols[1][i] > 0))
      k = -1;
  if (k < 2)
    {
      g_free (cols[0]);
      g_free (cols[1]);
      return tm_value_error (k < 0 ? TM_ERR_NUM : TM_ERR_DIV0);
    }
  for (int i = 0; i < k; i++)
    {
      double w = 1 / (cols[1][i] * cols[1][i]);
      sw += w;
      sw2 += w * w;
      swy += w * cols[0][i];
    }
  for (int i = 0; i < k; i++)
    {
      double w = 1 / (cols[1][i] * cols[1][i]), d = cols[0][i] - swy / sw;
      qstat += w * d * d;
    }
  tau2 = MAX (0.0, (qstat - (k - 1)) / (sw - sw2 / sw));
  for (int i = 0; i < k; i++)
    {
      double w = 1 / (cols[1][i] * cols[1][i] + tau2);
      num += w * cols[0][i];
      den += w;
    }
  mu = num / den;
  g_free (cols[0]);
  g_free (cols[1]);
  b = se * se + tau2 > 0 ? se * se / (se * se + tau2) : 1;
  return tm_value_number (mu + (1 - b) * (y - mu));
}

/* The same for rates: a unit's successes in its trials, pulled towards
 * the rate of all of them together, by a beta prior fitted to the spread
 * of the units' rates beyond what their trials alone would make. */
static TmValue
fn_shrink_rate (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  const TmArg *cols_args[2] = { &args[2], &args[3] };
  double *cols[2], s, t, ss = 0, st = 0, m, v = 0, inv = 0, tau2, nu;
  int k;

  ARG_NUM (0, s);
  ARG_NUM (1, t);
  if (s < 0 || t < s)
    return tm_value_error (TM_ERR_NUM);
  if (!tm_arg_columns (ctx, cols_args, 2, cols, &k, &err))
    return err;
  for (int i = 0; i < k; i++)
    {
      if (cols[0][i] < 0 || !(cols[1][i] > 0) || cols[0][i] > cols[1][i])
        k = -1;
      else
        {
          ss += cols[0][i];
          st += cols[1][i];
        }
    }
  if (k < 2 || ss <= 0 || ss >= st)
    {
      g_free (cols[0]);
      g_free (cols[1]);
      return tm_value_error (k < 0 ? TM_ERR_NUM : TM_ERR_DIV0);
    }
  m = ss / st;
  for (int i = 0; i < k; i++)
    {
      double p = cols[0][i] / cols[1][i];
      v += cols[1][i] * (p - m) * (p - m) / st;
      inv += 1 / cols[1][i] / k;
    }
  g_free (cols[0]);
  g_free (cols[1]);
  tau2 = MAX (1e-12, v - m * (1 - m) * inv);
  nu = MAX (1e-9, m * (1 - m) / tau2 - 1);
  return tm_value_number ((m * nu + s) / (nu + t));
}

/* ---- Lifetimes ---------------------------------------------------------- */

static TmValue
fn_weibull_dist (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double x, k, lambda;
  gboolean cumulative;

  ARG_NUM (0, x);
  ARG_NUM (1, k);
  ARG_NUM (2, lambda);
  ARG_BOOL (3, cumulative);
  if (x < 0 || !(k > 0) || !(lambda > 0))
    return tm_value_error (TM_ERR_NUM);
  if (cumulative)
    return tm_value_number (1 - exp (-pow (x / lambda, k)));
  return tm_value_number (k / lambda * pow (x / lambda, k - 1) * exp (-pow (x / lambda, k)));
}

/* A Weibull lifetime; given that it has lasted to survived_to, the age at
 * which it ends. */
static TmValue
fn_rand_weibull (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double k, lambda, age, u;

  ARG_NUM (0, k);
  ARG_NUM (1, lambda);
  OPT_NUM (2, age, 0);
  if (!(k > 0) || !(lambda > 0) || age < 0)
    return tm_value_error (TM_ERR_NUM);
  u = CLAMP (draw_uniform (ctx), 0, 1 - 1e-16);
  return tm_value_number (lambda * pow (pow (age / lambda, k) - log1p (-u), 1 / k));
}

/* Lifetimes and whether each ended: 1 if it failed at that time, 0 if it
 * was still going (censored).  Without the flags, all failed. */
static gboolean
read_lifetimes (TmEvalContext *ctx, const TmArg *times, const TmArg *flags,
                double **t, double **d, int *m, TmValue *err)
{
  const TmArg *list[2] = { times, flags };
  double *cols[2];

  if (!tm_arg_columns (ctx, list, flags != NULL ? 2 : 1, cols, m, err))
    return FALSE;
  *t = cols[0];
  if (flags != NULL)
    *d = cols[1];
  else
    {
      *d = g_new (double, MAX (*m, 1));
      for (int i = 0; i < *m; i++)
        (*d)[i] = 1;
    }
  for (int i = 0; i < *m; i++)
    if ((*t)[i] < 0 || ((*d)[i] != 0 && (*d)[i] != 1))
      {
        g_free (*t);
        g_free (*d);
        *err = tm_value_error (TM_ERR_NUM);
        return FALSE;
      }
  return TRUE;
}

/* WEIBULL.FIT(times, "shape"|"scale", [failed]): the Weibull that best
 * explains the lifetimes, by maximum likelihood, counting the units still
 * going for as long as they have lasted.  Shape below 1: failures come
 * early; 1: at random; above 1: they wear out. */
static TmValue
fn_weibull_fit (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double *t, *d, big = 0, mean_log = 0, lo = log (0.02), hi = log (50), k = 1, sum = 0;
  char *which;
  int m, r = 0;
  gboolean scale;

  if ((which = tm_arg_text (ctx, &args[1], &err)) == NULL)
    return err;
  scale = g_ascii_strcasecmp (which, "scale") == 0;
  if (!scale && g_ascii_strcasecmp (which, "shape") != 0)
    {
      g_free (which);
      return tm_value_error (TM_ERR_VALUE);
    }
  g_free (which);
  if (!read_lifetimes (ctx, &args[0], HAS_ARG (2) ? &args[2] : NULL, &t, &d, &m, &err))
    return err;
  for (int i = 0; i < m; i++)
    {
      big = MAX (big, t[i]);
      if (d[i] == 1)
        {
          if (!(t[i] > 0))
            r = -m;
          r++;
        }
    }
  if (r < 1 || !(big > 0))
    {
      g_free (t);
      g_free (d);
      return tm_value_error (r < 0 ? TM_ERR_NUM : TM_ERR_DIV0);
    }
  /* On times scaled by the longest, so that t^k cannot overflow; the
   * shape does not care. */
  for (int i = 0; i < m; i++)
    {
      t[i] /= big;
      if (d[i] == 1)
        mean_log += log (t[i]) / r;
    }
  for (int iter = 0; iter < 200; iter++)
    {
      double sa = 0, sb = 0;

      k = exp ((lo + hi) / 2);
      for (int i = 0; i < m; i++)
        if (t[i] > 0)
          {
            double tk = pow (t[i], k);
            sa += tk * log (t[i]);
            sb += tk;
          }
      if (sa / sb - 1 / k - mean_log < 0)
        lo = log (k);
      else
        hi = log (k);
    }
  for (int i = 0; i < m; i++)
    sum += t[i] > 0 ? pow (t[i], k) : 0;
  g_free (t);
  g_free (d);
  return tm_value_number (scale ? big * pow (sum / r, 1 / k) : k);
}

/* The Kaplan-Meier survival curve's steps: at each time something ended,
 * the share of those still at risk that did. */
typedef struct {
  double t, s;
} Step;

static int
by_time (const void *pa, const void *pb)
{
  const double *a = pa, *b = pb;

  /* Ties: ends before censorings, as Kaplan and Meier count them. */
  if (a[0] != b[0])
    return a[0] < b[0] ? -1 : 1;
  return (b[1] > a[1]) - (b[1] < a[1]);
}

static Step *
kaplan_meier (const double *t, const double *d, int m, int *steps, double *events, double *exposure)
{
  double *pairs = g_new (double, 2 * MAX (m, 1)), s = 1;
  Step *out = g_new (Step, MAX (m, 1));
  int i = 0;

  *steps = 0;
  *events = 0;
  *exposure = 0;
  for (int j = 0; j < m; j++)
    {
      pairs[2 * j] = t[j];
      pairs[2 * j + 1] = d[j];
      *events += d[j];
      *exposure += t[j];
    }
  qsort (pairs, m, 2 * sizeof (double), by_time);
  while (i < m)
    {
      double at = pairs[2 * i];
      int ended = 0, risk = m - i;

      while (i < m && pairs[2 * i] == at)
        {
          ended += pairs[2 * i + 1] == 1;
          i++;
        }
      if (ended > 0)
        {
          s *= 1 - (double) ended / risk;
          out[*steps].t = at;
          out[*steps].s = s;
          (*steps)++;
        }
    }
  g_free (pairs);
  return out;
}

static TmValue
fn_kaplan_meier (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double at, *t, *d, events, exposure, s = 1;
  int m, steps;
  Step *km;

  ARG_NUM (0, at);
  if (!read_lifetimes (ctx, &args[1], HAS_ARG (2) ? &args[2] : NULL, &t, &d, &m, &err))
    return err;
  km = kaplan_meier (t, d, m, &steps, &events, &exposure);
  for (int i = 0; i < steps && km[i].t <= at; i++)
    s = km[i].s;
  g_free (t);
  g_free (d);
  g_free (km);
  if (m == 0)
    return tm_value_error (TM_ERR_DIV0);
  return tm_value_number (s);
}

/* A lifetime drawn from the Kaplan-Meier curve; past its last step, where
 * the data run out, from an exponential tail at the overall rate. */
static TmValue
fn_rand_survival (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err, r;
  double *t, *d, events, exposure, u, last;
  int m, steps;
  Step *km;

  if (!read_lifetimes (ctx, &args[0], HAS_ARG (1) ? &args[1] : NULL, &t, &d, &m, &err))
    return err;
  km = kaplan_meier (t, d, m, &steps, &events, &exposure);
  g_free (t);
  g_free (d);
  if (steps == 0 || !(exposure > 0))
    {
      g_free (km);
      return tm_value_error (steps == 0 ? TM_ERR_DIV0 : TM_ERR_NUM);
    }
  u = CLAMP (draw_uniform (ctx), 1e-12, 1 - 1e-12);
  last = km[steps - 1].s;
  r = tm_value_empty ();
  for (int i = 0; i < steps; i++)
    if (km[i].s <= 1 - u)
      {
        r = tm_value_number (km[i].t);
        break;
      }
  if (r.type == TM_VALUE_EMPTY)
    r = tm_value_number (km[steps - 1].t - log ((1 - u) / last) / (events / exposure));
  g_free (km);
  return r;
}

/* ---- Inputs that move together ----------------------------------------- */

/* A uniform draw, correlated with the others drawn from the same matrix of
 * correlations: the Gaussian copula.  Feed it to any inverse -- NORM.INV,
 * BETA.INV, LOGNORM.INV, PERCENTILE of history -- to give that input its
 * own distribution and the correlations too.  Within a future every cell
 * that names the same matrix and group sees the same draw of all p
 * normals, of which it takes the index'th. */
static TmValue
fn_rand_copula (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  int rows, cols, count, p, idx;
  double di, group, *c = NULL, *l = NULL, *eps = NULL, zi = 0;
  const TmValue **cells;
  guint64 key = 1469598103934665603ULL;
  TmRng rng;
  TmValue r = tm_value_error (TM_ERR_NUM);

  ARG_NUM (1, di);
  OPT_NUM (2, group, 0);
  shape_of (&args[0], &rows, &cols);
  cells = tm_arg_cells (ctx, &args[0], &count);
  p = rows;
  idx = (int) di;
  if (has_error (cells, count, &err))
    {
      g_free (cells);
      return err;
    }
  if (rows != cols || idx < 1 || idx > p)
    {
      g_free (cells);
      return tm_value_error (rows != cols ? TM_ERR_VALUE : TM_ERR_REF);
    }
  c = g_new (double, p * p);
  for (int i = 0; i < p * p; i++)
    {
      double v;

      if (!as_number (cells[i], &v))
        goto out;
      c[i] = v;
    }
  for (int i = 0; i < p; i++)
    for (int j = 0; j < p; j++)
      {
        /* Either triangle will do; blanks in the other are allowed. */
        double v = i == j ? 1 : (c[i * p + j] + c[j * p + i]) / 2;
        if (fabs (v) > 1)
          goto out;
      }
  /* The key: the matrix and the group, as the stream's name. */
  for (int i = 0; i < p * p; i++)
    {
      guint64 bits;
      memcpy (&bits, &c[i], sizeof bits);
      key = (key ^ bits) * 1099511628211ULL;
    }
  key = (key ^ (guint64) (gint64) group) * 1099511628211ULL;

  /* Symmetric, unit diagonal; shrunk towards independence until it is a
   * proper correlation matrix, if it is not one already. */
  l = g_new (double, p * p);
  for (double w = 1; ; w -= 0.01)
    {
      for (int i = 0; i < p; i++)
        for (int j = 0; j < p; j++)
          l[i * p + j] = i == j ? 1 : w * (c[i * p + j] + c[j * p + i]) / 2;
      if (tm_cholesky (l, p) || w <= 0)
        break;
    }
  eps = g_new (double, p);
  if (ctx->shared_stream != NULL)
    ctx->shared_stream (ctx->data, key, &rng);
  else
    rng = *ctx->rng;
  for (int i = 0; i < p; i++)
    eps[i] = tm_rng_normal (&rng);
  for (int j = 0; j < idx; j++)
    zi += l[(idx - 1) * p + j] * eps[j];
  r = tm_value_number (tm_norm_cdf (zi));

out:
  g_free (cells);
  g_free (c);
  g_free (l);
  g_free (eps);
  return r;
}

/* ---- Series ------------------------------------------------------------- */

static GArray *
series_arg (TmEvalContext *ctx, TmArg *arg, int least, TmValue *err)
{
  GArray *a = collect (ctx, arg, 1, err);

  if (a != NULL && (int) a->len < least)
    {
      g_array_free (a, TRUE);
      *err = tm_value_error (TM_ERR_DIV0);
      return NULL;
    }
  return a;
}

/* Simple exponential smoothing's one-step squared error, and its last
 * level. */
static double
ses (const double *y, int n, double alpha, double *level)
{
  double l = y[0], sse = 0;

  for (int i = 1; i < n; i++)
    {
      double e = y[i] - l;
      sse += e * e;
      l += alpha * e;
    }
  *level = l;
  return sse;
}

/* The Theta method, which won the M3 competition: simple exponential
 * smoothing plus half the series' straight-line trend, as Hyndman and
 * Billah showed it to be. */
static TmValue
fn_theta (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  GArray *a;
  double h, *y, st = 0, sy = 0, stt = 0, sty = 0, b, lo = 0.001, hi = 0.999, level, alpha;
  int m;
  const double phi = (sqrt (5) - 1) / 2;

  ARG_NUM (0, h);
  if (h < 0)
    return tm_value_error (TM_ERR_NUM);
  if ((a = series_arg (ctx, &args[1], 3, &err)) == NULL)
    return err;
  y = (double *) a->data;
  m = a->len;
  for (int i = 0; i < m; i++)
    {
      st += i;
      sy += y[i];
      stt += (double) i * i;
      sty += i * y[i];
    }
  b = (m * sty - st * sy) / (m * stt - st * st);
  /* The smoothing weight that forecast the series best a step ahead, by
   * golden-section search. */
  for (int i = 0; i < 60; i++)
    {
      double c = hi - phi * (hi - lo), d = lo + phi * (hi - lo);
      if (ses (y, m, c, &level) < ses (y, m, d, &level))
        hi = d;
      else
        lo = c;
    }
  alpha = (lo + hi) / 2;
  ses (y, m, alpha, &level);
  g_array_free (a, TRUE);
  return tm_value_number (level + b / 2 * (h - 1 + (1 - pow (1 - alpha, m)) / alpha));
}

/* Croston's method for intermittent demand -- spare parts, rare
 * incidents -- smoothing the sizes of the non-zero periods and the gaps
 * between them apart; with Syntetos and Boylan's correction for its bias,
 * unless sba is FALSE.  The demand to expect a period. */
static TmValue
fn_croston (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  GArray *a;
  double alpha, size = 0, gap = 0, since = 0;
  gboolean sba = TRUE, started = FALSE;

  OPT_NUM (1, alpha, 0.1);
  if (HAS_ARG (2))
    ARG_BOOL (2, sba);
  if (!(alpha > 0 && alpha <= 1))
    return tm_value_error (TM_ERR_NUM);
  if ((a = series_arg (ctx, &args[0], 1, &err)) == NULL)
    return err;
  for (guint i = 0; i < a->len; i++)
    {
      double y = g_array_index (a, double, i);

      since++;
      if (y < 0)
        {
          g_array_free (a, TRUE);
          return tm_value_error (TM_ERR_NUM);
        }
      if (y > 0)
        {
          if (!started)
            {
              size = y;
              gap = since;
              started = TRUE;
            }
          else
            {
              size += alpha * (y - size);
              gap += alpha * (since - gap);
            }
          since = 0;
        }
    }
  g_array_free (a, TRUE);
  if (!started)
    return tm_value_number (0);
  return tm_value_number ((sba ? 1 - alpha / 2 : 1) * size / gap);
}

/* The sum of squares about the mean of y[a..b), from running sums. */
static double
segment_sse (const double *s, const double *s2, int a, int b)
{
  double sum = s[b] - s[a];

  return s2[b] - s2[a] - sum * sum / (b - a);
}

/* Binary segmentation of the segment [a, b): where its best split is, if
 * splitting there lowers the squared error by more than the penalty. */
static int
latest_shift (const double *s, const double *s2, int a, int b, double penalty)
{
  double whole = segment_sse (s, s2, a, b), best = INFINITY;
  int at = -1;

  if (b - a < 4)
    return -1;
  for (int c = a + 2; c <= b - 2; c++)
    {
      double split = segment_sse (s, s2, a, c) + segment_sse (s, s2, c, b);
      if (split < best)
        {
          best = split;
          at = c;
        }
    }
  if (whole - best <= penalty)
    return -1;
  {
    int later = latest_shift (s, s2, at, b, penalty);
    return later >= 0 ? later : at;
  }
}

/* Where the series' current level began: the position, from 1, of the
 * first value after its latest shift in level -- to forecast from the
 * present regime rather than the whole of history.  1 if it has not
 * shifted. */
static TmValue
fn_changepoint (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  GArray *a;
  double *y, *s, *s2, *diffs, sigma;
  int m, at;

  if ((a = series_arg (ctx, &args[0], 1, &err)) == NULL)
    return err;
  y = (double *) a->data;
  m = a->len;
  if (m < 4)
    {
      g_array_free (a, TRUE);
      return tm_value_number (1);
    }
  s = g_new0 (double, m + 1);
  s2 = g_new0 (double, m + 1);
  diffs = g_new (double, m - 1);
  for (int i = 0; i < m; i++)
    {
      s[i + 1] = s[i] + y[i];
      s2[i + 1] = s2[i] + y[i] * y[i];
    }
  /* The noise, from the median step between neighbours, which a shift in
   * level hardly moves. */
  for (int i = 1; i < m; i++)
    diffs[i - 1] = fabs (y[i] - y[i - 1]);
  qsort (diffs, m - 1, sizeof (double), tm_compare_doubles);
  sigma = tm_percentile_sorted (diffs, m - 1, 0.5) / (0.6745 * G_SQRT2);
  if (!(sigma > 0))
    {
      double mean, sd;
      mean_sd (y, m, &mean, &sd);
      sigma = sd > 0 ? sd / 10 : 1;
    }
  at = latest_shift (s, s2, 0, m, 3 * log (m) * sigma * sigma);
  g_free (s);
  g_free (s2);
  g_free (diffs);
  g_array_free (a, TRUE);
  return tm_value_number (at < 0 ? 1 : at + 1);
}

/* The method of analogues for a series: the stretches of history most
 * like its last few values, and how the series moved in the steps after
 * each.  The forecast is the last value plus those moves -- their mean,
 * or one of them for a draw. */
static TmValue
analog (TmEvalContext *ctx, TmArg *args, int n, gboolean draw)
{
  TmValue err;
  GArray *a;
  double dh, dw, dk, *y, mean, sd;
  int h, w, m, cands, k;
  Neighbour *nb;
  TmValue r;

  ARG_NUM (0, dh);
  OPT_NUM (2, dw, 3);
  if (dh < 1 || dw < 1)
    return tm_value_error (TM_ERR_NUM);
  if ((a = series_arg (ctx, &args[1], 2, &err)) == NULL)
    return err;
  y = (double *) a->data;
  m = a->len;
  h = (int) dh;
  w = (int) dw;
  cands = m - h - w + 1;
  if (cands < 1)
    {
      g_array_free (a, TRUE);
      return tm_value_error (TM_ERR_DIV0);
    }
  if (HAS_ARG (3))
    {
      if (!tm_arg_number (ctx, &args[3], &dk, &err))
        {
          g_array_free (a, TRUE);
          return err;
        }
    }
  else
    dk = MAX (1, floor (sqrt (cands) + 0.5));
  if (dk < 1)
    {
      g_array_free (a, TRUE);
      return tm_value_error (TM_ERR_NUM);
    }
  k = (int) MIN (dk, cands);
  mean_sd (y, m, &mean, &sd);
  if (!(sd > 0))
    sd = 1;
  nb = g_new (Neighbour, cands);
  for (int c = 0; c < cands; c++)
    {
      /* The stretch ending at t = c + w - 1, against the last w values. */
      double d = 0;
      int t = c + w - 1;

      for (int j = 0; j < w; j++)
        {
          double z = (y[c + j] - y[m - w + j]) / sd;
          d += z * z;
        }
      nb[c].d = d;
      nb[c].y = y[t + h] - y[t];
      nb[c].i = -t;          /* ties: the most recent first */
      nb[c].w = 1.0 / k;
    }
  qsort (nb, cands, sizeof *nb, by_distance);
  if (!draw)
    {
      double s = 0;
      for (int i = 0; i < k; i++)
        s += nb[i].y / k;
      r = tm_value_number (y[m - 1] + s);
    }
  else
    {
      int pick = MIN (k - 1, (int) floor (draw_uniform (ctx) * k));
      qsort (nb, k, sizeof *nb, by_outcome);
      r = tm_value_number (y[m - 1] + nb[pick].y);
    }
  g_free (nb);
  g_array_free (a, TRUE);
  return r;
}

static TmValue fn_forecast_analog (TmEvalContext *c, TmArg *a, int n) { return analog (c, a, n, FALSE); }
static TmValue fn_rand_analog     (TmEvalContext *c, TmArg *a, int n) { return analog (c, a, n, TRUE); }

/* ---- Extremes ------------------------------------------------------------ */

/* Peaks over a threshold: the excesses of the data above their
 * threshold'th percentile, fitted with a generalised Pareto tail by
 * Hosking and Wallis's probability-weighted moments. */
typedef struct {
  double u, xi, sigma, rate;   /* threshold; shape; scale; share above */
} Tail;

static gboolean
fit_tail (const double *x, int m, double threshold, Tail *tail)
{
  int nu = 0, start;
  double a0 = 0, a1 = 0;

  tail->u = tm_percentile_sorted (x, m, threshold);
  for (start = 0; start < m && x[start] <= tail->u; start++)
    ;
  nu = m - start;
  if (nu < 5)
    return FALSE;
  for (int i = 0; i < nu; i++)
    {
      double e = x[start + i] - tail->u;
      a0 += e / nu;
      a1 += (double) (nu - 1 - i) / (nu - 1) * e / nu;
    }
  if (!(a0 - 2 * a1 > 0))
    return FALSE;
  tail->xi = 2 - a0 / (a0 - 2 * a1);
  tail->sigma = 2 * a0 * a1 / (a0 - 2 * a1);
  tail->rate = (double) nu / m;
  return tail->sigma > 0;
}

static TmValue
extreme (TmEvalContext *ctx, TmArg *args, int n, gboolean level)
{
  TmValue err;
  double v, threshold, *x;
  int m;
  Tail tail;
  TmValue r;

  ARG_NUM (0, v);
  OPT_NUM (2, threshold, 0.9);
  if (!(threshold > 0 && threshold < 1) || (level && !(v > 1)))
    return tm_value_error (TM_ERR_NUM);
  if ((x = sorted_numbers (ctx, &args[1], &m, &err)) == NULL)
    return err;
  if (!fit_tail (x, m, threshold, &tail))
    r = tm_value_error (TM_ERR_NUM);
  else if (!level)
    {
      if (v <= tail.u)
        {
          int above = 0;
          for (int i = 0; i < m; i++)
            above += x[i] > v;
          r = tm_value_number ((double) above / m);
        }
      else
        {
          double z = (v - tail.u) / tail.sigma;
          if (fabs (tail.xi) < 1e-9)
            r = tm_value_number (tail.rate * exp (-z));
          else
            {
              double base = 1 + tail.xi * z;
              r = tm_value_number (base <= 0 ? 0 : tail.rate * pow (base, -1 / tail.xi));
            }
        }
    }
  else
    {
      /* The level exceeded once in v observations. */
      double k = v * tail.rate;
      if (k <= 1)
        r = tm_value_number (tm_percentile_sorted (x, m, 1 - 1 / v));
      else if (fabs (tail.xi) < 1e-9)
        r = tm_value_number (tail.u + tail.sigma * log (k));
      else
        r = tm_value_number (tail.u + tail.sigma / tail.xi * (pow (k, tail.xi) - 1));
    }
  g_free (x);
  return r;
}

static TmValue fn_tail_prob    (TmEvalContext *c, TmArg *a, int n) { return extreme (c, a, n, FALSE); }
static TmValue fn_return_level (TmEvalContext *c, TmArg *a, int n) { return extreme (c, a, n, TRUE); }

/* ---- Queues and lifetimes with no data ------------------------------------ */

/* Erlang's C formula: callers arriving at random at a rate, served by so
 * many servers each finishing at their own rate -- the chance a caller has
 * to wait, or ("wait") the mean wait, in the rates' unit of time. */
static TmValue
fn_erlang_c (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double lambda, mu, dc, a, b = 1, c, pw;
  gboolean wait = FALSE;

  ARG_NUM (0, lambda);
  ARG_NUM (1, mu);
  ARG_NUM (2, dc);
  if (HAS_ARG (3))
    {
      char *which = tm_arg_text (ctx, &args[3], &err);
      if (which == NULL)
        return err;
      wait = g_ascii_strcasecmp (which, "wait") == 0;
      if (!wait && g_ascii_strcasecmp (which, "prob") != 0)
        {
          g_free (which);
          return tm_value_error (TM_ERR_VALUE);
        }
      g_free (which);
    }
  c = floor (dc);
  if (lambda < 0 || !(mu > 0) || c < 1)
    return tm_value_error (TM_ERR_NUM);
  a = lambda / mu;
  if (a >= c)
    return tm_value_error (TM_ERR_NUM);   /* the queue grows without end */
  /* Erlang B by its recurrence, then C from it. */
  for (int k = 1; k <= (int) c; k++)
    b = a * b / (k + a * b);
  pw = c * b / (c - a * (1 - b));
  return tm_value_number (wait ? pw / (c * mu - lambda) : pw);
}

/* Gott's rule, or Lindy's law: something that has lasted age, seen at a
 * random moment of its life, has a remaining life with even odds of being
 * longer than age, and a chance 1/(1 + x) of lasting x times age more.
 * For when there is nothing else to go on.  Its mean is infinite: read
 * its percentiles. */
static TmValue
fn_rand_lindy (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double age, u;

  ARG_NUM (0, age);
  if (age < 0)
    return tm_value_error (TM_ERR_NUM);
  u = CLAMP (draw_uniform (ctx), 0, 1 - 1e-12);
  return tm_value_number (age * u / (1 - u));
}

/* ---- Recalibration ------------------------------------------------------- */

/* CALIBRATE(p, past_probabilities, past_outcomes, [method]): a probability
 * corrected by the forecaster's own record.  Method 0 fits the outcomes
 * with a logistic curve in the log-odds forecast (Platt's scaling): a
 * slope above 1 says the forecaster was too timid, below 1 too bold.
 * Method 1 is isotonic regression, which only assumes that higher
 * forecasts should mean likelier events; it wants a long record. */
static TmValue
fn_calibrate (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  const TmArg *list[2] = { &args[1], &args[2] };
  double p0, method, *cols[2];
  int m;
  TmValue r;

  ARG_NUM (0, p0);
  OPT_NUM (3, method, 0);
  if (p0 < 0 || p0 > 1 || (method != 0 && method != 1))
    return tm_value_error (TM_ERR_NUM);
  if (!tm_arg_columns (ctx, list, 2, cols, &m, &err))
    return err;
  for (int i = 0; i < m; i++)
    if (cols[0][i] < 0 || cols[0][i] > 1 || cols[1][i] < 0 || cols[1][i] > 1)
      m = -1;
  if (m < 2)
    r = tm_value_error (m < 0 ? TM_ERR_NUM : TM_ERR_DIV0);
  else if (method == 0)
    {
      Table t = { m, 1, cols[1], g_new (double, m), NULL };
      const Model *model;
      double x0 = log (CLAMP (p0, 1e-6, 1 - 1e-6) / (1 - CLAMP (p0, 1e-6, 1 - 1e-6)));

      for (int i = 0; i < m; i++)
        {
          double p = CLAMP (cols[0][i], 1e-6, 1 - 1e-6);
          t.x[i] = log (p / (1 - p));
        }
      model = model_get (&t, FIT_LOGIT, 0);
      g_free (t.x);
      if (model->error != TM_ERR_NONE)
        r = tm_value_error (model->error);
      else
        {
          double z0[2];
          r = tm_value_number (1 / (1 + exp (-model_eta (model, &x0, z0, NULL))));
        }
    }
  else
    {
      /* Pool adjacent violators, over the record sorted by forecast. */
      double *pairs = g_new (double, 2 * m), *bp = g_new (double, m), *by = g_new (double, m),
             *bw = g_new (double, m);
      int nb = 0;

      for (int i = 0; i < m; i++)
        {
          pairs[2 * i] = cols[0][i];
          pairs[2 * i + 1] = cols[1][i];
        }
      qsort (pairs, m, 2 * sizeof (double), tm_compare_doubles);
      for (int i = 0; i < m; i++)
        {
          bp[nb] = pairs[2 * i];
          by[nb] = pairs[2 * i + 1];
          bw[nb] = 1;
          nb++;
          while (nb > 1 && by[nb - 2] >= by[nb - 1])
            {
              double w = bw[nb - 2] + bw[nb - 1];
              bp[nb - 2] = (bp[nb - 2] * bw[nb - 2] + bp[nb - 1] * bw[nb - 1]) / w;
              by[nb - 2] = (by[nb - 2] * bw[nb - 2] + by[nb - 1] * bw[nb - 1]) / w;
              bw[nb - 2] = w;
              nb--;
            }
        }
      /* Between the blocks' mean forecasts, joined by straight lines. */
      if (p0 <= bp[0])
        r = tm_value_number (by[0]);
      else if (p0 >= bp[nb - 1])
        r = tm_value_number (by[nb - 1]);
      else
        {
          int i = 0;
          while (bp[i + 1] < p0)
            i++;
          r = tm_value_number (by[i] + (by[i + 1] - by[i]) * (p0 - bp[i]) / (bp[i + 1] - bp[i]));
        }
      g_free (pairs);
      g_free (bp);
      g_free (by);
      g_free (bw);
    }
  if (m >= 0 || TRUE)
    {
      g_free (cols[0]);
      g_free (cols[1]);
    }
  return r;
}

const TmFunction tm_fn_learn[] = {
  FN ("KNN.FORECAST", 3, 5, fn_knn, 0, L, "KNN.FORECAST(x, known_y, known_X, [k], [kernel])", "The mean outcome of the k past cases most like this one (analogues)."),
  FN ("KNN.PERCENTILE", 4, 6, fn_knn_percentile, 0, L, "KNN.PERCENTILE(x, known_y, known_X, p, [k], [kernel])", "The outcome a fraction p of the k most similar past cases stayed under."),
  FN ("RAND.KNN", 3, 5, fn_rand_knn, RND, L, "RAND.KNN(x, known_y, known_X, [k], [kernel])", "How one of the k most similar past cases turned out."),
  FN ("FORECAST.MLR", 3, 4, fn_mlr, 0, L, "FORECAST.MLR(x, known_y, known_X, [ridge])", "Multiple regression: the least-squares plane through many features, read at x."),
  FN ("FORECAST.MLR.CONFINT", 3, 5, fn_mlr_confint, 0, L, "FORECAST.MLR.CONFINT(x, known_y, known_X, [confidence], [ridge])", "Half the width of the regression's prediction interval at x."),
  FN ("RAND.MLR", 3, 4, fn_rand_mlr, RND, L, "RAND.MLR(x, known_y, known_X, [ridge])", "The regression's forecast at x, plus a draw from its error and its own uncertainty."),
  FN ("MLR.COEF", 2, 4, fn_mlr_coef, 0, L, "MLR.COEF(known_y, known_X, [i], [ridge])", "The regression's intercept (i = 0) or the i'th feature's coefficient."),
  FN ("LOGIT.PROB", 3, 4, fn_logit_prob, 0, L, "LOGIT.PROB(x, known_events, known_X, [ridge])", "The chance of the event for a case like x, by logistic regression on past cases."),
  FN ("POISSON.REG", 3, 5, fn_poisson_reg, 0, L, "POISSON.REG(x, known_counts, known_X, [exposures], [exposure])", "The expected count for a case like x, by Poisson regression on past cases."),
  FN ("RAND.POISSON.REG", 3, 5, fn_rand_poisson_reg, RND, L, "RAND.POISSON.REG(x, known_counts, known_X, [exposures], [exposure])", "A count for a case like x, with the rate's uncertainty and any overdispersion."),
  FN ("QUANTILE.REG", 4, 4, fn_quantile_reg, 0, L, "QUANTILE.REG(x, known_y, known_X, tau)", "The tau'th percentile of the outcome for a case like x, by quantile regression."),
  FN ("CONFORMAL.CONFINT", 1, 2, fn_conformal, 0, L, "CONFORMAL.CONFINT(past_errors, [confidence])", "The half-width that covers the next error as often as asked, from past errors alone."),
  FN ("KDE.DIST", 2, 4, fn_kde_dist, 0, D, "KDE.DIST(x, data, [cumulative], [bandwidth])", "The data's smoothed density at x, or the chance of less than x."),
  FN ("KDE.INV", 2, 3, fn_kde_inv, 0, D, "KDE.INV(p, data, [bandwidth])", "The smoothed distribution's quantile."),
  FN ("RAND.KDE", 1, 3, fn_rand_kde, RND, R, "RAND.KDE(data, [bandwidth], [lower])", "The data, resampled and smoothed: a value like theirs, but not one of them."),
  FN ("RAND.EMPIRICAL", 1, 1, fn_rand_empirical, RND, R, "RAND.EMPIRICAL(data)", "A value from the data's own percentiles, joined up; never beyond them."),
  FN ("RAND.NEXT", 1, 1, fn_rand_next, RND, U, "RAND.NEXT(data)", "The next value from the same source: Student's t, the mean itself uncertain."),
  FN ("RAND.PROPORTION", 2, 5, fn_rand_proportion, RND, U, "RAND.PROPORTION(successes, trials, [future_trials], [prior_a], [prior_b])", "The success rate learnt from a record, or how many future trials succeed."),
  FN ("RAND.RATE", 2, 5, fn_rand_rate, RND, U, "RAND.RATE(events, exposure, [future_exposure], [prior_shape], [prior_rate])", "The event rate learnt from a record, or how many events a future exposure brings."),
  FN ("SHRINK", 4, 4, fn_shrink, 0, U, "SHRINK(estimate, se, estimates, ses)", "An estimate pulled towards its group's mean as far as its noise warrants."),
  FN ("SHRINK.RATE", 4, 4, fn_shrink_rate, 0, U, "SHRINK.RATE(successes, trials, all_successes, all_trials)", "A unit's success rate pulled towards the group's: partial pooling."),
  FN ("WEIBULL.DIST", 4, 4, fn_weibull_dist, 0, D, "WEIBULL.DIST(x, shape, scale, cumulative)", "The Weibull distribution of lifetimes."),
  FN ("RAND.WEIBULL", 2, 3, fn_rand_weibull, RND, V, "RAND.WEIBULL(shape, scale, [survived_to])", "A lifetime; given it has lasted survived_to, the age at which it ends."),
  FN ("WEIBULL.FIT", 2, 3, fn_weibull_fit, 0, V, "WEIBULL.FIT(times, \"shape\"|\"scale\", [failed])", "The Weibull that fits the lifetimes, counting those still going (failed 0)."),
  FN ("KAPLAN.MEIER", 2, 3, fn_kaplan_meier, 0, V, "KAPLAN.MEIER(t, times, [failed])", "The share still going at t, from lifetimes some of which have not ended."),
  FN ("RAND.SURVIVAL", 1, 2, fn_rand_survival, RND, V, "RAND.SURVIVAL(times, [failed])", "A lifetime drawn from the Kaplan-Meier curve, with an exponential tail."),
  FN ("RAND.COPULA", 2, 3, fn_rand_copula, RND, R, "RAND.COPULA(correlations, index, [group])", "A uniform correlated with its fellows: feed it to NORM.INV, BETA.INV..."),
  FN ("FORECAST.THETA", 2, 2, fn_theta, 0, F, "FORECAST.THETA(steps, values)", "The Theta method: smoothing plus half the trend; a hard benchmark to beat."),
  FN ("CROSTON", 1, 3, fn_croston, 0, F, "CROSTON(values, [alpha], [sba])", "The demand to expect a period, for demand that is mostly zero."),
  FN ("CHANGEPOINT", 1, 1, fn_changepoint, 0, F, "CHANGEPOINT(values)", "Where the series' current level began: the position after its latest shift."),
  FN ("FORECAST.ANALOG", 2, 4, fn_forecast_analog, 0, F, "FORECAST.ANALOG(steps, values, [window], [k])", "What followed the k stretches of history most like the latest: their mean."),
  FN ("RAND.ANALOG", 2, 4, fn_rand_analog, RND, F, "RAND.ANALOG(steps, values, [window], [k])", "What followed one of the k stretches of history most like the latest."),
  FN ("TAIL.PROB", 2, 3, fn_tail_prob, 0, D, "TAIL.PROB(x, data, [threshold])", "The chance of exceeding x, beyond the data if need be, by a Pareto tail."),
  FN ("RETURN.LEVEL", 2, 3, fn_return_level, 0, D, "RETURN.LEVEL(period, data, [threshold])", "The level exceeded once in so many observations: the 100-year flood."),
  FN ("ERLANG.C", 3, 4, fn_erlang_c, 0, P, "ERLANG.C(arrival_rate, service_rate, servers, [\"prob\"|\"wait\"])", "The chance a caller waits for one of the servers, or the mean wait."),
  FN ("RAND.LINDY", 1, 1, fn_rand_lindy, RND, R, "RAND.LINDY(age)", "How much longer something that has lasted age will last, by Gott's rule."),
  FN ("CALIBRATE", 3, 4, fn_calibrate, 0, "Scoring", "CALIBRATE(p, past_probabilities, past_outcomes, [method])", "A probability corrected by the forecaster's record: 0 logistic, 1 isotonic."),
};
const int tm_fn_learn_count = G_N_ELEMENTS (tm_fn_learn);
