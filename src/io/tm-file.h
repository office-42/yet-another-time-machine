/* tm-file.h - reading and writing sheets
 *
 * Copyright (C) 2026 The timemachine authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A .tm file is text, a line per cell, so that a model can be read,
 * diffed and kept under version control like any other source:
 *
 *     timemachine 1
 *     iterations	10000
 *     seed	1
 *     width	A	160
 *     cell	A1	Revenue
 *     cell	B1	=RAND.PERT(80,100,150)
 *
 * Fields are separated by tabs; a tab, a newline or a backslash in a cell
 * is written \t, \n or \\.  CSV is read and written too, for history
 * coming in from elsewhere and results going out.
 */

#pragma once

#include "tm-sheet.h"

G_BEGIN_DECLS

/* By the name's extension: .csv is CSV, anything else .tm.  Loading
 * replaces what the sheet held and recalculates it. */
gboolean tm_file_load (TmSheet *sheet, const char *path, GError **error);
gboolean tm_file_save (TmSheet *sheet, const char *path, GError **error);

/* Every sample the last simulation kept, a column per cell and a row per
 * iteration, for analysis somewhere else. */
gboolean tm_file_export_samples (TmSheet *sheet, const char *path, GError **error);

G_END_DECLS
