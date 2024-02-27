/* WirePlumber
 *
 * Copyright © 2021 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "../common/test-log.h"

static void
test_spa_json_basic (void)
{
  /* Null */
  {
    g_autoptr (WpSpaJson) json = wp_spa_json_new_null ();
    g_assert_nonnull (json);
    g_assert_true (wp_spa_json_is_null (json));
    g_assert_cmpmem (wp_spa_json_get_data (json), wp_spa_json_get_size (json),
        "null", 4);
  }

  /* Boolean */
  {
    g_autoptr (WpSpaJson) json = wp_spa_json_new_boolean (TRUE);
    g_assert_nonnull (json);
    g_assert_true (wp_spa_json_is_boolean (json));
    gboolean v = FALSE;
    g_assert_true (wp_spa_json_parse_boolean (json, &v));
    g_assert_true (v);
    g_assert_cmpmem (wp_spa_json_get_data (json), wp_spa_json_get_size (json),
        "true", 4);
  }

  /* Int */
  {
    g_autoptr (WpSpaJson) json = wp_spa_json_new_int (8);
    g_assert_nonnull (json);
    g_assert_true (wp_spa_json_is_int (json));
    gint32 v = 0;
    g_assert_true (wp_spa_json_parse_int (json, &v));
    g_assert_cmpint (v, ==, 8);
    g_assert_cmpmem (wp_spa_json_get_data (json), wp_spa_json_get_size (json),
        "8", 1);
  }

  /* Float */
  {
    g_autoptr (WpSpaJson) json = wp_spa_json_new_float (3.14f);
    g_assert_nonnull (json);
    g_assert_true (wp_spa_json_is_float (json));
    float v = 0;
    g_assert_true (wp_spa_json_parse_float (json, &v));
    g_assert_cmpfloat_with_epsilon (v, 3.14f, 0.001f);
  }

  /* String */
  {
    g_autoptr (WpSpaJson) json = wp_spa_json_new_string ("wireplumber");
    g_assert_nonnull (json);
    g_assert_true (wp_spa_json_is_string (json));
    g_autofree gchar *v1 = wp_spa_json_parse_string (json);
    g_assert_nonnull (v1);
    g_assert_cmpstr (v1, ==, "wireplumber");

    g_autoptr (WpSpaJson) jsone = wp_spa_json_new_string ("");
    g_assert_nonnull (jsone);
    g_assert_true (wp_spa_json_is_string (jsone));
    g_autofree gchar *v2 = wp_spa_json_parse_string (jsone);
    g_assert_nonnull (v2);
    g_assert_cmpstr (v2, ==, "");

    g_autoptr (WpSpaJson) jsonl = wp_spa_json_new_string (
        "looooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooong");
    g_assert_nonnull (jsonl);
    g_assert_true (wp_spa_json_is_string (jsonl));
    g_autofree gchar *v3 = wp_spa_json_parse_string (jsonl);
    g_assert_nonnull (v3);
    g_assert_cmpstr (v3, ==,
        "looooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooong");

    g_autoptr (WpSpaJson) jsons = wp_spa_json_new_string ("\v\v\v\v");
    g_assert_nonnull (jsons);
    g_assert_true (wp_spa_json_is_string (jsons));
    g_autofree gchar *v4 = wp_spa_json_parse_string (jsons);
    g_assert_nonnull (v4);
    g_assert_cmpstr (v4, ==, "\v\v\v\v");
  }

  /* Array */
  {
    g_autoptr (WpSpaJson) empty = wp_spa_json_new_array (NULL, NULL);
    g_assert_nonnull (empty);
    g_assert_true (wp_spa_json_is_array (empty));
    g_assert_cmpmem (wp_spa_json_get_data (empty), wp_spa_json_get_size (empty),
        "[]", 2);

    g_autoptr (WpSpaJson) json = wp_spa_json_new_array ("i", 1, "i", 2, NULL);
    g_assert_nonnull (json);
    g_assert_true (wp_spa_json_is_array (json));
    gint32 v1 = 0, v2 = 0;
    g_assert_true (wp_spa_json_parse_array (json, "i", &v1, "i", &v2, NULL));
    g_assert_cmpint (v1, ==, 1);
    g_assert_cmpint (v2, ==, 2);
    g_assert_cmpmem (wp_spa_json_get_data (json), wp_spa_json_get_size (json),
        "[1, 2]", 6);
  }

  /* Object */
  {
    g_autoptr (WpSpaJson) empty = wp_spa_json_new_object (NULL, NULL, NULL);
    g_assert_nonnull (empty);
    g_assert_true (wp_spa_json_is_object (empty));
    g_assert_cmpmem (wp_spa_json_get_data (empty), wp_spa_json_get_size (empty),
        "{}", 2);

    g_autoptr (WpSpaJson) subjson = wp_spa_json_new_array ("b", TRUE, NULL);
    g_autoptr (WpSpaJson) json = wp_spa_json_new_object (
        "key1", "n",
        "key2", "b", TRUE,
        "key3", "i", 3,
        "key4", "f", 2.72f,
        "key5", "s", "str",
        "key6", "J", subjson,
        NULL);
    g_assert_nonnull (json);
    g_assert_true (wp_spa_json_is_object (json));

    {
      g_autofree gchar *key1 = NULL, *key2 = NULL, *key3 = NULL, *key4 = NULL,
          *key5 = NULL, *key6 = NULL;
      gboolean v2 = FALSE;
      gint32 v3 = 0;
      float v4 = 0.0f;
      g_autofree gchar *v5 = NULL;
      g_autoptr (WpSpaJson) v6 = NULL;
      g_assert_true (wp_spa_json_parse_object (json,
          &key1, "n",
          &key2, "b", &v2,
          &key3, "i", &v3,
          &key4, "f", &v4,
          &key5, "s", &v5,
          &key6, "J", &v6,
          NULL));
      g_assert_cmpstr (key1, ==, "key1");
      g_assert_cmpstr (key2, ==, "key2");
      g_assert_true (v2);
      g_assert_cmpstr (key3, ==, "key3");
      g_assert_cmpint (v3, ==, 3);
      g_assert_cmpstr (key4, ==, "key4");
      g_assert_cmpfloat_with_epsilon (v4, 2.72f, 0.001f);
      g_assert_cmpstr (key5, ==, "key5");
      g_assert_cmpstr (v5, ==, "str");
      g_assert_cmpstr (key6, ==, "key6");
      g_assert_nonnull (v6);
      g_assert_cmpmem (wp_spa_json_get_data (v6), wp_spa_json_get_size (v6),
          "[true]", 6);
    }

    {
      gboolean v2 = FALSE;
      gint32 v3 = 0;
      float v4 = 0.0f;
      g_autofree gchar *v5 = NULL;
      g_autoptr (WpSpaJson) v6 = NULL;
      g_assert_true (wp_spa_json_object_get (json,
          "key6", "J", &v6,
          "key3", "i", &v3,
          "key5", "s", &v5,
          "key1", "n",
          "key2", "b", &v2,
          "key4", "f", &v4,
          NULL));
      g_assert_true (v2);
      g_assert_cmpint (v3, ==, 3);
      g_assert_cmpfloat_with_epsilon (v4, 2.72f, 0.001f);
      g_assert_cmpstr (v5, ==, "str");
      g_assert_nonnull (v6);
      g_assert_cmpmem (wp_spa_json_get_data (v6), wp_spa_json_get_size (v6),
          "[true]", 6);
    }
  }
}

static void
test_spa_json_array_builder_parser_iterator (void)
{
  g_autoptr (WpSpaJson) json = NULL;

  {
    g_autoptr (WpSpaJsonBuilder) b = wp_spa_json_builder_new_array ();
    g_assert_nonnull (b);
    wp_spa_json_builder_add_int (b, 1);
    wp_spa_json_builder_add_int (b, 2);
    wp_spa_json_builder_add_int (b, 3);
    json = wp_spa_json_builder_end (b);
  }

  g_assert_true (wp_spa_json_is_array (json));
  g_assert_cmpmem (wp_spa_json_get_data (json), wp_spa_json_get_size (json),
      "[1, 2, 3]", 9);

  {
    g_autoptr (WpSpaJsonParser) p = wp_spa_json_parser_new_array (json);
    g_assert_nonnull (p);
    gint32 v = 0;
    g_assert_true (wp_spa_json_parser_get_int (p, &v));
    g_assert_cmpint (v, ==, 1);
    g_assert_true (wp_spa_json_parser_get_int (p, &v));
    g_assert_cmpint (v, ==, 2);
    g_assert_true (wp_spa_json_parser_get_int (p, &v));
    g_assert_cmpint (v, ==, 3);
    wp_spa_json_parser_end (p);
    g_assert_false (wp_spa_json_parser_get_null (p));
  }

  g_autoptr (WpIterator) it = wp_spa_json_new_iterator (json);
  g_assert_nonnull (it);

  {
    GValue next = G_VALUE_INIT;
    g_assert_true (wp_iterator_next (it, &next));
    WpSpaJson *j = g_value_get_boxed (&next);
    g_assert_nonnull (j);
    g_assert_true (wp_spa_json_is_int (j));
    gint32 v = 0;
    g_assert_true (wp_spa_json_parse_int (j, &v));
    g_assert_cmpint (v, ==, 1);
    g_value_unset (&next);
  }

  {
    GValue next = G_VALUE_INIT;
    g_assert_true (wp_iterator_next (it, &next));
    WpSpaJson *j = g_value_get_boxed (&next);
    g_assert_nonnull (j);
    g_assert_true (wp_spa_json_is_int (j));
    gint32 v = 0;
    g_assert_true (wp_spa_json_parse_int (j, &v));
    g_assert_cmpint (v, ==, 2);
    g_value_unset (&next);
  }

  {
    GValue next = G_VALUE_INIT;
    g_assert_true (wp_iterator_next (it, &next));
    WpSpaJson *j = g_value_get_boxed (&next);
    g_assert_nonnull (j);
    g_assert_true (wp_spa_json_is_int (j));
    gint32 v = 0;
    g_assert_true (wp_spa_json_parse_int (j, &v));
    g_assert_cmpint (v, ==, 3);
    g_value_unset (&next);
  }

  g_assert_false (wp_iterator_next (it, NULL));
  wp_iterator_reset (it);

  {
    GValue next = G_VALUE_INIT;
    g_assert_true (wp_iterator_next (it, &next));
    WpSpaJson *j = g_value_get_boxed (&next);
    g_assert_nonnull (j);
    g_assert_true (wp_spa_json_is_int (j));
    gint32 v = 0;
    g_assert_true (wp_spa_json_parse_int (j, &v));
    g_assert_cmpint (v, ==, 1);
    g_value_unset (&next);
  }
}

static void
test_spa_json_object_builder_parser_iterator (void)
{
  g_autoptr (WpSpaJson) json = NULL;

  {
    g_autoptr (WpSpaJsonBuilder) b = wp_spa_json_builder_new_object ();
    g_assert_nonnull (b);
    wp_spa_json_builder_add_property (b, "key-null");
    wp_spa_json_builder_add_null (b);
    wp_spa_json_builder_add_property (b, "key-boolean");
    wp_spa_json_builder_add_boolean (b, TRUE);
    wp_spa_json_builder_add_property (b, "key-int");
    wp_spa_json_builder_add_int (b, 7);
    wp_spa_json_builder_add_property (b, "key-float");
    wp_spa_json_builder_add_float (b, 0.12f);
    wp_spa_json_builder_add_property (b, "key-string");
    wp_spa_json_builder_add_string (b, "str");
    wp_spa_json_builder_add_property (b, "key-empty-string");
    wp_spa_json_builder_add_string (b, "");
    wp_spa_json_builder_add_property (b, "key-special-char-string");
    wp_spa_json_builder_add_string (b, "\v\v\v\v");
    json = wp_spa_json_builder_end (b);
  }

  g_assert_true (wp_spa_json_is_object (json));

  {
    g_autoptr (WpSpaJsonParser) p = wp_spa_json_parser_new_object (json);
    g_assert_nonnull (p);

    g_autofree gchar *key_null = wp_spa_json_parser_get_string (p);
    g_assert_nonnull (key_null);
    g_assert_cmpstr (key_null, ==, "key-null");
    g_assert_true (wp_spa_json_parser_get_null (p));

    g_autofree gchar *key_boolean = wp_spa_json_parser_get_string (p);
    g_assert_nonnull (key_boolean);
    g_assert_cmpstr (key_boolean, ==, "key-boolean");
    gboolean v_boolean = FALSE;
    g_assert_true (wp_spa_json_parser_get_boolean (p, &v_boolean));
    g_assert_true (v_boolean);

    g_autofree gchar *key_int = wp_spa_json_parser_get_string (p);
    g_assert_nonnull (key_int);
    g_assert_cmpstr (key_int, ==, "key-int");
    gint32 v_int = 0;
    g_assert_true (wp_spa_json_parser_get_int (p, &v_int));
    g_assert_cmpint (v_int, ==, 7);

    g_autofree gchar *key_float = wp_spa_json_parser_get_string (p);
    g_assert_nonnull (key_float);
    g_assert_cmpstr (key_float, ==, "key-float");
    float v_float = 0.0f;
    g_assert_true (wp_spa_json_parser_get_float (p, &v_float));
    g_assert_cmpfloat_with_epsilon (v_float, 0.12f, 0.001f);

    g_autofree gchar *key_string = wp_spa_json_parser_get_string (p);
    g_assert_nonnull (key_string);
    g_assert_cmpstr (key_string, ==, "key-string");
    g_autofree gchar *v_string = wp_spa_json_parser_get_string (p);
    g_assert_nonnull (v_string);
    g_assert_cmpstr (v_string, ==, "str");

    g_autofree gchar *key_empty_string = wp_spa_json_parser_get_string (p);
    g_assert_nonnull (key_empty_string);
    g_assert_cmpstr (key_empty_string, ==, "key-empty-string");
    g_autofree gchar *v_empty_string = wp_spa_json_parser_get_string (p);
    g_assert_nonnull (v_empty_string);
    g_assert_cmpstr (v_empty_string, ==, "");

    g_autofree gchar *key_special_char_string = wp_spa_json_parser_get_string (p);
    g_assert_nonnull (key_special_char_string);
    g_assert_cmpstr (key_special_char_string, ==, "key-special-char-string");
    g_autofree gchar *v_special_char_string = wp_spa_json_parser_get_string (p);
    g_assert_nonnull (v_special_char_string);
    g_assert_cmpstr (v_special_char_string, ==, "\v\v\v\v");

    wp_spa_json_parser_end (p);
    g_assert_false (wp_spa_json_parser_get_null (p));
  }

  g_autoptr (WpIterator) it = wp_spa_json_new_iterator (json);
  g_assert_nonnull (it);

  {
    GValue next = G_VALUE_INIT;
    g_assert_true (wp_iterator_next (it, &next));
    WpSpaJson *j = g_value_get_boxed (&next);
    g_assert_nonnull (j);
    g_assert_true (wp_spa_json_is_string (j));
    g_autofree gchar *v = wp_spa_json_parse_string (j);
    g_assert_nonnull (v);
    g_assert_cmpstr (v, ==, "key-null");
    g_value_unset (&next);
  }

  {
    GValue next = G_VALUE_INIT;
    g_assert_true (wp_iterator_next (it, &next));
    WpSpaJson *j = g_value_get_boxed (&next);
    g_assert_nonnull (j);
    g_assert_true (wp_spa_json_is_null (j));
    g_value_unset (&next);
  }

  {
    GValue next = G_VALUE_INIT;
    g_assert_true (wp_iterator_next (it, &next));
    WpSpaJson *j = g_value_get_boxed (&next);
    g_assert_nonnull (j);
    g_assert_true (wp_spa_json_is_string (j));
    g_autofree gchar *v = wp_spa_json_parse_string (j);
    g_assert_nonnull (v);
    g_assert_cmpstr (v, ==, "key-boolean");
    g_value_unset (&next);
  }

  {
    GValue next = G_VALUE_INIT;
    g_assert_true (wp_iterator_next (it, &next));
    WpSpaJson *j = g_value_get_boxed (&next);
    g_assert_nonnull (j);
    g_assert_true (wp_spa_json_is_boolean (j));
    gboolean v = FALSE;
    g_assert_true (wp_spa_json_parse_boolean (j, &v));
    g_assert_true (v);
    g_value_unset (&next);
  }

  {
    GValue next = G_VALUE_INIT;
    g_assert_true (wp_iterator_next (it, &next));
    WpSpaJson *j = g_value_get_boxed (&next);
    g_assert_nonnull (j);
    g_assert_true (wp_spa_json_is_string (j));
    g_autofree gchar *v = wp_spa_json_parse_string (j);
    g_assert_nonnull (v);
    g_assert_cmpstr (v, ==, "key-int");
    g_value_unset (&next);
  }

  {
    GValue next = G_VALUE_INIT;
    g_assert_true (wp_iterator_next (it, &next));
    WpSpaJson *j = g_value_get_boxed (&next);
    g_assert_nonnull (j);
    g_assert_true (wp_spa_json_is_int (j));
    gint32 v = 0;
    g_assert_true (wp_spa_json_parse_int (j, &v));
    g_assert_cmpint (v, ==, 7);
    g_value_unset (&next);
  }

  {
    GValue next = G_VALUE_INIT;
    g_assert_true (wp_iterator_next (it, &next));
    WpSpaJson *j = g_value_get_boxed (&next);
    g_assert_nonnull (j);
    g_assert_true (wp_spa_json_is_string (j));
    g_autofree gchar *v = wp_spa_json_parse_string (j);
    g_assert_nonnull (v);
    g_assert_cmpstr (v, ==, "key-float");
    g_value_unset (&next);
  }

  {
    GValue next = G_VALUE_INIT;
    g_assert_true (wp_iterator_next (it, &next));
    WpSpaJson *j = g_value_get_boxed (&next);
    g_assert_nonnull (j);
    g_assert_true (wp_spa_json_is_float (j));
    float v = 0;
    g_assert_true (wp_spa_json_parse_float (j, &v));
    g_assert_cmpfloat_with_epsilon (v, 0.12f, 0.001f);
    g_value_unset (&next);
  }

  {
    GValue next = G_VALUE_INIT;
    g_assert_true (wp_iterator_next (it, &next));
    WpSpaJson *j = g_value_get_boxed (&next);
    g_assert_nonnull (j);
    g_assert_true (wp_spa_json_is_string (j));
    g_autofree gchar *v = wp_spa_json_parse_string (j);
    g_assert_nonnull (v);
    g_assert_cmpstr (v, ==, "key-string");
    g_value_unset (&next);
  }

  {
    GValue next = G_VALUE_INIT;
    g_assert_true (wp_iterator_next (it, &next));
    WpSpaJson *j = g_value_get_boxed (&next);
    g_assert_nonnull (j);
    g_assert_true (wp_spa_json_is_string (j));
    g_autofree gchar *v = wp_spa_json_parse_string (j);
    g_assert_nonnull (v);
    g_assert_cmpstr (v, ==, "str");
    g_value_unset (&next);
  }

  {
    GValue next = G_VALUE_INIT;
    g_assert_true (wp_iterator_next (it, &next));
    WpSpaJson *j = g_value_get_boxed (&next);
    g_assert_nonnull (j);
    g_assert_true (wp_spa_json_is_string (j));
    g_autofree gchar *v = wp_spa_json_parse_string (j);
    g_assert_nonnull (v);
    g_assert_cmpstr (v, ==, "key-empty-string");
    g_value_unset (&next);
  }

  {
    GValue next = G_VALUE_INIT;
    g_assert_true (wp_iterator_next (it, &next));
    WpSpaJson *j = g_value_get_boxed (&next);
    g_assert_nonnull (j);
    g_assert_true (wp_spa_json_is_string (j));
    g_autofree gchar *v = wp_spa_json_parse_string (j);
    g_assert_nonnull (v);
    g_assert_cmpstr (v, ==, "");
    g_value_unset (&next);
  }

  {
    GValue next = G_VALUE_INIT;
    g_assert_true (wp_iterator_next (it, &next));
    WpSpaJson *j = g_value_get_boxed (&next);
    g_assert_nonnull (j);
    g_assert_true (wp_spa_json_is_string (j));
    g_autofree gchar *v = wp_spa_json_parse_string (j);
    g_assert_nonnull (v);
    g_assert_cmpstr (v, ==, "key-special-char-string");
    g_value_unset (&next);
  }

  {
    GValue next = G_VALUE_INIT;
    g_assert_true (wp_iterator_next (it, &next));
    WpSpaJson *j = g_value_get_boxed (&next);
    g_assert_nonnull (j);
    g_assert_true (wp_spa_json_is_string (j));
    g_autofree gchar *v = wp_spa_json_parse_string (j);
    g_assert_nonnull (v);
    g_assert_cmpstr (v, ==, "\v\v\v\v");
    g_value_unset (&next);
  }

  g_assert_false (wp_iterator_next (it, NULL));
  wp_iterator_reset (it);

  {
    GValue next = G_VALUE_INIT;
    g_assert_true (wp_iterator_next (it, &next));
    WpSpaJson *j = g_value_get_boxed (&next);
    g_assert_nonnull (j);
    g_assert_true (wp_spa_json_is_string (j));
    g_autofree gchar *v = wp_spa_json_parse_string (j);
    g_assert_nonnull (v);
    g_assert_cmpstr (v, ==, "key-null");
    g_value_unset (&next);
  }
}

static void
test_spa_json_nested (void)
{
  g_autoptr (WpSpaJson) array = NULL;
  g_autoptr (WpSpaJson) array2 = NULL;
  g_autoptr (WpSpaJson) object = NULL;
  g_autoptr (WpSpaJson) json = NULL;

  {
    g_autoptr (WpSpaJsonBuilder) b = wp_spa_json_builder_new_array ();
    g_assert_nonnull (b);
    wp_spa_json_builder_add_int (b, 5);
    wp_spa_json_builder_add_int (b, 10);
    wp_spa_json_builder_add_int (b, 15);
    array = wp_spa_json_builder_end (b);
  }
  g_assert_true (wp_spa_json_is_array (array));
  g_assert_cmpuint (wp_spa_json_get_size (array), ==, 11);
  g_assert_cmpmem (wp_spa_json_get_data (array), wp_spa_json_get_size (array),
      "[5, 10, 15]", 11);

  {
    g_autoptr (WpSpaJsonBuilder) b = wp_spa_json_builder_new_array ();
    g_assert_nonnull (b);
    wp_spa_json_builder_add_int (b, 2);
    wp_spa_json_builder_add_int (b, 4);
    array2 = wp_spa_json_builder_end (b);
  }
  g_assert_true (wp_spa_json_is_array (array2));
  g_assert_cmpuint (wp_spa_json_get_size (array2), ==, 6);
  g_assert_cmpmem (wp_spa_json_get_data (array2), wp_spa_json_get_size (array2),
      "[2, 4]", 6);

  {
    g_autoptr (WpSpaJsonBuilder) b = wp_spa_json_builder_new_object ();
    g_assert_nonnull (b);
    wp_spa_json_builder_add_property (b, "key-boolean");
    wp_spa_json_builder_add_boolean (b, FALSE);
    wp_spa_json_builder_add_property (b, "key-int");
    wp_spa_json_builder_add_int (b, 8);
    wp_spa_json_builder_add_property (b, "key-array");
    wp_spa_json_builder_add_json (b, array2);
    object = wp_spa_json_builder_end (b);
  }
  g_assert_true (wp_spa_json_is_object (object));
  g_assert_cmpuint (wp_spa_json_get_size (object), ==, 54);
  g_assert_cmpmem (wp_spa_json_get_data (object), wp_spa_json_get_size (object),
      "{\"key-boolean\":false, \"key-int\":8, \"key-array\":[2, 4]}", 54);

  {
    g_autoptr (WpSpaJsonBuilder) b = wp_spa_json_builder_new_object ();
    g_assert_nonnull (b);
    wp_spa_json_builder_add_property (b, "key-array");
    wp_spa_json_builder_add_json (b, array);
    wp_spa_json_builder_add_property (b, "key-object");
    wp_spa_json_builder_add_json (b, object);
    json = wp_spa_json_builder_end (b);
  }
  g_assert_true (wp_spa_json_is_object (json));
  g_assert_cmpuint (wp_spa_json_get_size (json), ==, 94);
  g_assert_cmpmem (wp_spa_json_get_data (json), wp_spa_json_get_size (json),
      "{\"key-array\":[5, 10, 15], \"key-object\":{\"key-boolean\":false, "
      "\"key-int\":8, \"key-array\":[2, 4]}}", 94);
  g_assert_cmpstr (wp_spa_json_get_data (json), ==,
      "{\"key-array\":[5, 10, 15], \"key-object\":{\"key-boolean\":false, "
      "\"key-int\":8, \"key-array\":[2, 4]}}");

  g_autoptr (WpIterator) it = wp_spa_json_new_iterator (json);
  g_assert_nonnull (it);

  {
    GValue next = G_VALUE_INIT;
    g_assert_true (wp_iterator_next (it, &next));
    WpSpaJson *j = g_value_get_boxed (&next);
    g_assert_nonnull (j);
    g_assert_true (wp_spa_json_is_string (j));
    g_autofree gchar *v = wp_spa_json_parse_string (j);
    g_assert_nonnull (v);
    g_assert_cmpstr (v, ==, "key-array");
    g_value_unset (&next);
  }

  {
    GValue next = G_VALUE_INIT;
    g_assert_true (wp_iterator_next (it, &next));
    WpSpaJson *j = g_value_get_boxed (&next);
    g_assert_nonnull (j);
    g_assert_true (wp_spa_json_is_array (j));
    g_assert_cmpuint (wp_spa_json_get_size (j), ==, 11);
    g_assert_cmpmem (wp_spa_json_get_data (j), wp_spa_json_get_size (j),
        "[5, 10, 15]", 11);

    g_autoptr (WpIterator) it2 = wp_spa_json_new_iterator (j);
    g_assert_nonnull (it2);

    {
      GValue next2 = G_VALUE_INIT;
      g_assert_true (wp_iterator_next (it2, &next2));
      WpSpaJson *j = g_value_get_boxed (&next2);
      g_assert_nonnull (j);
      g_assert_true (wp_spa_json_is_int (j));
      gint32 v = 0;
      g_assert_true (wp_spa_json_parse_int (j, &v));
      g_assert_cmpint (v, ==, 5);
      g_value_unset (&next2);
    }

    {
      GValue next2 = G_VALUE_INIT;
      g_assert_true (wp_iterator_next (it2, &next2));
      WpSpaJson *j = g_value_get_boxed (&next2);
      g_assert_nonnull (j);
      g_assert_true (wp_spa_json_is_int (j));
      gint32 v = 0;
      g_assert_true (wp_spa_json_parse_int (j, &v));
      g_assert_cmpint (v, ==, 10);
      g_value_unset (&next2);
    }

    {
      GValue next2 = G_VALUE_INIT;
      g_assert_true (wp_iterator_next (it2, &next2));
      WpSpaJson *j = g_value_get_boxed (&next2);
      g_assert_nonnull (j);
      g_assert_true (wp_spa_json_is_int (j));
      gint32 v = 0;
      g_assert_true (wp_spa_json_parse_int (j, &v));
      g_assert_cmpint (v, ==, 15);
      g_value_unset (&next2);
    }

    g_value_unset (&next);
  }

  {
    GValue next = G_VALUE_INIT;
    g_assert_true (wp_iterator_next (it, &next));
    WpSpaJson *j = g_value_get_boxed (&next);
    g_assert_nonnull (j);
    g_assert_true (wp_spa_json_is_string (j));
    g_autofree gchar *v = wp_spa_json_parse_string (j);
    g_assert_nonnull (v);
    g_assert_cmpstr (v, ==, "key-object");
    g_value_unset (&next);
  }

  {
    GValue next = G_VALUE_INIT;
    g_assert_true (wp_iterator_next (it, &next));
    WpSpaJson *j = g_value_get_boxed (&next);
    g_assert_nonnull (j);
    g_assert_true (wp_spa_json_is_object (j));
    g_assert_cmpuint (wp_spa_json_get_size (j), ==, 54);
    g_assert_cmpmem (wp_spa_json_get_data (j), wp_spa_json_get_size (j),
        "{\"key-boolean\":false, \"key-int\":8, \"key-array\":[2, 4]}", 54);

    g_autoptr (WpIterator) it2 = wp_spa_json_new_iterator (j);
    g_assert_nonnull (it2);

    {
      GValue next2 = G_VALUE_INIT;
      g_assert_true (wp_iterator_next (it2, &next2));
      WpSpaJson *j = g_value_get_boxed (&next2);
      g_assert_nonnull (j);
      g_assert_true (wp_spa_json_is_string (j));
      g_autofree gchar *v = wp_spa_json_parse_string (j);
      g_assert_nonnull (v);
      g_assert_cmpstr (v, ==, "key-boolean");
      g_value_unset (&next2);
    }

    {
      GValue next2 = G_VALUE_INIT;
      g_assert_true (wp_iterator_next (it2, &next2));
      WpSpaJson *j = g_value_get_boxed (&next2);
      g_assert_nonnull (j);
      g_assert_true (wp_spa_json_is_boolean (j));
      gboolean v = TRUE;
      g_assert_true (wp_spa_json_parse_boolean (j, &v));
      g_assert_false (v);
      g_value_unset (&next2);
    }

    {
      GValue next2 = G_VALUE_INIT;
      g_assert_true (wp_iterator_next (it2, &next2));
      WpSpaJson *j = g_value_get_boxed (&next2);
      g_assert_nonnull (j);
      g_assert_true (wp_spa_json_is_string (j));
      g_autofree gchar *v = wp_spa_json_parse_string (j);
      g_assert_nonnull (v);
      g_assert_cmpstr (v, ==, "key-int");
      g_value_unset (&next2);
    }

    {
      GValue next2 = G_VALUE_INIT;
      g_assert_true (wp_iterator_next (it2, &next2));
      WpSpaJson *j = g_value_get_boxed (&next2);
      g_assert_nonnull (j);
      g_assert_true (wp_spa_json_is_int (j));
      gint32 v = 0;
      g_assert_true (wp_spa_json_parse_int (j, &v));
      g_assert_cmpint (v, ==, 8);
      g_value_unset (&next2);
    }

    {
      GValue next2 = G_VALUE_INIT;
      g_assert_true (wp_iterator_next (it2, &next2));
      WpSpaJson *j = g_value_get_boxed (&next2);
      g_assert_nonnull (j);
      g_assert_true (wp_spa_json_is_string (j));
      g_autofree gchar *v = wp_spa_json_parse_string (j);
      g_assert_nonnull (v);
      g_assert_cmpstr (v, ==, "key-array");
      g_value_unset (&next2);
    }

    {
      GValue next2 = G_VALUE_INIT;
      g_assert_true (wp_iterator_next (it2, &next2));
      WpSpaJson *j = g_value_get_boxed (&next2);
      g_assert_nonnull (j);
      g_assert_true (wp_spa_json_is_array (array2));
      g_assert_cmpuint (wp_spa_json_get_size (array2), ==, 6);
      g_assert_cmpmem (wp_spa_json_get_data (array2),
          wp_spa_json_get_size (array2), "[2, 4]", 6);

      g_autoptr (WpIterator) it3 = wp_spa_json_new_iterator (j);
      g_assert_nonnull (it3);

      {
        GValue next3 = G_VALUE_INIT;
        g_assert_true (wp_iterator_next (it3, &next3));
        WpSpaJson *j = g_value_get_boxed (&next3);
        g_assert_nonnull (j);
        g_assert_true (wp_spa_json_is_int (j));
        gint32 v = 0;
        g_assert_true (wp_spa_json_parse_int (j, &v));
        g_assert_cmpint (v, ==, 2);
        g_value_unset (&next3);
      }

      {
        GValue next3 = G_VALUE_INIT;
        g_assert_true (wp_iterator_next (it3, &next3));
        WpSpaJson *j = g_value_get_boxed (&next3);
        g_assert_nonnull (j);
        g_assert_true (wp_spa_json_is_int (j));
        gint32 v = 0;
        g_assert_true (wp_spa_json_parse_int (j, &v));
        g_assert_cmpint (v, ==, 4);
        g_value_unset (&next3);
      }

      g_value_unset (&next2);
    }

    g_value_unset (&next);
  }

  g_assert_false (wp_iterator_next (it, NULL));
  wp_iterator_reset (it);

  {
    GValue next = G_VALUE_INIT;
    g_assert_true (wp_iterator_next (it, &next));
    WpSpaJson *j = g_value_get_boxed (&next);
    g_assert_nonnull (j);
    g_assert_true (wp_spa_json_is_string (j));
    g_autofree gchar *v = wp_spa_json_parse_string (j);
    g_assert_nonnull (v);
    g_assert_cmpstr (v, ==, "key-array");
    g_value_unset (&next);
  }
}

static void
test_spa_json_nested2 (void)
{
  const gchar json_str[] = "[[[[1], [2]], [3]], [4]]";
  g_autoptr (WpSpaJson) json = wp_spa_json_new_wrap_string (json_str);

  g_assert_true (wp_spa_json_is_array (json));
  g_assert_cmpmem (wp_spa_json_get_data (json), wp_spa_json_get_size (json),
        "[[[[1], [2]], [3]], [4]]", 24);

  {
    g_autoptr (WpSpaJsonParser) p = wp_spa_json_parser_new_array (json);
    g_assert_nonnull (p);
    g_autoptr (WpSpaJson) j0 = wp_spa_json_parser_get_json (p);
    g_assert_nonnull (j0);
    g_assert_cmpmem (wp_spa_json_get_data (j0), wp_spa_json_get_size (j0),
        "[[[1], [2]], [3]]", 17);
    g_autoptr (WpSpaJson) j1 = wp_spa_json_parser_get_json (p);
    g_assert_nonnull (j1);
    g_assert_cmpmem (wp_spa_json_get_data (j1), wp_spa_json_get_size (j1),
        "[4]", 3);
    wp_spa_json_parser_end (p);
    g_assert_false (wp_spa_json_parser_get_null (p));
  }

  g_autoptr (WpIterator) it = wp_spa_json_new_iterator (json);
  g_assert_nonnull (it);

  {
    GValue next = G_VALUE_INIT;
    g_assert_true (wp_iterator_next (it, &next));
    WpSpaJson *j = g_value_get_boxed (&next);
    g_assert_nonnull (j);
    g_assert_true (wp_spa_json_is_array (j));
    g_assert_cmpuint (wp_spa_json_get_size (j), ==, 17);
    g_assert_cmpmem (wp_spa_json_get_data (j), wp_spa_json_get_size (j),
        "[[[1], [2]], [3]]", 17);

    g_autoptr (WpIterator) it2 = wp_spa_json_new_iterator (j);
    g_assert_nonnull (it2);

    {
      GValue next2 = G_VALUE_INIT;
      g_assert_true (wp_iterator_next (it2, &next2));
      WpSpaJson *j = g_value_get_boxed (&next2);
      g_assert_nonnull (j);
      g_assert_true (wp_spa_json_is_array (j));
      g_assert_cmpuint (wp_spa_json_get_size (j), ==, 10);
      g_assert_cmpmem (wp_spa_json_get_data (j), wp_spa_json_get_size (j),
          "[[1], [2]]", 10);

      g_autoptr (WpIterator) it3 = wp_spa_json_new_iterator (j);
      g_assert_nonnull (it3);

      {
        GValue next3 = G_VALUE_INIT;
        g_assert_true (wp_iterator_next (it3, &next3));
        WpSpaJson *j = g_value_get_boxed (&next3);
        g_assert_nonnull (j);
        g_assert_true (wp_spa_json_is_array (j));
        g_assert_cmpuint (wp_spa_json_get_size (j), ==, 3);
        g_assert_cmpmem (wp_spa_json_get_data (j), wp_spa_json_get_size (j),
            "[1]", 3);

        g_autoptr (WpIterator) it4 = wp_spa_json_new_iterator (j);
        g_assert_nonnull (it4);

        {
          GValue next4 = G_VALUE_INIT;
          g_assert_true (wp_iterator_next (it4, &next4));
          WpSpaJson *j = g_value_get_boxed (&next4);
          g_assert_nonnull (j);
          g_assert_true (wp_spa_json_is_int (j));
          g_assert_cmpmem (wp_spa_json_get_data (j), wp_spa_json_get_size (j),
              "1", 1);
          g_value_unset (&next4);
        }

        g_value_unset (&next3);
      }

      {
        GValue next3 = G_VALUE_INIT;
        g_assert_true (wp_iterator_next (it3, &next3));
        WpSpaJson *j = g_value_get_boxed (&next3);
        g_assert_nonnull (j);
        g_assert_true (wp_spa_json_is_array (j));
        g_assert_cmpuint (wp_spa_json_get_size (j), ==, 3);
        g_assert_cmpmem (wp_spa_json_get_data (j), wp_spa_json_get_size (j),
            "[2]", 3);

        g_autoptr (WpIterator) it4 = wp_spa_json_new_iterator (j);
        g_assert_nonnull (it4);

        {
          GValue next4 = G_VALUE_INIT;
          g_assert_true (wp_iterator_next (it4, &next4));
          WpSpaJson *j = g_value_get_boxed (&next4);
          g_assert_nonnull (j);
          g_assert_true (wp_spa_json_is_int (j));
          g_assert_cmpmem (wp_spa_json_get_data (j), wp_spa_json_get_size (j),
              "2", 1);
          g_value_unset (&next4);
        }

        g_value_unset (&next3);
      }

      g_value_unset (&next2);
    }

    {
      GValue next2 = G_VALUE_INIT;
      g_assert_true (wp_iterator_next (it2, &next2));
      WpSpaJson *j = g_value_get_boxed (&next2);
      g_assert_nonnull (j);
      g_assert_true (wp_spa_json_is_array (j));
      g_assert_cmpuint (wp_spa_json_get_size (j), ==, 3);
      g_assert_cmpmem (wp_spa_json_get_data (j), wp_spa_json_get_size (j),
          "[3]", 3);

      g_autoptr (WpIterator) it3 = wp_spa_json_new_iterator (j);
      g_assert_nonnull (it3);

      {
        GValue next3 = G_VALUE_INIT;
        g_assert_true (wp_iterator_next (it3, &next3));
        WpSpaJson *j = g_value_get_boxed (&next3);
        g_assert_nonnull (j);
        g_assert_true (wp_spa_json_is_int (j));
        g_assert_cmpmem (wp_spa_json_get_data (j), wp_spa_json_get_size (j),
            "3", 1);
        g_value_unset (&next3);
      }

      g_value_unset (&next2);
    }

    g_value_unset (&next);
  }

  {
    GValue next = G_VALUE_INIT;
    g_assert_true (wp_iterator_next (it, &next));
    WpSpaJson *j = g_value_get_boxed (&next);
    g_assert_nonnull (j);
    g_assert_true (wp_spa_json_is_array (j));
    g_assert_cmpuint (wp_spa_json_get_size (j), ==, 3);
    g_assert_cmpmem (wp_spa_json_get_data (j), wp_spa_json_get_size (j),
        "[4]", 3);

    g_autoptr (WpIterator) it2 = wp_spa_json_new_iterator (j);
    g_assert_nonnull (it2);

    {
      GValue next2 = G_VALUE_INIT;
      g_assert_true (wp_iterator_next (it2, &next2));
      WpSpaJson *j = g_value_get_boxed (&next2);
      g_assert_nonnull (j);
      g_assert_true (wp_spa_json_is_int (j));
      g_assert_cmpmem (wp_spa_json_get_data (j), wp_spa_json_get_size (j),
          "4", 1);
      g_value_unset (&next2);
    }

    g_value_unset (&next);
  }
}

static void
test_spa_json_nested3 (void)
{
  const gchar json_str[] =
      "{ test-setting-json3: { key1: \"value\", key2: 2, key3: true } }";
  g_autoptr (WpSpaJson) json = wp_spa_json_new_wrap_string (json_str);
  g_assert_nonnull (json);
  g_assert_true (wp_spa_json_is_object (json));

  g_autoptr (WpIterator) it = wp_spa_json_new_iterator (json);
  g_assert_nonnull (it);

  {
    GValue next = G_VALUE_INIT;
    g_assert_true (wp_iterator_next (it, &next));
    WpSpaJson *j = g_value_get_boxed (&next);
    g_assert_nonnull (j);
    g_autofree gchar *v = wp_spa_json_parse_string (j);
    g_assert_cmpstr (v, ==, "test-setting-json3");
    g_value_unset (&next);
  }

  {
    GValue next = G_VALUE_INIT;
    g_assert_true (wp_iterator_next (it, &next));
    WpSpaJson *j = g_value_get_boxed (&next);
    g_assert_nonnull (j);
    g_assert_true (wp_spa_json_is_object (j));
    g_autofree gchar *v = wp_spa_json_to_string (j);
    g_assert_cmpstr (v, ==, "{ key1: \"value\", key2: 2, key3: true }");
    g_value_unset (&next);
  }
}

static void
test_spa_json_ownership (void)
{
  g_autoptr (WpSpaJson) json = NULL;

  {
    const gchar json_str[] = "{\"name\":\"John\", \"age\":30, \"car\":null}";
    json = wp_spa_json_new_wrap_string (json_str);
    g_assert_nonnull (json);

    g_assert_false (wp_spa_json_is_unique_owner (json));

    g_assert_true (wp_spa_json_is_object (json));
    g_assert_cmpmem (wp_spa_json_get_data (json), wp_spa_json_get_size (json),
        "{\"name\":\"John\", \"age\":30, \"car\":null}", 37);

    json = wp_spa_json_ensure_unique_owner (json);
    g_assert_nonnull (json);
    g_assert_true (wp_spa_json_is_unique_owner (json));
  }

  g_assert_true (wp_spa_json_is_object (json));
  g_assert_cmpmem (wp_spa_json_get_data (json), wp_spa_json_get_size (json),
      "{\"name\":\"John\", \"age\":30, \"car\":null}", 37);

  g_autoptr (WpIterator) it = wp_spa_json_new_iterator (json);
  g_assert_nonnull (it);

  {
    GValue next = G_VALUE_INIT;
    g_assert_true (wp_iterator_next (it, &next));
    WpSpaJson *j = g_value_get_boxed (&next);
    g_assert_nonnull (j);
    g_assert_true (wp_spa_json_is_string (j));
    g_autofree gchar *v = wp_spa_json_parse_string (j);
    g_assert_nonnull (v);
    g_assert_cmpstr (v, ==, "name");
    g_value_unset (&next);
  }

  {
    GValue next = G_VALUE_INIT;
    g_assert_true (wp_iterator_next (it, &next));
    WpSpaJson *j = g_value_get_boxed (&next);
    g_assert_nonnull (j);
    g_assert_true (wp_spa_json_is_string (j));
    g_autofree gchar *v = wp_spa_json_parse_string (j);
    g_assert_nonnull (v);
    g_assert_cmpstr (v, ==, "John");
    g_value_unset (&next);
  }

  {
    GValue next = G_VALUE_INIT;
    g_assert_true (wp_iterator_next (it, &next));
    WpSpaJson *j = g_value_get_boxed (&next);
    g_assert_nonnull (j);
    g_assert_true (wp_spa_json_is_string (j));
    g_autofree gchar *v = wp_spa_json_parse_string (j);
    g_assert_nonnull (v);
    g_assert_cmpstr (v, ==, "age");
    g_value_unset (&next);
  }

  {
    GValue next = G_VALUE_INIT;
    g_assert_true (wp_iterator_next (it, &next));
    WpSpaJson *j = g_value_get_boxed (&next);
    g_assert_nonnull (j);
    g_assert_true (wp_spa_json_is_int (j));
    gint32 v = 0;
    g_assert_true (wp_spa_json_parse_int (j, &v));
    g_assert_cmpint (v, ==, 30);
    g_value_unset (&next);
  }

  {
    GValue next = G_VALUE_INIT;
    g_assert_true (wp_iterator_next (it, &next));
    WpSpaJson *j = g_value_get_boxed (&next);
    g_assert_nonnull (j);
    g_assert_true (wp_spa_json_is_string (j));
    g_autofree gchar *v = wp_spa_json_parse_string (j);
    g_assert_nonnull (v);
    g_assert_cmpstr (v, ==, "car");
    g_value_unset (&next);
  }

  {
    GValue next = G_VALUE_INIT;
    g_assert_true (wp_iterator_next (it, &next));
    WpSpaJson *j = g_value_get_boxed (&next);
    g_assert_nonnull (j);
    g_assert_true (wp_spa_json_is_null (j));
    g_value_unset (&next);
  }
}

static void
test_spa_json_spa_format (void)
{
  g_autoptr (WpSpaJson) json = NULL;

  const gchar json_str[] = "{ name = John age:30, \"car\" null }";
  json = wp_spa_json_new_wrap_string (json_str);
  g_assert_nonnull (json);

  g_assert_true (wp_spa_json_is_object (json));

  g_autoptr (WpIterator) it = wp_spa_json_new_iterator (json);
  g_assert_nonnull (it);

  {
    GValue next = G_VALUE_INIT;
    g_assert_true (wp_iterator_next (it, &next));
    WpSpaJson *j = g_value_get_boxed (&next);
    g_assert_nonnull (j);
    g_assert_false (wp_spa_json_is_string (j));  // FALSE because no quotes
    g_autofree gchar *v = wp_spa_json_parse_string (j);
    g_assert_nonnull (v);
    g_assert_cmpstr (v, ==, "name");
    g_value_unset (&next);
  }

  {
    GValue next = G_VALUE_INIT;
    g_assert_true (wp_iterator_next (it, &next));
    WpSpaJson *j = g_value_get_boxed (&next);
    g_assert_nonnull (j);
    g_assert_false (wp_spa_json_is_string (j));  // FALSE because no quotes
    g_autofree gchar *v = wp_spa_json_parse_string (j);
    g_assert_nonnull (v);
    g_assert_cmpstr (v, ==, "John");
    g_value_unset (&next);
  }

  {
    GValue next = G_VALUE_INIT;
    g_assert_true (wp_iterator_next (it, &next));
    WpSpaJson *j = g_value_get_boxed (&next);
    g_assert_nonnull (j);
    g_assert_false (wp_spa_json_is_string (j));  // FALSE because no quotes
    g_autofree gchar *v = wp_spa_json_parse_string (j);
    g_assert_nonnull (v);
    g_assert_cmpstr (v, ==, "age");
    g_value_unset (&next);
  }

  {
    GValue next = G_VALUE_INIT;
    g_assert_true (wp_iterator_next (it, &next));
    WpSpaJson *j = g_value_get_boxed (&next);
    g_assert_nonnull (j);
    g_assert_true (wp_spa_json_is_int (j));
    gint32 v = 0;
    g_assert_true (wp_spa_json_parse_int (j, &v));
    g_assert_cmpint (v, ==, 30);
    g_value_unset (&next);
  }

  {
    GValue next = G_VALUE_INIT;
    g_assert_true (wp_iterator_next (it, &next));
    WpSpaJson *j = g_value_get_boxed (&next);
    g_assert_nonnull (j);
    g_assert_true (wp_spa_json_is_string (j));
    g_autofree gchar *v = wp_spa_json_parse_string (j);
    g_assert_nonnull (v);
    g_assert_cmpstr (v, ==, "car");
    g_value_unset (&next);
  }

  {
    GValue next = G_VALUE_INIT;
    g_assert_true (wp_iterator_next (it, &next));
    WpSpaJson *j = g_value_get_boxed (&next);
    g_assert_nonnull (j);
    g_assert_true (wp_spa_json_is_null (j));
    g_value_unset (&next);
  }
}

static void
test_spa_json_to_string (void)
{
  const gchar json_str[] = "[{\"key0\":\"val0\"}, {\"key1\":\"val1\"}]";
  g_autoptr (WpSpaJson) json = wp_spa_json_new_wrap_string (json_str);
  g_assert_nonnull (json);

  {
    g_autofree gchar *str = wp_spa_json_to_string (json);
    g_assert_cmpstr (str, ==, wp_spa_json_get_data (json));
    g_assert_cmpstr (str, ==, json_str);
  }

  g_autoptr (WpIterator) it = wp_spa_json_new_iterator (json);
  g_assert_nonnull (it);

  {
    GValue next = G_VALUE_INIT;
    g_assert_true (wp_iterator_next (it, &next));
    WpSpaJson *o = g_value_get_boxed (&next);
    g_assert_nonnull (o);
    g_assert_true (wp_spa_json_is_object (o));
    g_autofree gchar *str = wp_spa_json_to_string (o);
    g_assert_cmpstr (str, ==, "{\"key0\":\"val0\"}");
    g_assert_cmpstr (str, !=, wp_spa_json_get_data (o));

    g_autoptr (WpSpaJsonBuilder) b = wp_spa_json_builder_new_array ();
    wp_spa_json_builder_add_json (b, o);
    g_autoptr (WpSpaJson) json2 = wp_spa_json_builder_end (b);
    g_autofree gchar *str2 = wp_spa_json_to_string (json2);
    g_assert_cmpstr (str2, ==, wp_spa_json_get_data (json2));
    g_assert_cmpstr (str2, ==, "[{\"key0\":\"val0\"}]");

    g_value_unset (&next);
  }

  {
    GValue next = G_VALUE_INIT;
    g_assert_true (wp_iterator_next (it, &next));
    WpSpaJson *o = g_value_get_boxed (&next);
    g_assert_nonnull (o);
    g_assert_true (wp_spa_json_is_object (o));
    g_autofree gchar *str = wp_spa_json_to_string (o);
    g_assert_cmpstr (str, ==, "{\"key1\":\"val1\"}");
    g_assert_cmpstr (str, !=, wp_spa_json_get_data (o));

    g_autoptr (WpSpaJsonBuilder) b = wp_spa_json_builder_new_array ();
    wp_spa_json_builder_add_json (b, o);
    g_autoptr (WpSpaJson) json2 = wp_spa_json_builder_end (b);
    g_autofree gchar *str2 = wp_spa_json_to_string (json2);
    g_assert_cmpstr (str2, ==, wp_spa_json_get_data (json2));
    g_assert_cmpstr (str2, ==, "[{\"key1\":\"val1\"}]");

    g_value_unset (&next);
  }
}

static void
test_spa_json_undefined_parser (void)
{
  const gchar json_str[] = "key0 = val0, key.array = [ val1 val2 ], "
      "key.object = { key-boolean = false, key-int = 8, key-array = [ 2 4 ] }";
  g_autoptr (WpSpaJson) json = wp_spa_json_new_wrap_string (json_str);
  g_assert_nonnull (json);

  g_assert_false (wp_spa_json_is_container (json));

  g_autoptr (WpSpaJsonParser) p = wp_spa_json_parser_new_undefined (json);
  g_assert_nonnull (p);

  {
    g_autofree gchar *k = wp_spa_json_parser_get_string (p);
    g_assert_cmpstr (k, ==, "key0");
  }
  {
    g_autofree gchar *v = wp_spa_json_parser_get_string (p);
    g_assert_cmpstr (v, ==, "val0");
  }
  {
    g_autofree gchar *k = wp_spa_json_parser_get_string (p);
    g_assert_cmpstr (k, ==, "key.array");
  }
  {
    g_autoptr (WpSpaJson) v = wp_spa_json_parser_get_json (p);
    g_autofree gchar *str = wp_spa_json_to_string (v);
    g_assert_cmpstr (str, ==, "[ val1 val2 ]");
    g_assert_true (wp_spa_json_is_array (v));
  }
  {
    g_autofree gchar *k = wp_spa_json_parser_get_string (p);
    g_assert_cmpstr (k, ==, "key.object");
  }
  {
    g_autoptr (WpSpaJson) v = wp_spa_json_parser_get_json (p);
    g_autofree gchar *str = wp_spa_json_to_string (v);
    g_assert_cmpstr (str, ==, "{ key-boolean = false, key-int = 8, key-array = [ 2 4 ] }");
    g_assert_true (wp_spa_json_is_object (v));
  }
  {
    g_autofree gchar *k = wp_spa_json_parser_get_string (p);
    g_assert_null (k);
  }
  {
    g_autofree gchar *k = wp_spa_json_parser_get_string (p);
    g_assert_null (k);
  }
}

int
main (int argc, char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  g_log_set_writer_func (wp_log_writer_default, NULL, NULL);

  g_test_add_func ("/wp/spa-json/basic", test_spa_json_basic);
  g_test_add_func ("/wp/spa-json/array-builder-parser-iterator",
      test_spa_json_array_builder_parser_iterator);
  g_test_add_func ("/wp/spa-json/object-builder-parser-iterator",
      test_spa_json_object_builder_parser_iterator);
  g_test_add_func ("/wp/spa-json/nested", test_spa_json_nested);
  g_test_add_func ("/wp/spa-json/nested2", test_spa_json_nested2);
  g_test_add_func ("/wp/spa-json/nested3", test_spa_json_nested3);
  g_test_add_func ("/wp/spa-json/ownership", test_spa_json_ownership);
  g_test_add_func ("/wp/spa-json/spa-format", test_spa_json_spa_format);
  g_test_add_func ("/wp/spa-json/to-string", test_spa_json_to_string);
  g_test_add_func ("/wp/spa-json/undefined-parser",
      test_spa_json_undefined_parser);

  return g_test_run ();
}
