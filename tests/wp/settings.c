/* WirePlumber
 *
 * Copyright © 2022 Collabora Ltd.
 *    @author Ashok Sidipotu <ashok.sidipotu@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */
#include "../common/base-test-fixture.h"

typedef struct {
  WpBaseTestFixture base;

  WpProperties *loaded_settings;
  WpProperties *loaded_schema;

  WpImplMetadata *metadata;
  WpImplMetadata *metadata_schema;
  WpImplMetadata *metadata_persistent;

  WpSettings *settings;

  gchar *triggered_setting;
  WpSpaJson *triggered_setting_value;
  gboolean triggered_callback;
} TestSettingsFixture;

static void
test_conf_file_setup (TestSettingsFixture *self, gconstpointer user_data)
{
  self->base.conf_file = g_strdup_printf ("%s/settings/wireplumber.conf",
      g_getenv ("G_TEST_SRCDIR"));

  wp_base_test_fixture_setup (&self->base, WP_BASE_TEST_FLAG_CLIENT_CORE);
}

static void
test_conf_file_teardown (TestSettingsFixture *self, gconstpointer user_data)
{
  wp_base_test_fixture_teardown (&self->base);
}

static WpProperties *
do_parse_section (WpSpaJson *json)
{
  g_autoptr (WpProperties) settings = wp_properties_new_empty ();
  g_autoptr (WpIterator) iter = wp_spa_json_new_iterator (json);
  g_auto (GValue) item = G_VALUE_INIT;

  if (!wp_spa_json_is_object (json))
    return NULL;

  while (wp_iterator_next (iter, &item)) {
    WpSpaJson *j = g_value_get_boxed (&item);
    g_autofree gchar *name = wp_spa_json_parse_string (j);
    g_autofree gchar *value = NULL;

    g_value_unset (&item);
    wp_iterator_next (iter, &item);
    j = g_value_get_boxed (&item);

    value = wp_spa_json_to_string (j);
    g_value_unset (&item);

    if (name && value)
      wp_properties_set (settings, name, value);
  }

  return g_steal_pointer (&settings);
}

static void
test_parsing_setup (TestSettingsFixture *self, gconstpointer user_data)
{
  test_conf_file_setup (self, user_data);

  g_autoptr (WpConf) conf = wp_conf_get_instance (self->base.core);
  g_assert_nonnull (conf);

  {
    g_autoptr (WpSpaJson) json = wp_conf_get_section (conf,
        "wireplumber.settings", NULL);
    g_assert_nonnull (json);
    self->loaded_settings = do_parse_section (json);
    g_assert_nonnull (self->loaded_settings);
  }

  {
    g_autoptr (WpSpaJson) json = wp_conf_get_section (conf,
        "wireplumber.settings.schema", NULL);
    self->loaded_schema = do_parse_section (json);
    g_assert_nonnull (self->loaded_schema);
  }
}

static void
test_parsing_teardown (TestSettingsFixture *self, gconstpointer user_data)
{
  g_clear_pointer (&self->loaded_settings, wp_properties_unref);
  g_clear_pointer (&self->loaded_schema, wp_properties_unref);

  test_conf_file_teardown (self, user_data);
}

static void
on_metadata_persistent_activated (WpMetadata * m, GAsyncResult * res,
    gpointer user_data)
{
  TestSettingsFixture *self = user_data;

  g_assert_true (wp_object_activate_finish (WP_OBJECT (m), res, NULL));

  g_main_loop_quit (self->base.loop);
}

static void
on_metadata_schema_activated (WpMetadata * m, GAsyncResult * res,
    gpointer user_data)
{
  TestSettingsFixture *self = user_data;
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) item = G_VALUE_INIT;

  g_assert_true (wp_object_activate_finish (WP_OBJECT (m), res, NULL));

  for (it = wp_properties_new_iterator (self->loaded_schema);
      wp_iterator_next (it, &item);
      g_value_unset (&item)) {
    WpPropertiesItem *pi = g_value_get_boxed (&item);
    const gchar *key = wp_properties_item_get_key (pi);
    const gchar *value = wp_properties_item_get_value (pi);

    wp_metadata_set (m, 0, key, "Spa:String:JSON", value);
  }

  self->metadata_persistent = wp_impl_metadata_new_full (self->base.core,
      WP_SETTINGS_PERSISTENT_METADATA_NAME_PREFIX "sm-settings", NULL);

  wp_object_activate (WP_OBJECT (self->metadata_persistent),
      WP_OBJECT_FEATURES_ALL,
      NULL,
      (GAsyncReadyCallback)on_metadata_persistent_activated,
      self);
}

static void
on_metadata_activated (WpMetadata * m, GAsyncResult * res, gpointer user_data)
{
  TestSettingsFixture *self = user_data;
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) item = G_VALUE_INIT;

  g_assert_true (wp_object_activate_finish (WP_OBJECT (m), res, NULL));

  for (it = wp_properties_new_iterator (self->loaded_settings);
        wp_iterator_next (it, &item);
        g_value_unset (&item)) {
    WpPropertiesItem *pi = g_value_get_boxed (&item);
    const gchar *key = wp_properties_item_get_key (pi);
    const gchar *value = wp_properties_item_get_value (pi);

    wp_metadata_set (m, 0, key, "Spa:String:JSON", value);
  }

  self->metadata_schema = wp_impl_metadata_new_full (self->base.core,
      WP_SETTINGS_SCHEMA_METADATA_NAME_PREFIX "sm-settings", NULL);

  wp_object_activate (WP_OBJECT (self->metadata_schema),
      WP_OBJECT_FEATURES_ALL,
      NULL,
      (GAsyncReadyCallback)on_metadata_schema_activated,
      self);
}

static void
test_metadata_setup (TestSettingsFixture *self, gconstpointer user_data)
{
  test_parsing_setup (self, user_data);

  self->metadata = wp_impl_metadata_new_full (self->base.core, "sm-settings",
      NULL);

  wp_object_activate (WP_OBJECT (self->metadata),
      WP_OBJECT_FEATURES_ALL,
      NULL,
      (GAsyncReadyCallback)on_metadata_activated,
      self);

  g_main_loop_run (self->base.loop);
}

static void
test_metadata_teardown (TestSettingsFixture *self, gconstpointer user_data)
{
  test_parsing_teardown (self, user_data);

  g_clear_object (&self->metadata);
  g_clear_object (&self->metadata_schema);
  g_clear_object (&self->metadata_persistent);
}

static void
on_settings_ready (WpSettings *s, GAsyncResult *res, gpointer data)
{
  TestSettingsFixture *self = data;

  g_assert_true (wp_object_activate_finish (WP_OBJECT (s), res, NULL));

  wp_core_register_object (self->base.core, g_object_ref (s));

  g_main_loop_quit (self->base.loop);
}

static void
test_settings_setup (TestSettingsFixture *self, gconstpointer user_data)
{
  test_metadata_setup (self, user_data);

  self->settings = wp_settings_new (self->base.core, "sm-settings");
  wp_object_activate (WP_OBJECT (self->settings),
      WP_OBJECT_FEATURES_ALL,
      NULL,
      (GAsyncReadyCallback)on_settings_ready,
      self);
  g_main_loop_run (self->base.loop);
}

static void
test_settings_teardown (TestSettingsFixture *self, gconstpointer user_data)
{
  test_metadata_teardown (self, user_data);
  g_clear_object (&self->settings);
}

static void
test_basic (TestSettingsFixture *self, gconstpointer data)
{
  /* Find */
  {
    g_autoptr (WpSettings) s1 =
        wp_settings_find (self->base.core, NULL);
    g_autoptr (WpSettings) s2 =
        wp_settings_find (self->base.core, "sm-settings");
    g_autoptr (WpSettings) s3 =
        wp_settings_find (self->base.core, "blah-blah");

    g_assert_true (self->settings == s1);
    g_assert_true (s1 == s2);
    g_assert_false (s1 == s3);
    g_assert_null (s3);

    g_autoptr (WpSettings) s4 = wp_settings_find (self->base.core, NULL);
    g_auto (GValue) value = G_VALUE_INIT;
    g_object_get_property (G_OBJECT(s4), "metadata-name", &value);
    g_assert_cmpstr (g_value_get_string (&value), ==, "sm-settings");
  }

  /* Iterator */
  {
    g_autoptr (WpProperties) settings = wp_properties_new_empty ();
    g_autoptr (WpIterator) it = wp_settings_new_iterator (self->settings);
    g_auto (GValue) val = G_VALUE_INIT;
    for (; wp_iterator_next (it, &val); g_value_unset (&val)) {
      WpSettingsItem *si = g_value_get_boxed (&val);
      const gchar *key = wp_settings_item_get_key (si);
      g_autoptr (WpSpaJson) value = wp_settings_item_get_value (si);
      wp_properties_set (settings, key, wp_spa_json_get_data (value));
    }
    g_assert_true (wp_properties_matches (self->loaded_settings, settings));
  }
}

static void
test_get_set_save_reset_delete (TestSettingsFixture *self, gconstpointer data)
{
  WpSettings *s = self->settings;
  g_autoptr (WpSettingsSpec) spec = NULL;
  const gchar *desc = NULL;
  g_autoptr (WpSpaJson) def = NULL;
  g_autoptr (WpSpaJson) min = NULL;
  g_autoptr (WpSpaJson) max = NULL;
  g_autoptr (WpSpaJson) j = NULL;

  /* Undefined */
  {
    spec = wp_settings_get_spec (s, "test-setting-undefined");
    g_assert_null (spec);

    j = wp_settings_get (s, "test-setting-undefined");
    g_assert_null (j);

    j = wp_spa_json_new_null ();
    g_assert_false (wp_settings_set (s, "test-setting-undefined", j));
    g_clear_pointer (&j, wp_spa_json_unref);
  }

  /* Boolean */
  {
    gboolean value = FALSE;

    spec = wp_settings_get_spec (s, "test-setting-bool");
    desc = wp_settings_spec_get_description (spec);
    g_assert_nonnull (desc);
    g_assert_cmpstr (desc, ==, "test-setting-bool description");
    g_assert_true (
        wp_settings_spec_get_value_type (spec) == WP_SETTINGS_SPEC_TYPE_BOOL);
    def = wp_settings_spec_get_default_value (spec);
    g_assert_nonnull (def);
    g_assert_true (wp_spa_json_parse_boolean (def, &value));
    g_assert_false (value);
    g_clear_pointer (&def, wp_spa_json_unref);
    g_clear_pointer (&spec, wp_settings_spec_unref);

    j = wp_settings_get (s, "test-setting-bool");
    g_assert_nonnull (j);
    g_assert_true (wp_spa_json_parse_boolean (j, &value));
    g_assert_true (value);
    g_clear_pointer (&j, wp_spa_json_unref);

    j = wp_spa_json_new_boolean (FALSE);
    g_assert_true (wp_settings_set (s, "test-setting-bool", j));
    g_clear_pointer (&j, wp_spa_json_unref);
    j = wp_settings_get (s, "test-setting-bool");
    g_assert_true (wp_spa_json_parse_boolean (j, &value));
    g_assert_false (value);
    g_clear_pointer (&j, wp_spa_json_unref);

    j = wp_spa_json_new_boolean (TRUE);
    g_assert_true (wp_settings_set (s, "test-setting-bool", j));
    g_clear_pointer (&j, wp_spa_json_unref);
    j = wp_settings_get (s, "test-setting-bool");
    g_assert_true (wp_spa_json_parse_boolean (j, &value));
    g_assert_true (value);
    g_clear_pointer (&j, wp_spa_json_unref);

    j = wp_spa_json_new_int (1);
    g_assert_false (wp_settings_set (s, "test-setting-bool", j));
    g_clear_pointer (&j, wp_spa_json_unref);
    j = wp_settings_get (s, "test-setting-bool");
    g_assert_true (wp_spa_json_parse_boolean (j, &value));
    g_assert_true (value);
    g_clear_pointer (&j, wp_spa_json_unref);

    j = wp_settings_get_saved (s, "test-setting-bool");
    g_assert_null (j);
    g_assert_true (wp_settings_save (s, "test-setting-bool"));
    g_clear_pointer (&j, wp_spa_json_unref);
    j = wp_settings_get_saved (s, "test-setting-bool");
    g_assert_true (wp_spa_json_parse_boolean (j, &value));
    g_assert_true (value);
    g_clear_pointer (&j, wp_spa_json_unref);
    g_assert_true (wp_settings_delete (s, "test-setting-bool"));
    j = wp_settings_get_saved (s, "test-setting-bool");
    g_assert_null (j);
    g_assert_true (wp_settings_reset (s, "test-setting-bool"));
    j = wp_settings_get (s, "test-setting-bool");
    g_assert_true (wp_spa_json_parse_boolean (j, &value));
    g_assert_false (value);
    g_clear_pointer (&j, wp_spa_json_unref);
  }

  /* Int */
  {
    gint value = 0;

    spec = wp_settings_get_spec (s, "test-setting-int");
    desc = wp_settings_spec_get_description (spec);
    g_assert_nonnull (desc);
    g_assert_cmpstr (desc, ==, "test-setting-int description");
    g_assert_true (
        wp_settings_spec_get_value_type (spec) == WP_SETTINGS_SPEC_TYPE_INT);
    def = wp_settings_spec_get_default_value (spec);
    min = wp_settings_spec_get_min_value (spec);
    max = wp_settings_spec_get_max_value (spec);
    g_assert_nonnull (def);
    g_assert_nonnull (min);
    g_assert_nonnull (max);
    g_assert_true (wp_spa_json_parse_int (def, &value));
    g_assert_cmpint (value, ==, 0);
    g_assert_true (wp_spa_json_parse_int (min, &value));
    g_assert_cmpint (value, ==, -100);
    g_assert_true (wp_spa_json_parse_int (max, &value));
    g_assert_cmpint (value, ==, 100);
    g_clear_pointer (&def, wp_spa_json_unref);
    g_clear_pointer (&min, wp_spa_json_unref);
    g_clear_pointer (&max, wp_spa_json_unref);
    g_clear_pointer (&spec, wp_settings_spec_unref);

    j = wp_settings_get (s, "test-setting-int");
    g_assert_nonnull (j);
    g_assert_true (wp_spa_json_parse_int (j, &value));
    g_assert_cmpint (value, ==, -20);
    g_clear_pointer (&j, wp_spa_json_unref);

    j = wp_spa_json_new_int (3);
    g_assert_true (wp_settings_set (s, "test-setting-int", j));
    g_clear_pointer (&j, wp_spa_json_unref);
    j = wp_settings_get (s, "test-setting-int");
    g_assert_true (wp_spa_json_parse_int (j, &value));
    g_assert_cmpint (value, ==, 3);
    g_clear_pointer (&j, wp_spa_json_unref);

    j = wp_spa_json_new_int (1000);
    g_assert_false (wp_settings_set (s, "test-setting-int", j));
    g_clear_pointer (&j, wp_spa_json_unref);
    j = wp_settings_get (s, "test-setting-int");
    g_assert_true (wp_spa_json_parse_int (j, &value));
    g_assert_cmpint (value, ==, 3);
    g_clear_pointer (&j, wp_spa_json_unref);

    j = wp_spa_json_new_int (-1000);
    g_assert_false (wp_settings_set (s, "test-setting-int", j));
    g_clear_pointer (&j, wp_spa_json_unref);
    j = wp_settings_get (s, "test-setting-int");
    g_assert_true (wp_spa_json_parse_int (j, &value));
    g_assert_cmpint (value, ==, 3);
    g_clear_pointer (&j, wp_spa_json_unref);

    j = wp_spa_json_new_float (50.5);
    g_assert_false (wp_settings_set (s, "test-setting-int", j));
    g_clear_pointer (&j, wp_spa_json_unref);
    j = wp_settings_get (s, "test-setting-int");
    g_assert_true (wp_spa_json_parse_int (j, &value));
    g_assert_cmpint (value, ==, 3);
    g_clear_pointer (&j, wp_spa_json_unref);

    j = wp_settings_get_saved (s, "test-setting-int");
    g_assert_null (j);
    g_assert_true (wp_settings_save (s, "test-setting-int"));
    g_clear_pointer (&j, wp_spa_json_unref);
    j = wp_settings_get_saved (s, "test-setting-int");
    g_assert_true (wp_spa_json_parse_int (j, &value));
    g_assert_cmpint (value, ==, 3);
    g_clear_pointer (&j, wp_spa_json_unref);
    g_assert_true (wp_settings_delete (s, "test-setting-int"));
    j = wp_settings_get_saved (s, "test-setting-int");
    g_assert_null (j);
    g_assert_true (wp_settings_reset (s, "test-setting-int"));
    j = wp_settings_get (s, "test-setting-int");
    g_assert_true (wp_spa_json_parse_int (j, &value));
    g_assert_cmpint (value, ==, 0);
    g_clear_pointer (&j, wp_spa_json_unref);
  }

  /* Float */
  {
    gfloat value = 0.0;

    spec = wp_settings_get_spec (s, "test-setting-float");
    desc = wp_settings_spec_get_description (spec);
    g_assert_nonnull (desc);
    g_assert_cmpstr (desc, ==, "test-setting-float description");
    g_assert_true (
        wp_settings_spec_get_value_type (spec) == WP_SETTINGS_SPEC_TYPE_FLOAT);
    def = wp_settings_spec_get_default_value (spec);
    min = wp_settings_spec_get_min_value (spec);
    max = wp_settings_spec_get_max_value (spec);
    g_assert_nonnull (def);
    g_assert_nonnull (min);
    g_assert_nonnull (max);
    g_assert_true (wp_spa_json_parse_float (def, &value));
    g_assert_cmpfloat_with_epsilon (value, 0.0, 0.001);
    g_assert_true (wp_spa_json_parse_float (min, &value));
    g_assert_cmpfloat_with_epsilon (value, -100.0, 0.001);
    g_assert_true (wp_spa_json_parse_float (max, &value));
    g_assert_cmpfloat_with_epsilon (value, 100.0, 0.001);
    g_clear_pointer (&def, wp_spa_json_unref);
    g_clear_pointer (&min, wp_spa_json_unref);
    g_clear_pointer (&max, wp_spa_json_unref);
    g_clear_pointer (&spec, wp_settings_spec_unref);

    j = wp_settings_get (s, "test-setting-float");
    g_assert_nonnull (j);
    g_assert_true (wp_spa_json_parse_float (j, &value));
    g_assert_cmpfloat_with_epsilon (value, 3.14, 0.001);
    g_clear_pointer (&j, wp_spa_json_unref);

    j = wp_spa_json_new_float (99.5);
    g_assert_true (wp_settings_set (s, "test-setting-float", j));
    g_clear_pointer (&j, wp_spa_json_unref);
    j = wp_settings_get (s, "test-setting-float");
    g_assert_true (wp_spa_json_parse_float (j, &value));
    g_assert_cmpfloat_with_epsilon (value, 99.5, 0.001);
    g_clear_pointer (&j, wp_spa_json_unref);

    j = wp_spa_json_new_float (150.5);
    g_assert_false (wp_settings_set (s, "test-setting-float", j));
    g_clear_pointer (&j, wp_spa_json_unref);
    j = wp_settings_get (s, "test-setting-float");
    g_assert_true (wp_spa_json_parse_float (j, &value));
    g_assert_cmpfloat_with_epsilon (value, 99.5, 0.001);
    g_clear_pointer (&j, wp_spa_json_unref);

    j = wp_spa_json_new_float (-150.5);
    g_assert_false (wp_settings_set (s, "test-setting-float", j));
    g_clear_pointer (&j, wp_spa_json_unref);
    j = wp_settings_get (s, "test-setting-float");
    g_assert_true (wp_spa_json_parse_float (j, &value));
    g_assert_cmpfloat_with_epsilon (value, 99.5, 0.001);
    g_clear_pointer (&j, wp_spa_json_unref);

    j = wp_spa_json_new_int (50);
    g_assert_false (wp_settings_set (s, "test-setting-float", j));
    g_clear_pointer (&j, wp_spa_json_unref);
    j = wp_settings_get (s, "test-setting-float");
    g_assert_true (wp_spa_json_parse_float (j, &value));
    g_assert_cmpfloat_with_epsilon (value, 99.5, 0.001);
    g_clear_pointer (&j, wp_spa_json_unref);

    j = wp_settings_get_saved (s, "test-setting-float");
    g_assert_null (j);
    g_assert_true (wp_settings_save (s, "test-setting-float"));
    g_clear_pointer (&j, wp_spa_json_unref);
    j = wp_settings_get_saved (s, "test-setting-float");
    g_assert_true (wp_spa_json_parse_float (j, &value));
    g_assert_cmpfloat_with_epsilon (value, 99.5, 0.001);
    g_clear_pointer (&j, wp_spa_json_unref);
    g_assert_true (wp_settings_delete (s, "test-setting-float"));
    j = wp_settings_get_saved (s, "test-setting-float");
    g_assert_null (j);
    g_assert_true (wp_settings_reset (s, "test-setting-float"));
    j = wp_settings_get (s, "test-setting-float");
    g_assert_true (wp_spa_json_parse_float (j, &value));
    g_assert_cmpfloat_with_epsilon (value, 0.0, 0.001);
    g_clear_pointer (&j, wp_spa_json_unref);
  }

  /* String */
  {
    g_autofree gchar *value = NULL;

    {
      spec = wp_settings_get_spec (s, "test-setting-string");
      desc = wp_settings_spec_get_description (spec);
      g_assert_nonnull (desc);
      g_assert_cmpstr (desc, ==, "test-setting-string description");
      g_assert_true (wp_settings_spec_get_value_type (spec) ==
          WP_SETTINGS_SPEC_TYPE_STRING);
      def = wp_settings_spec_get_default_value (spec);
      g_assert_nonnull (def);
      value = wp_spa_json_parse_string (def);
      g_assert_nonnull (value);
      g_assert_cmpstr (value, ==, "default");
      g_clear_pointer (&value, g_free);
      g_clear_pointer (&def, wp_spa_json_unref);
      g_clear_pointer (&spec, wp_settings_spec_unref);

      j = wp_settings_get (s, "test-setting-string");
      g_assert_nonnull (j);
      value = wp_spa_json_parse_string (j);
      g_assert_nonnull (value);
      g_assert_cmpstr (value, ==, "blahblah");
      g_clear_pointer (&value, g_free);
      g_clear_pointer (&j, wp_spa_json_unref);

      j = wp_spa_json_new_string ("new-string-value");
      g_assert_true (wp_settings_set (s, "test-setting-string", j));
      g_clear_pointer (&j, wp_spa_json_unref);
      j = wp_settings_get (s, "test-setting-string");
      value = wp_spa_json_parse_string (j);
      g_assert_nonnull (value);
      g_assert_cmpstr (value, ==, "new-string-value");
      g_clear_pointer (&value, g_free);
      g_clear_pointer (&j, wp_spa_json_unref);

      j = wp_spa_json_new_int (99);
      g_assert_false (wp_settings_set (s, "test-setting-string", j));
      g_clear_pointer (&j, wp_spa_json_unref);
      j = wp_settings_get (s, "test-setting-string");
      value = wp_spa_json_parse_string (j);
      g_assert_nonnull (value);
      g_assert_cmpstr (value, ==, "new-string-value");
      g_clear_pointer (&value, g_free);
      g_clear_pointer (&j, wp_spa_json_unref);

      j = wp_spa_json_new_string ("99");
      g_assert_true (wp_settings_set (s, "test-setting-string", j));
      g_clear_pointer (&j, wp_spa_json_unref);
      j = wp_settings_get (s, "test-setting-string");
      value = wp_spa_json_parse_string (j);
      g_assert_nonnull (value);
      g_assert_cmpstr (value, ==, "99");
      g_clear_pointer (&value, g_free);
      g_clear_pointer (&j, wp_spa_json_unref);

      j = wp_settings_get_saved (s, "test-setting-string");
      g_assert_null (j);
      g_assert_true (wp_settings_save (s, "test-setting-string"));
      g_clear_pointer (&j, wp_spa_json_unref);
      j = wp_settings_get_saved (s, "test-setting-string");
      value = wp_spa_json_parse_string (j);
      g_assert_nonnull (value);
      g_assert_cmpstr (value, ==, "99");
      g_clear_pointer (&value, g_free);
      g_clear_pointer (&j, wp_spa_json_unref);
      g_assert_true (wp_settings_delete (s, "test-setting-string"));
      j = wp_settings_get_saved (s, "test-setting-string");
      g_assert_null (j);
      g_assert_true (wp_settings_reset (s, "test-setting-string"));
      j = wp_settings_get (s, "test-setting-string");
      value = wp_spa_json_parse_string (j);
      g_assert_nonnull (value);
      g_assert_cmpstr (value, ==, "default");
      g_clear_pointer (&value, g_free);
      g_clear_pointer (&j, wp_spa_json_unref);
    }

    {
      spec = wp_settings_get_spec (s, "test-setting-string");
      desc = wp_settings_spec_get_description (spec);
      g_assert_nonnull (desc);
      g_assert_cmpstr (desc, ==, "test-setting-string description");
      g_assert_true (wp_settings_spec_get_value_type (spec) ==
          WP_SETTINGS_SPEC_TYPE_STRING);
      def = wp_settings_spec_get_default_value (spec);
      g_assert_nonnull (def);
      value = wp_spa_json_parse_string (def);
      g_assert_nonnull (value);
      g_assert_cmpstr (value, ==, "default");
      g_clear_pointer (&value, g_free);
      g_clear_pointer (&def, wp_spa_json_unref);
      g_clear_pointer (&spec, wp_settings_spec_unref);

      j = wp_settings_get (s, "test-setting-string2");
      g_assert_nonnull (j);
      value = wp_spa_json_parse_string (j);
      g_assert_nonnull (value);
      g_assert_cmpstr (value, ==, "a string with \"quotes\"");
      g_clear_pointer (&value, g_free);
      g_clear_pointer (&j, wp_spa_json_unref);

      j = wp_spa_json_new_string ("a new string with \"quotes\"");
      g_assert_true (wp_settings_set (s, "test-setting-string2", j));
      g_clear_pointer (&j, wp_spa_json_unref);
      j = wp_settings_get (s, "test-setting-string2");
      value = wp_spa_json_parse_string (j);
      g_assert_nonnull (value);
      g_assert_cmpstr (value, ==, "a new string with \"quotes\"");
      g_clear_pointer (&value, g_free);
      g_clear_pointer (&j, wp_spa_json_unref);

      j = wp_spa_json_new_float (99.5);
      g_assert_false (wp_settings_set (s, "test-setting-string2", j));
      g_clear_pointer (&j, wp_spa_json_unref);
      j = wp_settings_get (s, "test-setting-string2");
      value = wp_spa_json_parse_string (j);
      g_assert_nonnull (value);
      g_assert_cmpstr (value, ==, "a new string with \"quotes\"");
      g_clear_pointer (&value, g_free);
      g_clear_pointer (&j, wp_spa_json_unref);

      j = wp_spa_json_new_string ("99.5");
      g_assert_true (wp_settings_set (s, "test-setting-string2", j));
      g_clear_pointer (&j, wp_spa_json_unref);
      j = wp_settings_get (s, "test-setting-string2");
      value = wp_spa_json_parse_string (j);
      g_assert_nonnull (value);
      g_assert_cmpstr (value, ==, "99.5");
      g_clear_pointer (&value, g_free);
      g_clear_pointer (&j, wp_spa_json_unref);

      j = wp_settings_get_saved (s, "test-setting-string2");
      g_assert_null (j);
      g_assert_true (wp_settings_save (s, "test-setting-string2"));
      g_clear_pointer (&j, wp_spa_json_unref);
      j = wp_settings_get_saved (s, "test-setting-string2");
      value = wp_spa_json_parse_string (j);
      g_assert_nonnull (value);
      g_assert_cmpstr (value, ==, "99.5");
      g_clear_pointer (&value, g_free);
      g_clear_pointer (&j, wp_spa_json_unref);
      g_assert_true (wp_settings_delete (s, "test-setting-string2"));
      j = wp_settings_get_saved (s, "test-setting-string2");
      g_assert_null (j);
      g_assert_true (wp_settings_reset (s, "test-setting-string2"));
      j = wp_settings_get (s, "test-setting-string2");
      value = wp_spa_json_parse_string (j);
      g_assert_nonnull (value);
      g_assert_cmpstr (value, ==, "default");
      g_clear_pointer (&value, g_free);
      g_clear_pointer (&j, wp_spa_json_unref);
    }
  }

  /* Array */
  {
    {
      spec = wp_settings_get_spec (s, "test-setting-array");
      desc = wp_settings_spec_get_description (spec);
      g_assert_nonnull (desc);
      g_assert_cmpstr (desc, ==, "test-setting-array description");
      g_assert_true (wp_settings_spec_get_value_type (spec) ==
          WP_SETTINGS_SPEC_TYPE_ARRAY);
      def = wp_settings_spec_get_default_value (spec);
      g_assert_nonnull (def);
      g_assert_cmpstr (wp_spa_json_get_data (def), ==, "[]");
      g_clear_pointer (&def, wp_spa_json_unref);
      g_clear_pointer (&spec, wp_settings_spec_unref);

      j = wp_settings_get (s, "test-setting-array");
      g_assert_nonnull (j);
      g_assert_true (wp_spa_json_is_array (j));
      g_assert_cmpstr (wp_spa_json_get_data (j), ==, "[1, 2, 3]");
      g_clear_pointer (&j, wp_spa_json_unref);

      j = wp_spa_json_new_array ("i", 4, "i", 5, NULL);
      g_assert_true (wp_settings_set (s, "test-setting-array", j));
      g_clear_pointer (&j, wp_spa_json_unref);
      j = wp_settings_get (s, "test-setting-array");
      g_assert_nonnull (j);
      g_assert_true (wp_spa_json_is_array (j));
      g_assert_cmpstr (wp_spa_json_get_data (j), ==, "[4, 5]");
      g_clear_pointer (&j, wp_spa_json_unref);

      j = wp_spa_json_new_int (6);
      g_assert_false (wp_settings_set (s, "test-setting-array", j));
      g_clear_pointer (&j, wp_spa_json_unref);
      j = wp_settings_get (s, "test-setting-array");
      g_assert_nonnull (j);
      g_assert_true (wp_spa_json_is_array (j));
      g_assert_cmpstr (wp_spa_json_get_data (j), ==, "[4, 5]");
      g_clear_pointer (&j, wp_spa_json_unref);

      j = wp_settings_get_saved (s, "test-setting-array");
      g_assert_null (j);
      g_assert_true (wp_settings_save (s, "test-setting-array"));
      g_clear_pointer (&j, wp_spa_json_unref);
      j = wp_settings_get_saved (s, "test-setting-array");
      g_assert_cmpstr (wp_spa_json_get_data (j), ==, "[4, 5]");
      g_clear_pointer (&j, wp_spa_json_unref);
      g_assert_true (wp_settings_delete (s, "test-setting-array"));
      j = wp_settings_get_saved (s, "test-setting-array");
      g_assert_null (j);
      g_assert_true (wp_settings_reset (s, "test-setting-array"));
      j = wp_settings_get (s, "test-setting-array");
      g_assert_cmpstr (wp_spa_json_get_data (j), ==, "[]");
      g_clear_pointer (&j, wp_spa_json_unref);
    }

    {
      spec = wp_settings_get_spec (s, "test-setting-array2");
      desc = wp_settings_spec_get_description (spec);
      g_assert_nonnull (desc);
      g_assert_cmpstr (desc, ==, "test-setting-array2 description");
      g_assert_true (wp_settings_spec_get_value_type (spec) ==
          WP_SETTINGS_SPEC_TYPE_ARRAY);
      def = wp_settings_spec_get_default_value (spec);
      g_assert_nonnull (def);
      g_assert_cmpstr (wp_spa_json_get_data (def), ==, "[]");
      g_clear_pointer (&def, wp_spa_json_unref);
      g_clear_pointer (&spec, wp_settings_spec_unref);

      j = wp_settings_get (s, "test-setting-array2");
      g_assert_nonnull (j);
      g_assert_true (wp_spa_json_is_array (j));
      g_assert_cmpstr (wp_spa_json_get_data (j), ==,
          "[\"test1\", \"test 2\", \"test three\", \"test-four\"]");
      g_clear_pointer (&j, wp_spa_json_unref);

      j = wp_spa_json_new_array ("s", "foo", "s", "bar", NULL);
      g_assert_true (wp_settings_set (s, "test-setting-array2", j));
      g_clear_pointer (&j, wp_spa_json_unref);
      j = wp_settings_get (s, "test-setting-array2");
      g_assert_nonnull (j);
      g_assert_true (wp_spa_json_is_array (j));
      g_assert_cmpstr (wp_spa_json_get_data (j), ==, "[\"foo\", \"bar\"]");
      g_clear_pointer (&j, wp_spa_json_unref);

      j = wp_spa_json_new_string ("value");
      g_assert_false (wp_settings_set (s, "test-setting-array2", j));
      g_clear_pointer (&j, wp_spa_json_unref);
      j = wp_settings_get (s, "test-setting-array2");
      g_assert_nonnull (j);
      g_assert_true (wp_spa_json_is_array (j));
      g_assert_cmpstr (wp_spa_json_get_data (j), ==, "[\"foo\", \"bar\"]");
      g_clear_pointer (&j, wp_spa_json_unref);

      j = wp_settings_get_saved (s, "test-setting-array2");
      g_assert_null (j);
      g_assert_true (wp_settings_save (s, "test-setting-array2"));
      g_clear_pointer (&j, wp_spa_json_unref);
      j = wp_settings_get_saved (s, "test-setting-array2");
      g_assert_cmpstr (wp_spa_json_get_data (j), ==, "[\"foo\", \"bar\"]");
      g_clear_pointer (&j, wp_spa_json_unref);
      g_assert_true (wp_settings_delete (s, "test-setting-array2"));
      j = wp_settings_get_saved (s, "test-setting-array2");
      g_assert_null (j);
      g_assert_true (wp_settings_reset (s, "test-setting-array2"));
      j = wp_settings_get (s, "test-setting-array2");
      g_assert_cmpstr (wp_spa_json_get_data (j), ==, "[]");
      g_clear_pointer (&j, wp_spa_json_unref);
    }
  }

  /* Object */
  {
    spec = wp_settings_get_spec (s, "test-setting-object");
    desc = wp_settings_spec_get_description (spec);
    g_assert_nonnull (desc);
    g_assert_cmpstr (desc, ==, "test-setting-object description");
    g_assert_true (wp_settings_spec_get_value_type (spec) ==
        WP_SETTINGS_SPEC_TYPE_OBJECT);
    def = wp_settings_spec_get_default_value (spec);
    g_assert_nonnull (def);
    g_assert_cmpstr (wp_spa_json_get_data (def), ==, "{}");
    g_clear_pointer (&def, wp_spa_json_unref);
    g_clear_pointer (&spec, wp_settings_spec_unref);

    j = wp_settings_get (s, "test-setting-object");
    g_assert_nonnull (j);
    g_assert_true (wp_spa_json_is_object (j));
    g_assert_cmpstr (wp_spa_json_get_data(j), ==,
      "{ key1: \"value\", key2: 2, key3: true }");
    g_clear_pointer (&j, wp_spa_json_unref);

    j = wp_spa_json_new_object (
        "key1", "s", "new-value",
        "key2", "i", 5,
        "key4", "b", FALSE,
        NULL);
    g_assert_true (wp_settings_set (s, "test-setting-object", j));
    g_clear_pointer (&j, wp_spa_json_unref);
    j = wp_settings_get (s, "test-setting-object");
    g_assert_nonnull (j);
    g_assert_true (wp_spa_json_is_object (j));
    g_assert_cmpstr (wp_spa_json_get_data (j), ==,
        "{\"key1\":\"new-value\", \"key2\":5, \"key4\":false}");
    g_clear_pointer (&j, wp_spa_json_unref);

    j = wp_spa_json_new_string ("value");
    g_assert_false (wp_settings_set (s, "test-setting-object", j));
    g_clear_pointer (&j, wp_spa_json_unref);
    j = wp_settings_get (s, "test-setting-object");
    g_assert_nonnull (j);
    g_assert_true (wp_spa_json_is_object (j));
    g_assert_cmpstr (wp_spa_json_get_data (j), ==,
        "{\"key1\":\"new-value\", \"key2\":5, \"key4\":false}");
    g_clear_pointer (&j, wp_spa_json_unref);

    j = wp_settings_get_saved (s, "test-setting-object");
    g_assert_null (j);
    g_assert_true (wp_settings_save (s, "test-setting-object"));
    g_clear_pointer (&j, wp_spa_json_unref);
    j = wp_settings_get_saved (s, "test-setting-object");
    g_assert_cmpstr (wp_spa_json_get_data (j), ==,
        "{\"key1\":\"new-value\", \"key2\":5, \"key4\":false}");
    g_clear_pointer (&j, wp_spa_json_unref);
    g_assert_true (wp_settings_delete (s, "test-setting-object"));
    j = wp_settings_get_saved (s, "test-setting-object");
    g_assert_null (j);
    g_assert_true (wp_settings_reset (s, "test-setting-object"));
    j = wp_settings_get (s, "test-setting-object");
    g_assert_cmpstr (wp_spa_json_get_data (j), ==, "{}");
    g_clear_pointer (&j, wp_spa_json_unref);
  }
}

static void
test_save_reset_delete_all (TestSettingsFixture *self, gconstpointer data)
{
  WpSettings *s = self->settings;

  /* Check settings are the same as the loaded ones, and not saved */
  {
    g_autoptr (WpProperties) props = wp_properties_new_empty ();
    g_autoptr (WpIterator) it = wp_settings_new_iterator (s);
    g_auto (GValue) val = G_VALUE_INIT;
    for (; wp_iterator_next (it, &val); g_value_unset (&val)) {
      WpSettingsItem *si = g_value_get_boxed (&val);
      const gchar *key = wp_settings_item_get_key (si);
      g_autoptr (WpSpaJson) value = wp_settings_item_get_value (si);
      g_autoptr (WpSpaJson) saved = wp_settings_get_saved (s, key);
      g_assert_null (saved);
      wp_properties_set (props, key, wp_spa_json_get_data (value));
    }
    g_assert_true (wp_properties_matches (self->loaded_settings, props));
  }

  /* Reset all settings to default value */
  wp_settings_reset_all (self->settings);

  /* Check all settings are set to their default values */
  {
    gint n_settings = 0;
    g_autoptr (WpIterator) it = wp_settings_new_iterator (s);
    g_auto (GValue) val = G_VALUE_INIT;
    for (; wp_iterator_next (it, &val); g_value_unset (&val)) {
      WpSettingsItem *si = g_value_get_boxed (&val);
      const gchar *key = wp_settings_item_get_key (si);
      g_autoptr (WpSpaJson) value = wp_settings_item_get_value (si);
      g_autoptr (WpSettingsSpec) spec = NULL;
      g_autoptr (WpSpaJson) def = NULL;
      spec = wp_settings_get_spec (s, key);
      g_assert_nonnull (spec);
      def = wp_settings_spec_get_default_value (spec);
      g_assert_cmpstr (wp_spa_json_get_data (def), ==,
          wp_spa_json_get_data (value));
      n_settings++;
    }
    g_assert_cmpint (n_settings, ==,
        wp_properties_get_count (self->loaded_settings));
  }

  /* Save all settings */
  wp_settings_save_all (self->settings);

  /* Check all settings are saved */
  {
    g_autoptr (WpIterator) it = wp_settings_new_iterator (s);
    g_auto (GValue) val = G_VALUE_INIT;
    for (; wp_iterator_next (it, &val); g_value_unset (&val)) {
      WpSettingsItem *si = g_value_get_boxed (&val);
      const gchar *key = wp_settings_item_get_key (si);
      g_autoptr (WpSpaJson) saved = wp_settings_get_saved (s, key);
      g_assert_nonnull (saved);
    }
  }

  /* Delete all saved settings */
  wp_settings_delete_all (self->settings);

  /* Check all settings are not saved anymore */
  {
    g_autoptr (WpIterator) it = wp_settings_new_iterator (s);
    g_auto (GValue) val = G_VALUE_INIT;
    for (; wp_iterator_next (it, &val); g_value_unset (&val)) {
      WpSettingsItem *si = g_value_get_boxed (&val);
      const gchar *key = wp_settings_item_get_key (si);
      g_autoptr (WpSpaJson) saved = wp_settings_get_saved (s, key);
      g_assert_null (saved);
    }
  }
}

static void
wp_settings_changed_callback (WpSettings *s, const gchar *key,
    WpSpaJson *value, gpointer user_data)
{
  TestSettingsFixture *self = user_data;
  g_autoptr (WpSettingsSpec) spec = NULL;

  g_assert_cmpstr (key, ==, self->triggered_setting);
  g_assert_nonnull (value);

  self->triggered_callback = TRUE;

  spec = wp_settings_get_spec (s, key);
  g_assert_nonnull (spec);

  switch (wp_settings_spec_get_value_type (spec)) {
    case WP_SETTINGS_SPEC_TYPE_BOOL: {
      gboolean val = FALSE, expected = FALSE;
      g_assert_true (wp_spa_json_parse_boolean (value, &val));
      g_assert_true (wp_spa_json_parse_boolean (self->triggered_setting_value,
          &expected));
      g_assert_true (val == expected);
      break;
    }
    case WP_SETTINGS_SPEC_TYPE_INT: {
      gint val = 0, expected = 0;
      g_assert_true (wp_spa_json_parse_int (value, &val));
      g_assert_true (wp_spa_json_parse_int (self->triggered_setting_value,
          &expected));
      g_assert_cmpint (val, ==, expected);
      break;
    }
    case WP_SETTINGS_SPEC_TYPE_FLOAT: {
      gfloat val = 0.0, expected = 0.0;
      g_assert_true (wp_spa_json_parse_float (value, &val));
      g_assert_true (wp_spa_json_parse_float (self->triggered_setting_value,
          &expected));
      g_assert_cmpfloat_with_epsilon (val, expected, 0.001);
      break;
    }
    case WP_SETTINGS_SPEC_TYPE_STRING: {
      g_autofree gchar *val = wp_spa_json_parse_string (value);
      g_autofree gchar *expected = wp_spa_json_parse_string (
          self->triggered_setting_value);
      g_assert_nonnull (value);
      g_assert_nonnull (expected);
      g_assert_cmpstr (val, ==, expected);
      break;
    }
    case WP_SETTINGS_SPEC_TYPE_ARRAY: {
      g_assert_cmpstr (wp_spa_json_get_data (value), ==,
          wp_spa_json_get_data (self->triggered_setting_value));
      break;
    }
    case WP_SETTINGS_SPEC_TYPE_OBJECT: {
      g_assert_cmpstr (wp_spa_json_get_data (value), ==,
          wp_spa_json_get_data (self->triggered_setting_value));
      break;
    }
    default:
      g_assert_not_reached ();
  }
}

static void
test_subscribe_unsibscribe (TestSettingsFixture *self, gconstpointer data)
{
  WpSettings *s = self->settings;
  g_autoptr (WpSpaJson) json = NULL;
  guintptr sub_id;

  /* Boolean */
  {
    sub_id = wp_settings_subscribe (s, "test*",
        wp_settings_changed_callback, (gpointer)self);

    json = wp_spa_json_new_boolean (FALSE);
    self->triggered_setting = "test-setting-bool";
    self->triggered_setting_value = json;
    self->triggered_callback = FALSE;
    g_assert_true (wp_settings_set (s, self->triggered_setting, json));
    g_assert_true (self->triggered_callback);
    g_clear_pointer (&json, wp_spa_json_unref);

    wp_settings_unsubscribe (s, sub_id);

    json = wp_spa_json_new_boolean (TRUE);
    self->triggered_setting = "test-setting-bool";
    self->triggered_setting_value = json;
    self->triggered_callback = FALSE;
    wp_settings_set (s, self->triggered_setting, json);
    g_assert_false (self->triggered_callback);
    g_clear_pointer (&json, wp_spa_json_unref);
  }

  /* Int */
  {
    sub_id = wp_settings_subscribe (s, "test*",
        wp_settings_changed_callback, (gpointer)self);

    json = wp_spa_json_new_int (99);
    self->triggered_setting = "test-setting-int";
    self->triggered_setting_value = json;
    self->triggered_callback = FALSE;
    g_assert_true (wp_settings_set (s, self->triggered_setting, json));
    g_assert_true (self->triggered_callback);
    g_clear_pointer (&json, wp_spa_json_unref);

    wp_settings_unsubscribe (s, sub_id);

    json = wp_spa_json_new_int (90);
    self->triggered_setting = "test-setting-int";
    self->triggered_setting_value = json;
    self->triggered_callback = FALSE;
    g_assert_true (wp_settings_set (s, self->triggered_setting, json));
    g_assert_false (self->triggered_callback);
    g_clear_pointer (&json, wp_spa_json_unref);
  }

  /* Float */
  {
    sub_id = wp_settings_subscribe (s, "test*",
        wp_settings_changed_callback, (gpointer)self);

    json = wp_spa_json_new_float (45.5);
    self->triggered_setting = "test-setting-float";
    self->triggered_setting_value = json;
    self->triggered_callback = FALSE;
    g_assert_true (wp_settings_set (s, self->triggered_setting, json));
    g_assert_true (self->triggered_callback);
    g_clear_pointer (&json, wp_spa_json_unref);

    wp_settings_unsubscribe (s, sub_id);

    json = wp_spa_json_new_float (30.5);
    self->triggered_setting = "test-setting-float";
    self->triggered_setting_value = json;
    self->triggered_callback = FALSE;
    g_assert_true (wp_settings_set (s, self->triggered_setting, json));
    g_assert_false (self->triggered_callback);
    g_clear_pointer (&json, wp_spa_json_unref);
  }

  /* String */
  {
    sub_id = wp_settings_subscribe (s, "test*",
        wp_settings_changed_callback, (gpointer)self);

    json = wp_spa_json_new_string ("lets not blabber");
    self->triggered_setting = "test-setting-string";
    self->triggered_setting_value = json;
    self->triggered_callback = FALSE;
    g_assert_true (wp_settings_set (s, self->triggered_setting, json));
    g_assert_true (self->triggered_callback);
    g_clear_pointer (&json, wp_spa_json_unref);

    wp_settings_unsubscribe (s, sub_id);

    json = wp_spa_json_new_string ("lets blabber");
    self->triggered_setting = "test-setting-string";
    self->triggered_setting_value = json;
    self->triggered_callback = FALSE;
    g_assert_true (wp_settings_set (s, self->triggered_setting, json));
    g_assert_false (self->triggered_callback);
    g_clear_pointer (&json, wp_spa_json_unref);
  }


  /* Array */
  {
    sub_id = wp_settings_subscribe (s, "test*",
        wp_settings_changed_callback, (gpointer)self);

    json = wp_spa_json_new_array ("i", 4, "i", 5, NULL);
    self->triggered_setting = "test-setting-array";
    self->triggered_setting_value = json;
    self->triggered_callback = FALSE;
    g_assert_true (wp_settings_set (s, self->triggered_setting, json));
    g_assert_true (self->triggered_callback);
    g_clear_pointer (&json, wp_spa_json_unref);

    wp_settings_unsubscribe (s, sub_id);

    json = wp_spa_json_new_array ("i", 6, "i", 7, NULL);
    self->triggered_setting = "test-setting-array";
    self->triggered_setting_value = json;
    self->triggered_callback = FALSE;
    g_assert_true (wp_settings_set (s, self->triggered_setting, json));
    g_assert_false (self->triggered_callback);
    g_clear_pointer (&json, wp_spa_json_unref);
  }

  /* Object */
  {
    sub_id = wp_settings_subscribe (s, "test*",
        wp_settings_changed_callback, (gpointer)self);

    json = wp_spa_json_new_object (
        "key1", "s", "value",
        "key2", "i", 3,
        "key4", "b", TRUE,
        NULL);
    self->triggered_setting = "test-setting-object";
    self->triggered_setting_value = json;
    self->triggered_callback = FALSE;
    g_assert_true (wp_settings_set (s, self->triggered_setting, json));
    g_assert_true (self->triggered_callback);
    g_clear_pointer (&json, wp_spa_json_unref);

    wp_settings_unsubscribe (s, sub_id);

    json = wp_spa_json_new_object (
        "key1", "f", 1.2,
        NULL);
    self->triggered_setting = "test-setting-object";
    self->triggered_setting_value = json;
    self->triggered_callback = FALSE;
    g_assert_true (wp_settings_set (s, self->triggered_setting, json));
    g_assert_false (self->triggered_callback);
    g_clear_pointer (&json, wp_spa_json_unref);
  }
}

gint
main (gint argc, gchar *argv[])
{
  g_test_init (&argc, &argv, NULL);
  wp_init (WP_INIT_ALL);

  g_test_add ("/wp/settings/basic", TestSettingsFixture, NULL,
      test_settings_setup, test_basic, test_settings_teardown);
  g_test_add ("/wp/settings/get-set-save-reset-delete", TestSettingsFixture, NULL,
      test_settings_setup, test_get_set_save_reset_delete, test_settings_teardown);
  g_test_add ("/wp/settings/save-reset-delete-all", TestSettingsFixture, NULL,
      test_settings_setup, test_save_reset_delete_all, test_settings_teardown);
  g_test_add ("/wp/settings/subscribe-unsubscribe", TestSettingsFixture, NULL,
      test_settings_setup, test_subscribe_unsibscribe, test_settings_teardown);

  return g_test_run ();
}
