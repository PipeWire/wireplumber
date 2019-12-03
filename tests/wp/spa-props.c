/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

/* private functions, they should be hidden in the shared library */
#include "wp/spa-props.c"

#include <spa/pod/iter.h>

static void
test_spa_props_set_get (void)
{
  WpSpaProps props = {0};
  const struct spa_pod *pod;
  float float_value = 0.0;
  const gchar *string_value = NULL;

  wp_spa_props_register (&props, SPA_PROP_volume, "Volume",
      SPA_POD_CHOICE_RANGE_Float (1.0, 0.0, 10.0));
  wp_spa_props_register (&props, SPA_PROP_START_CUSTOM + 1, "Test property",
      SPA_POD_String ("default value"));

  g_assert_nonnull (pod = wp_spa_props_get_stored (&props, SPA_PROP_volume));
  g_assert_cmpint (spa_pod_get_float (pod, &float_value), ==, 0);
  g_assert_cmpfloat_with_epsilon (float_value, 1.0, 0.001);

  g_assert_nonnull (pod = wp_spa_props_get_stored (&props, SPA_PROP_START_CUSTOM + 1));
  g_assert_cmpint (spa_pod_get_string (pod, &string_value), ==, 0);
  g_assert_cmpstr (string_value, ==, "default value");

  g_assert_cmpint (wp_spa_props_store (&props, SPA_PROP_volume,
          SPA_POD_Float (0.8)), ==, 1);
  g_assert_cmpint (wp_spa_props_store (&props, SPA_PROP_START_CUSTOM + 1,
          SPA_POD_String ("test value")), ==, 1);

  g_assert_nonnull (pod = wp_spa_props_get_stored (&props, SPA_PROP_volume));
  g_assert_cmpint (spa_pod_get_float (pod, &float_value), ==, 0);
  g_assert_cmpfloat_with_epsilon (float_value, 0.8, 0.001);

  g_assert_nonnull (pod = wp_spa_props_get_stored (&props, SPA_PROP_START_CUSTOM + 1));
  g_assert_cmpint (spa_pod_get_string (pod, &string_value), ==, 0);
  g_assert_cmpstr (string_value, ==, "test value");

  wp_spa_props_clear (&props);
}

static void
test_spa_props_build_all (void)
{
  WpSpaProps props = {0};
  gchar buffer[512];
  struct spa_pod_builder b = SPA_POD_BUILDER_INIT (buffer, sizeof (buffer));
  struct spa_pod *pod;
  float float_value = 0.0;
  const gchar *string_value = NULL;
  guint32 id;
  g_autoptr (GPtrArray) arr = NULL;

  wp_spa_props_register (&props, SPA_PROP_volume, "Volume",
      SPA_POD_CHOICE_RANGE_Float (1.0, 0.0, 10.0));
  wp_spa_props_register (&props, SPA_PROP_START_CUSTOM + 1, "Test property",
      SPA_POD_String ("default value"));

  g_assert_cmpint (wp_spa_props_store (&props, SPA_PROP_volume,
          SPA_POD_Float (0.8)), ==, 1);
  g_assert_cmpint (wp_spa_props_store (&props, SPA_PROP_START_CUSTOM + 1,
          SPA_POD_String ("test value")), ==, 1);

  arr = wp_spa_props_build_all_pods (&props, &b);
  g_assert_nonnull (arr);
  g_assert_cmpint (arr->len, ==, 3);

  pod = g_ptr_array_index (arr, 0);
  g_assert_nonnull (pod);
  g_assert_true (spa_pod_is_object_type (pod, SPA_TYPE_OBJECT_Props));
  g_assert_true (spa_pod_is_object_id (pod, SPA_PARAM_Props));
  g_assert_cmpint (spa_pod_parse_object (pod,
          SPA_TYPE_OBJECT_Props, NULL,
          SPA_PROP_volume, SPA_POD_Float (&float_value),
          SPA_PROP_START_CUSTOM + 1, SPA_POD_String (&string_value)),
      ==, 2);
  g_assert_cmpfloat_with_epsilon (float_value, 0.8, 0.001);
  g_assert_cmpstr (string_value, ==, "test value");

  pod = g_ptr_array_index (arr, 1);
  g_assert_nonnull (pod);
  g_assert_true (spa_pod_is_object_type (pod, SPA_TYPE_OBJECT_PropInfo));
  g_assert_true (spa_pod_is_object_id (pod, SPA_PARAM_PropInfo));

  g_assert_cmpint (spa_pod_parse_object (pod,
          SPA_TYPE_OBJECT_PropInfo, NULL,
          SPA_PROP_INFO_id, SPA_POD_Id (&id),
          SPA_PROP_INFO_name, SPA_POD_String (&string_value),
          SPA_PROP_INFO_type, SPA_POD_Pod (&pod)),
      ==, 3);
  g_assert_cmpuint (id, ==, SPA_PROP_volume);
  g_assert_cmpstr (string_value, ==, "Volume");
  g_assert_nonnull (pod);
  /* https://gitlab.freedesktop.org/pipewire/pipewire/issues/196
  g_assert_true (spa_pod_is_choice (pod));
  g_assert_true (SPA_POD_CHOICE_VALUE_TYPE (pod) == SPA_TYPE_Float); */
  g_assert_true (SPA_POD_TYPE (pod) == SPA_TYPE_Float);

  pod = g_ptr_array_index (arr, 2);
  g_assert_nonnull (pod);
  g_assert_true (spa_pod_is_object_type (pod, SPA_TYPE_OBJECT_PropInfo));
  g_assert_true (spa_pod_is_object_id (pod, SPA_PARAM_PropInfo));

  g_assert_cmpint (spa_pod_parse_object (pod,
          SPA_TYPE_OBJECT_PropInfo, NULL,
          SPA_PROP_INFO_id, SPA_POD_Id (&id),
          SPA_PROP_INFO_name, SPA_POD_String (&string_value),
          SPA_PROP_INFO_type, SPA_POD_Pod (&pod)),
      ==, 3);
  g_assert_cmpuint (id, ==, SPA_PROP_START_CUSTOM + 1);
  g_assert_cmpstr (string_value, ==, "Test property");
  g_assert_nonnull (pod);
  g_assert_true (spa_pod_is_string (pod));

  wp_spa_props_clear (&props);
}

static void
test_spa_props_store_from_props (void)
{
  WpSpaProps props = {0};
  gchar buffer[512];
  struct spa_pod_builder b = SPA_POD_BUILDER_INIT (buffer, sizeof (buffer));
  const struct spa_pod *pod;
  float float_value = 0.0;
  const gchar *string_value = NULL;
  g_autoptr (GArray) arr = g_array_new (FALSE, FALSE, sizeof (guint32));

  wp_spa_props_register (&props, SPA_PROP_volume, "Volume",
      SPA_POD_CHOICE_RANGE_Float (1.0, 0.0, 10.0));
  wp_spa_props_register (&props, SPA_PROP_START_CUSTOM + 1, "Test property",
      SPA_POD_String ("default value"));

  pod = spa_pod_builder_add_object (&b,
      SPA_TYPE_OBJECT_Props, SPA_PARAM_Props,
      SPA_PROP_volume, SPA_POD_Float (0.8),
      SPA_PROP_START_CUSTOM + 1, SPA_POD_String ("test value"));

  g_assert_cmpint (wp_spa_props_store_from_props (&props, pod, arr), ==, 2);
  g_assert_cmpint (arr->len, ==, 2);
  g_assert_cmpint (((guint32 *)arr->data)[0], ==, SPA_PROP_volume);
  g_assert_cmpint (((guint32 *)arr->data)[1], ==, SPA_PROP_START_CUSTOM + 1);

  g_assert_nonnull (pod = wp_spa_props_get_stored (&props, SPA_PROP_volume));
  g_assert_cmpint (spa_pod_get_float (pod, &float_value), ==, 0);
  g_assert_cmpfloat_with_epsilon (float_value, 0.8, 0.001);

  g_assert_nonnull (pod = wp_spa_props_get_stored (&props, SPA_PROP_START_CUSTOM + 1));
  g_assert_cmpint (spa_pod_get_string (pod, &string_value), ==, 0);
  g_assert_cmpstr (string_value, ==, "test value");

  wp_spa_props_clear (&props);
}

static void
test_spa_props_register_from_prop_info (void)
{
  WpSpaProps props = {0};
  gchar buffer[512];
  struct spa_pod_builder b = SPA_POD_BUILDER_INIT (buffer, sizeof (buffer));
  const struct spa_pod *pod;
  float float_value = 0.0;
  const gchar *string_value = NULL;
  g_autoptr (GPtrArray) arr = NULL;
  guint32 id;

  pod = spa_pod_builder_add_object (&b,
      SPA_TYPE_OBJECT_PropInfo, SPA_PARAM_PropInfo,
      SPA_PROP_INFO_id, SPA_POD_Id (SPA_PROP_volume),
      SPA_PROP_INFO_name, SPA_POD_String ("Volume"),
      SPA_PROP_INFO_type, SPA_POD_CHOICE_RANGE_Float (1.0, 0.0, 10.0));

  g_assert_cmpint (wp_spa_props_register_from_prop_info (&props, pod), ==, 0);

  pod = spa_pod_builder_add_object (&b,
      SPA_TYPE_OBJECT_PropInfo, SPA_PARAM_PropInfo,
      SPA_PROP_INFO_id, SPA_POD_Id (SPA_PROP_START_CUSTOM + 1),
      SPA_PROP_INFO_name, SPA_POD_String ("Test property"),
      SPA_PROP_INFO_type, SPA_POD_String ("default value"));

  g_assert_cmpint (wp_spa_props_register_from_prop_info (&props, pod), ==, 0);

  g_assert_cmpint (wp_spa_props_store (&props, SPA_PROP_volume,
          SPA_POD_Float (0.8)), ==, 1);
  g_assert_cmpint (wp_spa_props_store (&props, SPA_PROP_START_CUSTOM + 1,
          SPA_POD_String ("test value")), ==, 1);

  arr = wp_spa_props_build_all_pods (&props, &b);
  g_assert_nonnull (arr);
  g_assert_cmpint (arr->len, ==, 3);

  pod = g_ptr_array_index (arr, 0);
  g_assert_nonnull (pod);
  g_assert_true (spa_pod_is_object_type (pod, SPA_TYPE_OBJECT_Props));
  g_assert_true (spa_pod_is_object_id (pod, SPA_PARAM_Props));
  g_assert_cmpint (spa_pod_parse_object (pod,
          SPA_TYPE_OBJECT_Props, NULL,
          SPA_PROP_volume, SPA_POD_Float (&float_value),
          SPA_PROP_START_CUSTOM + 1, SPA_POD_String (&string_value)),
      ==, 2);
  g_assert_cmpfloat_with_epsilon (float_value, 0.8, 0.001);
  g_assert_cmpstr (string_value, ==, "test value");

  pod = g_ptr_array_index (arr, 1);
  g_assert_nonnull (pod);
  g_assert_true (spa_pod_is_object_type (pod, SPA_TYPE_OBJECT_PropInfo));
  g_assert_true (spa_pod_is_object_id (pod, SPA_PARAM_PropInfo));

  g_assert_cmpint (spa_pod_parse_object (pod,
          SPA_TYPE_OBJECT_PropInfo, NULL,
          SPA_PROP_INFO_id, SPA_POD_Id (&id),
          SPA_PROP_INFO_name, SPA_POD_String (&string_value),
          SPA_PROP_INFO_type, SPA_POD_Pod (&pod)),
      ==, 3);
  g_assert_cmpuint (id, ==, SPA_PROP_volume);
  g_assert_cmpstr (string_value, ==, "Volume");
  g_assert_nonnull (pod);
  /* https://gitlab.freedesktop.org/pipewire/pipewire/issues/196
  g_assert_true (spa_pod_is_choice (pod));
  g_assert_true (SPA_POD_CHOICE_VALUE_TYPE (pod) == SPA_TYPE_Float); */
  g_assert_true (SPA_POD_TYPE (pod) == SPA_TYPE_Float);

  pod = g_ptr_array_index (arr, 2);
  g_assert_nonnull (pod);
  g_assert_true (spa_pod_is_object_type (pod, SPA_TYPE_OBJECT_PropInfo));
  g_assert_true (spa_pod_is_object_id (pod, SPA_PARAM_PropInfo));

  g_assert_cmpint (spa_pod_parse_object (pod,
          SPA_TYPE_OBJECT_PropInfo, NULL,
          SPA_PROP_INFO_id, SPA_POD_Id (&id),
          SPA_PROP_INFO_name, SPA_POD_String (&string_value),
          SPA_PROP_INFO_type, SPA_POD_Pod (&pod)),
      ==, 3);
  g_assert_cmpuint (id, ==, SPA_PROP_START_CUSTOM + 1);
  g_assert_cmpstr (string_value, ==, "Test property");
  g_assert_nonnull (pod);
  g_assert_true (spa_pod_is_string (pod));

  wp_spa_props_clear (&props);
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
