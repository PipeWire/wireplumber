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

static void
load_component (ScriptRunnerFixture *f, const gchar *name, const gchar *type)
{
  g_autofree gchar *component_name = NULL;
  g_autofree gchar *plugin_name = NULL;
  g_autoptr (GError) error = NULL;

  if ((g_str_equal (type, "script/lua")) &&
      (g_file_test (name, G_FILE_TEST_EXISTS))) {
    g_autofree gchar *filename = g_path_get_basename (name);
    plugin_name = g_strdup_printf ("script:%s", (const gchar *) filename);
    component_name = g_strdup (name);

  } else if (g_str_equal (type, "script/lua")) {
    component_name = g_strdup (name);
    plugin_name = g_strdup_printf ("script:%s", (const gchar *) name);

  } else {
    component_name = g_strdup_printf ("libwireplumber-module-%s", name);
    plugin_name = g_strdup (name);
  }

  wp_core_load_component (f->base.core, component_name, type, NULL, &error);
  g_assert_no_error (error);

  if (!g_str_has_prefix (name, "si")) {
    g_autoptr (WpPlugin) plugin = wp_plugin_find (f->base.core, plugin_name);

    wp_object_activate (WP_OBJECT (plugin), WP_PLUGIN_FEATURE_ENABLED,
        NULL, (GAsyncReadyCallback) test_object_activate_finish_cb, f);

    g_main_loop_run (f->base.loop);
  }
}

static void
script_run (ScriptRunnerFixture *f, gconstpointer argv)
{
  gchar **args = (gchar **) argv;
  gchar *test_suite = args [1];
  const gchar *test_script = args [2];

  if (g_str_equal (test_suite, "script-tests"))
    test_script = g_test_get_filename (G_TEST_DIST, g_test_get_path (), NULL);

  /* load the test script */
  load_component (f, (const gchar *) test_script, "script/lua");
}

static void
load_components (ScriptRunnerFixture *f, gconstpointer argv)
{
  g_autoptr (WpPlugin) plugin = NULL;
  g_autoptr (GError) error = NULL;
  g_autofree gchar *pluginname = NULL;
  gchar **args = (gchar **) argv;
  gchar *test_suite = args [1];
  /* TODO: we could do some more stuff here to provide the test script with an
     API to deal with the main loop and test asynchronous stuff, if necessary */

  load_component (f, "lua-scripting", "module");

  load_component (f, "settings", "module");

  if (g_str_equal (test_suite, "script-tests")) {

    load_component (f, "standard-event-source", "module");

    load_component (f, "si-audio-adapter", "module");

    load_component (f, "si-standard-link", "module");

    load_component (f, "create-item.lua", "script/lua");

    load_component (f, "policy-hooks.lua", "script/lua");

    g_assert_nonnull (pw_context_load_module (f->base.server.context,
        "libpipewire-module-adapter", NULL, NULL));

    g_assert_nonnull (pw_context_load_module (f->base.server.context,
        "libpipewire-module-link-factory", NULL, NULL));
  }
}

static void
script_runner_setup (ScriptRunnerFixture *f, gconstpointer data)
{
  f->base.conf_file =
      g_strdup_printf ("%s/settings.conf", g_getenv ("G_TEST_SRCDIR"));

  wp_base_test_fixture_setup (&f->base, WP_BASE_TEST_FLAG_CLIENT_CORE);

  load_components (f, data);
}

static void
script_runner_teardown (ScriptRunnerFixture *f, gconstpointer data)
{
  wp_base_test_fixture_teardown (&f->base);
}

gint
main (gint argc, gchar *argv[])
{
  g_test_init (&argc, &argv, NULL);
  wp_init (WP_INIT_ALL);

  g_assert_cmpint (argc, >= , 1);
  gchar **args = (gchar **) argv;
  gchar *test_suite = args [1];
  gchar *test_case = args [2];

  if (g_str_equal (test_suite, "script-tests")) {
    if (g_str_equal (test_case, "policy-tests")) {
      g_test_add ("/policy-tests/non-default-device-nodes.lua",
          ScriptRunnerFixture, argv, script_runner_setup, script_run,
          script_runner_teardown);
    }
  } else
    g_test_add ("/lua/wprun/tests", ScriptRunnerFixture, argv,
        script_runner_setup, script_run, script_runner_teardown);

  return g_test_run ();
}
