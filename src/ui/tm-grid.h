/* tm-grid.h - the grid of cells
 *
 * Copyright (C) 2026 The timemachine authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Draws a sheet the way a spreadsheet looks -- lettered columns, numbered
 * rows, a cursor and a selection -- and turns keys and clicks into moves
 * of that cursor.  Cells whose value is a draw from a distribution are
 * tinted, so that what is known and what is guessed can be told apart at
 * a glance.  Editing happens in the window's formula bar; the grid asks
 * for it with the "edit" signal.
 */

#pragma once

#include <gtk/gtk.h>

#include "tm-sheet.h"

G_BEGIN_DECLS

#define TM_TYPE_GRID (tm_grid_get_type ())
G_DECLARE_FINAL_TYPE (TmGrid, tm_grid, TM, GRID, GtkWidget)

GtkWidget *tm_grid_new (TmSheet *sheet);

void     tm_grid_set_sheet     (TmGrid *grid, TmSheet *sheet);
/* The active cell, and the selection it is part of. */
TmRef    tm_grid_get_cursor    (TmGrid *grid);
void     tm_grid_get_selection (TmGrid *grid, TmRange *out);
void     tm_grid_select        (TmGrid *grid, const TmRange *range);
void     tm_grid_move_cursor   (TmGrid *grid, int drow, int dcol, gboolean extend);

/* Scrollbars attach to these; the grid keeps their ranges up to date. */
GtkAdjustment *tm_grid_get_hadjustment (TmGrid *grid);
GtkAdjustment *tm_grid_get_vadjustment (TmGrid *grid);

/* The sheet has changed: draw it again. */
void     tm_grid_refresh (TmGrid *grid);

G_END_DECLS
