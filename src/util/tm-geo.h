/* tm-geo.h - places on Earth, and maps of them
 *
 * Copyright (C) 2026 The timemachine authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Great-circle arithmetic on a spherical Earth of mean radius 6371.0088 km
 * -- distance, bearing, and where a heading and a distance lead -- which
 * is within half a percent of the ellipsoid anywhere, far inside the
 * uncertainty of anything being forecast.
 *
 * And maps: GeoJSON (RFC 7946) layers of countries, regions, coasts or
 * catchments, each feature a set of polygons with properties, so that a
 * formula can ask which region a place is in, how large a region is, or
 * how far a place is from it.  Coordinates are kept as GeoJSON has them:
 * longitude first.
 */

#pragma once

#include "tm-value.h"

G_BEGIN_DECLS

#define TM_EARTH_RADIUS_KM 6371.0088

double   tm_geo_distance    (double lat1, double lon1, double lat2, double lon2);
/* The initial bearing from the first place to the second, in degrees
 * clockwise from north, 0 to 360. */
double   tm_geo_bearing     (double lat1, double lon1, double lat2, double lon2);
/* Where distance km on bearing degrees from (lat, lon) leads. */
void     tm_geo_destination (double lat, double lon, double distance, double bearing,
                             double *lat2, double *lon2);

typedef struct {
  int     n;
  double *xy;                /* n (longitude, latitude) pairs */
} TmRing;

typedef struct {
  GPtrArray  *polygons;      /* of GPtrArray of TmRing: an outer ring, then holes */
  GPtrArray  *lines;         /* of TmRing: LineStrings, for drawing */
  GArray     *points;        /* of double, (longitude, latitude) pairs */
  GHashTable *properties;    /* char * -> TmValue * */
  double      west, south, east, north;
} TmFeature;

typedef struct {
  GPtrArray *features;       /* of TmFeature */
  double     west, south, east, north;
} TmMap;

TmMap   *tm_map_from_geojson (const char *text, gsize length, GError **error);
void     tm_map_free (TmMap *map);

/* The feature whose polygons hold the place, or -1. */
int      tm_map_feature_at (const TmMap *map, double lat, double lon);
/* The feature whose property (or, with property NULL, whose name: "name",
 * "NAME", "ADMIN" and the like) matches value, or -1. */
int      tm_map_find (const TmMap *map, const char *property, const TmValue *value);
/* A property's value, or with property NULL the feature's name; NULL if
 * it has none. */
const TmValue *tm_feature_property (const TmFeature *feature, const char *property);

gboolean tm_feature_contains (const TmFeature *feature, double lat, double lon);
/* The area in km², on the sphere. */
double   tm_feature_area (const TmFeature *feature);
/* A middle point: the area-weighted centroid of the polygons (or the mean
 * of the points). */
void     tm_feature_centroid (const TmFeature *feature, double *lat, double *lon);
/* The distance in km from a place to the feature's nearest edge, 0 if the
 * place is inside. */
double   tm_feature_distance (const TmFeature *feature, double lat, double lon);

G_END_DECLS
