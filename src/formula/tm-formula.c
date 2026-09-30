/* tm-formula.c - formulas, parsed and printed
 *
 * Copyright (C) 2026 The timemachine authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Recursive descent over a hand-written lexer.  The grammar, loosest
 * binding first, is Excel's:
 *
 *   comparison      =  <>  <  >  <=  >=
 *   concatenation   &
 *   additive        +  -
 *   multiplicative  *  /
 *   power           ^         (left to right: 2^3^2 is 64, as in Excel)
 *   unary           -  +      (tighter than ^: -2^2 is 4)
 *   postfix         %
 *   primary         number, "text", TRUE, #N/A, (expr), A1, A1:B2, F(...)
 */

#include "tm-formula.h"
#include "tm-eval.h"

#include <math.h>
#include <string.h>

typedef enum {
  TOK_END,
  TOK_NUMBER,
  TOK_TEXT,
  TOK_WORD,       /* a function name, a reference, TRUE, or a name */
  TOK_ERROR,
  TOK_OP,         /* one of + - * / ^ & % = < > <= >= <> */
  TOK_LPAREN,
  TOK_RPAREN,
  TOK_COMMA,
  TOK_COLON,
  TOK_BAD
} TokType;

typedef struct {
  const char *text;
  const char *pos;
  TokType type;
  char *value;        /* the token's text, owned */
  double number;
  char *error;        /* first error, owned */
} Parser;

static void
fail (Parser *p, const char *message)
{
  if (p->error == NULL)
    p->error = g_strdup (message);
}

static void
next (Parser *p)
{
  const char *s = p->pos;

  g_clear_pointer (&p->value, g_free);
  while (g_ascii_isspace (*s))
    s++;

  if (*s == '\0')
    {
      p->type = TOK_END;
      p->pos = s;
      return;
    }

  if (g_ascii_isdigit (*s) || (*s == '.' && g_ascii_isdigit (s[1])))
    {
      const char *start = s;
      char *end;

      while (g_ascii_isdigit (*s))
        s++;
      if (*s == '.')
        {
          s++;
          while (g_ascii_isdigit (*s))
            s++;
        }
      if ((*s == 'e' || *s == 'E')
          && (g_ascii_isdigit (s[1])
              || ((s[1] == '+' || s[1] == '-') && g_ascii_isdigit (s[2]))))
        {
          s += 2;
          while (g_ascii_isdigit (*s))
            s++;
        }
      p->value = g_strndup (start, (gsize) (s - start));
      p->number = g_ascii_strtod (p->value, &end);
      p->type = TOK_NUMBER;
      p->pos = s;
      return;
    }

  if (*s == '"')
    {
      GString *str = g_string_new (NULL);

      s++;
      for (;;)
        {
          if (*s == '\0')
            {
              fail (p, "a text in quotes is not closed");
              g_string_free (str, TRUE);
              p->type = TOK_BAD;
              p->pos = s;
              return;
            }
          if (*s == '"')
            {
              if (s[1] == '"')
                {
                  g_string_append_c (str, '"');
                  s += 2;
                  continue;
                }
              s++;
              break;
            }
          g_string_append_c (str, *s++);
        }
      p->value = g_string_free (str, FALSE);
      p->type = TOK_TEXT;
      p->pos = s;
      return;
    }

  if (*s == '#')
    {
      /* The longest error name that matches. */
      static const char *const names[] = {
        "#DIV/0!", "#VALUE!", "#NULL!", "#NAME?", "#CIRC!", "#REF!", "#NUM!", "#N/A"
      };
      for (guint i = 0; i < G_N_ELEMENTS (names); i++)
        if (g_ascii_strncasecmp (s, names[i], strlen (names[i])) == 0)
          {
            p->value = g_strndup (s, strlen (names[i]));
            p->type = TOK_ERROR;
            p->pos = s + strlen (names[i]);
            return;
          }
      fail (p, "not an error value");
      p->type = TOK_BAD;
      p->pos = s + 1;
      return;
    }

  if (g_ascii_isalpha (*s) || *s == '_' || *s == '$')
    {
      const char *start = s;

      while (g_ascii_isalnum (*s) || *s == '_' || *s == '.' || *s == '$')
        s++;
      p->value = g_strndup (start, (gsize) (s - start));
      p->type = TOK_WORD;
      p->pos = s;
      return;
    }

  p->pos = s + 1;
  switch (*s)
    {
    case '(': p->type = TOK_LPAREN; return;
    case ')': p->type = TOK_RPAREN; return;
    case ',': case ';': p->type = TOK_COMMA; return;
    case ':': p->type = TOK_COLON; return;
    case '<':
      if (s[1] == '=' || s[1] == '>')
        {
          p->value = g_strndup (s, 2);
          p->pos = s + 2;
        }
      else
        p->value = g_strdup ("<");
      p->type = TOK_OP;
      return;
    case '>':
      if (s[1] == '=')
        {
          p->value = g_strdup (">=");
          p->pos = s + 2;
        }
      else
        p->value = g_strdup (">");
      p->type = TOK_OP;
      return;
    case '+': case '-': case '*': case '/': case '^': case '&': case '%': case '=':
      p->value = g_strndup (s, 1);
      p->type = TOK_OP;
      return;
    default:
      fail (p, "a character a formula cannot hold");
      p->type = TOK_BAD;
      return;
    }
}

static gboolean
is_op (Parser *p, const char *op)
{
  return p->type == TOK_OP && strcmp (p->value, op) == 0;
}

static TmNode *
node_new (TmNodeType type)
{
  TmNode *n = g_new0 (TmNode, 1);
  n->type = type;
  return n;
}

static TmNode *
binary (TmOp op, TmNode *left, TmNode *right)
{
  TmNode *n = node_new (TM_NODE_BINARY);

  n->as.binary.op = op;
  n->as.binary.left = left;
  n->as.binary.right = right;
  return n;
}

static TmNode *parse_expr (Parser *p);

/* A word as a reference: "A1", "$B$7".  Fills abs with which halves had
 * a dollar. */
static gboolean
word_as_ref (const char *word, TmRef *ref, guint8 *abs)
{
  *abs = 0;
  if (!tm_ref_parse (word, ref))
    return FALSE;
  if (word[0] == '$')
    *abs |= TM_ABS_COL;
  if (strchr (word + 1, '$') != NULL)
    *abs |= TM_ABS_ROW;
  return TRUE;
}

static TmNode *
parse_call (Parser *p, const char *word)
{
  TmNode *n = node_new (TM_NODE_CALL);
  GPtrArray *args = g_ptr_array_new ();

  n->as.call.name = g_ascii_strup (word, -1);
  n->as.call.fn = tm_function_lookup (n->as.call.name);

  next (p);                             /* the ( */
  if (p->type != TOK_RPAREN)
    {
      for (;;)
        {
          if (p->type == TOK_COMMA || p->type == TOK_RPAREN)
            g_ptr_array_add (args, node_new (TM_NODE_MISSING));
          else
            {
              TmNode *arg = parse_expr (p);
              if (arg == NULL)
                break;
              g_ptr_array_add (args, arg);
            }
          if (p->type == TOK_COMMA)
            {
              next (p);
              continue;
            }
          break;
        }
    }
  if (p->type != TOK_RPAREN)
    fail (p, "a function's brackets are not closed");
  else
    next (p);

  n->as.call.n_args = (int) args->len;
  n->as.call.args = (TmNode **) g_ptr_array_free (args, FALSE);
  return n;
}

static TmNode *
parse_primary (Parser *p)
{
  TmNode *n;

  switch (p->type)
    {
    case TOK_NUMBER:
      n = node_new (TM_NODE_NUMBER);
      n->as.number.value = p->number;
      n->as.number.source = g_strdup (p->value);
      next (p);
      return n;

    case TOK_TEXT:
      n = node_new (TM_NODE_TEXT);
      n->as.text = g_strdup (p->value);
      next (p);
      return n;

    case TOK_ERROR:
      n = node_new (TM_NODE_ERROR);
      tm_error_code_parse (p->value, &n->as.error);
      next (p);
      return n;

    case TOK_LPAREN:
      next (p);
      n = node_new (TM_NODE_PAREN);
      n->as.arg = parse_expr (p);
      if (n->as.arg == NULL)
        {
          tm_node_free (n);
          return NULL;
        }
      if (p->type != TOK_RPAREN)
        fail (p, "a bracket is not closed");
      else
        next (p);
      return n;

    case TOK_WORD:
      {
        char *word = g_strdup (p->value);
        TmRef ref;
        guint8 abs;

        next (p);
        if (p->type == TOK_LPAREN)
          {
            n = parse_call (p, word);
            g_free (word);
            return n;
          }
        if (word_as_ref (word, &ref, &abs))
          {
            g_free (word);
            if (p->type == TOK_COLON)
              {
                TmRef ref2;
                guint8 abs2;

                next (p);
                if (p->type != TOK_WORD || !word_as_ref (p->value, &ref2, &abs2))
                  {
                    fail (p, "a range needs a cell after its colon");
                    return NULL;
                  }
                next (p);
                n = node_new (TM_NODE_RANGE);
                n->as.range.a = ref;
                n->as.range.b = ref2;
                n->as.range.abs_a = abs;
                n->as.range.abs_b = abs2;
                return n;
              }
            n = node_new (TM_NODE_REF);
            n->as.ref.ref = ref;
            n->as.ref.abs = abs;
            return n;
          }
        if (g_ascii_strcasecmp (word, "TRUE") == 0 || g_ascii_strcasecmp (word, "FALSE") == 0)
          {
            n = node_new (TM_NODE_BOOL);
            n->as.boolean = g_ascii_strcasecmp (word, "TRUE") == 0;
            g_free (word);
            return n;
          }
        n = node_new (TM_NODE_NAME);
        n->as.text = word;
        return n;
      }

    case TOK_END:
      fail (p, "the formula stops where a value should be");
      return NULL;

    default:
      fail (p, "something unexpected where a value should be");
      return NULL;
    }
}

static TmNode *
parse_postfix (Parser *p)
{
  TmNode *n = parse_primary (p);

  while (n != NULL && is_op (p, "%"))
    {
      TmNode *pct = node_new (TM_NODE_PERCENT);
      pct->as.arg = n;
      n = pct;
      next (p);
    }
  return n;
}

static TmNode *
parse_unary (Parser *p)
{
  if (is_op (p, "-") || is_op (p, "+"))
    {
      TmNode *n = node_new (is_op (p, "-") ? TM_NODE_NEGATE : TM_NODE_PLUS);

      next (p);
      n->as.arg = parse_unary (p);
      if (n->as.arg == NULL)
        {
          tm_node_free (n);
          return NULL;
        }
      return n;
    }
  return parse_postfix (p);
}

/* One level of left-associative binary operators. */
typedef TmNode *(*Level) (Parser *p);

static TmNode *
parse_level (Parser *p, Level operand, const char *const *ops, const TmOp *codes, int n_ops)
{
  TmNode *left = operand (p);

  while (left != NULL && p->type == TOK_OP)
    {
      int which = -1;

      for (int i = 0; i < n_ops; i++)
        if (strcmp (p->value, ops[i]) == 0)
          which = i;
      if (which < 0)
        break;
      next (p);
      {
        TmNode *right = operand (p);
        if (right == NULL)
          {
            tm_node_free (left);
            return NULL;
          }
        left = binary (codes[which], left, right);
      }
    }
  return left;
}

static TmNode *
parse_power (Parser *p)
{
  static const char *const ops[] = { "^" };
  static const TmOp codes[] = { TM_OP_POW };
  return parse_level (p, parse_unary, ops, codes, 1);
}

static TmNode *
parse_mul (Parser *p)
{
  static const char *const ops[] = { "*", "/" };
  static const TmOp codes[] = { TM_OP_MUL, TM_OP_DIV };
  return parse_level (p, parse_power, ops, codes, 2);
}

static TmNode *
parse_add (Parser *p)
{
  static const char *const ops[] = { "+", "-" };
  static const TmOp codes[] = { TM_OP_ADD, TM_OP_SUB };
  return parse_level (p, parse_mul, ops, codes, 2);
}

static TmNode *
parse_concat (Parser *p)
{
  static const char *const ops[] = { "&" };
  static const TmOp codes[] = { TM_OP_CONCAT };
  return parse_level (p, parse_add, ops, codes, 1);
}

static TmNode *
parse_expr (Parser *p)
{
  static const char *const ops[] = { "=", "<>", "<", "<=", ">", ">=" };
  static const TmOp codes[] = { TM_OP_EQ, TM_OP_NE, TM_OP_LT, TM_OP_LE, TM_OP_GT, TM_OP_GE };
  return parse_level (p, parse_concat, ops, codes, 6);
}

TmNode *
tm_formula_parse (const char *text, char **error)
{
  Parser p = { 0 };
  TmNode *node;

  p.text = text;
  p.pos = text;
  next (&p);
  node = parse_expr (&p);
  if (node != NULL && p.type != TOK_END)
    fail (&p, p.type == TOK_RPAREN ? "a closing bracket too many"
                                   : "something left over after the formula");
  if (p.error != NULL)
    {
      tm_node_free (node);
      node = NULL;
      if (error != NULL)
        *error = g_steal_pointer (&p.error);
    }
  g_free (p.error);
  g_free (p.value);
  return node;
}

void
tm_node_free (TmNode *node)
{
  if (node == NULL)
    return;
  switch (node->type)
    {
    case TM_NODE_NUMBER:
      g_free (node->as.number.source);
      break;
    case TM_NODE_TEXT:
    case TM_NODE_NAME:
      g_free (node->as.text);
      break;
    case TM_NODE_NEGATE:
    case TM_NODE_PLUS:
    case TM_NODE_PERCENT:
    case TM_NODE_PAREN:
      tm_node_free (node->as.arg);
      break;
    case TM_NODE_BINARY:
      tm_node_free (node->as.binary.left);
      tm_node_free (node->as.binary.right);
      break;
    case TM_NODE_CALL:
      for (int i = 0; i < node->as.call.n_args; i++)
        tm_node_free (node->as.call.args[i]);
      g_free (node->as.call.args);
      g_free (node->as.call.name);
      break;
    default:
      break;
    }
  g_free (node);
}

/* ---- Printing --------------------------------------------------------- */

static gboolean
print_ref (GString *out, TmRef ref, guint8 abs, int drow, int dcol)
{
  char col[8];

  if (!(abs & TM_ABS_ROW))
    ref.row += drow;
  if (!(abs & TM_ABS_COL))
    ref.col += dcol;
  if (ref.row < 0 || ref.row >= TM_MAX_ROWS || ref.col < 0 || ref.col >= TM_MAX_COLS)
    return FALSE;
  tm_col_name (ref.col, col, sizeof col);
  g_string_append_printf (out, "%s%s%s%d",
                          (abs & TM_ABS_COL) ? "$" : "", col,
                          (abs & TM_ABS_ROW) ? "$" : "", ref.row + 1);
  return TRUE;
}

static const char *
op_text (TmOp op)
{
  static const char *const text[] = {
    "+", "-", "*", "/", "^", "&", "=", "<>", "<", "<=", ">", ">="
  };
  return text[op];
}

static void
print_node (GString *out, const TmNode *n, int drow, int dcol)
{
  switch (n->type)
    {
    case TM_NODE_NUMBER:
      g_string_append (out, n->as.number.source);
      break;
    case TM_NODE_TEXT:
      g_string_append_c (out, '"');
      for (const char *s = n->as.text; *s != '\0'; s++)
        {
          if (*s == '"')
            g_string_append_c (out, '"');
          g_string_append_c (out, *s);
        }
      g_string_append_c (out, '"');
      break;
    case TM_NODE_BOOL:
      g_string_append (out, n->as.boolean ? "TRUE" : "FALSE");
      break;
    case TM_NODE_ERROR:
      g_string_append (out, tm_error_name (n->as.error));
      break;
    case TM_NODE_MISSING:
      break;
    case TM_NODE_REF:
      if (!print_ref (out, n->as.ref.ref, n->as.ref.abs, drow, dcol))
        g_string_append (out, "#REF!");
      break;
    case TM_NODE_RANGE:
      {
        gsize mark = out->len;

        if (!print_ref (out, n->as.range.a, n->as.range.abs_a, drow, dcol))
          {
            g_string_truncate (out, mark);
            g_string_append (out, "#REF!");
            break;
          }
        g_string_append_c (out, ':');
        if (!print_ref (out, n->as.range.b, n->as.range.abs_b, drow, dcol))
          {
            g_string_truncate (out, mark);
            g_string_append (out, "#REF!");
          }
      }
      break;
    case TM_NODE_NAME:
      g_string_append (out, n->as.text);
      break;
    case TM_NODE_NEGATE:
      g_string_append_c (out, '-');
      print_node (out, n->as.arg, drow, dcol);
      break;
    case TM_NODE_PLUS:
      g_string_append_c (out, '+');
      print_node (out, n->as.arg, drow, dcol);
      break;
    case TM_NODE_PERCENT:
      print_node (out, n->as.arg, drow, dcol);
      g_string_append_c (out, '%');
      break;
    case TM_NODE_PAREN:
      g_string_append_c (out, '(');
      print_node (out, n->as.arg, drow, dcol);
      g_string_append_c (out, ')');
      break;
    case TM_NODE_BINARY:
      print_node (out, n->as.binary.left, drow, dcol);
      g_string_append (out, op_text (n->as.binary.op));
      print_node (out, n->as.binary.right, drow, dcol);
      break;
    case TM_NODE_CALL:
      g_string_append (out, n->as.call.name);
      g_string_append_c (out, '(');
      for (int i = 0; i < n->as.call.n_args; i++)
        {
          if (i > 0)
            g_string_append_c (out, ',');
          print_node (out, n->as.call.args[i], drow, dcol);
        }
      g_string_append_c (out, ')');
      break;
    }
}

char *
tm_formula_print (const TmNode *node, int drow, int dcol)
{
  GString *out = g_string_new (NULL);

  print_node (out, node, drow, dcol);
  return g_string_free (out, FALSE);
}

char *
tm_formula_shift (const char *input, int drow, int dcol)
{
  TmNode *node;
  char *body, *result;

  if (input == NULL || input[0] != '=')
    return g_strdup (input);
  node = tm_formula_parse (input + 1, NULL);
  if (node == NULL)
    return g_strdup (input);
  body = tm_formula_print (node, drow, dcol);
  result = g_strconcat ("=", body, NULL);
  g_free (body);
  tm_node_free (node);
  return result;
}

static void
foreach_range (const TmNode *n, const char *prefix, gboolean take, TmRangeFunc func, gpointer data)
{
  TmRange r;

  switch (n->type)
    {
    case TM_NODE_REF:
      if (take)
        {
          r.row0 = r.row1 = n->as.ref.ref.row;
          r.col0 = r.col1 = n->as.ref.ref.col;
          func (&r, data);
        }
      break;
    case TM_NODE_RANGE:
      if (take)
        {
          r.row0 = n->as.range.a.row;
          r.col0 = n->as.range.a.col;
          r.row1 = n->as.range.b.row;
          r.col1 = n->as.range.b.col;
          tm_range_normalize (&r);
          func (&r, data);
        }
      break;
    case TM_NODE_NEGATE:
    case TM_NODE_PLUS:
    case TM_NODE_PERCENT:
    case TM_NODE_PAREN:
      foreach_range (n->as.arg, prefix, take, func, data);
      break;
    case TM_NODE_BINARY:
      foreach_range (n->as.binary.left, prefix, take, func, data);
      foreach_range (n->as.binary.right, prefix, take, func, data);
      break;
    case TM_NODE_CALL:
      for (int i = 0; i < n->as.call.n_args; i++)
        {
          gboolean t = take;

          if (prefix != NULL && i == 0
              && g_str_has_prefix (n->as.call.name, prefix))
            t = TRUE;
          foreach_range (n->as.call.args[i], prefix, t, func, data);
        }
      break;
    default:
      break;
    }
}

void
tm_formula_foreach_range (const TmNode *node, const char *prefix,
                          TmRangeFunc func, gpointer data)
{
  if (node != NULL)
    foreach_range (node, prefix, prefix == NULL, func, data);
}
