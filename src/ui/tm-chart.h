/* tm-chart.h - pictures of the futures a simulation found
 *
 * Copyright (C) 2026 The timemachine authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * One uncertain cell is drawn as a histogram of its futures, with the 5th,
 * 50th and 95th percentiles marked.  A row or column of cells -- a
 * quantity month by month -- is drawn as a fan chart: the band nine
 * futures in ten fall inside, the band half of them do, and the median,
 * with any cells that are plain facts (the history before the forecast)
 * drawn as a line leading into it.
 */

#pragma once

#include <gtk/gtk.h>

#include "tm-sheet.h"

G_BEGIN_DECLS

#define TM_TYPE_CHART (tm_chart_get_type ())
G_DECLARE_FINAL_TYPE (TmChart, tm_chart, TM, CHART, GtkWidget)

GtkWidget *tm_chart_new (void);

/* What to draw: the cells of range, from sheet's last simulation. */
void tm_chart_show (TmChart *chart, TmSheet *sheet, const TmRange *range);

G_END_DECLS
