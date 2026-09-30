/* tm-window.h - the main window
 *
 * Copyright (C) 2026 The timemachine authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A spreadsheet's window -- menus, a formula bar, the grid -- with a panel
 * down the right that shows the futures of whatever is selected.
 */

#pragma once

#include <gtk/gtk.h>

#include "tm-sheet.h"

G_BEGIN_DECLS

#define TM_TYPE_WINDOW (tm_window_get_type ())
G_DECLARE_FINAL_TYPE (TmWindow, tm_window, TM, WINDOW, GtkApplicationWindow)

TmWindow *tm_window_new (GtkApplication *app);

gboolean  tm_window_load     (TmWindow *window, const char *path, GError **error);
void      tm_window_select   (TmWindow *window, const TmRange *range);
void      tm_window_simulate (TmWindow *window);

/* Where the example models are: installed, or beside the source when run
 * from a build directory. */
char     *tm_samples_dir (void);

G_END_DECLS
