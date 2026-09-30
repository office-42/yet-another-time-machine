/* tm-types.h - cell addresses and ranges
 *
 * Copyright (C) 2026 The timemachine authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Rows and columns are zero-based inside the program and one-based and
 * lettered on screen.  The only place the two conventions meet is
 * tm_ref_name and tm_ref_parse.
 */

#pragma once

#include <glib.h>

G_BEGIN_DECLS

/* The grid is Excel 97's: 65,536 rows by 256 columns, A to IV.  A model
 * of the future rarely needs more than a column per month for a few
 * decades, and a bound this size keeps a cell key in one 64-bit word. */
#define TM_MAX_ROWS 65536
#define TM_MAX_COLS 256

typedef struct {
  int row;
  int col;
} TmRef;

typedef struct {
  int row0, col0;   /* top left, inclusive */
  int row1, col1;   /* bottom right, inclusive */
} TmRange;

/* One integer per cell, row in the high word, so that the sparse store is
 * an ordinary hash table and sorting keys sorts cells row by row. */
static inline guint64
tm_key (int row, int col)
{
  return ((guint64) (guint32) row << 32) | (guint32) col;
}

static inline int tm_key_row (guint64 key) { return (int) (key >> 32); }
static inline int tm_key_col (guint64 key) { return (int) (key & 0xffffffffu); }

/* "A", "Z", "AA", "IV" -- bijective base 26.  Writes into buf and returns
 * it. */
char    *tm_col_name  (int col, char *buf, gsize size);
/* A column's letters at the start of text; the number of characters read,
 * or 0 if there were none or they name a column past IV. */
int      tm_col_parse (const char *text, int *col);

/* "B7"; free with g_free. */
char    *tm_ref_name  (int row, int col);
/* The whole of text must be a reference, $ signs allowed: "b7", "$B$7". */
gboolean tm_ref_parse (const char *text, TmRef *out);

/* "A1:C9", or a single cell "A1" as a one-cell range. */
gboolean tm_range_parse (const char *text, TmRange *out);
char    *tm_range_name  (const TmRange *range);
/* Puts the corners in order: row0 <= row1 and col0 <= col1. */
void     tm_range_normalize (TmRange *range);

static inline int tm_range_rows  (const TmRange *r) { return r->row1 - r->row0 + 1; }
static inline int tm_range_cols  (const TmRange *r) { return r->col1 - r->col0 + 1; }
static inline gboolean
tm_range_contains (const TmRange *r, int row, int col)
{
  return row >= r->row0 && row <= r->row1 && col >= r->col0 && col <= r->col1;
}

G_END_DECLS
