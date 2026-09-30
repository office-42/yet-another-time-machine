/* tm-fn-core.c - arithmetic, logic, text and money
 *
 * Copyright (C) 2026 The timemachine authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The ordinary functions every spreadsheet has, so that a model of the
 * future can be written in the language a model of the present already
 * is.  They behave as Excel's do.
 */

#include "tm-fn-private.h"

#include <string.h>

int
tm_compare_doubles (const void *a, const void *b)
{
  double x = *(const double *) a, y = *(const double *) b;
  return x < y ? -1 : x > y;
}

/* ---- Arithmetic ------------------------------------------------------- */

static TmValue
fn_sum (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  GArray *a = collect (ctx, args, n, &err);
  double s = 0;

  if (a == NULL)
    return err;
  for (guint i = 0; i < a->len; i++)
    s += g_array_index (a, double, i);
  g_array_free (a, TRUE);
  return tm_value_number (s);
}

static TmValue
fn_product (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  GArray *a = collect (ctx, args, n, &err);
  double p = 1;

  if (a == NULL)
    return err;
  for (guint i = 0; i < a->len; i++)
    p *= g_array_index (a, double, i);
  if (a->len == 0)
    p = 0;
  g_array_free (a, TRUE);
  return tm_value_number (p);
}

static TmValue
fn_sumproduct (TmEvalContext *ctx, TmArg *args, int n)
{
  const TmValue **cells[16];
  int count = -1, len;
  double total = 0;

  if (n > 16)
    return tm_value_error (TM_ERR_VALUE);
  for (int i = 0; i < n; i++)
    {
      cells[i] = tm_arg_cells (ctx, &args[i], &len);
      if (count >= 0 && len != count)
        {
          for (int j = 0; j <= i; j++)
            g_free (cells[j]);
          return tm_value_error (TM_ERR_VALUE);
        }
      count = len;
    }
  for (int k = 0; k < count; k++)
    {
      double p = 1;

      for (int i = 0; i < n; i++)
        {
          const TmValue *v = cells[i][k];

          if (v->type == TM_VALUE_ERROR)
            {
              TmValue e = tm_value_copy (v);
              for (int j = 0; j < n; j++)
                g_free (cells[j]);
              return e;
            }
          p *= v->type == TM_VALUE_NUMBER ? v->as.number : 0;
        }
      total += p;
    }
  for (int i = 0; i < n; i++)
    g_free (cells[i]);
  return tm_value_number (total);
}

typedef double (*Unary) (double);

static TmValue
unary (TmEvalContext *ctx, TmArg *args, Unary f)
{
  TmValue err;
  double x;

  ARG_NUM (0, x);
  return num_or_error (f (x));
}

static TmValue fn_abs (TmEvalContext *c, TmArg *a, int n) { return unary (c, a, fabs); }
static TmValue fn_exp (TmEvalContext *c, TmArg *a, int n) { return unary (c, a, exp); }
static TmValue fn_int (TmEvalContext *c, TmArg *a, int n) { return unary (c, a, floor); }
static TmValue fn_sin (TmEvalContext *c, TmArg *a, int n) { return unary (c, a, sin); }
static TmValue fn_cos (TmEvalContext *c, TmArg *a, int n) { return unary (c, a, cos); }

static TmValue
fn_sqrt (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double x;

  ARG_NUM (0, x);
  if (x < 0)
    return tm_value_error (TM_ERR_NUM);
  return tm_value_number (sqrt (x));
}

static TmValue
fn_ln (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double x;

  ARG_NUM (0, x);
  if (x <= 0)
    return tm_value_error (TM_ERR_NUM);
  return tm_value_number (log (x));
}

static TmValue
fn_log (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double x, base;

  ARG_NUM (0, x);
  OPT_NUM (1, base, 10);
  if (x <= 0 || base <= 0)
    return tm_value_error (TM_ERR_NUM);
  if (base == 1)
    return tm_value_error (TM_ERR_DIV0);
  return tm_value_number (log (x) / log (base));
}

static TmValue
fn_log10 (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double x;

  ARG_NUM (0, x);
  if (x <= 0)
    return tm_value_error (TM_ERR_NUM);
  return tm_value_number (log10 (x));
}

static TmValue
fn_power (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double x, y;

  ARG_NUM (0, x);
  ARG_NUM (1, y);
  if (x == 0 && y == 0)
    return tm_value_error (TM_ERR_NUM);
  if (x == 0 && y < 0)
    return tm_value_error (TM_ERR_DIV0);
  return tm_value_number (pow (x, y));
}

static TmValue
fn_mod (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double x, y;

  ARG_NUM (0, x);
  ARG_NUM (1, y);
  if (y == 0)
    return tm_value_error (TM_ERR_DIV0);
  /* The sign of the divisor, as Excel has it: MOD(-1,3) is 2. */
  return tm_value_number (x - y * floor (x / y));
}

static TmValue
fn_pi (TmEvalContext *ctx, TmArg *args, int n)
{
  return tm_value_number (G_PI);
}

static TmValue
fn_sign (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double x;

  ARG_NUM (0, x);
  return tm_value_number (x > 0 ? 1 : x < 0 ? -1 : 0);
}

/* Rounding halves away from zero, as a spreadsheet does, and at a power
 * of ten that may be negative: ROUND(1234,-2) is 1200. */
static double
round_to (double x, int digits, int mode)
{
  double f = pow (10, digits);
  char buf[32];
  double y;

  /* Through fifteen significant digits first, so that 2.675 rounds as it
   * reads rather than as the double nearest it (2.67499999...) would. */
  g_snprintf (buf, sizeof buf, "%.15g", x * f);
  y = g_ascii_strtod (buf, NULL);
  switch (mode)
    {
    case 0:  y = y < 0 ? -floor (-y + 0.5) : floor (y + 0.5); break;
    case 1:  y = y < 0 ? -ceil (-y) : ceil (y); break;   /* away from 0 */
    default: y = trunc (y); break;                       /* towards 0 */
    }
  return y / f;
}

static TmValue
round_fn (TmEvalContext *ctx, TmArg *args, int n, int mode)
{
  TmValue err;
  double x, digits;

  ARG_NUM (0, x);
  OPT_NUM (1, digits, 0);
  return tm_value_number (round_to (x, (int) trunc (digits), mode));
}

static TmValue fn_round     (TmEvalContext *c, TmArg *a, int n) { return round_fn (c, a, n, 0); }
static TmValue fn_roundup   (TmEvalContext *c, TmArg *a, int n) { return round_fn (c, a, n, 1); }
static TmValue fn_rounddown (TmEvalContext *c, TmArg *a, int n) { return round_fn (c, a, n, 2); }

/* ---- Logic ------------------------------------------------------------ */

static gboolean
lazy_bool (TmEvalContext *ctx, TmNode *node, gboolean *out, TmValue *err)
{
  TmValue v = tm_eval (ctx, node);
  TmErrorCode code;
  gboolean ok = tm_value_to_bool (&v, out, &code);

  tm_value_clear (&v);
  if (!ok)
    *err = tm_value_error (code);
  return ok;
}

static TmValue
lazy_if (TmEvalContext *ctx, TmNode **args, int n)
{
  TmValue err;
  gboolean cond;

  if (!lazy_bool (ctx, args[0], &cond, &err))
    return err;
  if (cond)
    return n > 1 ? tm_eval (ctx, args[1]) : tm_value_bool (TRUE);
  if (n > 2)
    {
      TmValue v = tm_eval (ctx, args[2]);
      return v;
    }
  return tm_value_bool (FALSE);
}

static TmValue
lazy_iferror (TmEvalContext *ctx, TmNode **args, int n)
{
  TmValue v = tm_eval (ctx, args[0]);

  if (v.type != TM_VALUE_ERROR)
    return v;
  return tm_eval (ctx, args[1]);
}

static TmValue
lazy_ifna (TmEvalContext *ctx, TmNode **args, int n)
{
  TmValue v = tm_eval (ctx, args[0]);

  if (v.type != TM_VALUE_ERROR || v.as.error != TM_ERR_NA)
    return v;
  return tm_eval (ctx, args[1]);
}

static TmValue
lazy_choose (TmEvalContext *ctx, TmNode **args, int n)
{
  TmValue v = tm_eval (ctx, args[0]);
  TmErrorCode code;
  double d;
  int k;

  if (!tm_value_to_number (&v, &d, &code))
    {
      tm_value_clear (&v);
      return tm_value_error (code);
    }
  tm_value_clear (&v);
  k = (int) floor (d);
  if (k < 1 || k >= n)
    return tm_value_error (TM_ERR_VALUE);
  return tm_eval (ctx, args[k]);
}

/* AND and OR take logical values from cells and skip the rest, as SUM
 * does with numbers. */
static TmValue
and_or (TmEvalContext *ctx, TmArg *args, int n, gboolean is_and)
{
  gboolean result = is_and, seen = FALSE;

  for (int i = 0; i < n; i++)
    {
      int count;
      const TmValue **cells = tm_arg_cells (ctx, &args[i], &count);

      for (int k = 0; k < count; k++)
        {
          const TmValue *v = cells[k];
          gboolean b;
          TmErrorCode code;

          if (v->type == TM_VALUE_ERROR)
            {
              TmValue e = tm_value_copy (v);
              g_free (cells);
              return e;
            }
          if (args[i].is_range && v->type != TM_VALUE_BOOL && v->type != TM_VALUE_NUMBER)
            continue;
          if (!tm_value_to_bool (v, &b, &code))
            {
              g_free (cells);
              return tm_value_error (code);
            }
          seen = TRUE;
          result = is_and ? (result && b) : (result || b);
        }
      g_free (cells);
    }
  if (!seen)
    return tm_value_error (TM_ERR_VALUE);
  return tm_value_bool (result);
}

static TmValue fn_and (TmEvalContext *c, TmArg *a, int n) { return and_or (c, a, n, TRUE); }
static TmValue fn_or  (TmEvalContext *c, TmArg *a, int n) { return and_or (c, a, n, FALSE); }

static TmValue
fn_not (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  gboolean b;

  ARG_BOOL (0, b);
  return tm_value_bool (!b);
}

static TmValue fn_true  (TmEvalContext *c, TmArg *a, int n) { return tm_value_bool (TRUE); }
static TmValue fn_false (TmEvalContext *c, TmArg *a, int n) { return tm_value_bool (FALSE); }
static TmValue fn_na    (TmEvalContext *c, TmArg *a, int n) { return tm_value_error (TM_ERR_NA); }

static TmValue
is_type (TmEvalContext *ctx, TmArg *args, int which)
{
  TmValue v = tm_arg_scalar (ctx, &args[0]);
  gboolean r;

  switch (which)
    {
    case 0: r = v.type == TM_VALUE_NUMBER; break;
    case 1: r = v.type == TM_VALUE_TEXT; break;
    case 2: r = v.type == TM_VALUE_EMPTY; break;
    case 3: r = v.type == TM_VALUE_ERROR; break;
    default: r = v.type == TM_VALUE_ERROR && v.as.error == TM_ERR_NA; break;
    }
  tm_value_clear (&v);
  return tm_value_bool (r);
}

static TmValue fn_isnumber (TmEvalContext *c, TmArg *a, int n) { return is_type (c, a, 0); }
static TmValue fn_istext   (TmEvalContext *c, TmArg *a, int n) { return is_type (c, a, 1); }
static TmValue fn_isblank  (TmEvalContext *c, TmArg *a, int n) { return is_type (c, a, 2); }
static TmValue fn_iserror  (TmEvalContext *c, TmArg *a, int n) { return is_type (c, a, 3); }
static TmValue fn_isna     (TmEvalContext *c, TmArg *a, int n) { return is_type (c, a, 4); }

/* ---- Text ------------------------------------------------------------- */

static TmValue
fn_concat (TmEvalContext *ctx, TmArg *args, int n)
{
  GString *out = g_string_new (NULL);

  for (int i = 0; i < n; i++)
    {
      int count;
      const TmValue **cells = tm_arg_cells (ctx, &args[i], &count);

      for (int k = 0; k < count; k++)
        {
          char *t;

          if (cells[k]->type == TM_VALUE_ERROR)
            {
              TmValue e = tm_value_copy (cells[k]);
              g_free (cells);
              g_string_free (out, TRUE);
              return e;
            }
          t = tm_value_to_text (cells[k]);
          g_string_append (out, t);
          g_free (t);
        }
      g_free (cells);
    }
  return tm_value_take (g_string_free (out, FALSE));
}

static TmValue
fn_len (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  char *t = tm_arg_text (ctx, &args[0], &err);
  double len;

  if (t == NULL)
    return err;
  len = (double) g_utf8_strlen (t, -1);
  g_free (t);
  return tm_value_number (len);
}

static TmValue
change_case (TmEvalContext *ctx, TmArg *args, gboolean upper)
{
  TmValue err;
  char *t = tm_arg_text (ctx, &args[0], &err);
  char *r;

  if (t == NULL)
    return err;
  r = upper ? g_utf8_strup (t, -1) : g_utf8_strdown (t, -1);
  g_free (t);
  return tm_value_take (r);
}

static TmValue fn_upper (TmEvalContext *c, TmArg *a, int n) { return change_case (c, a, TRUE); }
static TmValue fn_lower (TmEvalContext *c, TmArg *a, int n) { return change_case (c, a, FALSE); }

/* TEXT with the handful of formats a forecast is shown in: "0", "0.0",
 * "0.00%", "#,##0", "#,##0.00" and the like. */
static TmValue
fn_text (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double x;
  char *fmt = NULL, *out;
  const char *dot;
  int decimals = 0;
  gboolean percent, thousands;

  ARG_NUM (0, x);
  fmt = tm_arg_text (ctx, &args[1], &err);
  if (fmt == NULL)
    return err;
  percent = strchr (fmt, '%') != NULL;
  thousands = strchr (fmt, ',') != NULL;
  dot = strchr (fmt, '.');
  if (dot != NULL)
    for (const char *p = dot + 1; *p == '0' || *p == '#'; p++)
      decimals++;
  if (percent)
    x *= 100;
  out = g_strdup_printf ("%.*f", decimals, x);
  if (thousands)
    {
      GString *s = g_string_new (NULL);
      const char *start = out + (out[0] == '-');
      const char *point = strchr (start, '.');
      int int_len = point != NULL ? (int) (point - start) : (int) strlen (start);

      if (out[0] == '-')
        g_string_append_c (s, '-');
      for (int i = 0; i < int_len; i++)
        {
          if (i > 0 && (int_len - i) % 3 == 0)
            g_string_append_c (s, ',');
          g_string_append_c (s, start[i]);
        }
      if (point != NULL)
        g_string_append (s, point);
      g_free (out);
      out = g_string_free (s, FALSE);
    }
  if (percent)
    {
      char *t = g_strconcat (out, "%", NULL);
      g_free (out);
      out = t;
    }
  g_free (fmt);
  return tm_value_take (out);
}

/* ---- Looking things up ------------------------------------------------ */

static TmValue
fn_index (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double r, c;
  const TmRange *range = &args[0].range;
  int row, col;

  if (!args[0].is_range)
    return tm_value_error (TM_ERR_VALUE);
  ARG_NUM (1, r);
  OPT_NUM (2, c, 1);
  /* A single row or column may be indexed by one number either way. */
  if (!HAS_ARG (2) && range->row0 == range->row1)
    {
      c = r;
      r = 1;
    }
  row = range->row0 + (int) r - 1;
  col = range->col0 + (int) c - 1;
  if (r < 1 || c < 1 || !tm_range_contains (range, row, col))
    return tm_value_error (TM_ERR_REF);
  return tm_value_copy (tm_ctx_cell (ctx, row, col));
}

static TmValue
fn_countif (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue crit = tm_arg_scalar (ctx, &args[1]);
  TmCriteria c;
  int count, hits = 0;
  const TmValue **cells;

  tm_criteria_parse (&crit, &c);
  tm_value_clear (&crit);
  cells = tm_arg_cells (ctx, &args[0], &count);
  for (int k = 0; k < count; k++)
    hits += tm_criteria_match (&c, cells[k]);
  g_free (cells);
  tm_criteria_clear (&c);
  return tm_value_number (hits);
}

static TmValue
fn_sumif (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue crit = tm_arg_scalar (ctx, &args[1]);
  TmCriteria c;
  int count, count2;
  const TmValue **cells, **sums;
  double total = 0;

  tm_criteria_parse (&crit, &c);
  tm_value_clear (&crit);
  cells = tm_arg_cells (ctx, &args[0], &count);
  sums = HAS_ARG (2) ? tm_arg_cells (ctx, &args[2], &count2) : cells;
  if (HAS_ARG (2) && count2 < count)
    count = count2;
  for (int k = 0; k < count; k++)
    if (tm_criteria_match (&c, cells[k]) && sums[k]->type == TM_VALUE_NUMBER)
      total += sums[k]->as.number;
  if (sums != cells)
    g_free (sums);
  g_free (cells);
  tm_criteria_clear (&c);
  return tm_value_number (total);
}

/* ---- Money ------------------------------------------------------------ */

static TmValue
fn_npv (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double rate, npv = 0, f = 1;
  GArray *flows;

  ARG_NUM (0, rate);
  flows = collect (ctx, args + 1, n - 1, &err);
  if (flows == NULL)
    return err;
  if (rate == -1)
    {
      g_array_free (flows, TRUE);
      return tm_value_error (TM_ERR_DIV0);
    }
  /* The first flow is a period away, as Excel's NPV has it. */
  for (guint i = 0; i < flows->len; i++)
    {
      f /= 1 + rate;
      npv += g_array_index (flows, double, i) * f;
    }
  g_array_free (flows, TRUE);
  return tm_value_number (npv);
}

static TmValue
fn_irr (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  GArray *flows = collect (ctx, args, 1, &err);
  double r, guess;

  if (flows == NULL)
    return err;
  OPT_NUM (1, guess, 0.1);
  r = guess;
  /* Newton's method on the net present value, with the flows a period
   * apart and the first one now. */
  for (int iter = 0; iter < 100; iter++)
    {
      double f = 0, df = 0, step;

      for (guint i = 0; i < flows->len; i++)
        {
          double c = g_array_index (flows, double, i);
          f += c / pow (1 + r, i);
          df -= i * c / pow (1 + r, i + 1);
        }
      if (df == 0)
        break;
      step = f / df;
      r -= step;
      if (r <= -1)
        r = -0.999999;
      if (fabs (step) < 1e-12)
        {
          g_array_free (flows, TRUE);
          return tm_value_number (r);
        }
    }
  g_array_free (flows, TRUE);
  return tm_value_error (TM_ERR_NUM);
}

static TmValue
fn_pmt (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double rate, nper, pv, fv, type, pmt;

  ARG_NUM (0, rate);
  ARG_NUM (1, nper);
  ARG_NUM (2, pv);
  OPT_NUM (3, fv, 0);
  OPT_NUM (4, type, 0);
  if (nper == 0)
    return tm_value_error (TM_ERR_NUM);
  if (rate == 0)
    pmt = -(pv + fv) / nper;
  else
    {
      double f = pow (1 + rate, nper);
      pmt = -(rate * (pv * f + fv)) / ((1 + rate * (type != 0)) * (f - 1));
    }
  return tm_value_number (pmt);
}

static TmValue
fn_fv (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double rate, nper, pmt, pv, type;

  ARG_NUM (0, rate);
  ARG_NUM (1, nper);
  ARG_NUM (2, pmt);
  OPT_NUM (3, pv, 0);
  OPT_NUM (4, type, 0);
  if (rate == 0)
    return tm_value_number (-(pv + pmt * nper));
  {
    double f = pow (1 + rate, nper);
    return tm_value_number (-(pv * f + pmt * (1 + rate * (type != 0)) * (f - 1) / rate));
  }
}

static TmValue
fn_pv (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double rate, nper, pmt, fv, type;

  ARG_NUM (0, rate);
  ARG_NUM (1, nper);
  ARG_NUM (2, pmt);
  OPT_NUM (3, fv, 0);
  OPT_NUM (4, type, 0);
  if (rate == 0)
    return tm_value_number (-(fv + pmt * nper));
  {
    double f = pow (1 + rate, nper);
    return tm_value_number (-(fv + pmt * (1 + rate * (type != 0)) * (f - 1) / rate) / f);
  }
}

#define M "Maths"
#define L "Logic"
#define T "Text"
#define R "Lookup"
#define F "Finance"

const TmFunction tm_fn_core[] = {
  FN ("SUM", 1, -1, fn_sum, 0, M, "SUM(number, ...)", "The numbers added up."),
  FN ("PRODUCT", 1, -1, fn_product, 0, M, "PRODUCT(number, ...)", "The numbers multiplied together."),
  FN ("SUMPRODUCT", 1, 16, fn_sumproduct, 0, M, "SUMPRODUCT(range, range, ...)", "Ranges multiplied cell by cell, and the products added."),
  FN ("ABS", 1, 1, fn_abs, 0, M, "ABS(number)", "The number without its sign."),
  FN ("SQRT", 1, 1, fn_sqrt, 0, M, "SQRT(number)", "The square root."),
  FN ("EXP", 1, 1, fn_exp, 0, M, "EXP(number)", "e raised to the number."),
  FN ("LN", 1, 1, fn_ln, 0, M, "LN(number)", "The natural logarithm."),
  FN ("LOG", 1, 2, fn_log, 0, M, "LOG(number, [base])", "The logarithm, to base 10 unless told otherwise."),
  FN ("LOG10", 1, 1, fn_log10, 0, M, "LOG10(number)", "The logarithm to base 10."),
  FN ("POWER", 2, 2, fn_power, 0, M, "POWER(number, power)", "The number raised to a power."),
  FN ("MOD", 2, 2, fn_mod, 0, M, "MOD(number, divisor)", "The remainder, with the divisor's sign."),
  FN ("INT", 1, 1, fn_int, 0, M, "INT(number)", "Rounded down to a whole number."),
  FN ("ROUND", 1, 2, fn_round, 0, M, "ROUND(number, [digits])", "Rounded to so many decimal places."),
  FN ("ROUNDUP", 1, 2, fn_roundup, 0, M, "ROUNDUP(number, [digits])", "Rounded away from zero."),
  FN ("ROUNDDOWN", 1, 2, fn_rounddown, 0, M, "ROUNDDOWN(number, [digits])", "Rounded towards zero."),
  FN ("SIGN", 1, 1, fn_sign, 0, M, "SIGN(number)", "1, 0 or -1."),
  FN ("PI", 0, 0, fn_pi, 0, M, "PI()", "3.14159265358979."),
  FN ("SIN", 1, 1, fn_sin, 0, M, "SIN(radians)", "The sine; for seasonal shapes."),
  FN ("COS", 1, 1, fn_cos, 0, M, "COS(radians)", "The cosine; for seasonal shapes."),

  LAZY ("IF", 1, 3, lazy_if, 0, L, "IF(test, [then], [else])", "One value or another; only the branch taken is worked out."),
  LAZY ("IFERROR", 2, 2, lazy_iferror, 0, L, "IFERROR(value, otherwise)", "The value, or the second if the first is an error."),
  LAZY ("IFNA", 2, 2, lazy_ifna, 0, L, "IFNA(value, otherwise)", "The value, or the second if the first is #N/A."),
  LAZY ("CHOOSE", 2, -1, lazy_choose, 0, L, "CHOOSE(index, value1, value2, ...)", "The index'th of the values."),
  FN ("AND", 1, -1, fn_and, 0, L, "AND(logical, ...)", "TRUE if all are."),
  FN ("OR", 1, -1, fn_or, 0, L, "OR(logical, ...)", "TRUE if any is."),
  FN ("NOT", 1, 1, fn_not, 0, L, "NOT(logical)", "The opposite."),
  FN ("TRUE", 0, 0, fn_true, 0, L, "TRUE()", "TRUE."),
  FN ("FALSE", 0, 0, fn_false, 0, L, "FALSE()", "FALSE."),
  FN ("NA", 0, 0, fn_na, 0, L, "NA()", "#N/A: no value, on purpose."),
  FN ("ISNUMBER", 1, 1, fn_isnumber, 0, L, "ISNUMBER(value)", "Whether the value is a number."),
  FN ("ISTEXT", 1, 1, fn_istext, 0, L, "ISTEXT(value)", "Whether the value is text."),
  FN ("ISBLANK", 1, 1, fn_isblank, 0, L, "ISBLANK(value)", "Whether the cell is empty."),
  FN ("ISERROR", 1, 1, fn_iserror, 0, L, "ISERROR(value)", "Whether the value is an error."),
  FN ("ISNA", 1, 1, fn_isna, 0, L, "ISNA(value)", "Whether the value is #N/A."),

  FN ("CONCAT", 1, -1, fn_concat, 0, T, "CONCAT(text, ...)", "The texts joined together."),
  FN ("CONCATENATE", 1, -1, fn_concat, 0, T, "CONCATENATE(text, ...)", "The texts joined together."),
  FN ("LEN", 1, 1, fn_len, 0, T, "LEN(text)", "The number of characters."),
  FN ("UPPER", 1, 1, fn_upper, 0, T, "UPPER(text)", "In capitals."),
  FN ("LOWER", 1, 1, fn_lower, 0, T, "LOWER(text)", "In small letters."),
  FN ("TEXT", 2, 2, fn_text, 0, T, "TEXT(number, format)", "The number as text: \"0.00\", \"#,##0\", \"0%\"."),

  FN ("INDEX", 2, 3, fn_index, 0, R, "INDEX(range, row, [column])", "The cell at a position in a range."),
  FN ("COUNTIF", 2, 2, fn_countif, 0, R, "COUNTIF(range, criteria)", "How many cells meet the criteria, such as \">100\"."),
  FN ("SUMIF", 2, 3, fn_sumif, 0, R, "SUMIF(range, criteria, [sum_range])", "The sum of the cells that meet the criteria."),

  FN ("NPV", 2, -1, fn_npv, 0, F, "NPV(rate, flow, ...)", "Net present value of flows a period apart, the first a period away."),
  FN ("IRR", 1, 2, fn_irr, 0, F, "IRR(flows, [guess])", "The rate at which the flows' present value is zero."),
  FN ("PMT", 3, 5, fn_pmt, 0, F, "PMT(rate, periods, pv, [fv], [type])", "The payment each period of a loan or annuity."),
  FN ("FV", 3, 5, fn_fv, 0, F, "FV(rate, periods, pmt, [pv], [type])", "The future value of an investment."),
  FN ("PV", 3, 5, fn_pv, 0, F, "PV(rate, periods, pmt, [fv], [type])", "The present value of an investment."),
};
const int tm_fn_core_count = G_N_ELEMENTS (tm_fn_core);
