/* tm-linalg.h - the little linear algebra that fitting a table needs
 *
 * Copyright (C) 2026 The timemachine authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Matrices are small and dense -- a few hundred rows of past cases, a
 * handful of columns -- and stored row by row.
 */

#pragma once

#include <glib.h>

G_BEGIN_DECLS

/* Least squares by Householder QR, which never forms X'X and so never
 * squares its condition number.  x is n rows of q columns. */
typedef struct {
  int     q;
  double *beta;     /* q coefficients */
  double *r;        /* q x q, upper triangular: X = QR */
  double  sse;      /* the residual sum of squares, over all n rows */
} TmLsq;

/* FALSE, and nothing to clear, when n < q or the columns are collinear. */
gboolean tm_lsq_fit      (const double *x, const double *y, int n, int q, TmLsq *fit);
/* x0'(X'X)^-1 x0, the leverage of a new row: how far it is from the
 * rows the fit was made from, in the fit's own terms. */
double   tm_lsq_leverage (const TmLsq *fit, const double *x0);
void     tm_lsq_clear    (TmLsq *fit);

/* a = LL', the lower triangle overwritten with L.  FALSE if a is not
 * positive definite. */
gboolean tm_cholesky       (double *a, int n);
/* Solves Lx = b, and LL'x = b, in place. */
void     tm_forward_solve  (const double *l, int n, double *b);
void     tm_cholesky_solve (const double *l, int n, double *b);

G_END_DECLS
