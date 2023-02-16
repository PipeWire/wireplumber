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

typedef struct _ScriptRunnerFixture ScriptRunnerFixture;
static void load_component (ScriptRunnerFixture *f, const gchar *name,
    const gchar *type);

struct _WpScriptTester
{
  WpPlugin parent;
  struct pw_stream *stream;
  ScriptRunnerFixture *test_fixture;
};

enum {
  ACTION_CREATE_STREAM_NODE,
  N_SIGNALS
};

enum {
  PROP_0,
  PROP_TEST_FIXTURE,
  PROP_SUPPORTED_FEATURES,
};

static guint signals [N_SIGNALS] = { 0 };

/* plugin for lua test scripts to trigger stream node creation, after all the
 * device nodes are created and ready.
 */
G_DECLARE_FINAL_TYPE (WpScriptTester, wp_script_tester,
    WP, SCRIPT_TESTER, WpPlugin)
G_DEFINE_TYPE (WpScriptTester, wp_script_tester, WP_TYPE_PLUGIN)

struct _ScriptRunnerFixture {
  WpBaseTestFixture base;
  WpScriptTester *plugin;
};

static void
wp_script_tester_init (WpScriptTester *self)
{
}

static G_GNUC_UNUSED void
dummy_cb (WpObject *object, GAsyncResult *res, WpBaseTestFixture *f)
{
}

static void
wp_script_tester_restart_plugin (WpScriptTester *self, const gchar *name)
{
  ScriptRunnerFixture *f = self->test_fixture;
  g_autoptr (WpPlugin) plugin = wp_plugin_find (f->base.core, name);

  wp_object_deactivate (WP_OBJECT (plugin), WP_PLUGIN_FEATURE_ENABLED);
  wp_object_activate (WP_OBJECT (plugin), WP_PLUGIN_FEATURE_ENABLED,
    NULL, (GAsyncReadyCallback) dummy_cb, f);
}
  static void
wp_script_tester_create_stream (WpScriptTester *self, const gchar *stream_type,
    WpProperties *stream_props)
{
  ScriptRunnerFixture *f = self->test_fixture;
  WpProperties *props = NULL;
  const struct spa_pod *params [1];
  uint8_t buffer [1024];
  struct spa_pod_builder b = SPA_POD_BUILDER_INIT (buffer, sizeof (buffer));
  int direction;

  wp_info ("create stream_type(%s) with props(%p)", stream_type, stream_props);

  if (g_str_equal (stream_type, "playback"))
    direction = PW_DIRECTION_OUTPUT;
  else
    direction = PW_DIRECTION_INPUT;

  props = wp_properties_new (
      PW_KEY_MEDIA_TYPE, "Audio",
      PW_KEY_NODE_NAME, "stream-node",
      NULL);

  if (stream_props)
    wp_properties_add (props, stream_props);

  self->stream = pw_stream_new (
      wp_core_get_pw_core (f->base.client_core),
      "stream-node", wp_properties_to_pw_properties (props));

  params [0] = spa_format_audio_raw_build (&b, SPA_PARAM_EnumFormat,
      &SPA_AUDIO_INFO_RAW_INIT (
          .format = SPA_AUDIO_FORMAT_F32,
          .channels = DEFAULT_CHANNELS,
          .rate = DEFAULT_RATE));

  pw_stream_connect (self->stream,
      direction,
      PW_ID_ANY,
      PW_STREAM_FLAG_AUTOCONNECT |
      PW_STREAM_FLAG_MAP_BUFFERS,
      params, 1);
}

static void
wp_script_tester_set_property (GObject *object, guint property_id,
  const GValue *value, GParamSpec *pspec)
{
  WpScriptTester *self = WP_SCRIPT_TESTER (object);

  switch (property_id) {
  case PROP_TEST_FIXTURE:
    self->test_fixture = g_value_get_pointer (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_script_tester_get_property (GObject *object, guint property_id, GValue *value,
  GParamSpec *pspec)
{
  WpScriptTester *self = WP_SCRIPT_TESTER (object);

  switch (property_id) {
  case PROP_TEST_FIXTURE:
    g_value_set_pointer (value, self->test_fixture);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_script_tester_class_init (WpScriptTesterClass *klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  object_class->get_property = wp_script_tester_get_property;
  object_class->set_property = wp_script_tester_set_property;

  g_object_class_install_property (object_class, PROP_TEST_FIXTURE,
      g_param_spec_pointer ("test-fixture", "test-fixture", "The Test Fixture",
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  signals [ACTION_CREATE_STREAM_NODE] = g_signal_new_class_handler (
      "create-stream", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      (GCallback) wp_script_tester_create_stream,
      NULL, NULL, NULL, G_TYPE_NONE, 2, G_TYPE_STRING, WP_TYPE_PROPERTIES);

  signals [ACTION_CREATE_STREAM_NODE] = g_signal_new_class_handler (
      "restart-plugin", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      (GCallback) wp_script_tester_restart_plugin,
      NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_STRING);

}

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
    load_component (f, "si-audio-virtual", "module");

    load_component (f, "default-nodes/apply-default-node.lua", "script/lua");
    load_component (f, "default-nodes/find-echo-cancel-default-node.lua", "script/lua");
    load_component (f, "default-nodes/state-default-nodes.lua", "script/lua");
    load_component (f, "default-nodes/find-best-default-node.lua", "script/lua");
    load_component (f, "default-nodes/rescan.lua", "script/lua");

    load_component (f, "metadata.lua", "script/lua");
    load_component (f, "default-nodes-api", "module");

    load_component (f, "node/create-item.lua", "script/lua");

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
      g_strdup_printf ("%s/settings/wireplumber.conf", g_getenv ("G_TEST_SRCDIR"));

  wp_base_test_fixture_setup (&f->base, WP_BASE_TEST_FLAG_CLIENT_CORE);

  load_components (f, data);
}

static void
script_tests_setup (ScriptRunnerFixture *f, gconstpointer data)
{
  base_tests_setup (f, data);

  f->plugin = g_object_new (wp_script_tester_get_type (),
      "name", "script-tester",
      "core", f->base.core, /*to register plugin*/
      "test-fixture", f,
      NULL);

  wp_plugin_register ((WpPlugin *)f->plugin);
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
