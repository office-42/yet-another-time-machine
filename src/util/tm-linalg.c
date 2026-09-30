/* tm-linalg.c - the little linear algebra that fitting a table needs
 *
 * Copyright (C) 2026 The timemachine authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "tm-linalg.h"

#include <math.h>
#include <string.h>

gboolean
tm_lsq_fit (const double *x, const double *y, int n, int q, TmLsq *fit)
{
  double *a, *b, *diag, big = 0;

  memset (fit, 0, sizeof *fit);
  if (q < 1 || n < q)
    return FALSE;
  a = g_memdup2 (x, sizeof (double) * n * q);
  b = g_memdup2 (y, sizeof (double) * n);
  diag = g_new (double, q);

  /* Column by column, a reflection that zeroes it below the diagonal,
   * applied to the columns after it and to y. */
  for (int k = 0; k < q; k++)
    {
      double norm = 0, alpha, vv = 0;

      for (int i = k; i < n; i++)
        norm += a[i * q + k] * a[i * q + k];
      norm = sqrt (norm);
      alpha = a[k * q + k] > 0 ? -norm : norm;
      a[k * q + k] -= alpha;
      for (int i = k; i < n; i++)
        vv += a[i * q + k] * a[i * q + k];
      if (vv > 0)
        {
          for (int j = k + 1; j < q; j++)
            {
              double s = 0;
              for (int i = k; i < n; i++)
                s += a[i * q + k] * a[i * q + j];
              s *= 2 / vv;
              for (int i = k; i < n; i++)
                a[i * q + j] -= s * a[i * q + k];
            }
          {
            double s = 0;
            for (int i = k; i < n; i++)
              s += a[i * q + k] * b[i];
            s *= 2 / vv;
            for (int i = k; i < n; i++)
              b[i] -= s * a[i * q + k];
          }
        }
      diag[k] = alpha;
      big = MAX (big, fabs (alpha));
    }

  for (int k = 0; k < q; k++)
    if (!(fabs (diag[k]) > 1e-10 * big))
      {
        g_free (a);
        g_free (b);
        g_free (diag);
        return FALSE;
      }

  fit->q = q;
  fit->r = g_new0 (double, q * q);
  fit->beta = g_new (double, q);
  for (int k = 0; k < q; k++)
    {
      fit->r[k * q + k] = diag[k];
      for (int j = k + 1; j < q; j++)
        fit->r[k * q + j] = a[k * q + j];
    }
  /* R beta = (Q'y), the first q of it, from the bottom up. */
  for (int k = q - 1; k >= 0; k--)
    {
      double s = b[k];
      for (int j = k + 1; j < q; j++)
        s -= fit->r[k * q + j] * fit->beta[j];
      fit->beta[k] = s / fit->r[k * q + k];
    }
  fit->sse = 0;
  for (int i = 0; i < n; i++)
    {
      double e = y[i];
      for (int j = 0; j < q; j++)
        e -= x[i * q + j] * fit->beta[j];
      fit->sse += e * e;
    }
  g_free (a);
  g_free (b);
  g_free (diag);
  return TRUE;
}

double
tm_lsq_leverage (const TmLsq *fit, const double *x0)
{
  int q = fit->q;
  double *v = g_new (double, q), h = 0;

  /* R'v = x0, from the top down: then x0'(R'R)^-1 x0 = v'v. */
  for (int k = 0; k < q; k++)
    {
      double s = x0[k];
      for (int j = 0; j < k; j++)
        s -= fit->r[j * q + k] * v[j];
      v[k] = s / fit->r[k * q + k];
      h += v[k] * v[k];
    }
  g_free (v);
  return h;
}

void
tm_lsq_clear (TmLsq *fit)
{
  g_clear_pointer (&fit->beta, g_free);
  g_clear_pointer (&fit->r, g_free);
}

gboolean
tm_cholesky (double *a, int n)
{
  for (int j = 0; j < n; j++)
    {
      double d = a[j * n + j];

      for (int k = 0; k < j; k++)
        d -= a[j * n + k] * a[j * n + k];
      if (!(d > 0))
        return FALSE;
      d = sqrt (d);
      a[j * n + j] = d;
      for (int i = j + 1; i < n; i++)
        {
          double s = a[i * n + j];
          for (int k = 0; k < j; k++)
            s -= a[i * n + k] * a[j * n + k];
          a[i * n + j] = s / d;
        }
      for (int k = j + 1; k < n; k++)
        a[j * n + k] = 0;
    }
  return TRUE;
}

void
tm_forward_solve (const double *l, int n, double *b)
{
  for (int i = 0; i < n; i++)
    {
      double s = b[i];
      for (int k = 0; k < i; k++)
        s -= l[i * n + k] * b[k];
      b[i] = s / l[i * n + i];
    }
}

void
tm_cholesky_solve (const double *l, int n, double *b)
{
  tm_forward_solve (l, n, b);
  for (int i = n - 1; i >= 0; i--)
    {
      double s = b[i];
      for (int k = i + 1; k < n; k++)
        s -= l[k * n + i] * b[k];
      b[i] = s / l[i * n + i];
    }
}
