/* tm-sim.h - the futures a simulation found
 *
 * Copyright (C) 2026 The timemachine authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A simulation recalculates the sheet thousands of times, each time with
 * fresh draws from every uncertain cell, and keeps what the cells that
 * depend on them came to: one sample per iteration, per cell.  TmSim is
 * that store and the arithmetic over it -- the statistics, percentiles and
 * histograms the forecast panel and the SIM.* functions show.
 */

#pragma once

#include "tm-types.h"

G_BEGIN_DECLS

typedef struct _TmSim TmSim;

TmSim   *tm_sim_new        (int iterations, guint64 seed, gboolean latin);
void     tm_sim_free       (TmSim *sim);

int      tm_sim_iterations (const TmSim *sim);
guint64  tm_sim_seed       (const TmSim *sim);
/* Whether the futures were drawn by Latin hypercube sampling. */
gboolean tm_sim_latin      (const TmSim *sim);
double   tm_sim_seconds    (const TmSim *sim);
void     tm_sim_set_seconds (TmSim *sim, double seconds);
/* Set when the sheet has changed since the simulation ran. */
gboolean tm_sim_stale      (const TmSim *sim);
void     tm_sim_set_stale  (TmSim *sim, gboolean stale);

/* A cell's samples, to be filled in iteration by iteration. */
double  *tm_sim_track      (TmSim *sim, int row, int col);
gboolean tm_sim_has        (const TmSim *sim, int row, int col);
int      tm_sim_n_tracked  (const TmSim *sim);
/* The tracked cells, row by row; free with g_free. */
TmRef   *tm_sim_cells      (const TmSim *sim, int *n);

/* One sample per iteration, NaN where the cell had no number; or, if
 * sorted, only the numbers, ascending.  NULL if the cell was not kept. */
const double *tm_sim_samples (TmSim *sim, int row, int col, gboolean sorted, int *n);

/* Spearman's rank correlation between two cells across the futures:
 * how much one moves with the other.  NaN if either was not kept or does
 * not vary. */
double   tm_sim_rank_correlation (TmSim *sim, int row1, int col1, int row2, int col2);

typedef struct {
  int    iterations;
  int    valid;          /* iterations that gave a number */
  double mean, sd, se;   /* se: the standard error of the mean */
  double min, max;
  double p5, p10, p25, p50, p75, p90, p95;
} TmSimStats;

gboolean tm_sim_stats (TmSim *sim, int row, int col, TmSimStats *out);

/* Counts of samples in bins equal-width bins from lo to hi, which span
 * the samples but for the outer half percent of a long tail (those are
 * not counted); fills counts (bins of them) and returns FALSE if the cell
 * has no samples. */
gboolean tm_sim_histogram (TmSim *sim, int row, int col, int bins,
                           double *lo, double *hi, int *counts);

G_END_DECLS
