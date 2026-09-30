/* tm-sheet.c - a sheet of cells, recalculated and simulated
 *
 * Copyright (C) 2026 The timemachine authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "tm-sheet.h"
#include "tm-eval.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  char     *input;       /* what was typed */
  TmNode   *formula;     /* NULL unless input starts with "=" and parses */
  gboolean  bad_formula; /* started with "=" and did not parse */
  TmValue   value;
  guint     gen;         /* the generation value belongs to */
  gboolean  computing;
  gboolean  random;
} Cell;

struct _TmSheet {
  GHashTable   *cells;       /* guint64 key -> Cell */
  GHashTable   *widths;      /* column -> width */
  guint         gen;
  int           depth;
  TmRng         rng;
  TmEvalContext ctx;
  TmSim        *sim;
  int           iterations;
  guint64       seed;
  gboolean      modified;
};

/* Formulas nest into cells nesting into formulas on the C stack; a chain
 * this deep is almost certainly a mistake, and a longer one would run the
 * stack out. */
#define MAX_DEPTH 4000

static const TmValue CIRCULAR = { TM_VALUE_ERROR, { .error = TM_ERR_CIRCULAR } };
static const TmValue EMPTY = { TM_VALUE_EMPTY, { 0 } };

static void
cell_free (gpointer p)
{
  Cell *c = p;

  g_free (c->input);
  tm_node_free (c->formula);
  tm_value_clear (&c->value);
  g_free (c);
}

static Cell *
lookup (TmSheet *sheet, int row, int col)
{
  guint64 key = tm_key (row, col);
  return g_hash_table_lookup (sheet->cells, &key);
}

static void
compute (TmSheet *sheet, Cell *c, int row, int col)
{
  gboolean outer_random = sheet->ctx.random;
  int outer_row = sheet->ctx.row, outer_col = sheet->ctx.col;
  TmValue v;

  c->computing = TRUE;
  sheet->depth++;
  sheet->ctx.random = FALSE;
  sheet->ctx.row = row;
  sheet->ctx.col = col;

  if (sheet->depth > MAX_DEPTH)
    v = tm_value_error (TM_ERR_CIRCULAR);
  else
    v = tm_eval (&sheet->ctx, c->formula);
  /* =A1 of an empty A1 shows 0, as it does in every spreadsheet. */
  if (v.type == TM_VALUE_EMPTY)
    v = tm_value_number (0);

  c->random = sheet->ctx.random;
  tm_value_clear (&c->value);
  c->value = v;
  c->gen = sheet->gen;
  c->computing = FALSE;

  sheet->depth--;
  sheet->ctx.random = outer_random || c->random;
  sheet->ctx.row = outer_row;
  sheet->ctx.col = outer_col;
}

static const TmValue *
cell_value (gpointer data, int row, int col)
{
  TmSheet *sheet = data;
  Cell *c = lookup (sheet, row, col);

  if (c == NULL)
    return NULL;
  if (c->formula != NULL)
    {
      if (c->computing)
        return &CIRCULAR;
      if (c->gen != sheet->gen)
        compute (sheet, c, row, col);
      else if (c->random)
        sheet->ctx.random = TRUE;
    }
  return &c->value;
}

static const double *
cell_samples (gpointer data, int row, int col, gboolean sorted, int *n)
{
  TmSheet *sheet = data;

  *n = 0;
  if (sheet->sim == NULL)
    return NULL;
  return tm_sim_samples (sheet->sim, row, col, sorted, n);
}

TmSheet *
tm_sheet_new (void)
{
  TmSheet *sheet = g_new0 (TmSheet, 1);

  sheet->cells = g_hash_table_new_full (g_int64_hash, g_int64_equal, g_free, cell_free);
  sheet->widths = g_hash_table_new (g_direct_hash, g_direct_equal);
  sheet->gen = 1;
  sheet->iterations = 10000;
  sheet->seed = 1;
  tm_rng_seed (&sheet->rng, g_get_real_time ());
  sheet->ctx.cell = cell_value;
  sheet->ctx.samples = cell_samples;
  sheet->ctx.data = sheet;
  sheet->ctx.rng = &sheet->rng;
  return sheet;
}

void
tm_sheet_free (TmSheet *sheet)
{
  if (sheet == NULL)
    return;
  g_hash_table_destroy (sheet->cells);
  g_hash_table_destroy (sheet->widths);
  tm_sim_free (sheet->sim);
  g_free (sheet);
}

void
tm_sheet_clear (TmSheet *sheet)
{
  g_hash_table_remove_all (sheet->cells);
  g_hash_table_remove_all (sheet->widths);
  g_clear_pointer (&sheet->sim, tm_sim_free);
  sheet->iterations = 10000;
  sheet->seed = 1;
  sheet->modified = FALSE;
}

static void
mark_changed (TmSheet *sheet)
{
  sheet->modified = TRUE;
  if (sheet->sim != NULL)
    tm_sim_set_stale (sheet->sim, TRUE);
}

void
tm_sheet_set_input (TmSheet *sheet, int row, int col, const char *input)
{
  guint64 key;
  Cell *c;

  if (row < 0 || row >= TM_MAX_ROWS || col < 0 || col >= TM_MAX_COLS)
    return;
  mark_changed (sheet);
  key = tm_key (row, col);
  if (input == NULL || *input == '\0')
    {
      g_hash_table_remove (sheet->cells, &key);
      return;
    }

  c = g_new0 (Cell, 1);
  c->input = g_strdup (input);
  if (input[0] == '=' && input[1] != '\0')
    {
      c->formula = tm_formula_parse (input + 1, NULL);
      if (c->formula == NULL)
        {
          c->bad_formula = TRUE;
          c->value = tm_value_error (TM_ERR_NAME);
        }
    }
  else
    c->value = tm_value_parse_input (input);

  {
    guint64 *k = g_new (guint64, 1);
    *k = key;
    g_hash_table_replace (sheet->cells, k, c);
  }
}

const char *
tm_sheet_get_input (TmSheet *sheet, int row, int col)
{
  Cell *c = lookup (sheet, row, col);
  return c != NULL ? c->input : NULL;
}

static int
compare_keys (const void *a, const void *b)
{
  guint64 x = *(const guint64 *) a, y = *(const guint64 *) b;
  return x < y ? -1 : x > y;
}

/* Every non-empty cell's key, row by row. */
static guint64 *
sorted_keys (TmSheet *sheet, guint *n)
{
  GHashTableIter iter;
  gpointer key;
  guint64 *keys = g_new (guint64, g_hash_table_size (sheet->cells) + 1);
  guint k = 0;

  g_hash_table_iter_init (&iter, sheet->cells);
  while (g_hash_table_iter_next (&iter, &key, NULL))
    keys[k++] = *(guint64 *) key;
  qsort (keys, k, sizeof (guint64), compare_keys);
  *n = k;
  return keys;
}

static void
evaluate_all (TmSheet *sheet)
{
  guint n;
  guint64 *keys = sorted_keys (sheet, &n);

  sheet->gen++;
  /* Row by row, which for a model laid out the usual way -- time running
   * across, cause above effect -- keeps the chain of cells waiting on
   * each other short. */
  for (guint i = 0; i < n; i++)
    cell_value (sheet, tm_key_row (keys[i]), tm_key_col (keys[i]));
  g_free (keys);
}

void
tm_sheet_recalc (TmSheet *sheet)
{
  evaluate_all (sheet);
}

void
tm_sheet_seed_draws (TmSheet *sheet, guint64 seed)
{
  tm_rng_seed (&sheet->rng, seed);
}

const TmValue *
tm_sheet_get_value (TmSheet *sheet, int row, int col)
{
  const TmValue *v = cell_value (sheet, row, col);
  return v != NULL ? v : &EMPTY;
}

char *
tm_sheet_get_display (TmSheet *sheet, int row, int col)
{
  return tm_value_to_text (tm_sheet_get_value (sheet, row, col));
}

gboolean
tm_sheet_is_formula (TmSheet *sheet, int row, int col)
{
  Cell *c = lookup (sheet, row, col);
  return c != NULL && (c->formula != NULL || c->bad_formula);
}

gboolean
tm_sheet_is_random (TmSheet *sheet, int row, int col)
{
  Cell *c = lookup (sheet, row, col);
  return c != NULL && c->random;
}

gboolean
tm_sheet_used_range (TmSheet *sheet, TmRange *out)
{
  GHashTableIter iter;
  gpointer key;
  gboolean any = FALSE;

  g_hash_table_iter_init (&iter, sheet->cells);
  while (g_hash_table_iter_next (&iter, &key, NULL))
    {
      guint64 k = *(guint64 *) key;
      int r = tm_key_row (k), c = tm_key_col (k);

      if (!any)
        {
          out->row0 = out->row1 = r;
          out->col0 = out->col1 = c;
          any = TRUE;
          continue;
        }
      out->row0 = MIN (out->row0, r);
      out->row1 = MAX (out->row1, r);
      out->col0 = MIN (out->col0, c);
      out->col1 = MAX (out->col1, c);
    }
  return any;
}

void
tm_sheet_foreach (TmSheet *sheet, TmCellFunc func, gpointer data)
{
  guint n;
  guint64 *keys = sorted_keys (sheet, &n);

  for (guint i = 0; i < n; i++)
    {
      int r = tm_key_row (keys[i]), c = tm_key_col (keys[i]);
      const char *input = tm_sheet_get_input (sheet, r, c);

      if (input != NULL)
        func (r, c, input, data);
    }
  g_free (keys);
}

void
tm_sheet_copy_range (TmSheet *sheet, const TmRange *src, int row, int col)
{
  int rows = tm_range_rows (src), cols = tm_range_cols (src);
  int drow = row - src->row0, dcol = col - src->col0;
  char **inputs = g_new0 (char *, rows * cols);

  /* Everything is read before anything is written, so that a copy onto
   * an overlapping range reads the cells as they were. */
  for (int r = 0; r < rows; r++)
    for (int c = 0; c < cols; c++)
      {
        const char *in = tm_sheet_get_input (sheet, src->row0 + r, src->col0 + c);
        inputs[r * cols + c] = in != NULL ? tm_formula_shift (in, drow, dcol) : NULL;
      }
  for (int r = 0; r < rows; r++)
    for (int c = 0; c < cols; c++)
      {
        tm_sheet_set_input (sheet, row + r, col + c, inputs[r * cols + c]);
        g_free (inputs[r * cols + c]);
      }
  g_free (inputs);
}

void
tm_sheet_fill_down (TmSheet *sheet, const TmRange *range)
{
  for (int c = range->col0; c <= range->col1; c++)
    {
      const char *in = tm_sheet_get_input (sheet, range->row0, c);
      char *src = g_strdup (in);

      for (int r = range->row0 + 1; r <= range->row1; r++)
        {
          char *shifted = src != NULL ? tm_formula_shift (src, r - range->row0, 0) : NULL;
          tm_sheet_set_input (sheet, r, c, shifted);
          g_free (shifted);
        }
      g_free (src);
    }
}

void
tm_sheet_fill_right (TmSheet *sheet, const TmRange *range)
{
  for (int r = range->row0; r <= range->row1; r++)
    {
      const char *in = tm_sheet_get_input (sheet, r, range->col0);
      char *src = g_strdup (in);

      for (int c = range->col0 + 1; c <= range->col1; c++)
        {
          char *shifted = src != NULL ? tm_formula_shift (src, 0, c - range->col0) : NULL;
          tm_sheet_set_input (sheet, r, c, shifted);
          g_free (shifted);
        }
      g_free (src);
    }
}

void
tm_sheet_clear_range (TmSheet *sheet, const TmRange *range)
{
  for (int r = range->row0; r <= range->row1; r++)
    for (int c = range->col0; c <= range->col1; c++)
      if (lookup (sheet, r, c) != NULL)
        tm_sheet_set_input (sheet, r, c, NULL);
}

int
tm_sheet_col_width (TmSheet *sheet, int col)
{
  int w = GPOINTER_TO_INT (g_hash_table_lookup (sheet->widths, GINT_TO_POINTER (col + 1)));
  return w > 0 ? w : TM_DEFAULT_COL_WIDTH;
}

void
tm_sheet_set_col_width (TmSheet *sheet, int col, int width)
{
  width = CLAMP (width, 8, 1000);
  if (width == TM_DEFAULT_COL_WIDTH)
    g_hash_table_remove (sheet->widths, GINT_TO_POINTER (col + 1));
  else
    g_hash_table_insert (sheet->widths, GINT_TO_POINTER (col + 1), GINT_TO_POINTER (width));
  sheet->modified = TRUE;
}

int      tm_sheet_iterations (TmSheet *sheet) { return sheet->iterations; }
guint64  tm_sheet_seed (TmSheet *sheet) { return sheet->seed; }
TmSim   *tm_sheet_get_sim (TmSheet *sheet) { return sheet->sim; }
gboolean tm_sheet_modified (TmSheet *sheet) { return sheet->modified; }
void     tm_sheet_set_modified (TmSheet *sheet, gboolean modified) { sheet->modified = modified; }

void
tm_sheet_set_iterations (TmSheet *sheet, int iterations)
{
  sheet->iterations = CLAMP (iterations, 10, 1000000);
  sheet->modified = TRUE;
}

void
tm_sheet_set_seed (TmSheet *sheet, guint64 seed)
{
  sheet->seed = seed;
  sheet->modified = TRUE;
}

/* ---- Simulation ------------------------------------------------------- */

static void
add_range_cells (const TmRange *range, gpointer data)
{
  GHashTable *set = data;

  /* A SIM.* function names a cell; a range here would be a mistake the
   * function reports, and a huge one should not cost memory. */
  if (tm_range_rows (range) * tm_range_cols (range) > 1)
    return;
  g_hash_table_add (set, GSIZE_TO_POINTER ((gsize) tm_key (range->row0, range->col0) + 1));
}

/* A sample is a number, or TRUE as 1 and FALSE as 0 -- so that the mean
 * of =B9<0 across the futures is the chance of a loss -- or NaN. */
static double
sample_of (const TmValue *v)
{
  if (v->type == TM_VALUE_NUMBER)
    return v->as.number;
  if (v->type == TM_VALUE_BOOL)
    return v->as.boolean ? 1 : 0;
  return NAN;
}

/* Keeping samples for every uncertain cell costs a double per cell per
 * iteration; past this many the sheet keeps only the cells SIM.* names. */
#define SAMPLE_BUDGET (32 * 1000 * 1000)

gboolean
tm_sheet_simulate (TmSheet *sheet, TmSimProgress progress, gpointer data)
{
  int iterations = sheet->iterations;
  GHashTable *wanted = g_hash_table_new (g_direct_hash, g_direct_equal);
  GArray *targets = g_array_new (FALSE, FALSE, sizeof (TmRef));
  double **tracks;
  guint n_keys;
  guint64 *keys;
  gint64 start = g_get_monotonic_time ();
  TmSim *sim = tm_sim_new (iterations, sheet->seed);
  TmRng saved = sheet->rng;
  gboolean done = TRUE;
  guint n_random = 0;

  /* The cells SIM.* functions ask about. */
  keys = sorted_keys (sheet, &n_keys);
  for (guint i = 0; i < n_keys; i++)
    {
      Cell *c = g_hash_table_lookup (sheet->cells, &keys[i]);
      if (c->formula != NULL)
        tm_formula_foreach_range (c->formula, "SIM.", add_range_cells, wanted);
    }

  /* The first iteration works out the whole sheet, which is how the
   * uncertain cells make themselves known. */
  tm_rng_seed (&sheet->rng, sheet->seed);
  evaluate_all (sheet);
  for (guint i = 0; i < n_keys; i++)
    {
      Cell *c = g_hash_table_lookup (sheet->cells, &keys[i]);
      if (c->formula != NULL && c->random)
        n_random++;
    }
  for (guint i = 0; i < n_keys; i++)
    {
      Cell *c = g_hash_table_lookup (sheet->cells, &keys[i]);
      gboolean asked = g_hash_table_contains (wanted, GSIZE_TO_POINTER ((gsize) keys[i] + 1));

      if (c->formula == NULL)
        continue;
      if (asked || (c->random && (gint64) n_random * iterations <= SAMPLE_BUDGET))
        {
          TmRef ref = { tm_key_row (keys[i]), tm_key_col (keys[i]) };
          g_array_append_val (targets, ref);
        }
    }

  tracks = g_new (double *, MAX (targets->len, 1));
  for (guint t = 0; t < targets->len; t++)
    {
      TmRef *ref = &g_array_index (targets, TmRef, t);
      tracks[t] = tm_sim_track (sim, ref->row, ref->col);
      tracks[t][0] = sample_of (tm_sheet_get_value (sheet, ref->row, ref->col));
    }

  for (int it = 1; it < iterations; it++)
    {
      sheet->gen++;
      for (guint t = 0; t < targets->len; t++)
        {
          TmRef *ref = &g_array_index (targets, TmRef, t);
          const TmValue *v = cell_value (sheet, ref->row, ref->col);
          tracks[t][it] = v != NULL ? sample_of (v) : NAN;
        }
      if (progress != NULL && (it % 250 == 0) && !progress (it, iterations, data))
        {
          done = FALSE;
          break;
        }
    }

  g_free (tracks);
  g_free (keys);
  g_array_free (targets, TRUE);
  g_hash_table_destroy (wanted);

  if (!done)
    {
      tm_sim_free (sim);
      sheet->rng = saved;
      evaluate_all (sheet);
      return FALSE;
    }

  tm_sim_set_seconds (sim, (g_get_monotonic_time () - start) / 1e6);
  tm_sim_free (sheet->sim);
  sheet->sim = sim;
  /* The random stream carries on from where the simulation left it, so
   * the draws shown in the grid afterwards are fresh ones. */
  evaluate_all (sheet);
  if (progress != NULL)
    progress (iterations, iterations, data);
  return TRUE;
}
