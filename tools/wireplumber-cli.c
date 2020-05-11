/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>
#include <pipewire/pipewire.h>

static GOptionEntry entries[] =
{
  { NULL }
};

struct WpCliData
{
  WpCore *core;
  GMainLoop *loop;

  union {
    struct {
      guint32 id;
    } set_default;
    struct {
      guint32 id;
      gfloat volume;
    } set_volume;
  } params;
};

static void
async_quit (WpCore *core, GAsyncResult *res, struct WpCliData * d)
{
  g_print ("Success\n");
  g_main_loop_quit (d->loop);
}

static void
print_dev_endpoint (WpEndpoint *ep, WpSession *session, const gchar *type_name)
{
  guint32 id = wp_proxy_get_bound_id (WP_PROXY (ep));
  gboolean is_default = (session && type_name != NULL &&
          wp_session_get_default_endpoint (session, type_name) == id);
  g_autoptr (WpSpaPod) ctrl = NULL;
  gfloat volume = 0.0;
  gboolean mute = FALSE;

  ctrl = wp_proxy_get_control (WP_PROXY (ep), "volume");
  wp_spa_pod_get_float (ctrl, &volume);
  ctrl = wp_proxy_get_control (WP_PROXY (ep), "mute");
  wp_spa_pod_get_boolean (ctrl, &mute);

  g_print (" %c %4u. %60s\tvol: %.2f %s\n", is_default ? '*' : ' ', id,
      wp_endpoint_get_name (ep), volume, mute ? "MUTE" : "");
}

static void
print_client_endpoint (WpEndpoint *ep)
{
  guint32 id = wp_proxy_get_bound_id (WP_PROXY (ep));
  g_print ("   %4u. %s (%s)\n", id, wp_endpoint_get_name (ep),
      wp_endpoint_get_media_class (ep));
}

static void
list_endpoints (WpObjectManager * om, struct WpCliData * d)
{
  g_autoptr (WpIterator) it = NULL;
  g_autoptr (WpSession) session = NULL;
  g_auto (GValue) val = G_VALUE_INIT;

  it = wp_object_manager_iterate (om);
  for (; wp_iterator_next (it, &val) &&
            g_type_is_a (G_VALUE_TYPE (&val), WP_TYPE_SESSION);
        g_value_unset (&val))
  {
    session = g_value_dup_object (&val);
    g_value_unset (&val);
    break;
  }

  wp_iterator_reset (it);

  g_print ("Audio capture devices:\n");
  for (; wp_iterator_next (it, &val) &&
            g_type_is_a (G_VALUE_TYPE (&val), WP_TYPE_ENDPOINT);
        g_value_unset (&val))
  {
    WpEndpoint *ep = g_value_get_object (&val);
    if (g_strcmp0 (wp_endpoint_get_media_class (ep), "Audio/Source") == 0)
      print_dev_endpoint (ep, session, "wp-session-default-endpoint-audio-source");
  }

  wp_iterator_reset (it);

  g_print ("\nAudio playback devices:\n");
  for (; wp_iterator_next (it, &val) &&
            g_type_is_a (G_VALUE_TYPE (&val), WP_TYPE_ENDPOINT);
        g_value_unset (&val))
  {
    WpEndpoint *ep = g_value_get_object (&val);
    if (g_strcmp0 (wp_endpoint_get_media_class (ep), "Audio/Sink") == 0)
      print_dev_endpoint (ep, session, "wp-session-default-endpoint-audio-sink");
  }

  wp_iterator_reset (it);

  g_print ("\nClient streams:\n");
  for (; wp_iterator_next (it, &val) &&
            g_type_is_a (G_VALUE_TYPE (&val), WP_TYPE_ENDPOINT);
        g_value_unset (&val))
  {
    WpEndpoint *ep = g_value_get_object (&val);
    if (g_str_has_suffix (wp_endpoint_get_media_class (ep), "/Audio"))
      print_client_endpoint (ep);
  }

  g_main_loop_quit (d->loop);
}

static void
set_default (WpObjectManager * om, struct WpCliData * d)
{
  g_autoptr (WpIterator) it = NULL;
  g_autoptr (WpSession) session = NULL;
  g_auto (GValue) val = G_VALUE_INIT;

  it = wp_object_manager_iterate (om);

  for (; wp_iterator_next (it, &val) &&
            g_type_is_a (G_VALUE_TYPE (&val), WP_TYPE_SESSION);
        g_value_unset (&val))
  {
    session = g_value_dup_object (&val);
    g_value_unset (&val);
    break;
  }

  if (!session) {
    g_print ("No Session object - changing the default endpoint is not supported\n");
    g_main_loop_quit (d->loop);
    return;
  }

  wp_iterator_reset (it);

  for (; wp_iterator_next (it, &val) &&
            g_type_is_a (G_VALUE_TYPE (&val), WP_TYPE_ENDPOINT);
        g_value_unset (&val))
  {
    WpEndpoint *ep = g_value_get_object (&val);
    guint32 id = wp_proxy_get_bound_id (WP_PROXY (ep));

    if (id == d->params.set_default.id) {
      const gchar * type_name;
      if (g_strcmp0 (wp_endpoint_get_media_class (ep), "Audio/Sink") == 0)
        type_name = "wp-session-default-endpoint-audio-sink";
      else if (g_strcmp0 (wp_endpoint_get_media_class (ep), "Audio/Source") == 0)
        type_name = "wp-session-default-endpoint-audio-source";
      else {
        g_print ("%u: not a device endpoint\n", id);
        g_main_loop_quit (d->loop);
        return;
      }

      wp_session_set_default_endpoint (session, type_name, id);
      wp_core_sync (d->core, NULL, (GAsyncReadyCallback) async_quit, d);
      return;
    }
  }

  g_print ("%u: not an endpoint\n", d->params.set_default.id);
  g_main_loop_quit (d->loop);
}

static void
set_volume (WpObjectManager * om, struct WpCliData * d)
{
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) val = G_VALUE_INIT;

  it = wp_object_manager_iterate (om);

  for (; wp_iterator_next (it, &val); g_value_unset (&val)) {
    WpEndpoint *ep = g_value_get_object (&val);
    guint32 id = wp_proxy_get_bound_id (WP_PROXY (ep));

    if (id == d->params.set_volume.id) {
      g_autoptr (WpSpaPod) vol = wp_spa_pod_new_float (d->params.set_volume.volume);
      wp_proxy_set_control (WP_PROXY (ep), "volume", vol);
      wp_core_sync (d->core, NULL, (GAsyncReadyCallback) async_quit, d);
      return;
    }
  }

  g_print ("%u: not an endpoint\n", d->params.set_default.id);
  g_main_loop_quit (d->loop);
}

static void
device_node_props (WpObjectManager * om, struct WpCliData * d)
{
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) val = G_VALUE_INIT;
  const struct spa_dict * dict;
  const struct spa_dict_item *item;

  it = wp_object_manager_iterate (om);

  g_print ("Capture device nodes:\n");

  for (; wp_iterator_next (it, &val); g_value_unset (&val)) {
    WpProxy *node = g_value_get_object (&val);
    g_autoptr (WpProperties) props = wp_proxy_get_properties (node);

    if (g_strcmp0 (wp_properties_get (props, "media.class"), "Audio/Source") != 0)
      continue;

    g_print (" node id: %u\n", wp_proxy_get_bound_id (node));

    dict = wp_properties_peek_dict (props);
    spa_dict_for_each (item, dict) {
      g_print ("    %s = \"%s\"\n", item->key, item->value);
    }

    g_print ("\n");
  }

  wp_iterator_reset (it);
  g_print ("Playback device nodes:\n");

  for (; wp_iterator_next (it, &val); g_value_unset (&val)) {
    WpProxy *node = g_value_get_object (&val);
    g_autoptr (WpProperties) props = wp_proxy_get_properties (node);

    if (g_strcmp0 (wp_properties_get (props, "media.class"), "Audio/Sink") != 0)
      continue;

    g_print (" node id: %u\n", wp_proxy_get_bound_id (node));

    dict = wp_properties_peek_dict (props);
    spa_dict_for_each (item, dict) {
      g_print ("    %s = \"%s\"\n", item->key, item->value);
    }

    g_print ("\n");
  }

  g_main_loop_quit (d->loop);
}

static void
on_disconnected (WpCore *core, struct WpCliData * d)
{
  g_main_loop_quit (d->loop);
}

static const gchar * const usage_string =
    "Operations:\n"
    "  ls-endpoints\t\tLists all endpoints\n"
    "  set-default [id]\tSets [id] to be the default device endpoint of its kind (capture/playback)\n"
    "  set-volume [id] [vol]\tSets the volume of [id] to [vol] (floating point, 1.0 is 100%%)\n"
    "  device-node-props\tShows device node properties\n"
    "";

gint
main (gint argc, gchar **argv)
{
  struct WpCliData data = {0};
  g_autoptr (GOptionContext) context = NULL;
  g_autoptr (GError) error = NULL;
  g_autoptr (WpCore) core = NULL;
  g_autoptr (WpObjectManager) om = NULL;
  g_autoptr (GMainLoop) loop = NULL;

  wp_init (WP_INIT_ALL);

  context = g_option_context_new ("- PipeWire Session/Policy Manager Helper CLI");
  g_option_context_add_main_entries (context, entries, NULL);
  g_option_context_set_description (context, usage_string);
  if (!g_option_context_parse (context, &argc, &argv, &error)) {
    return 1;
  }

  data.loop = loop = g_main_loop_new (NULL, FALSE);
  data.core = core = wp_core_new (NULL, NULL);
  g_signal_connect (core, "disconnected", (GCallback) on_disconnected, &data);

  om = wp_object_manager_new ();

  if (argc == 2 && !g_strcmp0 (argv[1], "ls-endpoints")) {
    wp_object_manager_add_interest (om, WP_TYPE_ENDPOINT,
        NULL, WP_PROXY_FEATURE_INFO | WP_PROXY_FEATURE_BOUND |
        WP_PROXY_FEATURE_CONTROLS);
    wp_object_manager_add_interest (om, WP_TYPE_SESSION,
        NULL, WP_PROXY_FEATURE_INFO | WP_PROXY_FEATURE_BOUND |
        WP_PROXY_FEATURE_CONTROLS);
    g_signal_connect (om, "objects-changed", (GCallback) list_endpoints, &data);
  }

  else if (argc == 3 && !g_strcmp0 (argv[1], "set-default")) {
    long id = strtol (argv[2], NULL, 10);
    if (id == 0) {
      g_print ("%s: not a valid id\n", argv[2]);
      return 1;
    }

    wp_object_manager_add_interest (om, WP_TYPE_ENDPOINT,
        NULL, WP_PROXY_FEATURE_INFO | WP_PROXY_FEATURE_BOUND);
    wp_object_manager_add_interest (om, WP_TYPE_SESSION,
        NULL, WP_PROXY_FEATURE_INFO | WP_PROXY_FEATURE_BOUND |
        WP_PROXY_FEATURE_CONTROLS);

    data.params.set_default.id = id;
    g_signal_connect (om, "objects-changed", (GCallback) set_default, &data);
  }

  else if (argc == 4 && !g_strcmp0 (argv[1], "set-volume")) {
    long id = strtol (argv[2], NULL, 10);
    float volume = strtof (argv[3], NULL);
    if (id == 0) {
      g_print ("%s: not a valid id\n", argv[2]);
      return 1;
    }

    wp_object_manager_add_interest (om, WP_TYPE_ENDPOINT,
        NULL, WP_PROXY_FEATURE_INFO | WP_PROXY_FEATURE_BOUND |
        WP_PROXY_FEATURE_CONTROLS);

    data.params.set_volume.id = id;
    data.params.set_volume.volume = volume;
    g_signal_connect (om, "objects-changed", (GCallback) set_volume, &data);
  }

  else if (argc == 2 && !g_strcmp0 (argv[1], "device-node-props")) {
    wp_object_manager_add_interest (om, WP_TYPE_NODE, NULL,
        WP_PROXY_FEATURE_INFO);
    g_signal_connect (om, "objects-changed", (GCallback) device_node_props,
        &data);
  }

  else {
    g_autofree gchar *help = g_option_context_get_help (context, TRUE, NULL);
    g_print ("%s", help);
    return 1;
  }

  wp_core_install_object_manager (core, om);
  if (wp_core_connect (core))
    g_main_loop_run (loop);

  return 0;
}
