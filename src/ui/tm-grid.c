/* tm-grid.c - the grid of cells
 *
 * Copyright (C) 2026 The timemachine authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "tm-grid.h"
#include "tm-numfmt.h"

#include <math.h>
#include <string.h>

#define ROW_HEIGHT     22
#define HEADER_HEIGHT  22
#define HEADER_WIDTH   48
#define PAD            4
#define RESIZE_SLOP    4

struct _TmGrid {
  GtkWidget parent_instance;

  TmSheet *sheet;
  TmRef cursor;
  TmRef anchor;           /* the other corner of the selection */
  int top_row, left_col;  /* the first cell in view */

  GtkAdjustment *hadj, *vadj;
  gboolean updating;      /* setting the adjustments ourselves */

  /* Dragging: a selection, or a column's right edge. */
  gboolean dragging;
  int resizing_col;       /* -1 when not resizing */
  int resize_start_width;
  double drag_x, drag_y;

  double scroll_accum;
  PangoFontDescription *font;

  /* An edit in progress, mirrored from the formula bar. */
  char *edit_text;
  TmRef edit_cell;
  int edit_caret;

  /* Point mode. */
  gboolean pointing;
  gboolean point_dragging;
  gboolean has_point;
  TmRange point;
  TmRef point_anchor;
};

G_DEFINE_FINAL_TYPE (TmGrid, tm_grid, GTK_TYPE_WIDGET)

enum {
  SIGNAL_SELECTION_CHANGED,
  SIGNAL_EDIT,
  SIGNAL_POINT,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

/* ---- Geometry --------------------------------------------------------- */

static int
col_width (TmGrid *self, int col)
{
  return self->sheet != NULL ? tm_sheet_col_width (self->sheet, col) : TM_DEFAULT_COL_WIDTH;
}

static int
visible_rows (TmGrid *self)
{
  int h = gtk_widget_get_height (GTK_WIDGET (self)) - HEADER_HEIGHT;
  return MAX (1, h / ROW_HEIGHT);
}

static int
visible_cols (TmGrid *self)
{
  int w = gtk_widget_get_width (GTK_WIDGET (self)) - HEADER_WIDTH;
  int n = 0, x = 0;

  for (int c = self->left_col; c < TM_MAX_COLS && x < w; c++)
    {
      x += col_width (self, c);
      if (x <= w)
        n++;
    }
  return MAX (1, n);
}

/* The x of a column's left edge; columns left of the view are off to the
 * left and give a negative x. */
static int
col_x (TmGrid *self, int col)
{
  int x = HEADER_WIDTH;

  if (col >= self->left_col)
    for (int c = self->left_col; c < col; c++)
      x += col_width (self, c);
  else
    for (int c = col; c < self->left_col; c++)
      x -= col_width (self, c);
  return x;
}

static int
row_y (TmGrid *self, int row)
{
  return HEADER_HEIGHT + (row - self->top_row) * ROW_HEIGHT;
}

/* The cell under a point, clamped to the sheet. */
static void
cell_at (TmGrid *self, double x, double y, int *row, int *col)
{
  int cx = HEADER_WIDTH, c = self->left_col;

  *row = self->top_row + (int) floor ((y - HEADER_HEIGHT) / ROW_HEIGHT);
  *row = CLAMP (*row, 0, TM_MAX_ROWS - 1);
  if (x < HEADER_WIDTH)
    {
      *col = MAX (0, self->left_col - 1);
      return;
    }
  while (c < TM_MAX_COLS - 1 && cx + col_width (self, c) <= x)
    {
      cx += col_width (self, c);
      c++;
    }
  *col = c;
}

/* A column whose right edge is under x, in the header, for resizing. */
static int
edge_at (TmGrid *self, double x, double y)
{
  int cx = HEADER_WIDTH;

  if (y > HEADER_HEIGHT)
    return -1;
  for (int c = self->left_col; c < TM_MAX_COLS && cx < gtk_widget_get_width (GTK_WIDGET (self)); c++)
    {
      cx += col_width (self, c);
      if (fabs (x - cx) <= RESIZE_SLOP)
        return c;
    }
  return -1;
}

/* ---- Scrolling -------------------------------------------------------- */

static void
update_adjustments (TmGrid *self)
{
  TmRange used = { 0, 0, 0, 0 };
  int rows = visible_rows (self), cols = visible_cols (self);
  double vupper, hupper;

  if (self->sheet != NULL)
    tm_sheet_used_range (self->sheet, &used);
  /* Room to scroll a good way past what is in use, but not to the end of
   * the sheet, which would make the scrollbar useless for the part that
   * matters. */
  vupper = MAX (MAX (used.row1, self->cursor.row), self->top_row + rows) + 100;
  hupper = MAX (MAX (used.col1, self->cursor.col), self->left_col + cols) + 10;
  vupper = MIN (vupper, TM_MAX_ROWS);
  hupper = MIN (hupper, TM_MAX_COLS);

  self->updating = TRUE;
  gtk_adjustment_configure (self->vadj, self->top_row, 0, vupper, 1, MAX (1, rows - 1), rows);
  gtk_adjustment_configure (self->hadj, self->left_col, 0, hupper, 1, MAX (1, cols - 1), cols);
  self->updating = FALSE;
}

static void
on_adjustment_changed (GtkAdjustment *adj, TmGrid *self)
{
  if (self->updating)
    return;
  self->top_row = (int) gtk_adjustment_get_value (self->vadj);
  self->left_col = (int) gtk_adjustment_get_value (self->hadj);
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

static void
scroll_to_cursor (TmGrid *self)
{
  int rows = visible_rows (self);

  if (self->cursor.row < self->top_row)
    self->top_row = self->cursor.row;
  else if (self->cursor.row >= self->top_row + rows)
    self->top_row = self->cursor.row - rows + 1;

  if (self->cursor.col < self->left_col)
    self->left_col = self->cursor.col;
  else
    while (self->left_col < self->cursor.col
           && col_x (self, self->cursor.col + 1) > gtk_widget_get_width (GTK_WIDGET (self)))
      self->left_col++;
  update_adjustments (self);
}

/* ---- Selection -------------------------------------------------------- */

void
tm_grid_get_selection (TmGrid *self, TmRange *out)
{
  out->row0 = self->anchor.row;
  out->col0 = self->anchor.col;
  out->row1 = self->cursor.row;
  out->col1 = self->cursor.col;
  tm_range_normalize (out);
}

TmRef
tm_grid_get_cursor (TmGrid *self)
{
  return self->cursor;
}

static void
set_cursor (TmGrid *self, int row, int col, gboolean extend)
{
  self->cursor.row = CLAMP (row, 0, TM_MAX_ROWS - 1);
  self->cursor.col = CLAMP (col, 0, TM_MAX_COLS - 1);
  if (!extend)
    self->anchor = self->cursor;
  scroll_to_cursor (self);
  gtk_widget_queue_draw (GTK_WIDGET (self));
  g_signal_emit (self, signals[SIGNAL_SELECTION_CHANGED], 0);
}

void
tm_grid_select (TmGrid *self, const TmRange *range)
{
  int width = gtk_widget_get_width (GTK_WIDGET (self));

  self->anchor.row = range->row0;
  self->anchor.col = range->col0;
  set_cursor (self, range->row1, range->col1, TRUE);

  /* A range chosen by name is shown from its start, and from the top
   * left of the sheet if it fits from there, so that its headings are in
   * view; its far end may be off screen. */
  self->left_col = 0;
  if (width > 0 && col_x (self, range->col0 + 1) > width)
    self->left_col = range->col0;
  self->top_row = range->row0 < visible_rows (self) ? 0 : range->row0;
  update_adjustments (self);
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

void
tm_grid_move_cursor (TmGrid *self, int drow, int dcol, gboolean extend)
{
  set_cursor (self, self->cursor.row + drow, self->cursor.col + dcol, extend);
}

/* Ctrl+arrow: to the edge of the block of filled cells, as Excel does. */
static void
jump (TmGrid *self, int drow, int dcol, gboolean extend)
{
  int r = self->cursor.row, c = self->cursor.col;
  gboolean here, next;

#define FILLED(rr, cc) (tm_sheet_get_input (self->sheet, (rr), (cc)) != NULL)
#define INSIDE(rr, cc) ((rr) >= 0 && (rr) < TM_MAX_ROWS && (cc) >= 0 && (cc) < TM_MAX_COLS)

  if (!INSIDE (r + drow, c + dcol))
    return;
  here = FILLED (r, c);
  next = FILLED (r + drow, c + dcol);
  if (here && next)
    while (INSIDE (r + drow, c + dcol) && FILLED (r + drow, c + dcol))
      {
        r += drow;
        c += dcol;
      }
  else
    {
      r += drow;
      c += dcol;
      while (INSIDE (r + drow, c + dcol) && !FILLED (r, c)
             && (r + drow) <= MAX (self->cursor.row, 0) + 2000)
        {
          r += drow;
          c += dcol;
        }
      if (!FILLED (r, c) && (drow > 0 || dcol > 0))
        {
          /* Nothing further on: stop at the edge of the used range. */
          TmRange used;
          if (tm_sheet_used_range (self->sheet, &used))
            {
              r = drow > 0 ? MAX (self->cursor.row, used.row1) : r;
              c = dcol > 0 ? MAX (self->cursor.col, used.col1) : c;
            }
        }
    }
#undef FILLED
#undef INSIDE
  set_cursor (self, r, c, extend);
}

/* ---- Drawing ---------------------------------------------------------- */

typedef struct { double r, g, b; } Rgb;

static const Rgb GRID_BG      = { 1.000, 1.000, 1.000 };
static const Rgb GRID_LINE    = { 0.855, 0.863, 0.878 };
static const Rgb HEADER_BG    = { 0.945, 0.949, 0.957 };
static const Rgb HEADER_SEL   = { 0.847, 0.878, 0.957 };
static const Rgb HEADER_TEXT  = { 0.300, 0.320, 0.360 };
static const Rgb TEXT         = { 0.110, 0.110, 0.130 };
static const Rgb ERROR_TEXT   = { 0.760, 0.150, 0.150 };
static const Rgb ACCENT       = { 0.231, 0.357, 0.859 };
static const Rgb RANDOM_BG    = { 0.949, 0.933, 1.000 };
static const Rgb RANDOM_MARK  = { 0.490, 0.360, 0.900 };

static void
set_rgb (cairo_t *cr, Rgb c)
{
  cairo_set_source_rgb (cr, c.r, c.g, c.b);
}

/* A number shown as wide as the column allows: fewer significant digits
 * when it will not fit, and hashes when nothing will, as Excel's General
 * format does. */
static char *
fit_number (PangoLayout *layout, double number, int width)
{
  char *text = tm_number_format_general (number);
  int w;

  pango_layout_set_text (layout, text, -1);
  pango_layout_get_pixel_size (layout, &w, NULL);
  if (w <= width)
    return text;
  for (int digits = 9; digits >= 1; digits--)
    {
      char buf[64];

      g_free (text);
      g_snprintf (buf, sizeof buf, "%.*G", digits, number);
      text = g_strdup (buf);
      pango_layout_set_text (layout, text, -1);
      pango_layout_get_pixel_size (layout, &w, NULL);
      if (w <= width)
        return text;
    }
  g_free (text);
  text = g_strnfill (MAX (1, width / 8), '#');
  return text;
}

static void
draw_cells (TmGrid *self, cairo_t *cr, PangoLayout *layout, int width, int height)
{
  int last_row = self->top_row + visible_rows (self) + 1;
  TmRange sel;

  tm_grid_get_selection (self, &sel);

  for (int r = self->top_row; r <= last_row && r < TM_MAX_ROWS; r++)
    {
      int y = row_y (self, r);

      for (int c = self->left_col; c < TM_MAX_COLS; c++)
        {
          int x = col_x (self, c), w = col_width (self, c);
          const TmValue *v;
          const char *input;
          char *text = NULL;
          int tw, th, tx;
          PangoAlignment align = PANGO_ALIGN_LEFT;
          gboolean is_error = FALSE;
          int clip_w = w;

          if (x >= width)
            break;
          input = tm_sheet_get_input (self->sheet, r, c);
          if (input == NULL)
            continue;

          if (tm_sheet_is_random (self->sheet, r, c))
            {
              set_rgb (cr, RANDOM_BG);
              cairo_rectangle (cr, x + 1, y + 1, w - 1, ROW_HEIGHT - 1);
              cairo_fill (cr);
              /* A small corner mark, for those who cannot tell the tint. */
              set_rgb (cr, RANDOM_MARK);
              cairo_move_to (cr, x + w - 6, y + 1);
              cairo_line_to (cr, x + w, y + 1);
              cairo_line_to (cr, x + w, y + 7);
              cairo_close_path (cr);
              cairo_fill (cr);
            }

          v = tm_sheet_get_value (self->sheet, r, c);
          switch (v->type)
            {
            case TM_VALUE_NUMBER:
              {
                const char *format = tm_sheet_get_format (self->sheet, r, c);

                if (format != NULL)
                  {
                    /* A formatted number either fits or is hashes: its
                     * digits are what the format says, no fewer. */
                    text = tm_format_number (v->as.number, format);
                    pango_layout_set_text (layout, text, -1);
                    pango_layout_get_pixel_size (layout, &tw, NULL);
                    if (tw > w - 2 * PAD)
                      {
                        g_free (text);
                        text = g_strnfill (MAX (1, (w - 2 * PAD) / 8), '#');
                      }
                  }
                else
                  text = fit_number (layout, v->as.number, w - 2 * PAD);
                align = PANGO_ALIGN_RIGHT;
              }
              break;
            case TM_VALUE_TEXT:
              text = g_strdup (v->as.text);
              /* Text runs on over empty neighbours, as in every
               * spreadsheet since VisiCalc. */
              for (int n = c + 1; n < TM_MAX_COLS && col_x (self, n) < width; n++)
                {
                  if (tm_sheet_get_input (self->sheet, r, n) != NULL)
                    break;
                  clip_w += col_width (self, n);
                }
              break;
            case TM_VALUE_BOOL:
              text = g_strdup (v->as.boolean ? "TRUE" : "FALSE");
              align = PANGO_ALIGN_CENTER;
              break;
            case TM_VALUE_ERROR:
              text = g_strdup (tm_error_name (v->as.error));
              align = PANGO_ALIGN_CENTER;
              is_error = TRUE;
              break;
            case TM_VALUE_EMPTY:
            default:
              break;
            }
          if (text == NULL || *text == '\0')
            {
              g_free (text);
              continue;
            }

          pango_layout_set_text (layout, text, -1);
          pango_layout_get_pixel_size (layout, &tw, &th);
          if (align == PANGO_ALIGN_RIGHT)
            tx = x + w - PAD - tw;
          else if (align == PANGO_ALIGN_CENTER)
            tx = x + (w - tw) / 2;
          else
            tx = x + PAD;

          cairo_save (cr);
          cairo_rectangle (cr, x + 1, y, clip_w - 1, ROW_HEIGHT);
          cairo_clip (cr);
          set_rgb (cr, is_error ? ERROR_TEXT : TEXT);
          cairo_move_to (cr, tx, y + (ROW_HEIGHT - th) / 2.0);
          pango_cairo_show_layout (cr, layout);
          cairo_restore (cr);
          g_free (text);
        }
    }
}

static void
draw (TmGrid *self, cairo_t *cr, int width, int height)
{
  PangoLayout *layout = pango_cairo_create_layout (cr);
  TmRange sel;
  int last_row = self->top_row + visible_rows (self) + 1;

  pango_layout_set_font_description (layout, self->font);
  tm_grid_get_selection (self, &sel);

  set_rgb (cr, GRID_BG);
  cairo_paint (cr);

  /* The selection's fill goes under the text. */
  {
    int x0 = MAX (col_x (self, sel.col0), HEADER_WIDTH);
    int y0 = MAX (row_y (self, sel.row0), HEADER_HEIGHT);
    int x1 = col_x (self, sel.col1 + 1), y1 = row_y (self, sel.row1 + 1);

    if (x1 > x0 && y1 > y0 && (sel.row0 != sel.row1 || sel.col0 != sel.col1))
      {
        cairo_set_source_rgba (cr, ACCENT.r, ACCENT.g, ACCENT.b, 0.10);
        cairo_rectangle (cr, x0, y0, x1 - x0, y1 - y0);
        cairo_fill (cr);
      }
  }

  /* Grid lines. */
  set_rgb (cr, GRID_LINE);
  cairo_set_line_width (cr, 1);
  for (int r = self->top_row; r <= last_row; r++)
    {
      double y = row_y (self, r) + ROW_HEIGHT - 0.5;
      cairo_move_to (cr, HEADER_WIDTH, y);
      cairo_line_to (cr, width, y);
    }
  for (int c = self->left_col; c < TM_MAX_COLS; c++)
    {
      double x = col_x (self, c) + col_width (self, c) - 0.5;
      if (x > width + col_width (self, c))
        break;
      cairo_move_to (cr, x, HEADER_HEIGHT);
      cairo_line_to (cr, x, height);
    }
  cairo_stroke (cr);

  if (self->sheet != NULL)
    draw_cells (self, cr, layout, width, height);

  /* Headers, over everything. */
  set_rgb (cr, HEADER_BG);
  cairo_rectangle (cr, 0, 0, width, HEADER_HEIGHT);
  cairo_rectangle (cr, 0, 0, HEADER_WIDTH, height);
  cairo_fill (cr);

  for (int c = self->left_col; c < TM_MAX_COLS; c++)
    {
      int x = col_x (self, c), w = col_width (self, c), tw, th;
      char name[8];

      if (x >= width)
        break;
      if (c >= sel.col0 && c <= sel.col1)
        {
          set_rgb (cr, HEADER_SEL);
          cairo_rectangle (cr, x, 0, w, HEADER_HEIGHT);
          cairo_fill (cr);
        }
      tm_col_name (c, name, sizeof name);
      pango_layout_set_text (layout, name, -1);
      pango_layout_get_pixel_size (layout, &tw, &th);
      set_rgb (cr, HEADER_TEXT);
      cairo_move_to (cr, x + (w - tw) / 2.0, (HEADER_HEIGHT - th) / 2.0);
      pango_cairo_show_layout (cr, layout);
      set_rgb (cr, GRID_LINE);
      cairo_move_to (cr, x + w - 0.5, 0);
      cairo_line_to (cr, x + w - 0.5, HEADER_HEIGHT);
      cairo_stroke (cr);
    }

  for (int r = self->top_row; r <= last_row && r < TM_MAX_ROWS; r++)
    {
      int y = row_y (self, r), tw, th;
      char num[16];

      if (r >= sel.row0 && r <= sel.row1)
        {
          set_rgb (cr, HEADER_SEL);
          cairo_rectangle (cr, 0, y, HEADER_WIDTH, ROW_HEIGHT);
          cairo_fill (cr);
        }
      g_snprintf (num, sizeof num, "%d", r + 1);
      pango_layout_set_text (layout, num, -1);
      pango_layout_get_pixel_size (layout, &tw, &th);
      set_rgb (cr, HEADER_TEXT);
      cairo_move_to (cr, (HEADER_WIDTH - tw) / 2.0, y + (ROW_HEIGHT - th) / 2.0);
      pango_cairo_show_layout (cr, layout);
      set_rgb (cr, GRID_LINE);
      cairo_move_to (cr, 0, y + ROW_HEIGHT - 0.5);
      cairo_line_to (cr, HEADER_WIDTH, y + ROW_HEIGHT - 0.5);
      cairo_stroke (cr);
    }

  set_rgb (cr, GRID_LINE);
  cairo_move_to (cr, 0, HEADER_HEIGHT - 0.5);
  cairo_line_to (cr, width, HEADER_HEIGHT - 0.5);
  cairo_move_to (cr, HEADER_WIDTH - 0.5, 0);
  cairo_line_to (cr, HEADER_WIDTH - 0.5, height);
  cairo_stroke (cr);

  /* The selection's border, and the cursor's. */
  cairo_save (cr);
  cairo_rectangle (cr, HEADER_WIDTH, HEADER_HEIGHT, width - HEADER_WIDTH, height - HEADER_HEIGHT);
  cairo_clip (cr);
  {
    int x0 = col_x (self, sel.col0), y0 = row_y (self, sel.row0);
    int x1 = col_x (self, sel.col1 + 1), y1 = row_y (self, sel.row1 + 1);

    set_rgb (cr, ACCENT);
    cairo_set_line_width (cr, 2);
    cairo_rectangle (cr, x0, y0, x1 - x0 - 1, y1 - y0 - 1);
    cairo_stroke (cr);
    /* The fill handle, bottom right, as a cue that the selection is a
     * thing that can be filled from. */
    cairo_rectangle (cr, x1 - 4, y1 - 4, 6, 6);
    cairo_fill (cr);
  }

  /* The range being pointed at, dashed, as Excel marks it. */
  if (self->has_point)
    {
      double dash[] = { 4, 3 };
      int x0 = col_x (self, self->point.col0), y0 = row_y (self, self->point.row0);
      int x1 = col_x (self, self->point.col1 + 1), y1 = row_y (self, self->point.row1 + 1);

      cairo_set_source_rgb (cr, 0.85, 0.35, 0.10);
      cairo_set_line_width (cr, 2);
      cairo_set_dash (cr, dash, 2, 0);
      cairo_rectangle (cr, x0 + 1, y0 + 1, x1 - x0 - 2, y1 - y0 - 2);
      cairo_stroke (cr);
      cairo_set_dash (cr, NULL, 0, 0);
    }

  /* The edit, in its cell, running on to the right as far as it needs. */
  if (self->edit_text != NULL)
    {
      int x = col_x (self, self->edit_cell.col), y = row_y (self, self->edit_cell.row);
      int w = col_width (self, self->edit_cell.col), tw, th;
      PangoRectangle caret;
      int index;

      pango_layout_set_text (layout, self->edit_text, -1);
      pango_layout_get_pixel_size (layout, &tw, &th);
      w = MAX (w, tw + 2 * PAD + 4);
      cairo_set_source_rgb (cr, 1, 1, 1);
      cairo_rectangle (cr, x, y, w, ROW_HEIGHT);
      cairo_fill (cr);
      set_rgb (cr, ACCENT);
      cairo_set_line_width (cr, 2);
      cairo_rectangle (cr, x, y, w - 1, ROW_HEIGHT - 1);
      cairo_stroke (cr);
      set_rgb (cr, TEXT);
      cairo_move_to (cr, x + PAD, y + (ROW_HEIGHT - th) / 2.0);
      pango_cairo_show_layout (cr, layout);

      index = (int) (g_utf8_offset_to_pointer (self->edit_text,
                                               MIN (self->edit_caret, (int) g_utf8_strlen (self->edit_text, -1)))
                     - self->edit_text);
      pango_layout_index_to_pos (layout, index, &caret);
      cairo_rectangle (cr, x + PAD + caret.x / PANGO_SCALE, y + 3, 1, ROW_HEIGHT - 6);
      cairo_fill (cr);
    }
  cairo_restore (cr);

  g_object_unref (layout);
}

static void
tm_grid_snapshot (GtkWidget *widget, GtkSnapshot *snapshot)
{
  TmGrid *self = TM_GRID (widget);
  int w = gtk_widget_get_width (widget), h = gtk_widget_get_height (widget);
  cairo_t *cr = gtk_snapshot_append_cairo (snapshot, &GRAPHENE_RECT_INIT (0, 0, w, h));

  draw (self, cr, w, h);
  cairo_destroy (cr);
}

static void
tm_grid_size_allocate (GtkWidget *widget, int width, int height, int baseline)
{
  update_adjustments (TM_GRID (widget));
}

static void
tm_grid_measure (GtkWidget *widget, GtkOrientation orientation, int for_size,
                 int *minimum, int *natural, int *min_baseline, int *nat_baseline)
{
  if (orientation == GTK_ORIENTATION_HORIZONTAL)
    {
      *minimum = HEADER_WIDTH + 2 * TM_DEFAULT_COL_WIDTH;
      *natural = HEADER_WIDTH + 9 * TM_DEFAULT_COL_WIDTH;
    }
  else
    {
      *minimum = HEADER_HEIGHT + 4 * ROW_HEIGHT;
      *natural = HEADER_HEIGHT + 26 * ROW_HEIGHT;
    }
}

/* ---- Input ------------------------------------------------------------ */

static gboolean
on_key_pressed (GtkEventControllerKey *controller, guint keyval, guint keycode,
                GdkModifierType state, TmGrid *self)
{
  gboolean shift = (state & GDK_SHIFT_MASK) != 0;
  gboolean ctrl = (state & GDK_CONTROL_MASK) != 0;
  GtkWidget *w = GTK_WIDGET (self);

  if (ctrl)
    {
      switch (keyval)
        {
        case GDK_KEY_Up:    jump (self, -1, 0, shift); return TRUE;
        case GDK_KEY_Down:  jump (self, 1, 0, shift); return TRUE;
        case GDK_KEY_Left:  jump (self, 0, -1, shift); return TRUE;
        case GDK_KEY_Right: jump (self, 0, 1, shift); return TRUE;
        case GDK_KEY_Home:  set_cursor (self, 0, 0, shift); return TRUE;
        case GDK_KEY_c: case GDK_KEY_C:
          gtk_widget_activate_action (w, "win.copy", NULL); return TRUE;
        case GDK_KEY_x: case GDK_KEY_X:
          gtk_widget_activate_action (w, "win.cut", NULL); return TRUE;
        case GDK_KEY_v: case GDK_KEY_V:
          gtk_widget_activate_action (w, "win.paste", NULL); return TRUE;
        case GDK_KEY_d: case GDK_KEY_D:
          gtk_widget_activate_action (w, "win.fill-down", NULL); return TRUE;
        case GDK_KEY_r: case GDK_KEY_R:
          gtk_widget_activate_action (w, "win.fill-right", NULL); return TRUE;
        case GDK_KEY_z: case GDK_KEY_Z:
          gtk_widget_activate_action (w, shift ? "win.redo" : "win.undo", NULL); return TRUE;
        case GDK_KEY_y: case GDK_KEY_Y:
          gtk_widget_activate_action (w, "win.redo", NULL); return TRUE;
        case GDK_KEY_a: case GDK_KEY_A:
          {
            TmRange all = { 0, 0, 0, 0 };
            if (tm_sheet_used_range (self->sheet, &all))
              {
                all.row0 = all.col0 = 0;
                tm_grid_select (self, &all);
              }
          }
          return TRUE;
        default:
          return FALSE;
        }
    }

  switch (keyval)
    {
    case GDK_KEY_Up:        tm_grid_move_cursor (self, -1, 0, shift); return TRUE;
    case GDK_KEY_Down:      tm_grid_move_cursor (self, 1, 0, shift); return TRUE;
    case GDK_KEY_Left:      tm_grid_move_cursor (self, 0, -1, shift); return TRUE;
    case GDK_KEY_Right:     tm_grid_move_cursor (self, 0, 1, shift); return TRUE;
    case GDK_KEY_Page_Up:   tm_grid_move_cursor (self, -visible_rows (self), 0, shift); return TRUE;
    case GDK_KEY_Page_Down: tm_grid_move_cursor (self, visible_rows (self), 0, shift); return TRUE;
    case GDK_KEY_Home:      set_cursor (self, self->cursor.row, 0, shift); return TRUE;
    case GDK_KEY_Return:
    case GDK_KEY_KP_Enter:  tm_grid_move_cursor (self, shift ? -1 : 1, 0, FALSE); return TRUE;
    case GDK_KEY_Tab:       tm_grid_move_cursor (self, 0, 1, FALSE); return TRUE;
    case GDK_KEY_ISO_Left_Tab: tm_grid_move_cursor (self, 0, -1, FALSE); return TRUE;
    case GDK_KEY_Delete:
    case GDK_KEY_KP_Delete:
      gtk_widget_activate_action (w, "win.clear", NULL);
      return TRUE;
    case GDK_KEY_BackSpace:
      g_signal_emit (self, signals[SIGNAL_EDIT], 0, "");
      return TRUE;
    case GDK_KEY_F2:
      g_signal_emit (self, signals[SIGNAL_EDIT], 0, NULL);
      return TRUE;
    default:
      break;
    }

  /* Typing starts an edit with what was typed. */
  if (!(state & (GDK_ALT_MASK | GDK_SUPER_MASK)))
    {
      gunichar ch = gdk_keyval_to_unicode (keyval);

      if (ch != 0 && g_unichar_isprint (ch))
        {
          char buf[8];
          int len = g_unichar_to_utf8 (ch, buf);

          buf[len] = '\0';
          g_signal_emit (self, signals[SIGNAL_EDIT], 0, buf);
          return TRUE;
        }
    }
  return FALSE;
}

static void
on_pressed (GtkGestureClick *gesture, int n_press, double x, double y, TmGrid *self)
{
  GdkModifierType state = gtk_event_controller_get_current_event_state (GTK_EVENT_CONTROLLER (gesture));
  int row, col, edge;

  edge = edge_at (self, x, y);
  if (edge >= 0)
    {
      if (n_press == 2)
        {
          /* A double click on the edge fits the column to what is in it
           * -- or, simply, puts it back to the default width. */
          tm_sheet_set_col_width (self->sheet, edge, TM_DEFAULT_COL_WIDTH);
          gtk_widget_queue_draw (GTK_WIDGET (self));
          return;
        }
      self->resizing_col = edge;
      self->resize_start_width = col_width (self, edge);
      self->drag_x = x;
      return;
    }

  cell_at (self, x, y, &row, &col);
  if (self->pointing && y >= HEADER_HEIGHT && x >= HEADER_WIDTH)
    {
      TmRange r = { row, col, row, col };

      /* The formula bar keeps the focus: the click is part of the edit. */
      self->point_anchor.row = row;
      self->point_anchor.col = col;
      self->point_dragging = TRUE;
      g_signal_emit (self, signals[SIGNAL_POINT], 0, &r);
      return;
    }
  gtk_widget_grab_focus (GTK_WIDGET (self));
  if (y < HEADER_HEIGHT && x >= HEADER_WIDTH)
    {
      /* A column header selects the column's used part. */
      TmRange r = { 0, col, 0, col };
      TmRange used;
      if (tm_sheet_used_range (self->sheet, &used))
        r.row1 = MAX (used.row1, 0);
      tm_grid_select (self, &r);
      return;
    }
  if (x < HEADER_WIDTH && y >= HEADER_HEIGHT)
    {
      TmRange r = { row, 0, row, 0 };
      TmRange used;
      if (tm_sheet_used_range (self->sheet, &used))
        r.col1 = MAX (used.col1, 0);
      tm_grid_select (self, &r);
      return;
    }

  set_cursor (self, row, col, (state & GDK_SHIFT_MASK) != 0);
  self->dragging = TRUE;
  if (n_press == 2)
    {
      self->dragging = FALSE;
      g_signal_emit (self, signals[SIGNAL_EDIT], 0, NULL);
    }
}

static void
on_drag_begin (GtkGestureDrag *gesture, double x, double y, TmGrid *self)
{
  self->drag_x = x;
  self->drag_y = y;
}

static void
on_drag_update (GtkGestureDrag *gesture, double dx, double dy, TmGrid *self)
{
  double sx, sy;
  int row, col;

  gtk_gesture_drag_get_start_point (gesture, &sx, &sy);
  if (self->resizing_col >= 0)
    {
      tm_sheet_set_col_width (self->sheet, self->resizing_col,
                              self->resize_start_width + (int) dx);
      update_adjustments (self);
      gtk_widget_queue_draw (GTK_WIDGET (self));
      return;
    }
  if (self->point_dragging)
    {
      TmRange r;

      cell_at (self, sx + dx, sy + dy, &row, &col);
      r.row0 = self->point_anchor.row;
      r.col0 = self->point_anchor.col;
      r.row1 = row;
      r.col1 = col;
      tm_range_normalize (&r);
      if (!self->has_point || memcmp (&r, &self->point, sizeof r) != 0)
        g_signal_emit (self, signals[SIGNAL_POINT], 0, &r);
      return;
    }
  if (!self->dragging)
    return;
  cell_at (self, sx + dx, sy + dy, &row, &col);
  if (row != self->cursor.row || col != self->cursor.col)
    set_cursor (self, row, col, TRUE);
}

static void
on_drag_end (GtkGestureDrag *gesture, double dx, double dy, TmGrid *self)
{
  self->dragging = FALSE;
  self->point_dragging = FALSE;
  self->resizing_col = -1;
}

static void
on_motion (GtkEventControllerMotion *motion, double x, double y, TmGrid *self)
{
  gtk_widget_set_cursor_from_name (GTK_WIDGET (self),
                                   edge_at (self, x, y) >= 0 ? "col-resize" : NULL);
}

static gboolean
on_scroll (GtkEventControllerScroll *controller, double dx, double dy, TmGrid *self)
{
  GdkScrollUnit unit = gtk_event_controller_scroll_get_unit (controller);
  int rows;

  if (unit == GDK_SCROLL_UNIT_SURFACE)
    {
      /* Touchpads scroll by pixels; rows are whole. */
      self->scroll_accum += dy / ROW_HEIGHT;
      rows = (int) trunc (self->scroll_accum);
      self->scroll_accum -= rows;
    }
  else
    rows = (int) (dy * 3);

  if (rows != 0)
    self->top_row = CLAMP (self->top_row + rows, 0, TM_MAX_ROWS - 1);
  if (dx != 0)
    self->left_col = CLAMP (self->left_col + (dx > 0 ? 1 : -1), 0, TM_MAX_COLS - 1);
  update_adjustments (self);
  gtk_widget_queue_draw (GTK_WIDGET (self));
  return TRUE;
}

/* ---- Life ------------------------------------------------------------- */

static void
tm_grid_dispose (GObject *object)
{
  TmGrid *self = TM_GRID (object);

  if (self->hadj != NULL)
    {
      g_signal_handlers_disconnect_by_data (self->hadj, self);
      g_signal_handlers_disconnect_by_data (self->vadj, self);
    }
  g_clear_object (&self->hadj);
  g_clear_object (&self->vadj);
  g_clear_pointer (&self->font, pango_font_description_free);
  g_clear_pointer (&self->edit_text, g_free);
  G_OBJECT_CLASS (tm_grid_parent_class)->dispose (object);
}

static void
tm_grid_class_init (TmGridClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = tm_grid_dispose;
  widget_class->snapshot = tm_grid_snapshot;
  widget_class->size_allocate = tm_grid_size_allocate;
  widget_class->measure = tm_grid_measure;

  signals[SIGNAL_SELECTION_CHANGED] =
    g_signal_new ("selection-changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL, G_TYPE_NONE, 0);
  /* The text to start the edit with, or NULL to edit what is there. */
  signals[SIGNAL_EDIT] =
    g_signal_new ("edit", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_STRING);
  /* A TmRange pointed at, for the formula being typed. */
  signals[SIGNAL_POINT] =
    g_signal_new ("point", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_POINTER);

  gtk_widget_class_set_css_name (widget_class, "tmgrid");
}

static void
tm_grid_init (TmGrid *self)
{
  GtkEventController *key, *motion, *scroll;
  GtkGesture *click, *drag;

  self->resizing_col = -1;
  self->font = pango_font_description_from_string ("Sans 10");
  self->hadj = g_object_ref_sink (gtk_adjustment_new (0, 0, 10, 1, 5, 10));
  self->vadj = g_object_ref_sink (gtk_adjustment_new (0, 0, 100, 1, 20, 20));
  g_signal_connect (self->hadj, "value-changed", G_CALLBACK (on_adjustment_changed), self);
  g_signal_connect (self->vadj, "value-changed", G_CALLBACK (on_adjustment_changed), self);

  gtk_widget_set_focusable (GTK_WIDGET (self), TRUE);
  gtk_widget_set_focus_on_click (GTK_WIDGET (self), TRUE);
  gtk_widget_set_hexpand (GTK_WIDGET (self), TRUE);
  gtk_widget_set_vexpand (GTK_WIDGET (self), TRUE);

  key = gtk_event_controller_key_new ();
  g_signal_connect (key, "key-pressed", G_CALLBACK (on_key_pressed), self);
  gtk_widget_add_controller (GTK_WIDGET (self), key);

  click = gtk_gesture_click_new ();
  g_signal_connect (click, "pressed", G_CALLBACK (on_pressed), self);
  gtk_widget_add_controller (GTK_WIDGET (self), GTK_EVENT_CONTROLLER (click));

  drag = gtk_gesture_drag_new ();
  g_signal_connect (drag, "drag-begin", G_CALLBACK (on_drag_begin), self);
  g_signal_connect (drag, "drag-update", G_CALLBACK (on_drag_update), self);
  g_signal_connect (drag, "drag-end", G_CALLBACK (on_drag_end), self);
  gtk_widget_add_controller (GTK_WIDGET (self), GTK_EVENT_CONTROLLER (drag));

  motion = gtk_event_controller_motion_new ();
  g_signal_connect (motion, "motion", G_CALLBACK (on_motion), self);
  gtk_widget_add_controller (GTK_WIDGET (self), motion);

  scroll = gtk_event_controller_scroll_new (GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES);
  g_signal_connect (scroll, "scroll", G_CALLBACK (on_scroll), self);
  gtk_widget_add_controller (GTK_WIDGET (self), scroll);
}

GtkWidget *
tm_grid_new (TmSheet *sheet)
{
  TmGrid *self = g_object_new (TM_TYPE_GRID, NULL);

  self->sheet = sheet;
  return GTK_WIDGET (self);
}

void
tm_grid_set_sheet (TmGrid *self, TmSheet *sheet)
{
  self->sheet = sheet;
  self->cursor.row = self->cursor.col = 0;
  self->anchor = self->cursor;
  self->top_row = self->left_col = 0;
  update_adjustments (self);
  gtk_widget_queue_draw (GTK_WIDGET (self));
  g_signal_emit (self, signals[SIGNAL_SELECTION_CHANGED], 0);
}

GtkAdjustment *tm_grid_get_hadjustment (TmGrid *self) { return self->hadj; }
GtkAdjustment *tm_grid_get_vadjustment (TmGrid *self) { return self->vadj; }

void
tm_grid_refresh (TmGrid *self)
{
  update_adjustments (self);
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

void
tm_grid_show_edit (TmGrid *self, TmRef cell, const char *text, int caret)
{
  g_free (self->edit_text);
  self->edit_text = g_strdup (text);
  self->edit_cell = cell;
  self->edit_caret = caret;
  if (text == NULL)
    self->has_point = FALSE;
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

void
tm_grid_set_pointing (TmGrid *self, gboolean pointing)
{
  self->pointing = pointing;
  gtk_widget_set_cursor_from_name (GTK_WIDGET (self), pointing ? "cell" : NULL);
}

void
tm_grid_show_point (TmGrid *self, const TmRange *range)
{
  self->has_point = range != NULL;
  if (range != NULL)
    self->point = *range;
  gtk_widget_queue_draw (GTK_WIDGET (self));
}
