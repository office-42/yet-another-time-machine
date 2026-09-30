/* tm-mapview.c - maps and heatmaps for the forecast panel
 *
 * Copyright (C) 2026 The timemachine authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "tm-mapview.h"

#include <math.h>
#include <string.h>

/* ---- Colour ----------------------------------------------------------- */

/* A perceptually even scale from dark purple through teal to yellow, after
 * matplotlib's viridis: readable in grey, and by the colour-blind. */
static void
scale_colour (double t, double rgb[3])
{
  static const double stops[5][3] = {
    { 0.267, 0.005, 0.329 },
    { 0.231, 0.322, 0.545 },
    { 0.129, 0.569, 0.549 },
    { 0.369, 0.788, 0.384 },
    { 0.992, 0.906, 0.145 },
  };
  double x;
  int i;

  if (isnan (t))
    {
      rgb[0] = rgb[1] = rgb[2] = 0.93;
      return;
    }
  x = CLAMP (t, 0, 1) * 4;
  i = MIN (3, (int) floor (x));
  x -= i;
  for (int k = 0; k < 3; k++)
    rgb[k] = stops[i][k] + x * (stops[i + 1][k] - stops[i][k]);
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

static char *
short_number (double v)
{
  double a = fabs (v);

  if (a >= 1e6)
    return g_strdup_printf ("%.3gM", v / 1e6);
  if (a >= 1e4)
    return g_strdup_printf ("%.3gk", v / 1e3);
  return g_strdup_printf ("%.3g", v);
}

/* A colour bar with its ends labelled, bottom right. */
static void
draw_legend (cairo_t *cr, PangoLayout *layout, int width, int height,
             double lo, double hi, gboolean percent)
{
  double x0 = width - 130, y0 = height - 20, w = 90;
  char *a, *b;

  for (int i = 0; i < (int) w; i++)
    {
      double rgb[3];

      scale_colour (i / (w - 1), rgb);
      cairo_set_source_rgb (cr, rgb[0], rgb[1], rgb[2]);
      cairo_rectangle (cr, x0 + i, y0, 1.2, 8);
      cairo_fill (cr);
    }
  if (percent)
    {
      a = g_strdup_printf ("%.0f%%", lo * 100);
      b = g_strdup_printf ("%.0f%%", hi * 100);
    }
  else
    {
      a = short_number (lo);
      b = short_number (hi);
    }
  cairo_set_source_rgb (cr, 0.35, 0.36, 0.40);
  text_at (cr, layout, a, x0 - 4, y0 + 4, 1, 0.5);
  text_at (cr, layout, b, x0 + w + 4, y0 + 4, 0, 0.5);
  g_free (a);
  g_free (b);
}

/* A cell's number for display: its simulated mean if it was simulated
 * (the chance it is true, for TRUE/FALSE), otherwise its value; NaN if it
 * has neither. */
static double
cell_number (TmSheet *sheet, TmSim *sim, int row, int col, gboolean *simulated)
{
  TmSimStats s;
  const TmValue *v;

  if (sim != NULL && tm_sim_stats (sim, row, col, &s) && s.valid > 0)
    {
      *simulated = TRUE;
      return s.mean;
    }
  v = tm_sheet_get_value (sheet, row, col);
  if (v->type == TM_VALUE_NUMBER)
    return v->as.number;
  if (v->type == TM_VALUE_BOOL)
    return v->as.boolean;
  return NAN;
}

/* ---- Heatmap ---------------------------------------------------------- */

void
tm_draw_heatmap (cairo_t *cr, PangoLayout *layout, int width, int height,
                 TmSheet *sheet, const TmRange *range)
{
  TmSim *sim = tm_sheet_get_sim (sheet);
  int rows = tm_range_rows (range), cols = tm_range_cols (range);
  int step = MAX (1, MAX (rows, cols) / 200);
  int nr = (rows + step - 1) / step, nc = (cols + step - 1) / step;
  double *v = g_new (double, (gsize) nr * nc);
  double lo = INFINITY, hi = -INFINITY, cell, x0, y0;
  gboolean simulated = FALSE, unit = TRUE;
  int any = 0;

  for (int i = 0; i < nr; i++)
    for (int j = 0; j < nc; j++)
      {
        double x = cell_number (sheet, sim, range->row0 + i * step, range->col0 + j * step, &simulated);

        v[i * nc + j] = x;
        if (!isnan (x))
          {
            lo = MIN (lo, x);
            hi = MAX (hi, x);
            unit = unit && x >= 0 && x <= 1;
            any++;
          }
      }
  if (any == 0)
    {
      cairo_set_source_rgb (cr, 0.55, 0.57, 0.62);
      text_at (cr, layout, "No numbers here to draw.", width / 2.0, height / 2.0, 0.5, 0.5);
      g_free (v);
      return;
    }
  /* Chances are drawn on their own scale, 0 to 1, so that two maps of
   * them can be compared by eye. */
  if (simulated && unit)
    {
      lo = 0;
      hi = 1;
    }
  if (hi <= lo)
    hi = lo + 1;

  cell = MIN ((width - 20.0) / nc, (height - 44.0) / nr);
  x0 = (width - cell * nc) / 2;
  y0 = 10 + (height - 44 - cell * nr) / 2;
  for (int i = 0; i < nr; i++)
    for (int j = 0; j < nc; j++)
      {
        double rgb[3], x = v[i * nc + j];

        scale_colour (isnan (x) ? NAN : (x - lo) / (hi - lo), rgb);
        cairo_set_source_rgb (cr, rgb[0], rgb[1], rgb[2]);
        cairo_rectangle (cr, x0 + j * cell, y0 + i * cell, cell + 0.5, cell + 0.5);
        cairo_fill (cr);
      }
  cairo_set_source_rgb (cr, 0.35, 0.36, 0.40);
  text_at (cr, layout, simulated ? (unit ? "chance, over the futures" : "mean, over the futures")
                                 : "values",
           12, height - 16, 0, 0.5);
  draw_legend (cr, layout, width, height, lo, hi, simulated && unit);
  g_free (v);
}

/* ---- Map -------------------------------------------------------------- */

typedef struct {
  double lat, lon;               /* the value, or the median if simulated */
  const double *slat, *slon;     /* samples, or NULL */
  int n;
  double value;                  /* NaN if none */
  char *label;
} Place;

/* The name a line of cells goes by: a text cell at its head, looking up
 * from its first cell (for a column) or left from it (for a row). */
static char *
line_name (TmSheet *sheet, int row, int col, gboolean column, int reach)
{
  for (int k = 0; k <= reach; k++)
    {
      int r = column ? row - k : row, c = column ? col : col - k;
      const TmValue *v;

      if (r < 0 || c < 0)
        break;
      v = tm_sheet_get_value (sheet, r, c);
      if (v->type == TM_VALUE_TEXT)
        return g_ascii_strdown (v->as.text, -1);
      if (k > 0 && v->type != TM_VALUE_EMPTY && !column)
        continue;
    }
  return NULL;
}

static gboolean
is_lat (const char *name)
{
  return name != NULL && strstr (name, "lat") != NULL;
}

static gboolean
is_lon (const char *name)
{
  return name != NULL && (strstr (name, "lon") != NULL || strstr (name, "lng") != NULL);
}

static double
median_of (TmSim *sim, int row, int col)
{
  TmSimStats s;

  return tm_sim_stats (sim, row, col, &s) ? s.p50 : NAN;
}

/* The places in the selection, if it holds any. */
static GArray *
find_places (TmSheet *sheet, const TmRange *r, gboolean *track, gboolean *has_values)
{
  TmSim *sim = tm_sheet_get_sim (sheet);
  GArray *places = g_array_new (FALSE, TRUE, sizeof (Place));
  int lat_line = -1, lon_line = -1, value_line = -1, label_line = -1;
  gboolean across = FALSE;

  *track = FALSE;
  *has_values = FALSE;

  /* Rows named lat and lon, their places across the columns. */
  if (tm_range_rows (r) >= 2)
    for (int row = r->row0; row <= r->row1; row++)
      {
        char *name = line_name (sheet, row, r->col0, FALSE, 3);

        if (is_lat (name) && lat_line < 0)
          lat_line = row;
        else if (is_lon (name) && lon_line < 0)
          lon_line = row;
        g_free (name);
      }
  if (lat_line >= 0 && lon_line >= 0)
    across = TRUE;
  else
    {
      /* Columns headed lat and lon, their places down the rows. */
      lat_line = lon_line = -1;
      for (int col = r->col0; col <= r->col1; col++)
        {
          char *name = line_name (sheet, r->row0, col, TRUE, 3);

          if (is_lat (name) && lat_line < 0)
            lat_line = col;
          else if (is_lon (name) && lon_line < 0)
            lon_line = col;
          g_free (name);
        }
      if (lat_line < 0 || lon_line < 0)
        return places;
      for (int col = r->col0; col <= r->col1; col++)
        {
          const TmValue *v;
          int probe = r->row0;

          if (col == lat_line || col == lon_line)
            continue;
          /* The first row may be the headings. */
          if (tm_sheet_get_value (sheet, probe, lat_line)->type == TM_VALUE_TEXT && probe < r->row1)
            probe++;
          v = tm_sheet_get_value (sheet, probe, col);
          if (v->type == TM_VALUE_TEXT && label_line < 0)
            label_line = col;
          else if (v->type != TM_VALUE_TEXT && v->type != TM_VALUE_EMPTY && value_line < 0)
            value_line = col;
        }
    }

  {
    int n = across ? tm_range_cols (r) : tm_range_rows (r);

    for (int i = 0; i < n; i++)
      {
        int lr = across ? lat_line : r->row0 + i, lc = across ? r->col0 + i : lat_line;
        int or = across ? lon_line : r->row0 + i, oc = across ? r->col0 + i : lon_line;
        const TmValue *a = tm_sheet_get_value (sheet, lr, lc), *o = tm_sheet_get_value (sheet, or, oc);
        Place p = { 0 };
        int na = 0, no = 0;

        if (a->type != TM_VALUE_NUMBER || o->type != TM_VALUE_NUMBER)
          continue;           /* a heading, or a gap */
        p.lat = a->as.number;
        p.lon = o->as.number;
        p.value = NAN;
        if (sim != NULL)
          {
            p.slat = tm_sim_samples (sim, lr, lc, FALSE, &na);
            p.slon = tm_sim_samples (sim, or, oc, FALSE, &no);
            if (p.slat != NULL || p.slon != NULL)
              {
                p.n = MAX (na, no);
                if (p.slat != NULL)
                  p.lat = median_of (sim, lr, lc);
                if (p.slon != NULL)
                  p.lon = median_of (sim, or, oc);
              }
          }
        if (value_line >= 0)
          {
            gboolean simulated = FALSE;
            p.value = cell_number (sheet, sim, lr, value_line, &simulated);
            *has_values = *has_values || !isnan (p.value);
          }
        if (label_line >= 0)
          {
            const TmValue *t = tm_sheet_get_value (sheet, lr, label_line);
            if (t->type == TM_VALUE_TEXT)
              p.label = g_strdup (t->as.text);
          }
        g_array_append_val (places, p);
      }
  }
  *track = across && places->len >= 2;
  return places;
}

/* The projection: longitude and latitude to the panel, equirectangular
 * with longitudes shrunk by the cosine of the middle latitude, so that
 * shapes near it keep their proportions. */
typedef struct {
  double west, south, east, north;
  double k, kx, x0, y0;
} View;

static void
view_fit (View *v, double west, double south, double east, double north,
          double x, double y, double w, double h)
{
  double c, span_x, span_y;

  v->west = west;
  v->south = south;
  v->east = east;
  v->north = north;
  c = cos ((south + north) / 2 * G_PI / 180);
  span_x = MAX (1e-6, (east - west) * c);
  span_y = MAX (1e-6, north - south);
  v->k = MIN (w / span_x, h / span_y);
  v->kx = v->k * c;
  v->x0 = x + (w - span_x * v->k) / 2;
  v->y0 = y + (h - span_y * v->k) / 2;
}

#define PX(v, lon) ((v)->x0 + ((lon) - (v)->west) * (v)->kx)
#define PY(v, lat) ((v)->y0 + ((v)->north - (lat)) * (v)->k)

static void
ring_path (cairo_t *cr, const View *v, const TmRing *ring, gboolean close)
{
  for (int i = 0; i < ring->n; i++)
    {
      double x = PX (v, ring->xy[2 * i]), y = PY (v, ring->xy[2 * i + 1]);

      /* An edge across the antimeridian would streak across the map. */
      if (i == 0 || fabs (ring->xy[2 * i] - ring->xy[2 * i - 2]) > 180)
        cairo_move_to (cr, x, y);
      else
        cairo_line_to (cr, x, y);
    }
  if (close)
    cairo_close_path (cr);
}

static void
draw_layers (cairo_t *cr, const View *v, TmSheet *sheet)
{
  char **maps = tm_sheet_map_names (sheet);

  for (int m = 0; maps[m] != NULL; m++)
    {
      TmMap *map = tm_sheet_get_map (sheet, maps[m]);

      for (guint i = 0; i < map->features->len; i++)
        {
          TmFeature *f = g_ptr_array_index (map->features, i);

          if (f->east < v->west || f->west > v->east || f->north < v->south || f->south > v->north)
            continue;
          cairo_new_path (cr);
          for (guint p = 0; p < f->polygons->len; p++)
            {
              GPtrArray *poly = g_ptr_array_index (f->polygons, p);
              for (guint h = 0; h < poly->len; h++)
                ring_path (cr, v, g_ptr_array_index (poly, h), TRUE);
            }
          cairo_set_fill_rule (cr, CAIRO_FILL_RULE_EVEN_ODD);
          cairo_set_source_rgb (cr, 0.955, 0.945, 0.915);
          cairo_fill_preserve (cr);
          cairo_set_source_rgb (cr, 0.70, 0.69, 0.66);
          cairo_set_line_width (cr, 0.7);
          cairo_stroke (cr);
          for (guint l = 0; l < f->lines->len; l++)
            ring_path (cr, v, g_ptr_array_index (f->lines, l), FALSE);
          cairo_set_source_rgb (cr, 0.45, 0.55, 0.75);
          cairo_stroke (cr);
          for (guint k = 0; k + 1 < f->points->len; k += 2)
            {
              cairo_rectangle (cr, PX (v, g_array_index (f->points, double, k)) - 1.5,
                               PY (v, g_array_index (f->points, double, k + 1)) - 1.5, 3, 3);
              cairo_fill (cr);
            }
        }
    }
  g_strfreev (maps);
}

/* Pictures that know where they are, under everything else. */
static void
draw_pictures (cairo_t *cr, const View *v, TmSheet *sheet)
{
  char **images = tm_sheet_image_names (sheet);

  for (int m = 0; images[m] != NULL; m++)
    {
      TmImage *im = tm_sheet_get_image (sheet, images[m]);
      cairo_surface_t *s;
      unsigned char *data;
      int stride;

      if (!im->has_bounds || im->east <= im->west || im->north <= im->south
          || im->east < v->west || im->west > v->east || im->north < v->south || im->south > v->north)
        continue;
      s = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, im->width, im->height);
      data = cairo_image_surface_get_data (s);
      stride = cairo_image_surface_get_stride (s);
      for (int y = 0; y < im->height; y++)
        for (int x = 0; x < im->width; x++)
          {
            const float *p = im->rgba + ((gsize) y * im->width + x) * 4;
            guint32 a = (guint32) (p[3] * 255), r = (guint32) (p[0] * p[3] * 255);
            guint32 g = (guint32) (p[1] * p[3] * 255), b = (guint32) (p[2] * p[3] * 255);

            *(guint32 *) (data + y * stride + x * 4) = (a << 24) | (r << 16) | (g << 8) | b;
          }
      cairo_surface_mark_dirty (s);
      cairo_save (cr);
      cairo_translate (cr, PX (v, im->west), PY (v, im->north));
      cairo_scale (cr, (PX (v, im->east) - PX (v, im->west)) / im->width,
                   (PY (v, im->south) - PY (v, im->north)) / im->height);
      cairo_set_source_surface (cr, s, 0, 0);
      cairo_paint_with_alpha (cr, 0.85);
      cairo_restore (cr);
      cairo_surface_destroy (s);
    }
  g_strfreev (images);
}

static double
nice_step (double span)
{
  double raw = span / 4, mag = pow (10, floor (log10 (raw))), f = raw / mag;

  return (f < 1.5 ? 1 : f < 3.5 ? 2 : f < 7.5 ? 5 : 10) * mag;
}

gboolean
tm_draw_map (cairo_t *cr, PangoLayout *layout, int width, int height,
             TmSheet *sheet, const TmRange *range)
{
  gboolean track, has_values, sampled = FALSE;
  GArray *places = find_places (sheet, range, &track, &has_values);
  double west = INFINITY, east = -INFINITY, south = INFINITY, north = -INFINITY;
  double vlo = INFINITY, vhi = -INFINITY;
  View view;

  if (places->len == 0)
    {
      g_array_free (places, TRUE);
      return FALSE;
    }

  /* The extent: every place, and the middle 96% of every cloud. */
  for (guint i = 0; i < places->len; i++)
    {
      Place *p = &g_array_index (places, Place, i);

      west = MIN (west, p->lon);
      east = MAX (east, p->lon);
      south = MIN (south, p->lat);
      north = MAX (north, p->lat);
      for (int k = 0; k < p->n; k += MAX (1, p->n / 400))
        {
          double a = p->slat != NULL ? p->slat[k] : p->lat, o = p->slon != NULL ? p->slon[k] : p->lon;

          if (isnan (a) || isnan (o))
            continue;
          sampled = TRUE;
          west = MIN (west, o);
          east = MAX (east, o);
          south = MIN (south, a);
          north = MAX (north, a);
        }
      if (!isnan (p->value))
        {
          vlo = MIN (vlo, p->value);
          vhi = MAX (vhi, p->value);
        }
    }
  {
    double mx = MAX (0.25, (east - west) * 0.15), my = MAX (0.25, (north - south) * 0.15);
    char **images = tm_sheet_image_names (sheet);

    west = MAX (-180, west - mx);
    east = MIN (180, east + mx);
    south = MAX (-85, south - my);
    north = MIN (85, north + my);
    /* Places on a picture that knows where it is -- home on a radar
     * frame -- are shown with the whole of it around them. */
    for (int i = 0; images[i] != NULL; i++)
      {
        TmImage *im = tm_sheet_get_image (sheet, images[i]);

        if (im->has_bounds && im->west <= west && im->east >= east
            && im->south <= south && im->north >= north)
          {
            west = im->west;
            east = im->east;
            south = im->south;
            north = im->north;
          }
      }
    g_strfreev (images);
  }
  view_fit (&view, west, south, east, north, 8, 8, width - 16, height - 40);

  /* Sea, pictures, land, a graticule. */
  cairo_save (cr);
  cairo_rectangle (cr, view.x0, view.y0, (east - west) * view.kx, (north - south) * view.k);
  cairo_clip_preserve (cr);
  cairo_set_source_rgb (cr, 0.905, 0.937, 0.973);
  cairo_fill (cr);
  /* Land under the pictures, which are drawn a little translucent; a
   * radar frame whose dry parts are transparent shows the coast through. */
  draw_layers (cr, &view, sheet);
  draw_pictures (cr, &view, sheet);
  {
    double step = nice_step (MAX (east - west, north - south));

    cairo_set_source_rgba (cr, 0.3, 0.35, 0.45, 0.18);
    cairo_set_line_width (cr, 0.6);
    for (double lo = ceil (west / step) * step; lo <= east; lo += step)
      {
        cairo_move_to (cr, PX (&view, lo), view.y0);
        cairo_line_to (cr, PX (&view, lo), PY (&view, south));
      }
    for (double la = ceil (south / step) * step; la <= north; la += step)
      {
        cairo_move_to (cr, view.x0, PY (&view, la));
        cairo_line_to (cr, PX (&view, east), PY (&view, la));
      }
    cairo_stroke (cr);
  }

  /* The futures: a cloud of positions, or a faint track for each. */
  cairo_set_line_width (cr, 0.8);
  cairo_set_line_join (cr, CAIRO_LINE_JOIN_ROUND);
  if (sampled)
    {
      int n = 0;

      for (guint i = 0; i < places->len; i++)
        n = MAX (n, g_array_index (places, Place, i).n);
      if (track)
        for (int k = 0; k < MIN (n, 120); k++)
          {
            int it = (int) (((gint64) k * 7919 + 13) % n);
            gboolean pen = FALSE;

            for (guint i = 0; i < places->len; i++)
              {
                Place *p = &g_array_index (places, Place, i);
                double a = p->slat != NULL && it < p->n ? p->slat[it] : p->lat;
                double o = p->slon != NULL && it < p->n ? p->slon[it] : p->lon;

                if (isnan (a) || isnan (o))
                  {
                    pen = FALSE;
                    continue;
                  }
                if (pen)
                  cairo_line_to (cr, PX (&view, o), PY (&view, a));
                else
                  cairo_move_to (cr, PX (&view, o), PY (&view, a));
                pen = TRUE;
              }
            cairo_set_source_rgba (cr, 0.490, 0.360, 0.900, 0.16);
            cairo_stroke (cr);
          }
      else
        for (guint i = 0; i < places->len; i++)
          {
            Place *p = &g_array_index (places, Place, i);

            for (int k = 0; k < MIN (p->n, 4000); k++)
              {
                int it = p->n > 4000 ? (int) (((gint64) k * 7919 + 13) % p->n) : k;
                double a = p->slat != NULL ? p->slat[it] : p->lat;
                double o = p->slon != NULL ? p->slon[it] : p->lon;

                if (!isnan (a) && !isnan (o))
                  cairo_rectangle (cr, PX (&view, o) - 1, PY (&view, a) - 1, 2, 2);
              }
            cairo_set_source_rgba (cr, 0.490, 0.360, 0.900, 0.10);
            cairo_fill (cr);
          }
    }

  /* The places themselves: the track's median line, then the points,
   * coloured by their value if they have one. */
  if (track)
    {
      for (guint i = 0; i < places->len; i++)
        {
          Place *p = &g_array_index (places, Place, i);
          if (i == 0)
            cairo_move_to (cr, PX (&view, p->lon), PY (&view, p->lat));
          else
            cairo_line_to (cr, PX (&view, p->lon), PY (&view, p->lat));
        }
      cairo_set_source_rgb (cr, 0.20, 0.21, 0.25);
      cairo_set_line_width (cr, 2);
      cairo_stroke (cr);
    }
  for (guint i = 0; i < places->len; i++)
    {
      Place *p = &g_array_index (places, Place, i);
      double x = PX (&view, p->lon), y = PY (&view, p->lat), rgb[3];

      if (has_values)
        scale_colour (vhi > vlo ? (p->value - vlo) / (vhi - vlo) : 0.5, rgb);
      else
        rgb[0] = 0.20, rgb[1] = 0.21, rgb[2] = 0.25;
      /* A path of its own: the label before it left a current point, and
       * an arc would join it with a line. */
      cairo_new_path (cr);
      cairo_arc (cr, x, y, has_values ? 5 : 3.5, 0, 2 * G_PI);
      cairo_set_source_rgb (cr, rgb[0], rgb[1], rgb[2]);
      cairo_fill_preserve (cr);
      cairo_set_source_rgb (cr, 1, 1, 1);
      cairo_set_line_width (cr, 1);
      cairo_stroke (cr);
      if (p->label != NULL && places->len <= 30)
        {
          cairo_set_source_rgb (cr, 0.20, 0.21, 0.25);
          text_at (cr, layout, p->label, x + 7, y, 0, 0.5);
        }
    }
  cairo_restore (cr);

  cairo_set_source_rgb (cr, 0.35, 0.36, 0.40);
  {
    char *where = g_strdup_printf ("%.2f° to %.2f° N, %.2f° to %.2f° E", south, north, west, east);
    /* Above the colour scale when there is one, which has the bottom. */
    text_at (cr, layout, where, 10, height - (has_values ? 36 : 16), 0, 0.5);
    g_free (where);
  }
  if (has_values && vhi >= vlo)
    draw_legend (cr, layout, width, height, vlo, vhi > vlo ? vhi : vlo + 1, FALSE);

  for (guint i = 0; i < places->len; i++)
    g_free (g_array_index (places, Place, i).label);
  g_array_free (places, TRUE);
  return TRUE;
}
