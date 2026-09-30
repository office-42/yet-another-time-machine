/* tm-rng.c - random numbers, and the distributions drawn from them
 *
 * Copyright (C) 2026 The timemachine authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "tm-rng.h"

#include <math.h>

static inline guint64
rotl (guint64 x, int k)
{
  return (x << k) | (x >> (64 - k));
}

static guint64
splitmix64 (guint64 *state)
{
  guint64 z = (*state += 0x9e3779b97f4a7c15ULL);

  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
  return z ^ (z >> 31);
}

void
tm_rng_seed (TmRng *rng, guint64 seed)
{
  guint64 state = seed;

  for (int i = 0; i < 4; i++)
    rng->s[i] = splitmix64 (&state);
  rng->has_spare = FALSE;
  rng->spare = 0;
}

guint64
tm_rng_next (TmRng *rng)
{
  guint64 *s = rng->s;
  guint64 result = rotl (s[1] * 5, 7) * 9;
  guint64 t = s[1] << 17;

  s[2] ^= s[0];
  s[3] ^= s[1];
  s[1] ^= s[2];
  s[0] ^= s[3];
  s[2] ^= t;
  s[3] = rotl (s[3], 45);
  return result;
}

double
tm_rng_uniform (TmRng *rng)
{
  /* The top 53 bits, shifted half a step off zero: (k + 0.5) / 2^53. */
  return ((double) (tm_rng_next (rng) >> 11) + 0.5) * (1.0 / 9007199254740992.0);
}

gint64
tm_rng_int (TmRng *rng, gint64 lo, gint64 hi)
{
  guint64 span, limit, x;

  if (hi <= lo)
    return lo;
  span = (guint64) (hi - lo) + 1;
  if (span == 0)                       /* the whole 64-bit range */
    return (gint64) tm_rng_next (rng);
  /* Reject the top sliver that would make low values likelier. */
  limit = G_MAXUINT64 - (G_MAXUINT64 % span);
  do
    x = tm_rng_next (rng);
  while (x >= limit);
  return lo + (gint64) (x % span);
}

double
tm_rng_normal (TmRng *rng)
{
  double u, v, s, f;

  if (rng->has_spare)
    {
      rng->has_spare = FALSE;
      return rng->spare;
    }

  /* Marsaglia's polar method: a point uniform in the unit disc gives two
   * independent normals without a sine or a cosine. */
  do
    {
      u = 2.0 * tm_rng_uniform (rng) - 1.0;
      v = 2.0 * tm_rng_uniform (rng) - 1.0;
      s = u * u + v * v;
    }
  while (s >= 1.0 || s == 0.0);

  f = sqrt (-2.0 * log (s) / s);
  rng->spare = v * f;
  rng->has_spare = TRUE;
  return u * f;
}

double
tm_rng_gamma (TmRng *rng, double shape)
{
  double d, c;

  if (!(shape > 0))
    return NAN;

  /* Below one, draw at shape + 1 and scale down by U^(1/shape), which is
   * exact (Marsaglia and Tsang, section 6). */
  if (shape < 1.0)
    return tm_rng_gamma (rng, shape + 1.0) * pow (tm_rng_uniform (rng), 1.0 / shape);

  /* Marsaglia and Tsang (2000): a cubed normal, squeezed then accepted. */
  d = shape - 1.0 / 3.0;
  c = 1.0 / sqrt (9.0 * d);
  for (;;)
    {
      double x, v, u;

      do
        {
          x = tm_rng_normal (rng);
          v = 1.0 + c * x;
        }
      while (v <= 0.0);
      v = v * v * v;
      u = tm_rng_uniform (rng);
      if (u < 1.0 - 0.0331 * (x * x) * (x * x))
        return d * v;
      if (log (u) < 0.5 * x * x + d * (1.0 - v + log (v)))
        return d * v;
    }
}

double
tm_rng_beta (TmRng *rng, double a, double b)
{
  double x, y;

  if (!(a > 0) || !(b > 0))
    return NAN;
  x = tm_rng_gamma (rng, a);
  y = tm_rng_gamma (rng, b);
  return x / (x + y);
}

double
tm_rng_exponential (TmRng *rng)
{
  return -log (tm_rng_uniform (rng));
}

double
tm_rng_triangular (TmRng *rng, double lo, double mode, double hi)
{
  double u = tm_rng_uniform (rng);
  double f;

  if (hi <= lo)
    return lo;
  /* The inverse of the distribution function, piece by piece either side
   * of the mode. */
  f = (mode - lo) / (hi - lo);
  if (u < f)
    return lo + sqrt (u * (hi - lo) * (mode - lo));
  return hi - sqrt ((1.0 - u) * (hi - lo) * (hi - mode));
}

gint64
tm_rng_poisson (TmRng *rng, double lambda)
{
  if (!(lambda >= 0))
    return -1;
  if (lambda == 0)
    return 0;

  if (lambda < 10.0)
    {
      /* Knuth: multiply uniforms until the product drops below e^-lambda. */
      double limit = exp (-lambda), prod = tm_rng_uniform (rng);
      gint64 k = 0;

      while (prod > limit)
        {
          prod *= tm_rng_uniform (rng);
          k++;
        }
      return k;
    }

  /* Hörmann's PTRS, transformed rejection with squeeze (1993): constant
   * time however large lambda is. */
  {
    double slam = sqrt (lambda), loglam = log (lambda);
    double b = 0.931 + 2.53 * slam;
    double a = -0.059 + 0.02483 * b;
    double invalpha = 1.1239 + 1.1328 / (b - 3.4);
    double vr = 0.9277 - 3.6224 / (b - 2.0);

    for (;;)
      {
        double u = tm_rng_uniform (rng) - 0.5;
        double v = tm_rng_uniform (rng);
        double us = 0.5 - fabs (u);
        double k = floor ((2.0 * a / us + b) * u + lambda + 0.43);

        if (us >= 0.07 && v <= vr)
          return (gint64) k;
        if (k < 0 || (us < 0.013 && v > us))
          continue;
        if (log (v) + log (invalpha) - log (a / (us * us) + b)
            <= -lambda + k * loglam - lgamma (k + 1.0))
          return (gint64) k;
      }
  }
}

gint64
tm_rng_binomial (TmRng *rng, gint64 n, double p)
{
  gboolean flip = FALSE;
  gint64 k = 0;

  if (n <= 0 || !(p > 0))
    return 0;
  if (p >= 1)
    return n;
  if (p > 0.5)
    {
      p = 1.0 - p;
      flip = TRUE;
    }

  if ((double) n * p < 30.0)
    {
      /* Waiting times: the gaps between successes are geometric, so the
       * work is proportional to the number of successes, not of trials. */
      double log_q = log1p (-p);
      gint64 trials = 0;

      for (;;)
        {
          trials += (gint64) floor (log (tm_rng_uniform (rng)) / log_q) + 1;
          if (trials > n)
            break;
          k++;
        }
    }
  else
    {
      /* With thirty expected successes or more the normal approximation,
       * rounded and clamped, is within a whisker of the real thing. */
      double mean = (double) n * p, sd = sqrt (mean * (1.0 - p));

      k = (gint64) floor (mean + sd * tm_rng_normal (rng) + 0.5);
      k = CLAMP (k, 0, n);
    }

  return flip ? n - k : k;
}

double
tm_rng_student_t (TmRng *rng, double df)
{
  if (!(df > 0))
    return NAN;
  return tm_rng_normal (rng) / sqrt (2.0 * tm_rng_gamma (rng, df / 2.0) / df);
}

double
tm_norm_cdf (double x)
{
  return 0.5 * erfc (-x / G_SQRT2);
}

double
tm_norm_inv (double p)
{
  static const double a[] = {
    -3.969683028665376e+01, 2.209460984245205e+02, -2.759285104469687e+02,
    1.383577518672690e+02, -3.066479806614716e+01, 2.506628277459239e+00
  };
  static const double b[] = {
    -5.447609879822406e+01, 1.615858368580409e+02, -1.556989798598866e+02,
    6.680131188771972e+01, -1.328068155288572e+01
  };
  static const double c[] = {
    -7.784894002430293e-03, -3.223964580411365e-01, -2.400758277161838e+00,
    -2.549732539343734e+00, 4.374664141464968e+00, 2.938163982698783e+00
  };
  static const double d[] = {
    7.784695709041462e-03, 3.224671290700398e-01, 2.445134137142996e+00,
    3.754408661907416e+00
  };
  const double lo = 0.02425, hi = 1.0 - lo;
  double q, r, x;

  if (!(p > 0.0 && p < 1.0))
    {
      if (p == 0.0)
        return -INFINITY;
      if (p == 1.0)
        return INFINITY;
      return NAN;
    }

  if (p < lo)
    {
      q = sqrt (-2.0 * log (p));
      x = (((((c[0] * q + c[1]) * q + c[2]) * q + c[3]) * q + c[4]) * q + c[5])
          / ((((d[0] * q + d[1]) * q + d[2]) * q + d[3]) * q + 1.0);
    }
  else if (p <= hi)
    {
      q = p - 0.5;
      r = q * q;
      x = (((((a[0] * r + a[1]) * r + a[2]) * r + a[3]) * r + a[4]) * r + a[5]) * q
          / (((((b[0] * r + b[1]) * r + b[2]) * r + b[3]) * r + b[4]) * r + 1.0);
    }
  else
    {
      q = sqrt (-2.0 * log (1.0 - p));
      x = -(((((c[0] * q + c[1]) * q + c[2]) * q + c[3]) * q + c[4]) * q + c[5])
          / ((((d[0] * q + d[1]) * q + d[2]) * q + d[3]) * q + 1.0);
    }

  /* One step of Halley's method against the exact distribution function
   * takes the approximation's 1e-9 to the double's last digits. */
  {
    double e = tm_norm_cdf (x) - p;
    double u = e * sqrt (2.0 * G_PI) * exp (x * x / 2.0);

    x = x - u / (1.0 + x * u / 2.0);
  }
  return x;
}
