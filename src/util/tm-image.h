/* tm-image.h - pictures as data
 *
 * Copyright (C) 2026 The timemachine authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A picture, to the engine, is a grid of measurements: a radar frame's
 * rain, a satellite photograph's greenness, a map's colour-coded heights.
 * TmImage holds the pixels as floating-point red, green, blue and alpha
 * from 0 to 1, and answers questions about them -- a channel at a point,
 * interpolated between pixels; the mean over a region; how far the whole
 * picture has moved since another one.  Decoding files is io/'s business;
 * nothing here knows a file format.
 *
 * An image may also know where on Earth it is: the longitudes of its left
 * and right edges and the latitudes of its bottom and top, for pictures
 * that are maps (equirectangular, or near enough over a small area).
 */

#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef struct {
  int     width, height;
  float  *rgba;              /* width * height * 4, row by row from the top */
  gboolean has_bounds;
  double  west, south, east, north;
} TmImage;

/* What to read from a pixel. */
typedef enum {
  TM_CHANNEL_GRAY,           /* luma, Rec. 709 */
  TM_CHANNEL_RED,
  TM_CHANNEL_GREEN,
  TM_CHANNEL_BLUE,
  TM_CHANNEL_ALPHA,
  TM_CHANNEL_HUE,            /* 0 to 1 around the colour wheel */
  TM_CHANNEL_SATURATION,
  TM_CHANNEL_VALUE,
  TM_CHANNEL_EXG,            /* excess green, 2g - r - b of chromatic
                              * coordinates: vegetation, from -1 to 2 */
  TM_CHANNEL_VARI            /* visible atmospherically resistant index */
} TmChannel;

TmImage *tm_image_new  (int width, int height);
void     tm_image_free (TmImage *image);

/* "gray", "red", ..., "exg", "vari"; FALSE for a name it does not know. */
gboolean tm_channel_parse (const char *name, TmChannel *out);

/* The channel of pixel (x, y), counted from 0 at the top left. */
double   tm_image_pixel (const TmImage *image, int x, int y, TmChannel channel);
/* ... at a point given as fractions of the width and height, (0, 0) the
 * top left corner and (1, 1) the bottom right, interpolated bilinearly
 * between the four pixels round it.  NaN outside the picture. */
double   tm_image_at    (const TmImage *image, double u, double v, TmChannel channel);
/* ... at a place on Earth, through the image's bounds; NaN if it has
 * none, or the place is off it. */
double   tm_image_at_geo (const TmImage *image, double lat, double lon, TmChannel channel);

/* The red, green and blue of the point, interpolated, for comparing with
 * the colours of a legend. */
void     tm_image_rgb_at (const TmImage *image, double u, double v, double rgb[3]);

/* The mean of a channel over the region from (u0, v0) to (u1, v1), and
 * the fraction of its pixels for which test says TRUE. */
double   tm_image_mean     (const TmImage *image, TmChannel channel,
                            double u0, double v0, double u1, double v1);
typedef gboolean (*TmPixelTest) (double value, gpointer data);
double   tm_image_fraction (const TmImage *image, TmChannel channel,
                            double u0, double v0, double u1, double v1,
                            TmPixelTest test, gpointer data);

/* Otsu's threshold: the level that best splits the channel's values into
 * two classes, for telling cloud from ground or water from land. */
double   tm_image_otsu (const TmImage *image, TmChannel channel);

/* How far the picture's content moved from before to after, in fractions
 * of the width (dx) and height (dy): the shift, of at most max_shift of
 * each, that best lines the two up -- block matching on the whole frame,
 * the first step of an extrapolation nowcast.  FALSE if the two differ in
 * size. */
gboolean tm_image_motion (const TmImage *before, const TmImage *after,
                          double max_shift, double *dx, double *dy);

G_END_DECLS
