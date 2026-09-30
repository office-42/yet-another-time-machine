/* tm-fn-random.c - uncertain quantities
 *
 * Copyright (C) 2026 The timemachine authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A cell holding one of these holds not a number but a distribution: each
 * recalculation draws from it afresh, and a simulation draws from it ten
 * thousand times to see what the cells that depend on it might come to.
 * This is Monte Carlo simulation the way @RISK and Crystal Ball put it in
 * a spreadsheet, and the way Hubbard's "How to Measure Anything" asks for
 * estimates: as a range someone is 90% sure of, not as a single number.
 */

#include "tm-fn-private.h"

#define R "Random"
#define P "Processes"

static TmValue
fn_rand (TmEvalContext *ctx, TmArg *args, int n)
{
  return tm_value_number (draw_uniform (ctx));
}

static TmValue
fn_randbetween (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double lo, hi;

  ARG_NUM (0, lo);
  ARG_NUM (1, hi);
  lo = ceil (lo);
  hi = floor (hi);
  if (hi < lo)
    return tm_value_error (TM_ERR_NUM);
  return tm_value_number (MIN (hi, lo + floor (draw_uniform (ctx) * (hi - lo + 1))));
}

static TmValue
fn_uniform (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double lo, hi;

  ARG_NUM (0, lo);
  ARG_NUM (1, hi);
  if (hi < lo)
    return tm_value_error (TM_ERR_NUM);
  return tm_value_number (lo + (hi - lo) * draw_uniform (ctx));
}

static TmValue
fn_normal (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double mean, sd;

  ARG_NUM (0, mean);
  ARG_NUM (1, sd);
  if (sd < 0)
    return tm_value_error (TM_ERR_NUM);
  return tm_value_number (mean + sd * draw_normal (ctx));
}

/* Parameterised by the mean and standard deviation of the quantity
 * itself, not of its logarithm, because those are what a person knows. */
static TmValue
fn_lognormal (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double mean, sd, s2, mu;

  ARG_NUM (0, mean);
  ARG_NUM (1, sd);
  if (mean <= 0 || sd < 0)
    return tm_value_error (TM_ERR_NUM);
  s2 = log1p ((sd * sd) / (mean * mean));
  mu = log (mean) - s2 / 2;
  return tm_value_number (exp (mu + sqrt (s2) * draw_normal (ctx)));
}

static TmValue
fn_triangular (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double u;
  double lo, mode, hi;

  ARG_NUM (0, lo);
  ARG_NUM (1, mode);
  ARG_NUM (2, hi);
  if (!(lo <= mode && mode <= hi))
    return tm_value_error (TM_ERR_NUM);
  if (stratified (ctx, &u))
    return tm_value_number (tm_triangular_inv (u, lo, mode, hi));
  return tm_value_number (tm_rng_triangular (ctx->rng, lo, mode, hi));
}

/* PERT: a beta distribution stretched over [min, max] with its mode where
 * the estimator put the most likely value.  Its mean, (min + 4 mode + max)
 * / 6, leans on the mode less than a triangle's does, which is why project
 * planners have used it since the Polaris programme. */
static TmValue
fn_pert (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double u;
  double lo, mode, hi, lambda, a, b;

  ARG_NUM (0, lo);
  ARG_NUM (1, mode);
  ARG_NUM (2, hi);
  OPT_NUM (3, lambda, 4);
  if (!(lo <= mode && mode <= hi) || lambda <= 0)
    return tm_value_error (TM_ERR_NUM);
  if (hi == lo)
    return tm_value_number (lo);
  a = 1 + lambda * (mode - lo) / (hi - lo);
  b = 1 + lambda * (hi - mode) / (hi - lo);
  return tm_value_number (lo + (hi - lo) * (stratified (ctx, &u) ? tm_beta_inv (u, a, b)
                                                               : tm_rng_beta (ctx->rng, a, b)));
}

static TmValue
fn_beta (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double u;
  double a, b, lo, hi;

  ARG_NUM (0, a);
  ARG_NUM (1, b);
  OPT_NUM (2, lo, 0);
  OPT_NUM (3, hi, 1);
  if (a <= 0 || b <= 0 || hi < lo)
    return tm_value_error (TM_ERR_NUM);
  return tm_value_number (lo + (hi - lo) * (stratified (ctx, &u) ? tm_beta_inv (u, a, b)
                                                               : tm_rng_beta (ctx->rng, a, b)));
}

static TmValue
fn_gamma (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double u;
  double shape, scale;

  ARG_NUM (0, shape);
  ARG_NUM (1, scale);
  if (shape <= 0 || scale <= 0)
    return tm_value_error (TM_ERR_NUM);
  return tm_value_number (scale * (stratified (ctx, &u) ? tm_gamma_inv (u, shape)
                                                     : tm_rng_gamma (ctx->rng, shape)));
}

static TmValue
fn_expon (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double mean;

  ARG_NUM (0, mean);
  if (mean <= 0)
    return tm_value_error (TM_ERR_NUM);
  return tm_value_number (mean * -log1p (-draw_uniform (ctx)));
}

static TmValue
fn_poisson (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double u;
  double mean;

  ARG_NUM (0, mean);
  if (mean < 0)
    return tm_value_error (TM_ERR_NUM);
  if (stratified (ctx, &u))
    return tm_value_number (tm_poisson_inv (u, mean));
  return tm_value_number ((double) tm_rng_poisson (ctx->rng, mean));
}

static TmValue
fn_binomial (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double u;
  double trials, p;

  ARG_NUM (0, trials);
  ARG_NUM (1, p);
  trials = floor (trials);
  if (trials < 0 || p < 0 || p > 1)
    return tm_value_error (TM_ERR_NUM);
  if (stratified (ctx, &u))
    return tm_value_number (tm_binomial_inv (u, trials, p));
  return tm_value_number ((double) tm_rng_binomial (ctx->rng, (gint64) trials, p));
}

static TmValue
fn_bernoulli (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double p;

  ARG_NUM (0, p);
  if (p < 0 || p > 1)
    return tm_value_error (TM_ERR_NUM);
  return tm_value_number (draw_uniform (ctx) < p ? 1 : 0);
}

static TmValue
fn_student (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double u;
  double df, mean, scale;

  ARG_NUM (0, df);
  OPT_NUM (1, mean, 0);
  OPT_NUM (2, scale, 1);
  if (df <= 0 || scale < 0)
    return tm_value_error (TM_ERR_NUM);
  return tm_value_number (mean + scale * (stratified (ctx, &u) ? tm_t_inv (u, df)
                                                             : tm_rng_student_t (ctx->rng, df)));
}

/* An estimate given as the range one is so sure of -- 90% unless told
 * otherwise -- as a normal distribution whose central interval it is. */
static TmValue
fn_ci (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double lo, hi, conf, z;

  ARG_NUM (0, lo);
  ARG_NUM (1, hi);
  OPT_NUM (2, conf, 0.9);
  if (hi < lo || conf <= 0 || conf >= 1)
    return tm_value_error (TM_ERR_NUM);
  z = tm_norm_inv (0.5 + conf / 2);
  return tm_value_number ((lo + hi) / 2 + (hi - lo) / (2 * z) * draw_normal (ctx));
}

/* The same for a quantity that cannot go below zero and whose upside is
 * longer than its downside -- a price, a duration, a market's size: a
 * lognormal distribution, even on a log scale. */
static TmValue
fn_logci (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double lo, hi, conf, z, mu, sigma;

  ARG_NUM (0, lo);
  ARG_NUM (1, hi);
  OPT_NUM (2, conf, 0.9);
  if (lo <= 0 || hi < lo || conf <= 0 || conf >= 1)
    return tm_value_error (TM_ERR_NUM);
  z = tm_norm_inv (0.5 + conf / 2);
  mu = (log (lo) + log (hi)) / 2;
  sigma = (log (hi) - log (lo)) / (2 * z);
  return tm_value_number (exp (mu + sigma * draw_normal (ctx)));
}

/* One of the values, each as likely as its weight says. */
static TmValue
fn_discrete (TmEvalContext *ctx, TmArg *args, int n)
{
  int nv, nw;
  const TmValue **v = tm_arg_cells (ctx, &args[0], &nv);
  const TmValue **w = tm_arg_cells (ctx, &args[1], &nw);
  double total = 0, u, acc = 0;
  TmValue r = tm_value_error (TM_ERR_NUM);

  if (nv != nw)
    {
      g_free (v);
      g_free (w);
      return tm_value_error (TM_ERR_NA);
    }
  for (int i = 0; i < nw; i++)
    {
      if (w[i]->type != TM_VALUE_NUMBER || w[i]->as.number < 0)
        {
          g_free (v);
          g_free (w);
          return tm_value_error (TM_ERR_NUM);
        }
      total += w[i]->as.number;
    }
  if (total > 0)
    {
      u = draw_uniform (ctx) * total;
      for (int i = 0; i < nw; i++)
        {
          acc += w[i]->as.number;
          if (u < acc || i == nw - 1)
            {
              r = tm_value_copy (v[i]);
              break;
            }
        }
    }
  g_free (v);
  g_free (w);
  return r;
}

/* One of the numbers in the range, each as likely as the next: the
 * bootstrap, which lets history be its own distribution when nothing is
 * known of its shape. */
static TmValue
fn_bootstrap (TmEvalContext *ctx, TmArg *args, int n)
{
  int count, k = 0;
  const TmValue **cells = tm_arg_cells (ctx, &args[0], &count);
  const TmValue **nums = g_new (const TmValue *, MAX (count, 1));
  TmValue r;

  for (int i = 0; i < count; i++)
    if (cells[i]->type == TM_VALUE_NUMBER)
      nums[k++] = cells[i];
  if (k == 0)
    r = tm_value_error (TM_ERR_NUM);
  else
    r = tm_value_copy (nums[MIN (k - 1, (int) floor (draw_uniform (ctx) * k))]);
  g_free (nums);
  g_free (cells);
  return r;
}

/* Geometric Brownian motion, the random walk of a price: where something
 * growing at drift a year, with volatility sigma, will be in t years.  The
 * draw is exact, not a step at a time, because the logarithm of the
 * price is normally distributed. */
static TmValue
fn_gbm (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double s0, mu, sigma, t;

  ARG_NUM (0, s0);
  ARG_NUM (1, mu);
  ARG_NUM (2, sigma);
  ARG_NUM (3, t);
  if (sigma < 0 || t < 0)
    return tm_value_error (TM_ERR_NUM);
  return tm_value_number (s0 * exp ((mu - sigma * sigma / 2) * t
                                    + sigma * sqrt (t) * draw_normal (ctx)));
}

/* Keelin's metalog, three terms, from the 10th, 50th and 90th
 * percentiles an expert gives: a smooth distribution that can lean
 * either way, whose quantile function is simply
 *
 *     M(u) = a1 + a2 L + a3 (u - 1/2) L,   L = ln(u / (1 - u)).
 *
 * Not every triple makes a distribution; one that does not is #NUM!. */
static TmValue
fn_metalog (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double p10, p50, p90, k, a2, a3, u, l;

  ARG_NUM (0, p10);
  ARG_NUM (1, p50);
  ARG_NUM (2, p90);
  if (!(p10 < p50 && p50 < p90))
    return tm_value_error (TM_ERR_NUM);
  k = log (9.0);                        /* ln((1 - 0.1) / 0.1) */
  a2 = (p90 - p10) / (2 * k);
  a3 = (p90 + p10 - 2 * p50) / (0.8 * k);
  if (fabs (a3) / a2 >= 1.66711)
    return tm_value_error (TM_ERR_NUM);
  u = draw_uniform (ctx);
  l = log (u / (1 - u));
  return tm_value_number (p50 + a2 * l + a3 * (u - 0.5) * l);
}

#define RND TM_FN_RANDOM

const TmFunction tm_fn_random[] = {
  FN ("RAND", 0, 0, fn_rand, RND, R, "RAND()", "Uniform between 0 and 1."),
  FN ("RANDBETWEEN", 2, 2, fn_randbetween, RND, R, "RANDBETWEEN(low, high)", "A whole number from low to high."),
  FN ("RAND.UNIFORM", 2, 2, fn_uniform, RND, R, "RAND.UNIFORM(min, max)", "Anywhere between min and max, equally likely."),
  FN ("RAND.NORMAL", 2, 2, fn_normal, RND, R, "RAND.NORMAL(mean, sd)", "The bell curve."),
  FN ("RAND.LOGNORMAL", 2, 2, fn_lognormal, RND, R, "RAND.LOGNORMAL(mean, sd)", "Positive and skewed to the right; mean and sd of the value itself."),
  FN ("RAND.TRIANGULAR", 3, 3, fn_triangular, RND, R, "RAND.TRIANGULAR(min, likely, max)", "A three-point estimate, as a triangle."),
  FN ("RAND.PERT", 3, 4, fn_pert, RND, R, "RAND.PERT(min, likely, max, [lambda])", "A three-point estimate, as a smooth PERT-beta curve."),
  FN ("RAND.BETA", 2, 4, fn_beta, RND, R, "RAND.BETA(alpha, beta, [min], [max])", "The beta distribution, for proportions and rates."),
  FN ("RAND.GAMMA", 2, 2, fn_gamma, RND, R, "RAND.GAMMA(shape, scale)", "The gamma distribution, for waiting times and sizes."),
  FN ("RAND.EXPON", 1, 1, fn_expon, RND, R, "RAND.EXPON(mean)", "Time to the next event of a steady random process."),
  FN ("RAND.POISSON", 1, 1, fn_poisson, RND, R, "RAND.POISSON(mean)", "How many events, when mean are expected."),
  FN ("RAND.BINOMIAL", 2, 2, fn_binomial, RND, R, "RAND.BINOMIAL(trials, p)", "How many of so many trials succeed."),
  FN ("RAND.BERNOULLI", 1, 1, fn_bernoulli, RND, R, "RAND.BERNOULLI(p)", "1 with probability p, else 0: will it happen?"),
  FN ("RAND.STUDENT", 1, 3, fn_student, RND, R, "RAND.STUDENT(df, [mean], [scale])", "Student's t: a bell curve with fat tails."),
  FN ("RAND.CI", 2, 3, fn_ci, RND, R, "RAND.CI(low, high, [confidence])", "A normal estimate from a range you are 90% sure of."),
  FN ("RAND.LOGCI", 2, 3, fn_logci, RND, R, "RAND.LOGCI(low, high, [confidence])", "A lognormal estimate from a range you are 90% sure of."),
  FN ("RAND.METALOG", 3, 3, fn_metalog, RND, R, "RAND.METALOG(p10, p50, p90)", "An expert's three percentiles, as a smooth, possibly skewed curve."),
  FN ("RAND.DISCRETE", 2, 2, fn_discrete, RND, R, "RAND.DISCRETE(values, weights)", "One of the values, as likely as its weight."),
  FN ("RAND.BOOTSTRAP", 1, 1, fn_bootstrap, RND, R, "RAND.BOOTSTRAP(history)", "One of the numbers in the range: history resampled."),
  FN ("RAND.GBM", 4, 4, fn_gbm, RND, P, "RAND.GBM(start, drift, volatility, time)", "Geometric Brownian motion: a price after so much time."),
};
const int tm_fn_random_count = G_N_ELEMENTS (tm_fn_random);
