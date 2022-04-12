/* WirePlumber
 *
 * Copyright © 2022 Collabora Ltd.
 *    @author Ashok Sidipotu <ashok.sidipotu@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */
#include "../common/base-test-fixture.h"

/*
 * tests the loading & parsing of JSON conf file(pls check the settings.conf),
 * metadata updates,  wpsetttings object creation and its API.
 */
typedef struct {
  WpBaseTestFixture base;

  WpProperties *settings;

  WpImplMetadata *impl_metadata;
  WpMetadata *metadata;

  WpSettings *s;
} TestSettingsFixture;

static void
test_conf_file_setup (TestSettingsFixture *self, gconstpointer user_data)
{
  self->base.conf_file =
      g_strdup_printf ("%s/settings.conf", g_getenv ("G_TEST_SRCDIR"));

  wp_base_test_fixture_setup (&self->base, WP_BASE_TEST_FLAG_CLIENT_CORE);
}

static void
test_conf_file_teardown (TestSettingsFixture *self, gconstpointer user_data)
{
  g_free (self->base.conf_file);
  wp_base_test_fixture_teardown (&self->base);
}

static int
dummy(void *data, const char *location, const char *section,
		const char *str, size_t len)
{
  return 1;
}

static void
test_conf_file (TestSettingsFixture *self, gconstpointer data)
{
  struct pw_context *pw_ctx = wp_core_get_pw_context (self->base.core);

  /* test if the "settings" section is present in the JSON config file */
  g_assert_true (pw_context_conf_section_for_each(pw_ctx,
      "wireplumber.settings", dummy, NULL));
}

struct data {
  int count;
  WpProperties *settings;
};

static int
do_parse_settings (void *data, const char *location,
    const char *section, const char *str, size_t len)
{
  struct data *d = data;
  g_autoptr (WpSpaJson) json = wp_spa_json_new_from_stringn (str, len);
  g_autoptr (WpIterator) iter = wp_spa_json_new_iterator (json);
  g_auto (GValue) item = G_VALUE_INIT;

  if (!wp_spa_json_is_object (json)) {
    return -EINVAL;
  }

  while (wp_iterator_next (iter, &item)) {
    WpSpaJson *j = g_value_get_boxed (&item);
    g_autofree gchar *name = wp_spa_json_parse_string (j);
    g_autofree gchar *value = NULL;

    g_value_unset (&item);
    wp_iterator_next (iter, &item);
    j = g_value_get_boxed (&item);

    value = wp_spa_json_parse_string (j);
    g_value_unset (&item);

    if (name && value) {
      wp_properties_set (d->settings, name, value);
      d->count++;
    }
  }

  g_debug ("parsed %d settings & rules from conf file\n", d->count);

 return 0;
}

static void
test_parsing_setup (TestSettingsFixture *self, gconstpointer user_data)
{
  test_conf_file_setup (self, user_data);

  {
    struct pw_context *pw_ctx = wp_core_get_pw_context (self->base.core);
    g_autoptr (WpProperties) settings = wp_properties_new_empty();
    struct data data = { .settings = settings };

    g_assert_false (pw_context_conf_section_for_each(pw_ctx,
        "wireplumber.settings", do_parse_settings, &data));

    self->settings = g_steal_pointer (&settings);

    /* total no.of properties in the conf file */
    g_assert_cmpint (data.count, ==, 4);
  }

}

static void
test_parsing_teardown (TestSettingsFixture *self, gconstpointer user_data)
{
  g_clear_pointer (&self->settings, wp_properties_unref);

  test_conf_file_teardown (self, user_data);
}

static void
test_parsing (TestSettingsFixture *self, gconstpointer data)
{
  /* total no.of properties in the conf file */
  g_assert_cmpint (wp_properties_get_count(self->settings), ==, 4);
}

static void
on_metadata_activated (WpMetadata * m, GAsyncResult * res, gpointer user_data)
{
  TestSettingsFixture *self = user_data;
  g_autoptr (GError) error = NULL;
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) item = G_VALUE_INIT;

  g_assert_true(wp_object_activate_finish (WP_OBJECT (m), res, NULL));

  for (it = wp_properties_new_iterator (self->settings);
        wp_iterator_next (it, &item);
        g_value_unset (&item)) {
    WpPropertiesItem *pi = g_value_get_boxed (&item);

    const gchar *setting = wp_properties_item_get_key (pi);
    const gchar *value = wp_properties_item_get_value (pi);

    wp_metadata_set (m, 0, setting, "Spa:String:JSON", value);
  }
  g_debug ("loaded settings(%d) to \"test-settings\" metadata\n",
      wp_properties_get_count (self->settings));

  self->metadata = g_object_ref(m);
  g_main_loop_quit(self->base.loop);
}

static void
test_metadata_setup (TestSettingsFixture *self, gconstpointer user_data)
{
  test_parsing_setup (self, user_data);

  {
    self->impl_metadata = wp_impl_metadata_new_full (self->base.core,
        "test-settings", NULL);

    wp_object_activate (WP_OBJECT (self->impl_metadata),
        WP_OBJECT_FEATURES_ALL,
        NULL,
        (GAsyncReadyCallback)on_metadata_activated,
        self);

    g_main_loop_run (self->base.loop);
  }

}

static void
test_metadata_teardown (TestSettingsFixture *self, gconstpointer user_data)
{
  test_parsing_teardown (self, user_data);

  g_clear_object (&self->impl_metadata);
  g_clear_object (&self->metadata);
}

static void
test_metadata (TestSettingsFixture *self, gconstpointer data)
{
  g_autoptr (WpProperties) settings = wp_properties_new_empty();

  g_autoptr (WpIterator) it = wp_metadata_new_iterator
      (WP_METADATA (self->metadata), 0);
  g_auto (GValue) val = G_VALUE_INIT;

  for (; wp_iterator_next (it, &val); g_value_unset (&val)) {
      const gchar *setting, *value;
      wp_metadata_iterator_item_extract (&val, NULL, &setting, NULL, &value);
      wp_properties_set (settings, setting, value);
      g_debug ("%s(%lu) = %s\n", setting, strlen(value), value);
  }

  /* match the settings loaded from conf file and metadata */
  g_assert_true (wp_properties_matches (self->settings, settings));

}

static void
on_settings_ready (WpSettings *s, GAsyncResult *res, gpointer data)
{
  TestSettingsFixture *self = data;

  g_assert_true(wp_object_activate_finish (WP_OBJECT (s), res, NULL));

  g_main_loop_quit(self->base.loop);
}

static void
test_wpsettings_setup (TestSettingsFixture *self, gconstpointer user_data)
{
  test_metadata_setup (self, user_data);

  {
    self->s = wp_settings_get_instance (self->base.core, "test-settings");

    wp_object_activate (WP_OBJECT (self->s),
        WP_OBJECT_FEATURES_ALL,
        NULL,
        (GAsyncReadyCallback)on_settings_ready,
        self);
    g_main_loop_run (self->base.loop);
  }

}

static void
test_wpsettings_teardown (TestSettingsFixture *self, gconstpointer user_data)
{
  test_metadata_teardown (self, user_data);
  g_clear_object (&self->s);
}

static void
test_wpsettings (TestSettingsFixture *self, gconstpointer data)
{
  WpSettings *s = self->s;
  g_assert_false (wp_settings_get_boolean (s, "test-property1"));
  g_assert_true (wp_settings_get_boolean (s, "test-property2"));

  /* test the wp_settings_get_instance() API */
  {
    g_autoptr (WpSettings) s1 =
        wp_settings_get_instance (self->base.core, "test-settings");
    g_autoptr (WpSettings) s2 =
        wp_settings_get_instance (self->base.core, "test-settings");
    g_autoptr (WpSettings) s3 =
        wp_settings_get_instance (self->base.core, "blah-blah");

    g_assert_true (s == s1);
    g_assert_true (s1 == s2);
    g_assert_false (s1 == s3);

  }

  {
    g_autoptr (WpSettings) s4 =
        wp_settings_get_instance (self->base.core, NULL);

    g_auto (GValue) value = G_VALUE_INIT;
    g_object_get_property (G_OBJECT(s4), "metadata-name", &value);

    g_assert_cmpstr (g_value_get_string (&value), ==, "sm-settings");

  }
}

gint
main (gint argc, gchar *argv[])
{
  g_test_init (&argc, &argv, NULL);
  wp_init (WP_INIT_ALL);

  g_test_add ("/wp/settings/conf-file-loading", TestSettingsFixture, NULL,
      test_conf_file_setup, test_conf_file, test_conf_file_teardown);
  g_test_add ("/wp/settings/parsing", TestSettingsFixture, NULL,
      test_parsing_setup, test_parsing, test_parsing_teardown);
  g_test_add ("/wp/settings/metadata-creation", TestSettingsFixture, NULL,
      test_metadata_setup, test_metadata, test_metadata_teardown);
  g_test_add ("/wp/settings/wpsettings-creation", TestSettingsFixture, NULL,
      test_wpsettings_setup, test_wpsettings, test_wpsettings_teardown);

  return g_test_run ();
}
