/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * module-pw-audio-client provides a WpEndpoint implementation
 * that wraps an audio client node in pipewire into an endpoint
 */

#include <wp/wp.h>
#include <pipewire/pipewire.h>

struct module_data
{
  WpObjectManager *om;
  GHashTable *registered_endpoints;
};

static void
on_endpoint_created(GObject *initable, GAsyncResult *res, gpointer d)
{
  struct module_data *data = d;
  g_autoptr (WpEndpoint) endpoint = NULL;
  g_autoptr (WpProxy) proxy = NULL;
  guint global_id = 0;
  GError *error = NULL;

  /* Get the endpoint */
  endpoint = wp_endpoint_new_finish(initable, res, &error);
  if (error) {
    g_warning ("Failed to create client endpoint: %s", error->message);
    return;
  }

  /* Get the endpoint global id */
  g_object_get (endpoint, "proxy-node", &proxy, NULL);
  global_id = wp_proxy_get_global_id (proxy);

  g_debug ("Created client endpoint for global id %d", global_id);

  /* Register the endpoint and add it to the table */
  wp_endpoint_register (endpoint);
  g_hash_table_insert (data->registered_endpoints, GUINT_TO_POINTER(global_id),
      g_steal_pointer (&endpoint));
}

static void
on_node_added (WpObjectManager *om, WpProxy *proxy, gpointer d)
{
  struct module_data *data = d;
  const gchar *name, *media_class;
  enum pw_direction direction;
  GVariantBuilder b;
  g_autoptr (GVariant) endpoint_props = NULL;
  g_autoptr (WpProperties) props = wp_proxy_get_global_properties (proxy);
  g_autoptr (WpCore) core = NULL;

  /* Get the media_class */
  media_class = wp_properties_get (props, PW_KEY_MEDIA_CLASS);

  /* Only handle client Stream nodes */
  if (!g_str_has_prefix (media_class, "Stream/"))
    return;

  /* Get the direction */
  if (g_str_has_prefix (media_class, "Stream/Input")) {
    direction = PW_DIRECTION_INPUT;
  } else if (g_str_has_prefix (media_class, "Stream/Output")) {
    direction = PW_DIRECTION_OUTPUT;
  } else {
    g_critical ("failed to parse direction");
    return;
  }

  /* Get the name */
  name = wp_properties_get (props, PW_KEY_MEDIA_NAME);
  if (!name)
    name = wp_properties_get (props, PW_KEY_NODE_NAME);

  /* Set the properties */
  g_variant_builder_init (&b, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&b, "{sv}",
      "name", g_variant_new_take_string (g_strdup_printf ("%s", name)));
  g_variant_builder_add (&b, "{sv}",
      "media-class", g_variant_new_string (media_class));
  g_variant_builder_add (&b, "{sv}",
      "direction", g_variant_new_uint32 (direction));
  g_variant_builder_add (&b, "{sv}",
      "proxy-node", g_variant_new_uint64 ((guint64) proxy));
  endpoint_props = g_variant_builder_end (&b);

  /* Create the endpoint async */
  g_object_get (om, "core", &core, NULL);
  wp_factory_make (core, "pw-audio-softdsp-endpoint", WP_TYPE_ENDPOINT,
      endpoint_props, on_endpoint_created, data);
}

static void
on_node_removed (WpObjectManager *om, WpProxy *proxy, gpointer d)
{
  struct module_data *data = d;
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
module_destroy (gpointer d)
{
  struct module_data *data = d;

  g_clear_object (&data->om);

  /* Destroy the registered endpoints table */
  g_clear_pointer (&data->registered_endpoints, g_hash_table_unref);

  /* Clean up */
  g_slice_free (struct module_data, data);
}

void
wireplumber__module_init (WpModule * module, WpCore * core, GVariant * args)
{
  struct module_data *data;

  /* Create the module data */
  data = g_slice_new0 (struct module_data);
  data->om = wp_object_manager_new ();
  data->registered_endpoints = g_hash_table_new_full (g_direct_hash,
      g_direct_equal, NULL, (GDestroyNotify) g_object_unref);

  /* Set the module destroy callback */
  wp_module_set_destroy_callback (module, module_destroy, data);

  /* Register the global added/removed callbacks */
  g_signal_connect(data->om, "object-added",
      (GCallback) on_node_added, data);
  g_signal_connect(data->om, "object-removed",
      (GCallback) on_node_removed, data);

  //TODO add constraints & features
  wp_object_manager_add_proxy_interest (data->om, PW_TYPE_INTERFACE_Node, NULL,
      0);
  wp_core_install_object_manager (core, data->om);
}
