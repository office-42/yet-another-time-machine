/* tm-value.c - what a cell can hold
 *
 * Copyright (C) 2026 The timemachine authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "tm-value.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

void
tm_value_clear (TmValue *value)
{
  if (value->type == TM_VALUE_TEXT)
    g_free (value->as.text);
  value->type = TM_VALUE_EMPTY;
  value->as.number = 0;
}

TmValue
tm_value_copy (const TmValue *value)
{
  TmValue v = *value;

  if (v.type == TM_VALUE_TEXT)
    v.as.text = g_strdup (value->as.text);
  return v;
}

TmValue
tm_value_empty (void)
{
  TmValue v = { TM_VALUE_EMPTY, { 0 } };
  return v;
}

TmValue
tm_value_number (double number)
{
  TmValue v;

  if (!isfinite (number))
    return tm_value_error (TM_ERR_NUM);
  v.type = TM_VALUE_NUMBER;
  /* -0 is a floating-point curiosity nobody wants to see in a cell. */
  v.as.number = number == 0 ? 0.0 : number;
  return v;
}

TmValue
tm_value_text (const char *text)
{
  return tm_value_take (g_strdup (text != NULL ? text : ""));
}

TmValue
tm_value_take (char *text)
{
  TmValue v;

  v.type = TM_VALUE_TEXT;
  v.as.text = text;
  return v;
}

TmValue
tm_value_bool (gboolean boolean)
{
  TmValue v;

  v.type = TM_VALUE_BOOL;
  v.as.boolean = boolean != FALSE;
  return v;
}

TmValue
tm_value_error (TmErrorCode code)
{
  TmValue v;

  v.type = TM_VALUE_ERROR;
  v.as.error = code;
  return v;
}

static const struct {
  TmErrorCode code;
  const char *name;
} ERRORS[] = {
  { TM_ERR_NULL,     "#NULL!"  },
  { TM_ERR_DIV0,     "#DIV/0!" },
  { TM_ERR_VALUE,    "#VALUE!" },
  { TM_ERR_REF,      "#REF!"   },
  { TM_ERR_NAME,     "#NAME?"  },
  { TM_ERR_NUM,      "#NUM!"   },
  { TM_ERR_NA,       "#N/A"    },
  { TM_ERR_CIRCULAR, "#CIRC!"  },
};

const char *
tm_error_name (TmErrorCode code)
{
  for (guint i = 0; i < G_N_ELEMENTS (ERRORS); i++)
    if (ERRORS[i].code == code)
      return ERRORS[i].name;
  return "#ERROR";
}

gboolean
tm_error_code_parse (const char *text, TmErrorCode *out)
{
  for (guint i = 0; i < G_N_ELEMENTS (ERRORS); i++)
    if (g_ascii_strcasecmp (text, ERRORS[i].name) == 0)
      {
        *out = ERRORS[i].code;
        return TRUE;
      }
  return FALSE;
}

/* The whole of text, give or take spaces, as a number; "45%" is 0.45. */
static gboolean
parse_number (const char *text, double *out)
{
  char *end;
  double d;

  while (g_ascii_isspace (*text))
    text++;
  if (*text == '\0')
    return FALSE;
  /* strtod reads "inf", "nan" and hex, none of which a user means. */
  for (const char *p = text; *p != '\0'; p++)
    if (!(g_ascii_isdigit (*p) || strchr ("+-.eE% ", *p) != NULL))
      return FALSE;

  d = g_ascii_strtod (text, &end);
  if (end == text)
    return FALSE;
  while (g_ascii_isspace (*end))
    end++;
  if (*end == '%')
    {
      d /= 100.0;
      end++;
      while (g_ascii_isspace (*end))
        end++;
    }
  if (*end != '\0' || !isfinite (d))
    return FALSE;
  *out = d;
  return TRUE;
}

gboolean
tm_value_to_number (const TmValue *value, double *out, TmErrorCode *error)
{
  switch (value->type)
    {
    case TM_VALUE_EMPTY:
      *out = 0;
      return TRUE;
    case TM_VALUE_NUMBER:
      *out = value->as.number;
      return TRUE;
    case TM_VALUE_BOOL:
      *out = value->as.boolean ? 1 : 0;
      return TRUE;
    case TM_VALUE_TEXT:
      if (parse_number (value->as.text, out))
        return TRUE;
      *error = TM_ERR_VALUE;
      return FALSE;
    case TM_VALUE_ERROR:
    default:
      *error = value->as.error;
      return FALSE;
    }
}

gboolean
tm_value_to_bool (const TmValue *value, gboolean *out, TmErrorCode *error)
{
  switch (value->type)
    {
    case TM_VALUE_EMPTY:
      *out = FALSE;
      return TRUE;
    case TM_VALUE_NUMBER:
      *out = value->as.number != 0;
      return TRUE;
    case TM_VALUE_BOOL:
      *out = value->as.boolean;
      return TRUE;
    case TM_VALUE_TEXT:
      if (g_ascii_strcasecmp (value->as.text, "TRUE") == 0)
        {
          *out = TRUE;
          return TRUE;
        }
      if (g_ascii_strcasecmp (value->as.text, "FALSE") == 0)
        {
          *out = FALSE;
          return TRUE;
        }
      *error = TM_ERR_VALUE;
      return FALSE;
    case TM_VALUE_ERROR:
    default:
      *error = value->as.error;
      return FALSE;
    }
}

char *
tm_value_to_text (const TmValue *value)
{
  switch (value->type)
    {
    case TM_VALUE_EMPTY:
      return g_strdup ("");
    case TM_VALUE_NUMBER:
      return tm_number_format_general (value->as.number);
    case TM_VALUE_TEXT:
      return g_strdup (value->as.text);
    case TM_VALUE_BOOL:
      return g_strdup (value->as.boolean ? "TRUE" : "FALSE");
    case TM_VALUE_ERROR:
    default:
      return g_strdup (tm_error_name (value->as.error));
    }
}

static int
type_rank (TmValueType type)
{
  switch (type)
    {
    case TM_VALUE_EMPTY:  return 0;
    case TM_VALUE_NUMBER: return 1;
    case TM_VALUE_TEXT:   return 2;
    case TM_VALUE_BOOL:   return 3;
    case TM_VALUE_ERROR:
    default:              return 4;
    }
}

int
tm_value_compare (const TmValue *a, const TmValue *b)
{
  TmValue ea = *a, eb = *b;

  /* An empty cell compares as whatever it is being compared with would be
   * when empty: 0, "" or FALSE. */
  if (ea.type == TM_VALUE_EMPTY && eb.type != TM_VALUE_EMPTY)
    {
      if (eb.type == TM_VALUE_TEXT)
        return eb.as.text[0] == '\0' ? 0 : -1;
      ea.type = eb.type;
      ea.as.number = 0;
      if (eb.type == TM_VALUE_BOOL)
        ea.as.boolean = FALSE;
    }
  if (eb.type == TM_VALUE_EMPTY && ea.type != TM_VALUE_EMPTY)
    return -tm_value_compare (b, a);

  if (type_rank (ea.type) != type_rank (eb.type))
    return type_rank (ea.type) < type_rank (eb.type) ? -1 : 1;

  switch (ea.type)
    {
    case TM_VALUE_NUMBER:
      return ea.as.number < eb.as.number ? -1 : ea.as.number > eb.as.number;
    case TM_VALUE_TEXT:
      {
        char *x = g_utf8_casefold (ea.as.text, -1);
        char *y = g_utf8_casefold (eb.as.text, -1);
        int r = g_utf8_collate (x, y);

        g_free (x);
        g_free (y);
        return r < 0 ? -1 : r > 0;
      }
    case TM_VALUE_BOOL:
      return (int) ea.as.boolean - (int) eb.as.boolean;
    case TM_VALUE_ERROR:
      return (int) ea.as.error - (int) eb.as.error;
    case TM_VALUE_EMPTY:
    default:
      return 0;
    }
}

TmValue
tm_value_parse_input (const char *input)
{
  double d;
  TmErrorCode code;

  if (input == NULL || *input == '\0')
    return tm_value_empty ();
  if (input[0] == '\'')
    return tm_value_text (input + 1);
  if (parse_number (input, &d))
    return tm_value_number (d);
  if (g_ascii_strcasecmp (input, "TRUE") == 0)
    return tm_value_bool (TRUE);
  if (g_ascii_strcasecmp (input, "FALSE") == 0)
    return tm_value_bool (FALSE);
  if (tm_error_code_parse (input, &code))
    return tm_value_error (code);
  return tm_value_text (input);
}

char *
tm_number_format_general (double number)
{
  char buf[64];
  double a = fabs (number);

  if (number == 0)
    return g_strdup ("0");

  if (a >= 1e-5 && a < 1e11)
    {
      /* Eleven significant digits, the rest rounded away, then the
       * trailing zeros of the fraction dropped. */
      int digits = 11 - (int) floor (log10 (a)) - 1;
      char *dot;

      digits = CLAMP (digits, 0, 15);
      g_snprintf (buf, sizeof buf, "%.*f", digits, number);
      dot = strchr (buf, '.');
      if (dot != NULL)
        {
          char *end = buf + strlen (buf) - 1;
          while (end > dot && *end == '0')
            *end-- = '\0';
          if (end == dot)
            *end = '\0';
        }
      if (strcmp (buf, "-0") == 0)
        return g_strdup ("0");
      return g_strdup (buf);
    }

  {
    /* 1.23456E+12, the way a spreadsheet writes it. */
    char mant[32];
    int exp10 = (int) floor (log10 (a));
    double m = number / pow (10, exp10);
    char *dot;

    if (fabs (m) >= 9.999995)
      {
        m /= 10;
        exp10++;
      }
    g_snprintf (mant, sizeof mant, "%.5f", m);
    dot = strchr (mant, '.');
    if (dot != NULL)
      {
        char *end = mant + strlen (mant) - 1;
        while (end > dot && *end == '0')
          *end-- = '\0';
        if (end == dot)
          *end = '\0';
      }
    g_snprintf (buf, sizeof buf, "%sE%c%02d", mant, exp10 < 0 ? '-' : '+', abs (exp10));
    return g_strdup (buf);
  }
}
