/* tm-sources.c - reading pictures and maps
 *
 * Copyright (C) 2026 The timemachine authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "tm-sources.h"

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <string.h>

TmImage *
tm_image_load (const char *path, GError **error)
{
  GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file (path, error);
  TmImage *image;
  int w, h, stride, channels;
  const guchar *pixels;

  if (pixbuf == NULL)
    return NULL;
  /* A picture's own orientation, as a camera records it. */
  {
    GdkPixbuf *turned = gdk_pixbuf_apply_embedded_orientation (pixbuf);
    if (turned != NULL)
      {
        g_object_unref (pixbuf);
        pixbuf = turned;
      }
  }
  w = gdk_pixbuf_get_width (pixbuf);
  h = gdk_pixbuf_get_height (pixbuf);
  stride = gdk_pixbuf_get_rowstride (pixbuf);
  channels = gdk_pixbuf_get_n_channels (pixbuf);
  pixels = gdk_pixbuf_read_pixels (pixbuf);
  image = tm_image_new (w, h);
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++)
      {
        const guchar *p = pixels + (gsize) y * stride + (gsize) x * channels;
        float *q = image->rgba + ((gsize) y * w + x) * 4;

        q[0] = p[0] / 255.0f;
        q[1] = p[channels > 2 ? 1 : 0] / 255.0f;
        q[2] = p[channels > 2 ? 2 : 0] / 255.0f;
        q[3] = channels == 4 ? p[3] / 255.0f : 1.0f;
      }
  g_object_unref (pixbuf);
  return image;
}

TmMap *
tm_map_load (const char *path, GError **error)
{
  char *text;
  gsize length;
  TmMap *map;

  if (!g_file_get_contents (path, &text, &length, error))
    return NULL;
  map = tm_map_from_geojson (text, length, error);
  g_free (text);
  if (map != NULL && map->features->len == 0)
    {
      g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "%s has no features in it", path);
      tm_map_free (map);
      return NULL;
    }
  return map;
}

static gboolean
is_map (const char *path)
{
  char *lower = g_ascii_strdown (path, -1);
  gboolean map = g_str_has_suffix (lower, ".geojson") || g_str_has_suffix (lower, ".json");

  g_free (lower);
  return map;
}

char *
tm_sheet_load_source (TmSheet *sheet, const char *path, const char *name, GError **error)
{
  char *used;

  if (name == NULL || *name == '\0')
    {
      char *base = g_path_get_basename (path);
      char *dot = strrchr (base, '.');

      if (dot != NULL && dot != base)
        *dot = '\0';
      used = base;
    }
  else
    used = g_strdup (name);

  if (is_map (path))
    {
      TmMap *map = tm_map_load (path, error);
      if (map == NULL)
        {
          g_free (used);
          return NULL;
        }
      tm_sheet_add_map (sheet, used, map, path);
    }
  else
    {
      TmImage *image = tm_image_load (path, error);
      if (image == NULL)
        {
          g_free (used);
          return NULL;
        }
      tm_sheet_add_image (sheet, used, image, path);
    }
  return used;
}
