/* tm-stats.c - statistics over arrays of samples
 *
 * Copyright (C) 2026 The timemachine authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "tm-stats.h"

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
