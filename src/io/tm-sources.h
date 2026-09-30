/* tm-sources.h - reading pictures and maps
 *
 * Copyright (C) 2026 The timemachine authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * PNG, JPEG and whatever else gdk-pixbuf reads, into a TmImage; GeoJSON
 * into a TmMap.  gdk-pixbuf is not GTK, and the engine may use it.
 */

#pragma once

#include "tm-sheet.h"

G_BEGIN_DECLS

TmImage *tm_image_load (const char *path, GError **error);
TmMap   *tm_map_load   (const char *path, GError **error);

/* Loads a file as a named source of the sheet: a map if it ends .geojson
 * or .json, a picture otherwise.  With name NULL the name is the file's,
 * less its extension.  Returns the name used, or NULL on error. */
char    *tm_sheet_load_source (TmSheet *sheet, const char *path, const char *name,
                               GError **error);

G_END_DECLS
