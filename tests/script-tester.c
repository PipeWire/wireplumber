/* WirePlumber
 *
 * Copyright © 2021 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "../common/base-test-fixture.h"
#include <spa/param/audio/format-utils.h>

#define DEFAULT_RATE		44100
#define DEFAULT_CHANNELS	2

typedef struct {
  WpBaseTestFixture base;
  struct pw_stream *stream;
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
  const gchar *test_script = args [2];

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
    load_component (f, "si-audio-endpoint", "module");

    load_component (f, "metadata", "module");
    load_component (f, "default-nodes-api", "module");

    load_component (f, "node/create-item.lua", "script/lua");

    load_component (f, "default-nodes/apply-default-node.lua", "script/lua");
    load_component (f, "default-nodes/state-default-nodes.lua", "script/lua");
    load_component (f, "default-nodes/find-best-default-node.lua", "script/lua");
    load_component (f, "default-nodes/select-default-nodes.lua", "script/lua");

    load_component (f, "linking/find-best-target.lua", "script/lua");
    load_component (f, "linking/find-default-target.lua", "script/lua");
    load_component (f, "linking/find-defined-target.lua", "script/lua");
    load_component (f, "linking/link-target.lua", "script/lua");
    load_component (f, "linking/prepare-link.lua", "script/lua");
    load_component (f, "linking/rescan.lua", "script/lua");

    g_assert_nonnull (pw_context_load_module (f->base.server.context,
        "libpipewire-module-adapter", NULL, NULL));

    g_assert_nonnull (pw_context_load_module (f->base.server.context,
        "libpipewire-module-link-factory", NULL, NULL));

    g_assert_cmpint (pw_context_add_spa_lib (f->base.server.context,
      "audiotestsrc", "audiotestsrc/libspa-audiotestsrc"), == , 0);

  }
}

static void
base_tests_setup (ScriptRunnerFixture *f, gconstpointer data)
{
  f->base.conf_file =
      g_strdup_printf ("%s/settings.conf", g_getenv ("G_TEST_SRCDIR"));

  wp_base_test_fixture_setup (&f->base, WP_BASE_TEST_FLAG_CLIENT_CORE);

  load_components (f, data);
}

static void
create_stream_node (ScriptRunnerFixture *f, gconstpointer argv)
{
  gchar **args = (gchar **) argv;
  gchar *test_case = args [2];

  struct pw_properties *props;
  const struct spa_pod *params [1];
  uint8_t buffer [1024];
  struct spa_pod_builder b = SPA_POD_BUILDER_INIT (buffer, sizeof (buffer));
  int direction = PW_DIRECTION_OUTPUT;

  if (g_str_has_suffix (test_case, "capture.lua"))
      direction = PW_DIRECTION_INPUT;

  props = pw_properties_new (
      PW_KEY_MEDIA_TYPE, "Audio",
      PW_KEY_NODE_NAME, "stream-node",
      NULL);

  f->stream = pw_stream_new (
      wp_core_get_pw_core (f->base.client_core),
      "stream-node", props);

  params [0] = spa_format_audio_raw_build (&b, SPA_PARAM_EnumFormat,
      &SPA_AUDIO_INFO_RAW_INIT (
          .format = SPA_AUDIO_FORMAT_F32,
          .channels = DEFAULT_CHANNELS,
          .rate = DEFAULT_RATE));

  pw_stream_connect (f->stream,
      direction,
      PW_ID_ANY,
      PW_STREAM_FLAG_AUTOCONNECT |
      PW_STREAM_FLAG_MAP_BUFFERS,
      params, 1);
}
static void
script_tests_setup (ScriptRunnerFixture *f, gconstpointer data)
{
  base_tests_setup (f, data);
  create_stream_node (f, data);
}

static void
base_tests_teardown (ScriptRunnerFixture *f, gconstpointer data)
{
  wp_base_test_fixture_teardown (&f->base);
}

static void
script_tests_teardown (ScriptRunnerFixture *f, gconstpointer data)
{
  base_tests_teardown (f, data);
}

gint
main (gint argc, gchar *argv[])
{
  g_test_init (&argc, &argv, NULL);
  wp_init (WP_INIT_ALL);

  g_assert_cmpint (argc, >= , 1);
  gchar **args = (gchar **) argv;
  gchar *test_suite = args [1];

  if (g_str_equal (test_suite, "script-tests"))
    g_test_add ("/lua/linking-tests", ScriptRunnerFixture, argv,
        script_tests_setup, script_run, script_tests_teardown);
  else
    g_test_add ("/lua/wprun/tests", ScriptRunnerFixture, argv,
        base_tests_setup, script_run, base_tests_teardown);

  return g_test_run ();
}
