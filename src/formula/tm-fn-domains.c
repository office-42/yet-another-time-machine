/* tm-fn-domains.c - weather, sport, markets, and the shapes of growth
 *
 * Copyright (C) 2026 The timemachine authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The methods particular kinds of future are forecast with.
 *
 * Weather: a Markov chain for which of a few states tomorrow is in (dry,
 * rain), estimated from the days before, and a mean-reverting AR(1)
 * process for how far temperature strays from the season's normal.
 *
 * Sport: goals as Poisson counts (Maher, 1982), each side's rate from its
 * attacking and the other's defending strength in past results, with Dixon
 * and Coles' (1997) correction for low scores; and Elo ratings.
 *
 * Markets: prices as geometric Brownian motion, its drift and volatility
 * estimated from a price history; Black and Scholes' option value, which
 * is the market's own forecast priced; and drawdowns.
 *
 * Growth: the benchmarks every forecast should beat (naive, seasonal
 * naive, drift), Gardner and McKenzie's damped trend, AR(1), and the
 * S-curves -- logistic, Gompertz, Bass diffusion -- that anything
 * spreading through a population follows.
 */

#include "tm-fn-private.h"

#include <stdlib.h>
#include <string.h>

#define P "Processes"
#define SP "Sports"
#define M "Markets"
#define F "Forecasting"
#define J "Judgment"

/* ---- Markov chains ---------------------------------------------------- */

/* A chain lives on the C stack while its cell is worked out, and a chain
 * of daily cells nests one frame per day; sixteen states keep each frame
 * to a couple of kilobytes. */
#define MAX_STATES 16

/* The states (a row or column) and the transition matrix (a square range,
 * from-state by row, to-state by column), each row scaled to sum to 1. */
typedef struct {
  int n;
  const TmValue *states[MAX_STATES];
  double p[MAX_STATES][MAX_STATES];
} Chain;

static gboolean
read_chain (TmEvalContext *ctx, TmArg *states, TmArg *matrix, Chain *c, TmValue *err)
{
  int ns;
  const TmValue **s;

  if (!states->is_range || !matrix->is_range)
    {
      *err = tm_value_error (TM_ERR_VALUE);
      return FALSE;
    }
  s = tm_arg_cells (ctx, states, &ns);
  if (ns > MAX_STATES || tm_range_rows (&matrix->range) != ns
      || tm_range_cols (&matrix->range) != ns)
    {
      g_free (s);
      *err = tm_value_error (TM_ERR_NA);
      return FALSE;
    }
  c->n = ns;
  for (int i = 0; i < ns; i++)
    {
      double total = 0;

      c->states[i] = s[i];
      for (int j = 0; j < ns; j++)
        {
          const TmValue *v = tm_ctx_cell (ctx, matrix->range.row0 + i, matrix->range.col0 + j);

          if (v->type == TM_VALUE_ERROR)
            {
              g_free (s);
              *err = tm_value_copy (v);
              return FALSE;
            }
          c->p[i][j] = v->type == TM_VALUE_NUMBER ? v->as.number : 0;
          if (c->p[i][j] < 0)
            {
              g_free (s);
              *err = tm_value_error (TM_ERR_NUM);
              return FALSE;
            }
          total += c->p[i][j];
        }
      if (total <= 0)
        {
          g_free (s);
          *err = tm_value_error (TM_ERR_NUM);
          return FALSE;
        }
      for (int j = 0; j < ns; j++)
        c->p[i][j] /= total;
    }
  g_free (s);
  return TRUE;
}

static int
state_index (const Chain *c, const TmValue *v)
{
  for (int i = 0; i < c->n; i++)
    if (tm_value_compare (c->states[i], v) == 0)
      return i;
  return -1;
}

static int
arg_state (TmEvalContext *ctx, const Chain *c, TmArg *arg, TmValue *err)
{
  TmValue v = tm_arg_scalar (ctx, arg);
  int i;

  if (v.type == TM_VALUE_ERROR)
    {
      *err = v;
      return -1;
    }
  i = state_index (c, &v);
  tm_value_clear (&v);
  if (i < 0)
    *err = tm_value_error (TM_ERR_NA);
  return i;
}

/* Tomorrow's state, drawn from today's row of the matrix. */
static TmValue
fn_rand_markov (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  Chain c;
  int from;
  double u, acc = 0;

  if (!read_chain (ctx, &args[1], &args[2], &c, &err))
    return err;
  if ((from = arg_state (ctx, &c, &args[0], &err)) < 0)
    return err;
  u = draw_uniform (ctx);
  for (int j = 0; j < c.n; j++)
    {
      acc += c.p[from][j];
      if (u < acc || j == c.n - 1)
        return tm_value_copy (c.states[j]);
    }
  return tm_value_error (TM_ERR_NUM);
}

static void
matmul (int n, double a[MAX_STATES][MAX_STATES], double b[MAX_STATES][MAX_STATES],
        double out[MAX_STATES][MAX_STATES])
{
  double t[MAX_STATES][MAX_STATES];

  for (int i = 0; i < n; i++)
    for (int j = 0; j < n; j++)
      {
        double s = 0;
        for (int k = 0; k < n; k++)
          s += a[i][k] * b[k][j];
        t[i][j] = s;
      }
  memcpy (out, t, sizeof t);
}

/* The chance of being in one state so many steps after another: the
 * matrix raised to that power, by repeated squaring. */
static TmValue
fn_markov_prob (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  Chain c;
  double result[MAX_STATES][MAX_STATES], power[MAX_STATES][MAX_STATES];
  int from, to;
  double steps;
  long k;

  if (!read_chain (ctx, &args[2], &args[3], &c, &err))
    return err;
  if ((from = arg_state (ctx, &c, &args[0], &err)) < 0 || (to = arg_state (ctx, &c, &args[1], &err)) < 0)
    return err;
  OPT_NUM (4, steps, 1);
  if (steps < 0 || steps > 1e9)
    return tm_value_error (TM_ERR_NUM);
  k = (long) steps;
  for (int i = 0; i < c.n; i++)
    for (int j = 0; j < c.n; j++)
      {
        result[i][j] = i == j;
        power[i][j] = c.p[i][j];
      }
  while (k > 0)
    {
      if (k & 1)
        matmul (c.n, result, power, result);
      matmul (c.n, power, power, power);
      k >>= 1;
    }
  return tm_value_number (result[from][to]);
}

/* The long-run share of time in a state: the stationary distribution,
 * pi P = pi with the shares summing to one, by Gaussian elimination. */
static TmValue
fn_markov_steady (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  Chain c;
  double a[MAX_STATES][MAX_STATES + 1];
  int s, k;

  if (!read_chain (ctx, &args[1], &args[2], &c, &err))
    return err;
  if ((s = arg_state (ctx, &c, &args[0], &err)) < 0)
    return err;
  k = c.n;
  /* (P^T - I) pi = 0, its last equation replaced by sum pi = 1. */
  for (int i = 0; i < k; i++)
    {
      for (int j = 0; j < k; j++)
        a[i][j] = c.p[j][i] - (i == j);
      a[i][k] = 0;
    }
  for (int j = 0; j < k; j++)
    a[k - 1][j] = 1;
  a[k - 1][k] = 1;
  for (int col = 0; col < k; col++)
    {
      int pivot = col;

      for (int r = col + 1; r < k; r++)
        if (fabs (a[r][col]) > fabs (a[pivot][col]))
          pivot = r;
      if (fabs (a[pivot][col]) < 1e-14)
        return tm_value_error (TM_ERR_NUM);   /* no unique long run */
      if (pivot != col)
        for (int j = 0; j <= k; j++)
          {
            double t = a[col][j];
            a[col][j] = a[pivot][j];
            a[pivot][j] = t;
          }
      for (int r = 0; r < k; r++)
        if (r != col)
          {
            double f = a[r][col] / a[col][col];
            for (int j = col; j <= k; j++)
              a[r][j] -= f * a[col][j];
          }
    }
  return tm_value_number (a[s][k] / a[s][s]);
}

/* A transition probability estimated from a sequence of observed states:
 * how often from was followed by to, out of how often from was followed
 * by anything; prior pseudo-counts per state keep an unseen transition
 * from being impossible. */
static TmValue
fn_markov_estimate (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err, from, to;
  int count;
  const TmValue **seq;
  double prior, hits = 0, total = 0;
  GPtrArray *distinct = g_ptr_array_new ();

  OPT_NUM (3, prior, 0);
  if (prior < 0)
    return tm_value_error (TM_ERR_NUM);
  from = tm_arg_scalar (ctx, &args[0]);
  to = tm_arg_scalar (ctx, &args[1]);
  seq = tm_arg_cells (ctx, &args[2], &count);
  for (int i = 0; i < count; i++)
    {
      gboolean seen = FALSE;

      if (seq[i]->type == TM_VALUE_EMPTY)
        continue;
      for (guint k = 0; k < distinct->len; k++)
        if (tm_value_compare (g_ptr_array_index (distinct, k), seq[i]) == 0)
          seen = TRUE;
      if (!seen)
        g_ptr_array_add (distinct, (gpointer) seq[i]);
      if (i + 1 < count && seq[i + 1]->type != TM_VALUE_EMPTY
          && tm_value_compare (seq[i], &from) == 0)
        {
          total++;
          if (tm_value_compare (seq[i + 1], &to) == 0)
            hits++;
        }
    }
  err = total + prior * distinct->len > 0
        ? tm_value_number ((hits + prior) / (total + prior * distinct->len))
        : tm_value_error (TM_ERR_DIV0);
  g_ptr_array_free (distinct, TRUE);
  g_free (seq);
  tm_value_clear (&from);
  tm_value_clear (&to);
  return err;
}

/* ---- Mean reversion --------------------------------------------------- */

/* One step of an AR(1) process: a pull of phi back towards the mean, and
 * a fresh shock.  Temperatures about their seasonal normal, interest rates
 * and utilisation behave this way. */
static TmValue
fn_rand_ar1 (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double prev, mean, phi, sigma;

  ARG_NUM (0, prev);
  ARG_NUM (1, mean);
  ARG_NUM (2, phi);
  ARG_NUM (3, sigma);
  if (sigma < 0)
    return tm_value_error (TM_ERR_NUM);
  return tm_value_number (mean + phi * (prev - mean) + sigma * draw_normal (ctx));
}

typedef struct {
  double mean, phi, sigma, last;
} Ar1;

/* Least squares of each value on the one before. */
static gboolean
fit_ar1 (TmEvalContext *ctx, TmArg *arg, Ar1 *fit, TmValue *err)
{
  GArray *y = collect (ctx, arg, 1, err);
  guint n;
  double mx = 0, my = 0, sxy = 0, sxx = 0, sse = 0, c;

  if (y == NULL)
    return FALSE;
  n = y->len;
  if (n < 4)
    {
      g_array_free (y, TRUE);
      *err = tm_value_error (TM_ERR_DIV0);
      return FALSE;
    }
#define Y(i) g_array_index (y, double, i)
  for (guint i = 1; i < n; i++)
    {
      mx += Y (i - 1);
      my += Y (i);
    }
  mx /= n - 1;
  my /= n - 1;
  for (guint i = 1; i < n; i++)
    {
      sxy += (Y (i - 1) - mx) * (Y (i) - my);
      sxx += (Y (i - 1) - mx) * (Y (i - 1) - mx);
    }
  if (sxx == 0)
    {
      g_array_free (y, TRUE);
      *err = tm_value_error (TM_ERR_DIV0);
      return FALSE;
    }
  fit->phi = sxy / sxx;
  c = my - fit->phi * mx;
  for (guint i = 1; i < n; i++)
    {
      double e = Y (i) - c - fit->phi * Y (i - 1);
      sse += e * e;
    }
  fit->sigma = sqrt (sse / (n - 3));
  fit->last = Y (n - 1);
#undef Y
  g_array_free (y, TRUE);
  if (fabs (fit->phi) >= 1)
    {
      *err = tm_value_error (TM_ERR_NUM);   /* a random walk: no mean to revert to */
      return FALSE;
    }
  fit->mean = c / (1 - fit->phi);
  return TRUE;
}

static TmValue
fn_forecast_ar1 (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  Ar1 fit;
  double h;

  ARG_NUM (0, h);
  if (h < 0)
    return tm_value_error (TM_ERR_NUM);
  if (!fit_ar1 (ctx, &args[1], &fit, &err))
    return err;
  return tm_value_number (fit.mean + pow (fit.phi, h) * (fit.last - fit.mean));
}

static TmValue
fn_forecast_ar1_confint (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  Ar1 fit;
  double h, conf, var;

  ARG_NUM (0, h);
  OPT_NUM (2, conf, 0.95);
  if (h < 0 || conf <= 0 || conf >= 1)
    return tm_value_error (TM_ERR_NUM);
  if (!fit_ar1 (ctx, &args[1], &fit, &err))
    return err;
  var = fit.sigma * fit.sigma * (1 - pow (fit.phi, 2 * h)) / (1 - fit.phi * fit.phi);
  return tm_value_number (sqrt (var) * tm_norm_inv (0.5 + conf / 2));
}

/* The Bank of England's split normal: a mode, and a different spread each
 * side of it, for a risk that leans one way. */
static TmValue
fn_rand_splitnormal (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double mode, s1, s2, u, w;

  ARG_NUM (0, mode);
  ARG_NUM (1, s1);
  ARG_NUM (2, s2);
  if (s1 <= 0 || s2 <= 0)
    return tm_value_error (TM_ERR_NUM);
  u = draw_uniform (ctx);
  w = s1 / (s1 + s2);
  /* The inverse distribution function, piece by piece. */
  if (u < w)
    return tm_value_number (mode + s1 * tm_norm_inv (u / (2 * w)));
  return tm_value_number (mode + s2 * tm_norm_inv (0.5 + (u - w) / (2 * (1 - w))));
}

/* ---- Benchmarks and trends -------------------------------------------- */

static GArray *
series (TmEvalContext *ctx, TmArg *arg, guint min, TmValue *err)
{
  GArray *y = collect (ctx, arg, 1, err);

  if (y != NULL && y->len < min)
    {
      g_array_free (y, TRUE);
      *err = tm_value_error (TM_ERR_DIV0);
      return NULL;
    }
  return y;
}

/* The last value carried on, plus the average step of the history: the
 * drift benchmark, which a fancier method should beat to earn its keep. */
static TmValue
fn_forecast_drift (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double h;
  GArray *y;
  double first, last;

  ARG_NUM (0, h);
  if ((y = series (ctx, &args[1], 2, &err)) == NULL)
    return err;
  first = g_array_index (y, double, 0);
  last = g_array_index (y, double, y->len - 1);
  err = tm_value_number (last + h * (last - first) / (y->len - 1));
  g_array_free (y, TRUE);
  return err;
}

/* The value a season before: the seasonal naive benchmark. */
static TmValue
fn_forecast_snaive (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double h, m;
  GArray *y;
  int T, k, i;

  ARG_NUM (0, h);
  ARG_NUM (2, m);
  h = floor (h);
  m = floor (m);
  if (h < 1 || m < 1)
    return tm_value_error (TM_ERR_NUM);
  if ((y = series (ctx, &args[1], (guint) m, &err)) == NULL)
    return err;
  T = (int) y->len;
  k = (int) ((h - 1) / m);
  i = T + (int) h - (int) m * (k + 1) - 1;
  err = tm_value_number (g_array_index (y, double, i));
  g_array_free (y, TRUE);
  return err;
}

/* Holt's trend, damped: the trend fades by phi a step, so that the
 * forecast levels off rather than running on for ever.  The most robust
 * automatic method in the M-competitions. */
static TmValue
fn_forecast_damped (TmEvalContext *ctx, TmArg *args, int n)
{
  static const double grid[] = { 0.05, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 0.98 };
  static const double phis[] = { 0.8, 0.85, 0.9, 0.95, 0.98 };
  TmValue err;
  double h, fixed_phi, best = INFINITY, level = 0, trend = 0, phi_best = 0.9, damp = 0, f;
  GArray *y;
  guint T, n_phi;

  ARG_NUM (0, h);
  OPT_NUM (2, fixed_phi, 0);
  n_phi = fixed_phi > 0 ? 1 : G_N_ELEMENTS (phis);
  if (h < 0 || fixed_phi < 0 || fixed_phi > 1)
    return tm_value_error (TM_ERR_NUM);
  if ((y = series (ctx, &args[1], 3, &err)) == NULL)
    return err;
  T = y->len;

  for (guint pi = 0; pi < n_phi; pi++)
    for (guint ai = 0; ai < G_N_ELEMENTS (grid); ai++)
      for (guint bi = 0; bi < G_N_ELEMENTS (grid); bi++)
        {
          double phi = fixed_phi > 0 ? fixed_phi : phis[pi];
          double a = grid[ai], b = grid[bi] * 0.5;
          double l = g_array_index (y, double, 0);
          double t = g_array_index (y, double, 1) - l, sse = 0;

          for (guint i = 1; i < T; i++)
            {
              double yi = g_array_index (y, double, i), e = yi - (l + phi * t), nl;

              sse += e * e;
              nl = a * yi + (1 - a) * (l + phi * t);
              t = b * (nl - l) + (1 - b) * phi * t;
              l = nl;
            }
          if (sse < best)
            {
              best = sse;
              level = l;
              trend = t;
              phi_best = phi;
            }
        }
  g_array_free (y, TRUE);
  /* l + (phi + phi^2 + ... + phi^h) b */
  f = phi_best;
  for (int i = 0; i < (int) floor (h); i++)
    {
      damp += f;
      f *= phi_best;
    }
  return tm_value_number (level + damp * trend);
}

static TmValue
fn_logistic (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double t, k, r, t0;

  ARG_NUM (0, t);
  ARG_NUM (1, k);
  ARG_NUM (2, r);
  ARG_NUM (3, t0);
  return tm_value_number (k / (1 + exp (-r * (t - t0))));
}

static TmValue
fn_gompertz (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double t, k, b, c;

  ARG_NUM (0, t);
  ARG_NUM (1, k);
  ARG_NUM (2, b);
  ARG_NUM (3, c);
  return tm_value_number (k * exp (-b * exp (-c * t)));
}

/* Bass diffusion: adopters by time t out of a market m, from p, the pull
 * of outside influence (advertising; about 0.03), and q, of imitation
 * (word of mouth; about 0.4). */
static TmValue
fn_bass (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double t, p, q, m, e, cumulative = 1;

  ARG_NUM (0, t);
  ARG_NUM (1, p);
  ARG_NUM (2, q);
  OPT_NUM (3, m, 1);
  if (HAS_ARG (4))
    {
      gboolean b;
      ARG_BOOL (4, b);
      cumulative = b;
    }
  if (p <= 0 || q < 0 || t < 0)
    return tm_value_error (TM_ERR_NUM);
  e = exp (-(p + q) * t);
  if (cumulative)
    return tm_value_number (m * (1 - e) / (1 + (q / p) * e));
  return tm_value_number (m * ((p + q) * (p + q) / p) * e / pow (1 + (q / p) * e, 2));
}

/* ---- Sport ------------------------------------------------------------ */

static double
poisson_pmf (int k, double lambda)
{
  if (lambda <= 0)
    return k == 0 ? 1 : 0;
  return exp (k * log (lambda) - lambda - lgamma (k + 1.0));
}

/* Dixon and Coles' correction: independent Poisson counts get 0-0, 1-1,
 * 1-0 and 0-1 slightly wrong; rho (typically a little below zero) puts it
 * right without changing the totals. */
static double
dixon_coles (int i, int j, double lambda, double mu, double rho)
{
  if (i == 0 && j == 0) return 1 - lambda * mu * rho;
  if (i == 0 && j == 1) return 1 + lambda * rho;
  if (i == 1 && j == 0) return 1 + mu * rho;
  if (i == 1 && j == 1) return 1 - rho;
  return 1;
}

static TmValue
fn_poisson_score (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double lambda, mu, hg, ag, rho;

  ARG_NUM (0, lambda);
  ARG_NUM (1, mu);
  ARG_NUM (2, hg);
  ARG_NUM (3, ag);
  OPT_NUM (4, rho, 0);
  if (lambda < 0 || mu < 0 || hg < 0 || ag < 0)
    return tm_value_error (TM_ERR_NUM);
  return tm_value_number (poisson_pmf ((int) hg, lambda) * poisson_pmf ((int) ag, mu)
                          * dixon_coles ((int) hg, (int) ag, lambda, mu, rho));
}

/* Which result: "home", "draw" or "away" (or H, D, A, or 1, 0, 2). */
static int
outcome_of (const TmValue *v)
{
  if (v->type == TM_VALUE_NUMBER)
    return v->as.number == 1 ? 1 : v->as.number == 0 ? 0 : v->as.number == 2 ? 2 : -1;
  if (v->type == TM_VALUE_TEXT)
    switch (g_ascii_toupper (v->as.text[0]))
      {
      case 'H': case '1': return 1;
      case 'D': case 'X': return 0;
      case 'A': case '2': return 2;
      default: return -1;
      }
  return -1;
}

static TmValue
fn_poisson_match (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err, which;
  double lambda, mu, rho, p = 0;
  int outcome, top;

  ARG_NUM (0, lambda);
  ARG_NUM (1, mu);
  OPT_NUM (3, rho, 0);
  if (lambda < 0 || mu < 0)
    return tm_value_error (TM_ERR_NUM);
  which = tm_arg_scalar (ctx, &args[2]);
  outcome = outcome_of (&which);
  tm_value_clear (&which);
  if (outcome < 0)
    return tm_value_error (TM_ERR_VALUE);
  /* Far enough into the tails that what is left out is below 1e-12. */
  top = (int) ceil (MAX (lambda, mu) + 12 * sqrt (MAX (lambda, mu)) + 12);
  for (int i = 0; i <= top; i++)
    for (int j = 0; j <= top; j++)
      {
        int r = i > j ? 1 : i == j ? 0 : 2;

        if (r == outcome)
          p += poisson_pmf (i, lambda) * poisson_pmf (j, mu) * dixon_coles (i, j, lambda, mu, rho);
      }
  return tm_value_number (p);
}

/* Expected goals for a fixture from past results: the league's average
 * home (or away) score, times the scoring side's attacking strength, times
 * the other side's defensive weakness -- each a ratio to the league
 * average, and each shrunk towards 1 by a game's worth of average
 * results, so that a side seen twice is not taken at its word. */
static TmValue
fn_match_xg (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err, home, away;
  int nh, na, ng, nt;
  const TmValue **ht, **at, **hg, **ag;
  double side = 1;
  double sum_h = 0, sum_a = 0, games = 0;
  double h_for = 0, h_against = 0, h_games = 0;   /* the home side, at home */
  double a_for = 0, a_against = 0, a_games = 0;   /* the away side, away */
  double avg_h, avg_a, attack, defence, r;

  OPT_NUM (6, side, 1);
  home = tm_arg_scalar (ctx, &args[0]);
  away = tm_arg_scalar (ctx, &args[1]);
  ht = tm_arg_cells (ctx, &args[2], &nh);
  at = tm_arg_cells (ctx, &args[3], &na);
  hg = tm_arg_cells (ctx, &args[4], &ng);
  ag = tm_arg_cells (ctx, &args[5], &nt);
  if (nh != na || nh != ng || nh != nt)
    r = -1;
  else
    {
      for (int i = 0; i < nh; i++)
        {
          double x, y;

          if (hg[i]->type != TM_VALUE_NUMBER || ag[i]->type != TM_VALUE_NUMBER)
            continue;           /* not played yet */
          x = hg[i]->as.number;
          y = ag[i]->as.number;
          sum_h += x;
          sum_a += y;
          games++;
          if (tm_value_compare (ht[i], &home) == 0)
            {
              h_for += x;
              h_against += y;
              h_games++;
            }
          if (tm_value_compare (at[i], &away) == 0)
            {
              a_for += y;
              a_against += x;
              a_games++;
            }
        }
      r = games;
    }
  g_free (ht);
  g_free (at);
  g_free (hg);
  g_free (ag);
  tm_value_clear (&home);
  tm_value_clear (&away);
  if (r < 0)
    return tm_value_error (TM_ERR_NA);
  if (games == 0)
    return tm_value_error (TM_ERR_DIV0);

  avg_h = sum_h / games;
  avg_a = sum_a / games;
  if (side == 1)
    {
      attack = (h_for + avg_h) / (h_games + 1) / avg_h;
      defence = (a_against + avg_h) / (a_games + 1) / avg_h;
      return tm_value_number (avg_h * attack * defence);
    }
  attack = (a_for + avg_a) / (a_games + 1) / avg_a;
  defence = (h_against + avg_a) / (h_games + 1) / avg_a;
  return tm_value_number (avg_a * attack * defence);
}

/* Elo: the expected score of A against B (a win counting 1, a draw one
 * half), from their ratings and any advantage A has at home. */
static TmValue
fn_elo_expect (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double ra, rb, hfa;

  ARG_NUM (0, ra);
  ARG_NUM (1, rb);
  OPT_NUM (2, hfa, 0);
  return tm_value_number (1 / (1 + pow (10, (rb - ra - hfa) / 400)));
}

static TmValue
fn_elo_update (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double rating, expected, score, k;

  ARG_NUM (0, rating);
  ARG_NUM (1, expected);
  ARG_NUM (2, score);
  OPT_NUM (3, k, 20);
  return tm_value_number (rating + k * (score - expected));
}

/* ---- Markets ---------------------------------------------------------- */

/* The log returns of a price series. */
static GArray *
log_returns (TmEvalContext *ctx, TmArg *arg, TmValue *err)
{
  GArray *p = collect (ctx, arg, 1, err);
  GArray *r;

  if (p == NULL)
    return NULL;
  if (p->len < 3)
    {
      g_array_free (p, TRUE);
      *err = tm_value_error (TM_ERR_DIV0);
      return NULL;
    }
  r = g_array_new (FALSE, FALSE, sizeof (double));
  for (guint i = 1; i < p->len; i++)
    {
      double a = g_array_index (p, double, i - 1), b = g_array_index (p, double, i), x;

      if (a <= 0 || b <= 0)
        {
          g_array_free (p, TRUE);
          g_array_free (r, TRUE);
          *err = tm_value_error (TM_ERR_NUM);
          return NULL;
        }
      x = log (b / a);
      g_array_append_val (r, x);
    }
  g_array_free (p, TRUE);
  return r;
}

static void
mean_sd (GArray *x, double *mean, double *sd)
{
  double m = 0, s = 0;

  for (guint i = 0; i < x->len; i++)
    m += g_array_index (x, double, i);
  m /= x->len;
  for (guint i = 0; i < x->len; i++)
    s += pow (g_array_index (x, double, i) - m, 2);
  *mean = m;
  *sd = sqrt (s / (x->len - 1));
}

/* The standard deviation of log returns, scaled to a year. */
static TmValue
fn_volatility (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double periods, m, s;
  GArray *r;

  OPT_NUM (1, periods, 252);
  if (periods <= 0)
    return tm_value_error (TM_ERR_NUM);
  if ((r = log_returns (ctx, &args[0], &err)) == NULL)
    return err;
  mean_sd (r, &m, &s);
  g_array_free (r, TRUE);
  return tm_value_number (s * sqrt (periods));
}

/* The drift mu of geometric Brownian motion, a year: the mean log return
 * scaled to a year, plus half the variance -- the growth rate of the
 * expected price, not of the median one. */
static TmValue
fn_drift (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double periods, m, s;
  GArray *r;

  OPT_NUM (1, periods, 252);
  if (periods <= 0)
    return tm_value_error (TM_ERR_NUM);
  if ((r = log_returns (ctx, &args[0], &err)) == NULL)
    return err;
  mean_sd (r, &m, &s);
  g_array_free (r, TRUE);
  return tm_value_number (m * periods + s * s * periods / 2);
}

/* The chance a price following GBM ends above target after time t. */
static TmValue
fn_gbm_prob (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double s0, k, mu, sigma, t;

  ARG_NUM (0, s0);
  ARG_NUM (1, k);
  ARG_NUM (2, mu);
  ARG_NUM (3, sigma);
  ARG_NUM (4, t);
  if (s0 <= 0 || k <= 0 || sigma <= 0 || t <= 0)
    return tm_value_error (TM_ERR_NUM);
  return tm_value_number (tm_norm_cdf ((log (s0 / k) + (mu - sigma * sigma / 2) * t) / (sigma * sqrt (t))));
}

/* The price a fraction p of GBM's futures stay under after time t. */
static TmValue
fn_gbm_percentile (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double s0, p, mu, sigma, t;

  ARG_NUM (0, s0);
  ARG_NUM (1, p);
  ARG_NUM (2, mu);
  ARG_NUM (3, sigma);
  ARG_NUM (4, t);
  if (s0 <= 0 || p <= 0 || p >= 1 || sigma < 0 || t < 0)
    return tm_value_error (TM_ERR_NUM);
  return tm_value_number (s0 * exp ((mu - sigma * sigma / 2) * t + sigma * sqrt (t) * tm_norm_inv (p)));
}

/* Black and Scholes' value of a European option: what the market charges
 * for the chance of a move, and so its own forecast of volatility. */
static TmValue
fn_blackscholes (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err, type;
  double s, k, r, sigma, t, d1, d2;
  gboolean put = FALSE;

  ARG_NUM (0, s);
  ARG_NUM (1, k);
  ARG_NUM (2, r);
  ARG_NUM (3, sigma);
  ARG_NUM (4, t);
  if (HAS_ARG (5))
    {
      type = tm_arg_scalar (ctx, &args[5]);
      put = type.type == TM_VALUE_TEXT && g_ascii_toupper (type.as.text[0]) == 'P';
      tm_value_clear (&type);
    }
  if (s <= 0 || k <= 0 || sigma <= 0 || t <= 0)
    return tm_value_error (TM_ERR_NUM);
  d1 = (log (s / k) + (r + sigma * sigma / 2) * t) / (sigma * sqrt (t));
  d2 = d1 - sigma * sqrt (t);
  if (put)
    return tm_value_number (k * exp (-r * t) * tm_norm_cdf (-d2) - s * tm_norm_cdf (-d1));
  return tm_value_number (s * tm_norm_cdf (d1) - k * exp (-r * t) * tm_norm_cdf (d2));
}

/* The largest fall from a peak to a later trough, as a fraction of the
 * peak: how bad the way there was, whatever the end. */
static TmValue
fn_drawdown (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  GArray *p = collect (ctx, args, 1, &err);
  double peak = -INFINITY, worst = 0;

  if (p == NULL)
    return err;
  for (guint i = 0; i < p->len; i++)
    {
      double x = g_array_index (p, double, i);

      peak = MAX (peak, x);
      if (peak > 0)
        worst = MAX (worst, (peak - x) / peak);
    }
  g_array_free (p, TRUE);
  return tm_value_number (worst);
}

/* ---- Judgment --------------------------------------------------------- */

/* How much better than a reference the forecasts scored: 1 - BS / BS_ref,
 * the reference by default climatology -- always forecasting the base
 * rate of what happened.  Weather services score themselves this way. */
static TmValue
fn_brier_skill (TmEvalContext *ctx, TmArg *args, int n)
{
  int np, no, nr = 0;
  const TmValue **p = tm_arg_cells (ctx, &args[0], &np);
  const TmValue **o = tm_arg_cells (ctx, &args[1], &no);
  const TmValue **ref = HAS_ARG (2) ? tm_arg_cells (ctx, &args[2], &nr) : NULL;
  double bs = 0, bs_ref = 0, base = 0;
  int k = 0;
  TmValue r;

  if (np != no || (ref != NULL && nr != np))
    r = tm_value_error (TM_ERR_NA);
  else
    {
      for (int i = 0; i < np; i++)
        if (p[i]->type == TM_VALUE_NUMBER && o[i]->type != TM_VALUE_EMPTY)
          {
            double ov = o[i]->type == TM_VALUE_BOOL ? o[i]->as.boolean
                        : o[i]->type == TM_VALUE_NUMBER ? o[i]->as.number : 0;
            base += ov;
            k++;
          }
      if (k == 0)
        r = tm_value_error (TM_ERR_DIV0);
      else
        {
          base /= k;
          for (int i = 0; i < np; i++)
            if (p[i]->type == TM_VALUE_NUMBER && o[i]->type != TM_VALUE_EMPTY)
              {
                double ov = o[i]->type == TM_VALUE_BOOL ? o[i]->as.boolean
                            : o[i]->type == TM_VALUE_NUMBER ? o[i]->as.number : 0;
                double rv = ref != NULL && ref[i]->type == TM_VALUE_NUMBER ? ref[i]->as.number : base;

                bs += pow (p[i]->as.number - ov, 2);
                bs_ref += pow (rv - ov, 2);
              }
          r = bs_ref == 0 ? tm_value_error (TM_ERR_DIV0) : tm_value_number (1 - bs / bs_ref);
        }
    }
  g_free (p);
  g_free (o);
  g_free (ref);
  return r;
}

#define RND TM_FN_RANDOM

const TmFunction tm_fn_domains[] = {
  FN ("RAND.MARKOV", 3, 3, fn_rand_markov, RND, P, "RAND.MARKOV(state, states, matrix)", "The next state of a Markov chain, drawn from the current state's row."),
  FN ("MARKOV.PROB", 4, 5, fn_markov_prob, 0, P, "MARKOV.PROB(from, to, states, matrix, [steps])", "The chance of being in one state so many steps after another."),
  FN ("MARKOV.STEADY", 3, 3, fn_markov_steady, 0, P, "MARKOV.STEADY(state, states, matrix)", "The long-run share of time a chain spends in a state."),
  FN ("MARKOV.ESTIMATE", 3, 4, fn_markov_estimate, 0, P, "MARKOV.ESTIMATE(from, to, sequence, [prior])", "A transition probability counted from an observed sequence of states."),
  FN ("RAND.AR1", 4, 4, fn_rand_ar1, RND, P, "RAND.AR1(previous, mean, phi, sigma)", "One step of a mean-reverting process: pulled back by phi, plus a shock."),
  FN ("RAND.SPLITNORMAL", 3, 3, fn_rand_splitnormal, RND, P, "RAND.SPLITNORMAL(mode, sd_below, sd_above)", "A bell with a different spread each side: a risk that leans one way."),

  FN ("FORECAST.AR1", 2, 2, fn_forecast_ar1, 0, F, "FORECAST.AR1(steps, values)", "A mean-reverting forecast: the gap to the mean shrinks by phi a step."),
  FN ("FORECAST.AR1.CONFINT", 2, 3, fn_forecast_ar1_confint, 0, F, "FORECAST.AR1.CONFINT(steps, values, [confidence])", "Half the width of the AR(1) forecast's interval."),
  FN ("FORECAST.DRIFT", 2, 2, fn_forecast_drift, 0, F, "FORECAST.DRIFT(steps, values)", "The last value plus the history's average step: a benchmark."),
  FN ("FORECAST.SNAIVE", 3, 3, fn_forecast_snaive, 0, F, "FORECAST.SNAIVE(steps, values, season)", "The value a season ago: the seasonal benchmark."),
  FN ("FORECAST.DAMPED", 2, 3, fn_forecast_damped, 0, F, "FORECAST.DAMPED(steps, values, [phi])", "Holt's trend, fading by phi a step so that the forecast levels off."),
  FN ("LOGISTIC", 4, 4, fn_logistic, 0, F, "LOGISTIC(t, capacity, rate, midpoint)", "The S-curve of growth that meets a ceiling."),
  FN ("GOMPERTZ", 4, 4, fn_gompertz, 0, F, "GOMPERTZ(t, capacity, b, c)", "A lopsided S-curve, slower to its ceiling than to its start."),
  FN ("BASS", 3, 5, fn_bass, 0, F, "BASS(t, p, q, [market], [cumulative])", "Adopters of something new by time t: innovation p, imitation q."),

  FN ("POISSON.MATCH", 3, 4, fn_poisson_match, 0, SP, "POISSON.MATCH(home_goals, away_goals, \"home\"|\"draw\"|\"away\", [rho])", "The chance of a result, goals being Poisson with these means."),
  FN ("POISSON.SCORE", 4, 5, fn_poisson_score, 0, SP, "POISSON.SCORE(home_goals, away_goals, h, a, [rho])", "The chance of the exact score h-a."),
  FN ("MATCH.XG", 6, 7, fn_match_xg, 0, SP, "MATCH.XG(home, away, home_teams, away_teams, home_goals, away_goals, [side])", "Expected goals for a fixture from attack and defence in past results; side 2 for the away team."),
  FN ("ELO.EXPECT", 2, 3, fn_elo_expect, 0, SP, "ELO.EXPECT(rating, opponent, [home_advantage])", "The expected score from Elo ratings: a win 1, a draw a half."),
  FN ("ELO.UPDATE", 3, 4, fn_elo_update, 0, SP, "ELO.UPDATE(rating, expected, score, [k])", "The rating after a game."),

  FN ("VOLATILITY", 1, 2, fn_volatility, 0, M, "VOLATILITY(prices, [periods_a_year])", "The standard deviation of log returns, a year."),
  FN ("DRIFT", 1, 2, fn_drift, 0, M, "DRIFT(prices, [periods_a_year])", "Geometric Brownian motion's drift, a year, estimated from prices."),
  FN ("GBM.PROB", 5, 5, fn_gbm_prob, 0, M, "GBM.PROB(price, target, drift, volatility, years)", "The chance a random-walking price ends above the target."),
  FN ("GBM.PERCENTILE", 5, 5, fn_gbm_percentile, 0, M, "GBM.PERCENTILE(price, p, drift, volatility, years)", "The price a fraction p of futures stay under."),
  FN ("BLACKSCHOLES", 5, 6, fn_blackscholes, 0, M, "BLACKSCHOLES(price, strike, rate, volatility, years, [\"call\"|\"put\"])", "The value of a European option."),
  FN ("DRAWDOWN", 1, 1, fn_drawdown, 0, M, "DRAWDOWN(prices)", "The largest fall from a peak, as a fraction of it."),

  FN ("BRIER.SKILL", 2, 3, fn_brier_skill, 0, J, "BRIER.SKILL(probabilities, outcomes, [reference])", "How much better than the base rate (or a reference) the forecasts scored."),
};
const int tm_fn_domains_count = G_N_ELEMENTS (tm_fn_domains);
