/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * module-pipewire provides basic integration between wireplumber and pipewire.
 * It provides the pipewire core and remote, connects to pipewire and provides
 * the most primitive implementations of WpEndpoint and WpEndpointLink
 */

#include <wp/wp.h>
#include <pipewire/pipewire.h>

void remote_endpoint_init (WpCore * core, struct pw_core * pw_core,
    struct pw_remote * remote);
void simple_endpoint_factory (WpFactory * factory, GType type,
    GVariant * properties, GAsyncReadyCallback ready, gpointer user_data);
void simple_endpoint_link_factory (WpFactory * factory, GType type,
    GVariant * properties, GAsyncReadyCallback ready, gpointer user_data);

struct module_data
{
  WpModule *module;
  WpRemotePipewire *remote_pipewire;
  GHashTable *registered_endpoints;
};

static void
on_endpoint_created(GObject *initable, GAsyncResult *res, gpointer d)
{
  struct module_data *data = d;
  WpEndpoint *endpoint = NULL;
  guint global_id = 0;

  /* Get the endpoint */
  endpoint = wp_endpoint_new_finish(initable, res, NULL);
  if (!endpoint)
    return;

  /* Get the endpoint global id */
  g_object_get (endpoint, "global-id", &global_id, NULL);
  g_debug ("Created client endpoint for global id %d", global_id);

  /* Register the endpoint and add it to the table */
  wp_endpoint_register (endpoint);
  g_hash_table_insert (data->registered_endpoints, GUINT_TO_POINTER(global_id),
      endpoint);
}

static void
on_node_added (WpRemotePipewire *rp, guint id, guint parent_id, gconstpointer p,
    gpointer d)
{
  struct module_data *data = d;
  const struct spa_dict *props = p;
  g_autoptr (WpCore) core = wp_module_get_core (data->module);
  const gchar *name, *media_class;
  GVariantBuilder b;
  g_autoptr (GVariant) endpoint_props = NULL;

  /* Make sure the node has properties */
  g_return_if_fail(props);

  /* Get the media_class */
  media_class = spa_dict_lookup(props, "media.class");

  /* Only handle client Stream nodes */
  if (!g_str_has_prefix (media_class, "Stream/"))
    return;

  /* Get the name */
  name = spa_dict_lookup (props, "media.name");
  if (!name)
    name = spa_dict_lookup (props, "node.name");

  /* Set the properties */
  g_variant_builder_init (&b, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&b, "{sv}",
      "name", name ?
      g_variant_new_take_string (g_strdup_printf ("Stream %u (%s)", id, name)) :
      g_variant_new_take_string (g_strdup_printf ("Stream %u", id)));
  g_variant_builder_add (&b, "{sv}",
      "media-class", g_variant_new_string (media_class));
  g_variant_builder_add (&b, "{sv}",
      "global-id", g_variant_new_uint32 (id));
  endpoint_props = g_variant_builder_end (&b);

  /* Create the endpoint async */
  wp_factory_make (core, "pipewire-simple-endpoint", WP_TYPE_ENDPOINT,
      endpoint_props, on_endpoint_created, data);
}

static void
on_global_removed (WpRemotePipewire *rp, guint id, gpointer d)
{
  struct module_data *data = d;
  WpEndpoint *endpoint = NULL;

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

  /* Set to NULL module and remote pipewire as we don't own the reference */
  data->module = NULL;
  data->remote_pipewire = NULL;

  /* Destroy the registered endpoints table */
  g_hash_table_unref(data->registered_endpoints);
  data->registered_endpoints = NULL;

  /* Clean up */
  g_slice_free (struct module_data, data);
}

void
wireplumber__module_init (WpModule * module, WpCore * core, GVariant * args)
{
  struct module_data *data;
  WpRemotePipewire *rp;
  struct pw_core *pw_core;
  struct pw_remote *pw_remote;

  /* Make sure the remote pipewire is valid */
  rp = wp_core_get_global (core, WP_GLOBAL_REMOTE_PIPEWIRE);
  if (!rp) {
    g_critical ("module-pipewire cannot be loaded without a registered "
        "WpRemotePipewire object");
    return;
  }

  /* Create the module data */
  data = g_slice_new0 (struct module_data);
  data->module = module;
  data->remote_pipewire = rp;
  data->registered_endpoints = g_hash_table_new_full (g_direct_hash,
      g_direct_equal, NULL, (GDestroyNotify)g_object_unref);

  /* Set the module destroy callback */
  wp_module_set_destroy_callback (module, module_destroy, data);

  /* Register the global added/removed callbacks */
  g_signal_connect(rp, "global-added::node", (GCallback)on_node_added, data);
  g_signal_connect(rp, "global-removed", (GCallback)on_global_removed, data);

  /* Init remoted endpoint */
  g_object_get (rp, "pw-core", &pw_core, "pw-remote", &pw_remote, NULL);
  remote_endpoint_init (core, pw_core, pw_remote);

  /* Register simple-endpoint and simple-endpoint-link */
  wp_factory_new (core, "pipewire-simple-endpoint",
      simple_endpoint_factory);
  wp_factory_new (core, "pipewire-simple-endpoint-link",
      simple_endpoint_link_factory);
}
