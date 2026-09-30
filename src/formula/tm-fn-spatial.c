/* tm-fn-spatial.c - pictures, places and maps as data
 *
 * Copyright (C) 2026 The timemachine authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A great deal of what is known about the present, and so about the
 * future, comes as pictures and maps: radar frames of rain, satellite
 * photographs of crops and forest, colour-coded maps of height or risk,
 * the borders of countries and catchments.  These functions read them.
 *
 * IMAGE.* reads a picture loaded as data (Data > Import Picture): a
 * channel at a point, a region's mean, the fraction of it that is green,
 * the value a colour stands for in the picture's legend, and how far the
 * picture moved since the one before -- the first step of an extrapolation
 * nowcast, as radar services make them.
 *
 * GEO.* is great-circle arithmetic and spatial interpolation: inverse
 * distance weighting and ordinary kriging, the second with an error
 * RAND.KRIGE can draw from, so that not knowing the rain between two gauges
 * flows into a simulation like any other uncertainty.
 *
 * MAP.* asks a GeoJSON layer which region a place is in, how far it is
 * from one, how large one is.  And RAND.SPREAD spreads a fire (or a flood,
 * a disease, a rumour) across a grid of fuel, one random future at a time.
 */

#include "tm-fn-private.h"

#include <stdlib.h>
#include <string.h>

#define I "Images"
#define G "Geography"
#define MP "Maps"
#define L "Lookup"

/* ---- Reading arguments ------------------------------------------------ */

static char *
text_arg (TmEvalContext *ctx, TmArg *arg, TmValue *err)
{
  return tm_arg_text (ctx, arg, err);
}

/* Which of two answers a text argument asks for: 0 for any of the first
 * words, 1 for any of the second, -1 for neither.  The word lists end in
 * NULL. */
static int
which_of (const char *which, const char *const *first, const char *const *second)
{
  for (int i = 0; first[i] != NULL; i++)
    if (g_ascii_strcasecmp (which, first[i]) == 0)
      return 0;
  for (int i = 0; second[i] != NULL; i++)
    if (g_ascii_strcasecmp (which, second[i]) == 0)
      return 1;
  return -1;
}

static const char *const LAT_WORDS[] = { "lat", "latitude", "y", NULL };
static const char *const LON_WORDS[] = { "lon", "lng", "long", "longitude", "x", NULL };

static const TmImage *
image_arg (TmEvalContext *ctx, TmArg *arg, TmValue *err)
{
  char *name = text_arg (ctx, arg, err);
  const TmImage *image;

  if (name == NULL)
    return NULL;
  image = ctx->image != NULL ? ctx->image (ctx->data, name) : NULL;
  g_free (name);
  if (image == NULL)
    *err = tm_value_error (TM_ERR_NAME);
  return image;
}

static const TmMap *
map_arg (TmEvalContext *ctx, TmArg *arg, TmValue *err)
{
  char *name = text_arg (ctx, arg, err);
  const TmMap *map;

  if (name == NULL)
    return NULL;
  map = ctx->map != NULL ? ctx->map (ctx->data, name) : NULL;
  g_free (name);
  if (map == NULL)
    *err = tm_value_error (TM_ERR_NAME);
  return map;
}

/* An optional channel name; FALSE with #VALUE! for one it does not know. */
static gboolean
channel_arg (TmEvalContext *ctx, TmArg *args, int n, int i, TmChannel *out, TmValue *err)
{
  char *name;
  gboolean ok;

  if (!HAS_ARG (i))
    {
      *out = TM_CHANNEL_GRAY;
      return TRUE;
    }
  name = text_arg (ctx, &args[i], err);
  if (name == NULL)
    return FALSE;
  ok = tm_channel_parse (name, out);
  g_free (name);
  if (!ok)
    *err = tm_value_error (TM_ERR_VALUE);
  return ok;
}

static TmValue
number_or_na (double d)
{
  return isnan (d) ? tm_value_error (TM_ERR_NA) : tm_value_number (d);
}

/* ---- Images ----------------------------------------------------------- */

static TmValue
fn_image_width (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  const TmImage *image = image_arg (ctx, &args[0], &err);

  return image != NULL ? tm_value_number (image->width) : err;
}

static TmValue
fn_image_height (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  const TmImage *image = image_arg (ctx, &args[0], &err);

  return image != NULL ? tm_value_number (image->height) : err;
}

static TmValue
fn_image_at (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  const TmImage *image = image_arg (ctx, &args[0], &err);
  TmChannel ch;
  double u, v;

  if (image == NULL)
    return err;
  ARG_NUM (1, u);
  ARG_NUM (2, v);
  if (!channel_arg (ctx, args, n, 3, &ch, &err))
    return err;
  return number_or_na (tm_image_at (image, u, v, ch));
}

static TmValue
fn_image_pixel (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  const TmImage *image = image_arg (ctx, &args[0], &err);
  TmChannel ch;
  double x, y;

  if (image == NULL)
    return err;
  ARG_NUM (1, x);
  ARG_NUM (2, y);
  if (!channel_arg (ctx, args, n, 3, &ch, &err))
    return err;
  if (x < 1 || y < 1 || x > image->width || y > image->height)
    return tm_value_error (TM_ERR_REF);
  return tm_value_number (tm_image_pixel (image, (int) x - 1, (int) y - 1, ch));
}

static TmValue
fn_image_geo (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  const TmImage *image = image_arg (ctx, &args[0], &err);
  TmChannel ch;
  double lat, lon;

  if (image == NULL)
    return err;
  ARG_NUM (1, lat);
  ARG_NUM (2, lon);
  if (!channel_arg (ctx, args, n, 3, &ch, &err))
    return err;
  if (!image->has_bounds)
    return tm_value_error (TM_ERR_VALUE);
  return number_or_na (tm_image_at_geo (image, lat, lon, ch));
}

/* Where a place falls on a picture that has bounds, as the fraction u
 * across or v down that IMAGE.AT and IMAGE.LEGEND take. */
static TmValue
fn_image_xy (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  const TmImage *image = image_arg (ctx, &args[0], &err);
  double lat, lon;
  char *which;
  TmValue r;

  if (image == NULL)
    return err;
  ARG_NUM (1, lat);
  ARG_NUM (2, lon);
  if ((which = text_arg (ctx, &args[3], &err)) == NULL)
    return err;
  static const char *const U[] = { "u", "x", NULL }, *const V[] = { "v", "y", NULL };
  int k = which_of (which, U, V);

  if (k < 0 || !image->has_bounds || image->east == image->west || image->north == image->south)
    r = tm_value_error (TM_ERR_VALUE);
  else if (k == 0)
    r = tm_value_number ((lon - image->west) / (image->east - image->west));
  else
    r = tm_value_number ((image->north - lat) / (image->north - image->south));
  g_free (which);
  return r;
}

/* The optional region of IMAGE.MEAN and IMAGE.FRACTION: u0, v0, u1, v1,
 * the whole picture if left out. */
static gboolean
region_args (TmEvalContext *ctx, TmArg *args, int n, int first, double r[4], TmValue *err)
{
  static const double whole[4] = { 0, 0, 1, 1 };

  for (int k = 0; k < 4; k++)
    {
      if (HAS_ARG (first + k))
        {
          if (!tm_arg_number (ctx, &args[first + k], &r[k], err))
            return FALSE;
        }
      else
        r[k] = whole[k];
    }
  return TRUE;
}

static TmValue
fn_image_mean (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  const TmImage *image = image_arg (ctx, &args[0], &err);
  TmChannel ch;
  double r[4];

  if (image == NULL)
    return err;
  if (!channel_arg (ctx, args, n, 1, &ch, &err) || !region_args (ctx, args, n, 2, r, &err))
    return err;
  return number_or_na (tm_image_mean (image, ch, r[0], r[1], r[2], r[3]));
}

static gboolean
criteria_test (double value, gpointer data)
{
  TmValue v = tm_value_number (value);
  return tm_criteria_match (data, &v);
}

static TmValue
fn_image_fraction (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err, crit;
  const TmImage *image = image_arg (ctx, &args[0], &err);
  TmChannel ch;
  TmCriteria c;
  double r[4], f;

  if (image == NULL)
    return err;
  if (!channel_arg (ctx, args, n, 1, &ch, &err) || !region_args (ctx, args, n, 3, r, &err))
    return err;
  crit = tm_arg_scalar (ctx, &args[2]);
  if (crit.type == TM_VALUE_ERROR)
    return crit;
  tm_criteria_parse (&crit, &c);
  tm_value_clear (&crit);
  f = tm_image_fraction (image, ch, r[0], r[1], r[2], r[3], criteria_test, &c);
  tm_criteria_clear (&c);
  return tm_value_number (f);
}

static TmValue
fn_image_otsu (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  const TmImage *image = image_arg (ctx, &args[0], &err);
  TmChannel ch;

  if (image == NULL)
    return err;
  if (!channel_arg (ctx, args, n, 1, &ch, &err))
    return err;
  return tm_value_number (tm_image_otsu (image, ch));
}

/* Block matching over the whole frame costs a few million operations;
 * the answer is kept until the sheet is next worked out. */
typedef struct {
  const TmImage *before, *after;
  double max_shift;
  guint gen;
  double dx, dy;
  gboolean ok;
} Motion;

static TmValue
fn_image_motion (TmEvalContext *ctx, TmArg *args, int n)
{
  static Motion cache[4];
  static int next;
  TmValue err;
  const TmImage *before = image_arg (ctx, &args[0], &err), *after;
  double max_shift;
  char *which;
  guint gen;
  Motion *m = NULL;
  TmValue r;

  if (before == NULL)
    return err;
  if ((after = image_arg (ctx, &args[1], &err)) == NULL)
    return err;
  OPT_NUM (3, max_shift, 0.25);
  if (max_shift <= 0 || max_shift > 0.5)
    return tm_value_error (TM_ERR_NUM);
  if ((which = text_arg (ctx, &args[2], &err)) == NULL)
    return err;
  gen = ctx->generation != NULL ? ctx->generation (ctx->data) : 0;
  for (int i = 0; i < 4; i++)
    if (cache[i].before == before && cache[i].after == after
        && cache[i].max_shift == max_shift && cache[i].gen == gen)
      m = &cache[i];
  if (m == NULL)
    {
      m = &cache[next];
      next = (next + 1) % 4;
      m->before = before;
      m->after = after;
      m->max_shift = max_shift;
      m->gen = gen;
      m->ok = tm_image_motion (before, after, max_shift, &m->dx, &m->dy);
    }
  {
    static const char *const X[] = { "dx", "x", NULL }, *const Y[] = { "dy", "y", NULL };
    int k = which_of (which, X, Y);

    if (!m->ok || k < 0)
      r = tm_value_error (TM_ERR_VALUE);
    else
      r = tm_value_number (k == 1 ? m->dy : m->dx);
  }
  g_free (which);
  return r;
}

/* "#1e88e5", "#18e" or "1e88e5" as red, green and blue from 0 to 1. */
static gboolean
parse_colour (const char *text, double rgb[3])
{
  const char *p = text[0] == '#' ? text + 1 : text;
  gsize len = strlen (p);

  for (gsize i = 0; i < len; i++)
    if (!g_ascii_isxdigit (p[i]))
      return FALSE;
  if (len == 6)
    for (int k = 0; k < 3; k++)
      rgb[k] = (g_ascii_xdigit_value (p[2 * k]) * 16 + g_ascii_xdigit_value (p[2 * k + 1])) / 255.0;
  else if (len == 3)
    for (int k = 0; k < 3; k++)
      rgb[k] = g_ascii_xdigit_value (p[k]) * 17 / 255.0;
  else
    return FALSE;
  return TRUE;
}

/* The value a colour-coded picture shows at a point: the value beside the
 * legend colour nearest the pixel's -- a radar frame's rain rate, a
 * relief map's height, a risk map's class.  #N/A where no legend colour
 * is within tolerance, which is where the picture shows background. */
static TmValue
fn_image_legend (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  const TmImage *image = image_arg (ctx, &args[0], &err);
  double u, v, tolerance, rgb[3], best = INFINITY;
  int nc, nv, found = -1;
  const TmValue **colours, **values;
  TmValue r;

  if (image == NULL)
    return err;
  ARG_NUM (1, u);
  ARG_NUM (2, v);
  OPT_NUM (5, tolerance, 0.15);
  tm_image_rgb_at (image, u, v, rgb);
  if (isnan (rgb[0]))
    return tm_value_error (TM_ERR_NA);
  colours = tm_arg_cells (ctx, &args[3], &nc);
  values = tm_arg_cells (ctx, &args[4], &nv);
  if (nc != nv)
    r = tm_value_error (TM_ERR_NA);
  else
    {
      for (int i = 0; i < nc; i++)
        {
          double c[3], d;

          if (colours[i]->type != TM_VALUE_TEXT || !parse_colour (colours[i]->as.text, c))
            continue;
          d = sqrt ((c[0] - rgb[0]) * (c[0] - rgb[0]) + (c[1] - rgb[1]) * (c[1] - rgb[1])
                    + (c[2] - rgb[2]) * (c[2] - rgb[2])) / sqrt (3.0);
          if (d < best)
            {
              best = d;
              found = i;
            }
        }
      r = found >= 0 && best <= tolerance ? tm_value_copy (values[found]) : tm_value_error (TM_ERR_NA);
    }
  g_free (colours);
  g_free (values);
  return r;
}

/* ---- Geography -------------------------------------------------------- */

static TmValue
fn_geo_distance (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double a, b, c, d;

  ARG_NUM (0, a);
  ARG_NUM (1, b);
  ARG_NUM (2, c);
  ARG_NUM (3, d);
  return tm_value_number (tm_geo_distance (a, b, c, d));
}

static TmValue
fn_geo_bearing (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double a, b, c, d;

  ARG_NUM (0, a);
  ARG_NUM (1, b);
  ARG_NUM (2, c);
  ARG_NUM (3, d);
  return tm_value_number (tm_geo_bearing (a, b, c, d));
}

static TmValue
fn_geo_destination (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double lat, lon, dist, bearing, lat2, lon2;
  char *which;
  TmValue r;

  ARG_NUM (0, lat);
  ARG_NUM (1, lon);
  ARG_NUM (2, dist);
  ARG_NUM (3, bearing);
  if ((which = text_arg (ctx, &args[4], &err)) == NULL)
    return err;
  tm_geo_destination (lat, lon, dist, bearing, &lat2, &lon2);
  switch (which_of (which, LAT_WORDS, LON_WORDS))
    {
    case 0: r = tm_value_number (lat2); break;
    case 1: r = tm_value_number (lon2); break;
    default: r = tm_value_error (TM_ERR_VALUE); break;
    }
  g_free (which);
  return r;
}

/* Stations: latitudes, longitudes and (if values is not NULL) their
 * values, as three parallel lists; stations with any part missing are
 * left out. */
typedef struct {
  int n;
  double *lat, *lon, *val;
} Stations;

static void
stations_free (Stations *s)
{
  g_free (s->lat);
  g_free (s->lon);
  g_free (s->val);
}

static gboolean
read_stations (TmEvalContext *ctx, TmArg *lats, TmArg *lons, TmArg *vals,
               Stations *s, TmValue *err)
{
  int na, no, nv = 0;
  const TmValue **a = tm_arg_cells (ctx, lats, &na);
  const TmValue **o = tm_arg_cells (ctx, lons, &no);
  const TmValue **v = vals != NULL ? tm_arg_cells (ctx, vals, &nv) : NULL;
  gboolean ok = TRUE;

  memset (s, 0, sizeof *s);
  if (na != no || (v != NULL && nv != na))
    {
      *err = tm_value_error (TM_ERR_NA);
      ok = FALSE;
    }
  else
    {
      s->lat = g_new (double, MAX (na, 1));
      s->lon = g_new (double, MAX (na, 1));
      s->val = g_new (double, MAX (na, 1));
      for (int i = 0; i < na; i++)
        {
          if (a[i]->type != TM_VALUE_NUMBER || o[i]->type != TM_VALUE_NUMBER
              || (v != NULL && v[i]->type != TM_VALUE_NUMBER))
            continue;
          s->lat[s->n] = a[i]->as.number;
          s->lon[s->n] = o[i]->as.number;
          s->val[s->n] = v != NULL ? v[i]->as.number : 0;
          s->n++;
        }
      if (s->n == 0)
        {
          *err = tm_value_error (TM_ERR_DIV0);
          ok = FALSE;
        }
    }
  g_free (a);
  g_free (o);
  g_free (v);
  if (!ok)
    stations_free (s);
  return ok;
}

/* Inverse distance weighting (Shepard, 1968): the stations' values,
 * weighted by one over their distance to the power p. */
static TmValue
fn_geo_idw (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  Stations s;
  double lat, lon, power, sw = 0, sv = 0;

  ARG_NUM (0, lat);
  ARG_NUM (1, lon);
  OPT_NUM (5, power, 2);
  if (!read_stations (ctx, &args[2], &args[3], &args[4], &s, &err))
    return err;
  for (int i = 0; i < s.n; i++)
    {
      double d = tm_geo_distance (lat, lon, s.lat[i], s.lon[i]), w;

      if (d < 1e-9)
        {
          double at = s.val[i];
          stations_free (&s);
          return tm_value_number (at);
        }
      w = 1 / pow (d, power);
      sw += w;
      sv += w * s.val[i];
    }
  stations_free (&s);
  return tm_value_number (sv / sw);
}

static TmValue
fn_geo_nearest (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  Stations s;
  double lat, lon, best = INFINITY, at = 0;

  ARG_NUM (0, lat);
  ARG_NUM (1, lon);
  if (!read_stations (ctx, &args[2], &args[3], HAS_ARG (4) ? &args[4] : NULL, &s, &err))
    return err;
  for (int i = 0; i < s.n; i++)
    {
      double d = tm_geo_distance (lat, lon, s.lat[i], s.lon[i]);
      if (d < best)
        {
          best = d;
          at = s.val[i];
        }
    }
  stations_free (&s);
  /* Without values, the distance to the nearest. */
  return tm_value_number (HAS_ARG (4) ? at : best);
}

static TmValue
fn_geo_within (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  Stations s;
  double lat, lon, radius, count = 0;

  ARG_NUM (0, lat);
  ARG_NUM (1, lon);
  ARG_NUM (4, radius);
  if (!read_stations (ctx, &args[2], &args[3], NULL, &s, &err))
    return err;
  for (int i = 0; i < s.n; i++)
    count += tm_geo_distance (lat, lon, s.lat[i], s.lon[i]) <= radius;
  stations_free (&s);
  return tm_value_number (count);
}

/* Ordinary kriging (Matheron): the best linear unbiased estimate from the
 * stations, under an exponential semivariogram
 *
 *     gamma(h) = nugget + (sill - nugget) (1 - exp(-3 h / range)),
 *
 * the sill the stations' variance and the range, unless given, a third of
 * the widest distance between them.  Solves
 *
 *     [ Gamma  1 ] [ w  ]   [ gamma0 ]
 *     [ 1'     0 ] [ mu ] = [   1    ]
 *
 * and gives the estimate sum w z, or its kriging standard deviation
 * sqrt(sum w gamma0 + mu). */
static gboolean
krige (TmEvalContext *ctx, TmArg *args, int n, double *estimate, double *sd, TmValue *err)
{
  Stations s;
  double lat, lon, range, nugget, mean = 0, sill = 0, far = 0;
  double *a, *b, *g0;
  int k, m;

  if (!tm_arg_number (ctx, &args[0], &lat, err) || !tm_arg_number (ctx, &args[1], &lon, err))
    return FALSE;
  if (!read_stations (ctx, &args[2], &args[3], &args[4], &s, err))
    return FALSE;
  if (s.n > 400)
    {
      stations_free (&s);
      *err = tm_value_error (TM_ERR_NUM);
      return FALSE;
    }
  k = s.n;
  for (int i = 0; i < k; i++)
    mean += s.val[i];
  mean /= k;
  for (int i = 0; i < k; i++)
    sill += (s.val[i] - mean) * (s.val[i] - mean);
  sill = k > 1 ? sill / (k - 1) : 0;
  for (int i = 0; i < k; i++)
    for (int j = i + 1; j < k; j++)
      far = MAX (far, tm_geo_distance (s.lat[i], s.lon[i], s.lat[j], s.lon[j]));
  range = far / 3;
  if (HAS_ARG (5) && !tm_arg_number (ctx, &args[5], &range, err))
    {
      stations_free (&s);
      return FALSE;
    }
  nugget = 0;
  if (HAS_ARG (6) && !tm_arg_number (ctx, &args[6], &nugget, err))
    {
      stations_free (&s);
      return FALSE;
    }
  if (k == 1 || sill == 0 || range <= 0)
    {
      /* Nothing to weigh: the mean, known as well as the stations are. */
      *estimate = mean;
      *sd = sqrt (sill);
      stations_free (&s);
      return TRUE;
    }

#define GAMMA(h) ((h) <= 0 ? 0 : nugget + (sill - nugget) * (1 - exp (-3 * (h) / range)))
  m = k + 1;
  a = g_new0 (double, (gsize) m * (m + 1));
  b = g_new0 (double, m);
  g0 = g_new0 (double, k);
  for (int i = 0; i < k; i++)
    {
      for (int j = 0; j < k; j++)
        a[i * (m + 1) + j] = GAMMA (tm_geo_distance (s.lat[i], s.lon[i], s.lat[j], s.lon[j]));
      a[i * (m + 1) + k] = 1;
      a[k * (m + 1) + i] = 1;
      g0[i] = GAMMA (tm_geo_distance (lat, lon, s.lat[i], s.lon[i]));
      a[i * (m + 1) + m] = g0[i];
    }
  a[k * (m + 1) + m] = 1;
#undef GAMMA

  /* Gaussian elimination with partial pivoting on the augmented matrix. */
  for (int c = 0; c < m; c++)
    {
      int p = c;
      for (int r = c + 1; r < m; r++)
        if (fabs (a[r * (m + 1) + c]) > fabs (a[p * (m + 1) + c]))
          p = r;
      if (fabs (a[p * (m + 1) + c]) < 1e-12)
        {
          g_free (a);
          g_free (b);
          g_free (g0);
          stations_free (&s);
          *err = tm_value_error (TM_ERR_NUM);   /* two stations in one place */
          return FALSE;
        }
      if (p != c)
        for (int j = 0; j <= m; j++)
          {
            double t = a[c * (m + 1) + j];
            a[c * (m + 1) + j] = a[p * (m + 1) + j];
            a[p * (m + 1) + j] = t;
          }
      for (int r = 0; r < m; r++)
        if (r != c)
          {
            double f = a[r * (m + 1) + c] / a[c * (m + 1) + c];
            for (int j = c; j <= m; j++)
              a[r * (m + 1) + j] -= f * a[c * (m + 1) + j];
          }
    }
  for (int i = 0; i < m; i++)
    b[i] = a[i * (m + 1) + m] / a[i * (m + 1) + i];

  *estimate = 0;
  *sd = b[k];                                   /* mu */
  for (int i = 0; i < k; i++)
    {
      *estimate += b[i] * s.val[i];
      *sd += b[i] * g0[i];
    }
  *sd = sqrt (MAX (0.0, *sd));
  g_free (a);
  g_free (b);
  g_free (g0);
  stations_free (&s);
  return TRUE;
}

static TmValue
fn_geo_krige (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double estimate, sd;

  if (!krige (ctx, args, n, &estimate, &sd, &err))
    return err;
  return tm_value_number (estimate);
}

static TmValue
fn_geo_krige_sd (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double estimate, sd;

  if (!krige (ctx, args, n, &estimate, &sd, &err))
    return err;
  return tm_value_number (sd);
}

static TmValue
fn_rand_krige (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  double estimate, sd;

  if (!krige (ctx, args, n, &estimate, &sd, &err))
    return err;
  return tm_value_number (estimate + sd * draw_normal (ctx));
}

/* ---- Maps ------------------------------------------------------------- */

static char *
optional_text (TmEvalContext *ctx, TmArg *args, int n, int i, TmValue *err, gboolean *ok)
{
  *ok = TRUE;
  if (!HAS_ARG (i))
    return NULL;
  {
    char *t = text_arg (ctx, &args[i], err);
    if (t == NULL)
      *ok = FALSE;
    return t;
  }
}

/* The feature named by argument i, matched on property (or on its name). */
static const TmFeature *
feature_arg (TmEvalContext *ctx, const TmMap *map, TmArg *arg, const char *property, TmValue *err)
{
  TmValue v = tm_arg_scalar (ctx, arg);
  int i;

  if (v.type == TM_VALUE_ERROR)
    {
      *err = v;
      return NULL;
    }
  i = tm_map_find (map, property, &v);
  tm_value_clear (&v);
  if (i < 0)
    {
      *err = tm_value_error (TM_ERR_NA);
      return NULL;
    }
  return g_ptr_array_index (map->features, i);
}

static TmValue
fn_map_region (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  const TmMap *map = map_arg (ctx, &args[0], &err);
  const TmValue *v;
  double lat, lon;
  char *property;
  gboolean ok;
  int i;

  if (map == NULL)
    return err;
  ARG_NUM (1, lat);
  ARG_NUM (2, lon);
  property = optional_text (ctx, args, n, 3, &err, &ok);
  if (!ok)
    return err;
  i = tm_map_feature_at (map, lat, lon);
  v = i >= 0 ? tm_feature_property (g_ptr_array_index (map->features, i), property) : NULL;
  g_free (property);
  if (i < 0)
    return tm_value_error (TM_ERR_NA);
  return v != NULL ? tm_value_copy (v) : tm_value_number (i + 1);
}

static TmValue
fn_map_nearest (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  const TmMap *map = map_arg (ctx, &args[0], &err);
  const TmValue *v;
  double lat, lon, best = INFINITY;
  char *property;
  gboolean ok;
  int found = -1;

  if (map == NULL)
    return err;
  ARG_NUM (1, lat);
  ARG_NUM (2, lon);
  property = optional_text (ctx, args, n, 3, &err, &ok);
  if (!ok)
    return err;
  for (guint i = 0; i < map->features->len; i++)
    {
      double d = tm_feature_distance (g_ptr_array_index (map->features, i), lat, lon);
      if (d < best)
        {
          best = d;
          found = (int) i;
        }
    }
  v = found >= 0 ? tm_feature_property (g_ptr_array_index (map->features, found), property) : NULL;
  g_free (property);
  if (found < 0)
    return tm_value_error (TM_ERR_NA);
  return v != NULL ? tm_value_copy (v) : tm_value_number (found + 1);
}

typedef enum { ASK_CONTAINS, ASK_DISTANCE, ASK_AREA, ASK_CENTROID } Ask;

static TmValue
map_question (TmEvalContext *ctx, TmArg *args, int n, Ask ask)
{
  TmValue err;
  const TmMap *map = map_arg (ctx, &args[0], &err);
  const TmFeature *f;
  char *property;
  gboolean ok;
  int prop_arg = ask == ASK_AREA ? 2 : ask == ASK_CENTROID ? 3 : 4;
  double a = 0, b = 0;
  TmValue r;

  if (map == NULL)
    return err;
  property = optional_text (ctx, args, n, prop_arg, &err, &ok);
  if (!ok)
    return err;
  f = feature_arg (ctx, map, &args[1], property, &err);
  g_free (property);
  if (f == NULL)
    return err;
  if (ask == ASK_AREA)
    return tm_value_number (tm_feature_area (f));
  if (ask == ASK_CENTROID)
    {
      char *which = text_arg (ctx, &args[2], &err);
      double lat, lon;

      if (which == NULL)
        return err;
      tm_feature_centroid (f, &lat, &lon);
      switch (which_of (which, LAT_WORDS, LON_WORDS))
        {
        case 0: r = tm_value_number (lat); break;
        case 1: r = tm_value_number (lon); break;
        default: r = tm_value_error (TM_ERR_VALUE); break;
        }
      g_free (which);
      return r;
    }
  ARG_NUM (2, a);
  ARG_NUM (3, b);
  if (ask == ASK_CONTAINS)
    return tm_value_bool (tm_feature_contains (f, a, b));
  return tm_value_number (tm_feature_distance (f, a, b));
}

static TmValue fn_map_contains (TmEvalContext *c, TmArg *a, int n) { return map_question (c, a, n, ASK_CONTAINS); }
static TmValue fn_map_distance (TmEvalContext *c, TmArg *a, int n) { return map_question (c, a, n, ASK_DISTANCE); }
static TmValue fn_map_area     (TmEvalContext *c, TmArg *a, int n) { return map_question (c, a, n, ASK_AREA); }
static TmValue fn_map_centroid (TmEvalContext *c, TmArg *a, int n) { return map_question (c, a, n, ASK_CENTROID); }

static TmValue
fn_map_property (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  const TmMap *map = map_arg (ctx, &args[0], &err);
  const TmFeature *f;
  const TmValue *v;
  char *wanted, *match;
  gboolean ok;

  if (map == NULL)
    return err;
  match = optional_text (ctx, args, n, 3, &err, &ok);
  if (!ok)
    return err;
  f = feature_arg (ctx, map, &args[1], match, &err);
  g_free (match);
  if (f == NULL)
    return err;
  if ((wanted = text_arg (ctx, &args[2], &err)) == NULL)
    return err;
  v = tm_feature_property (f, wanted);
  g_free (wanted);
  return v != NULL ? tm_value_copy (v) : tm_value_error (TM_ERR_NA);
}

static TmValue
fn_map_count (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  const TmMap *map = map_arg (ctx, &args[0], &err);

  return map != NULL ? tm_value_number (map->features->len) : err;
}

/* The i'th feature's name (or property), to list a layer's regions down a
 * column. */
static TmValue
fn_map_feature (TmEvalContext *ctx, TmArg *args, int n)
{
  TmValue err;
  const TmMap *map = map_arg (ctx, &args[0], &err);
  const TmValue *v;
  double i;
  char *property;
  gboolean ok;

  if (map == NULL)
    return err;
  ARG_NUM (1, i);
  if (i < 1 || i > map->features->len)
    return tm_value_error (TM_ERR_REF);
  property = optional_text (ctx, args, n, 2, &err, &ok);
  if (!ok)
    return err;
  v = tm_feature_property (g_ptr_array_index (map->features, (int) i - 1), property);
  g_free (property);
  return v != NULL ? tm_value_copy (v) : tm_value_error (TM_ERR_NA);
}

/* ---- Spread across a grid --------------------------------------------- */

/* A stochastic cellular automaton for anything that spreads from where it
 * is to where it can go -- fire through fuel above all (Alexandridis et
 * al., 2008), but as well a flood over low ground or an outbreak through a
 * population.  Each step, every burning cell tries to set each of its
 * eight neighbours alight with probability
 *
 *     p * fuel(neighbour) * exp(c1 V) exp(c2 V (cos t - 1)),
 *
 * t the angle between the direction of spread and the way the wind blows,
 * V its speed in m/s, c1 = 0.045 and c2 = 0.131, a diagonal neighbour at
 * 0.7 of that; a cell burns for one step and is then burnt out.  The whole
 * field is one draw for all the cells that read it in a future; it is
 * worked out once and kept. */
typedef struct {
  guint64 key;
  guint gen;
  int rows, cols;
  guint8 *burnt;
} Field;

static void
spread (TmEvalContext *ctx, Field *f, const TmArg *fuel_arg, int r0, int c0, double p,
        int steps, double wind_from, double wind)
{
  int rows = f->rows, cols = f->cols, n = rows * cols;
  double *fuel = g_new (double, n);
  int *front = g_new (int, n), *next = g_new (int, n);
  int nf = 0;
  TmRng rng;
  double to = (wind_from + 180) * G_PI / 180;

  for (int r = 0; r < rows; r++)
    for (int c = 0; c < cols; c++)
      {
        const TmValue *v = tm_ctx_cell (ctx, fuel_arg->range.row0 + r, fuel_arg->range.col0 + c);
        fuel[r * cols + c] = v->type == TM_VALUE_NUMBER ? CLAMP (v->as.number, 0, 1) : 0;
      }
  memset (f->burnt, 0, n);
  ctx->shared_stream (ctx->data, f->key, &rng);
  f->burnt[r0 * cols + c0] = 1;
  front[nf++] = r0 * cols + c0;

  for (int step = 0; step < steps && nf > 0; step++)
    {
      int nn = 0;

      for (int i = 0; i < nf; i++)
        {
          int r = front[i] / cols, c = front[i] % cols;

          for (int dr = -1; dr <= 1; dr++)
            for (int dc = -1; dc <= 1; dc++)
              {
                int rr = r + dr, cc = c + dc, k;
                double q, t;

                if ((dr == 0 && dc == 0) || rr < 0 || cc < 0 || rr >= rows || cc >= cols)
                  continue;
                k = rr * cols + cc;
                if (f->burnt[k] || fuel[k] <= 0)
                  continue;
                /* Rows run south, columns east: the bearing of the step. */
                t = atan2 (dc, -dr) - to;
                q = p * fuel[k] * exp (0.045 * wind) * exp (0.131 * wind * (cos (t) - 1));
                if (dr != 0 && dc != 0)
                  q *= 0.7;
                if (tm_rng_uniform (&rng) < q)
                  {
                    f->burnt[k] = 1;
                    next[nn++] = k;
                  }
              }
        }
      memcpy (front, next, sizeof (int) * nn);
      nf = nn;
    }
  g_free (fuel);
  g_free (front);
  g_free (next);
}

static TmValue
fn_rand_spread (TmEvalContext *ctx, TmArg *args, int n)
{
  static Field cache[4];
  static int next;
  TmValue err;
  double r0, c0, p, steps, row, col, wind_from, wind;
  Field *f = NULL;
  guint gen;
  guint64 key;
  int rows, cols;

  if (!args[0].is_range || ctx->shared_stream == NULL)
    return tm_value_error (TM_ERR_VALUE);
  ARG_NUM (1, r0);
  ARG_NUM (2, c0);
  ARG_NUM (3, p);
  ARG_NUM (4, steps);
  ARG_NUM (5, row);
  ARG_NUM (6, col);
  OPT_NUM (7, wind_from, 0);
  OPT_NUM (8, wind, 0);
  rows = tm_range_rows (&args[0].range);
  cols = tm_range_cols (&args[0].range);
  if (p < 0 || p > 1 || steps < 0 || wind < 0 || (gint64) rows * cols > 1000000)
    return tm_value_error (TM_ERR_NUM);
  if (r0 < 1 || c0 < 1 || r0 > rows || c0 > cols || row < 1 || col < 1 || row > rows || col > cols)
    return tm_value_error (TM_ERR_REF);

  /* Everything that shapes the field, hashed: the cells that read it
   * differ only in which of its cells they ask about. */
  {
    double parts[] = { args[0].range.row0, args[0].range.col0, rows, cols,
                       r0, c0, p, steps, wind_from, wind };
    key = 0xcbf29ce484222325ULL;
    for (guint i = 0; i < G_N_ELEMENTS (parts); i++)
      {
        const guchar *b = (const guchar *) &parts[i];
        for (gsize j = 0; j < sizeof (double); j++)
          key = (key ^ b[j]) * 0x100000001b3ULL;
      }
  }
  gen = ctx->generation != NULL ? ctx->generation (ctx->data) : 0;
  for (int i = 0; i < 4; i++)
    if (cache[i].burnt != NULL && cache[i].key == key && cache[i].gen == gen)
      f = &cache[i];
  if (f == NULL)
    {
      f = &cache[next];
      next = (next + 1) % 4;
      g_free (f->burnt);
      f->key = key;
      f->gen = gen;
      f->rows = rows;
      f->cols = cols;
      f->burnt = g_new (guint8, (gsize) rows * cols);
      spread (ctx, f, &args[0], (int) r0 - 1, (int) c0 - 1, p, (int) steps, wind_from, wind);
    }
  return tm_value_number (f->burnt[((int) row - 1) * cols + (int) col - 1]);
}

/* ---- Where a formula is ----------------------------------------------- */

static TmValue
fn_row (TmEvalContext *ctx, TmArg *args, int n)
{
  if (n > 0 && args[0].is_range)
    return tm_value_number (args[0].range.row0 + 1);
  return tm_value_number (ctx->row + 1);
}

static TmValue
fn_column (TmEvalContext *ctx, TmArg *args, int n)
{
  if (n > 0 && args[0].is_range)
    return tm_value_number (args[0].range.col0 + 1);
  return tm_value_number (ctx->col + 1);
}

#define RND TM_FN_RANDOM

const TmFunction tm_fn_spatial[] = {
  FN ("IMAGE.WIDTH", 1, 1, fn_image_width, 0, I, "IMAGE.WIDTH(picture)", "The picture's width in pixels."),
  FN ("IMAGE.HEIGHT", 1, 1, fn_image_height, 0, I, "IMAGE.HEIGHT(picture)", "The picture's height in pixels."),
  FN ("IMAGE.AT", 3, 4, fn_image_at, 0, I, "IMAGE.AT(picture, u, v, [channel])", "A channel at a point, u across and v down from 0 to 1: \"gray\", \"red\", \"hue\", \"exg\"..."),
  FN ("IMAGE.PIXEL", 3, 4, fn_image_pixel, 0, I, "IMAGE.PIXEL(picture, x, y, [channel])", "A channel of one pixel, counted from 1 at the top left."),
  FN ("IMAGE.GEO", 3, 4, fn_image_geo, 0, I, "IMAGE.GEO(picture, lat, lon, [channel])", "A channel at a place, on a picture with bounds."),
  FN ("IMAGE.XY", 4, 4, fn_image_xy, 0, I, "IMAGE.XY(picture, lat, lon, \"u\"|\"v\")", "Where a place falls on a picture with bounds, as u across or v down."),
  FN ("IMAGE.MEAN", 1, 6, fn_image_mean, 0, I, "IMAGE.MEAN(picture, [channel], [u0], [v0], [u1], [v1])", "A channel's mean over the picture or a region of it."),
  FN ("IMAGE.FRACTION", 3, 7, fn_image_fraction, 0, I, "IMAGE.FRACTION(picture, channel, criteria, [u0], [v0], [u1], [v1])", "The share of pixels meeting criteria: IMAGE.FRACTION(\"field\",\"exg\",\">0.1\") is green cover."),
  FN ("IMAGE.OTSU", 1, 2, fn_image_otsu, 0, I, "IMAGE.OTSU(picture, [channel])", "The level that best splits a channel in two: cloud from ground, water from land."),
  FN ("IMAGE.MOTION", 3, 4, fn_image_motion, 0, I, "IMAGE.MOTION(before, after, \"dx\"|\"dy\", [max_shift])", "How far the picture moved between frames, as a fraction of its size."),
  FN ("IMAGE.LEGEND", 5, 6, fn_image_legend, 0, I, "IMAGE.LEGEND(picture, u, v, colours, values, [tolerance])", "The value a colour-coded map shows at a point, read off its legend."),

  FN ("GEO.DISTANCE", 4, 4, fn_geo_distance, 0, G, "GEO.DISTANCE(lat1, lon1, lat2, lon2)", "The great-circle distance in km."),
  FN ("GEO.BEARING", 4, 4, fn_geo_bearing, 0, G, "GEO.BEARING(lat1, lon1, lat2, lon2)", "The initial bearing from the first place to the second, in degrees."),
  FN ("GEO.DESTINATION", 5, 5, fn_geo_destination, 0, G, "GEO.DESTINATION(lat, lon, km, bearing, \"lat\"|\"lon\")", "Where so many km on a bearing lead."),
  FN ("GEO.IDW", 5, 6, fn_geo_idw, 0, G, "GEO.IDW(lat, lon, lats, lons, values, [power])", "Stations' values at a place, weighted by inverse distance."),
  FN ("GEO.KRIGE", 5, 7, fn_geo_krige, 0, G, "GEO.KRIGE(lat, lon, lats, lons, values, [range_km], [nugget])", "Stations' values at a place by ordinary kriging."),
  FN ("GEO.KRIGE.SD", 5, 7, fn_geo_krige_sd, 0, G, "GEO.KRIGE.SD(lat, lon, lats, lons, values, [range_km], [nugget])", "How uncertain the kriged value is: its standard deviation."),
  FN ("RAND.KRIGE", 5, 7, fn_rand_krige, RND, G, "RAND.KRIGE(lat, lon, lats, lons, values, [range_km], [nugget])", "A draw from the kriged value's uncertainty."),
  FN ("GEO.NEAREST", 4, 5, fn_geo_nearest, 0, G, "GEO.NEAREST(lat, lon, lats, lons, [values])", "The nearest station's value, or without values its distance in km."),
  FN ("GEO.WITHIN", 5, 5, fn_geo_within, 0, G, "GEO.WITHIN(lat, lon, lats, lons, km)", "How many of the places are within so many km."),

  FN ("MAP.REGION", 3, 4, fn_map_region, 0, MP, "MAP.REGION(map, lat, lon, [property])", "The name (or a property) of the region a place is in; #N/A if none."),
  FN ("MAP.NEAREST", 3, 4, fn_map_nearest, 0, MP, "MAP.NEAREST(map, lat, lon, [property])", "The name of the region nearest a place."),
  FN ("MAP.CONTAINS", 4, 5, fn_map_contains, 0, MP, "MAP.CONTAINS(map, region, lat, lon, [property])", "Whether a place is in a region."),
  FN ("MAP.DISTANCE", 4, 5, fn_map_distance, 0, MP, "MAP.DISTANCE(map, region, lat, lon, [property])", "How far a place is from a region's edge in km; 0 inside."),
  FN ("MAP.AREA", 2, 3, fn_map_area, 0, MP, "MAP.AREA(map, region, [property])", "A region's area in km²."),
  FN ("MAP.CENTROID", 3, 5, fn_map_centroid, 0, MP, "MAP.CENTROID(map, region, \"lat\"|\"lon\", [property])", "A region's middle."),
  FN ("MAP.PROPERTY", 3, 4, fn_map_property, 0, MP, "MAP.PROPERTY(map, region, property, [match_property])", "One of a region's properties, such as its population."),
  FN ("MAP.COUNT", 1, 1, fn_map_count, 0, MP, "MAP.COUNT(map)", "How many regions the map has."),
  FN ("MAP.FEATURE", 2, 3, fn_map_feature, 0, MP, "MAP.FEATURE(map, i, [property])", "The i'th region's name, to list them down a column."),
  FN ("RAND.SPREAD", 7, 9, fn_rand_spread, RND, MP, "RAND.SPREAD(fuel, start_row, start_col, p, steps, row, col, [wind_from], [wind_speed])", "1 if a fire spreading through a grid of fuel reaches the cell, else 0."),

  FN ("ROW", 0, 1, fn_row, 0, L, "ROW([reference])", "The row number of the reference, or of the formula's own cell."),
  FN ("COLUMN", 0, 1, fn_column, 0, L, "COLUMN([reference])", "The column number of the reference, or of the formula's own cell."),
};
const int tm_fn_spatial_count = G_N_ELEMENTS (tm_fn_spatial);
