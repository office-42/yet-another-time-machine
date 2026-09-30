/* tm-file.c - reading and writing sheets
 *
 * Copyright (C) 2026 The timemachine authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "tm-file.h"
#include "tm-sources.h"

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
      case '\r': g_string_append (out, "\\r"); break;
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
            case 'r': g_string_append_c (out, '\r'); break;
            default:  g_string_append_c (out, *p); break;
            }
          continue;
        }
      g_string_append_c (out, *p);
    }
  return g_string_free (out, FALSE);
}

/* ---- .tm -------------------------------------------------------------- */

/* A source's path as the file has it, resolved against the file's own
 * folder, so that a model and its pictures can move together. */
static char *
resolve (const char *tm_path, const char *path)
{
  char *dir, *full;

  if (g_path_is_absolute (path))
    return g_strdup (path);
  dir = g_path_get_dirname (tm_path);
  full = g_build_filename (dir, path, NULL);
  g_free (dir);
  return full;
}

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

      /* A \r from Windows goes; spaces stay, for a cell that ends in one. */
      if (*line != '\0' && line[strlen (line) - 1] == '\r')
        line[strlen (line) - 1] = '\0';
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
      else if ((strcmp (f[0], "image") == 0 || strcmp (f[0], "map") == 0) && nf == 3)
        {
          /* image NAME PATH [west south east north] */
          char **rest = g_strsplit (f[2], "\t", -1);
          char *name = unescape (f[1]);
          char *where = unescape (rest[0]);
          char *full = resolve (path, where);
          GError *e = NULL;
          char *used = tm_sheet_load_source (sheet, full, name, &e);

          if (used == NULL)
            {
              /* A missing picture is reported, not fatal: the rest of the
               * model still loads, and its formulas say #NAME?. */
              g_warning ("%s", e->message);
              g_error_free (e);
            }
          else if (g_strv_length (rest) >= 5)
            {
              TmImage *image = tm_sheet_get_image (sheet, used);
              if (image != NULL)
                {
                  image->has_bounds = TRUE;
                  image->west = g_ascii_strtod (rest[1], NULL);
                  image->south = g_ascii_strtod (rest[2], NULL);
                  image->east = g_ascii_strtod (rest[3], NULL);
                  image->north = g_ascii_strtod (rest[4], NULL);
                }
            }
          g_free (used);
          g_free (full);
          g_free (where);
          g_free (name);
          g_strfreev (rest);
        }
      else if (strcmp (f[0], "format") == 0 && nf == 3)
        {
          TmRef ref;

          if (tm_ref_parse (f[1], &ref))
            {
              char *format = unescape (f[2]);
              tm_sheet_set_format (sheet, ref.row, ref.col, format);
              g_free (format);
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
      else if (strcmp (f[0], "sampling") == 0 && nf >= 2)
        tm_sheet_set_sampling (sheet, strcmp (f[1], "latin") == 0
                                      ? TM_SAMPLING_LATIN_HYPERCUBE : TM_SAMPLING_MONTE_CARLO);
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

static void
save_format (int row, int col, const char *format, gpointer data)
{
  GString *out = data;
  char *name = tm_ref_name (row, col);
  char *text = escape (format);

  g_string_append_printf (out, "format\t%s\t%s\n", name, text);
  g_free (name);
  g_free (text);
}

/* A source's path relative to the file's folder when it is inside it. */
static char *
relative (const char *tm_path, const char *path)
{
  char *dir = g_path_get_dirname (tm_path);
  char *abs_dir = g_canonicalize_filename (dir, NULL);
  char *abs_path = g_canonicalize_filename (path, NULL);
  char *out;
  gsize n = strlen (abs_dir);

  if (g_str_has_prefix (abs_path, abs_dir) && abs_path[n] == G_DIR_SEPARATOR)
    out = g_strdup (abs_path + n + 1);
  else
    out = g_strdup (abs_path);
  g_free (dir);
  g_free (abs_dir);
  g_free (abs_path);
  return out;
}

static void
save_sources (TmSheet *sheet, GString *out, const char *tm_path)
{
  char **images = tm_sheet_image_names (sheet);
  char **maps = tm_sheet_map_names (sheet);

  for (int i = 0; images[i] != NULL; i++)
    {
      TmImage *image = tm_sheet_get_image (sheet, images[i]);
      char *rel = relative (tm_path, tm_sheet_source_path (sheet, images[i]));
      char *name = escape (images[i]), *where = escape (rel);

      g_string_append_printf (out, "image\t%s\t%s", name, where);
      if (image->has_bounds)
        {
          char b[4][G_ASCII_DTOSTR_BUF_SIZE];
          g_string_append_printf (out, "\t%s\t%s\t%s\t%s",
                                  g_ascii_dtostr (b[0], sizeof b[0], image->west),
                                  g_ascii_dtostr (b[1], sizeof b[1], image->south),
                                  g_ascii_dtostr (b[2], sizeof b[2], image->east),
                                  g_ascii_dtostr (b[3], sizeof b[3], image->north));
        }
      g_string_append_c (out, '\n');
      g_free (rel);
      g_free (name);
      g_free (where);
    }
  for (int i = 0; maps[i] != NULL; i++)
    {
      char *rel = relative (tm_path, tm_sheet_source_path (sheet, maps[i]));
      char *name = escape (maps[i]), *where = escape (rel);

      g_string_append_printf (out, "map\t%s\t%s\n", name, where);
      g_free (rel);
      g_free (name);
      g_free (where);
    }
  g_strfreev (images);
  g_strfreev (maps);
}

static char *
save_tm (TmSheet *sheet, const char *tm_path)
{
  GString *out = g_string_new ("timemachine 1\n");

  g_string_append_printf (out, "iterations\t%d\n", tm_sheet_iterations (sheet));
  g_string_append_printf (out, "seed\t%" G_GUINT64_FORMAT "\n", tm_sheet_seed (sheet));
  if (tm_sheet_sampling (sheet) == TM_SAMPLING_LATIN_HYPERCUBE)
    g_string_append (out, "sampling\tlatin\n");
  for (int c = 0; c < TM_MAX_COLS; c++)
    if (tm_sheet_col_width (sheet, c) != TM_DEFAULT_COL_WIDTH)
      {
        char name[8];
        tm_col_name (c, name, sizeof name);
        g_string_append_printf (out, "width\t%s\t%d\n", name, tm_sheet_col_width (sheet, c));
      }
  save_sources (sheet, out, tm_path);
  tm_sheet_foreach_format (sheet, save_format, out);
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

/* Values: text as the grid shows it, numbers in full; a CSV file has no
 * place for formulas that the next program would not take for text. */
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
          /* Numbers as numbers, not as their format shows them: "1,235"
           * or "12%" in a CSV file is text to the next program. */
          const TmValue *v = tm_sheet_get_value (sheet, r, c);
          char *text = v->type == TM_VALUE_NUMBER ? g_strdup_printf ("%.17g", v->as.number)
                                                  : tm_sheet_get_display (sheet, r, c);

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
      tm_sheet_forget_undo (sheet);
      tm_sheet_recalc (sheet);
      tm_sheet_set_modified (sheet, FALSE);
    }
  return ok;
}

gboolean
tm_file_save (TmSheet *sheet, const char *path, GError **error)
{
  char *text = is_csv (path) ? save_csv (sheet) : save_tm (sheet, path);
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
