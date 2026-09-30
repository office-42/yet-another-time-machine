/* tm-sim.c - the futures a simulation found
 *
 * Copyright (C) 2026 The timemachine authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "tm-sim.h"
#include "tm-stats.h"

#include <math.h>
#include <stdlib.h>

typedef struct {
  double *raw;       /* iterations of them */
  double *sorted;    /* the numbers among them, ascending; made on demand */
  int     n_sorted;
  double *ranks;     /* of raw, when every future gave a number; on demand */
} Series;

struct _TmSim {
  int         iterations;
  guint64     seed;
  double      seconds;
  gboolean    stale;
  GHashTable *series;    /* key -> Series */
};

static void
series_free (gpointer p)
{
  Series *s = p;

  g_free (s->raw);
  g_free (s->sorted);
  g_free (s->ranks);
  g_free (s);
}

TmSim *
tm_sim_new (int iterations, guint64 seed)
{
  TmSim *sim = g_new0 (TmSim, 1);

  sim->iterations = iterations;
  sim->seed = seed;
  sim->series = g_hash_table_new_full (g_int64_hash, g_int64_equal, g_free, series_free);
  return sim;
}

void
tm_sim_free (TmSim *sim)
{
  if (sim == NULL)
    return;
  g_hash_table_destroy (sim->series);
  g_free (sim);
}

int      tm_sim_iterations (const TmSim *sim) { return sim->iterations; }
guint64  tm_sim_seed (const TmSim *sim) { return sim->seed; }
double   tm_sim_seconds (const TmSim *sim) { return sim->seconds; }
void     tm_sim_set_seconds (TmSim *sim, double seconds) { sim->seconds = seconds; }
gboolean tm_sim_stale (const TmSim *sim) { return sim->stale; }
void     tm_sim_set_stale (TmSim *sim, gboolean stale) { sim->stale = stale; }

static Series *
lookup (const TmSim *sim, int row, int col)
{
  gint64 key = (gint64) tm_key (row, col);
  return g_hash_table_lookup (sim->series, &key);
}

double *
tm_sim_track (TmSim *sim, int row, int col)
{
  Series *s = lookup (sim, row, col);

  if (s == NULL)
    {
      gint64 *key = g_new (gint64, 1);

      *key = (gint64) tm_key (row, col);
      s = g_new0 (Series, 1);
      s->raw = g_new (double, sim->iterations);
      for (int i = 0; i < sim->iterations; i++)
        s->raw[i] = NAN;
      g_hash_table_insert (sim->series, key, s);
    }
  return s->raw;
}

gboolean
tm_sim_has (const TmSim *sim, int row, int col)
{
  return sim != NULL && lookup (sim, row, col) != NULL;
}

int
tm_sim_n_tracked (const TmSim *sim)
{
  return (int) g_hash_table_size (sim->series);
}

static int
compare_keys (const void *a, const void *b)
{
  const TmRef *x = a, *y = b;

  if (x->row != y->row)
    return x->row < y->row ? -1 : 1;
  return x->col < y->col ? -1 : x->col > y->col;
}

TmRef *
tm_sim_cells (const TmSim *sim, int *n)
{
  GHashTableIter iter;
  gpointer key;
  TmRef *refs = g_new (TmRef, g_hash_table_size (sim->series) + 1);
  int k = 0;

  g_hash_table_iter_init (&iter, sim->series);
  while (g_hash_table_iter_next (&iter, &key, NULL))
    {
      guint64 kk = (guint64) *(gint64 *) key;
      refs[k].row = tm_key_row (kk);
      refs[k].col = tm_key_col (kk);
      k++;
    }
  qsort (refs, (size_t) k, sizeof (TmRef), compare_keys);
  *n = k;
  return refs;
}

const double *
tm_sim_samples (TmSim *sim, int row, int col, gboolean sorted, int *n)
{
  Series *s;

  *n = 0;
  if (sim == NULL || (s = lookup (sim, row, col)) == NULL)
    return NULL;
  if (!sorted)
    {
      *n = sim->iterations;
      return s->raw;
    }
  if (s->sorted == NULL)
    {
      s->sorted = g_new (double, sim->iterations);
      s->n_sorted = 0;
      for (int i = 0; i < sim->iterations; i++)
        if (!isnan (s->raw[i]))
          s->sorted[s->n_sorted++] = s->raw[i];
      qsort (s->sorted, (size_t) s->n_sorted, sizeof (double), tm_compare_doubles);
    }
  *n = s->n_sorted;
  return s->sorted;
}

/* The ranks of a series every future of which is a number, kept, since a
 * tornado chart asks for the same inputs' ranks again and again. */
static const double *
ranks_of (TmSim *sim, Series *s)
{
  int n;

  if (s->ranks != NULL)
    return s->ranks;
  for (n = 0; n < sim->iterations; n++)
    if (isnan (s->raw[n]))
      return NULL;
  s->ranks = g_new (double, sim->iterations);
  tm_ranks (s->raw, sim->iterations, s->ranks);
  return s->ranks;
}

double
tm_sim_rank_correlation (TmSim *sim, int row1, int col1, int row2, int col2)
{
  Series *a, *b;
  const double *ra, *rb;
  double *x, *y, *rx, *ry, rho;
  int k = 0;

  if (sim == NULL || (a = lookup (sim, row1, col1)) == NULL || (b = lookup (sim, row2, col2)) == NULL)
    return NAN;
  ra = ranks_of (sim, a);
  rb = ranks_of (sim, b);
  if (ra != NULL && rb != NULL)
    return tm_correlation (ra, rb, sim->iterations);

  /* Some futures gave no number: rank only those where both did. */
  x = g_new (double, sim->iterations);
  y = g_new (double, sim->iterations);
  for (int i = 0; i < sim->iterations; i++)
    if (!isnan (a->raw[i]) && !isnan (b->raw[i]))
      {
        x[k] = a->raw[i];
        y[k] = b->raw[i];
        k++;
      }
  rx = g_new (double, MAX (k, 1));
  ry = g_new (double, MAX (k, 1));
  tm_ranks (x, k, rx);
  tm_ranks (y, k, ry);
  rho = tm_correlation (rx, ry, k);
  g_free (x);
  g_free (y);
  g_free (rx);
  g_free (ry);
  return rho;
}

gboolean
tm_sim_stats (TmSim *sim, int row, int col, TmSimStats *out)
{
  int n;
  const double *x = tm_sim_samples (sim, row, col, TRUE, &n);
  double mean = 0, ss = 0;

  if (x == NULL)
    return FALSE;
  out->iterations = sim->iterations;
  out->valid = n;
  if (n == 0)
    {
      out->mean = out->sd = out->se = out->min = out->max = NAN;
      out->p5 = out->p10 = out->p25 = out->p50 = out->p75 = out->p90 = out->p95 = NAN;
      return TRUE;
    }
  for (int i = 0; i < n; i++)
    mean += x[i];
  mean /= n;
  for (int i = 0; i < n; i++)
    ss += (x[i] - mean) * (x[i] - mean);
  out->mean = mean;
  out->sd = n > 1 ? sqrt (ss / (n - 1)) : 0;
  out->se = out->sd / sqrt (n);
  out->min = x[0];
  out->max = x[n - 1];
  out->p5 = tm_percentile_sorted (x, n, 0.05);
  out->p10 = tm_percentile_sorted (x, n, 0.10);
  out->p25 = tm_percentile_sorted (x, n, 0.25);
  out->p50 = tm_percentile_sorted (x, n, 0.50);
  out->p75 = tm_percentile_sorted (x, n, 0.75);
  out->p90 = tm_percentile_sorted (x, n, 0.90);
  out->p95 = tm_percentile_sorted (x, n, 0.95);
  return TRUE;
}

gboolean
tm_sim_histogram (TmSim *sim, int row, int col, int bins,
                  double *lo, double *hi, int *counts)
{
  int n;
  const double *x = tm_sim_samples (sim, row, col, TRUE, &n);
  double a, b, w;

  if (x == NULL || n == 0 || bins <= 0)
    return FALSE;
  /* The axis runs from the least future to the greatest -- unless a tail
   * is long, when its outermost half percent is left off, so that one
   * wild future does not squash the other 9,999 into a single bar. */
  a = x[0];
  b = x[n - 1];
  if (n >= 200)
    {
      double p_lo = tm_percentile_sorted (x, n, 0.005);
      double p_hi = tm_percentile_sorted (x, n, 0.995);

      if (p_lo - a > 0.25 * (p_hi - p_lo))
        a = p_lo;
      if (b - p_hi > 0.25 * (p_hi - p_lo))
        b = p_hi;
    }
  if (b <= a)
    {
      double pad = fabs (a) > 0 ? fabs (a) * 0.05 : 0.5;
      a -= pad;
      b += pad;
    }
  w = (b - a) / bins;
  for (int i = 0; i < bins; i++)
    counts[i] = 0;
  for (int i = 0; i < n; i++)
    {
      int k = (int) floor ((x[i] - a) / w);

      /* The greatest value lands exactly on the last edge. */
      if (k == bins && x[i] <= b)
        k = bins - 1;
      if (k >= 0 && k < bins)
        counts[k]++;
    }
  *lo = a;
  *hi = b;
  return TRUE;
}
