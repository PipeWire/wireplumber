/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * module-pw-alsa-udev provides alsa device detection through pipewire
 * and automatically creates endpoints for all alsa device nodes that appear
 */

#include <spa/utils/keys.h>
#include <spa/utils/names.h>
#include <spa/monitor/monitor.h>
#include <pipewire/pipewire.h>
#include <wp/wp.h>

struct impl
{
  WpModule *module;
  GHashTable *registered_endpoints;
  GVariant *streams;
};

static void
on_endpoint_created(GObject *initable, GAsyncResult *res, gpointer d)
{
  struct impl *impl = d;
  g_autoptr (WpEndpoint) endpoint = NULL;
  g_autoptr (WpProxy) proxy = NULL;
  guint global_id = 0;
  GError *error = NULL;

  /* Get the endpoint */
  endpoint = wp_endpoint_new_finish(initable, res, &error);
  if (error) {
    g_warning ("Failed to create alsa endpoint: %s", error->message);
    return;
  }

  /* Get the endpoint global id */
  g_object_get (endpoint, "proxy-node", &proxy, NULL);
  global_id = wp_proxy_get_global_id (proxy);

  g_debug ("Created alsa endpoint for global id %d", global_id);

  /* Register the endpoint and add it to the table */
  wp_endpoint_register (endpoint);
  g_hash_table_insert (impl->registered_endpoints, GUINT_TO_POINTER(global_id),
      g_steal_pointer (&endpoint));
}

static gboolean
parse_alsa_properties (WpProperties *props, const gchar **name,
    const gchar **media_class, enum pw_direction *direction)
{
  const char *local_name = NULL;
  const char *local_media_class = NULL;
  enum pw_direction local_direction;

  /* Get the name */
  local_name = wp_properties_get (props, PW_KEY_NODE_NAME);
  if (!local_name)
    return FALSE;

  /* Get the media class */
  local_media_class = wp_properties_get (props, PW_KEY_MEDIA_CLASS);
  if (!local_media_class)
    return FALSE;

  /* Get the direction */
  if (g_str_has_prefix (local_media_class, "Audio/Sink"))
    local_direction = PW_DIRECTION_INPUT;
  else if (g_str_has_prefix (local_media_class, "Audio/Source"))
    local_direction = PW_DIRECTION_OUTPUT;
  else
    return FALSE;

  /* Set the name */
  if (name)
    *name = local_name;

  /* Set the media class */
  if (media_class) {
    switch (local_direction) {
    case PW_DIRECTION_INPUT:
      *media_class = "Alsa/Sink";
      break;
    case PW_DIRECTION_OUTPUT:
      *media_class = "Alsa/Source";
      break;
    default:
      break;
    }
  }

  /* Set the direction */
  if (direction)
    *direction = local_direction;

  return TRUE;
}

static inline gboolean
is_alsa_node (WpProperties * props)
{
  const gchar *name = wp_properties_get (props, PW_KEY_NODE_NAME);
  return g_str_has_prefix (name, "api.alsa");
}

static void
on_node_added(WpCore *core, WpProxy *proxy, struct impl *impl)
{
  const gchar *media_class, *name;
  enum pw_direction direction;
  GVariantBuilder b;
  g_autoptr (WpProperties) props = NULL;
  g_autoptr (GVariant) endpoint_props = NULL;

  props = wp_proxy_get_global_properties (proxy);
  g_return_if_fail(props);

  /* Only handle alsa nodes */
  if (!is_alsa_node (props))
    return;

  /* Parse the alsa properties */
  if (!parse_alsa_properties (props, &name, &media_class, &direction)) {
    g_critical ("failed to parse alsa properties");
    return;
  }

  /* Set the properties */
  g_variant_builder_init (&b, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&b, "{sv}",
      "name", g_variant_new_take_string (g_strdup_printf (
              "Alsa %u (%s)", wp_proxy_get_global_id (proxy), name)));
  g_variant_builder_add (&b, "{sv}",
      "media-class", g_variant_new_string (media_class));
  g_variant_builder_add (&b, "{sv}",
      "direction", g_variant_new_uint32 (direction));
  g_variant_builder_add (&b, "{sv}",
      "proxy-node", g_variant_new_uint64 ((guint64) proxy));
  g_variant_builder_add (&b, "{sv}",
      "streams", impl->streams);
  endpoint_props = g_variant_builder_end (&b);

  /* Create the endpoint async */
  wp_factory_make (core, "pw-audio-softdsp-endpoint", WP_TYPE_ENDPOINT,
      endpoint_props, on_endpoint_created, impl);
}

static void
on_node_removed (WpCore *core, WpProxy *proxy, struct impl *impl)
{
  WpEndpoint *endpoint = NULL;
  guint32 id = wp_proxy_get_global_id (proxy);

  /* Get the endpoint */
  endpoint = g_hash_table_lookup (impl->registered_endpoints,
      GUINT_TO_POINTER(id));
  if (!endpoint)
    return;

  /* Unregister the endpoint and remove it from the table */
  wp_endpoint_unregister (endpoint);
  g_hash_table_remove (impl->registered_endpoints, GUINT_TO_POINTER(id));
}

static void
module_destroy (gpointer data)
{
  struct impl *impl = data;

  /* Set to NULL as we don't own the reference */
  impl->module = NULL;

  /* Destroy the registered endpoints table */
  g_hash_table_unref(impl->registered_endpoints);
  impl->registered_endpoints = NULL;

  g_clear_pointer (&impl->streams, g_variant_unref);

  /* Clean up */
  g_slice_free (struct impl, impl);
}

void
wireplumber__module_init (WpModule * module, WpCore * core, GVariant * args)
{
  struct impl *impl;

  /* Create the module data */
  impl = g_slice_new0(struct impl);
  impl->module = module;
  impl->registered_endpoints = g_hash_table_new_full (g_direct_hash,
      g_direct_equal, NULL, (GDestroyNotify)g_object_unref);
  impl->streams = g_variant_lookup_value (args, "streams",
      G_VARIANT_TYPE ("as"));

  /* Set destroy callback for impl */
  wp_module_set_destroy_callback (module, module_destroy, impl);

  /* Register the global addded/removed callbacks */
  g_signal_connect(core, "remote-global-added::node",
      (GCallback) on_node_added, impl);
  g_signal_connect(core, "remote-global-removed::node",
      (GCallback) on_node_removed, impl);
}
