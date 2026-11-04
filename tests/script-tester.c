/* WirePlumber
 *
 * Copyright © 2021 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "../common/base-test-fixture.h"
#include <spa/param/audio/format-utils.h>
#include <spa/param/video/format-utils.h>
#include <spa/param/video/type-info.h>

#define DEFAULT_RATE		44100
#define DEFAULT_CHANNELS	2
#define DEFAULT_WIDTH		320
#define DEFAULT_HEIGHT		240
#define DEFAULT_FRAMERATE	25

typedef struct _ScriptRunnerFixture ScriptRunnerFixture;
static void load_component (ScriptRunnerFixture *f, const gchar *name,
    const gchar *type);

/* One client stream created by a test, kept around so that the test can ask
 * whether the daemon sent an error to it */
struct stream_entry
{
  struct pw_stream *stream;
  struct spa_hook listener;
  gchar *error;
  gboolean has_format;
};

struct _WpScriptTester
{
  WpPlugin parent;
  GHashTable *streams;
  GPtrArray *clients;
  ScriptRunnerFixture *test_fixture;
};

enum {
  ACTION_CREATE_STREAM_NODE,
  ACTION_RESTART_PLUGIN,
  ACTION_GET_STREAM_ERROR,
  ACTION_GET_STREAM_HAS_FORMAT,
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
  /* the components named in argv after the test script, which are not loaded */
  const gchar * const *disabled_components;
};

static void
disconnect_client (gpointer client)
{
  pw_core_disconnect (client);
}

static void
stream_entry_free (gpointer data)
{
  struct stream_entry *entry = data;

  spa_hook_remove (&entry->listener);
  pw_stream_destroy (entry->stream);
  g_free (entry->error);
  g_free (entry);
}

static void
wp_script_tester_init (WpScriptTester *self)
{
  self->streams = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
      stream_entry_free);
  self->clients = g_ptr_array_new_with_free_func (disconnect_client);
}

static void
wp_script_tester_finalize (GObject *object)
{
  WpScriptTester *self = WP_SCRIPT_TESTER (object);

  g_clear_pointer (&self->streams, g_hash_table_unref);
  g_clear_pointer (&self->clients, g_ptr_array_unref);

  G_OBJECT_CLASS (wp_script_tester_parent_class)->finalize (object);
}

/* pw_stream reports an error on its node as a state change to ERROR without
 * updating its own state, so pw_stream_get_state() cannot be used here; see
 * proxy_error() in pipewire's stream.c */
static void
on_client_stream_state_changed (void *data, enum pw_stream_state old_state,
    enum pw_stream_state state, const char *error)
{
  struct stream_entry *entry = data;

  if (state == PW_STREAM_STATE_ERROR) {
    g_free (entry->error);
    entry->error = g_strdup (error);
  }
}

static void
on_client_stream_param_changed (void *data, uint32_t id,
    const struct spa_pod *param)
{
  struct stream_entry *entry = data;

  if (id == SPA_PARAM_Format)
    entry->has_format = (param != NULL);
}

static const struct pw_stream_events client_stream_events = {
  PW_VERSION_STREAM_EVENTS,
  .state_changed = on_client_stream_state_changed,
  .param_changed = on_client_stream_param_changed,
};

static gchar *
wp_script_tester_get_stream_error (WpScriptTester *self, const gchar *name)
{
  struct stream_entry *entry = g_hash_table_lookup (self->streams, name);

  g_assert_nonnull (entry);
  return g_strdup (entry->error);
}

static gboolean
wp_script_tester_get_stream_has_format (WpScriptTester *self,
    const gchar *name)
{
  struct stream_entry *entry = g_hash_table_lookup (self->streams, name);

  g_assert_nonnull (entry);
  return entry->has_format;
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
  g_autoptr (WpProperties) props = NULL;
  const struct spa_pod *params [2];
  guint n_params = 0;
  uint8_t buffer [1024];
  struct spa_pod_builder b = SPA_POD_BUILDER_INIT (buffer, sizeof (buffer));
  gboolean video, any, encoded, dual;
  enum spa_video_format video_format = SPA_VIDEO_FORMAT_RGB;
  struct stream_entry *entry;
  const gchar *name;
  int direction;

  wp_info ("create stream_type(%s) with props(%p)", stream_type, stream_props);

  video = g_str_has_prefix (stream_type, "video-");
  /* no EnumFormat, like pipewiresrc with ANY caps */
  any = g_str_has_prefix (stream_type, "video-any-");
  encoded = g_str_has_prefix (stream_type, "encoded-");
  dual = g_str_has_prefix (stream_type, "dual-");

  if (g_str_has_suffix (stream_type, "playback"))
    direction = PW_DIRECTION_OUTPUT;
  else
    direction = PW_DIRECTION_INPUT;

  props = wp_properties_new (
      PW_KEY_MEDIA_TYPE, video ? "Video" : "Audio",
      PW_KEY_NODE_NAME, "stream-node",
      NULL);

  /* update, not add, so that a test can override the defaults */
  if (stream_props)
    wp_properties_update (props, stream_props);

  name = wp_properties_get (props, PW_KEY_NODE_NAME);

  if (video) {
    const gchar *format_name = wp_properties_get (props, "test.video.format");
    if (format_name) {
      WpSpaIdValue idval = wp_spa_id_value_from_short_name (
          SPA_TYPE_INFO_VideoFormat, format_name);
      g_assert_nonnull (idval);
      video_format = wp_spa_id_value_number (idval);
    }
  }

  entry = g_new0 (struct stream_entry, 1);
  entry->stream = pw_stream_new (
      wp_core_get_pw_core (f->base.client_core),
      name, wp_properties_unref_and_take_pw_properties (g_steal_pointer (&props)));
  pw_stream_add_listener (entry->stream, &entry->listener,
      &client_stream_events, entry);
  g_hash_table_insert (self->streams, g_strdup (name), entry);

  if (video) {
    if (!any)
      params [n_params++] = spa_format_video_raw_build (&b,
          SPA_PARAM_EnumFormat,
          &SPA_VIDEO_INFO_RAW_INIT (
              .format = video_format,
              .size = SPA_RECTANGLE (DEFAULT_WIDTH, DEFAULT_HEIGHT),
              .framerate = SPA_FRACTION (DEFAULT_FRAMERATE, 1)));
  } else {
    if (!encoded)
      params [n_params++] = spa_format_audio_raw_build (&b,
          SPA_PARAM_EnumFormat,
          &SPA_AUDIO_INFO_RAW_INIT (
              .format = SPA_AUDIO_FORMAT_F32,
              .channels = DEFAULT_CHANNELS,
              .rate = DEFAULT_RATE));
    if (encoded || dual)
      params [n_params++] = spa_format_audio_iec958_build (&b,
          SPA_PARAM_EnumFormat,
          &SPA_AUDIO_INFO_IEC958_INIT (
              .codec = SPA_AUDIO_IEC958_CODEC_AC3,
              .rate = DEFAULT_RATE));
  }

  pw_stream_connect (entry->stream,
      direction,
      PW_ID_ANY,
      PW_STREAM_FLAG_AUTOCONNECT |
      PW_STREAM_FLAG_MAP_BUFFERS,
      params, n_params);
}

/* destroys all the streams created so far */
static void
wp_script_tester_destroy_streams (WpScriptTester *self)
{
  g_hash_table_remove_all (self->streams);
}

/* connects an additional client, with the given properties */
static void
wp_script_tester_connect_client (WpScriptTester *self, WpProperties *props)
{
  ScriptRunnerFixture *f = self->test_fixture;
  WpMainContext *wp_ctx =
      wp_core_get_main_context (f->base.client_core);
  struct pw_core *client = pw_context_connect (
      wp_context_get_pw_context (WP_CONTEXT (wp_ctx)),
      props ? wp_properties_to_pw_properties (props) : NULL, 0);

  g_assert_nonnull (client);
  g_ptr_array_add (self->clients, client);
}

/* disconnects the most recently connected additional client */
static void
wp_script_tester_disconnect_client (WpScriptTester *self)
{
  g_assert_cmpuint (self->clients->len, >, 0);
  g_ptr_array_set_size (self->clients, self->clients->len - 1);
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
  object_class->finalize = wp_script_tester_finalize;
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

  signals [ACTION_RESTART_PLUGIN] = g_signal_new_class_handler (
      "restart-plugin", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      (GCallback) wp_script_tester_restart_plugin,
      NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_STRING);

  g_signal_new_class_handler ("destroy-streams", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      (GCallback) wp_script_tester_destroy_streams,
      NULL, NULL, NULL, G_TYPE_NONE, 0);

  g_signal_new_class_handler ("connect-client", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      (GCallback) wp_script_tester_connect_client,
      NULL, NULL, NULL, G_TYPE_NONE, 1, WP_TYPE_PROPERTIES);

  g_signal_new_class_handler ("disconnect-client", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      (GCallback) wp_script_tester_disconnect_client,
      NULL, NULL, NULL, G_TYPE_NONE, 0);

  signals [ACTION_GET_STREAM_ERROR] = g_signal_new_class_handler (
      "get-stream-error", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      (GCallback) wp_script_tester_get_stream_error,
      NULL, NULL, NULL, G_TYPE_STRING, 1, G_TYPE_STRING);

  signals [ACTION_GET_STREAM_HAS_FORMAT] = g_signal_new_class_handler (
      "get-stream-has-format", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      (GCallback) wp_script_tester_get_stream_has_format,
      NULL, NULL, NULL, G_TYPE_BOOLEAN, 1, G_TYPE_STRING);
}

static void
on_plugin_loaded (WpCore * core, GAsyncResult * res, ScriptRunnerFixture *f)
{
  gboolean loaded;
  GError *error = NULL;

  loaded = wp_core_load_component_finish (core, res, &error);
  g_assert_no_error (error);
  g_assert_true (loaded);

  g_main_loop_quit (f->base.loop);
}

static void
load_component (ScriptRunnerFixture *f, const gchar *name, const gchar *type)
{
  g_autoptr (WpSpaJson) arguments = NULL;

  if (f->disabled_components &&
      g_strv_contains (f->disabled_components, name)) {
    wp_info ("not loading disabled component %s", name);
    return;
  }

  if (g_str_equal (name, "metadata.lua")) {
    arguments = wp_spa_json_new_object ("metadata.name", "s", "default",
        NULL);
  }

  wp_core_load_component (f->base.core, name, type, arguments, NULL, NULL,
      (GAsyncReadyCallback) on_plugin_loaded, f);
  g_main_loop_run (f->base.loop);
}

static void
script_run (ScriptRunnerFixture *f, gconstpointer argv)
{
  gchar **args = (gchar **) argv;
  const gchar *test_script = args [2];

  if (!test_is_spa_lib_installed (&f->base, "audiotestsrc")) {
    g_test_skip ("The pipewire audiotestsrc factory was not found");
    return;
  }

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

  f->disabled_components = (const gchar * const *) (args + 3);

  load_component (f, "libwireplumber-module-lua-scripting", "module");

  load_component (f, "libwireplumber-module-settings", "module");
  load_component (f, "settings-instance", "built-in");
  load_component (f, "stream-manager", "built-in");
  load_component (f, "stream-link-manager", "built-in");

  if (g_str_equal (test_suite, "script-tests")) {

    load_component (f, "libwireplumber-module-standard-event-source", "module");

    load_component (f, "default-streams/apply-default-stream.lua",
        "script/lua");
    load_component (f, "default-streams/state-default-streams.lua",
        "script/lua");
    load_component (f, "default-streams/find-best-default-stream.lua",
        "script/lua");
    load_component (f, "default-streams/rescan.lua", "script/lua");

    load_component (f, "metadata.lua", "script/lua");
    load_component (f, "libwireplumber-module-default-streams-api", "module");

    load_component (f, "node/suspend-node.lua", "script/lua");

    load_component (f, "linking/rescan.lua", "script/lua");
    load_component (f, "linking/rescan-on-stream.lua", "script/lua");
    load_component (f, "linking/find-defined-target.lua", "script/lua");
    load_component (f, "linking/find-audio-group-target.lua", "script/lua");
    load_component (f, "linking/find-filter-target.lua", "script/lua");
    load_component (f, "linking/find-media-role-target.lua", "script/lua");
    load_component (f, "linking/find-media-role-sink-target.lua", "script/lua");
    load_component (f, "linking/find-default-target.lua", "script/lua");
    load_component (f, "linking/find-best-target.lua", "script/lua");
    load_component (f, "linking/get-filter-from-target.lua", "script/lua");
    load_component (f, "linking/prepare-stream-link.lua", "script/lua");
    load_component (f, "linking/create-stream-link.lua", "script/lua");

    load_component (f, "stream/configure-ports.lua", "script/lua");

    {
      g_autoptr (WpTestServerLocker) lock =
        wp_test_server_locker_new (&f->base.server);

      g_assert_nonnull (pw_context_load_module (f->base.server.context,
          "libpipewire-module-adapter", NULL, NULL));

      g_assert_nonnull (pw_context_load_module (f->base.server.context,
          "libpipewire-module-link-factory", NULL, NULL));

      g_assert_cmpint (pw_context_add_spa_lib (f->base.server.context,
          "audiotestsrc", "audiotestsrc/libspa-audiotestsrc"), == , 0);
    }
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
  f->base.conf_file =
      g_strdup_printf ("%s/config/wireplumber.conf", g_getenv ("G_TEST_SRCDIR"));

  wp_base_test_fixture_setup (&f->base, WP_BASE_TEST_FLAG_CLIENT_CORE);

  load_components (f, data);

  f->plugin = g_object_new (wp_script_tester_get_type (),
      "name", "script-tester",
      "core", f->base.core,
      "test-fixture", f,
      NULL);

  wp_core_register_object (f->base.core, (WpPlugin *)f->plugin);
}

static void
base_tests_teardown (ScriptRunnerFixture *f, gconstpointer data)
{
  wp_base_test_fixture_teardown (&f->base);
}

static void
script_tests_teardown (ScriptRunnerFixture *f, gconstpointer data)
{
  /* destroy streams and disconnect additional clients while the client core
   * is still alive */
  g_hash_table_remove_all (f->plugin->streams);
  g_ptr_array_set_size (f->plugin->clients, 0);
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

  if (g_str_equal (test_suite, "script-tests"))
    g_test_add ("/lua/linking-tests", ScriptRunnerFixture, argv,
        script_tests_setup, script_run, script_tests_teardown);
  else
    g_test_add ("/lua/wprun/tests", ScriptRunnerFixture, argv,
        base_tests_setup, script_run, base_tests_teardown);

  return g_test_run ();
}
