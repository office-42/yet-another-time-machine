/* tm-rng.h - random numbers, and the distributions drawn from them
 *
 * Copyright (C) 2026 The timemachine authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A simulation is only worth something if it can be run again and give
 * the same answer, so the generator is ours rather than the C library's:
 * xoshiro256** (Blackman and Vigna), seeded through splitmix64 so that a
 * seed of 1 and a seed of 2 give unrelated streams.  It is fast, passes
 * BigCrush, and its whole state is four words that can be copied.
 */

#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef struct {
  guint64 s[4];
  gboolean has_spare;   /* the polar method makes normals in pairs */
  double spare;
} TmRng;

void    tm_rng_seed    (TmRng *rng, guint64 seed);
guint64 tm_rng_next    (TmRng *rng);

/* Uniform on the open interval (0, 1): never exactly 0, so that log(u)
 * is always finite, and never exactly 1. */
double  tm_rng_uniform (TmRng *rng);
/* An integer in [lo, hi], without the bias of a plain modulo. */
gint64  tm_rng_int     (TmRng *rng, gint64 lo, gint64 hi);

double  tm_rng_normal      (TmRng *rng);                 /* N(0, 1) */
double  tm_rng_gamma       (TmRng *rng, double shape);   /* scale 1 */
double  tm_rng_beta        (TmRng *rng, double a, double b);
double  tm_rng_exponential (TmRng *rng);                 /* mean 1 */
double  tm_rng_triangular  (TmRng *rng, double lo, double mode, double hi);
gint64  tm_rng_poisson     (TmRng *rng, double lambda);
gint64  tm_rng_binomial    (TmRng *rng, gint64 n, double p);
double  tm_rng_student_t   (TmRng *rng, double df);

/* The standard normal's distribution function and its inverse.  The
 * inverse is Acklam's rational approximation polished by one Halley step,
 * which is good to about 1e-15 across (0, 1). */
double  tm_norm_cdf (double x);
double  tm_norm_inv (double p);

G_END_DECLS
