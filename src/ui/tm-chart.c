/* tm-chart.c - pictures of the futures a simulation found
 *
 * Copyright (C) 2026 The timemachine authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "tm-chart.h"
#include "tm-numfmt.h"

#include <math.h>
#include <string.h>

#define BINS_MAX 48
#define MARGIN_L 52
#define MARGIN_R 14
#define MARGIN_T 26
#define MARGIN_B 30

typedef struct {
  double p5, p25, p50, p75, p95, mean;
  gboolean simulated;   /* FALSE: a plain fact, all percentiles alike */
  gboolean valid;
  char *label;
} Point;

struct _TmChart {
  GtkWidget parent_instance;

  TmSheet *sheet;
  TmRange range;
  gboolean has_range;
};

G_DEFINE_FINAL_TYPE (TmChart, tm_chart, GTK_TYPE_WIDGET)

typedef struct { double r, g, b; } Rgb;

static const Rgb INK       = { 0.20, 0.21, 0.25 };
static const Rgb FAINT     = { 0.55, 0.57, 0.62 };
static const Rgb RULE      = { 0.88, 0.89, 0.91 };
static const Rgb BAR       = { 0.490, 0.360, 0.900 };
static const Rgb BAR_LOSS  = { 0.880, 0.380, 0.330 };
static const Rgb FAN       = { 0.490, 0.360, 0.900 };
static const Rgb HISTORY   = { 0.20, 0.21, 0.25 };

static void
set_rgb (cairo_t *cr, Rgb c, double alpha)
{
  cairo_set_source_rgba (cr, c.r, c.g, c.b, alpha);
}

/* 1, 2 or 5 times a power of ten, giving about target ticks. */
static double
nice_step (double span, int target)
{
  double raw = span / MAX (target, 1), mag, f;

  if (!(raw > 0))
    return 1;
  mag = pow (10, floor (log10 (raw)));
  f = raw / mag;
  return (f < 1.5 ? 1 : f < 3 ? 2 : f < 7 ? 5 : 10) * mag;
}

/* 12.5k, 3.1M: axis labels have little room. */
static char *
compact (double v, double step)
{
  double a = fabs (v);
  const char *suffix = "";
  double div = 1;

  if (a >= 1e9)       { div = 1e9; suffix = "G"; }
  else if (a >= 1e6)  { div = 1e6; suffix = "M"; }
  else if (a >= 1e4)  { div = 1e3; suffix = "k"; }
  {
    double s = step / div;
    int decimals = s >= 1 ? 0 : (int) ceil (-log10 (s) - 1e-9);
    decimals = CLAMP (decimals, 0, 6);
    if (fabs (v) < step * 1e-6)
      v = 0;
    return g_strdup_printf ("%.*f%s", decimals, v / div, suffix);
  }
}

/* An axis label, as a percentage when the cell is shown as one. */
static char *
axis_label (double v, double step, gboolean percent)
{
  char *s, *t;

  if (!percent)
    return compact (v, step);
  s = compact (v * 100, step * 100);
  t = g_strconcat (s, "%", NULL);
  g_free (s);
  return t;
}

static void
text_at (cairo_t *cr, PangoLayout *layout, const char *text, double x, double y,
         double xalign, double yalign)
{
  int w, h;

  pango_layout_set_text (layout, text, -1);
  pango_layout_get_pixel_size (layout, &w, &h);
  cairo_move_to (cr, x - w * xalign, y - h * yalign);
  pango_cairo_show_layout (cr, layout);
}

static void
draw_message (cairo_t *cr, PangoLayout *layout, int width, int height, const char *text)
{
  pango_layout_set_width (layout, (width - 40) * PANGO_SCALE);
  pango_layout_set_alignment (layout, PANGO_ALIGN_CENTER);
  pango_layout_set_wrap (layout, PANGO_WRAP_WORD);
  set_rgb (cr, FAINT, 1);
  text_at (cr, layout, text, width / 2.0, height / 2.0, 0.5, 0.5);
  pango_layout_set_width (layout, -1);
}

static void
draw_x_axis (cairo_t *cr, PangoLayout *layout, double lo, double hi,
             double x0, double x1, double y, gboolean percent)
{
  double step = nice_step (hi - lo, MAX (2, (int) ((x1 - x0) / 70)));

  set_rgb (cr, RULE, 1);
  cairo_set_line_width (cr, 1);
  cairo_move_to (cr, x0, y + 0.5);
  cairo_line_to (cr, x1, y + 0.5);
  cairo_stroke (cr);
  set_rgb (cr, FAINT, 1);
  for (double t = ceil (lo / step) * step; t <= hi + step * 1e-9; t += step)
    {
      double x = x0 + (t - lo) / (hi - lo) * (x1 - x0);
      char *s = axis_label (t, step, percent);

      cairo_move_to (cr, x + 0.5, y);
      cairo_line_to (cr, x + 0.5, y + 4);
      cairo_stroke (cr);
      text_at (cr, layout, s, x, y + 6, 0.5, 0);
      g_free (s);
    }
}

static void
marker (cairo_t *cr, PangoLayout *layout, double x, double top, double bottom,
        const char *label, gboolean dashed, int slot)
{
  double dash[] = { 3, 3 };

  set_rgb (cr, INK, 0.85);
  cairo_set_line_width (cr, 1.2);
  if (dashed)
    cairo_set_dash (cr, dash, 2, 0);
  cairo_move_to (cr, floor (x) + 0.5, top);
  cairo_line_to (cr, floor (x) + 0.5, bottom);
  cairo_stroke (cr);
  cairo_set_dash (cr, NULL, 0, 0);
  text_at (cr, layout, label, x, top - 4 - slot * 13, 0.5, 1);
}

static void
draw_histogram (TmChart *self, cairo_t *cr, PangoLayout *layout, int width, int height,
                TmSim *sim, int row, int col)
{
  int bins = CLAMP ((width - MARGIN_L - MARGIN_R) / 7, 10, BINS_MAX);
  int counts[BINS_MAX];
  double lo, hi, x0 = 16, x1 = width - MARGIN_R, y0 = MARGIN_T + 26, y1 = height - MARGIN_B;
  int most = 0;
  TmSimStats s;

  if (!tm_sim_histogram (sim, row, col, bins, &lo, &hi, counts) || !tm_sim_stats (sim, row, col, &s))
    return;
  for (int i = 0; i < bins; i++)
    most = MAX (most, counts[i]);
  if (most == 0)
    return;

  /* Light rules at quarter heights, to read the shape against. */
  set_rgb (cr, RULE, 1);
  cairo_set_line_width (cr, 1);
  for (int k = 1; k <= 4; k++)
    {
      double y = floor (y1 - (y1 - y0) * k / 4.0) + 0.5;
      cairo_move_to (cr, x0, y);
      cairo_line_to (cr, x1, y);
    }
  cairo_stroke (cr);

  for (int i = 0; i < bins; i++)
    {
      double bw = (x1 - x0) / bins;
      double bx = x0 + i * bw;
      double bh = (y1 - y0) * counts[i] / most;
      double centre = lo + (hi - lo) * (i + 0.5) / bins;

      /* Futures in which the cell is below zero -- a loss, a shortfall --
       * are drawn warm when the distribution straddles zero. */
      set_rgb (cr, (lo < 0 && hi > 0 && centre < 0) ? BAR_LOSS : BAR, 0.85);
      cairo_rectangle (cr, bx + 0.5, y1 - bh, MAX (1.0, bw - 1.0), bh);
      cairo_fill (cr);
    }

  draw_x_axis (cr, layout, lo, hi, x0, x1, y1,
               tm_format_is_percent (tm_sheet_get_format (self->sheet, row, col)));

#define XOF(v) (x0 + ((v) - lo) / (hi - lo) * (x1 - x0))
  if (s.p5 >= lo && s.p5 <= hi)
    marker (cr, layout, XOF (s.p5), y0, y1, "P5", TRUE, 0);
  if (s.p95 >= lo && s.p95 <= hi)
    marker (cr, layout, XOF (s.p95), y0, y1, "P95", TRUE, 0);
  if (s.p50 >= lo && s.p50 <= hi)
    marker (cr, layout, XOF (s.p50), y0, y1, "P50", FALSE, 1);
  if (s.mean >= lo && s.mean <= hi)
    {
      double x = XOF (s.mean);

      set_rgb (cr, INK, 1);
      cairo_move_to (cr, x, y1 - 1);
      cairo_line_to (cr, x - 5, y1 + 7);
      cairo_line_to (cr, x + 5, y1 + 7);
      cairo_close_path (cr);
      cairo_fill (cr);
    }
#undef XOF
}

/* The line of headings the fan's axis is labelled with: of the rows above
 * a row of cells (or the columns left of a column), the one with the most
 * cells that hold plain, certain values -- years, months, dates -- the
 * nearest winning a tie.  -1 if none has them for half the points. */
static int
heading_line (TmSheet *sheet, const TmRange *r, gboolean across)
{
  int best = -1, best_count = 0;
  int n = across ? tm_range_cols (r) : tm_range_rows (r);
  int start = across ? r->row0 - 1 : r->col0 - 1;

  for (int line = start; line >= 0 && line >= start - 20; line--)
    {
      int count = 0;

      for (int i = 0; i < n; i++)
        {
          int row = across ? line : r->row0 + i;
          int col = across ? r->col0 + i : line;

          if (tm_sheet_get_input (sheet, row, col) != NULL
              && !tm_sheet_is_random (sheet, row, col))
            count++;
        }
      if (count > best_count)
        {
          best = line;
          best_count = count;
        }
    }
  return best_count * 2 >= n ? best : -1;
}

static char *
point_label (TmSheet *sheet, int row, int col, gboolean across, int heading)
{
  if (heading >= 0)
    {
      const TmValue *v = across ? tm_sheet_get_value (sheet, heading, col)
                                : tm_sheet_get_value (sheet, row, heading);
      if (v->type != TM_VALUE_EMPTY)
        return tm_value_to_text (v);
      return g_strdup ("");
    }
  if (across)
    {
      char name[8];
      return g_strdup (tm_col_name (col, name, sizeof name));
    }
  return g_strdup_printf ("%d", row + 1);
}

static void
draw_fan (TmChart *self, cairo_t *cr, PangoLayout *layout, int width, int height,
          Point *pts, int n, gboolean percent)
{
  double lo = INFINITY, hi = -INFINITY, step;
  double x0 = MARGIN_L, x1 = width - MARGIN_R, y0 = MARGIN_T, y1 = height - MARGIN_B;
  int label_every;

  for (int i = 0; i < n; i++)
    if (pts[i].valid)
      {
        lo = MIN (lo, pts[i].p5);
        hi = MAX (hi, pts[i].p95);
      }
  if (!(hi > lo))
    {
      double pad = fabs (lo) > 0 ? fabs (lo) * 0.1 : 1;
      lo -= pad;
      hi += pad;
    }
  step = nice_step (hi - lo, MAX (2, (int) ((y1 - y0) / 40)));
  lo = floor (lo / step) * step;
  hi = ceil (hi / step) * step;

#define XOF(i) (n > 1 ? x0 + (x1 - x0) * (i) / (double) (n - 1) : (x0 + x1) / 2)
#define YOF(v) (y1 - ((v) - lo) / (hi - lo) * (y1 - y0))

  /* The value axis. */
  cairo_set_line_width (cr, 1);
  for (double t = lo; t <= hi + step * 1e-9; t += step)
    {
      double y = floor (YOF (t)) + 0.5;
      char *s = axis_label (t, step, percent);

      set_rgb (cr, RULE, 1);
      cairo_move_to (cr, x0, y);
      cairo_line_to (cr, x1, y);
      cairo_stroke (cr);
      set_rgb (cr, FAINT, 1);
      text_at (cr, layout, s, x0 - 6, y, 1, 0.5);
      g_free (s);
    }

  /* The bands, outer first, each a polygon along the upper percentile
   * and back along the lower -- over the stretches that were simulated,
   * starting from the last plain fact before them so that the fan opens
   * out of the history rather than beside it. */
  for (int band = 0; band < 2; band++)
    {
      int start = -1;

      for (int i = 0; i <= n; i++)
        {
          gboolean in = i < n && pts[i].valid && pts[i].simulated;

          if (in && start < 0)
            start = (i > 0 && pts[i - 1].valid) ? i - 1 : i;
          if (!in && start >= 0)
            {
              int end = i - 1;

              cairo_new_path (cr);
              for (int k = start; k <= end; k++)
                cairo_line_to (cr, XOF (k), YOF (band == 0 ? pts[k].p95 : pts[k].p75));
              for (int k = end; k >= start; k--)
                cairo_line_to (cr, XOF (k), YOF (band == 0 ? pts[k].p5 : pts[k].p25));
              cairo_close_path (cr);
              set_rgb (cr, FAN, band == 0 ? 0.18 : 0.32);
              cairo_fill (cr);
              start = -1;
            }
        }
    }

  /* The median through the futures, and the history as a darker line. */
  cairo_set_line_width (cr, 2);
  cairo_set_line_join (cr, CAIRO_LINE_JOIN_ROUND);
  for (int pass = 0; pass < 2; pass++)
    {
      gboolean pen = FALSE;

      for (int i = 0; i < n; i++)
        {
          gboolean draw_it = pts[i].valid
                             && (pass == 0 ? !pts[i].simulated
                                           : (pts[i].simulated
                                              || (i + 1 < n && pts[i + 1].simulated)));
          if (!draw_it)
            {
              pen = FALSE;
              continue;
            }
          if (!pen)
            cairo_move_to (cr, XOF (i), YOF (pts[i].p50));
          else
            cairo_line_to (cr, XOF (i), YOF (pts[i].p50));
          pen = TRUE;
        }
      set_rgb (cr, pass == 0 ? HISTORY : FAN, 1);
      cairo_stroke (cr);
    }

  /* Labels along the bottom, thinned to what fits. */
  label_every = MAX (1, (int) ceil (n * 60.0 / MAX (1.0, x1 - x0)));
  set_rgb (cr, FAINT, 1);
  for (int i = 0; i < n; i += label_every)
    text_at (cr, layout, pts[i].label, XOF (i), y1 + 6, 0.5, 0);

  /* A key. */
  {
    double kx = x0, ky = 6;

    set_rgb (cr, FAN, 0.18);
    cairo_rectangle (cr, kx, ky + 2, 14, 10);
    cairo_fill (cr);
    set_rgb (cr, FAINT, 1);
    text_at (cr, layout, "90%", kx + 18, ky, 0, 0);
    kx += 52;
    set_rgb (cr, FAN, 0.32);
    cairo_rectangle (cr, kx, ky + 2, 14, 10);
    cairo_fill (cr);
    set_rgb (cr, FAINT, 1);
    text_at (cr, layout, "50%", kx + 18, ky, 0, 0);
    kx += 52;
    set_rgb (cr, FAN, 1);
    cairo_rectangle (cr, kx, ky + 6, 14, 2);
    cairo_fill (cr);
    set_rgb (cr, FAINT, 1);
    text_at (cr, layout, "median", kx + 18, ky, 0, 0);
  }
#undef XOF
#undef YOF
}

static void
draw (TmChart *self, cairo_t *cr, int width, int height)
{
  PangoLayout *layout = pango_cairo_create_layout (cr);
  PangoFontDescription *font = pango_font_description_from_string ("Sans 8.5");
  TmSim *sim = self->sheet != NULL ? tm_sheet_get_sim (self->sheet) : NULL;
  TmRange *r = &self->range;

  pango_layout_set_font_description (layout, font);
  pango_font_description_free (font);

  cairo_set_source_rgb (cr, 1, 1, 1);
  cairo_paint (cr);

  if (!self->has_range || self->sheet == NULL)
    ;
  else if (r->row0 == r->row1 && r->col0 == r->col1)
    {
      if (sim != NULL && tm_sim_has (sim, r->row0, r->col0))
        draw_histogram (self, cr, layout, width, height, sim, r->row0, r->col0);
      else if (sim == NULL)
        draw_message (cr, layout, width, height,
                      "Press F5 to run the simulation, then pick an uncertain "
                      "cell (the tinted ones) to see its possible futures.");
      else
        draw_message (cr, layout, width, height,
                      "This cell is certain: it has one future.  Pick a tinted cell, "
                      "or select a row of them to see a fan chart.");
    }
  else if (r->row0 == r->row1 || r->col0 == r->col1)
    {
      gboolean across = r->row0 == r->row1;
      int n = across ? tm_range_cols (r) : tm_range_rows (r);
      Point *pts = g_new0 (Point, n);
      int simulated = 0;
      int heading = heading_line (self->sheet, r, across);

      for (int i = 0; i < n; i++)
        {
          int row = across ? r->row0 : r->row0 + i;
          int col = across ? r->col0 + i : r->col0;
          TmSimStats s;

          pts[i].label = point_label (self->sheet, row, col, across, heading);
          if (sim != NULL && tm_sim_stats (sim, row, col, &s) && s.valid > 0)
            {
              pts[i].p5 = s.p5;
              pts[i].p25 = s.p25;
              pts[i].p50 = s.p50;
              pts[i].p75 = s.p75;
              pts[i].p95 = s.p95;
              pts[i].mean = s.mean;
              pts[i].simulated = TRUE;
              pts[i].valid = TRUE;
              simulated++;
            }
          else
            {
              const TmValue *v = tm_sheet_get_value (self->sheet, row, col);
              if (v->type == TM_VALUE_NUMBER)
                {
                  pts[i].p5 = pts[i].p25 = pts[i].p50 = pts[i].p75 = pts[i].p95 =
                    pts[i].mean = v->as.number;
                  pts[i].valid = TRUE;
                }
            }
        }
      if (simulated > 0)
        draw_fan (self, cr, layout, width, height, pts, n,
                  tm_format_is_percent (tm_sheet_get_format (self->sheet, r->row1, r->col1)));
      else
        draw_message (cr, layout, width, height,
                      sim == NULL ? "Press F5 to run the simulation."
                                  : "None of these cells is uncertain.");
      for (int i = 0; i < n; i++)
        g_free (pts[i].label);
      g_free (pts);
    }
  else
    draw_message (cr, layout, width, height,
                  "Select one cell for its histogram, or one row or column of "
                  "cells for a fan chart.");

  g_object_unref (layout);
}

static void
tm_chart_snapshot (GtkWidget *widget, GtkSnapshot *snapshot)
{
  int w = gtk_widget_get_width (widget), h = gtk_widget_get_height (widget);
  cairo_t *cr = gtk_snapshot_append_cairo (snapshot, &GRAPHENE_RECT_INIT (0, 0, w, h));

  draw (TM_CHART (widget), cr, w, h);
  cairo_destroy (cr);
}

static void
tm_chart_measure (GtkWidget *widget, GtkOrientation orientation, int for_size,
                  int *minimum, int *natural, int *min_baseline, int *nat_baseline)
{
  *minimum = orientation == GTK_ORIENTATION_HORIZONTAL ? 220 : 160;
  *natural = orientation == GTK_ORIENTATION_HORIZONTAL ? 360 : 240;
}

static void
tm_chart_class_init (TmChartClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  widget_class->snapshot = tm_chart_snapshot;
  widget_class->measure = tm_chart_measure;
  gtk_widget_class_set_css_name (widget_class, "tmchart");
}

static void
tm_chart_init (TmChart *self)
{
}

GtkWidget *
tm_chart_new (void)
{
  return g_object_new (TM_TYPE_CHART, NULL);
}

void
tm_chart_show (TmChart *self, TmSheet *sheet, const TmRange *range)
{
  self->sheet = sheet;
  self->has_range = range != NULL;
  if (range != NULL)
    self->range = *range;
  gtk_widget_queue_draw (GTK_WIDGET (self));
}
