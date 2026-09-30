/* tm-numfmt.c - number formats
 *
 * Copyright (C) 2026 The timemachine authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "tm-numfmt.h"
#include "tm-value.h"

#include <math.h>
#include <string.h>

typedef struct {
  GString *prefix, *suffix;
  int decimals;
  gboolean thousands, percent, scientific, general;
  gboolean digits;      /* has a place for the number at all */
} Format;

/* Literal text: a quoted run, a backslashed character, or anything that
 * is not part of a number's picture. */
static const char *
take_literal (const char *p, GString *out)
{
  if (*p == '"')
    {
      p++;
      while (*p != '\0' && *p != '"')
        g_string_append_c (out, *p++);
      return *p == '"' ? p + 1 : p;
    }
  if (*p == '\\' && p[1] != '\0')
    {
      g_string_append_c (out, p[1]);
      return p + 2;
    }
  if (*p == '_' && p[1] != '\0')
    {
      /* "_)" leaves room the width of ")"; a space will do. */
      g_string_append_c (out, ' ');
      return p + 2;
    }
  g_string_append_c (out, *p);
  return p + 1;
}

static void
parse (const char *code, Format *f)
{
  const char *p = code;
  gboolean in_decimals = FALSE;

  memset (f, 0, sizeof *f);
  f->prefix = g_string_new (NULL);
  f->suffix = g_string_new (NULL);
  if (code == NULL || *code == '\0' || g_ascii_strcasecmp (code, "General") == 0)
    {
      f->general = TRUE;
      return;
    }

  while (*p != '\0' && *p != ';' && strchr ("#0?.", *p) == NULL)
    p = take_literal (p, f->prefix);
  while (*p != '\0' && strchr ("#0?,.", *p) != NULL)
    {
      if (*p != ',' && *p != '.')
        f->digits = TRUE;
      if (*p == ',')
        f->thousands = TRUE;
      else if (*p == '.')
        in_decimals = TRUE;
      else if (in_decimals)
        f->decimals++;
      p++;
    }
  if ((*p == 'E' || *p == 'e') && (p[1] == '+' || p[1] == '-'))
    {
      f->scientific = TRUE;
      p += 2;
      while (*p == '0' || *p == '#')
        p++;
    }
  while (*p != '\0' && *p != ';')
    p = take_literal (p, f->suffix);
  if (strchr (f->prefix->str, '%') != NULL || strchr (f->suffix->str, '%') != NULL)
    f->percent = TRUE;
  f->decimals = MIN (f->decimals, 15);
}

static void
clear (Format *f)
{
  g_string_free (f->prefix, TRUE);
  g_string_free (f->suffix, TRUE);
}

static void
group_thousands (GString *out, const char *digits)
{
  const char *point = strchr (digits, '.');
  int int_len = point != NULL ? (int) (point - digits) : (int) strlen (digits);

  for (int i = 0; i < int_len; i++)
    {
      if (i > 0 && (int_len - i) % 3 == 0)
        g_string_append_c (out, ',');
      g_string_append_c (out, digits[i]);
    }
  if (point != NULL)
    g_string_append (out, point);
}

/* The n'th ";"-separated section of a code, or NULL if it has none;
 * semicolons inside quotes do not count. */
static char *
section (const char *code, int n)
{
  const char *start = code;
  gboolean quoted = FALSE;

  for (const char *p = code; ; p++)
    {
      if (*p == '"')
        quoted = !quoted;
      else if ((*p == ';' && !quoted) || *p == '\0')
        {
          if (n == 0)
            return g_strndup (start, (gsize) (p - start));
          if (*p == '\0')
            return NULL;
          n--;
          start = p + 1;
        }
    }
}

static char *format_section (double number, const char *code, gboolean signed_);

/* Excel's sections: positive;negative;zero.  A negative section shows
 * the number without its sign, which the section writes itself if it
 * wants one. */
char *
tm_format_number (double number, const char *code)
{
  char *part = NULL, *out;

  if (code != NULL && strchr (code, ';') != NULL)
    {
      if (number < 0)
        part = section (code, 1);
      else if (number == 0)
        part = section (code, 2);
    }
  if (part != NULL && *part != '\0')
    out = format_section (fabs (number), part, FALSE);
  else
    {
      char *first = code != NULL ? section (code, 0) : NULL;
      out = format_section (number, first, TRUE);
      g_free (first);
    }
  g_free (part);
  return out;
}

static char *
format_section (double number, const char *code, gboolean signed_)
{
  Format f;
  GString *out;
  char buf[400];
  double v;
  gboolean negative;

  parse (code, &f);
  if (f.general)
    {
      clear (&f);
      return tm_number_format_general (number);
    }
  if (!f.digits)
    {
      /* Only text, such as a zero section of "-": the text is all. */
      out = g_string_new (f.prefix->str);
      g_string_append (out, f.suffix->str);
      clear (&f);
      return g_string_free (out, FALSE);
    }

  v = f.percent ? number * 100 : number;
  negative = v < 0;
  v = fabs (v);
  if (f.scientific)
    {
      int exp10 = v == 0 ? 0 : (int) floor (log10 (v));
      double m = v / pow (10, exp10);

      g_snprintf (buf, sizeof buf, "%.*f", f.decimals, m);
      /* Rounding can carry the mantissa to 10. */
      if (g_ascii_strtod (buf, NULL) >= 10)
        {
          exp10++;
          g_snprintf (buf, sizeof buf, "%.*f", f.decimals, v / pow (10, exp10));
        }
      g_snprintf (buf + strlen (buf), sizeof buf - strlen (buf), "E%c%02d",
                  exp10 < 0 ? '-' : '+', ABS (exp10));
    }
  else if (v >= 1e300)
    g_strlcpy (buf, "#", sizeof buf);
  else
    {
      /* Halves round away from zero, as a spreadsheet's do, not to even
       * as printf's do: 1234.5 in "#,##0" is 1,235.  Through fifteen
       * significant digits first, so that 2.675 counts as the half it
       * reads as. */
      double scale = pow (10, f.decimals), t = v * scale;

      if (t < 1e15)
        {
          char tmp[64];

          g_snprintf (tmp, sizeof tmp, "%.15g", t);
          v = floor (g_ascii_strtod (tmp, NULL) + 0.5) / scale;
        }
      g_snprintf (buf, sizeof buf, "%.*f", f.decimals, v);
    }

  /* A negative that rounds to nothing has no sign: -0.001 as "0.00". */
  if (negative)
    {
      gboolean zero = TRUE;
      for (const char *s = buf; *s != '\0' && *s != 'E'; s++)
        if (*s >= '1' && *s <= '9')
          zero = FALSE;
      negative = !zero;
    }

  out = g_string_new (negative && signed_ ? "-" : "");
  g_string_append (out, f.prefix->str);
  if (f.thousands && !f.scientific)
    group_thousands (out, buf);
  else
    g_string_append (out, buf);
  g_string_append (out, f.suffix->str);
  clear (&f);
  return g_string_free (out, FALSE);
}

char *
tm_format_change_decimals (const char *code, int delta)
{
  Format f;
  GString *out;
  int decimals;

  parse (code, &f);
  if (f.general)
    {
      clear (&f);
      return g_strdup (delta > 0 ? "0.0" : "0");
    }
  decimals = CLAMP (f.decimals + delta, 0, 10);
  out = g_string_new (NULL);
  /* The literals are written back quoted, so that they read as text
   * whatever they hold. */
  if (f.prefix->len > 0)
    {
      if (strcmp (f.prefix->str, "$") == 0)
        g_string_append (out, "$");
      else
        g_string_append_printf (out, "\"%s\"", f.prefix->str);
    }
  g_string_append (out, f.thousands ? "#,##0" : "0");
  if (decimals > 0)
    {
      g_string_append_c (out, '.');
      for (int i = 0; i < decimals; i++)
        g_string_append_c (out, '0');
    }
  if (f.scientific)
    g_string_append (out, "E+00");
  if (f.suffix->len > 0)
    {
      if (strcmp (f.suffix->str, "%") == 0)
        g_string_append (out, "%");
      else
        g_string_append_printf (out, "\"%s\"", f.suffix->str);
    }
  clear (&f);
  return g_string_free (out, FALSE);
}

gboolean
tm_format_is_percent (const char *code)
{
  Format f;
  gboolean percent;

  parse (code, &f);
  percent = f.percent;
  clear (&f);
  return percent;
}
