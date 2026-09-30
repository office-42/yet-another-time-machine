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

/* ---- Distribution functions and their inverses ------------------------ *
 *
 * Latin hypercube sampling draws each input through its inverse
 * distribution function from a stratified uniform, so every distribution
 * needs one.  Those without a closed form are found by Newton's method,
 * kept inside a bracket by bisection. */

/* The regularised incomplete beta function I_x(a, b), and its inverse. */
double tm_beta_inc (double a, double b, double x);
double tm_beta_inv (double p, double a, double b);
/* The regularised lower incomplete gamma function P(a, x), and the x at
 * which it is p. */
double tm_gamma_p   (double a, double x);
double tm_gamma_inv (double p, double a);
/* Student's t with df degrees of freedom. */
double tm_t_cdf (double t, double df);
double tm_t_inv (double p, double df);
/* The least k with P(X <= k) >= p. */
double tm_poisson_inv  (double p, double lambda);
double tm_binomial_inv (double p, double n, double prob);
double tm_triangular_inv (double p, double lo, double mode, double hi);

G_END_DECLS
