/* tm-numfmt.h - number formats
 *
 * Copyright (C) 2026 The timemachine authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The part of Excel's format codes a model of the future needs: a number
 * of decimals ("0.00"), thousands separated ("#,##0"), percent ("0.0%"),
 * scientific ("0.00E+00"), and text either side ("$#,##0", "0.0 \"days\"").
 * Only the first section of a code with several is used.
 */

#pragma once

#include <glib.h>

G_BEGIN_DECLS

/* The number as the format shows it; NULL or "General" is the General
 * format. */
char    *tm_format_number (double number, const char *format);

/* The same format with delta more (or fewer) decimals: what the Increase
 * and Decrease Decimal buttons do. */
char    *tm_format_change_decimals (const char *format, int delta);

gboolean tm_format_is_percent (const char *format);

G_END_DECLS
