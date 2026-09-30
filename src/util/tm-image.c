/* tm-image.c - pictures as data
 *
 * Copyright (C) 2026 The timemachine authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "tm-image.h"

#include <math.h>
#include <string.h>

TmImage *
tm_image_new (int width, int height)
{
  TmImage *image = g_new0 (TmImage, 1);

  image->width = MAX (width, 1);
  image->height = MAX (height, 1);
  image->rgba = g_new0 (float, (gsize) image->width * image->height * 4);
  return image;
}

void
tm_image_free (TmImage *image)
{
  if (image == NULL)
    return;
  g_free (image->rgba);
  g_free (image);
}

gboolean
tm_channel_parse (const char *name, TmChannel *out)
{
  static const struct { const char *name; TmChannel channel; } names[] = {
    { "gray", TM_CHANNEL_GRAY }, { "grey", TM_CHANNEL_GRAY },
    { "luma", TM_CHANNEL_GRAY }, { "brightness", TM_CHANNEL_GRAY },
    { "red", TM_CHANNEL_RED }, { "r", TM_CHANNEL_RED },
    { "green", TM_CHANNEL_GREEN }, { "g", TM_CHANNEL_GREEN },
    { "blue", TM_CHANNEL_BLUE }, { "b", TM_CHANNEL_BLUE },
    { "alpha", TM_CHANNEL_ALPHA }, { "a", TM_CHANNEL_ALPHA },
    { "hue", TM_CHANNEL_HUE }, { "h", TM_CHANNEL_HUE },
    { "saturation", TM_CHANNEL_SATURATION }, { "s", TM_CHANNEL_SATURATION },
    { "value", TM_CHANNEL_VALUE }, { "v", TM_CHANNEL_VALUE },
    { "exg", TM_CHANNEL_EXG }, { "vari", TM_CHANNEL_VARI },
  };

  if (name == NULL || *name == '\0')
    {
      *out = TM_CHANNEL_GRAY;
      return TRUE;
    }
  for (guint i = 0; i < G_N_ELEMENTS (names); i++)
    if (g_ascii_strcasecmp (name, names[i].name) == 0)
      {
        *out = names[i].channel;
        return TRUE;
      }
  return FALSE;
}

/* A channel from red, green, blue and alpha. */
static double
channel_of (const float *p, TmChannel channel)
{
  double r = p[0], g = p[1], b = p[2];

  switch (channel)
    {
    case TM_CHANNEL_RED:   return r;
    case TM_CHANNEL_GREEN: return g;
    case TM_CHANNEL_BLUE:  return b;
    case TM_CHANNEL_ALPHA: return p[3];
    case TM_CHANNEL_GRAY:  return 0.2126 * r + 0.7152 * g + 0.0722 * b;
    case TM_CHANNEL_VALUE: return MAX (r, MAX (g, b));
    case TM_CHANNEL_SATURATION:
      {
        double mx = MAX (r, MAX (g, b)), mn = MIN (r, MIN (g, b));
        return mx > 0 ? (mx - mn) / mx : 0;
      }
    case TM_CHANNEL_HUE:
      {
        double mx = MAX (r, MAX (g, b)), mn = MIN (r, MIN (g, b)), d = mx - mn, h;

        if (d == 0)
          return 0;
        if (mx == r)
          h = fmod ((g - b) / d, 6.0);
        else if (mx == g)
          h = (b - r) / d + 2;
        else
          h = (r - g) / d + 4;
        h /= 6;
        return h < 0 ? h + 1 : h;
      }
    case TM_CHANNEL_EXG:
      {
        double s = r + g + b;
        return s > 0 ? (2 * g - r - b) / s : 0;
      }
    case TM_CHANNEL_VARI:
      {
        double d = g + r - b;
        return fabs (d) > 1e-6 ? (g - r) / d : 0;
      }
    default:
      return NAN;
    }
}

static const float *
pixel (const TmImage *image, int x, int y)
{
  x = CLAMP (x, 0, image->width - 1);
  y = CLAMP (y, 0, image->height - 1);
  return image->rgba + ((gsize) y * image->width + x) * 4;
}

double
tm_image_pixel (const TmImage *image, int x, int y, TmChannel channel)
{
  if (x < 0 || y < 0 || x >= image->width || y >= image->height)
    return NAN;
  return channel_of (pixel (image, x, y), channel);
}

/* Red, green, blue and alpha at a fractional point, bilinearly between
 * the centres of the four pixels round it. */
static gboolean
sample (const TmImage *image, double u, double v, float out[4])
{
  double x, y, fx, fy;
  int x0, y0;

  if (!(u >= 0 && u <= 1 && v >= 0 && v <= 1))
    return FALSE;
  x = u * image->width - 0.5;
  y = v * image->height - 0.5;
  x0 = (int) floor (x);
  y0 = (int) floor (y);
  fx = x - x0;
  fy = y - y0;
  for (int k = 0; k < 4; k++)
    {
      double a = pixel (image, x0, y0)[k], b = pixel (image, x0 + 1, y0)[k];
      double c = pixel (image, x0, y0 + 1)[k], d = pixel (image, x0 + 1, y0 + 1)[k];

      out[k] = (float) ((a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy);
    }
  return TRUE;
}

double
tm_image_at (const TmImage *image, double u, double v, TmChannel channel)
{
  float p[4];

  if (!sample (image, u, v, p))
    return NAN;
  return channel_of (p, channel);
}

double
tm_image_at_geo (const TmImage *image, double lat, double lon, TmChannel channel)
{
  if (!image->has_bounds || image->east == image->west || image->north == image->south)
    return NAN;
  return tm_image_at (image, (lon - image->west) / (image->east - image->west),
                      (image->north - lat) / (image->north - image->south), channel);
}

void
tm_image_rgb_at (const TmImage *image, double u, double v, double rgb[3])
{
  float p[4];

  if (!sample (image, u, v, p))
    {
      rgb[0] = rgb[1] = rgb[2] = NAN;
      return;
    }
  rgb[0] = p[0];
  rgb[1] = p[1];
  rgb[2] = p[2];
}

/* The pixels a region covers: at least one, never off the picture. */
static void
region (const TmImage *image, double u0, double v0, double u1, double v1,
        int *x0, int *y0, int *x1, int *y1)
{
  double t;

  if (u1 < u0) { t = u0; u0 = u1; u1 = t; }
  if (v1 < v0) { t = v0; v0 = v1; v1 = t; }
  *x0 = CLAMP ((int) floor (u0 * image->width), 0, image->width - 1);
  *y0 = CLAMP ((int) floor (v0 * image->height), 0, image->height - 1);
  *x1 = CLAMP ((int) ceil (u1 * image->width) - 1, *x0, image->width - 1);
  *y1 = CLAMP ((int) ceil (v1 * image->height) - 1, *y0, image->height - 1);
}

double
tm_image_mean (const TmImage *image, TmChannel channel,
               double u0, double v0, double u1, double v1)
{
  int x0, y0, x1, y1;
  double sum = 0;

  region (image, u0, v0, u1, v1, &x0, &y0, &x1, &y1);
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++)
      sum += channel_of (pixel (image, x, y), channel);
  return sum / ((double) (x1 - x0 + 1) * (y1 - y0 + 1));
}

double
tm_image_fraction (const TmImage *image, TmChannel channel,
                   double u0, double v0, double u1, double v1,
                   TmPixelTest test, gpointer data)
{
  int x0, y0, x1, y1;
  double hits = 0;

  region (image, u0, v0, u1, v1, &x0, &y0, &x1, &y1);
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++)
      hits += test (channel_of (pixel (image, x, y), channel), data);
  return hits / ((double) (x1 - x0 + 1) * (y1 - y0 + 1));
}

double
tm_image_otsu (const TmImage *image, TmChannel channel)
{
  enum { BINS = 256 };
  double hist[BINS] = { 0 }, lo = INFINITY, hi = -INFINITY;
  gsize n = (gsize) image->width * image->height;
  double total = 0, sum_all = 0, w0 = 0, sum0 = 0, best = -1, threshold;
  int best_bin = 0;

  for (gsize i = 0; i < n; i++)
    {
      double v = channel_of (image->rgba + i * 4, channel);
      lo = MIN (lo, v);
      hi = MAX (hi, v);
    }
  if (!(hi > lo))
    return lo;
  for (gsize i = 0; i < n; i++)
    {
      double v = channel_of (image->rgba + i * 4, channel);
      int b = (int) floor ((v - lo) / (hi - lo) * (BINS - 1) + 0.5);
      hist[CLAMP (b, 0, BINS - 1)]++;
    }
  for (int b = 0; b < BINS; b++)
    {
      total += hist[b];
      sum_all += b * hist[b];
    }
  /* The split that maximises the variance between the two classes. */
  for (int b = 0; b < BINS - 1; b++)
    {
      double w1, m0, m1, between;

      w0 += hist[b];
      sum0 += b * hist[b];
      w1 = total - w0;
      if (w0 == 0 || w1 == 0)
        continue;
      m0 = sum0 / w0;
      m1 = (sum_all - sum0) / w1;
      between = w0 * w1 * (m0 - m1) * (m0 - m1);
      if (between > best)
        {
          best = between;
          best_bin = b;
        }
    }
  threshold = lo + (best_bin + 0.5) / (BINS - 1) * (hi - lo);
  return threshold;
}

/* A grayscale copy at most 96 pixels across, for matching. */
static float *
shrink (const TmImage *image, int *w, int *h)
{
  int step = MAX (1, (MAX (image->width, image->height) + 95) / 96);
  float *out;

  *w = MAX (1, image->width / step);
  *h = MAX (1, image->height / step);
  out = g_new (float, (gsize) *w * *h);
  for (int y = 0; y < *h; y++)
    for (int x = 0; x < *w; x++)
      {
        double s = 0;
        for (int j = 0; j < step; j++)
          for (int i = 0; i < step; i++)
            s += channel_of (pixel (image, x * step + i, y * step + j), TM_CHANNEL_GRAY);
        out[y * *w + x] = (float) (s / (step * step));
      }
  return out;
}

gboolean
tm_image_motion (const TmImage *before, const TmImage *after,
                 double max_shift, double *dx, double *dy)
{
  int w, h, w2, h2, mx, my, bx = 0, by = 0;
  float *a, *b;
  double best = INFINITY, *cost;

  if (before->width != after->width || before->height != after->height)
    return FALSE;
  a = shrink (before, &w, &h);
  b = shrink (after, &w2, &h2);
  mx = MAX (1, (int) ceil (max_shift * w));
  my = MAX (1, (int) ceil (max_shift * h));
  cost = g_new (double, (gsize) (2 * mx + 1) * (2 * my + 1));

  /* Every shift (sx, sy) of the after frame against the before: the mean
   * squared difference over where they overlap. */
  for (int sy = -my; sy <= my; sy++)
    for (int sx = -mx; sx <= mx; sx++)
      {
        double s = 0;
        long n = 0;

        for (int y = MAX (0, -sy); y < MIN (h, h - sy); y++)
          for (int x = MAX (0, -sx); x < MIN (w, w - sx); x++)
            {
              double d = a[y * w + x] - b[(y + sy) * w + x + sx];
              s += d * d;
              n++;
            }
        s = n * 4 >= (long) w * h ? s / n : INFINITY;   /* a quarter at least */
        cost[(sy + my) * (2 * mx + 1) + sx + mx] = s;
        if (s < best - 1e-15 || (s <= best + 1e-15 && abs (sx) + abs (sy) < abs (bx) + abs (by)))
          {
            best = MIN (best, s);
            bx = sx;
            by = sy;
          }
      }

  /* To a fraction of a pixel: the vertex of the parabola through the best
   * shift and its neighbours, each way. */
  {
    double fx = bx, fy = by;
#define COST(sx, sy) cost[((sy) + my) * (2 * mx + 1) + (sx) + mx]
    if (bx > -mx && bx < mx)
      {
        double l = COST (bx - 1, by), c = COST (bx, by), r = COST (bx + 1, by);
        double d = l - 2 * c + r;
        if (isfinite (d) && d > 0)
          fx += 0.5 * (l - r) / d;
      }
    if (by > -my && by < my)
      {
        double l = COST (bx, by - 1), c = COST (bx, by), r = COST (bx, by + 1);
        double d = l - 2 * c + r;
        if (isfinite (d) && d > 0)
          fy += 0.5 * (l - r) / d;
      }
#undef COST
    *dx = fx / w;
    *dy = fy / h;
  }
  g_free (cost);
  g_free (a);
  g_free (b);
  return TRUE;
}
