/* tm-stats.c - statistics over arrays of samples
 *
 * Copyright (C) 2026 The timemachine authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "tm-stats.h"
#include "tm-rng.h"

#include <math.h>
#include <stdlib.h>

int
tm_compare_doubles (const void *a, const void *b)
{
  double x = *(const double *) a, y = *(const double *) b;
  return x < y ? -1 : x > y;
}

double
tm_percentile_sorted (const double *sorted, int n, double p)
{
  double h, lo;
  int i;

  if (n <= 0)
    return NAN;
  if (n == 1 || p <= 0)
    return sorted[0];
  if (p >= 1)
    return sorted[n - 1];
  /* Rank (n - 1) p, interpolated between its neighbours. */
  h = (n - 1) * p;
  lo = floor (h);
  i = (int) lo;
  if (i + 1 >= n)
    return sorted[n - 1];
  return sorted[i] + (h - lo) * (sorted[i + 1] - sorted[i]);
}

typedef struct {
  double value;
  int index;
} Keyed;

static int
compare_keyed (const void *a, const void *b)
{
  const Keyed *x = a, *y = b;

  if (x->value != y->value)
    return x->value < y->value ? -1 : 1;
  return x->index - y->index;
}

void
tm_ranks (const double *x, int n, double *rank)
{
  Keyed *k = g_new (Keyed, MAX (n, 1));

  for (int i = 0; i < n; i++)
    {
      k[i].value = x[i];
      k[i].index = i;
    }
  qsort (k, (size_t) n, sizeof (Keyed), compare_keyed);
  for (int i = 0; i < n;)
    {
      int j = i;

      while (j + 1 < n && k[j + 1].value == k[i].value)
        j++;
      for (int m = i; m <= j; m++)
        rank[k[m].index] = (i + j) / 2.0;
      i = j + 1;
    }
  g_free (k);
}

double
tm_correlation (const double *x, const double *y, int n)
{
  double mx = 0, my = 0, sxy = 0, sxx = 0, syy = 0;

  if (n < 2)
    return NAN;
  for (int i = 0; i < n; i++)
    {
      mx += x[i];
      my += y[i];
    }
  mx /= n;
  my /= n;
  for (int i = 0; i < n; i++)
    {
      sxy += (x[i] - mx) * (y[i] - my);
      sxx += (x[i] - mx) * (x[i] - mx);
      syy += (y[i] - my) * (y[i] - my);
    }
  if (sxx == 0 || syy == 0)
    return NAN;
  return sxy / sqrt (sxx * syy);
}

/* ---- Distribution functions and their inverses ------------------------ */

/* The continued fraction for the incomplete beta function, by the
 * modified Lentz method (Numerical Recipes' betacf). */
static double
beta_cf (double a, double b, double x)
{
  const double tiny = 1e-300;
  double c = 1, d = 1 - (a + b) * x / (a + 1), h;

  if (fabs (d) < tiny)
    d = tiny;
  d = 1 / d;
  h = d;
  for (int m = 1; m <= 300; m++)
    {
      double m2 = 2.0 * m, aa, del;

      aa = m * (b - m) * x / ((a + m2 - 1) * (a + m2));
      d = 1 + aa * d;
      if (fabs (d) < tiny) d = tiny;
      c = 1 + aa / c;
      if (fabs (c) < tiny) c = tiny;
      d = 1 / d;
      h *= d * c;
      aa = -(a + m) * (a + b + m) * x / ((a + m2) * (a + m2 + 1));
      d = 1 + aa * d;
      if (fabs (d) < tiny) d = tiny;
      c = 1 + aa / c;
      if (fabs (c) < tiny) c = tiny;
      d = 1 / d;
      del = d * c;
      h *= del;
      if (fabs (del - 1) < 1e-15)
        break;
    }
  return h;
}

double
tm_beta_inc (double a, double b, double x)
{
  double bt;

  if (x <= 0)
    return 0;
  if (x >= 1)
    return 1;
  bt = exp (lgamma (a + b) - lgamma (a) - lgamma (b) + a * log (x) + b * log1p (-x));
  if (x < (a + 1) / (a + b + 2))
    return bt * beta_cf (a, b, x) / a;
  return 1 - bt * beta_cf (b, a, 1 - x) / b;
}

/* Newton's method on f(x) = F(x) - p, falling back to bisection whenever
 * a step would leave the bracket [lo, hi] that is known to hold the
 * answer. */
typedef double (*Cdf) (double x, const double *params);
typedef double (*Pdf) (double x, const double *params);

static double
invert (Cdf cdf, Pdf pdf, const double *params, double p, double x, double lo, double hi)
{
  for (int i = 0; i < 200; i++)
    {
      double f = cdf (x, params) - p, d, next;

      if (f == 0)
        return x;
      if (f < 0)
        lo = x;
      else
        hi = x;
      d = pdf (x, params);
      next = d > 0 ? x - f / d : NAN;
      /* Outside the bracket: halve it, or, if it is open on the side
       * the answer lies, stride out that way. */
      if (!(next > lo && next < hi))
        {
          if (isinf (hi))
            next = x > 0 ? 2 * x : x + 1;
          else if (isinf (lo))
            next = x < 0 ? 2 * x : x - 1;
          else
            next = (lo + hi) / 2;
        }
      if (fabs (next - x) <= 1e-13 * MAX (1.0, fabs (x)))
        return next;
      x = next;
    }
  return x;
}

static double
beta_cdf (double x, const double *ab)
{
  return tm_beta_inc (ab[0], ab[1], x);
}

static double
beta_pdf (double x, const double *ab)
{
  if (x <= 0 || x >= 1)
    return 0;
  return exp ((ab[0] - 1) * log (x) + (ab[1] - 1) * log1p (-x)
              + lgamma (ab[0] + ab[1]) - lgamma (ab[0]) - lgamma (ab[1]));
}

double
tm_beta_inv (double p, double a, double b)
{
  double ab[2] = { a, b };

  if (p <= 0)
    return 0;
  if (p >= 1)
    return 1;
  return invert (beta_cdf, beta_pdf, ab, p, a / (a + b), 0, 1);
}

double
tm_gamma_p (double a, double x)
{
  double gln = lgamma (a);

  if (x <= 0)
    return 0;
  if (x < a + 1)
    {
      /* The series. */
      double ap = a, sum = 1 / a, del = sum;

      for (int n = 0; n < 1000; n++)
        {
          ap += 1;
          del *= x / ap;
          sum += del;
          if (fabs (del) < fabs (sum) * 1e-16)
            break;
        }
      return sum * exp (-x + a * log (x) - gln);
    }
  else
    {
      /* The continued fraction, for the upper function. */
      const double tiny = 1e-300;
      double b = x + 1 - a, c = 1 / tiny, d = 1 / b, h = d;

      for (int i = 1; i < 1000; i++)
        {
          double an = -i * (i - a), del;

          b += 2;
          d = an * d + b;
          if (fabs (d) < tiny) d = tiny;
          c = b + an / c;
          if (fabs (c) < tiny) c = tiny;
          d = 1 / d;
          del = d * c;
          h *= del;
          if (fabs (del - 1) < 1e-16)
            break;
        }
      return 1 - exp (-x + a * log (x) - gln) * h;
    }
}

static double
gamma_cdf (double x, const double *a)
{
  return tm_gamma_p (a[0], x);
}

static double
gamma_pdf (double x, const double *a)
{
  if (x <= 0)
    return 0;
  return exp ((a[0] - 1) * log (x) - x - lgamma (a[0]));
}

double
tm_gamma_inv (double p, double a)
{
  double z, x;

  if (p <= 0)
    return 0;
  if (p >= 1)
    return INFINITY;
  /* Wilson and Hilferty's cube-root normal approximation to start. */
  z = tm_norm_inv (p);
  x = a * pow (1 - 1 / (9 * a) + z / (3 * sqrt (a)), 3);
  if (!(x > 0))
    x = a * 0.5;
  return invert (gamma_cdf, gamma_pdf, &a, p, x, 0, INFINITY);
}

double
tm_t_cdf (double t, double df)
{
  double x = df / (df + t * t);
  double tail = 0.5 * tm_beta_inc (df / 2, 0.5, x);

  return t >= 0 ? 1 - tail : tail;
}

static double
t_cdf (double x, const double *df)
{
  return tm_t_cdf (x, df[0]);
}

static double
t_pdf (double x, const double *df)
{
  double v = df[0];

  return exp (lgamma ((v + 1) / 2) - lgamma (v / 2) - 0.5 * log (v * G_PI)
              - (v + 1) / 2 * log1p (x * x / v));
}

double
tm_t_inv (double p, double df)
{
  if (p <= 0)
    return -INFINITY;
  if (p >= 1)
    return INFINITY;
  return invert (t_cdf, t_pdf, &df, p, tm_norm_inv (p), -INFINITY, INFINITY);
}

double
tm_poisson_inv (double p, double lambda)
{
  double k, cdf, pmf;

  if (lambda <= 0)
    return 0;
  if (lambda < 200)
    {
      /* Up the distribution from zero, a term at a time. */
      k = 0;
      pmf = exp (-lambda);
      cdf = pmf;
      while (cdf < p && k < 100000)
        {
          k += 1;
          pmf *= lambda / k;
          cdf += pmf;
        }
      return k;
    }
  /* From the normal approximation, stepping to the exact answer:
   * P(X <= k) is the upper incomplete gamma function Q(k + 1, lambda). */
  k = floor (lambda + sqrt (lambda) * tm_norm_inv (p));
  k = MAX (0, k);
  while (k > 0 && 1 - tm_gamma_p (k, lambda) >= p)
    k -= 1;
  while (1 - tm_gamma_p (k + 1, lambda) < p)
    k += 1;
  return k;
}

double
tm_binomial_inv (double p, double n, double prob)
{
  double k;

  if (prob <= 0 || n <= 0)
    return 0;
  if (prob >= 1)
    return n;
  /* P(X <= k) = I_{1-prob}(n - k, k + 1), from a normal first guess. */
  k = floor (n * prob + sqrt (n * prob * (1 - prob)) * tm_norm_inv (p));
  k = CLAMP (k, 0, n);
#define BCDF(kk) ((kk) >= n ? 1.0 : tm_beta_inc (n - (kk), (kk) + 1, 1 - prob))
  while (k > 0 && BCDF (k - 1) >= p)
    k -= 1;
  while (k < n && BCDF (k) < p)
    k += 1;
#undef BCDF
  return k;
}

double
tm_triangular_inv (double p, double lo, double mode, double hi)
{
  double f;

  if (hi <= lo)
    return lo;
  f = (mode - lo) / (hi - lo);
  if (p < f)
    return lo + sqrt (p * (hi - lo) * (mode - lo));
  return hi - sqrt ((1 - p) * (hi - lo) * (hi - mode));
}
