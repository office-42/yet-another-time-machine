/* tm-window.c - the main window
 *
 * Copyright (C) 2026 The timemachine authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "tm-window.h"
#include "tm-grid.h"
#include "tm-chart.h"
#include "tm-file.h"
#include "tm-eval.h"

#include <math.h>
#include <string.h>

/* The figures under the chart, in the order they are laid out: two
 * columns of name and value. */
enum {
  STAT_MEAN, STAT_P5,
  STAT_SD, STAT_P25,
  STAT_SE, STAT_P50,
  STAT_MIN, STAT_P75,
  STAT_MAX, STAT_P95,
  STAT_BELOW, STAT_VALID,
  N_STATS
};

static const char *const STAT_NAMES[N_STATS] = {
  "Mean", "P5",
  "Std dev", "P25",
  "Std error", "Median",
  "Min", "P75",
  "Max", "P95",
  "P(< 0)", "Valid",
};

struct _TmWindow {
  GtkApplicationWindow parent_instance;

  TmSheet *sheet;
  char *path;

  GtkWidget *grid;
  GtkWidget *name_entry;
  GtkWidget *formula_entry;
  GtkWidget *content;
  GtkWidget *chart;
  GtkWidget *panel_title;
  GtkWidget *panel_subtitle;
  GtkWidget *stat_values[N_STATS];
  GtkWidget *stats_box;
  GtkWidget *status;
  GtkWidget *progress;
  GtkWidget *stop_button;
  GtkWidget *iter_spin;
  GtkWidget *seed_spin;

  gboolean editing;         /* the formula bar holds an edit not yet made */
  gboolean setting_text;    /* the formula bar is being filled, not typed in */
  TmRef edit_ref;           /* the cell the edit is for */

  gboolean simulating;
  gboolean stop_requested;

  /* What Copy last put on the clipboard, so that pasting it back here
   * can move formulas' references rather than paste their values. */
  char *clip_text;
  char **clip_inputs;
  TmRange clip_range;
  gboolean clip_cut;
};

G_DEFINE_FINAL_TYPE (TmWindow, tm_window, GTK_TYPE_APPLICATION_WINDOW)

static void update_panel (TmWindow *self);
static void update_formula_bar (TmWindow *self);

/* ---- Small things ----------------------------------------------------- */

static void
update_title (TmWindow *self)
{
  char *base = self->path != NULL ? g_path_get_basename (self->path) : g_strdup ("Untitled");
  char *title = g_strdup_printf ("%s%s — Time Machine",
                                 tm_sheet_modified (self->sheet) ? "*" : "", base);

  gtk_window_set_title (GTK_WINDOW (self), title);
  g_free (title);
  g_free (base);
}

static void
set_status (TmWindow *self, const char *text)
{
  gtk_label_set_text (GTK_LABEL (self->status), text);
}

static void
sheet_changed (TmWindow *self)
{
  tm_sheet_recalc (self->sheet);
  tm_grid_refresh (TM_GRID (self->grid));
  update_panel (self);
  update_title (self);
}

static char *
format_stat (double v)
{
  char buf[64];

  if (isnan (v))
    return g_strdup ("—");
  if (fabs (v) >= 1e12 || (fabs (v) < 1e-3 && v != 0))
    g_snprintf (buf, sizeof buf, "%.4g", v);
  else if (fabs (v) >= 1000)
    g_snprintf (buf, sizeof buf, "%.0f", v);
  else
    g_snprintf (buf, sizeof buf, "%.4g", v);
  /* Thousands separated, for reading at a glance. */
  {
    GString *out = g_string_new (NULL);
    const char *s = buf, *e = strpbrk (buf, ".eE");
    int int_len;

    if (*s == '-')
      g_string_append_c (out, *s++);
    int_len = (int) ((e != NULL ? e : buf + strlen (buf)) - s);
    if (strchr (buf, 'e') != NULL || int_len <= 4)
      {
        g_string_free (out, TRUE);
        return g_strdup (buf);
      }
    for (int i = 0; i < int_len; i++)
      {
        if (i > 0 && (int_len - i) % 3 == 0)
          g_string_append_c (out, ',');
        g_string_append_c (out, s[i]);
      }
    g_string_append (out, s + int_len);
    return g_string_free (out, FALSE);
  }
}

/* The label a cell goes by: text to its left, or above it. */
static char *
cell_caption (TmSheet *sheet, int row, int col)
{
  for (int c = col - 1; c >= 0 && c >= col - 3; c--)
    {
      const TmValue *v = tm_sheet_get_value (sheet, row, c);
      if (v->type == TM_VALUE_TEXT)
        return g_strdup (v->as.text);
      if (v->type != TM_VALUE_EMPTY)
        break;
    }
  for (int r = row - 1; r >= 0 && r >= row - 2; r--)
    {
      const TmValue *v = tm_sheet_get_value (sheet, r, col);
      if (v->type == TM_VALUE_TEXT)
        return g_strdup (v->as.text);
      if (v->type != TM_VALUE_EMPTY)
        break;
    }
  return NULL;
}

/* ---- The forecast panel ----------------------------------------------- */

static void
update_panel (TmWindow *self)
{
  TmRange sel;
  TmSim *sim = tm_sheet_get_sim (self->sheet);
  char *name, *caption, *title;

  tm_grid_get_selection (TM_GRID (self->grid), &sel);
  tm_chart_show (TM_CHART (self->chart), self->sheet, &sel);

  name = tm_range_name (&sel);
  caption = cell_caption (self->sheet, sel.row0, sel.col0);
  title = caption != NULL ? g_strdup_printf ("%s  ·  %s", name, caption) : g_strdup (name);
  gtk_label_set_text (GTK_LABEL (self->panel_title), title);
  g_free (title);
  g_free (caption);
  g_free (name);

  if (sim == NULL)
    gtk_label_set_text (GTK_LABEL (self->panel_subtitle), "Not simulated yet");
  else
    {
      char *sub = g_strdup_printf ("%d futures, seed %" G_GUINT64_FORMAT "%s",
                                   tm_sim_iterations (sim), tm_sim_seed (sim),
                                   tm_sim_stale (sim) ? "  ·  out of date: press F5" : "");
      gtk_label_set_text (GTK_LABEL (self->panel_subtitle), sub);
      g_free (sub);
    }

  {
    TmSimStats s;
    gboolean single = sel.row0 == sel.row1 && sel.col0 == sel.col1;
    gboolean have = single && sim != NULL && tm_sim_stats (sim, sel.row0, sel.col0, &s);

    gtk_widget_set_visible (self->stats_box, have);
    if (have)
      {
        double values[N_STATS];
        int n, below = 0;
        const double *x = tm_sim_samples (sim, sel.row0, sel.col0, TRUE, &n);

        for (int i = 0; i < n && x[i] < 0; i++)
          below++;
        values[STAT_MEAN] = s.mean;
        values[STAT_SD] = s.sd;
        values[STAT_SE] = s.se;
        values[STAT_MIN] = s.min;
        values[STAT_MAX] = s.max;
        values[STAT_P5] = s.p5;
        values[STAT_P25] = s.p25;
        values[STAT_P50] = s.p50;
        values[STAT_P75] = s.p75;
        values[STAT_P95] = s.p95;
        values[STAT_BELOW] = n > 0 ? (double) below / n : NAN;
        values[STAT_VALID] = s.valid;
        for (int i = 0; i < N_STATS; i++)
          {
            char *t;

            if (i == STAT_BELOW)
              t = isnan (values[i]) ? g_strdup ("—") : g_strdup_printf ("%.1f%%", 100 * values[i]);
            else if (i == STAT_VALID)
              t = g_strdup_printf ("%d / %d", s.valid, s.iterations);
            else
              t = format_stat (values[i]);
            gtk_label_set_text (GTK_LABEL (self->stat_values[i]), t);
            g_free (t);
          }
      }
  }
}

/* ---- The formula bar -------------------------------------------------- */

static void
update_formula_bar (TmWindow *self)
{
  TmRange sel;
  TmRef cur = tm_grid_get_cursor (TM_GRID (self->grid));
  const char *input = tm_sheet_get_input (self->sheet, cur.row, cur.col);
  char *name;

  tm_grid_get_selection (TM_GRID (self->grid), &sel);
  name = (sel.row0 == sel.row1 && sel.col0 == sel.col1) ? tm_ref_name (cur.row, cur.col)
                                                        : tm_range_name (&sel);
  gtk_editable_set_text (GTK_EDITABLE (self->name_entry), name);
  g_free (name);

  self->setting_text = TRUE;
  gtk_editable_set_text (GTK_EDITABLE (self->formula_entry), input != NULL ? input : "");
  self->setting_text = FALSE;
  self->editing = FALSE;
}

static void
commit_edit (TmWindow *self)
{
  const char *text;

  if (!self->editing)
    return;
  self->editing = FALSE;
  text = gtk_editable_get_text (GTK_EDITABLE (self->formula_entry));
  if (g_strcmp0 (text, tm_sheet_get_input (self->sheet, self->edit_ref.row, self->edit_ref.col)) == 0
      || (*text == '\0' && tm_sheet_get_input (self->sheet, self->edit_ref.row, self->edit_ref.col) == NULL))
    return;
  tm_sheet_set_input (self->sheet, self->edit_ref.row, self->edit_ref.col, text);
  sheet_changed (self);
}

static void
cancel_edit (TmWindow *self)
{
  self->editing = FALSE;
  update_formula_bar (self);
  gtk_widget_grab_focus (self->grid);
}

static void
on_selection_changed (TmGrid *grid, TmWindow *self)
{
  if (self->editing)
    commit_edit (self);
  update_formula_bar (self);
  update_panel (self);
}

static void
on_grid_edit (TmGrid *grid, const char *text, TmWindow *self)
{
  self->edit_ref = tm_grid_get_cursor (grid);
  if (text != NULL)
    {
      self->setting_text = TRUE;
      gtk_editable_set_text (GTK_EDITABLE (self->formula_entry), text);
      self->setting_text = FALSE;
    }
  self->editing = TRUE;
  gtk_entry_grab_focus_without_selecting (GTK_ENTRY (self->formula_entry));
  gtk_editable_set_position (GTK_EDITABLE (self->formula_entry), -1);
}

static void
on_formula_changed (GtkEditable *editable, TmWindow *self)
{
  if (self->setting_text || self->editing)
    return;
  self->editing = TRUE;
  self->edit_ref = tm_grid_get_cursor (TM_GRID (self->grid));
}

static void
on_formula_activate (GtkEntry *entry, TmWindow *self)
{
  commit_edit (self);
  gtk_widget_grab_focus (self->grid);
  tm_grid_move_cursor (TM_GRID (self->grid), 1, 0, FALSE);
}

static gboolean
on_formula_key (GtkEventControllerKey *controller, guint keyval, guint keycode,
                GdkModifierType state, TmWindow *self)
{
  int drow = 0, dcol = 0;

  switch (keyval)
    {
    case GDK_KEY_Escape:
      cancel_edit (self);
      return TRUE;
    case GDK_KEY_Tab:          dcol = 1; break;
    case GDK_KEY_ISO_Left_Tab: dcol = -1; break;
    case GDK_KEY_Up:           drow = -1; break;
    case GDK_KEY_Down:         drow = 1; break;
    case GDK_KEY_Return:
    case GDK_KEY_KP_Enter:
      drow = (state & GDK_SHIFT_MASK) ? -1 : 1;
      break;
    default:
      return FALSE;
    }
  commit_edit (self);
  gtk_widget_grab_focus (self->grid);
  tm_grid_move_cursor (TM_GRID (self->grid), drow, dcol, FALSE);
  return TRUE;
}

static void
on_name_activate (GtkEntry *entry, TmWindow *self)
{
  TmRange r;
  char *text = g_strstrip (g_strdup (gtk_editable_get_text (GTK_EDITABLE (entry))));

  if (tm_range_parse (text, &r))
    tm_grid_select (TM_GRID (self->grid), &r);
  else
    update_formula_bar (self);
  g_free (text);
  gtk_widget_grab_focus (self->grid);
}

/* ---- Simulation ------------------------------------------------------- */

static gboolean
on_progress (int done, int total, gpointer data)
{
  TmWindow *self = data;

  gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (self->progress), (double) done / total);
  /* Let the window draw and the Stop button be pressed. */
  while (g_main_context_pending (NULL))
    g_main_context_iteration (NULL, FALSE);
  return !self->stop_requested;
}

void
tm_window_simulate (TmWindow *self)
{
  TmSim *sim;
  gboolean done;
  char *msg;

  if (self->simulating)
    return;
  commit_edit (self);
  tm_sheet_set_iterations (self->sheet, gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (self->iter_spin)));
  tm_sheet_set_seed (self->sheet, (guint64) gtk_spin_button_get_value (GTK_SPIN_BUTTON (self->seed_spin)));

  self->simulating = TRUE;
  self->stop_requested = FALSE;
  gtk_widget_set_sensitive (self->content, FALSE);
  gtk_widget_set_visible (self->progress, TRUE);
  gtk_widget_set_visible (self->stop_button, TRUE);
  gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (self->progress), 0);
  set_status (self, "Travelling to the future…");

  done = tm_sheet_simulate (self->sheet, on_progress, self);

  gtk_widget_set_visible (self->progress, FALSE);
  gtk_widget_set_visible (self->stop_button, FALSE);
  gtk_widget_set_sensitive (self->content, TRUE);
  self->simulating = FALSE;

  sim = tm_sheet_get_sim (self->sheet);
  if (done && sim != NULL)
    {
      int n = tm_sim_n_tracked (sim);
      msg = g_strdup_printf ("Simulated %d futures in %.2f s; %d uncertain cell%s.",
                             tm_sim_iterations (sim), tm_sim_seconds (sim),
                             n, n == 1 ? "" : "s");
    }
  else
    msg = g_strdup ("Simulation stopped.");
  set_status (self, msg);
  g_free (msg);
  tm_grid_refresh (TM_GRID (self->grid));
  update_panel (self);
  update_title (self);
  gtk_widget_grab_focus (self->grid);
}

static void
on_stop_clicked (GtkButton *button, TmWindow *self)
{
  self->stop_requested = TRUE;
}

/* ---- Clipboard -------------------------------------------------------- */

static void
clear_clip (TmWindow *self)
{
  g_clear_pointer (&self->clip_text, g_free);
  g_clear_pointer (&self->clip_inputs, g_strfreev);
}

static void
copy_selection (TmWindow *self, gboolean cut)
{
  TmRange sel;
  GString *text = g_string_new (NULL);
  int rows, cols;

  commit_edit (self);
  clear_clip (self);
  tm_grid_get_selection (TM_GRID (self->grid), &sel);
  rows = tm_range_rows (&sel);
  cols = tm_range_cols (&sel);
  self->clip_inputs = g_new0 (char *, rows * cols + 1);
  for (int r = 0; r < rows; r++)
    {
      for (int c = 0; c < cols; c++)
        {
          const char *in = tm_sheet_get_input (self->sheet, sel.row0 + r, sel.col0 + c);
          char *shown = tm_sheet_get_display (self->sheet, sel.row0 + r, sel.col0 + c);

          self->clip_inputs[r * cols + c] = g_strdup (in != NULL ? in : "");
          if (c > 0)
            g_string_append_c (text, '\t');
          g_string_append (text, shown);
          g_free (shown);
        }
      g_string_append_c (text, '\n');
    }
  self->clip_range = sel;
  self->clip_cut = cut;
  self->clip_text = g_string_free (text, FALSE);
  gdk_clipboard_set_text (gtk_widget_get_clipboard (GTK_WIDGET (self)), self->clip_text);
  set_status (self, cut ? "Cut: paste to move." : "Copied.");
}

/* Pastes rows of cells at the cursor, repeating them across a selection
 * that is a whole number of them, as Excel does. */
static void
paste_inputs (TmWindow *self, char **inputs, int rows, int cols, gboolean shift_refs,
              int src_row, int src_col)
{
  TmRange sel;
  int tile_r = 1, tile_c = 1;

  tm_grid_get_selection (TM_GRID (self->grid), &sel);
  if (tm_range_rows (&sel) % rows == 0 && tm_range_cols (&sel) % cols == 0)
    {
      tile_r = tm_range_rows (&sel) / rows;
      tile_c = tm_range_cols (&sel) / cols;
    }
  for (int tr = 0; tr < tile_r; tr++)
    for (int tc = 0; tc < tile_c; tc++)
      for (int r = 0; r < rows; r++)
        for (int c = 0; c < cols; c++)
          {
            int row = sel.row0 + tr * rows + r, col = sel.col0 + tc * cols + c;
            const char *in = inputs[r * cols + c];
            char *moved = shift_refs ? tm_formula_shift (in, row - (src_row + r), col - (src_col + c))
                                     : g_strdup (in);

            tm_sheet_set_input (self->sheet, row, col, moved);
            g_free (moved);
          }
  {
    TmRange done = { sel.row0, sel.col0,
                     sel.row0 + tile_r * rows - 1, sel.col0 + tile_c * cols - 1 };
    tm_grid_select (TM_GRID (self->grid), &done);
  }
}

static void
on_paste_text (GObject *source, GAsyncResult *result, gpointer data)
{
  TmWindow *self = data;
  char *text = gdk_clipboard_read_text_finish (GDK_CLIPBOARD (source), result, NULL);

  if (text == NULL)
    {
      g_object_unref (self);
      return;
    }

  if (self->clip_text != NULL && strcmp (text, self->clip_text) == 0)
    {
      /* Our own copy: the formulas, with their references moved -- or, for
       * a cut, moved as they are and taken away from where they were. */
      int rows = tm_range_rows (&self->clip_range), cols = tm_range_cols (&self->clip_range);

      if (self->clip_cut)
        {
          char **inputs = g_strdupv (self->clip_inputs);
          tm_sheet_clear_range (self->sheet, &self->clip_range);
          paste_inputs (self, inputs, rows, cols, FALSE, 0, 0);
          g_strfreev (inputs);
          clear_clip (self);
        }
      else
        paste_inputs (self, self->clip_inputs, rows, cols, TRUE,
                      self->clip_range.row0, self->clip_range.col0);
    }
  else
    {
      /* Text from somewhere else: tab-separated rows, as every
       * spreadsheet puts them on the clipboard. */
      char **lines = g_strsplit (text, "\n", -1);
      int rows = 0, cols = 0;
      char **inputs;

      for (int i = 0; lines[i] != NULL; i++)
        {
          g_strchomp (lines[i]);
          if (lines[i][0] != '\0' || lines[i + 1] != NULL)
            {
              char **f = g_strsplit (lines[i], "\t", -1);
              cols = MAX (cols, (int) g_strv_length (f));
              g_strfreev (f);
              rows = i + 1;
            }
        }
      while (rows > 0 && lines[rows - 1][0] == '\0')
        rows--;
      if (rows > 0 && cols > 0)
        {
          inputs = g_new0 (char *, rows * cols + 1);
          for (int r = 0; r < rows; r++)
            {
              char **f = g_strsplit (lines[r], "\t", -1);
              for (int c = 0; c < cols; c++)
                inputs[r * cols + c] = g_strdup (c < (int) g_strv_length (f) ? f[c] : "");
              g_strfreev (f);
            }
          paste_inputs (self, inputs, rows, cols, FALSE, 0, 0);
          g_strfreev (inputs);
        }
      g_strfreev (lines);
    }
  g_free (text);
  sheet_changed (self);
  g_object_unref (self);
}

/* ---- Actions ---------------------------------------------------------- */

static void
action_copy (GSimpleAction *a, GVariant *p, gpointer data)
{
  copy_selection (TM_WINDOW (data), FALSE);
}

static void
action_cut (GSimpleAction *a, GVariant *p, gpointer data)
{
  copy_selection (TM_WINDOW (data), TRUE);
}

static void
action_paste (GSimpleAction *a, GVariant *p, gpointer data)
{
  TmWindow *self = data;

  commit_edit (self);
  gdk_clipboard_read_text_async (gtk_widget_get_clipboard (GTK_WIDGET (self)), NULL,
                                 on_paste_text, g_object_ref (self));
}

static void
action_clear (GSimpleAction *a, GVariant *p, gpointer data)
{
  TmWindow *self = data;
  TmRange sel;

  tm_grid_get_selection (TM_GRID (self->grid), &sel);
  tm_sheet_clear_range (self->sheet, &sel);
  sheet_changed (self);
  update_formula_bar (self);
}

static void
action_fill_down (GSimpleAction *a, GVariant *p, gpointer data)
{
  TmWindow *self = data;
  TmRange sel;

  commit_edit (self);
  tm_grid_get_selection (TM_GRID (self->grid), &sel);
  if (sel.row0 == sel.row1)
    return;
  tm_sheet_fill_down (self->sheet, &sel);
  sheet_changed (self);
}

static void
action_fill_right (GSimpleAction *a, GVariant *p, gpointer data)
{
  TmWindow *self = data;
  TmRange sel;

  commit_edit (self);
  tm_grid_get_selection (TM_GRID (self->grid), &sel);
  if (sel.col0 == sel.col1)
    return;
  tm_sheet_fill_right (self->sheet, &sel);
  sheet_changed (self);
}

static void
action_simulate (GSimpleAction *a, GVariant *p, gpointer data)
{
  tm_window_simulate (TM_WINDOW (data));
}

static void
action_recalc (GSimpleAction *a, GVariant *p, gpointer data)
{
  TmWindow *self = data;

  commit_edit (self);
  tm_sheet_recalc (self->sheet);
  tm_grid_refresh (TM_GRID (self->grid));
  update_panel (self);
  set_status (self, "Recalculated: every uncertain cell drew again.");
}

static void
on_seed_changed (GtkSpinButton *spin, TmWindow *self)
{
  tm_sheet_set_seed (self->sheet, (guint64) gtk_spin_button_get_value (spin));
  update_title (self);
}

static void
on_iterations_changed (GtkSpinButton *spin, TmWindow *self)
{
  tm_sheet_set_iterations (self->sheet, gtk_spin_button_get_value_as_int (spin));
  update_title (self);
}

static void
sync_settings (TmWindow *self)
{
  g_signal_handlers_block_by_func (self->iter_spin, on_iterations_changed, self);
  g_signal_handlers_block_by_func (self->seed_spin, on_seed_changed, self);
  gtk_spin_button_set_value (GTK_SPIN_BUTTON (self->iter_spin), tm_sheet_iterations (self->sheet));
  gtk_spin_button_set_value (GTK_SPIN_BUTTON (self->seed_spin), (double) tm_sheet_seed (self->sheet));
  g_signal_handlers_unblock_by_func (self->iter_spin, on_iterations_changed, self);
  g_signal_handlers_unblock_by_func (self->seed_spin, on_seed_changed, self);
}

gboolean
tm_window_load (TmWindow *self, const char *path, GError **error)
{
  if (!tm_file_load (self->sheet, path, error))
    return FALSE;
  g_free (self->path);
  /* A CSV file is data, not a model: saving goes to a .tm of its own. */
  self->path = g_str_has_suffix (path, ".csv") ? NULL : g_strdup (path);
  sync_settings (self);
  tm_grid_set_sheet (TM_GRID (self->grid), self->sheet);
  update_title (self);
  update_panel (self);
  set_status (self, "Press F5 to simulate the futures of the tinted cells.");
  return TRUE;
}

static void
show_error (TmWindow *self, const char *message, const char *detail)
{
  GtkAlertDialog *dialog = gtk_alert_dialog_new ("%s", message);

  if (detail != NULL)
    gtk_alert_dialog_set_detail (dialog, detail);
  gtk_alert_dialog_show (dialog, GTK_WINDOW (self));
  g_object_unref (dialog);
}

static void
on_open_done (GObject *source, GAsyncResult *result, gpointer data)
{
  TmWindow *self = data;
  GFile *file = gtk_file_dialog_open_finish (GTK_FILE_DIALOG (source), result, NULL);

  if (file != NULL)
    {
      GError *error = NULL;
      char *path = g_file_get_path (file);

      if (!tm_window_load (self, path, &error))
        {
          show_error (self, "The file could not be opened.", error->message);
          g_error_free (error);
        }
      g_free (path);
      g_object_unref (file);
    }
  g_object_unref (self);
}

static GListModel *
file_filters (void)
{
  GListStore *filters = g_list_store_new (GTK_TYPE_FILE_FILTER);
  GtkFileFilter *f;

  f = gtk_file_filter_new ();
  gtk_file_filter_set_name (f, "Time Machine sheets and CSV");
  gtk_file_filter_add_pattern (f, "*.tm");
  gtk_file_filter_add_pattern (f, "*.csv");
  g_list_store_append (filters, f);
  g_object_unref (f);
  f = gtk_file_filter_new ();
  gtk_file_filter_set_name (f, "All files");
  gtk_file_filter_add_pattern (f, "*");
  g_list_store_append (filters, f);
  g_object_unref (f);
  return G_LIST_MODEL (filters);
}

static void
action_open (GSimpleAction *a, GVariant *p, gpointer data)
{
  TmWindow *self = data;
  GtkFileDialog *dialog = gtk_file_dialog_new ();
  GListModel *filters = file_filters ();

  gtk_file_dialog_set_filters (dialog, filters);
  gtk_file_dialog_open (dialog, GTK_WINDOW (self), NULL, on_open_done, g_object_ref (self));
  g_object_unref (filters);
  g_object_unref (dialog);
}

static void
action_open_example (GSimpleAction *a, GVariant *p, gpointer data)
{
  TmWindow *self = data;
  char *dir = tm_samples_dir ();
  char *path = g_build_filename (dir, g_variant_get_string (p, NULL), NULL);
  GError *error = NULL;

  if (!tm_window_load (self, path, &error))
    {
      show_error (self, "The example could not be opened.", error->message);
      g_error_free (error);
    }
  else
    {
      /* An example is to be played with, not saved over. */
      g_clear_pointer (&self->path, g_free);
      update_title (self);
    }
  g_free (path);
  g_free (dir);
}

static void
save_to (TmWindow *self, const char *path)
{
  GError *error = NULL;

  commit_edit (self);
  if (!tm_file_save (self->sheet, path, &error))
    {
      show_error (self, "The file could not be saved.", error->message);
      g_error_free (error);
      return;
    }
  if (!g_str_has_suffix (path, ".csv"))
    {
      g_free (self->path);
      self->path = g_strdup (path);
    }
  update_title (self);
  set_status (self, "Saved.");
}

static void
on_save_done (GObject *source, GAsyncResult *result, gpointer data)
{
  TmWindow *self = data;
  GFile *file = gtk_file_dialog_save_finish (GTK_FILE_DIALOG (source), result, NULL);

  if (file != NULL)
    {
      char *path = g_file_get_path (file);
      char *base = g_path_get_basename (path);
      gboolean bare = strchr (base, '.') == NULL;

      g_free (base);
      if (bare)
        {
          char *with = g_strconcat (path, ".tm", NULL);
          g_free (path);
          path = with;
        }
      save_to (self, path);
      g_free (path);
      g_object_unref (file);
    }
  g_object_unref (self);
}

static void
action_save_as (GSimpleAction *a, GVariant *p, gpointer data)
{
  TmWindow *self = data;
  GtkFileDialog *dialog = gtk_file_dialog_new ();
  GListModel *filters = file_filters ();

  gtk_file_dialog_set_filters (dialog, filters);
  gtk_file_dialog_set_initial_name (dialog, "model.tm");
  gtk_file_dialog_save (dialog, GTK_WINDOW (self), NULL, on_save_done, g_object_ref (self));
  g_object_unref (filters);
  g_object_unref (dialog);
}

static void
action_save (GSimpleAction *a, GVariant *p, gpointer data)
{
  TmWindow *self = data;

  if (self->path == NULL)
    action_save_as (a, p, data);
  else
    save_to (self, self->path);
}

static void
on_export_done (GObject *source, GAsyncResult *result, gpointer data)
{
  TmWindow *self = data;
  GFile *file = gtk_file_dialog_save_finish (GTK_FILE_DIALOG (source), result, NULL);

  if (file != NULL)
    {
      char *path = g_file_get_path (file);
      GError *error = NULL;

      if (!tm_file_export_samples (self->sheet, path, &error))
        {
          show_error (self, "The samples could not be exported.", error->message);
          g_error_free (error);
        }
      else
        set_status (self, "Samples exported.");
      g_free (path);
      g_object_unref (file);
    }
  g_object_unref (self);
}

static void
action_export (GSimpleAction *a, GVariant *p, gpointer data)
{
  TmWindow *self = data;
  GtkFileDialog *dialog;

  if (tm_sheet_get_sim (self->sheet) == NULL)
    {
      show_error (self, "There is nothing to export yet.",
                  "Run the simulation (F5) first; its samples are what is exported.");
      return;
    }
  dialog = gtk_file_dialog_new ();
  gtk_file_dialog_set_initial_name (dialog, "samples.csv");
  gtk_file_dialog_save (dialog, GTK_WINDOW (self), NULL, on_export_done, g_object_ref (self));
  g_object_unref (dialog);
}

static void
action_new (GSimpleAction *a, GVariant *p, gpointer data)
{
  TmWindow *self = data;
  GtkApplication *app = gtk_window_get_application (GTK_WINDOW (self));
  TmWindow *win = tm_window_new (app);

  gtk_window_present (GTK_WINDOW (win));
}

/* Help > Functions: every function the engine knows, by category. */
static void
action_functions (GSimpleAction *a, GVariant *p, gpointer data)
{
  TmWindow *self = data;
  GtkWidget *win = gtk_window_new ();
  GtkWidget *scroll = gtk_scrolled_window_new ();
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 2);
  int n;
  const TmFunction *const *list = tm_function_list (&n);
  static const char *const order[] = {
    "Random", "Processes", "Simulation", "Forecasting", "Judgment",
    "Statistics", "Distributions", "Maths", "Logic", "Text", "Lookup", "Finance"
  };

  gtk_window_set_title (GTK_WINDOW (win), "Functions");
  gtk_window_set_transient_for (GTK_WINDOW (win), GTK_WINDOW (self));
  gtk_window_set_default_size (GTK_WINDOW (win), 560, 640);
  gtk_widget_set_margin_start (box, 16);
  gtk_widget_set_margin_end (box, 16);
  gtk_widget_set_margin_top (box, 12);
  gtk_widget_set_margin_bottom (box, 16);

  for (guint k = 0; k < G_N_ELEMENTS (order); k++)
    {
      GtkWidget *head = gtk_label_new (order[k]);

      gtk_widget_add_css_class (head, "title-4");
      gtk_widget_set_halign (head, GTK_ALIGN_START);
      gtk_widget_set_margin_top (head, k == 0 ? 0 : 14);
      gtk_box_append (GTK_BOX (box), head);
      for (int i = 0; i < n; i++)
        {
          GtkWidget *syntax, *help;
          char *markup;

          if (strcmp (list[i]->category, order[k]) != 0)
            continue;
          markup = g_markup_printf_escaped ("<tt>%s</tt>", list[i]->syntax);
          syntax = gtk_label_new (NULL);
          gtk_label_set_markup (GTK_LABEL (syntax), markup);
          gtk_label_set_selectable (GTK_LABEL (syntax), TRUE);
          gtk_widget_set_halign (syntax, GTK_ALIGN_START);
          gtk_widget_set_margin_top (syntax, 4);
          help = gtk_label_new (list[i]->help);
          gtk_label_set_wrap (GTK_LABEL (help), TRUE);
          gtk_label_set_xalign (GTK_LABEL (help), 0);
          gtk_widget_set_margin_start (help, 16);
          gtk_widget_add_css_class (help, "dim-label");
          gtk_box_append (GTK_BOX (box), syntax);
          gtk_box_append (GTK_BOX (box), help);
          g_free (markup);
        }
    }
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scroll), box);
  gtk_window_set_child (GTK_WINDOW (win), scroll);
  gtk_window_present (GTK_WINDOW (win));
}

static void
action_about (GSimpleAction *a, GVariant *p, gpointer data)
{
  static const char *authors[] = { "The timemachine authors", NULL };

  gtk_show_about_dialog (GTK_WINDOW (data),
                         "program-name", "Time Machine",
                         "version", TM_VERSION,
                         "comments", "A spreadsheet for making predictions about the future:\n"
                                     "Monte Carlo simulation, exponential smoothing and "
                                     "calibrated judgment in the shape of Excel.",
                         "license-type", GTK_LICENSE_GPL_3_0,
                         "authors", authors,
                         "logo-icon-name", "net.office42.timemachine",
                         NULL);
}

static const GActionEntry WIN_ACTIONS[] = {
  { "new", action_new, NULL, NULL, NULL, { 0 } },
  { "open", action_open, NULL, NULL, NULL, { 0 } },
  { "open-example", action_open_example, "s", NULL, NULL, { 0 } },
  { "save", action_save, NULL, NULL, NULL, { 0 } },
  { "save-as", action_save_as, NULL, NULL, NULL, { 0 } },
  { "export-samples", action_export, NULL, NULL, NULL, { 0 } },
  { "copy", action_copy, NULL, NULL, NULL, { 0 } },
  { "cut", action_cut, NULL, NULL, NULL, { 0 } },
  { "paste", action_paste, NULL, NULL, NULL, { 0 } },
  { "clear", action_clear, NULL, NULL, NULL, { 0 } },
  { "fill-down", action_fill_down, NULL, NULL, NULL, { 0 } },
  { "fill-right", action_fill_right, NULL, NULL, NULL, { 0 } },
  { "simulate", action_simulate, NULL, NULL, NULL, { 0 } },
  { "recalc", action_recalc, NULL, NULL, NULL, { 0 } },
  { "functions", action_functions, NULL, NULL, NULL, { 0 } },
  { "about", action_about, NULL, NULL, NULL, { 0 } },
};

/* ---- Building the window ---------------------------------------------- */

static GMenuModel *
build_menu (void)
{
  GMenu *bar = g_menu_new ();
  GMenu *m, *section, *examples;
  GMenuItem *item;

#define ADD(menu, label, action, accel) \
  do { \
    item = g_menu_item_new (label, action); \
    if (accel != NULL) \
      g_menu_item_set_attribute (item, "accel", "s", accel); \
    g_menu_append_item (menu, item); \
    g_object_unref (item); \
  } while (0)

  m = g_menu_new ();
  section = g_menu_new ();
  ADD (section, "_New", "win.new", NULL);
  ADD (section, "_Open…", "win.open", NULL);
  examples = g_menu_new ();
  ADD (examples, "Product launch (profit at risk)", "win.open-example::launch.tm", NULL);
  ADD (examples, "Sales forecast (exponential smoothing)", "win.open-example::sales.tm", NULL);
  ADD (examples, "Retirement savings (random walk)", "win.open-example::retirement.tm", NULL);
  ADD (examples, "Project schedule (PERT)", "win.open-example::project.tm", NULL);
  ADD (examples, "Forecasting tournament (Brier scores)", "win.open-example::judgment.tm", NULL);
  g_menu_append_submenu (section, "Open _Example", G_MENU_MODEL (examples));
  g_object_unref (examples);
  g_menu_append_section (m, NULL, G_MENU_MODEL (section));
  g_object_unref (section);
  section = g_menu_new ();
  ADD (section, "_Save", "win.save", NULL);
  ADD (section, "Save _As…", "win.save-as", NULL);
  ADD (section, "_Export Samples…", "win.export-samples", NULL);
  g_menu_append_section (m, NULL, G_MENU_MODEL (section));
  g_object_unref (section);
  section = g_menu_new ();
  ADD (section, "_Quit", "app.quit", NULL);
  g_menu_append_section (m, NULL, G_MENU_MODEL (section));
  g_object_unref (section);
  g_menu_append_submenu (bar, "_File", G_MENU_MODEL (m));
  g_object_unref (m);

  m = g_menu_new ();
  section = g_menu_new ();
  ADD (section, "Cu_t", "win.cut", "<Control>x");
  ADD (section, "_Copy", "win.copy", "<Control>c");
  ADD (section, "_Paste", "win.paste", "<Control>v");
  ADD (section, "Cle_ar", "win.clear", "Delete");
  g_menu_append_section (m, NULL, G_MENU_MODEL (section));
  g_object_unref (section);
  section = g_menu_new ();
  ADD (section, "Fill _Down", "win.fill-down", "<Control>d");
  ADD (section, "Fill _Right", "win.fill-right", "<Control>r");
  g_menu_append_section (m, NULL, G_MENU_MODEL (section));
  g_object_unref (section);
  g_menu_append_submenu (bar, "_Edit", G_MENU_MODEL (m));
  g_object_unref (m);

  m = g_menu_new ();
  ADD (m, "_Run Simulation", "win.simulate", NULL);
  ADD (m, "Re_calculate (new draws)", "win.recalc", NULL);
  g_menu_append_submenu (bar, "_Simulate", G_MENU_MODEL (m));
  g_object_unref (m);

  m = g_menu_new ();
  ADD (m, "_Functions", "win.functions", NULL);
  ADD (m, "_About Time Machine", "win.about", NULL);
  g_menu_append_submenu (bar, "_Help", G_MENU_MODEL (m));
  g_object_unref (m);
#undef ADD

  return G_MENU_MODEL (bar);
}

static GtkWidget *
labelled (const char *text, GtkWidget *widget)
{
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  GtkWidget *label = gtk_label_new (text);

  gtk_widget_add_css_class (label, "dim-label");
  gtk_box_append (GTK_BOX (box), label);
  gtk_box_append (GTK_BOX (box), widget);
  return box;
}

static GtkWidget *
build_panel (TmWindow *self)
{
  GtkWidget *panel = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
  GtkWidget *grid;

  gtk_widget_add_css_class (panel, "forecast-panel");
  gtk_widget_set_margin_start (panel, 14);
  gtk_widget_set_margin_end (panel, 14);
  gtk_widget_set_margin_top (panel, 12);
  gtk_widget_set_margin_bottom (panel, 12);

  {
    GtkWidget *heading = gtk_label_new ("FORECAST");
    gtk_widget_add_css_class (heading, "panel-heading");
    gtk_widget_set_halign (heading, GTK_ALIGN_START);
    gtk_box_append (GTK_BOX (panel), heading);
  }
  self->panel_title = gtk_label_new ("");
  gtk_widget_add_css_class (self->panel_title, "panel-title");
  gtk_widget_set_halign (self->panel_title, GTK_ALIGN_START);
  gtk_label_set_ellipsize (GTK_LABEL (self->panel_title), PANGO_ELLIPSIZE_END);
  gtk_box_append (GTK_BOX (panel), self->panel_title);

  self->panel_subtitle = gtk_label_new ("");
  gtk_widget_add_css_class (self->panel_subtitle, "dim-label");
  gtk_widget_set_halign (self->panel_subtitle, GTK_ALIGN_START);
  gtk_label_set_ellipsize (GTK_LABEL (self->panel_subtitle), PANGO_ELLIPSIZE_END);
  gtk_box_append (GTK_BOX (panel), self->panel_subtitle);

  self->chart = tm_chart_new ();
  gtk_widget_set_vexpand (self->chart, TRUE);
  gtk_widget_add_css_class (self->chart, "chart-frame");
  gtk_widget_set_margin_top (self->chart, 6);
  gtk_box_append (GTK_BOX (panel), self->chart);

  grid = gtk_grid_new ();
  gtk_grid_set_column_spacing (GTK_GRID (grid), 10);
  gtk_grid_set_row_spacing (GTK_GRID (grid), 3);
  gtk_widget_set_margin_top (grid, 8);
  for (int i = 0; i < N_STATS; i++)
    {
      GtkWidget *name = gtk_label_new (STAT_NAMES[i]);
      GtkWidget *value = gtk_label_new ("");

      gtk_widget_add_css_class (name, "dim-label");
      gtk_widget_set_halign (name, GTK_ALIGN_START);
      gtk_widget_set_halign (value, GTK_ALIGN_END);
      gtk_widget_set_hexpand (value, TRUE);
      gtk_widget_add_css_class (value, "stat-value");
      gtk_grid_attach (GTK_GRID (grid), name, (i % 2) * 2, i / 2, 1, 1);
      gtk_grid_attach (GTK_GRID (grid), value, (i % 2) * 2 + 1, i / 2, 1, 1);
      self->stat_values[i] = value;
    }
  self->stats_box = grid;
  gtk_box_append (GTK_BOX (panel), grid);

  return panel;
}

static gboolean
on_close_request (GtkWindow *window, gpointer data)
{
  TmWindow *self = TM_WINDOW (window);

  /* Closing mid-simulation would pull the sheet out from under it. */
  if (self->simulating)
    {
      self->stop_requested = TRUE;
      return TRUE;
    }
  return FALSE;
}

static void
tm_window_dispose (GObject *object)
{
  TmWindow *self = TM_WINDOW (object);

  clear_clip (self);
  G_OBJECT_CLASS (tm_window_parent_class)->dispose (object);
}

static void
tm_window_finalize (GObject *object)
{
  TmWindow *self = TM_WINDOW (object);

  tm_sheet_free (self->sheet);
  g_free (self->path);
  G_OBJECT_CLASS (tm_window_parent_class)->finalize (object);
}

static void
tm_window_class_init (TmWindowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = tm_window_dispose;
  object_class->finalize = tm_window_finalize;
}

static void
tm_window_init (TmWindow *self)
{
  GtkWidget *outer, *menubar, *toolbar, *fbar, *paned, *grid_area, *vscroll, *hscroll;
  GtkWidget *statusbar, *run, *fx;
  GMenuModel *menu;

  self->sheet = tm_sheet_new ();
  g_action_map_add_action_entries (G_ACTION_MAP (self), WIN_ACTIONS,
                                   G_N_ELEMENTS (WIN_ACTIONS), self);

  outer = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);

  menu = build_menu ();
  menubar = gtk_popover_menu_bar_new_from_model (menu);
  g_object_unref (menu);
  gtk_box_append (GTK_BOX (outer), menubar);

  self->content = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_vexpand (self->content, TRUE);
  gtk_box_append (GTK_BOX (outer), self->content);

  /* The toolbar: the one button that matters, and its settings. */
  toolbar = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_widget_add_css_class (toolbar, "toolbar");
  run = gtk_button_new_with_label ("▶  Simulate");
  gtk_widget_add_css_class (run, "suggested-action");
  gtk_widget_set_tooltip_text (run, "Run the model through thousands of possible futures (F5)");
  gtk_actionable_set_action_name (GTK_ACTIONABLE (run), "win.simulate");
  gtk_box_append (GTK_BOX (toolbar), run);
  self->iter_spin = gtk_spin_button_new_with_range (100, 1000000, 1000);
  gtk_spin_button_set_value (GTK_SPIN_BUTTON (self->iter_spin), tm_sheet_iterations (self->sheet));
  gtk_widget_set_tooltip_text (self->iter_spin, "How many futures to simulate; the error in their mean shrinks as one over its square root");
  g_signal_connect (self->iter_spin, "value-changed", G_CALLBACK (on_iterations_changed), self);
  gtk_box_append (GTK_BOX (toolbar), labelled ("Futures", self->iter_spin));
  self->seed_spin = gtk_spin_button_new_with_range (0, 4294967295.0, 1);
  gtk_spin_button_set_value (GTK_SPIN_BUTTON (self->seed_spin), (double) tm_sheet_seed (self->sheet));
  gtk_widget_set_tooltip_text (self->seed_spin, "The same seed gives the same futures");
  g_signal_connect (self->seed_spin, "value-changed", G_CALLBACK (on_seed_changed), self);
  gtk_box_append (GTK_BOX (toolbar), labelled ("Seed", self->seed_spin));
  {
    GtkWidget *recalc = gtk_button_new_with_label ("Draw again");
    gtk_widget_set_tooltip_text (recalc, "Recalculate: every uncertain cell draws a new value (F9)");
    gtk_actionable_set_action_name (GTK_ACTIONABLE (recalc), "win.recalc");
    gtk_box_append (GTK_BOX (toolbar), recalc);
  }
  gtk_box_append (GTK_BOX (self->content), toolbar);

  /* The formula bar. */
  fbar = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_widget_add_css_class (fbar, "formula-bar");
  self->name_entry = gtk_entry_new ();
  gtk_editable_set_width_chars (GTK_EDITABLE (self->name_entry), 9);
  gtk_editable_set_max_width_chars (GTK_EDITABLE (self->name_entry), 9);
  gtk_widget_set_tooltip_text (self->name_entry, "Type a cell or range to go to it");
  g_signal_connect (self->name_entry, "activate", G_CALLBACK (on_name_activate), self);
  gtk_box_append (GTK_BOX (fbar), self->name_entry);
  fx = gtk_label_new ("fx");
  gtk_widget_add_css_class (fx, "fx");
  gtk_box_append (GTK_BOX (fbar), fx);
  self->formula_entry = gtk_entry_new ();
  gtk_widget_set_hexpand (self->formula_entry, TRUE);
  gtk_widget_add_css_class (self->formula_entry, "formula");
  g_signal_connect (self->formula_entry, "activate", G_CALLBACK (on_formula_activate), self);
  g_signal_connect (self->formula_entry, "changed", G_CALLBACK (on_formula_changed), self);
  {
    GtkEventController *key = gtk_event_controller_key_new ();
    gtk_event_controller_set_propagation_phase (key, GTK_PHASE_CAPTURE);
    g_signal_connect (key, "key-pressed", G_CALLBACK (on_formula_key), self);
    gtk_widget_add_controller (self->formula_entry, key);
  }
  gtk_box_append (GTK_BOX (fbar), self->formula_entry);
  gtk_box_append (GTK_BOX (self->content), fbar);

  /* The grid and its scrollbars, and the panel beside them. */
  paned = gtk_paned_new (GTK_ORIENTATION_HORIZONTAL);
  gtk_widget_set_vexpand (paned, TRUE);
  gtk_paned_set_shrink_start_child (GTK_PANED (paned), FALSE);
  gtk_paned_set_shrink_end_child (GTK_PANED (paned), FALSE);
  gtk_paned_set_resize_end_child (GTK_PANED (paned), FALSE);

  grid_area = gtk_grid_new ();
  self->grid = tm_grid_new (self->sheet);
  vscroll = gtk_scrollbar_new (GTK_ORIENTATION_VERTICAL, tm_grid_get_vadjustment (TM_GRID (self->grid)));
  hscroll = gtk_scrollbar_new (GTK_ORIENTATION_HORIZONTAL, tm_grid_get_hadjustment (TM_GRID (self->grid)));
  gtk_grid_attach (GTK_GRID (grid_area), self->grid, 0, 0, 1, 1);
  gtk_grid_attach (GTK_GRID (grid_area), vscroll, 1, 0, 1, 1);
  gtk_grid_attach (GTK_GRID (grid_area), hscroll, 0, 1, 1, 1);
  g_signal_connect (self->grid, "selection-changed", G_CALLBACK (on_selection_changed), self);
  g_signal_connect (self->grid, "edit", G_CALLBACK (on_grid_edit), self);
  gtk_paned_set_start_child (GTK_PANED (paned), grid_area);

  {
    GtkWidget *panel = build_panel (self);
    gtk_widget_set_size_request (panel, 340, -1);
    gtk_paned_set_end_child (GTK_PANED (paned), panel);
  }
  gtk_box_append (GTK_BOX (self->content), paned);

  /* The status bar, with room for a simulation's progress. */
  statusbar = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 10);
  gtk_widget_add_css_class (statusbar, "statusbar");
  self->status = gtk_label_new ("Type a formula such as =RAND.PERT(80,100,150), then press F5.");
  gtk_label_set_ellipsize (GTK_LABEL (self->status), PANGO_ELLIPSIZE_END);
  gtk_widget_set_hexpand (self->status, TRUE);
  gtk_widget_set_halign (self->status, GTK_ALIGN_START);
  gtk_box_append (GTK_BOX (statusbar), self->status);
  self->progress = gtk_progress_bar_new ();
  gtk_widget_set_size_request (self->progress, 200, -1);
  gtk_widget_set_valign (self->progress, GTK_ALIGN_CENTER);
  gtk_widget_set_visible (self->progress, FALSE);
  gtk_box_append (GTK_BOX (statusbar), self->progress);
  self->stop_button = gtk_button_new_with_label ("Stop");
  gtk_widget_set_visible (self->stop_button, FALSE);
  g_signal_connect (self->stop_button, "clicked", G_CALLBACK (on_stop_clicked), self);
  gtk_box_append (GTK_BOX (statusbar), self->stop_button);
  gtk_box_append (GTK_BOX (outer), statusbar);

  gtk_window_set_child (GTK_WINDOW (self), outer);
  gtk_window_set_default_size (GTK_WINDOW (self), 1280, 800);
  g_signal_connect (self, "close-request", G_CALLBACK (on_close_request), NULL);

  update_title (self);
  update_formula_bar (self);
  update_panel (self);
  gtk_widget_grab_focus (self->grid);
}

TmWindow *
tm_window_new (GtkApplication *app)
{
  return g_object_new (TM_TYPE_WINDOW, "application", app, NULL);
}

void
tm_window_select (TmWindow *self, const TmRange *range)
{
  tm_grid_select (TM_GRID (self->grid), range);
}

char *
tm_samples_dir (void)
{
  char *exe = g_file_read_link ("/proc/self/exe", NULL);

  /* Run from a build directory, the examples are in the source tree. */
  if (g_file_test (TM_SOURCE_SAMPLES_DIR, G_FILE_TEST_IS_DIR) && exe != NULL
      && !g_str_has_prefix (exe, TM_PREFIX))
    {
      g_free (exe);
      return g_strdup (TM_SOURCE_SAMPLES_DIR);
    }
  g_free (exe);
  if (g_file_test (TM_SAMPLES_DIR, G_FILE_TEST_IS_DIR))
    return g_strdup (TM_SAMPLES_DIR);
  return g_strdup (TM_SOURCE_SAMPLES_DIR);
}
