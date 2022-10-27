/* WirePlumber
 *
 * Copyright © 2021 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "../common/base-test-fixture.h"

typedef struct {
  WpBaseTestFixture base;
} ScriptRunnerFixture;

#define METADATA_NAME "test-settings"

static void
script_runner_setup (ScriptRunnerFixture *f, gconstpointer data)
{
  f->base.conf_file =
      g_strdup_printf ("%s/settings.conf", g_getenv ("G_TEST_SRCDIR"));

  wp_base_test_fixture_setup (&f->base, 0);
}

static void
script_runner_teardown (ScriptRunnerFixture *f, gconstpointer data)
{
  wp_base_test_fixture_teardown (&f->base);
}

static void
script_run (ScriptRunnerFixture *f, gconstpointer data)
{
  g_autoptr (WpPlugin) plugin = NULL;
  g_autoptr (GError) error = NULL;
  g_autofree gchar *pluginname = NULL;

  /* TODO: we could do some more stuff here to provide the test script with an
     API to deal with the main loop and test asynchronous stuff, if necessary */

  wp_core_load_component (f->base.core,
      "libwireplumber-module-lua-scripting", "module", NULL, &error);
  g_assert_no_error (error);

  plugin = wp_plugin_find (f->base.core, "lua-scripting");
  wp_object_activate (WP_OBJECT (plugin), WP_PLUGIN_FEATURE_ENABLED,
      NULL, (GAsyncReadyCallback) test_object_activate_finish_cb, f);
  g_main_loop_run (f->base.loop);
  g_clear_object (&plugin);

  {
    wp_core_load_component (f->base.core,
        "libwireplumber-module-settings", "module",
         g_variant_new_string (METADATA_NAME), &error);
    g_assert_no_error (error);

    plugin = wp_plugin_find (f->base.core, "settings");
    wp_object_activate (WP_OBJECT (plugin), WP_PLUGIN_FEATURE_ENABLED,
        NULL, (GAsyncReadyCallback) test_object_activate_finish_cb, f);
    g_main_loop_run (f->base.loop);
  }
  wp_core_load_component (f->base.core, (const gchar *) data, "script/lua",
      NULL, &error);
  g_assert_no_error (error);

  pluginname = g_strdup_printf ("script:%s", (const gchar *) data);

  plugin = wp_plugin_find (f->base.core, pluginname);
  g_assert_nonnull (plugin);
  wp_object_activate (WP_OBJECT (plugin), WP_PLUGIN_FEATURE_ENABLED,
      NULL, (GAsyncReadyCallback) test_object_activate_finish_cb, f);
  g_main_loop_run (f->base.loop);
}

gint
main (gint argc, gchar *argv[])
{
  g_test_init (&argc, &argv, NULL);
  wp_init (WP_INIT_ALL);

  g_assert_cmpint (argc, >=, 1);

  g_test_add ("/lua/script/run", ScriptRunnerFixture, argv[1],
      script_runner_setup, script_run, script_runner_teardown);

  return g_test_run ();
}
