/* tm-geo.c - places on Earth, and maps of them
 *
 * Copyright (C) 2026 The timemachine authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "tm-geo.h"
#include "tm-json.h"

#include <math.h>
#include <string.h>

#define RAD (G_PI / 180.0)

double
tm_geo_distance (double lat1, double lon1, double lat2, double lon2)
{
  /* The haversine form, which keeps its precision for short distances. */
  double p1 = lat1 * RAD, p2 = lat2 * RAD;
  double dp = (lat2 - lat1) * RAD, dl = (lon2 - lon1) * RAD;
  double a = sin (dp / 2) * sin (dp / 2) + cos (p1) * cos (p2) * sin (dl / 2) * sin (dl / 2);

  return 2 * TM_EARTH_RADIUS_KM * atan2 (sqrt (a), sqrt (MAX (0.0, 1 - a)));
}

double
tm_geo_bearing (double lat1, double lon1, double lat2, double lon2)
{
  double p1 = lat1 * RAD, p2 = lat2 * RAD, dl = (lon2 - lon1) * RAD;
  double y = sin (dl) * cos (p2);
  double x = cos (p1) * sin (p2) - sin (p1) * cos (p2) * cos (dl);

  return fmod (atan2 (y, x) / RAD + 360, 360);
}

void
tm_geo_destination (double lat, double lon, double distance, double bearing,
                    double *lat2, double *lon2)
{
  double d = distance / TM_EARTH_RADIUS_KM, t = bearing * RAD;
  double p1 = lat * RAD, l1 = lon * RAD;
  double p2 = asin (sin (p1) * cos (d) + cos (p1) * sin (d) * cos (t));
  double l2 = l1 + atan2 (sin (t) * sin (d) * cos (p1), cos (d) - sin (p1) * sin (p2));

  *lat2 = p2 / RAD;
  *lon2 = fmod (l2 / RAD + 540, 360) - 180;
}

/* ---- GeoJSON ---------------------------------------------------------- */

static void
ring_free (gpointer p)
{
  TmRing *r = p;

  g_free (r->xy);
  g_free (r);
}

static void
polygon_free (gpointer p)
{
  g_ptr_array_free (p, TRUE);
}

static void
value_free (gpointer p)
{
  tm_value_clear (p);
  g_free (p);
}

static void
feature_free (gpointer p)
{
  TmFeature *f = p;

  g_ptr_array_free (f->polygons, TRUE);
  g_ptr_array_free (f->lines, TRUE);
  g_array_free (f->points, TRUE);
  g_hash_table_destroy (f->properties);
  g_free (f);
}

void
tm_map_free (TmMap *map)
{
  if (map == NULL)
    return;
  g_ptr_array_free (map->features, TRUE);
  g_free (map);
}

static void
extend (double *w, double *s, double *e, double *n, double lon, double lat)
{
  *w = MIN (*w, lon);
  *e = MAX (*e, lon);
  *s = MIN (*s, lat);
  *n = MAX (*n, lat);
}

/* [[lon, lat], ...] as a ring. */
static TmRing *
read_ring (const TmJson *coords, TmFeature *f)
{
  TmRing *r;

  if (coords == NULL || coords->type != TM_JSON_ARRAY)
    return NULL;
  r = g_new0 (TmRing, 1);
  r->xy = g_new (double, 2 * MAX (coords->items->len, 1));
  for (guint i = 0; i < coords->items->len; i++)
    {
      const TmJson *pt = g_ptr_array_index (coords->items, i);
      const TmJson *x, *y;

      if (pt->type != TM_JSON_ARRAY || pt->items->len < 2)
        continue;
      x = g_ptr_array_index (pt->items, 0);
      y = g_ptr_array_index (pt->items, 1);
      if (x->type != TM_JSON_NUMBER || y->type != TM_JSON_NUMBER)
        continue;
      r->xy[2 * r->n] = x->number;
      r->xy[2 * r->n + 1] = y->number;
      extend (&f->west, &f->south, &f->east, &f->north, x->number, y->number);
      r->n++;
    }
  return r;
}

static void
read_polygon (const TmJson *rings, TmFeature *f)
{
  GPtrArray *poly;

  if (rings == NULL || rings->type != TM_JSON_ARRAY)
    return;
  poly = g_ptr_array_new_with_free_func (ring_free);
  for (guint i = 0; i < rings->items->len; i++)
    {
      TmRing *r = read_ring (g_ptr_array_index (rings->items, i), f);
      if (r != NULL)
        g_ptr_array_add (poly, r);
    }
  if (poly->len > 0)
    g_ptr_array_add (f->polygons, poly);
  else
    g_ptr_array_free (poly, TRUE);
}

static void
read_geometry (const TmJson *geometry, TmFeature *f)
{
  const TmJson *type = tm_json_member (geometry, "type");
  const TmJson *coords = tm_json_member (geometry, "coordinates");
  const char *t;

  if (type == NULL || type->type != TM_JSON_STRING)
    return;
  t = type->string;
  if (strcmp (t, "GeometryCollection") == 0)
    {
      const TmJson *list = tm_json_member (geometry, "geometries");
      if (list != NULL && list->type == TM_JSON_ARRAY)
        for (guint i = 0; i < list->items->len; i++)
          read_geometry (g_ptr_array_index (list->items, i), f);
      return;
    }
  if (coords == NULL || coords->type != TM_JSON_ARRAY)
    return;
  if (strcmp (t, "Polygon") == 0)
    read_polygon (coords, f);
  else if (strcmp (t, "MultiPolygon") == 0)
    for (guint i = 0; i < coords->items->len; i++)
      read_polygon (g_ptr_array_index (coords->items, i), f);
  else if (strcmp (t, "LineString") == 0)
    {
      TmRing *r = read_ring (coords, f);
      if (r != NULL)
        g_ptr_array_add (f->lines, r);
    }
  else if (strcmp (t, "MultiLineString") == 0)
    for (guint i = 0; i < coords->items->len; i++)
      {
        TmRing *r = read_ring (g_ptr_array_index (coords->items, i), f);
        if (r != NULL)
          g_ptr_array_add (f->lines, r);
      }
  else if (strcmp (t, "Point") == 0 || strcmp (t, "MultiPoint") == 0)
    {
      /* A Point is one [lon, lat]; a MultiPoint a list of them. */
      gboolean one = strcmp (t, "Point") == 0;
      guint n = one ? 1 : coords->items->len;

      for (guint i = 0; i < n; i++)
        {
          const TmJson *pt = one ? coords : g_ptr_array_index (coords->items, i);
          const TmJson *x, *y;

          if (pt->type != TM_JSON_ARRAY || pt->items->len < 2)
            continue;
          x = g_ptr_array_index (pt->items, 0);
          y = g_ptr_array_index (pt->items, 1);
          if (x->type != TM_JSON_NUMBER || y->type != TM_JSON_NUMBER)
            continue;
          g_array_append_val (f->points, x->number);
          g_array_append_val (f->points, y->number);
          extend (&f->west, &f->south, &f->east, &f->north, x->number, y->number);
        }
    }
}

static TmFeature *
read_feature (const TmJson *json)
{
  TmFeature *f = g_new0 (TmFeature, 1);
  const TmJson *props = tm_json_member (json, "properties");
  const TmJson *geometry = tm_json_member (json, "geometry");

  f->polygons = g_ptr_array_new_with_free_func (polygon_free);
  f->lines = g_ptr_array_new_with_free_func (ring_free);
  f->points = g_array_new (FALSE, FALSE, sizeof (double));
  f->properties = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, value_free);
  f->west = f->south = INFINITY;
  f->east = f->north = -INFINITY;

  if (props != NULL && props->type == TM_JSON_OBJECT)
    for (guint i = 0; i < props->keys->len; i++)
      {
        const TmJson *v = g_ptr_array_index (props->items, i);
        TmValue *value = g_new (TmValue, 1);

        switch (v->type)
          {
          case TM_JSON_NUMBER: *value = tm_value_number (v->number); break;
          case TM_JSON_STRING: *value = tm_value_text (v->string); break;
          case TM_JSON_BOOL:   *value = tm_value_bool (v->boolean); break;
          default:             *value = tm_value_empty (); break;
          }
        g_hash_table_replace (f->properties, g_strdup (g_ptr_array_index (props->keys, i)), value);
      }
  if (geometry != NULL && geometry->type == TM_JSON_OBJECT)
    read_geometry (geometry, f);
  return f;
}

TmMap *
tm_map_from_geojson (const char *text, gsize length, GError **error)
{
  TmJson *json = tm_json_parse (text, length, error);
  const TmJson *type, *list;
  TmMap *map;

  if (json == NULL)
    return NULL;
  type = tm_json_member (json, "type");
  if (type == NULL || type->type != TM_JSON_STRING)
    {
      g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "not GeoJSON: no \"type\"");
      tm_json_free (json);
      return NULL;
    }

  map = g_new0 (TmMap, 1);
  map->features = g_ptr_array_new_with_free_func (feature_free);
  if (strcmp (type->string, "FeatureCollection") == 0)
    {
      list = tm_json_member (json, "features");
      if (list != NULL && list->type == TM_JSON_ARRAY)
        for (guint i = 0; i < list->items->len; i++)
          g_ptr_array_add (map->features, read_feature (g_ptr_array_index (list->items, i)));
    }
  else if (strcmp (type->string, "Feature") == 0)
    g_ptr_array_add (map->features, read_feature (json));
  else
    {
      /* A bare geometry: one feature without properties. */
      TmFeature *f = read_feature (json);
      read_geometry (json, f);
      g_ptr_array_add (map->features, f);
    }
  tm_json_free (json);

  map->west = map->south = INFINITY;
  map->east = map->north = -INFINITY;
  for (guint i = 0; i < map->features->len; i++)
    {
      TmFeature *f = g_ptr_array_index (map->features, i);
      if (f->west <= f->east)
        {
          extend (&map->west, &map->south, &map->east, &map->north, f->west, f->south);
          extend (&map->west, &map->south, &map->east, &map->north, f->east, f->north);
        }
    }
  return map;
}

/* ---- Questions of features -------------------------------------------- */

/* Ray casting: a ray east from the point crosses the ring's edges an odd
 * number of times if the point is inside. */
static gboolean
ring_contains (const TmRing *r, double x, double y)
{
  gboolean inside = FALSE;

  for (int i = 0, j = r->n - 1; i < r->n; j = i++)
    {
      double xi = r->xy[2 * i], yi = r->xy[2 * i + 1];
      double xj = r->xy[2 * j], yj = r->xy[2 * j + 1];

      if (((yi > y) != (yj > y)) && x < (xj - xi) * (y - yi) / (yj - yi) + xi)
        inside = !inside;
    }
  return inside;
}

gboolean
tm_feature_contains (const TmFeature *f, double lat, double lon)
{
  if (lon < f->west || lon > f->east || lat < f->south || lat > f->north)
    return FALSE;
  for (guint p = 0; p < f->polygons->len; p++)
    {
      GPtrArray *poly = g_ptr_array_index (f->polygons, p);
      gboolean in = ring_contains (g_ptr_array_index (poly, 0), lon, lat);

      /* Inside the outer ring and in no hole. */
      for (guint h = 1; in && h < poly->len; h++)
        if (ring_contains (g_ptr_array_index (poly, h), lon, lat))
          in = FALSE;
      if (in)
        return TRUE;
    }
  return FALSE;
}

int
tm_map_feature_at (const TmMap *map, double lat, double lon)
{
  for (guint i = 0; i < map->features->len; i++)
    if (tm_feature_contains (g_ptr_array_index (map->features, i), lat, lon))
      return (int) i;
  return -1;
}

static const char *const NAME_KEYS[] = {
  "name", "NAME", "Name", "ADMIN", "NAME_EN", "name_en", "NAME_LONG", "title", "id"
};

const TmValue *
tm_feature_property (const TmFeature *f, const char *property)
{
  if (property != NULL && *property != '\0')
    {
      const TmValue *v = g_hash_table_lookup (f->properties, property);
      if (v != NULL)
        return v;
      /* Property names differ in case from one source to the next. */
      {
        GHashTableIter iter;
        gpointer key, value;

        g_hash_table_iter_init (&iter, f->properties);
        while (g_hash_table_iter_next (&iter, &key, &value))
          if (g_ascii_strcasecmp (key, property) == 0)
            return value;
      }
      return NULL;
    }
  for (guint i = 0; i < G_N_ELEMENTS (NAME_KEYS); i++)
    {
      const TmValue *v = g_hash_table_lookup (f->properties, NAME_KEYS[i]);
      if (v != NULL && v->type != TM_VALUE_EMPTY)
        return v;
    }
  return NULL;
}

int
tm_map_find (const TmMap *map, const char *property, const TmValue *value)
{
  for (guint i = 0; i < map->features->len; i++)
    {
      const TmValue *v = tm_feature_property (g_ptr_array_index (map->features, i), property);
      if (v != NULL && tm_value_compare (v, value) == 0)
        return (int) i;
    }
  return -1;
}

/* A ring's area on the sphere, by the formula d3 and turf use (after
 * Chamberlain and Duquette): R^2/2 times the sum of (l2 - l1)(2 + sin p1
 * + sin p2), exact for edges along parallels and close otherwise. */
static double
ring_area (const TmRing *r)
{
  double sum = 0;

  for (int i = 0; i < r->n; i++)
    {
      int j = (i + 1) % r->n;
      double l1 = r->xy[2 * i] * RAD, p1 = r->xy[2 * i + 1] * RAD;
      double l2 = r->xy[2 * j] * RAD, p2 = r->xy[2 * j + 1] * RAD;

      sum += (l2 - l1) * (2 + sin (p1) + sin (p2));
    }
  return fabs (sum * TM_EARTH_RADIUS_KM * TM_EARTH_RADIUS_KM / 2);
}

double
tm_feature_area (const TmFeature *f)
{
  double area = 0;

  for (guint p = 0; p < f->polygons->len; p++)
    {
      GPtrArray *poly = g_ptr_array_index (f->polygons, p);

      area += ring_area (g_ptr_array_index (poly, 0));
      for (guint h = 1; h < poly->len; h++)
        area -= ring_area (g_ptr_array_index (poly, h));
    }
  return area;
}

void
tm_feature_centroid (const TmFeature *f, double *lat, double *lon)
{
  double ax = 0, ay = 0, a = 0;

  /* Planar, in degrees, weighted by each outer ring's area: good enough
   * for a label or a distance, which is what it is for. */
  for (guint p = 0; p < f->polygons->len; p++)
    {
      GPtrArray *poly = g_ptr_array_index (f->polygons, p);
      TmRing *r = g_ptr_array_index (poly, 0);

      for (int i = 0; i < r->n; i++)
        {
          int j = (i + 1) % r->n;
          double x0 = r->xy[2 * i], y0 = r->xy[2 * i + 1];
          double x1 = r->xy[2 * j], y1 = r->xy[2 * j + 1];
          double cross = x0 * y1 - x1 * y0;

          a += cross;
          ax += (x0 + x1) * cross;
          ay += (y0 + y1) * cross;
        }
    }
  if (fabs (a) > 1e-12)
    {
      *lon = ax / (3 * a);
      *lat = ay / (3 * a);
      return;
    }
  if (f->points->len >= 2)
    {
      double sx = 0, sy = 0;
      guint n = f->points->len / 2;

      for (guint i = 0; i < n; i++)
        {
          sx += g_array_index (f->points, double, 2 * i);
          sy += g_array_index (f->points, double, 2 * i + 1);
        }
      *lon = sx / n;
      *lat = sy / n;
      return;
    }
  *lon = (f->west + f->east) / 2;
  *lat = (f->south + f->north) / 2;
}

/* The distance from a place to a segment, in a flat projection centred
 * on the place: kilometres east and north, which is accurate to well
 * under a percent for the few hundred kilometres such a question is
 * usually about. */
static double
segment_km (double lat, double lon, double x0, double y0, double x1, double y1)
{
  double k = TM_EARTH_RADIUS_KM * RAD, c = cos (lat * RAD);
  double ax = (x0 - lon) * k * c, ay = (y0 - lat) * k;
  double bx = (x1 - lon) * k * c, by = (y1 - lat) * k;
  double dx = bx - ax, dy = by - ay, t;

  /* Longitudes the other side of the antimeridian. */
  if (fabs (x0 - lon) > 180 || fabs (x1 - lon) > 180)
    return tm_geo_distance (lat, lon, y0, x0);
  t = (dx == 0 && dy == 0) ? 0 : CLAMP (-(ax * dx + ay * dy) / (dx * dx + dy * dy), 0, 1);
  return hypot (ax + t * dx, ay + t * dy);
}

double
tm_feature_distance (const TmFeature *f, double lat, double lon)
{
  double best = INFINITY;

  if (tm_feature_contains (f, lat, lon))
    return 0;
  for (guint p = 0; p < f->polygons->len; p++)
    {
      GPtrArray *poly = g_ptr_array_index (f->polygons, p);

      for (guint h = 0; h < poly->len; h++)
        {
          TmRing *r = g_ptr_array_index (poly, h);
          for (int i = 0; i + 1 < r->n; i++)
            best = MIN (best, segment_km (lat, lon, r->xy[2 * i], r->xy[2 * i + 1],
                                          r->xy[2 * i + 2], r->xy[2 * i + 3]));
        }
    }
  for (guint l = 0; l < f->lines->len; l++)
    {
      TmRing *r = g_ptr_array_index (f->lines, l);
      for (int i = 0; i + 1 < r->n; i++)
        best = MIN (best, segment_km (lat, lon, r->xy[2 * i], r->xy[2 * i + 1],
                                      r->xy[2 * i + 2], r->xy[2 * i + 3]));
    }
  for (guint i = 0; i + 1 < f->points->len; i += 2)
    best = MIN (best, tm_geo_distance (lat, lon, g_array_index (f->points, double, i + 1),
                                       g_array_index (f->points, double, i)));
  return best;
}
