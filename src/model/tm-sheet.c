/* tm-sheet.c - a sheet of cells, recalculated and simulated
 *
 * Copyright (C) 2026 The timemachine authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "tm-sheet.h"
#include "tm-eval.h"
#include "tm-numfmt.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  char     *input;       /* what was typed */
  TmNode   *formula;     /* NULL unless input starts with "=" and parses */
  gboolean  bad_formula; /* started with "=" and did not parse */
  gboolean  draws;       /* the formula itself calls a random function */
  TmValue   value;
  guint     gen;         /* the generation value belongs to */
  gboolean  computing;
  gboolean  random;      /* the value is a draw, here or upstream */
  guint8    may_draw;    /* during a simulation: 0 unknown, 1 working it
                          * out, 2 could be random, 3 cannot be */
} Cell;

/* One change, for undo: what a cell held (or its format) before. */
typedef struct {
  int       row, col;
  gboolean  is_format;
  char     *old;
} Change;

struct _TmSheet {
  GHashTable   *cells;       /* guint64 key -> Cell */
  GHashTable   *formats;     /* guint64 key -> format code */
  GHashTable   *widths;      /* column + 1 -> width */
  guint         gen;
  int           depth;
  TmEvalContext ctx;
  TmSim        *sim;
  int           iterations;
  guint64       seed;
  TmSampling    sampling;
  gboolean      modified;

  /* While a Latin hypercube simulation runs: for each cell and each
   * random draw within its formula (its slot), the order in which the
   * futures visit the strata. */
  GHashTable   *strata;      /* guint64 (cell key * 64 + slot) -> int[] */
  int           slot;
  /* While a simulation runs past its first future: cells that cannot be
   * random keep the value the first gave them. */
  gboolean      freezing;

  /* Which draw every random cell makes: a cell's stream is seeded from
   * these and from where the cell is, and from nothing else. */
  guint64       stream_seed;
  guint64       stream_index;
  guint64       draw_seed;   /* ordinary recalculation's */
  guint64       draw_index;

  GPtrArray    *undo;        /* of GArray of Change, oldest first */
  GPtrArray    *redo;
  GArray       *group;       /* the group being recorded */
  int           group_depth;
  gboolean      replaying;

  /* For a random function met outside any cell's own stream, which a
   * well-formed sheet never does. */
  TmRng         fallback;
};

/* Formulas nest into cells nesting into formulas on the C stack; a chain
 * this deep is almost certainly a mistake, and a longer one would run the
 * stack out. */
#define MAX_DEPTH 4000
#define MAX_UNDO  500

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

static void
group_free (gpointer p)
{
  GArray *group = p;

  for (guint i = 0; i < group->len; i++)
    g_free (g_array_index (group, Change, i).old);
  g_array_free (group, TRUE);
}

static Cell *
lookup (TmSheet *sheet, int row, int col)
{
  guint64 key = tm_key (row, col);
  return g_hash_table_lookup (sheet->cells, &key);
}

static guint64 *
new_key (int row, int col)
{
  guint64 *k = g_new (guint64, 1);

  *k = tm_key (row, col);
  return k;
}

/* A cell's own random stream for one draw: a hash of the seed, the cell
 * and the draw's number.  Because nothing else goes in, the cell draws
 * the same whatever else in the sheet changes -- and two versions of a
 * model run with the same seed see the same futures (common random
 * numbers), so that their difference is the change and not the luck. */
static void
seed_stream (TmSheet *sheet, TmRng *rng, int row, int col)
{
  guint64 h = sheet->stream_seed;

  h ^= (tm_key (row, col) + 1) * 0x9e3779b97f4a7c15ULL;
  h = (h ^ (h >> 31)) * 0xbf58476d1ce4e5b9ULL;
  h ^= (sheet->stream_index + 1) * 0xd6e8feb86659fd93ULL;
  h = (h ^ (h >> 29)) * 0x94d049bb133111ebULL;
  tm_rng_seed (rng, h ^ (h >> 32));
}

static void
compute (TmSheet *sheet, Cell *c, int row, int col)
{
  gboolean outer_random = sheet->ctx.random;
  int outer_row = sheet->ctx.row, outer_col = sheet->ctx.col;
  int outer_slot = sheet->slot;
  TmRng *outer_rng = sheet->ctx.rng;
  TmRng stream;
  TmValue v;

  c->computing = TRUE;
  sheet->depth++;
  sheet->ctx.random = FALSE;
  sheet->ctx.row = row;
  sheet->ctx.col = col;
  sheet->slot = 0;
  /* The stream lives on this frame, so that a cell worked out in the
   * middle of another's formula does not disturb the other's draws. */
  if (c->draws)
    {
      seed_stream (sheet, &stream, row, col);
      sheet->ctx.rng = &stream;
    }

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
  sheet->slot = outer_slot;
  sheet->ctx.rng = outer_rng;
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
        {
          if (sheet->freezing && c->may_draw == 3)
            c->gen = sheet->gen;
          else
            compute (sheet, c, row, col);
        }
      else if (c->random)
        sheet->ctx.random = TRUE;
    }
  return &c->value;
}

/* A stratified uniform for draw number slot of the cell being worked out,
 * in future stream_index: stratum perm[future] of the iterations, and a
 * point within it from the cell's own stream. */
static gboolean
cell_stratified (gpointer data, double *u)
{
  TmSheet *sheet = data;
  int n = sheet->iterations;
  guint64 key;
  int *perm;

  if (sheet->strata == NULL || sheet->slot >= 64 || sheet->stream_index >= (guint64) n)
    return FALSE;
  key = tm_key (sheet->ctx.row, sheet->ctx.col) * 64 + (guint64) sheet->slot++;
  perm = g_hash_table_lookup (sheet->strata, &key);
  if (perm == NULL)
    {
      TmRng rng;
      guint64 *k = g_new (guint64, 1);

      /* A shuffle of 0..n-1 (Fisher and Yates), seeded from the seed and
       * the cell, so that each input's strata pair up with the others'
       * at random but the same way every run. */
      tm_rng_seed (&rng, sheet->seed ^ (key * 0x9e3779b97f4a7c15ULL));
      perm = g_new (int, n);
      for (int i = 0; i < n; i++)
        perm[i] = i;
      for (int i = n - 1; i > 0; i--)
        {
          int j = (int) tm_rng_int (&rng, 0, i), t = perm[i];
          perm[i] = perm[j];
          perm[j] = t;
        }
      *k = key;
      g_hash_table_insert (sheet->strata, k, perm);
    }
  *u = (perm[sheet->stream_index] + tm_rng_uniform (sheet->ctx.rng)) / n;
  return TRUE;
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
  sheet->formats = g_hash_table_new_full (g_int64_hash, g_int64_equal, g_free, g_free);
  sheet->widths = g_hash_table_new (g_direct_hash, g_direct_equal);
  sheet->undo = g_ptr_array_new_with_free_func (group_free);
  sheet->redo = g_ptr_array_new_with_free_func (group_free);
  sheet->gen = 1;
  sheet->iterations = 10000;
  sheet->seed = 1;
  sheet->draw_seed = (guint64) g_get_real_time ();
  sheet->stream_seed = sheet->draw_seed;
  sheet->ctx.cell = cell_value;
  sheet->ctx.samples = cell_samples;
  sheet->ctx.stratified = cell_stratified;
  sheet->ctx.data = sheet;
  tm_rng_seed (&sheet->fallback, 1);
  sheet->ctx.rng = &sheet->fallback;
  return sheet;
}

void
tm_sheet_free (TmSheet *sheet)
{
  if (sheet == NULL)
    return;
  g_hash_table_destroy (sheet->cells);
  g_hash_table_destroy (sheet->formats);
  g_hash_table_destroy (sheet->widths);
  g_ptr_array_free (sheet->undo, TRUE);
  g_ptr_array_free (sheet->redo, TRUE);
  if (sheet->group != NULL)
    group_free (sheet->group);
  tm_sim_free (sheet->sim);
  g_free (sheet);
}

void
tm_sheet_clear (TmSheet *sheet)
{
  g_hash_table_remove_all (sheet->cells);
  g_hash_table_remove_all (sheet->formats);
  g_hash_table_remove_all (sheet->widths);
  g_ptr_array_set_size (sheet->undo, 0);
  g_ptr_array_set_size (sheet->redo, 0);
  g_clear_pointer (&sheet->sim, tm_sim_free);
  sheet->iterations = 10000;
  sheet->seed = 1;
  sheet->sampling = TM_SAMPLING_MONTE_CARLO;
  sheet->modified = FALSE;
}

static void
mark_changed (TmSheet *sheet)
{
  sheet->modified = TRUE;
  if (sheet->sim != NULL)
    tm_sim_set_stale (sheet->sim, TRUE);
}

/* ---- Undo ------------------------------------------------------------- */

void
tm_sheet_begin_undo (TmSheet *sheet)
{
  if (sheet->group_depth++ == 0)
    sheet->group = g_array_new (FALSE, FALSE, sizeof (Change));
}

void
tm_sheet_end_undo (TmSheet *sheet)
{
  GArray *group;

  if (sheet->group_depth == 0 || --sheet->group_depth > 0)
    return;
  group = g_steal_pointer (&sheet->group);
  if (group->len == 0)
    {
      group_free (group);
      return;
    }
  g_ptr_array_add (sheet->undo, group);
  if (sheet->undo->len > MAX_UNDO)
    g_ptr_array_remove_index (sheet->undo, 0);
  g_ptr_array_set_size (sheet->redo, 0);
}

static void
record (TmSheet *sheet, int row, int col, gboolean is_format, const char *old)
{
  Change change = { row, col, is_format, NULL };
  gboolean alone = sheet->group_depth == 0;

  if (sheet->replaying)
    return;
  change.old = g_strdup (old);
  if (alone)
    tm_sheet_begin_undo (sheet);
  g_array_append_val (sheet->group, change);
  if (alone)
    tm_sheet_end_undo (sheet);
}

static void set_input (TmSheet *sheet, int row, int col, const char *input);
static void set_format (TmSheet *sheet, int row, int col, const char *format);

/* Applies a group backwards, returning the group that would put it
 * back. */
static GArray *
replay (TmSheet *sheet, GArray *group)
{
  GArray *inverse = g_array_new (FALSE, FALSE, sizeof (Change));

  sheet->replaying = TRUE;
  for (int i = (int) group->len - 1; i >= 0; i--)
    {
      Change *c = &g_array_index (group, Change, i);
      Change back = { c->row, c->col, c->is_format, NULL };

      if (c->is_format)
        {
          back.old = g_strdup (tm_sheet_get_format (sheet, c->row, c->col));
          set_format (sheet, c->row, c->col, c->old);
        }
      else
        {
          back.old = g_strdup (tm_sheet_get_input (sheet, c->row, c->col));
          set_input (sheet, c->row, c->col, c->old);
        }
      g_array_append_val (inverse, back);
    }
  sheet->replaying = FALSE;
  return inverse;
}

static gboolean
undo_redo (TmSheet *sheet, GPtrArray *from, GPtrArray *to)
{
  GArray *group;

  if (from->len == 0 || sheet->group_depth > 0)
    return FALSE;
  group = g_ptr_array_steal_index (from, from->len - 1);
  g_ptr_array_add (to, replay (sheet, group));
  group_free (group);
  mark_changed (sheet);
  return TRUE;
}

gboolean tm_sheet_undo (TmSheet *sheet) { return undo_redo (sheet, sheet->undo, sheet->redo); }
gboolean tm_sheet_redo (TmSheet *sheet) { return undo_redo (sheet, sheet->redo, sheet->undo); }
gboolean tm_sheet_can_undo (TmSheet *sheet) { return sheet->undo->len > 0; }

void
tm_sheet_forget_undo (TmSheet *sheet)
{
  g_ptr_array_set_size (sheet->undo, 0);
  g_ptr_array_set_size (sheet->redo, 0);
}
gboolean tm_sheet_can_redo (TmSheet *sheet) { return sheet->redo->len > 0; }

/* ---- Cells ------------------------------------------------------------ */

static void
set_input (TmSheet *sheet, int row, int col, const char *input)
{
  guint64 key = tm_key (row, col);
  Cell *c;

  mark_changed (sheet);
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
      else
        c->draws = tm_formula_calls (c->formula, TM_FN_RANDOM);
    }
  else
    c->value = tm_value_parse_input (input);
  g_hash_table_replace (sheet->cells, new_key (row, col), c);
}

/* "12%" typed into a cell with no format of its own is shown as a
 * percentage, as Excel does. */
static void
format_from_input (TmSheet *sheet, int row, int col, const char *input)
{
  TmValue v;

  if (input == NULL || input[0] == '=' || strchr (input, '%') == NULL
      || tm_sheet_get_format (sheet, row, col) != NULL)
    return;
  v = tm_value_parse_input (input);
  if (v.type == TM_VALUE_NUMBER)
    {
      const char *dot = strchr (input, '.');
      int decimals = 0;

      if (dot != NULL)
        while (g_ascii_isdigit (dot[decimals + 1]))
          decimals++;
      tm_sheet_set_format (sheet, row, col,
                           decimals == 0 ? "0%" : decimals == 1 ? "0.0%" : "0.00%");
    }
  tm_value_clear (&v);
}

void
tm_sheet_set_input (TmSheet *sheet, int row, int col, const char *input)
{
  const char *old;

  if (row < 0 || row >= TM_MAX_ROWS || col < 0 || col >= TM_MAX_COLS)
    return;
  if (input != NULL && *input == '\0')
    input = NULL;
  old = tm_sheet_get_input (sheet, row, col);
  if (g_strcmp0 (old, input) == 0)
    return;
  tm_sheet_begin_undo (sheet);
  record (sheet, row, col, FALSE, old);
  format_from_input (sheet, row, col, input);
  set_input (sheet, row, col, input);
  tm_sheet_end_undo (sheet);
}

const char *
tm_sheet_get_input (TmSheet *sheet, int row, int col)
{
  Cell *c = lookup (sheet, row, col);
  return c != NULL ? c->input : NULL;
}

static void
set_format (TmSheet *sheet, int row, int col, const char *format)
{
  sheet->modified = TRUE;
  if (format == NULL || *format == '\0' || g_ascii_strcasecmp (format, "General") == 0)
    {
      guint64 key = tm_key (row, col);
      g_hash_table_remove (sheet->formats, &key);
    }
  else
    g_hash_table_replace (sheet->formats, new_key (row, col), g_strdup (format));
}

const char *
tm_sheet_get_format (TmSheet *sheet, int row, int col)
{
  guint64 key = tm_key (row, col);
  return g_hash_table_lookup (sheet->formats, &key);
}

void
tm_sheet_set_format (TmSheet *sheet, int row, int col, const char *format)
{
  const char *old;

  if (row < 0 || row >= TM_MAX_ROWS || col < 0 || col >= TM_MAX_COLS)
    return;
  if (format != NULL && (*format == '\0' || g_ascii_strcasecmp (format, "General") == 0))
    format = NULL;
  old = tm_sheet_get_format (sheet, row, col);
  if (g_strcmp0 (old, format) == 0)
    return;
  record (sheet, row, col, TRUE, old);
  set_format (sheet, row, col, format);
}

void
tm_sheet_format_range (TmSheet *sheet, const TmRange *range, const char *format)
{
  tm_sheet_begin_undo (sheet);
  for (int r = range->row0; r <= range->row1; r++)
    for (int c = range->col0; c <= range->col1; c++)
      tm_sheet_set_format (sheet, r, c, format);
  tm_sheet_end_undo (sheet);
}

static int
compare_keys (const void *a, const void *b)
{
  guint64 x = *(const guint64 *) a, y = *(const guint64 *) b;
  return x < y ? -1 : x > y;
}

/* Every key of a table, row by row. */
static guint64 *
sorted_keys (GHashTable *table, guint *n)
{
  GHashTableIter iter;
  gpointer key;
  guint64 *keys = g_new (guint64, g_hash_table_size (table) + 1);
  guint k = 0;

  g_hash_table_iter_init (&iter, table);
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
  guint64 *keys = sorted_keys (sheet->cells, &n);

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
  sheet->stream_seed = sheet->draw_seed;
  sheet->stream_index = sheet->draw_index;
  evaluate_all (sheet);
}

void
tm_sheet_redraw (TmSheet *sheet)
{
  sheet->draw_index++;
  tm_sheet_recalc (sheet);
}

void
tm_sheet_seed_draws (TmSheet *sheet, guint64 seed)
{
  sheet->draw_seed = seed;
  sheet->draw_index = 0;
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
  const TmValue *v = tm_sheet_get_value (sheet, row, col);
  const char *format;

  if (v->type == TM_VALUE_NUMBER && (format = tm_sheet_get_format (sheet, row, col)) != NULL)
    return tm_format_number (v->as.number, format);
  return tm_value_to_text (v);
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
tm_sheet_is_source (TmSheet *sheet, int row, int col)
{
  Cell *c = lookup (sheet, row, col);
  return c != NULL && c->draws;
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
  guint64 *keys = sorted_keys (sheet->cells, &n);

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
tm_sheet_foreach_format (TmSheet *sheet, TmCellFunc func, gpointer data)
{
  guint n;
  guint64 *keys = sorted_keys (sheet->formats, &n);

  for (guint i = 0; i < n; i++)
    {
      int r = tm_key_row (keys[i]), c = tm_key_col (keys[i]);
      func (r, c, tm_sheet_get_format (sheet, r, c), data);
    }
  g_free (keys);
}

void
tm_sheet_copy_range (TmSheet *sheet, const TmRange *src, int row, int col)
{
  int rows = tm_range_rows (src), cols = tm_range_cols (src);
  int drow = row - src->row0, dcol = col - src->col0;
  char **inputs = g_new0 (char *, rows * cols);
  char **formats = g_new0 (char *, rows * cols);

  /* Everything is read before anything is written, so that a copy onto
   * an overlapping range reads the cells as they were. */
  for (int r = 0; r < rows; r++)
    for (int c = 0; c < cols; c++)
      {
        const char *in = tm_sheet_get_input (sheet, src->row0 + r, src->col0 + c);
        inputs[r * cols + c] = in != NULL ? tm_formula_shift (in, drow, dcol) : NULL;
        formats[r * cols + c] = g_strdup (tm_sheet_get_format (sheet, src->row0 + r, src->col0 + c));
      }
  tm_sheet_begin_undo (sheet);
  for (int r = 0; r < rows; r++)
    for (int c = 0; c < cols; c++)
      {
        tm_sheet_set_format (sheet, row + r, col + c, formats[r * cols + c]);
        tm_sheet_set_input (sheet, row + r, col + c, inputs[r * cols + c]);
        g_free (inputs[r * cols + c]);
        g_free (formats[r * cols + c]);
      }
  tm_sheet_end_undo (sheet);
  g_free (inputs);
  g_free (formats);
}

static void
fill (TmSheet *sheet, const TmRange *range, gboolean down)
{
  int lines = down ? tm_range_cols (range) : tm_range_rows (range);

  tm_sheet_begin_undo (sheet);
  for (int i = 0; i < lines; i++)
    {
      int r0 = down ? range->row0 : range->row0 + i;
      int c0 = down ? range->col0 + i : range->col0;
      char *src = g_strdup (tm_sheet_get_input (sheet, r0, c0));
      char *format = g_strdup (tm_sheet_get_format (sheet, r0, c0));
      int count = down ? tm_range_rows (range) : tm_range_cols (range);

      for (int k = 1; k < count; k++)
        {
          int r = down ? r0 + k : r0, c = down ? c0 : c0 + k;
          char *shifted = src != NULL ? tm_formula_shift (src, r - r0, c - c0) : NULL;

          tm_sheet_set_format (sheet, r, c, format);
          tm_sheet_set_input (sheet, r, c, shifted);
          g_free (shifted);
        }
      g_free (src);
      g_free (format);
    }
  tm_sheet_end_undo (sheet);
}

void tm_sheet_fill_down (TmSheet *sheet, const TmRange *range) { fill (sheet, range, TRUE); }
void tm_sheet_fill_right (TmSheet *sheet, const TmRange *range) { fill (sheet, range, FALSE); }

void
tm_sheet_clear_range (TmSheet *sheet, const TmRange *range)
{
  tm_sheet_begin_undo (sheet);
  for (int r = range->row0; r <= range->row1; r++)
    for (int c = range->col0; c <= range->col1; c++)
      if (lookup (sheet, r, c) != NULL)
        tm_sheet_set_input (sheet, r, c, NULL);
  tm_sheet_end_undo (sheet);
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
TmSampling tm_sheet_sampling (TmSheet *sheet) { return sheet->sampling; }

void
tm_sheet_set_sampling (TmSheet *sheet, TmSampling sampling)
{
  if (sampling != sheet->sampling)
    sheet->modified = TRUE;
  sheet->sampling = sampling;
}
guint64  tm_sheet_seed (TmSheet *sheet) { return sheet->seed; }
TmSim   *tm_sheet_get_sim (TmSheet *sheet) { return sheet->sim; }
gboolean tm_sheet_modified (TmSheet *sheet) { return sheet->modified; }
void     tm_sheet_set_modified (TmSheet *sheet, gboolean modified) { sheet->modified = modified; }

void
tm_sheet_set_iterations (TmSheet *sheet, int iterations)
{
  iterations = CLAMP (iterations, 10, 1000000);
  if (iterations != sheet->iterations)
    sheet->modified = TRUE;
  sheet->iterations = iterations;
}

void
tm_sheet_set_seed (TmSheet *sheet, guint64 seed)
{
  if (seed != sheet->seed)
    sheet->modified = TRUE;
  sheet->seed = seed;
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

/* Whether a cell could ever be random: it draws itself, or a cell it
 * names could be.  Worked out from the formulas, not from one run of
 * them, because a branch of an IF not taken in one future can be taken in
 * the next.  Depth first, remembering answers; a cycle counts as not
 * random (it is #CIRC! in any case). */
static gboolean may_draw (TmSheet *sheet, Cell *c);

typedef struct {
  TmSheet *sheet;
  gboolean found;
} MayDraw;

static void
may_draw_range (const TmRange *range, gpointer data)
{
  MayDraw *m = data;
  gint64 area = (gint64) tm_range_rows (range) * tm_range_cols (range);

  if (m->found)
    return;
  if (area <= 4096)
    {
      for (int r = range->row0; r <= range->row1 && !m->found; r++)
        for (int col = range->col0; col <= range->col1 && !m->found; col++)
          {
            Cell *c = lookup (m->sheet, r, col);
            if (c != NULL && may_draw (m->sheet, c))
              m->found = TRUE;
          }
      return;
    }
  {
    GHashTableIter iter;
    gpointer key, value;

    g_hash_table_iter_init (&iter, m->sheet->cells);
    while (!m->found && g_hash_table_iter_next (&iter, &key, &value))
      {
        guint64 k = *(guint64 *) key;
        if (tm_range_contains (range, tm_key_row (k), tm_key_col (k)) && may_draw (m->sheet, value))
          m->found = TRUE;
      }
  }
}

static gboolean
may_draw (TmSheet *sheet, Cell *c)
{
  MayDraw m = { sheet, FALSE };

  if (c->may_draw >= 2)
    return c->may_draw == 2;
  if (c->may_draw == 1 || c->formula == NULL)
    return FALSE;
  c->may_draw = 1;
  if (c->draws)
    m.found = TRUE;
  else
    tm_formula_foreach_range (c->formula, NULL, may_draw_range, &m);
  c->may_draw = m.found ? 2 : 3;
  return m.found;
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
  gboolean latin = sheet->sampling == TM_SAMPLING_LATIN_HYPERCUBE;
  TmSim *sim = tm_sim_new (iterations, sheet->seed, latin);
  gboolean done = TRUE;
  guint n_random = 0;

  /* The cells SIM.* functions ask about. */
  keys = sorted_keys (sheet->cells, &n_keys);
  for (guint i = 0; i < n_keys; i++)
    {
      Cell *c = g_hash_table_lookup (sheet->cells, &keys[i]);
      if (c->formula != NULL)
        tm_formula_foreach_range (c->formula, "SIM.", add_range_cells, wanted);
    }

  /* Future i is draw i of every cell's stream under the simulation's
   * seed.  The first works out the whole sheet, which is how the
   * uncertain cells make themselves known. */
  sheet->stream_seed = sheet->seed ^ 0x5851f42d4c957f2dULL;
  sheet->stream_index = 0;
  if (latin)
    sheet->strata = g_hash_table_new_full (g_int64_hash, g_int64_equal, g_free, g_free);
  for (guint i = 0; i < n_keys; i++)
    ((Cell *) g_hash_table_lookup (sheet->cells, &keys[i]))->may_draw = 0;
  for (guint i = 0; i < n_keys; i++)
    may_draw (sheet, g_hash_table_lookup (sheet->cells, &keys[i]));
  evaluate_all (sheet);
  sheet->freezing = TRUE;
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
      sheet->stream_index = (guint64) it;
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
  g_clear_pointer (&sheet->strata, g_hash_table_destroy);
  sheet->freezing = FALSE;

  if (!done)
    {
      tm_sim_free (sim);
      tm_sheet_recalc (sheet);
      return FALSE;
    }

  tm_sim_set_seconds (sim, (g_get_monotonic_time () - start) / 1e6);
  tm_sim_free (sheet->sim);
  sheet->sim = sim;
  /* Back to the draws the grid was showing, now with the results for the
   * SIM.* functions to read. */
  tm_sheet_recalc (sheet);
  if (progress != NULL)
    progress (iterations, iterations, data);
  return TRUE;
}
