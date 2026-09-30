/* tm-file.c - reading and writing sheets
 *
 * Copyright (C) 2026 The timemachine authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "tm-file.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static gboolean
is_csv (const char *path)
{
  return g_str_has_suffix (path, ".csv") || g_str_has_suffix (path, ".CSV");
}

static char *
escape (const char *text)
{
  GString *out = g_string_new (NULL);

  for (const char *p = text; *p != '\0'; p++)
    switch (*p)
      {
      case '\t': g_string_append (out, "\\t"); break;
      case '\n': g_string_append (out, "\\n"); break;
      case '\\': g_string_append (out, "\\\\"); break;
      default:   g_string_append_c (out, *p); break;
      }
  return g_string_free (out, FALSE);
}

static char *
unescape (const char *text)
{
  GString *out = g_string_new (NULL);

  for (const char *p = text; *p != '\0'; p++)
    {
      if (*p == '\\' && p[1] != '\0')
        {
          p++;
          switch (*p)
            {
            case 't': g_string_append_c (out, '\t'); break;
            case 'n': g_string_append_c (out, '\n'); break;
            default:  g_string_append_c (out, *p); break;
            }
          continue;
        }
      g_string_append_c (out, *p);
    }
  return g_string_free (out, FALSE);
}

/* ---- .tm -------------------------------------------------------------- */

static gboolean
load_tm (TmSheet *sheet, const char *contents, const char *path, GError **error)
{
  char **lines = g_strsplit (contents, "\n", -1);
  gboolean ok = TRUE;

  if (lines[0] == NULL || !g_str_has_prefix (lines[0], "timemachine"))
    {
      g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
                   "%s is not a timemachine sheet", path);
      g_strfreev (lines);
      return FALSE;
    }

  tm_sheet_clear (sheet);
  for (int i = 1; lines[i] != NULL; i++)
    {
      char *line = lines[i];
      char **f;
      int nf;

      g_strchomp (line);         /* also drops a \r from Windows */
      if (*line == '\0' || *line == '#')
        continue;
      f = g_strsplit (line, "\t", 3);
      nf = (int) g_strv_length (f);

      if (strcmp (f[0], "cell") == 0 && nf >= 2)
        {
          TmRef ref;

          if (tm_ref_parse (f[1], &ref))
            {
              char *input = unescape (nf >= 3 ? f[2] : "");
              tm_sheet_set_input (sheet, ref.row, ref.col, input);
              g_free (input);
            }
        }
      else if (strcmp (f[0], "width") == 0 && nf == 3)
        {
          int col;
          if (tm_col_parse (f[1], &col) == (int) strlen (f[1]))
            tm_sheet_set_col_width (sheet, col, atoi (f[2]));
        }
      else if (strcmp (f[0], "iterations") == 0 && nf >= 2)
        tm_sheet_set_iterations (sheet, atoi (f[1]));
      else if (strcmp (f[0], "seed") == 0 && nf >= 2)
        tm_sheet_set_seed (sheet, g_ascii_strtoull (f[1], NULL, 10));
      /* Anything else is from a later version, and skipped. */
      g_strfreev (f);
    }
  g_strfreev (lines);
  return ok;
}

static void
save_cell (int row, int col, const char *input, gpointer data)
{
  GString *out = data;
  char *name = tm_ref_name (row, col);
  char *text = escape (input);

  g_string_append_printf (out, "cell\t%s\t%s\n", name, text);
  g_free (name);
  g_free (text);
}

static char *
save_tm (TmSheet *sheet)
{
  GString *out = g_string_new ("timemachine 1\n");

  g_string_append_printf (out, "iterations\t%d\n", tm_sheet_iterations (sheet));
  g_string_append_printf (out, "seed\t%" G_GUINT64_FORMAT "\n", tm_sheet_seed (sheet));
  for (int c = 0; c < TM_MAX_COLS; c++)
    if (tm_sheet_col_width (sheet, c) != TM_DEFAULT_COL_WIDTH)
      {
        char name[8];
        tm_col_name (c, name, sizeof name);
        g_string_append_printf (out, "width\t%s\t%d\n", name, tm_sheet_col_width (sheet, c));
      }
  tm_sheet_foreach (sheet, save_cell, out);
  return g_string_free (out, FALSE);
}

/* ---- CSV -------------------------------------------------------------- */

/* One record's fields, RFC 4180: quotes around a field that holds a
 * comma, a quote or a line break, and a quote inside doubled. */
static char **
csv_record (const char **p)
{
  GPtrArray *fields = g_ptr_array_new ();
  const char *s = *p;

  for (;;)
    {
      GString *field = g_string_new (NULL);

      if (*s == '"')
        {
          s++;
          for (;;)
            {
              if (*s == '\0')
                break;
              if (*s == '"')
                {
                  if (s[1] == '"')
                    {
                      g_string_append_c (field, '"');
                      s += 2;
                      continue;
                    }
                  s++;
                  break;
                }
              g_string_append_c (field, *s++);
            }
          while (*s != '\0' && *s != ',' && *s != '\n')
            s++;
        }
      else
        while (*s != '\0' && *s != ',' && *s != '\n')
          {
            if (*s != '\r')
              g_string_append_c (field, *s);
            s++;
          }
      g_ptr_array_add (fields, g_string_free (field, FALSE));
      if (*s == ',')
        {
          s++;
          continue;
        }
      if (*s == '\n')
        s++;
      break;
    }
  *p = s;
  g_ptr_array_add (fields, NULL);
  return (char **) g_ptr_array_free (fields, FALSE);
}

static void
load_csv (TmSheet *sheet, const char *contents)
{
  const char *p = contents;
  int row = 0;

  /* A byte-order mark is not part of the first field. */
  if (g_str_has_prefix (p, "\xef\xbb\xbf"))
    p += 3;
  tm_sheet_clear (sheet);
  while (*p != '\0' && row < TM_MAX_ROWS)
    {
      char **f = csv_record (&p);

      for (int col = 0; f[col] != NULL && col < TM_MAX_COLS; col++)
        if (f[col][0] != '\0')
          tm_sheet_set_input (sheet, row, col, f[col]);
      g_strfreev (f);
      row++;
    }
}

static void
csv_field (GString *out, const char *text)
{
  if (strpbrk (text, ",\"\n\r") == NULL)
    {
      g_string_append (out, text);
      return;
    }
  g_string_append_c (out, '"');
  for (const char *s = text; *s != '\0'; s++)
    {
      if (*s == '"')
        g_string_append_c (out, '"');
      g_string_append_c (out, *s);
    }
  g_string_append_c (out, '"');
}

/* Values, as the grid shows them; a CSV file has no place for formulas
 * that the next program would not take for text. */
static char *
save_csv (TmSheet *sheet)
{
  GString *out = g_string_new (NULL);
  TmRange used;

  if (!tm_sheet_used_range (sheet, &used))
    return g_string_free (out, FALSE);
  for (int r = 0; r <= used.row1; r++)
    {
      for (int c = 0; c <= used.col1; c++)
        {
          char *text = tm_sheet_get_display (sheet, r, c);

          if (c > 0)
            g_string_append_c (out, ',');
          csv_field (out, text);
          g_free (text);
        }
      g_string_append (out, "\r\n");
    }
  return g_string_free (out, FALSE);
}

gboolean
tm_file_load (TmSheet *sheet, const char *path, GError **error)
{
  char *contents;
  gboolean ok;

  if (!g_file_get_contents (path, &contents, NULL, error))
    return FALSE;
  if (!g_utf8_validate (contents, -1, NULL))
    {
      /* Text from older programs is Latin-1 more often than anything. */
      char *utf8 = g_convert (contents, -1, "UTF-8", "WINDOWS-1252", NULL, NULL, NULL);

      if (utf8 != NULL)
        {
          g_free (contents);
          contents = utf8;
        }
    }
  if (is_csv (path))
    {
      load_csv (sheet, contents);
      ok = TRUE;
    }
  else
    ok = load_tm (sheet, contents, path, error);
  g_free (contents);
  if (ok)
    {
      tm_sheet_recalc (sheet);
      tm_sheet_set_modified (sheet, FALSE);
    }
  return ok;
}

gboolean
tm_file_save (TmSheet *sheet, const char *path, GError **error)
{
  char *text = is_csv (path) ? save_csv (sheet) : save_tm (sheet);
  gboolean ok = g_file_set_contents (path, text, -1, error);

  g_free (text);
  if (ok && !is_csv (path))
    tm_sheet_set_modified (sheet, FALSE);
  return ok;
}

gboolean
tm_file_export_samples (TmSheet *sheet, const char *path, GError **error)
{
  TmSim *sim = tm_sheet_get_sim (sheet);
  GString *out;
  TmRef *cells;
  const double **cols;
  int n, iterations;
  gboolean ok;

  if (sim == NULL)
    {
      g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
                   "there is no simulation to export: run one first");
      return FALSE;
    }
  cells = tm_sim_cells (sim, &n);
  cols = g_new (const double *, MAX (n, 1));
  iterations = tm_sim_iterations (sim);
  out = g_string_new ("iteration");
  for (int i = 0; i < n; i++)
    {
      char *name = tm_ref_name (cells[i].row, cells[i].col);
      int k;

      g_string_append_printf (out, ",%s", name);
      g_free (name);
      cols[i] = tm_sim_samples (sim, cells[i].row, cells[i].col, FALSE, &k);
    }
  g_string_append (out, "\r\n");
  for (int it = 0; it < iterations; it++)
    {
      g_string_append_printf (out, "%d", it + 1);
      for (int i = 0; i < n; i++)
        {
          char buf[G_ASCII_DTOSTR_BUF_SIZE];

          g_string_append_c (out, ',');
          if (!isnan (cols[i][it]))
            g_string_append (out, g_ascii_formatd (buf, sizeof buf, "%.15g", cols[i][it]));
        }
      g_string_append (out, "\r\n");
    }
  ok = g_file_set_contents (path, out->str, (gssize) out->len, error);
  g_string_free (out, TRUE);
  g_free (cols);
  g_free (cells);
  return ok;
}
