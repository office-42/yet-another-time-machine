/* tm-types.c - cell addresses and ranges
 *
 * Copyright (C) 2026 The timemachine authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "tm-types.h"

#include <string.h>

char *
tm_col_name (int col, char *buf, gsize size)
{
  char tmp[8];
  int n = 0;

  /* The "- 1" each round is what makes the numbering bijective: after Z
   * comes AA, not BA, because there is no zero digit to write. */
  col += 1;
  while (col > 0 && n < (int) sizeof tmp)
    {
      col -= 1;
      tmp[n++] = (char) ('A' + col % 26);
      col /= 26;
    }

  if (size == 0)
    return buf;
  for (int i = 0; i < n && (gsize) i < size - 1; i++)
    buf[i] = tmp[n - 1 - i];
  buf[MIN ((gsize) n, size - 1)] = '\0';
  return buf;
}

int
tm_col_parse (const char *text, int *col)
{
  int value = 0, n = 0;

  while (g_ascii_isalpha (text[n]))
    {
      value = value * 26 + (g_ascii_toupper (text[n]) - 'A' + 1);
      n++;
      if (n > 3 || value > TM_MAX_COLS)
        return 0;
    }
  if (n == 0)
    return 0;
  *col = value - 1;
  return n;
}

char *
tm_ref_name (int row, int col)
{
  char name[8];

  tm_col_name (col, name, sizeof name);
  return g_strdup_printf ("%s%d", name, row + 1);
}

gboolean
tm_ref_parse (const char *text, TmRef *out)
{
  const char *p = text;
  int col, n;
  gint64 row = 0;

  if (*p == '$')
    p++;
  n = tm_col_parse (p, &col);
  if (n == 0)
    return FALSE;
  p += n;
  if (*p == '$')
    p++;
  if (!g_ascii_isdigit (*p) || *p == '0')
    return FALSE;
  while (g_ascii_isdigit (*p))
    {
      row = row * 10 + (*p - '0');
      if (row > TM_MAX_ROWS)
        return FALSE;
      p++;
    }
  if (*p != '\0')
    return FALSE;

  out->row = (int) row - 1;
  out->col = col;
  return TRUE;
}

gboolean
tm_range_parse (const char *text, TmRange *out)
{
  const char *colon = strchr (text, ':');
  TmRef a, b;

  if (colon == NULL)
    {
      if (!tm_ref_parse (text, &a))
        return FALSE;
      out->row0 = out->row1 = a.row;
      out->col0 = out->col1 = a.col;
      return TRUE;
    }

  {
    char *left = g_strndup (text, (gsize) (colon - text));
    gboolean ok = tm_ref_parse (left, &a) && tm_ref_parse (colon + 1, &b);

    g_free (left);
    if (!ok)
      return FALSE;
  }
  out->row0 = a.row;
  out->col0 = a.col;
  out->row1 = b.row;
  out->col1 = b.col;
  tm_range_normalize (out);
  return TRUE;
}

char *
tm_range_name (const TmRange *range)
{
  char *a = tm_ref_name (range->row0, range->col0);
  char *b, *name;

  if (range->row0 == range->row1 && range->col0 == range->col1)
    return a;
  b = tm_ref_name (range->row1, range->col1);
  name = g_strdup_printf ("%s:%s", a, b);
  g_free (a);
  g_free (b);
  return name;
}

void
tm_range_normalize (TmRange *range)
{
  if (range->row0 > range->row1)
    {
      int t = range->row0;
      range->row0 = range->row1;
      range->row1 = t;
    }
  if (range->col0 > range->col1)
    {
      int t = range->col0;
      range->col0 = range->col1;
      range->col1 = t;
    }
}
