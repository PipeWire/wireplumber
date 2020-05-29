/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>

#include <spa/param/props.h>
#include <spa/pod/vararg.h>

static void
test_props_set_get (void)
{
  g_autoptr (WpProps) props = NULL;
  float float_value = 0.0;
  const gchar *string_value = NULL;

  props = wp_props_new (WP_PROPS_MODE_STORE, NULL);
  wp_props_register (props, "volume", "Volume",
      wp_spa_pod_new_choice ("Range", "f", 1.0, "f", 0.0, "f", 10.0, NULL));
  wp_props_register (props, "wp-test-property", "Test property",
      wp_spa_pod_new_string ("default value"));

  {
    g_autoptr (WpSpaPod) pod = wp_props_get (props, "volume");
    g_assert_nonnull (pod);
    g_assert_true (wp_spa_pod_get_float (pod, &float_value));
    g_assert_cmpfloat_with_epsilon (float_value, 1.0, 0.001);
  }

  {
    g_autoptr (WpSpaPod) pod = wp_props_get (props, "wp-test-property");
    g_assert_nonnull (pod);
    g_assert_true (wp_spa_pod_get_string (pod, &string_value));
    g_assert_cmpstr (string_value, ==, "default value");
  }

  wp_props_set (props, "volume", wp_spa_pod_new_float (0.8));
  wp_props_set (props, "wp-test-property",
      wp_spa_pod_new_string ("test value"));

  {
    g_autoptr (WpSpaPod) pod = wp_props_get (props, "volume");
    g_assert_nonnull (pod);
    g_assert_true (wp_spa_pod_get_float (pod, &float_value));
    g_assert_cmpfloat_with_epsilon (float_value, 0.8, 0.001);
  }

  {
    g_autoptr (WpSpaPod) pod = wp_props_get (props, "wp-test-property");
    g_assert_nonnull (pod);
    g_assert_true (wp_spa_pod_get_string (pod, &string_value));
    g_assert_cmpstr (string_value, ==, "test value");
  }
}

static void
test_props_get_all (void)
{
  g_autoptr (WpProps) props = NULL;
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) item = G_VALUE_INIT;
  float float_value = 0.0;
  const gchar *string_value = NULL;
  const gchar *id_name;
  guint32 id;

  props = wp_props_new (WP_PROPS_MODE_STORE, NULL);
  wp_props_register (props, "volume", "Volume",
      wp_spa_pod_new_choice ("Range", "f", 1.0, "f", 0.0, "f", 10.0, NULL));
  wp_props_register (props, "wp-test-property", "Test property",
      wp_spa_pod_new_string ("default value"));

  wp_props_set (props, "volume", wp_spa_pod_new_float (0.8));
  wp_props_set (props, "wp-test-property",
      wp_spa_pod_new_string ("test value"));

  {
    g_autoptr (WpSpaPod) pod = wp_props_get_all (props);
    g_assert_nonnull (pod);
    g_assert_true (wp_spa_pod_get_object (pod,
        "Props", &id_name,
        "volume", "f", &float_value,
        "wp-test-property", "s", &string_value,
        NULL));
    g_assert_cmpfloat_with_epsilon (float_value, 0.8, 0.001);
    g_assert_cmpstr (string_value, ==, "test value");
  }

  it = wp_props_iterate_prop_info (props);
  g_assert_true (wp_iterator_next (it, &item));

  {
    g_autoptr (WpSpaPod) pod = g_value_dup_boxed (&item);
    g_autoptr (WpSpaPod) pod_value = NULL;
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
  }

  g_value_unset (&item);
  g_assert_true (wp_iterator_next (it, &item));

  {
    g_autoptr (WpSpaPod) pod = g_value_dup_boxed (&item);
    g_autoptr (WpSpaPod) pod_value = NULL;
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
  }
}

static void
test_props_store_from_props (void)
{
  g_autoptr (WpProps) props = NULL;
  float float_value = 0.0;
  const gchar *string_value = NULL;

  props = wp_props_new (WP_PROPS_MODE_STORE, NULL);
  wp_props_register (props, "volume", "Volume",
      wp_spa_pod_new_choice ("Range", "f", 1.0, "f", 0.0, "f", 10.0, NULL));
  wp_props_register (props, "wp-test-property", "Test property",
      wp_spa_pod_new_string ("default value"));

  wp_props_set (props, NULL, wp_spa_pod_new_object (
          "Props", "Props",
          "volume", "f", 0.8,
          "wp-test-property", "s", "test value",
          NULL));

  {
    g_autoptr (WpSpaPod) pod = wp_props_get (props, "volume");
    g_assert_nonnull (pod);
    g_assert_true (wp_spa_pod_get_float (pod, &float_value));
    g_assert_cmpfloat_with_epsilon (float_value, 0.8, 0.001);
  }

  {
    g_autoptr (WpSpaPod) pod = wp_props_get (props, "wp-test-property");
    g_assert_nonnull (pod);
    g_assert_true (wp_spa_pod_get_string (pod, &string_value));
    g_assert_cmpstr (string_value, ==, "test value");
  }
}

static void
test_props_register_from_info (void)
{
  g_autoptr (WpProps) props = NULL;
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) item = G_VALUE_INIT;
  float float_value = 0.0;
  const gchar *string_value = NULL;
  const gchar *id_name;
  guint32 id;
  guint test_property_id = 0;

  wp_spa_type_get_by_nick (WP_SPA_TYPE_TABLE_PROPS, "wp-test-property",
      &test_property_id, NULL, NULL);

  props = wp_props_new (WP_PROPS_MODE_STORE, NULL);

  wp_props_register_from_info (props, wp_spa_pod_new_object (
          "PropInfo", "PropInfo",
          "id", "I", SPA_PROP_volume,
          "name", "s", "Volume",
          "type", SPA_POD_CHOICE_RANGE_Float (1.0, 0.0, 10.0),
          NULL));

  wp_props_register_from_info (props, wp_spa_pod_new_object (
          "PropInfo", "PropInfo",
          "id", "I", test_property_id,
          "name", "s", "Test property",
          "type", "s", "default value",
          NULL));

  wp_props_set (props, "volume", wp_spa_pod_new_float (0.8));
  wp_props_set (props, "wp-test-property",
      wp_spa_pod_new_string ("test value"));

  {
    g_autoptr (WpSpaPod) pod = wp_props_get_all (props);
    g_assert_nonnull (pod);
    g_assert_true (wp_spa_pod_get_object (pod,
        "Props", &id_name,
        "volume", "f", &float_value,
        "wp-test-property", "s", &string_value,
        NULL));
    g_assert_cmpfloat_with_epsilon (float_value, 0.8, 0.001);
    g_assert_cmpstr (string_value, ==, "test value");
  }

  it = wp_props_iterate_prop_info (props);
  g_assert_true (wp_iterator_next (it, &item));

  {
    g_autoptr (WpSpaPod) pod = g_value_dup_boxed (&item);
    g_autoptr (WpSpaPod) pod_value = NULL;
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
  }

  g_value_unset (&item);
  g_assert_true (wp_iterator_next (it, &item));

  {
    g_autoptr (WpSpaPod) pod = g_value_dup_boxed (&item);
    g_autoptr (WpSpaPod) pod_value = NULL;
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
  }
}

int
main (int argc, char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  wp_init (WP_INIT_ALL);

  g_assert_true (wp_spa_type_register (WP_SPA_TYPE_TABLE_PROPS,
          "Wp:Test:Property", "wp-test-property"));

  g_test_add_func ("/wp/props/set_get", test_props_set_get);
  g_test_add_func ("/wp/props/get_all", test_props_get_all);
  g_test_add_func ("/wp/props/store_from_props",
      test_props_store_from_props);
  g_test_add_func ("/wp/props/register_from_info",
      test_props_register_from_info);

  return g_test_run ();
}
