/* tm-formula.h - formulas, parsed
 *
 * Copyright (C) 2026 The timemachine authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A formula is parsed once, when it is typed, into a tree of TmNode, and
 * the tree is what the evaluator walks every time the sheet is
 * recalculated -- ten thousand times over in a simulation, which is why
 * functions are looked up here rather than there.
 */

#pragma once

#include "tm-value.h"

G_BEGIN_DECLS

typedef struct _TmFunction TmFunction;   /* tm-eval.h */

typedef enum {
  TM_OP_ADD, TM_OP_SUB, TM_OP_MUL, TM_OP_DIV, TM_OP_POW, TM_OP_CONCAT,
  TM_OP_EQ, TM_OP_NE, TM_OP_LT, TM_OP_LE, TM_OP_GT, TM_OP_GE
} TmOp;

typedef enum {
  TM_NODE_NUMBER,
  TM_NODE_TEXT,
  TM_NODE_BOOL,
  TM_NODE_ERROR,
  TM_NODE_MISSING,   /* an argument left out: IF(A1,,2) */
  TM_NODE_REF,
  TM_NODE_RANGE,
  TM_NODE_NAME,      /* a word that is neither function nor reference */
  TM_NODE_NEGATE,
  TM_NODE_PLUS,
  TM_NODE_PERCENT,
  TM_NODE_PAREN,     /* kept so that a formula prints back as typed */
  TM_NODE_BINARY,
  TM_NODE_CALL
} TmNodeType;

/* Which halves of a reference have a $ in front of them. */
enum {
  TM_ABS_COL = 1 << 0,
  TM_ABS_ROW = 1 << 1
};

typedef struct _TmNode TmNode;

struct _TmNode {
  TmNodeType type;
  union {
    struct { double value; char *source; } number;
    char        *text;                         /* TEXT, NAME */
    gboolean     boolean;
    TmErrorCode  error;
    struct { TmRef ref; guint8 abs; } ref;
    struct { TmRef a, b; guint8 abs_a, abs_b; } range;
    TmNode      *arg;                          /* NEGATE, PLUS, PERCENT, PAREN */
    struct { TmOp op; TmNode *left, *right; } binary;
    struct {
      char              *name;                 /* upper case, as printed */
      const TmFunction  *fn;                   /* NULL if unknown: #NAME? */
      TmNode           **args;
      int                n_args;
    } call;
  } as;
};

/* text is the formula without its leading "=".  On failure returns NULL
 * and, if error is not NULL, says why. */
TmNode *tm_formula_parse (const char *text, char **error);
void    tm_node_free     (TmNode *node);

/* The formula as text, without the "=", every relative reference moved by
 * drow rows and dcol columns -- which is what copying a cell does.  A
 * reference pushed off the sheet becomes #REF!. */
char   *tm_formula_print (const TmNode *node, int drow, int dcol);

/* The same, from and to the text a user typed ("=A1+1" to "=A2+1"). */
char   *tm_formula_shift (const char *input, int drow, int dcol);

/* Calls func on every reference and range the formula names, including
 * the first argument of each call to a function whose name starts with
 * prefix when prefix is not NULL (and only those, then). */
typedef void (*TmRangeFunc) (const TmRange *range, gpointer data);
void    tm_formula_foreach_range (const TmNode *node, const char *prefix,
                                  TmRangeFunc func, gpointer data);

/* Whether the formula calls a function with any of flags (TM_FN_RANDOM,
 * TM_FN_SIM) anywhere in it. */
gboolean tm_formula_calls (const TmNode *node, guint flags);

G_END_DECLS
