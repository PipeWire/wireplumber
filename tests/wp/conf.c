/* WirePlumber
 *
 * Copyright © 2022 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "../common/test-log.h"

typedef struct {
  WpConf *conf;
} TestConfFixture;

static void
test_conf_setup (TestConfFixture *self, gconstpointer user_data)
{
  g_autoptr (GError) error = NULL;
  g_autofree gchar *file =
      g_strdup_printf ("%s/conf/wireplumber.conf", g_getenv ("G_TEST_SRCDIR"));
  self->conf = wp_conf_new_open (file, NULL, &error);
  g_assert_no_error (error);
  g_assert_nonnull (self->conf);
}

static void
test_conf_teardown (TestConfFixture *self, gconstpointer user_data)
{
  g_clear_object (&self->conf);
}

static void
test_conf_basic (TestConfFixture *f, gconstpointer data)
{
  g_assert_nonnull (f->conf);

  /* Boolean Array */
  {
    g_autoptr (WpSpaJson) s = wp_conf_get_section (f->conf,
        "wireplumber.section.array.boolean");
    g_assert_nonnull (s);
    g_assert_true (wp_spa_json_is_array (s));
    gboolean v1 = FALSE, v2 = TRUE;
    g_assert_true (wp_spa_json_parse_array (s, "b", &v1, "b", &v2, NULL));
    g_assert_true (v1);
    g_assert_false (v2);
  }

  /* Int Array */
  {
    g_autoptr (WpSpaJson) s = wp_conf_get_section (f->conf,
        "wireplumber.section.array.int");
    g_assert_nonnull (s);
    g_assert_true (wp_spa_json_is_array (s));
    gint v1 = 0, v2 = 0, v3 = 0;
    g_assert_true (wp_spa_json_parse_array (s, "i", &v1, "i", &v2, "i", &v3,
        NULL));
    g_assert_cmpint (v1, ==, 1);
    g_assert_cmpint (v2, ==, 2);
    g_assert_cmpint (v3, ==, 3);
  }

  /* Float Array */
  {
    g_autoptr (WpSpaJson) s = wp_conf_get_section (f->conf,
        "wireplumber.section.array.float");
    g_assert_nonnull (s);
    g_assert_true (wp_spa_json_is_array (s));
    float v1 = 0.0, v2 = 0.0, v3 = 0.0;
    g_assert_true (wp_spa_json_parse_array (s, "f", &v1, "f", &v2, "f", &v3,
        NULL));
    g_assert_cmpfloat_with_epsilon (v1, 1.11, 0.001);
    g_assert_cmpfloat_with_epsilon (v2, 2.22, 0.001);
    g_assert_cmpfloat_with_epsilon (v3, 3.33, 0.001);
  }

  /* String Array */
  {
    g_autoptr (WpSpaJson) s = wp_conf_get_section (f->conf,
        "wireplumber.section.array.string");
    g_assert_nonnull (s);
    g_assert_true (wp_spa_json_is_array (s));
    g_autofree gchar *v1 = NULL, *v2 = NULL;
    g_assert_true (wp_spa_json_parse_array (s, "s", &v1, "s", &v2, NULL));
    g_assert_nonnull (v1);
    g_assert_nonnull (v2);
    g_assert_cmpstr (v1, ==, "foo");
    g_assert_cmpstr (v2, ==, "bar");
  }

  /* Array Array */
  {
    g_autoptr (WpSpaJson) s = wp_conf_get_section (f->conf,
        "wireplumber.section.array.array");
    g_assert_nonnull (s);
    g_assert_true (wp_spa_json_is_array (s));
    g_autoptr (WpSpaJson) v1 = NULL;
    g_autoptr (WpSpaJson) v2 = NULL;
    g_assert_true (wp_spa_json_parse_array (s, "J", &v1, "J", &v2, NULL));
    g_assert_nonnull (v1);
    g_assert_nonnull (v2);
    g_assert_true (wp_spa_json_is_array (v1));
    g_assert_true (wp_spa_json_is_array (v2));
    gboolean v3 = FALSE, v4 = TRUE;
    g_assert_true (wp_spa_json_parse_array (v1, "b", &v3, NULL));
    g_assert_true (v3);
    g_assert_true (wp_spa_json_parse_array (v2, "b", &v4, NULL));
    g_assert_false (v4);
  }

  /* Object Array */
  {
    g_autoptr (WpSpaJson) s = wp_conf_get_section (f->conf,
        "wireplumber.section.array.object");
    g_assert_nonnull (s);
    g_assert_true (wp_spa_json_is_array (s));
    g_autoptr (WpSpaJson) v1 = NULL;
    g_autoptr (WpSpaJson) v2 = NULL;
    g_assert_true (wp_spa_json_parse_array (s, "J", &v1, "J", &v2, NULL));
    g_assert_nonnull (v1);
    g_assert_nonnull (v2);
    g_assert_true (wp_spa_json_is_object (v1));
    g_assert_true (wp_spa_json_is_object (v2));
    g_autofree gchar *v3 = NULL;
    gint v4 = 0;
    g_assert_true (wp_spa_json_object_get (v1, "key1", "s", &v3, NULL));
    g_assert_cmpstr (v3, ==, "foo");
    g_assert_true (wp_spa_json_object_get (v2, "key2", "i", &v4, NULL));
    g_assert_cmpint (v4, ==, 4);
  }

  /* Object */
  {
    g_autoptr (WpSpaJson) s = wp_conf_get_section (f->conf,
        "wireplumber.section.object");
    g_assert_nonnull (s);
    g_assert_true (wp_spa_json_is_object (s));
    gboolean v1 = FALSE;
    gint v2 = 0;
    float v3 = 0.0;
    g_autofree gchar *v4 = NULL;
    g_autoptr (WpSpaJson) v5 = NULL;
    g_autoptr (WpSpaJson) v6 = NULL;
    g_assert_true (wp_spa_json_object_get (s,
        "key.boolean", "b", &v1,
        "key.int", "i", &v2,
        "key.float", "f", &v3,
        "key.string", "s", &v4,
        "key.array", "J", &v5,
        "key.object", "J", &v6,
        NULL));
    g_assert_true (v1);
    g_assert_cmpint (v2, ==, -1);
    g_assert_cmpfloat_with_epsilon (v3, 3.14, 0.001);
    g_assert_cmpstr (v4, ==, "wireplumber");
    g_assert_true (wp_spa_json_is_array (v5));
    g_autofree gchar *v7 = NULL, *v8 = NULL;
    g_assert_true (wp_spa_json_parse_array (v5, "s", &v7, "s", &v8, NULL));
    g_assert_cmpstr (v7, ==, "an");
    g_assert_cmpstr (v8, ==, "array");
    g_assert_true (wp_spa_json_is_object (v6));
    gboolean v9 = TRUE;
    g_assert_true (wp_spa_json_object_get (v6,
        "key.nested.boolean", "b", &v9,
        NULL));
    g_assert_false (v9);
  }
}

static void
test_conf_merge (TestConfFixture *f, gconstpointer data)
{
  g_assert_nonnull (f->conf);

  /* Boolean Array */
  {
    g_autoptr (WpSpaJson) s = wp_conf_get_section (f->conf,
        "wireplumber.section-merged.array.boolean");
    g_assert_nonnull (s);
    g_assert_true (wp_spa_json_is_array (s));
    gboolean v1 = TRUE, v2 = FALSE;
    g_assert_true (wp_spa_json_parse_array (s, "b", &v1, "b", &v2, NULL));
    g_assert_false (v1);
    g_assert_true (v2);
  }

  /* Int Array */
  {
    g_autoptr (WpSpaJson) s = wp_conf_get_section (f->conf,
        "wireplumber.section-merged.array.int");
    g_assert_nonnull (s);
    g_assert_true (wp_spa_json_is_array (s));
    gint v1 = 0, v2 = 0;
    g_assert_true (wp_spa_json_parse_array (s, "i", &v1, "i", &v2, NULL));
    g_assert_cmpint (v1, ==, 4);
    g_assert_cmpint (v2, ==, 5);
  }

  /* Float Array */
  {
    g_autoptr (WpSpaJson) s = wp_conf_get_section (f->conf,
        "wireplumber.section-merged.array.float");
    g_assert_nonnull (s);
    g_assert_true (wp_spa_json_is_array (s));
    float v1 = 0.0, v2 = 0.0;
    g_assert_true (wp_spa_json_parse_array (s, "f", &v1, "f", &v2, NULL));
    g_assert_cmpfloat_with_epsilon (v1, 4.44, 0.001);
    g_assert_cmpfloat_with_epsilon (v2, 5.55, 0.001);
  }

  /* String Array */
  {
    g_autoptr (WpSpaJson) s = wp_conf_get_section (f->conf,
        "wireplumber.section-merged.array.string");
    g_assert_nonnull (s);
    g_assert_true (wp_spa_json_is_array (s));
    g_autofree gchar *v1 = NULL, *v2 = NULL;
    g_assert_true (wp_spa_json_parse_array (s, "s", &v1, "s", &v2, NULL));
    g_assert_nonnull (v1);
    g_assert_nonnull (v2);
    g_assert_cmpstr (v1, ==, "first");
    g_assert_cmpstr (v2, ==, "second");
  }

  /* Array Array */
  {
    g_autoptr (WpSpaJson) s = wp_conf_get_section (f->conf,
        "wireplumber.section-merged.array.array");
    g_assert_nonnull (s);
    g_assert_true (wp_spa_json_is_array (s));
    g_autoptr (WpSpaJson) v1 = NULL;
    g_autoptr (WpSpaJson) v2 = NULL;
    g_assert_true (wp_spa_json_parse_array (s, "J", &v1, "J", &v2, NULL));
    g_assert_nonnull (v1);
    g_assert_nonnull (v2);
    g_assert_true (wp_spa_json_is_array (v1));
    g_assert_true (wp_spa_json_is_array (v2));
    gboolean v3 = FALSE, v4 = TRUE;
    g_assert_true (wp_spa_json_parse_array (v1, "b", &v3, NULL));
    g_assert_true (v3);
    g_assert_true (wp_spa_json_parse_array (v2, "b", &v4, NULL));
    g_assert_false (v4);
  }

  /* Object Array */
  {
    g_autoptr (WpSpaJson) s = wp_conf_get_section (f->conf,
        "wireplumber.section-merged.array.object");
    g_assert_nonnull (s);
    g_assert_true (wp_spa_json_is_array (s));
    g_autoptr (WpSpaJson) v1 = NULL;
    g_autoptr (WpSpaJson) v2 = NULL;
    g_assert_true (wp_spa_json_parse_array (s, "J", &v1, "J", &v2, NULL));
    g_assert_nonnull (v1);
    g_assert_nonnull (v2);
    g_assert_true (wp_spa_json_is_object (v1));
    g_assert_true (wp_spa_json_is_object (v2));
    g_autofree gchar *v3 = NULL;
    gint v4 = 0;
    g_assert_true (wp_spa_json_object_get (v1, "key1", "s", &v3, NULL));
    g_assert_cmpstr (v3, ==, "foo");
    g_assert_true (wp_spa_json_object_get (v2, "key2", "i", &v4, NULL));
    g_assert_cmpint (v4, ==, 4);
  }

  /* Object */
  {
    g_autoptr (WpSpaJson) s = wp_conf_get_section (f->conf,
        "wireplumber.section-merged.object");
    g_assert_nonnull (s);
    g_assert_true (wp_spa_json_is_object (s));
    gboolean v1 = FALSE;
    gint v2 = 0;
    float v3 = 0.0;
    g_autofree gchar *v4 = NULL;
    g_autoptr (WpSpaJson) v5 = NULL;
    g_autoptr (WpSpaJson) v6 = NULL;
    g_assert_true (wp_spa_json_object_get (s,
        "key.boolean", "b", &v1,
        "key.int", "i", &v2,
        "key.float", "f", &v3,
        "key.string", "s", &v4,
        "key.array", "J", &v5,
        "key.object", "J", &v6,
        NULL));
    g_assert_false (v1);
    g_assert_cmpint (v2, ==, 6);
    g_assert_cmpfloat_with_epsilon (v3, 6.66, 0.001);
    g_assert_cmpstr (v4, ==, "merged");
    g_assert_true (wp_spa_json_is_array (v5));
    g_autofree gchar *v7 = NULL, *v8 = NULL;
    g_assert_true (wp_spa_json_parse_array (v5, "s", &v7, "s", &v8, NULL));
    g_assert_cmpstr (v7, ==, "an");
    g_assert_cmpstr (v8, ==, "array");
    g_assert_true (wp_spa_json_is_object (v6));
    gboolean v9 = TRUE;
    g_assert_true (wp_spa_json_object_get (v6,
        "key.nested.boolean", "b", &v9,
        NULL));
    g_assert_false (v9);
  }
}

static void
test_conf_merge_nested (TestConfFixture *f, gconstpointer data)
{
  g_assert_nonnull (f->conf);

  g_autoptr (WpSpaJson) s = wp_conf_get_section (f->conf,
      "wireplumber.section-nested-merged");
  g_assert_nonnull (s);
  g_assert_true (wp_spa_json_is_object (s));

  /* Make sure both keys exist in the nested object  */
  {
    g_autoptr (WpSpaJson) v1 = NULL;
    g_assert_true (wp_spa_json_object_get (s, "nested-object", "J", &v1, NULL));
    g_assert_nonnull (v1);
    g_assert_true (wp_spa_json_is_object (v1));
    gboolean v2 = FALSE;
    g_assert_true (wp_spa_json_object_get (v1, "key1", "b", &v2, NULL));
    gint v3 = 0;
    g_assert_true (wp_spa_json_object_get (v1, "key2", "i", &v3, NULL));
    g_assert_cmpint (v3, ==, 3);
  }

  /* Make sure array has all its elements */
  {
    g_autoptr (WpSpaJson) v1 = NULL;
    g_assert_true (wp_spa_json_object_get (s, "nested-array", "J", &v1, NULL));
    g_assert_nonnull (v1);
    g_assert_true (wp_spa_json_is_array (v1));
    gint v2 = 0, v3 = 0, v4 = 0, v5 = 0;
    g_assert_true (wp_spa_json_parse_array (v1,
        "i", &v2, "i", &v3, "i", &v4, "i", &v5, NULL));
    g_assert_cmpint (v2, ==, 1);
    g_assert_cmpint (v3, ==, 2);
    g_assert_cmpint (v4, ==, 3);
    g_assert_cmpint (v5, ==, 4);
  }
}

static void
test_conf_override (TestConfFixture *f, gconstpointer data)
{
  g_assert_nonnull (f->conf);

  g_autoptr (WpSpaJson) s = wp_conf_get_section (f->conf,
      "wireplumber.section-override");
  g_assert_nonnull (s);
  g_assert_true (wp_spa_json_is_object (s));

  /* Make sure key1 does not exist because it was overridden */
  gboolean v1 = FALSE;
  g_assert_false (wp_spa_json_object_get (s, "key1", "b", &v1, NULL));

  /* Make sure key2 exists */
  gint v2 = 0;
  g_assert_true (wp_spa_json_object_get (s, "key2", "i", &v2, NULL));
  g_assert_cmpint (v2, ==, 5);
}

static void
test_conf_override_nested (TestConfFixture *f, gconstpointer data)
{
  g_assert_nonnull (f->conf);

  g_autoptr (WpSpaJson) s = wp_conf_get_section (f->conf,
      "wireplumber.section-nested-override");
  g_assert_nonnull (s);
  g_assert_true (wp_spa_json_is_object (s));

  g_autoptr (WpSpaJson) v1 = NULL;
  g_assert_true (wp_spa_json_object_get (s, "nested-object", "J", &v1, NULL));
  g_assert_nonnull (v1);
  g_assert_true (wp_spa_json_is_object (v1));

  /* Make sure key1 does not exist because it was overridden */
  gboolean v2 = FALSE;
  g_assert_false (wp_spa_json_object_get (v1, "key1", "b", &v2, NULL));

  /* Make sure key2 exists */
  gint v3 = 0;
  g_assert_true (wp_spa_json_object_get (v1, "key2", "i", &v3, NULL));
  g_assert_cmpint (v3, ==, 3);
}

static void
test_conf_as_section (TestConfFixture *f, gconstpointer data)
{
  g_autoptr (GError) error = NULL;
  g_autofree gchar *file =
      g_strdup_printf ("%s/conf/section.conf", g_getenv ("G_TEST_SRCDIR"));
  g_autoptr (WpProperties) props =
      wp_properties_new ("as-section", "test", NULL);
  f->conf = wp_conf_new_open (file, g_steal_pointer (&props), &error);
  g_assert_no_error (error);
  g_assert_nonnull (f->conf);

  g_autoptr (WpSpaJson) s = wp_conf_get_section (f->conf, "test");
  g_assert_nonnull (s);
  g_assert_true (wp_spa_json_is_object (s));

  g_autofree gchar *v = NULL;
  g_assert_true (wp_spa_json_object_get (s,
      "some", "s", &v,
      NULL));
  g_assert_cmpstr (v, ==, "json");

  g_clear_object (&f->conf);
}

static void
test_conf_sections_iterator (TestConfFixture *f, gconstpointer data)
{
  g_autoptr (WpSpaJson) merged = NULL;
  g_autoptr (WpIterator) it = NULL;
  g_autoptr (GHashTable) names = NULL;
  g_auto (GValue) item = G_VALUE_INIT;
  guint matching_sections = 0;
  guint override_sections = 0;

  /* Merging first must not alter the original definitions */
  merged = wp_conf_get_section (f->conf, "wireplumber.section-merged.object");
  g_assert_nonnull (merged);

  it = wp_conf_new_sections_iterator (f->conf, WP_CONF_SECTION_MODE_ALL);
  g_assert_nonnull (it);
  for (; wp_iterator_next (it, &item); g_value_unset (&item)) {
    WpSpaJson *json = g_value_get_boxed (&item);
    g_autofree gchar *name = NULL;
    g_autofree gchar *location = NULL;
    g_autoptr (WpSpaJson) value = NULL;

    g_assert_true (wp_spa_json_object_get (json,
        "section.name", "s", &name,
        "section.location", "s", &location,
        "section.value", "J", &value,
        NULL));
    g_assert_nonnull (location);
    g_assert_nonnull (value);

    if (g_str_equal (name, "wireplumber.section-merged.object"))
      matching_sections++;
    if (g_str_equal (name, "override.wireplumber.section-override"))
      override_sections++;
  }
  g_assert_cmpuint (matching_sections, ==, 2);
  g_assert_cmpuint (override_sections, ==, 1);

  g_clear_pointer (&it, wp_iterator_unref);
  names = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  it = wp_conf_new_sections_iterator (f->conf, WP_CONF_SECTION_MODE_MERGED);
  g_assert_nonnull (it);
  override_sections = 0;
  for (; wp_iterator_next (it, &item); g_value_unset (&item)) {
    WpSpaJson *json = g_value_get_boxed (&item);
    g_autofree gchar *name = NULL;

    g_assert_true (wp_spa_json_object_get (json,
        "section.name", "s", &name, NULL));
    g_assert_false (g_str_has_prefix (name, "override."));
    g_assert_false (g_hash_table_contains (names, name));
    g_hash_table_add (names, g_strdup (name));

    if (g_str_equal (name, "wireplumber.section-override"))
      override_sections++;
  }
  g_assert_cmpuint (override_sections, ==, 1);

  /* Iterator items must remain valid after the configuration is closed */
  g_clear_pointer (&it, wp_iterator_unref);
  it = wp_conf_new_sections_iterator (f->conf, WP_CONF_SECTION_MODE_ALL);
  wp_conf_close (f->conf);
  g_assert_true (wp_iterator_next (it, &item));
  WpSpaJson *json = g_value_get_boxed (&item);
  g_autofree gchar *name = NULL;
  g_autofree gchar *location = NULL;
  g_autoptr (WpSpaJson) value = NULL;
  g_assert_true (wp_spa_json_object_get (json,
      "section.name", "s", &name,
      "section.location", "s", &location,
      "section.value", "J", &value,
      NULL));
  g_assert_nonnull (name);
  g_assert_nonnull (location);
  g_assert_nonnull (value);
  g_value_unset (&item);
}

static void
test_conf_locations_iterator (TestConfFixture *f, gconstpointer data)
{
  g_autoptr (WpIterator) it = wp_conf_new_locations_iterator (f->conf);
  g_auto (GValue) item = G_VALUE_INIT;
  const gchar *expected_basenames[] = {
    "wireplumber.conf", "empty.conf", "merge.conf", "merge2.conf",
  };
  guint n_locations = 0;

  g_assert_nonnull (it);
  for (; wp_iterator_next (it, &item); g_value_unset (&item)) {
    WpSpaJson *json = g_value_get_boxed (&item);
    g_autofree gchar *location = NULL;
    g_autofree gchar *basename = NULL;

    g_assert_true (wp_spa_json_object_get (json,
        "section.location", "s", &location, NULL));
    g_assert_true (g_path_is_absolute (location));
    g_assert_cmpuint (n_locations, <, G_N_ELEMENTS (expected_basenames));
    basename = g_path_get_basename (location);
    g_assert_cmpstr (basename, ==, expected_basenames[n_locations]);
    n_locations++;
  }
  g_assert_cmpuint (n_locations, ==, G_N_ELEMENTS (expected_basenames));
}

static void
test_conf_empty_file_lifecycle (void)
{
  g_autoptr (GError) error = NULL;
  g_autofree gchar *file =
      g_strdup_printf ("%s/conf/wireplumber.conf.d/empty.conf",
          g_getenv ("G_TEST_SRCDIR"));
  g_autoptr (WpConf) conf = wp_conf_new (file,
      wp_properties_new ("no-fragments", "true", NULL));

  for (guint open_count = 0; open_count < 2; open_count++) {
    g_autoptr (WpIterator) it = NULL;
    g_auto (GValue) item = G_VALUE_INIT;

    g_assert_true (wp_conf_open (conf, &error));
    g_assert_no_error (error);
    g_assert_true (wp_conf_is_open (conf));

    it = wp_conf_new_locations_iterator (conf);
    g_assert_true (wp_iterator_next (it, &item));
    WpSpaJson *json = g_value_get_boxed (&item);
    g_autofree gchar *location = NULL;
    g_assert_true (wp_spa_json_object_get (json,
        "section.location", "s", &location, NULL));
    g_assert_cmpstr (location, ==, file);
    g_value_unset (&item);
    g_assert_false (wp_iterator_next (it, &item));

    g_clear_pointer (&it, wp_iterator_unref);
    it = wp_conf_new_sections_iterator (conf, WP_CONF_SECTION_MODE_ALL);
    g_assert_false (wp_iterator_next (it, &item));

    wp_conf_close (conf);
    g_assert_false (wp_conf_is_open (conf));
  }
}

gint
main (gint argc, gchar *argv[])
{
  g_test_init (&argc, &argv, NULL);
  wp_init (WP_INIT_ALL);

  g_test_add ("/wp/conf/basic", TestConfFixture, NULL,
      test_conf_setup, test_conf_basic, test_conf_teardown);
  g_test_add ("/wp/conf/merge", TestConfFixture, NULL,
      test_conf_setup, test_conf_merge, test_conf_teardown);
  g_test_add ("/wp/conf/merge_nested", TestConfFixture, NULL,
      test_conf_setup, test_conf_merge_nested, test_conf_teardown);
  g_test_add ("/wp/conf/override", TestConfFixture, NULL,
      test_conf_setup, test_conf_override, test_conf_teardown);
  g_test_add ("/wp/conf/override_nested", TestConfFixture, NULL,
      test_conf_setup, test_conf_override_nested, test_conf_teardown);
  g_test_add ("/wp/conf/as_section", TestConfFixture, NULL,
      NULL, test_conf_as_section, NULL);
  g_test_add ("/wp/conf/sections_iterator", TestConfFixture, NULL,
      test_conf_setup, test_conf_sections_iterator, test_conf_teardown);
  g_test_add ("/wp/conf/locations_iterator", TestConfFixture, NULL,
      test_conf_setup, test_conf_locations_iterator, test_conf_teardown);
  g_test_add_func ("/wp/conf/empty_file_lifecycle",
      test_conf_empty_file_lifecycle);

  return g_test_run ();
}
