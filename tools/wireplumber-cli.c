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
};

static void
on_objects_changed (WpObjectManager * om, struct WpCliData * d)
{
  g_autoptr (GPtrArray) arr = NULL;
  guint i;
  const struct spa_dict * dict;
  const struct spa_dict_item *item;

  arr = wp_object_manager_get_objects (om, WP_TYPE_PROXY_NODE);

  g_print ("Capture device nodes:\n");

  for (i = 0; i < arr->len; i++) {
    WpProxyNode *node = g_ptr_array_index (arr, i);
    g_autoptr (WpProperties) props = wp_proxy_node_get_properties (node);

    if (g_strcmp0 (wp_properties_get (props, "media.class"), "Audio/Source") != 0)
      continue;

    g_print (" node id: %u\n", wp_proxy_get_global_id (WP_PROXY (node)));

    dict = wp_properties_peek_dict (props);
    spa_dict_for_each (item, dict) {
      g_print ("    %s = \"%s\"\n", item->key, item->value);
    }

    g_print ("\n");
  }

  g_print ("Playback device nodes:\n");

  for (i = 0; i < arr->len; i++) {
    WpProxyNode *node = g_ptr_array_index (arr, i);
    g_autoptr (WpProperties) props = wp_proxy_node_get_properties (node);

    if (g_strcmp0 (wp_properties_get (props, "media.class"), "Audio/Sink") != 0)
      continue;

    g_print (" node id: %u\n", wp_proxy_get_global_id (WP_PROXY (node)));

    dict = wp_properties_peek_dict (props);
    spa_dict_for_each (item, dict) {
      g_print ("    %s = \"%s\"\n", item->key, item->value);
    }

    g_print ("\n");
  }

  g_main_loop_quit (d->loop);
}

static void
remote_state_changed (WpCore *core, WpRemoteState state,
    struct WpCliData * d)
{
  switch (state) {
  case WP_REMOTE_STATE_UNCONNECTED:
    g_main_loop_quit (d->loop);
    break;
  case WP_REMOTE_STATE_ERROR:
    g_message ("pipewire remote error");
    g_main_loop_quit (d->loop);
    break;
  default:
    break;
  }
}

gint
main (gint argc, gchar **argv)
{
  struct WpCliData data = {0};
  g_autoptr (GOptionContext) context = NULL;
  g_autoptr (GError) error = NULL;
  g_autoptr (WpCore) core = NULL;
  g_autoptr (WpObjectManager) om = NULL;
  g_autoptr (GMainLoop) loop = NULL;

  context = g_option_context_new ("- PipeWire Session/Policy Manager Helper CLI");
  g_option_context_add_main_entries (context, entries, NULL);
  if (!g_option_context_parse (context, &argc, &argv, &error)) {
    return 1;
  }

  data.core = core = wp_core_new (NULL, NULL);
  g_signal_connect (core, "remote-state-changed",
      (GCallback) remote_state_changed, &data);

  om = wp_object_manager_new ();
  wp_object_manager_add_proxy_interest (om, PW_TYPE_INTERFACE_Node, NULL,
      WP_PROXY_FEATURE_INFO);
  g_signal_connect (om, "objects-changed",
      (GCallback) on_objects_changed, &data);
  wp_core_install_object_manager (core, om);

  wp_core_connect (core);

  data.loop = loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (loop);

  return 0;
}
