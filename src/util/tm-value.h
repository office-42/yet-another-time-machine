/* tm-value.h - what a cell can hold
 *
 * Copyright (C) 2026 The timemachine authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A value is one of five things, and the rules for turning one into
 * another are the whole of what makes =1+"2" behave the way people who
 * know Excel expect.  Those rules live here so that the evaluator can be
 * about evaluation.
 */

#pragma once

#include "tm-types.h"

G_BEGIN_DECLS

typedef enum {
  TM_VALUE_EMPTY = 0,
  TM_VALUE_NUMBER,
  TM_VALUE_TEXT,
  TM_VALUE_BOOL,
  TM_VALUE_ERROR
} TmValueType;

/* Errors are values, not failures: they travel through arithmetic and can
 * be tested for, which is why IFERROR can exist at all. */
typedef enum {
  TM_ERR_NONE = 0,
  TM_ERR_NULL,      /* #NULL!   */
  TM_ERR_DIV0,      /* #DIV/0!  */
  TM_ERR_VALUE,     /* #VALUE!  */
  TM_ERR_REF,       /* #REF!    */
  TM_ERR_NAME,      /* #NAME?   */
  TM_ERR_NUM,       /* #NUM!    */
  TM_ERR_NA,        /* #N/A     */
  TM_ERR_CIRCULAR   /* #CIRC!: a cell that depends on itself */
} TmErrorCode;

typedef struct {
  TmValueType type;
  union {
    double       number;
    char        *text;     /* owned */
    gboolean     boolean;
    TmErrorCode  error;
  } as;
} TmValue;

/* Values are passed by value and own their text, so every one that has
 * been assigned to must be cleared exactly once. */
void     tm_value_clear  (TmValue *value);
TmValue  tm_value_copy   (const TmValue *value);

TmValue  tm_value_empty  (void);
TmValue  tm_value_number (double number);   /* NaN and infinities: #NUM! */
TmValue  tm_value_text   (const char *text);
TmValue  tm_value_take   (char *text);
TmValue  tm_value_bool   (gboolean boolean);
TmValue  tm_value_error  (TmErrorCode code);

static inline gboolean tm_value_is_error  (const TmValue *v) { return v->type == TM_VALUE_ERROR; }
static inline gboolean tm_value_is_number (const TmValue *v) { return v->type == TM_VALUE_NUMBER; }

const char *tm_error_name       (TmErrorCode code);
gboolean    tm_error_code_parse (const char *text, TmErrorCode *out);

/* Excel's coercions: an empty cell is 0 in arithmetic and "" in text,
 * TRUE is 1, and text is a number only if the whole of it is one.  A
 * coercion that cannot be made gives #VALUE! rather than a guess. */
gboolean tm_value_to_number (const TmValue *value, double *out, TmErrorCode *error);
gboolean tm_value_to_bool   (const TmValue *value, gboolean *out, TmErrorCode *error);
char    *tm_value_to_text   (const TmValue *value);

/* Numbers before text before booleans; text without regard to case. */
int      tm_value_compare   (const TmValue *a, const TmValue *b);

/* What a user typed, read as a constant: "12", "1e3", "45%", "TRUE",
 * "#N/A", or else text.  A leading apostrophe forces text. */
TmValue  tm_value_parse_input (const char *input);
/* A number typed with a currency sign in front or thousands separators
 * ("$1,234.50"), without them ("1234.50"); NULL if it has neither. */
char    *tm_number_plain (const char *text);
/* A number the way the General format shows it: up to eleven significant
 * digits, no trailing zeros, exponent only when it must. */
char    *tm_number_format_general (double number);

G_END_DECLS
