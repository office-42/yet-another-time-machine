/* tm-eval.c - evaluating formulas
 *
 * Copyright (C) 2026 The timemachine authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "tm-eval.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static const TmValue EMPTY = { TM_VALUE_EMPTY, { 0 } };

const TmValue *
tm_ctx_cell (TmEvalContext *ctx, int row, int col)
{
  const TmValue *v = NULL;

  if (ctx->cell != NULL)
    v = ctx->cell (ctx->data, row, col);
  return v != NULL ? v : &EMPTY;
}

/* ---- The function table ----------------------------------------------- */

static GHashTable *functions;
static const TmFunction **sorted;
static int n_sorted;

static int
compare_functions (const void *a, const void *b)
{
  const TmFunction *const *x = a, *const *y = b;
  return strcmp ((*x)->name, (*y)->name);
}

static void
add_table (GPtrArray *all, const TmFunction *table, int n)
{
  for (int i = 0; i < n; i++)
    {
      g_hash_table_insert (functions, (gpointer) table[i].name, (gpointer) &table[i]);
      g_ptr_array_add (all, (gpointer) &table[i]);
    }
}

static void
ensure_functions (void)
{
  static gsize once = 0;

  if (g_once_init_enter (&once))
    {
      GPtrArray *all = g_ptr_array_new ();

      functions = g_hash_table_new (g_str_hash, g_str_equal);
      add_table (all, tm_fn_core, tm_fn_core_count);
      add_table (all, tm_fn_stats, tm_fn_stats_count);
      add_table (all, tm_fn_random, tm_fn_random_count);
      add_table (all, tm_fn_forecast, tm_fn_forecast_count);
      add_table (all, tm_fn_sim, tm_fn_sim_count);
      n_sorted = (int) all->len;
      sorted = (const TmFunction **) g_ptr_array_free (all, FALSE);
      qsort (sorted, (size_t) n_sorted, sizeof *sorted, compare_functions);
      g_once_init_leave (&once, 1);
    }
}

const TmFunction *
tm_function_lookup (const char *name)
{
  char *upper;
  const TmFunction *fn;

  ensure_functions ();
  fn = g_hash_table_lookup (functions, name);
  if (fn != NULL)
    return fn;
  upper = g_ascii_strup (name, -1);
  fn = g_hash_table_lookup (functions, upper);
  g_free (upper);
  return fn;
}

const TmFunction *const *
tm_function_list (int *n)
{
  ensure_functions ();
  *n = n_sorted;
  return sorted;
}

/* ---- Arguments -------------------------------------------------------- */

TmValue
tm_arg_scalar (TmEvalContext *ctx, const TmArg *arg)
{
  if (!arg->is_range)
    return tm_value_copy (&arg->value);
  if (arg->range.row0 != arg->range.row1 || arg->range.col0 != arg->range.col1)
    return tm_value_error (TM_ERR_VALUE);
  return tm_value_copy (tm_ctx_cell (ctx, arg->range.row0, arg->range.col0));
}

gboolean
tm_arg_number (TmEvalContext *ctx, const TmArg *arg, double *out, TmValue *err)
{
  TmValue v = tm_arg_scalar (ctx, arg);
  TmErrorCode code;
  gboolean ok = tm_value_to_number (&v, out, &code);

  tm_value_clear (&v);
  if (!ok)
    *err = tm_value_error (code);
  return ok;
}

gboolean
tm_arg_bool (TmEvalContext *ctx, const TmArg *arg, gboolean *out, TmValue *err)
{
  TmValue v = tm_arg_scalar (ctx, arg);
  TmErrorCode code;
  gboolean ok = tm_value_to_bool (&v, out, &code);

  tm_value_clear (&v);
  if (!ok)
    *err = tm_value_error (code);
  return ok;
}

char *
tm_arg_text (TmEvalContext *ctx, const TmArg *arg, TmValue *err)
{
  TmValue v = tm_arg_scalar (ctx, arg);
  char *text;

  if (v.type == TM_VALUE_ERROR)
    {
      *err = v;
      return NULL;
    }
  text = tm_value_to_text (&v);
  tm_value_clear (&v);
  return text;
}

gboolean
tm_args_numbers (TmEvalContext *ctx, TmArg *args, int n, GArray *out, TmValue *err)
{
  for (int i = 0; i < n; i++)
    {
      TmArg *arg = &args[i];

      if (arg->missing)
        continue;
      if (!arg->is_range)
        {
          double d;
          TmErrorCode code;

          if (!tm_value_to_number (&arg->value, &d, &code))
            {
              *err = tm_value_error (code);
              return FALSE;
            }
          g_array_append_val (out, d);
          continue;
        }
      for (int r = arg->range.row0; r <= arg->range.row1; r++)
        for (int c = arg->range.col0; c <= arg->range.col1; c++)
          {
            const TmValue *v = tm_ctx_cell (ctx, r, c);

            if (v->type == TM_VALUE_NUMBER)
              g_array_append_val (out, v->as.number);
            else if (v->type == TM_VALUE_ERROR)
              {
                *err = tm_value_copy (v);
                return FALSE;
              }
          }
    }
  return TRUE;
}

const TmValue **
tm_arg_cells (TmEvalContext *ctx, const TmArg *arg, int *n)
{
  const TmValue **cells;
  int k = 0;

  if (!arg->is_range)
    {
      cells = g_new (const TmValue *, 1);
      cells[0] = &arg->value;
      *n = 1;
      return cells;
    }
  *n = tm_range_rows (&arg->range) * tm_range_cols (&arg->range);
  cells = g_new (const TmValue *, *n);
  for (int r = arg->range.row0; r <= arg->range.row1; r++)
    for (int c = arg->range.col0; c <= arg->range.col1; c++)
      cells[k++] = tm_ctx_cell (ctx, r, c);
  return cells;
}

gboolean
tm_arg_pairs (TmEvalContext *ctx, const TmArg *ys, const TmArg *xs,
              GArray *y_out, GArray *x_out, TmValue *err)
{
  int ny, nx;
  const TmValue **y = tm_arg_cells (ctx, ys, &ny);
  const TmValue **x = tm_arg_cells (ctx, xs, &nx);

  if (ny != nx)
    {
      g_free (y);
      g_free (x);
      *err = tm_value_error (TM_ERR_NA);
      return FALSE;
    }
  for (int i = 0; i < ny; i++)
    {
      if (y[i]->type == TM_VALUE_ERROR || x[i]->type == TM_VALUE_ERROR)
        {
          *err = tm_value_copy (y[i]->type == TM_VALUE_ERROR ? y[i] : x[i]);
          g_free (y);
          g_free (x);
          return FALSE;
        }
      if (y[i]->type == TM_VALUE_NUMBER && x[i]->type == TM_VALUE_NUMBER)
        {
          g_array_append_val (y_out, y[i]->as.number);
          g_array_append_val (x_out, x[i]->as.number);
        }
    }
  g_free (y);
  g_free (x);
  return TRUE;
}

/* ---- Criteria --------------------------------------------------------- */

void
tm_criteria_parse (const TmValue *criteria, TmCriteria *out)
{
  static const struct { const char *text; TmOp op; } ops[] = {
    { ">=", TM_OP_GE }, { "<=", TM_OP_LE }, { "<>", TM_OP_NE },
    { ">",  TM_OP_GT }, { "<",  TM_OP_LT }, { "=",  TM_OP_EQ },
  };

  out->op = TM_OP_EQ;
  if (criteria->type != TM_VALUE_TEXT)
    {
      out->value = tm_value_copy (criteria);
      return;
    }
  for (guint i = 0; i < G_N_ELEMENTS (ops); i++)
    if (g_str_has_prefix (criteria->as.text, ops[i].text))
      {
        out->op = ops[i].op;
        out->value = tm_value_parse_input (criteria->as.text + strlen (ops[i].text));
        return;
      }
  out->value = tm_value_parse_input (criteria->as.text);
}

gboolean
tm_criteria_match (const TmCriteria *criteria, const TmValue *value)
{
  int c;

  if (value->type == TM_VALUE_ERROR)
    return FALSE;
  /* ">5" asks a question of numbers; text is never greater than five. */
  if (criteria->value.type == TM_VALUE_NUMBER && value->type != TM_VALUE_NUMBER)
    return criteria->op == TM_OP_NE;
  c = tm_value_compare (value, &criteria->value);
  switch (criteria->op)
    {
    case TM_OP_EQ: return c == 0;
    case TM_OP_NE: return c != 0;
    case TM_OP_LT: return c < 0;
    case TM_OP_LE: return c <= 0;
    case TM_OP_GT: return c > 0;
    case TM_OP_GE: return c >= 0;
    default:       return FALSE;
    }
}

void
tm_criteria_clear (TmCriteria *criteria)
{
  tm_value_clear (&criteria->value);
}

double
tm_percentile_sorted (const double *sorted_values, int n, double p)
{
  double h, lo;
  int i;

  if (n <= 0)
    return NAN;
  if (n == 1 || p <= 0)
    return sorted_values[0];
  if (p >= 1)
    return sorted_values[n - 1];
  /* PERCENTILE.INC: rank (n - 1) p, interpolated between neighbours. */
  h = (n - 1) * p;
  lo = floor (h);
  i = (int) lo;
  if (i + 1 >= n)
    return sorted_values[n - 1];
  return sorted_values[i] + (h - lo) * (sorted_values[i + 1] - sorted_values[i]);
}

/* ---- Evaluation ------------------------------------------------------- */

static TmValue
deref (TmEvalContext *ctx, const TmNode *node)
{
  if (node->type == TM_NODE_REF)
    return tm_value_copy (tm_ctx_cell (ctx, node->as.ref.ref.row, node->as.ref.ref.col));
  if (node->type == TM_NODE_RANGE)
    {
      /* A one-cell range is a reference; anything wider has no single
       * value to give. */
      if (node->as.range.a.row == node->as.range.b.row
          && node->as.range.a.col == node->as.range.b.col)
        return tm_value_copy (tm_ctx_cell (ctx, node->as.range.a.row, node->as.range.a.col));
      return tm_value_error (TM_ERR_VALUE);
    }
  return tm_eval (ctx, node);
}

static TmValue
eval_number_op (TmOp op, double a, double b)
{
  switch (op)
    {
    case TM_OP_ADD: return tm_value_number (a + b);
    case TM_OP_SUB: return tm_value_number (a - b);
    case TM_OP_MUL: return tm_value_number (a * b);
    case TM_OP_DIV:
      if (b == 0)
        return tm_value_error (TM_ERR_DIV0);
      return tm_value_number (a / b);
    case TM_OP_POW:
      if (a == 0 && b == 0)
        return tm_value_error (TM_ERR_NUM);
      if (a == 0 && b < 0)
        return tm_value_error (TM_ERR_DIV0);
      return tm_value_number (pow (a, b));
    default:
      return tm_value_error (TM_ERR_VALUE);
    }
}

static TmValue
eval_binary (TmEvalContext *ctx, const TmNode *node)
{
  TmOp op = node->as.binary.op;
  TmValue a = deref (ctx, node->as.binary.left);
  TmValue b = deref (ctx, node->as.binary.right);
  TmValue result;

  if (a.type == TM_VALUE_ERROR)
    {
      tm_value_clear (&b);
      return a;
    }
  if (b.type == TM_VALUE_ERROR)
    {
      tm_value_clear (&a);
      return b;
    }

  if (op == TM_OP_CONCAT)
    {
      char *x = tm_value_to_text (&a), *y = tm_value_to_text (&b);

      result = tm_value_take (g_strconcat (x, y, NULL));
      g_free (x);
      g_free (y);
    }
  else if (op >= TM_OP_EQ)
    {
      int c = tm_value_compare (&a, &b);
      gboolean r = FALSE;

      switch (op)
        {
        case TM_OP_EQ: r = c == 0; break;
        case TM_OP_NE: r = c != 0; break;
        case TM_OP_LT: r = c < 0; break;
        case TM_OP_LE: r = c <= 0; break;
        case TM_OP_GT: r = c > 0; break;
        case TM_OP_GE: r = c >= 0; break;
        default: break;
        }
      result = tm_value_bool (r);
    }
  else
    {
      double x, y;
      TmErrorCode code;

      if (!tm_value_to_number (&a, &x, &code) || !tm_value_to_number (&b, &y, &code))
        result = tm_value_error (code);
      else
        result = eval_number_op (op, x, y);
    }

  tm_value_clear (&a);
  tm_value_clear (&b);
  return result;
}

static TmValue
eval_call (TmEvalContext *ctx, const TmNode *node)
{
  const TmFunction *fn = node->as.call.fn;
  int n = node->as.call.n_args;
  TmArg stack_args[8];
  TmArg *args;
  TmValue result;

  if (fn == NULL)
    return tm_value_error (TM_ERR_NAME);
  if (n < fn->min_args || (fn->max_args >= 0 && n > fn->max_args))
    return tm_value_error (TM_ERR_VALUE);
  if (fn->flags & TM_FN_RANDOM)
    ctx->random = TRUE;
  if (fn->lazy != NULL)
    return fn->lazy (ctx, node->as.call.args, n);

  args = n <= (int) G_N_ELEMENTS (stack_args) ? stack_args : g_new (TmArg, n);
  for (int i = 0; i < n; i++)
    {
      const TmNode *a = node->as.call.args[i];
      TmArg *arg = &args[i];

      memset (arg, 0, sizeof *arg);
      switch (a->type)
        {
        case TM_NODE_REF:
          arg->is_range = TRUE;
          arg->is_ref = TRUE;
          arg->range.row0 = arg->range.row1 = a->as.ref.ref.row;
          arg->range.col0 = arg->range.col1 = a->as.ref.ref.col;
          break;
        case TM_NODE_RANGE:
          arg->is_range = TRUE;
          arg->range.row0 = a->as.range.a.row;
          arg->range.col0 = a->as.range.a.col;
          arg->range.row1 = a->as.range.b.row;
          arg->range.col1 = a->as.range.b.col;
          tm_range_normalize (&arg->range);
          break;
        case TM_NODE_MISSING:
          arg->missing = TRUE;
          arg->value = tm_value_empty ();
          break;
        default:
          arg->value = tm_eval (ctx, a);
          break;
        }
    }

  result = fn->impl (ctx, args, n);

  for (int i = 0; i < n; i++)
    if (!args[i].is_range)
      tm_value_clear (&args[i].value);
  if (args != stack_args)
    g_free (args);
  return result;
}

TmValue
tm_eval (TmEvalContext *ctx, const TmNode *node)
{
  switch (node->type)
    {
    case TM_NODE_NUMBER:
      return tm_value_number (node->as.number.value);
    case TM_NODE_TEXT:
      return tm_value_text (node->as.text);
    case TM_NODE_BOOL:
      return tm_value_bool (node->as.boolean);
    case TM_NODE_ERROR:
      return tm_value_error (node->as.error);
    case TM_NODE_MISSING:
      return tm_value_empty ();
    case TM_NODE_REF:
    case TM_NODE_RANGE:
      return deref (ctx, node);
    case TM_NODE_NAME:
      return tm_value_error (TM_ERR_NAME);
    case TM_NODE_PAREN:
      return deref (ctx, node->as.arg);
    case TM_NODE_NEGATE:
    case TM_NODE_PLUS:
    case TM_NODE_PERCENT:
      {
        TmValue v = deref (ctx, node->as.arg);
        double d;
        TmErrorCode code;

        if (!tm_value_to_number (&v, &d, &code))
          {
            tm_value_clear (&v);
            return tm_value_error (code);
          }
        tm_value_clear (&v);
        if (node->type == TM_NODE_NEGATE)
          d = -d;
        else if (node->type == TM_NODE_PERCENT)
          d /= 100.0;
        return tm_value_number (d);
      }
    case TM_NODE_BINARY:
      return eval_binary (ctx, node);
    case TM_NODE_CALL:
      return eval_call (ctx, node);
    default:
      return tm_value_error (TM_ERR_VALUE);
    }
}
