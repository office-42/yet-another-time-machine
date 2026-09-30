/* tm-application.h - the application
 *
 * Copyright (C) 2026 The timemachine authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define TM_TYPE_APPLICATION (tm_application_get_type ())
G_DECLARE_FINAL_TYPE (TmApplication, tm_application, TM, APPLICATION, GtkApplication)

TmApplication *tm_application_new (void);

G_END_DECLS
