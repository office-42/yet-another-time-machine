/* calc-main.c - drive the timemachine engine from a terminal
 *
 * Copyright (C) 2026 The timemachine authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * Reads lines like these from standard input, after loading the file named
 * on the command line if there is one:
 *
 *     A1 = 100                 set a cell
 *     B1 = =RAND.PERT(80,100,150)*A1
 *     B1                       show one: name, value, what was typed
 *     dump                     show the used range
 *     simulate                 run the simulation
 *     stats B1                 what the futures of B1 look like
 *     histogram B1 [bins]      ... drawn in the terminal
 *     fan B1:M1                percentile bands along a row or column
 *     iterations 5000          simulation settings
 *     seed 42
 *     draws 7                  seed the ordinary recalculation's draws
 *     recalc                   work every formula out again
 *     redraw                   ... with the next draw from every random cell
 *     format B1:B9 0.0%        a number format; no code for General
 *     undo                     redo
 *     filldown A1:A9           fillright A1:F1
 *     copy A1:B2 D1            clear A1:B2
 *     load FILE                save FILE          export FILE
 *     functions                every function, with its syntax
 *
 * It exists so the engine can be exercised with no window, and so that
 * the window is the only thing the window has to get right.
 */

#include "tm-sheet.h"
#include "tm-file.h"
#include "tm-eval.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
show (TmSheet *sheet, int row, int col)
{
  char *name = tm_ref_name (row, col);
  char *text = tm_sheet_get_display (sheet, row, col);
  const char *input = tm_sheet_get_input (sheet, row, col);

  printf ("%s\t%s\t%s\n", name, text, input != NULL ? input : "");
  g_free (name);
  g_free (text);
}

static void
dump (TmSheet *sheet)
{
  TmRange used;

  if (!tm_sheet_used_range (sheet, &used))
    {
      printf ("(empty)\n");
      return;
    }
  printf ("      ");
  for (int c = used.col0; c <= used.col1 && c < used.col0 + 26; c++)
    {
      char name[8];
      printf ("%-14s", tm_col_name (c, name, sizeof name));
    }
  printf ("\n");
  for (int r = used.row0; r <= used.row1 && r < used.row0 + 200; r++)
    {
      printf ("%-6d", r + 1);
      for (int c = used.col0; c <= used.col1 && c < used.col0 + 26; c++)
        {
          char *text = tm_sheet_get_display (sheet, r, c);

          if (g_utf8_strlen (text, -1) > 13)
            {
              char *cut = g_utf8_substring (text, 0, 12);
              printf ("%s~ ", cut);
              g_free (cut);
            }
          else
            printf ("%-14s", text);
          g_free (text);
        }
      printf ("\n");
    }
}

static gboolean
parse_ref (const char *text, TmRef *ref)
{
  if (tm_ref_parse (text, ref))
    return TRUE;
  fprintf (stderr, "not a cell: %s\n", text);
  return FALSE;
}

static gboolean
parse_range (const char *text, TmRange *range)
{
  if (tm_range_parse (text, range))
    return TRUE;
  fprintf (stderr, "not a range: %s\n", text);
  return FALSE;
}

static void
stats (TmSheet *sheet, const char *arg)
{
  TmRef ref;
  TmSimStats s;
  TmSim *sim = tm_sheet_get_sim (sheet);

  if (!parse_ref (arg, &ref))
    return;
  if (sim == NULL || !tm_sim_stats (sim, ref.row, ref.col, &s))
    {
      printf ("%s\tno samples: not uncertain, or not simulated yet\n", arg);
      return;
    }
  printf ("%s\titerations=%d valid=%d mean=%.6g sd=%.6g se=%.3g\n",
          arg, s.iterations, s.valid, s.mean, s.sd, s.se);
  printf ("%s\tmin=%.6g p5=%.6g p10=%.6g p25=%.6g p50=%.6g p75=%.6g p90=%.6g p95=%.6g max=%.6g\n",
          arg, s.min, s.p5, s.p10, s.p25, s.p50, s.p75, s.p90, s.p95, s.max);
}

static void
histogram (TmSheet *sheet, const char *arg, int bins)
{
  TmRef ref;
  TmSim *sim = tm_sheet_get_sim (sheet);
  double lo, hi;
  int *counts, most = 0;

  if (!parse_ref (arg, &ref))
    return;
  bins = CLAMP (bins, 2, 100);
  counts = g_new0 (int, bins);
  if (sim == NULL || !tm_sim_histogram (sim, ref.row, ref.col, bins, &lo, &hi, counts))
    {
      printf ("%s\tno samples\n", arg);
      g_free (counts);
      return;
    }
  for (int i = 0; i < bins; i++)
    most = MAX (most, counts[i]);
  for (int i = 0; i < bins; i++)
    {
      double a = lo + (hi - lo) * i / bins;
      int w = most > 0 ? (int) lround (50.0 * counts[i] / most) : 0;

      printf ("%12.5g | ", a);
      for (int k = 0; k < w; k++)
        putchar ('#');
      printf (" %d\n", counts[i]);
    }
  g_free (counts);
}

static void
fan (TmSheet *sheet, const char *arg)
{
  TmRange r;
  TmSim *sim = tm_sheet_get_sim (sheet);

  if (!parse_range (arg, &r))
    return;
  printf ("cell\tp5\tp25\tp50\tp75\tp95\n");
  for (int row = r.row0; row <= r.row1; row++)
    for (int col = r.col0; col <= r.col1; col++)
      {
        TmSimStats s;
        char *name = tm_ref_name (row, col);

        if (sim != NULL && tm_sim_stats (sim, row, col, &s))
          printf ("%s\t%.6g\t%.6g\t%.6g\t%.6g\t%.6g\n", name, s.p5, s.p25, s.p50, s.p75, s.p95);
        else
          printf ("%s\t-\n", name);
        g_free (name);
      }
}

static void
functions (void)
{
  int n;
  const TmFunction *const *list = tm_function_list (&n);

  for (int i = 0; i < n; i++)
    printf ("%-14s %s\n    %s\n", list[i]->category, list[i]->syntax, list[i]->help);
}

static gboolean
progress (int done, int total, gpointer data)
{
  return TRUE;
}

/* Returns FALSE on a line it could not make sense of. */
static gboolean
run_line (TmSheet *sheet, char *line)
{
  char *eq, *arg;
  TmRef ref;
  TmRange range;
  GError *error = NULL;

  g_strstrip (line);
  if (*line == '\0' || *line == '#')
    return TRUE;

  /* "A1 = input": the first " = " or "=" after a reference.  Everything
   * after it, including a leading "=", is what the cell is set to. */
  eq = strchr (line, '=');
  if (eq != NULL)
    {
      char *name = g_strndup (line, (gsize) (eq - line));

      g_strstrip (name);
      if (tm_ref_parse (name, &ref))
        {
          char *input = eq + 1;

          while (*input == ' ')
            input++;
          tm_sheet_set_input (sheet, ref.row, ref.col, input);
          tm_sheet_recalc (sheet);
          g_free (name);
          return TRUE;
        }
      g_free (name);
    }

  if (tm_ref_parse (line, &ref))
    {
      show (sheet, ref.row, ref.col);
      return TRUE;
    }

  arg = strchr (line, ' ');
  if (arg != NULL)
    {
      *arg++ = '\0';
      while (*arg == ' ')
        arg++;
    }

  if (strcmp (line, "dump") == 0)
    dump (sheet);
  else if (strcmp (line, "recalc") == 0)
    tm_sheet_recalc (sheet);
  else if (strcmp (line, "redraw") == 0)
    tm_sheet_redraw (sheet);
  else if (strcmp (line, "undo") == 0 || strcmp (line, "redo") == 0)
    {
      if (!(line[0] == 'u' ? tm_sheet_undo (sheet) : tm_sheet_redo (sheet)))
        printf ("nothing to %s\n", line);
      tm_sheet_recalc (sheet);
    }
  else if (strcmp (line, "functions") == 0)
    functions ();
  else if (strcmp (line, "simulate") == 0)
    {
      TmSim *sim;

      if (arg != NULL)
        tm_sheet_set_iterations (sheet, atoi (arg));
      tm_sheet_simulate (sheet, progress, NULL);
      sim = tm_sheet_get_sim (sheet);
      printf ("simulated %d iterations, seed %" G_GUINT64_FORMAT ", %d cells kept\n",
              tm_sim_iterations (sim), tm_sim_seed (sim), tm_sim_n_tracked (sim));
      fprintf (stderr, "(%.3f s)\n", tm_sim_seconds (sim));
    }
  else if (arg == NULL)
    {
      fprintf (stderr, "not understood: %s\n", line);
      return FALSE;
    }
  else if (strcmp (line, "stats") == 0)
    stats (sheet, arg);
  else if (strcmp (line, "histogram") == 0)
    {
      char *bins = strchr (arg, ' ');
      if (bins != NULL)
        *bins++ = '\0';
      histogram (sheet, arg, bins != NULL ? atoi (bins) : 20);
    }
  else if (strcmp (line, "fan") == 0)
    fan (sheet, arg);
  else if (strcmp (line, "format") == 0)
    {
      char *code = strchr (arg, ' ');

      if (code != NULL)
        *code++ = '\0';
      if (!parse_range (arg, &range))
        return FALSE;
      tm_sheet_format_range (sheet, &range, code != NULL ? g_strstrip (code) : NULL);
    }
  else if (strcmp (line, "iterations") == 0)
    tm_sheet_set_iterations (sheet, atoi (arg));
  else if (strcmp (line, "seed") == 0)
    tm_sheet_set_seed (sheet, g_ascii_strtoull (arg, NULL, 10));
  else if (strcmp (line, "draws") == 0)
    {
      tm_sheet_seed_draws (sheet, g_ascii_strtoull (arg, NULL, 10));
      tm_sheet_recalc (sheet);
    }
  else if (strcmp (line, "filldown") == 0 || strcmp (line, "fillright") == 0
           || strcmp (line, "clear") == 0)
    {
      if (!parse_range (arg, &range))
        return FALSE;
      if (line[0] == 'c')
        tm_sheet_clear_range (sheet, &range);
      else if (line[4] == 'd')
        tm_sheet_fill_down (sheet, &range);
      else
        tm_sheet_fill_right (sheet, &range);
      tm_sheet_recalc (sheet);
    }
  else if (strcmp (line, "copy") == 0)
    {
      char *dest = strchr (arg, ' ');

      if (dest == NULL)
        {
          fprintf (stderr, "copy RANGE CELL\n");
          return FALSE;
        }
      *dest++ = '\0';
      if (!parse_range (arg, &range) || !parse_ref (g_strstrip (dest), &ref))
        return FALSE;
      tm_sheet_copy_range (sheet, &range, ref.row, ref.col);
      tm_sheet_recalc (sheet);
    }
  else if (strcmp (line, "load") == 0)
    {
      if (!tm_file_load (sheet, arg, &error))
        goto file_error;
    }
  else if (strcmp (line, "save") == 0)
    {
      if (!tm_file_save (sheet, arg, &error))
        goto file_error;
    }
  else if (strcmp (line, "export") == 0)
    {
      if (!tm_file_export_samples (sheet, arg, &error))
        goto file_error;
    }
  else
    {
      fprintf (stderr, "not understood: %s %s\n", line, arg);
      return FALSE;
    }
  return TRUE;

file_error:
  fprintf (stderr, "%s\n", error->message);
  g_error_free (error);
  return FALSE;
}

int
main (int argc, char *argv[])
{
  TmSheet *sheet = tm_sheet_new ();
  char buf[65536];
  int status = 0;

  /* Scripts and the smoke test compare output, so the draws the grid
   * would show are the same every run unless a script says otherwise. */
  tm_sheet_seed_draws (sheet, 1);

  if (argc > 1)
    {
      GError *error = NULL;

      if (strcmp (argv[1], "--help") == 0 || strcmp (argv[1], "-h") == 0)
        {
          printf ("usage: timemachine-calc [FILE] < script\n"
                  "Reads commands from standard input; see the top of calc-main.c.\n");
          return 0;
        }
      if (strcmp (argv[1], "--version") == 0)
        {
          printf ("timemachine-calc %s\n", TM_VERSION);
          return 0;
        }
      if (!tm_file_load (sheet, argv[1], &error))
        {
          fprintf (stderr, "timemachine-calc: %s\n", error->message);
          g_error_free (error);
          tm_sheet_free (sheet);
          return 1;
        }
    }

  while (fgets (buf, sizeof buf, stdin) != NULL)
    if (!run_line (sheet, buf))
      status = 1;

  tm_sheet_free (sheet);
  return status;
}
