/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * module-pw-bluez provides bluetooth device detection through pipewire
 * and automatically creates pipewire audio nodes to play and capture audio
 */

#include <spa/utils/keys.h>
#include <spa/utils/names.h>
#include <spa/monitor/monitor.h>
#include <pipewire/pipewire.h>
#include <wp/wp.h>

enum wp_bluez_profile {
  WP_BLUEZ_A2DP = 0,
  WP_BLUEZ_HEADUNIT = 1,  /* HSP/HFP Head Unit (Headsets) */
  WP_BLUEZ_GATEWAY = 2    /* HSP/HFP Gateway (Phones) */
};

struct impl {
  WpModule *module;
  GHashTable *registered_endpoints;
};

static void
on_endpoint_created(GObject *initable, GAsyncResult *res, gpointer d)
{
  struct impl *data = d;
  WpEndpoint *endpoint = NULL;
  guint global_id = 0;
  GError *error = NULL;

  /* Get the endpoint */
  endpoint = wp_endpoint_new_finish(initable, res, NULL);
  g_return_if_fail (endpoint);

  /* Check for error */
  if (error) {
    g_clear_object (&endpoint);
    g_warning ("Failed to create client endpoint: %s", error->message);
    return;
  }

  /* Get the endpoint global id */
  g_object_get (endpoint, "global-id", &global_id, NULL);
  g_debug ("Created bluetooth endpoint for global id %d", global_id);

  /* Register the endpoint and add it to the table */
  wp_endpoint_register (endpoint);
  g_hash_table_insert (data->registered_endpoints, GUINT_TO_POINTER(global_id),
      endpoint);
}


static gboolean
parse_bluez_properties (WpProperties *props, const gchar **name,
    const gchar **media_class, enum pw_direction *direction)
{
  const char *local_name = NULL;
  const char *local_media_class = NULL;
  enum pw_direction local_direction;
  enum wp_bluez_profile profile;

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

  /* Get the bluez profile */
  if (g_str_has_suffix (local_name, "a2dp-source") ||
      g_str_has_suffix (local_name, "a2dp-sink"))
    profile = WP_BLUEZ_A2DP;
  else if (g_str_has_suffix (local_name, "hsp-hs") ||
           g_str_has_suffix (local_name, "hfp-hf"))
    profile = WP_BLUEZ_HEADUNIT;
  else if (g_str_has_suffix (local_name, "hsp-ag") ||
           g_str_has_suffix (local_name, "hfp-ag"))
    profile = WP_BLUEZ_GATEWAY;
  else
    return FALSE;

  /* Set the name */
  if (name)
    *name = local_name;

  /* Set the media class */
  if (media_class) {
    switch (local_direction) {
    case PW_DIRECTION_INPUT:
      switch (profile) {
      case WP_BLUEZ_A2DP:
        *media_class = "Bluez/Sink/A2dp";
        break;
      case WP_BLUEZ_HEADUNIT:
        *media_class = "Bluez/Sink/Headunit";
        break;
      case WP_BLUEZ_GATEWAY:
        *media_class = "Bluez/Sink/Gateway";
        break;
      default:
        break;
      }
      break;

    case PW_DIRECTION_OUTPUT:
      switch (profile) {
      case WP_BLUEZ_A2DP:
        *media_class = "Bluez/Source/A2dp";
        break;
      case WP_BLUEZ_HEADUNIT:
        *media_class = "Bluez/Source/Headunit";
        break;
      case WP_BLUEZ_GATEWAY:
        *media_class = "Bluez/Source/Gateway";
        break;
      }
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
is_bluez_node (WpProperties *props)
{
  const gchar *name = wp_properties_get (props, PW_KEY_NODE_NAME);
  return g_str_has_prefix (name, "api.bluez5");
}

static void
on_node_added (WpCore *core, WpProxy *proxy, struct impl *data)
{
  const gchar *name, *media_class;
  enum pw_direction direction;
  GVariantBuilder b;
  g_autoptr (WpProperties) props = NULL;
  g_autoptr (GVariant) endpoint_props = NULL;
  guint32 id = wp_proxy_get_global_id (proxy);

  props = wp_proxy_get_global_properties (proxy);
  g_return_if_fail(props);

  /* Only handle bluez nodes */
  if (!is_bluez_node (props))
    return;

  /* Parse the bluez properties */
  if (!parse_bluez_properties (props, &name, &media_class, &direction)) {
    g_critical ("failed to parse bluez properties");
    return;
  }

  /* Set the properties */
  g_variant_builder_init (&b, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&b, "{sv}",
      "name", g_variant_new_take_string (
          g_strdup_printf ("Bluez %u (%s)", id, name)));
  g_variant_builder_add (&b, "{sv}",
      "media-class", g_variant_new_string (media_class));
  g_variant_builder_add (&b, "{sv}",
      "direction", g_variant_new_uint32 (direction));
  g_variant_builder_add (&b, "{sv}",
      "proxy-node", g_variant_new_uint64 ((guint64) proxy));
  endpoint_props = g_variant_builder_end (&b);

  /* Create the endpoint async */
  wp_factory_make (core, "pw-audio-softdsp-endpoint", WP_TYPE_ENDPOINT,
      endpoint_props, on_endpoint_created, data);
}

static void
on_node_removed (WpCore *core, WpProxy *proxy, struct impl *data)
{
  WpEndpoint *endpoint = NULL;
  guint32 id = wp_proxy_get_global_id (proxy);

  /* Get the endpoint */
  endpoint = g_hash_table_lookup (data->registered_endpoints,
      GUINT_TO_POINTER(id));
  if (!endpoint)
    return;

  /* Unregister the endpoint and remove it from the table */
  wp_endpoint_unregister (endpoint);
  g_hash_table_remove (data->registered_endpoints, GUINT_TO_POINTER(id));
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

  /* Set destroy callback for impl */
  wp_module_set_destroy_callback (module, module_destroy, impl);

  /* Register the global addded/removed callbacks */
  g_signal_connect(core, "remote-global-added::node",
      (GCallback) on_node_added, impl);
  g_signal_connect(core, "remote-global-removed::node",
      (GCallback) on_node_removed, impl);
}
