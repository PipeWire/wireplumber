/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <spa/pod/iter.h>
#include <spa/pod/vararg.h>
#include <spa/param/props.h>

#include "../../lib/wp/private.h"

#include <wp/wp.h>

static void
test_spa_props_set_get (void)
{
  wp_spa_type_init (TRUE);
  g_assert_true (wp_spa_type_register (WP_SPA_TYPE_TABLE_PROPS, "Wp:Test:Property", "wp-test-property"));

  WpSpaProps props = {0};
  g_autoptr (WpSpaPod) pod = NULL;
  float float_value = 0.0;
  const gchar *string_value = NULL;

  wp_spa_props_register (&props, "volume", "Volume",
      wp_spa_pod_new_choice ("Range", "f", 1.0, "f", 0.0, "f", 10.0, NULL));
  wp_spa_props_register (&props, "wp-test-property", "Test property",
      wp_spa_pod_new_string ("default value"));

  g_assert_nonnull (pod = wp_spa_props_get_stored (&props, "volume"));
  g_assert_true (wp_spa_pod_get_float (pod, &float_value));
  g_assert_cmpfloat_with_epsilon (float_value, 1.0, 0.001);

  g_assert_nonnull (pod = wp_spa_props_get_stored (&props, "wp-test-property"));
  g_assert_true (wp_spa_pod_get_string (pod, &string_value));
  g_assert_cmpstr (string_value, ==, "default value");

  g_autoptr (WpSpaPod) new_float = wp_spa_pod_new_float (0.8);
  g_autoptr (WpSpaPod) new_str = wp_spa_pod_new_string ("test value");
  g_assert_true (wp_spa_props_store (&props, "volume", new_float));
  g_assert_true (wp_spa_props_store (&props, "wp-test-property", new_str));

  g_assert_nonnull (pod = wp_spa_props_get_stored (&props, "volume"));
  g_assert_true (wp_spa_pod_get_float (pod, &float_value));
  g_assert_cmpfloat_with_epsilon (float_value, 0.8, 0.001);

  g_assert_nonnull (pod = wp_spa_props_get_stored (&props, "wp-test-property"));
  g_assert_true (wp_spa_pod_get_string (pod, &string_value));
  g_assert_cmpstr (string_value, ==, "test value");

  wp_spa_props_clear (&props);

  wp_spa_type_deinit ();
}

static void
test_spa_props_build_all (void)
{
  wp_spa_type_init (TRUE);
  g_assert_true (wp_spa_type_register (WP_SPA_TYPE_TABLE_PROPS, "Wp:Test:Property", "wp-test-property"));

  WpSpaProps props = {0};
  WpSpaPod *pod = NULL;
  float float_value = 0.0;
  const gchar *string_value = NULL;
  g_autoptr (WpSpaPod) pod_value = NULL;
  g_autoptr (GPtrArray) arr = NULL;
  const gchar *id_name;
  guint32 id;

  wp_spa_props_register (&props, "volume", "Volume",
      wp_spa_pod_new_choice ("Range", "f", 1.0, "f", 0.0, "f", 10.0, NULL));
  wp_spa_props_register (&props, "wp-test-property", "Test property",
      wp_spa_pod_new_string ("default value"));

  g_autoptr (WpSpaPod) new_float = wp_spa_pod_new_float (0.8);
  g_autoptr (WpSpaPod) new_str = wp_spa_pod_new_string ("test value");
  g_assert_true (wp_spa_props_store (&props, "volume", new_float));
  g_assert_true (wp_spa_props_store (&props, "wp-test-property", new_str));

  arr = wp_spa_props_build_all_pods (&props);
  g_assert_nonnull (arr);
  g_assert_cmpint (arr->len, ==, 3);

  pod = g_ptr_array_index (arr, 0);
  g_assert_nonnull (pod);
  g_assert_true (wp_spa_pod_get_object (pod,
      "Props", &id_name,
      "volume", "f", &float_value,
      "wp-test-property", "s", &string_value,
      NULL));
  g_assert_cmpfloat_with_epsilon (float_value, 0.8, 0.001);
  g_assert_cmpstr (string_value, ==, "test value");

  pod = g_ptr_array_index (arr, 1);
  g_assert_nonnull (pod);
  g_assert_true (wp_spa_pod_get_object (pod,
      "PropInfo", &id_name,
      "id", "I", &id,
      "name", "s", &string_value,
      "type", "P", &pod_value,
      NULL));
  g_assert_cmpuint (id, ==, SPA_PROP_volume);
  g_assert_cmpstr (string_value, ==, "Volume");
  g_assert_nonnull (pod_value);
  g_assert_true (wp_spa_pod_is_choice (pod_value));

  pod = g_ptr_array_index (arr, 2);
  g_assert_nonnull (pod);
  g_assert_true (wp_spa_pod_get_object (pod,
      "PropInfo", &id_name,
      "id", "I", &id,
      "name", "s", &string_value,
      "type", "P", &pod_value,
      NULL));
  g_assert_cmpuint (id, >, SPA_PROP_START_CUSTOM);
  g_assert_cmpstr (string_value, ==, "Test property");
  g_assert_nonnull (pod_value);
  g_assert_true (wp_spa_pod_is_string (pod_value));

  wp_spa_props_clear (&props);

  wp_spa_type_deinit ();
}

static void
test_spa_props_store_from_props (void)
{
  wp_spa_type_init (TRUE);
  g_assert_true (wp_spa_type_register (WP_SPA_TYPE_TABLE_PROPS, "Wp:Test:Property", "wp-test-property"));

  WpSpaProps props = {0};
  g_autoptr (WpSpaPod) pod = NULL;
  float float_value = 0.0;
  const gchar *string_value = NULL;
  g_autoptr (GPtrArray) arr = g_ptr_array_new_with_free_func (g_free);

  wp_spa_props_register (&props, "volume", "Volume",
      wp_spa_pod_new_choice ("Range", "f", 1.0, "f", 0.0, "f", 10.0, NULL));
  wp_spa_props_register (&props, "wp-test-property", "Test property",
      wp_spa_pod_new_string ("default value"));

  pod = wp_spa_pod_new_object (
      "Props", "Props",
      "volume", "f", 0.8,
      "wp-test-property", "s", "test value",
      NULL);
  g_assert_nonnull (pod);
  g_assert_true (wp_spa_props_store_from_props (&props, pod, arr));
  g_assert_cmpint (arr->len, ==, 2);
  g_assert_cmpstr ((const gchar *)g_ptr_array_index (arr, 0), ==, "volume");
  g_assert_cmpstr ((const gchar *)g_ptr_array_index (arr, 1), ==, "wp-test-property");

  g_assert_nonnull (pod = wp_spa_props_get_stored (&props, "volume"));
  g_assert_true (wp_spa_pod_get_float (pod, &float_value));
  g_assert_cmpfloat_with_epsilon (float_value, 0.8, 0.001);

  g_assert_nonnull (pod = wp_spa_props_get_stored (&props, "wp-test-property"));
  g_assert_true (wp_spa_pod_get_string (pod, &string_value));
  g_assert_cmpstr (string_value, ==, "test value");

  wp_spa_props_clear (&props);

  wp_spa_type_deinit ();
}

static void
test_spa_props_register_from_prop_info (void)
{
  wp_spa_type_init (TRUE);
  g_assert_true (wp_spa_type_register (WP_SPA_TYPE_TABLE_PROPS, "Wp:Test:Property", "wp-test-property"));
  guint test_property_id = 0;
  wp_spa_type_get_by_nick (WP_SPA_TYPE_TABLE_PROPS, "wp-test-property", &test_property_id, NULL, NULL);

  WpSpaProps props = {0};
  g_autoptr (WpSpaPod) prop_info = NULL;
  WpSpaPod *pod = NULL;
  float float_value = 0.0;
  const gchar *string_value = NULL;
  g_autoptr (WpSpaPod) pod_value = NULL;
  g_autoptr (GPtrArray) arr = NULL;
  const gchar *id_name;
  guint32 id;

  prop_info = wp_spa_pod_new_object (
      "PropInfo", "PropInfo",
      "id", "I", SPA_PROP_volume,
      "name", "s", "Volume",
      "type", SPA_POD_CHOICE_RANGE_Float (1.0, 0.0, 10.0),
      NULL);
  g_assert_nonnull (prop_info);
  g_assert_true (wp_spa_props_register_from_prop_info (&props, prop_info));

  prop_info = wp_spa_pod_new_object (
      "PropInfo", "PropInfo",
      "id", "I", test_property_id,
      "name", "s", "Test property",
      "type", "s", "default value",
      NULL);
  g_assert_nonnull (prop_info);
  g_assert_true (wp_spa_props_register_from_prop_info (&props, prop_info));

  g_autoptr (WpSpaPod) float_pod = wp_spa_pod_new_float (0.8);
  g_autoptr (WpSpaPod) string_pod = wp_spa_pod_new_string ("test value");
  g_assert_true (wp_spa_props_store (&props, "volume", float_pod));
  g_assert_true (wp_spa_props_store (&props, "wp-test-property", string_pod));

  arr = wp_spa_props_build_all_pods (&props);
  g_assert_nonnull (arr);
  g_assert_cmpint (arr->len, ==, 3);

  pod = g_ptr_array_index (arr, 0);
  g_assert_nonnull (pod);
  g_assert_true (wp_spa_pod_get_object (pod,
      "Props", &id_name,
      "volume", "f", &float_value,
      "wp-test-property", "s", &string_value,
      NULL));
  g_assert_cmpfloat_with_epsilon (float_value, 0.8, 0.001);
  g_assert_cmpstr (string_value, ==, "test value");

  pod = g_ptr_array_index (arr, 1);
  g_assert_nonnull (pod);
  g_assert_true (wp_spa_pod_get_object (pod,
      "PropInfo", &id_name,
      "id", "I", &id,
      "name", "s", &string_value,
      "type", "P", &pod_value,
      NULL));
  g_assert_cmpuint (id, ==, SPA_PROP_volume);
  g_assert_cmpstr (string_value, ==, "Volume");
  g_assert_nonnull (pod_value);
  g_assert_true (wp_spa_pod_is_choice (pod_value));

  pod = g_ptr_array_index (arr, 2);
  g_assert_nonnull (pod);
  g_assert_true (wp_spa_pod_get_object (pod,
      "PropInfo", &id_name,
      "id", "I", &id,
      "name", "s", &string_value,
      "type", "P", &pod_value,
      NULL));
  g_assert_cmpuint (id, ==, test_property_id);
  g_assert_cmpstr (string_value, ==, "Test property");
  g_assert_nonnull (pod_value);
  g_assert_true (wp_spa_pod_is_string (pod_value));

  wp_spa_props_clear (&props);

  wp_spa_type_deinit ();
}

int
main (int argc, char *argv[])
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/wp/spa_props/set_get", test_spa_props_set_get);
  g_test_add_func ("/wp/spa_props/build_all", test_spa_props_build_all);
  g_test_add_func ("/wp/spa_props/store_from_props",
      test_spa_props_store_from_props);
  g_test_add_func ("/wp/spa_props/register_from_prop_info",
      test_spa_props_register_from_prop_info);

  return g_test_run ();
}
