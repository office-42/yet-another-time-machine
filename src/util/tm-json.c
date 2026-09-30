/* tm-json.c - just enough JSON to read GeoJSON
 *
 * Copyright (C) 2026 The timemachine authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "tm-json.h"

#include <string.h>

typedef struct {
  const char *p, *end;
  int depth;
  char *error;
} Parser;

static TmJson *parse_value (Parser *ps);

static void
fail (Parser *ps, const char *message)
{
  if (ps->error == NULL)
    ps->error = g_strdup (message);
}

static void
skip_space (Parser *ps)
{
  while (ps->p < ps->end && (*ps->p == ' ' || *ps->p == '\t' || *ps->p == '\n' || *ps->p == '\r'))
    ps->p++;
}

static TmJson *
node (TmJsonType type)
{
  TmJson *j = g_new0 (TmJson, 1);
  j->type = type;
  return j;
}

void
tm_json_free (TmJson *json)
{
  if (json == NULL)
    return;
  g_free (json->string);
  if (json->items != NULL)
    {
      for (guint i = 0; i < json->items->len; i++)
        tm_json_free (g_ptr_array_index (json->items, i));
      g_ptr_array_free (json->items, TRUE);
    }
  if (json->keys != NULL)
    g_ptr_array_free (json->keys, TRUE);
  g_free (json);
}

static char *
parse_string (Parser *ps)
{
  GString *out = g_string_new (NULL);

  ps->p++;                              /* the opening quote */
  while (ps->p < ps->end && *ps->p != '"')
    {
      char c = *ps->p++;

      if (c != '\\')
        {
          g_string_append_c (out, c);
          continue;
        }
      if (ps->p >= ps->end)
        break;
      c = *ps->p++;
      switch (c)
        {
        case 'n': g_string_append_c (out, '\n'); break;
        case 't': g_string_append_c (out, '\t'); break;
        case 'r': g_string_append_c (out, '\r'); break;
        case 'b': g_string_append_c (out, '\b'); break;
        case 'f': g_string_append_c (out, '\f'); break;
        case 'u':
          {
            gunichar ch = 0;

            for (int i = 0; i < 4 && ps->p < ps->end; i++)
              ch = ch * 16 + g_ascii_xdigit_value (*ps->p++);
            /* A surrogate pair: the low half follows as \uDCxx. */
            if (ch >= 0xd800 && ch < 0xdc00 && ps->end - ps->p >= 6 && ps->p[0] == '\\' && ps->p[1] == 'u')
              {
                gunichar lo = 0;
                ps->p += 2;
                for (int i = 0; i < 4; i++)
                  lo = lo * 16 + g_ascii_xdigit_value (*ps->p++);
                ch = 0x10000 + ((ch - 0xd800) << 10) + (lo - 0xdc00);
              }
            g_string_append_unichar (out, ch);
          }
          break;
        default:
          g_string_append_c (out, c);   /* \" \\ \/ */
          break;
        }
    }
  if (ps->p >= ps->end)
    fail (ps, "a string is not closed");
  else
    ps->p++;
  return g_string_free (out, FALSE);
}

static TmJson *
parse_value (Parser *ps)
{
  TmJson *j;

  skip_space (ps);
  if (ps->p >= ps->end)
    {
      fail (ps, "the text stops where a value should be");
      return NULL;
    }
  if (++ps->depth > 256)
    {
      fail (ps, "nested too deeply");
      return NULL;
    }

  switch (*ps->p)
    {
    case '{':
      j = node (TM_JSON_OBJECT);
      j->items = g_ptr_array_new ();
      j->keys = g_ptr_array_new_with_free_func (g_free);
      ps->p++;
      skip_space (ps);
      if (ps->p < ps->end && *ps->p == '}')
        {
          ps->p++;
          break;
        }
      for (;;)
        {
          TmJson *v;
          char *key;

          skip_space (ps);
          if (ps->p >= ps->end || *ps->p != '"')
            {
              fail (ps, "an object's key must be a string");
              break;
            }
          key = parse_string (ps);
          skip_space (ps);
          if (ps->p >= ps->end || *ps->p != ':')
            {
              g_free (key);
              fail (ps, "a colon is missing after a key");
              break;
            }
          ps->p++;
          v = parse_value (ps);
          if (v == NULL)
            {
              g_free (key);
              break;
            }
          g_ptr_array_add (j->keys, key);
          g_ptr_array_add (j->items, v);
          skip_space (ps);
          if (ps->p < ps->end && *ps->p == ',')
            {
              ps->p++;
              continue;
            }
          if (ps->p < ps->end && *ps->p == '}')
            ps->p++;
          else
            fail (ps, "an object is not closed");
          break;
        }
      break;

    case '[':
      j = node (TM_JSON_ARRAY);
      j->items = g_ptr_array_new ();
      ps->p++;
      skip_space (ps);
      if (ps->p < ps->end && *ps->p == ']')
        {
          ps->p++;
          break;
        }
      for (;;)
        {
          TmJson *v = parse_value (ps);

          if (v == NULL)
            break;
          g_ptr_array_add (j->items, v);
          skip_space (ps);
          if (ps->p < ps->end && *ps->p == ',')
            {
              ps->p++;
              continue;
            }
          if (ps->p < ps->end && *ps->p == ']')
            ps->p++;
          else
            fail (ps, "an array is not closed");
          break;
        }
      break;

    case '"':
      j = node (TM_JSON_STRING);
      j->string = parse_string (ps);
      break;

    case 't':
    case 'f':
    case 'n':
      {
        const char *words[] = { "true", "false", "null" };
        int k = *ps->p == 't' ? 0 : *ps->p == 'f' ? 1 : 2;
        gsize len = strlen (words[k]);

        if ((gsize) (ps->end - ps->p) < len || strncmp (ps->p, words[k], len) != 0)
          {
            fail (ps, "an unknown word");
            j = NULL;
            break;
          }
        ps->p += len;
        j = node (k == 2 ? TM_JSON_NULL : TM_JSON_BOOL);
        j->boolean = k == 0;
      }
      break;

    default:
      {
        char buf[64];
        const char *s = ps->p;
        gsize len;
        char *endp;

        while (ps->p < ps->end && strchr ("+-0123456789.eE", *ps->p) != NULL)
          ps->p++;
        len = (gsize) (ps->p - s);
        if (len == 0 || len >= sizeof buf)
          {
            fail (ps, "something unexpected where a value should be");
            j = NULL;
            break;
          }
        memcpy (buf, s, len);
        buf[len] = '\0';
        j = node (TM_JSON_NUMBER);
        j->number = g_ascii_strtod (buf, &endp);
        if (*endp != '\0')
          fail (ps, "a malformed number");
      }
      break;
    }
  ps->depth--;
  return j;
}

TmJson *
tm_json_parse (const char *text, gsize length, GError **error)
{
  Parser ps = { text, text + length, 0, NULL };
  TmJson *j = parse_value (&ps);

  skip_space (&ps);
  if (ps.error == NULL && ps.p < ps.end)
    fail (&ps, "something left over after the value");
  if (ps.error != NULL)
    {
      g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "not valid JSON: %s, at byte %ld",
                   ps.error, (long) (ps.p - text));
      g_free (ps.error);
      tm_json_free (j);
      return NULL;
    }
  return j;
}

const TmJson *
tm_json_member (const TmJson *object, const char *key)
{
  if (object == NULL || object->type != TM_JSON_OBJECT)
    return NULL;
  for (guint i = 0; i < object->keys->len; i++)
    if (strcmp (g_ptr_array_index (object->keys, i), key) == 0)
      return g_ptr_array_index (object->items, i);
  return NULL;
}
