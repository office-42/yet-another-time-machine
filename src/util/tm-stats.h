/* tm-stats.h - statistics over arrays of samples
 *
 * Copyright (C) 2026 The timemachine authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The arithmetic the function library and the simulator share.
 */

#pragma once

#include <glib.h>

G_BEGIN_DECLS

/* PERCENTILE.INC -- Hyndman and Fan's type 7 -- on a sorted array. */
double tm_percentile_sorted (const double *sorted, int n, double p);

/* The rank of each of x among the others, 0 to n - 1, ties sharing their
 * mean rank; fills rank, n of them. */
void   tm_ranks (const double *x, int n, double *rank);

/* Pearson's correlation of two arrays; of ranks, it is Spearman's.  NaN
 * when either does not vary. */
double tm_correlation (const double *x, const double *y, int n);

int    tm_compare_doubles (const void *a, const void *b);

G_END_DECLS
