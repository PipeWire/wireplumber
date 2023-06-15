/* WirePlumber
 *
 * Copyright © 2022 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */
#include "../common/base-test-fixture.h"

typedef struct {
  WpBaseTestFixture base;
  WpConf *conf;
} TestConfFixture;

static void
test_conf_setup (TestConfFixture *self, gconstpointer user_data)
{
  self->base.conf_file =
      g_strdup_printf ("%s/conf/wireplumber.conf", g_getenv ("G_TEST_SRCDIR"));
  wp_base_test_fixture_setup (&self->base, WP_BASE_TEST_FLAG_CLIENT_CORE);
  self->conf = wp_conf_get_instance (self->base.core);
}

static void
test_conf_teardown (TestConfFixture *self, gconstpointer user_data)
{
  g_clear_object (&self->conf);
  wp_base_test_fixture_teardown (&self->base);
}

static void
test_conf_basic (TestConfFixture *f, gconstpointer data)
{
  g_assert_nonnull (f->conf);

  /* Boolean Array */
  {
    g_autoptr (WpSpaJson) s = wp_conf_get_section (f->conf,
        "wireplumber.section.array.boolean", NULL);
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
        "wireplumber.section.array.int", NULL);
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
        "wireplumber.section.array.float", NULL);
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
        "wireplumber.section.array.string", NULL);
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
        "wireplumber.section.array.array", NULL);
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
        "wireplumber.section.array.object", NULL);
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
        "wireplumber.section.object", NULL);
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

  /* Fallback */
  {
    g_autoptr (WpSpaJson) fallback = wp_spa_json_new_from_string ("{key1 = 3");
    g_assert_nonnull (fallback);

    g_autoptr (WpSpaJson) s = wp_conf_get_section (f->conf,
        "wireplumber.section.object", wp_spa_json_ref (fallback));
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

    g_autoptr (WpSpaJson) s2 = wp_conf_get_section (f->conf,
        "invalid-section", wp_spa_json_ref (fallback));
    g_assert_nonnull (s2);
    g_assert_true (wp_spa_json_is_object (s2));
    gint v = 0;
    g_assert_true (wp_spa_json_object_get (s2, "key1", "i", &v, NULL));
    g_assert_cmpint (v, ==, 3);
  }
}

static void
test_conf_merge (TestConfFixture *f, gconstpointer data)
{
  g_assert_nonnull (f->conf);

  /* Boolean Array */
  {
    g_autoptr (WpSpaJson) s = wp_conf_get_section (f->conf,
        "wireplumber.section-merged.array.boolean", NULL);
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
        "wireplumber.section-merged.array.int", NULL);
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
        "wireplumber.section-merged.array.float", NULL);
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
        "wireplumber.section-merged.array.string", NULL);
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
        "wireplumber.section-merged.array.array", NULL);
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
        "wireplumber.section-merged.array.object", NULL);
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
        "wireplumber.section-merged.object", NULL);
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
      "wireplumber.section-nested-merged", NULL);
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
      "wireplumber.section-override", NULL);
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
      "wireplumber.section-nested-override", NULL);
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
test_conf_get_value (TestConfFixture *f, gconstpointer data)
{
  g_assert_nonnull (f->conf);

  /* Value */
  {
    g_autoptr (WpSpaJson) fallback = wp_spa_json_new_int (8);
    g_assert_nonnull (fallback);

    g_autoptr (WpSpaJson) v1 = wp_conf_get_value (f->conf,
        "wireplumber.section.object", "key.int", wp_spa_json_ref (fallback));
    g_assert_nonnull (v1);
    gint v1_int = 0;
    g_assert_true (wp_spa_json_parse_int (v1, &v1_int));
    g_assert_cmpint (v1_int, ==, -1);

    g_autoptr (WpSpaJson) v2 = wp_conf_get_value (f->conf,
        "wireplumber.section.object", "unavailable", wp_spa_json_ref (fallback));
    g_assert_nonnull (v2);
    gint v2_int = 0;
    g_assert_true (wp_spa_json_parse_int (v2, &v2_int));
    g_assert_cmpint (v2_int, ==, 8);

    g_autoptr (WpSpaJson) v3 = wp_conf_get_value (f->conf,
        "wireplumber.section.object", "key.int", NULL);
    g_assert_nonnull (v3);
    gint v3_int = 0;
    g_assert_true (wp_spa_json_parse_int (v3, &v3_int));
    g_assert_cmpint (v3_int, ==, -1);

    g_autoptr (WpSpaJson) v4 = wp_conf_get_value (f->conf,
        "wireplumber.section.object", "unavailable", NULL);
    g_assert_null (v4);
  }

  /* Boolean */
  {
    gboolean v1 = wp_conf_get_value_boolean (f->conf,
        "wireplumber.section.object", "key.boolean", FALSE);
    g_assert_true (v1);

    gboolean v2 = wp_conf_get_value_boolean (f->conf,
        "wireplumber.section.object", "unavailable", TRUE);
    g_assert_true (v2);
  }

  /* Int */
  {
    gint v1 = wp_conf_get_value_int (f->conf,
        "wireplumber.section.object", "key.int", 4);
    g_assert_cmpint (v1, ==, -1);

    gint v2 = wp_conf_get_value_int (f->conf,
        "wireplumber.section.object", "unavailable", 4);
    g_assert_cmpint (v2, ==, 4);
  }

  /* Float */
  {
    float v1 = wp_conf_get_value_float (f->conf,
        "wireplumber.section.object", "key.float", 9.99);
    g_assert_cmpfloat_with_epsilon (v1, 3.14, 0.001);

    float v2 = wp_conf_get_value_float (f->conf,
        "wireplumber.section.object", "unavailable", 9.99);
    g_assert_cmpfloat_with_epsilon (v2, 9.99, 0.001);
  }

  /* String */
  {
    g_autofree gchar *v1 = wp_conf_get_value_string (f->conf,
        "wireplumber.section.object", "key.string", "fallback");
    g_assert_cmpstr (v1, ==, "wireplumber");

    g_autofree gchar *v2 = wp_conf_get_value_string (f->conf,
        "wireplumber.section.object", "unavailable", "fallback");
    g_assert_cmpstr (v2, ==, "fallback");

    g_autofree gchar *v3 = wp_conf_get_value_string (f->conf,
        "wireplumber.section.object", "key.string", NULL);
    g_assert_cmpstr (v3, ==, "wireplumber");

    g_autofree gchar *v4 = wp_conf_get_value_string (f->conf,
        "wireplumber.section.object", "unavailable", NULL);
    g_assert_null (v4);
  }
}

static void
test_conf_apply_rules (TestConfFixture *f, gconstpointer data)
{
  g_assert_nonnull (f->conf);

  /* Unmatched */
  {
    g_autoptr (WpProperties) match_props = NULL;

    match_props = wp_properties_new (
        "device.name", "unmatched-device-name",
        NULL);

    g_assert_false (wp_conf_apply_rules (f->conf, "wireplumber.section.rules",
        match_props, NULL, NULL));

    g_assert_false (wp_conf_apply_rules (f->conf, "invalid-section",
        match_props, NULL, NULL));
  }

  /* Match equal */
  {
    g_autoptr (WpProperties) match_props = NULL;

    match_props = wp_properties_new (
        "node.name", "alsa_output.0.my-alsa-device",
        NULL);

    g_assert_true (wp_conf_apply_rules (f->conf, "wireplumber.section.rules",
        match_props, NULL, NULL));
  }

  /* Without applied_props */
  {
    g_autoptr (WpProperties) match_props = NULL;
    const gchar *str = NULL;

    match_props = wp_properties_new (
        "device.name", "alsa_card.0.my-alsa-device",
        NULL);
    g_assert_nonnull (match_props);

    str = wp_properties_get (match_props, "device.name");
    g_assert_cmpstr (str, ==, "alsa_card.0.my-alsa-device");
    str = wp_properties_get (match_props, "api.alsa.use-acp");
    g_assert_null (str);
    str = wp_properties_get (match_props, "api.acp.auto-port");
    g_assert_null (str);

    g_assert_true (wp_conf_apply_rules (f->conf, "wireplumber.section.rules",
        match_props, NULL, NULL));

    str = wp_properties_get (match_props, "device.name");
    g_assert_cmpstr (str, ==, "alsa_card.0.my-alsa-device");
    str = wp_properties_get (match_props, "api.alsa.use-acp");
    g_assert_cmpstr (str, ==, "true");
    str = wp_properties_get (match_props, "api.acp.auto-port");
    g_assert_cmpstr (str, ==, "false");
  }

  /* With applied_props */
  {
    g_autoptr (WpProperties) match_props = NULL;
    g_autoptr (WpProperties) applied_props = NULL;
    const gchar *str = NULL;

    match_props = wp_properties_new (
        "device.name", "alsa_card.0.my-alsa-device",
        NULL);
    g_assert_nonnull (match_props);

    applied_props = wp_properties_new_empty ();

    str = wp_properties_get (match_props, "device.name");
    g_assert_cmpstr (str, ==, "alsa_card.0.my-alsa-device");
    str = wp_properties_get (match_props, "api.alsa.use-acp");
    g_assert_null (str);
    str = wp_properties_get (match_props, "api.acp.auto-port");
    g_assert_null (str);

    g_assert_true (wp_conf_apply_rules (f->conf, "wireplumber.section.rules",
        match_props, applied_props, NULL));

    str = wp_properties_get (match_props, "device.name");
    g_assert_cmpstr (str, ==, "alsa_card.0.my-alsa-device");
    str = wp_properties_get (match_props, "api.alsa.use-acp");
    g_assert_null (str);
    str = wp_properties_get (match_props, "api.acp.auto-port");
    g_assert_null (str);

    str = wp_properties_get (applied_props, "device.name");
    g_assert_cmpstr (str, ==, "alsa_card.0.my-alsa-device");
    str = wp_properties_get (applied_props, "api.alsa.use-acp");
    g_assert_cmpstr (str, ==, "true");
    str = wp_properties_get (applied_props, "api.acp.auto-port");
    g_assert_cmpstr (str, ==, "false");
  }

  /* Fallback without applied_props and invalid section */
  {
    g_autoptr (WpProperties) match_props = NULL;
    g_autoptr (WpSpaJson) fallback = NULL;
    const gchar *str = NULL;

    match_props = wp_properties_new (
        "device.name", "alsa_card.0.my-alsa-device",
        NULL);
    g_assert_nonnull (match_props);

    str = wp_properties_get (match_props, "device.name");
    g_assert_cmpstr (str, ==, "alsa_card.0.my-alsa-device");
    str = wp_properties_get (match_props, "api.alsa.use-acp");
    g_assert_null (str);
    str = wp_properties_get (match_props, "api.acp.auto-port");
    g_assert_null (str);
    str = wp_properties_get (match_props, "fallback.key");
    g_assert_null (str);

    fallback = wp_spa_json_new_from_string (
        "[{matches = [{device.name = ~alsa_card.*}], update-props = {fallback.key = true}}]");

    g_assert_true (wp_conf_apply_rules (f->conf, "invalid-section",
        match_props, NULL, wp_spa_json_ref (fallback)));

    str = wp_properties_get (match_props, "device.name");
    g_assert_cmpstr (str, ==, "alsa_card.0.my-alsa-device");
    str = wp_properties_get (match_props, "api.alsa.use-acp");
    g_assert_null (str);
    str = wp_properties_get (match_props, "api.acp.auto-port");
    g_assert_null (str);
    str = wp_properties_get (match_props, "fallback.key");
    g_assert_cmpstr (str, ==, "true");
  }

  /* Fallback without applied_props and valid section */
  {
    g_autoptr (WpProperties) match_props = NULL;
    g_autoptr (WpSpaJson) fallback = NULL;
    const gchar *str = NULL;

    match_props = wp_properties_new (
        "device.name", "alsa_card.0.my-alsa-device",
        NULL);
    g_assert_nonnull (match_props);

    str = wp_properties_get (match_props, "device.name");
    g_assert_cmpstr (str, ==, "alsa_card.0.my-alsa-device");
    str = wp_properties_get (match_props, "api.alsa.use-acp");
    g_assert_null (str);
    str = wp_properties_get (match_props, "api.acp.auto-port");
    g_assert_null (str);
    str = wp_properties_get (match_props, "fallback.key");
    g_assert_null (str);

    fallback = wp_spa_json_new_from_string (
        "[{matches = [{device.name = ~alsa_card.*}], update-props = {fallback.key = true}}]");

    g_assert_true (wp_conf_apply_rules (f->conf, "wireplumber.section.rules",
        match_props, NULL, wp_spa_json_ref (fallback)));

    str = wp_properties_get (match_props, "device.name");
    g_assert_cmpstr (str, ==, "alsa_card.0.my-alsa-device");
    str = wp_properties_get (match_props, "api.alsa.use-acp");
    g_assert_cmpstr (str, ==, "true");
    str = wp_properties_get (match_props, "api.acp.auto-port");
    g_assert_cmpstr (str, ==, "false");
    str = wp_properties_get (match_props, "fallback.key");
    g_assert_null (str);
  }

  /* Fallback with applied_props and invalid section */
  {
    g_autoptr (WpProperties) match_props = NULL;
    g_autoptr (WpProperties) applied_props = NULL;
    g_autoptr (WpSpaJson) fallback = NULL;
    const gchar *str = NULL;

    match_props = wp_properties_new (
        "device.name", "alsa_card.0.my-alsa-device",
        NULL);
    g_assert_nonnull (match_props);

    applied_props = wp_properties_new_empty ();

    str = wp_properties_get (match_props, "device.name");
    g_assert_cmpstr (str, ==, "alsa_card.0.my-alsa-device");
    str = wp_properties_get (match_props, "api.alsa.use-acp");
    g_assert_null (str);
    str = wp_properties_get (match_props, "api.acp.auto-port");
    g_assert_null (str);
    str = wp_properties_get (match_props, "fallback.key");
    g_assert_null (str);

    fallback = wp_spa_json_new_from_string (
        "[{matches = [{device.name = ~alsa_card.*}], update-props = {fallback.key = true}}]");

    g_assert_true (wp_conf_apply_rules (f->conf, "invalid-section",
        match_props, applied_props, wp_spa_json_ref (fallback)));

    str = wp_properties_get (match_props, "device.name");
    g_assert_cmpstr (str, ==, "alsa_card.0.my-alsa-device");
    str = wp_properties_get (match_props, "api.alsa.use-acp");
    g_assert_null (str);
    str = wp_properties_get (match_props, "api.acp.auto-port");
    g_assert_null (str);
    str = wp_properties_get (match_props, "fallback.key");
    g_assert_null (str);

    str = wp_properties_get (applied_props, "device.name");
    g_assert_cmpstr (str, ==, "alsa_card.0.my-alsa-device");
    str = wp_properties_get (applied_props, "api.alsa.use-acp");
    g_assert_null (str);
    str = wp_properties_get (applied_props, "api.acp.auto-port");
    g_assert_null (str);
    str = wp_properties_get (applied_props, "fallback.key");
    g_assert_cmpstr (str, ==, "true");
  }

  /* Fallback with applied_props and valid section */
  {
    g_autoptr (WpProperties) match_props = NULL;
    g_autoptr (WpProperties) applied_props = NULL;
    g_autoptr (WpSpaJson) fallback = NULL;
    const gchar *str = NULL;

    match_props = wp_properties_new (
        "device.name", "alsa_card.0.my-alsa-device",
        NULL);
    g_assert_nonnull (match_props);

    applied_props = wp_properties_new_empty ();

    str = wp_properties_get (match_props, "device.name");
    g_assert_cmpstr (str, ==, "alsa_card.0.my-alsa-device");
    str = wp_properties_get (match_props, "api.alsa.use-acp");
    g_assert_null (str);
    str = wp_properties_get (match_props, "api.acp.auto-port");
    g_assert_null (str);
    str = wp_properties_get (match_props, "fallback.key");
    g_assert_null (str);

    fallback = wp_spa_json_new_from_string (
        "[{matches = [{device.name = ~alsa_card.*}], update-props = {fallback.key = true}}]");

    g_assert_true (wp_conf_apply_rules (f->conf, "wireplumber.section.rules",
        match_props, applied_props, wp_spa_json_ref (fallback)));

    str = wp_properties_get (match_props, "device.name");
    g_assert_cmpstr (str, ==, "alsa_card.0.my-alsa-device");
    str = wp_properties_get (match_props, "api.alsa.use-acp");
    g_assert_null (str);
    str = wp_properties_get (match_props, "api.acp.auto-port");
    g_assert_null (str);
    str = wp_properties_get (match_props, "fallback.key");
    g_assert_null (str);

    str = wp_properties_get (applied_props, "device.name");
    g_assert_cmpstr (str, ==, "alsa_card.0.my-alsa-device");
    str = wp_properties_get (applied_props, "api.alsa.use-acp");
    g_assert_cmpstr (str, ==, "true");
    str = wp_properties_get (applied_props, "api.acp.auto-port");
    g_assert_cmpstr (str, ==, "false");
    str = wp_properties_get (applied_props, "fallback.key");
    g_assert_null (str);
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
  g_test_add ("/wp/conf/get_value", TestConfFixture, NULL,
      test_conf_setup, test_conf_get_value, test_conf_teardown);
  g_test_add ("/wp/conf/apply_rules", TestConfFixture, NULL,
      test_conf_setup, test_conf_apply_rules, test_conf_teardown);

  return g_test_run ();
}
