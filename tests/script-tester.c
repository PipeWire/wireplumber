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

#define TEST_METADATA_NAME "test-settings"
#define DEFAULT_METADATA_NAME "sm-settings"

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
load_component (ScriptRunnerFixture *f, const gchar *name, const gchar *type,
    GVariant *args)
{
  g_autofree gchar *component_name = NULL;
  g_autofree gchar *plugin_name = NULL;
  g_autoptr (WpPlugin) plugin = NULL;
  g_autoptr (GError) error = NULL;
  g_autoptr (WpSiFactory) si = NULL;

  if (g_str_equal (type, "script/lua")) {
    component_name = g_strdup (name);
    plugin_name = g_strdup_printf ("script:%s", (const gchar *) name);
  }
  else {
    component_name = g_strdup_printf ("libwireplumber-module-%s", name);
    plugin_name = g_strdup (name);
  }

  wp_core_load_component (f->base.core, component_name, type, args, &error);
  g_assert_no_error (error);

  plugin = wp_plugin_find (f->base.core, plugin_name);

  if (!g_str_has_prefix (name, "si")) {
    wp_object_activate (WP_OBJECT (plugin), WP_PLUGIN_FEATURE_ENABLED,
        NULL, (GAsyncReadyCallback) test_object_activate_finish_cb, f);

    g_main_loop_run (f->base.loop);
  }
}

static void
script_run (ScriptRunnerFixture *f, gconstpointer argv)
{
  g_autoptr (WpPlugin) plugin = NULL;
  g_autoptr (GError) error = NULL;
  g_autofree gchar *pluginname = NULL;
  gchar **args = (gchar **) argv;
  gchar *test_type = args [1];
  gchar *test_script = args [2];
  GVariant *metadata_name = NULL;
  /* TODO: we could do some more stuff here to provide the test script with an
     API to deal with the main loop and test asynchronous stuff, if necessary */

  load_component (f, "lua-scripting", "module", NULL);

  if (g_str_equal (test_type, "policy-tests")) {
    metadata_name = g_variant_new_string (DEFAULT_METADATA_NAME);
    load_component (f, "settings", "module", metadata_name);

    load_component (f, "si-node", "module", NULL);

    load_component (f, "si-audio-adapter", "module", NULL);

    load_component (f, "si-standard-link", "module", NULL);

    load_component (f, "policy-hooks.lua", "script/lua", NULL);
  } else {
    metadata_name = g_variant_new_string (TEST_METADATA_NAME);
    load_component (f, "settings", "module", metadata_name);
  }
  /* load the test script */
  load_component (f, (const gchar *) test_script, "script/lua", NULL);
}

gint
main (gint argc, gchar *argv[])
{
  g_test_init (&argc, &argv, NULL);
  wp_init (WP_INIT_ALL);

  g_assert_cmpint (argc, >=, 1);

  g_test_add ("/lua/script/run", ScriptRunnerFixture, argv,
      script_runner_setup, script_run, script_runner_teardown);

  return g_test_run ();
}
