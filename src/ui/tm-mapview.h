/* tm-mapview.h - maps and heatmaps for the forecast panel
 *
 * Copyright (C) 2026 The timemachine authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <gtk/gtk.h>

#include "tm-sheet.h"

G_BEGIN_DECLS

/* Draws the selection as places on a map, if it is places: two columns
 * headed lat and lon (stations, their values in a third column, their
 * names in a text one), or two rows named so (a track across time).
 * Uncertain places are drawn as the cloud of their simulated positions,
 * or as one faint track per future.  The maps and pictures with bounds
 * loaded as data are drawn underneath.  FALSE if the selection is not
 * places. */
gboolean tm_draw_map (cairo_t *cr, PangoLayout *layout, int width, int height,
                      TmSheet *sheet, const TmRange *range);

/* Draws a block of cells as a heatmap: their values, or, for uncertain
 * cells, their simulated means -- the chance each is true, for a block of
 * TRUE/FALSE or 1/0 cells such as where a fire reaches. */
void     tm_draw_heatmap (cairo_t *cr, PangoLayout *layout, int width, int height,
                          TmSheet *sheet, const TmRange *range);

G_END_DECLS
