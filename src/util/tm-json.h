/* tm-json.h - just enough JSON to read GeoJSON
 *
 * Copyright (C) 2026 The timemachine authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * RFC 8259, parsed into a tree.  Small, and without a dependency, because
 * maps are the only JSON the program reads.
 */

#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
  TM_JSON_NULL,
  TM_JSON_BOOL,
  TM_JSON_NUMBER,
  TM_JSON_STRING,
  TM_JSON_ARRAY,
  TM_JSON_OBJECT
} TmJsonType;

typedef struct _TmJson TmJson;

struct _TmJson {
  TmJsonType type;
  gboolean   boolean;
  double     number;
  char      *string;
  GPtrArray *items;          /* of TmJson: an array's elements, an object's values */
  GPtrArray *keys;           /* of char *: an object's keys, beside items */
};

TmJson       *tm_json_parse (const char *text, gsize length, GError **error);
void          tm_json_free  (TmJson *json);
/* An object's member, or NULL. */
const TmJson *tm_json_member (const TmJson *object, const char *key);

G_END_DECLS
