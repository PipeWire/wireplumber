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

#include <wp/wp.h>
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>

struct impl
{
  WpCore *core;

  /* Remote */
  struct spa_hook remote_listener;

  /* Registry */
  struct pw_registry_proxy *registry_proxy;
  struct spa_hook registry_listener;

  /* The alsa node proxies */
  GHashTable *alsa_nodes_info;
};

struct endpoint_info
{
  gchar *name;
  gchar *media_class;
};

struct proxy_info
{
  const struct impl *impl;
  uint32_t node_id;
  WpProxyPort *proxy_port;
};

static void
endpoint_info_destroy(gpointer p)
{
  struct endpoint_info *ei = p;

  /* Free the name */
  g_free (ei->name);

  /* Free the media class */
  g_free (ei->media_class);

  /* Clean up */
  g_slice_free (struct endpoint_info, p);
}

static void
proxy_info_destroy(gpointer p)
{
  struct proxy_info *pi = p;

  /* Unref the proxy port */
  g_clear_object (&pi->proxy_port);

  /* Clean up */
  g_slice_free (struct proxy_info, p);
}

static void
proxy_node_created(GObject *initable, GAsyncResult *res, gpointer data)
{
  struct proxy_info *pi = data;
  const struct impl *impl = pi->impl;
  g_autoptr(WpProxyNode) proxy_node = NULL;
  struct endpoint_info *ei = NULL;
  GVariantBuilder b;
  g_autoptr(GVariant) endpoint_props = NULL;
  WpEndpoint *endpoint = NULL;

  /* Get the proxy */
  proxy_node = wp_proxy_node_new_finish(initable, res, NULL);
  if (!proxy_node)
    return;

  /* Register the proxy node */
  wp_proxy_register(WP_PROXY(proxy_node));

  /* Get the alsa node info */
  ei = g_hash_table_lookup(impl->alsa_nodes_info, GINT_TO_POINTER(pi->node_id));
  if (!ei)
    return;

  /* Build the properties for the endpoint */
  g_variant_builder_init (&b, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&b, "{sv}", "name",
      g_variant_new_take_string (g_strdup_printf ("Endpoint %u: %s",
          pi->node_id, ei->name)));
  g_variant_builder_add (&b, "{sv}",
      "media-class", g_variant_new_string (ei->media_class));
  g_variant_builder_add (&b, "{sv}",
      "proxy-node", g_variant_new_uint64 ((guint64) proxy_node));
  g_variant_builder_add (&b, "{sv}",
      "proxy-port", g_variant_new_uint64 ((guint64) pi->proxy_port));
  endpoint_props = g_variant_builder_end (&b);

  /* Create and register the endpoint */
  endpoint = wp_factory_make (impl->core, "pw-audio-softdsp-endpoint",
      WP_TYPE_ENDPOINT, endpoint_props);

  /* Register the endpoint */
  wp_endpoint_register (endpoint);

  /* Clean up */
  proxy_info_destroy (pi);
}

static void
proxy_port_created(GObject *initable, GAsyncResult *res, gpointer data)
{
  struct proxy_info *pi = data;
  const struct impl *impl = pi->impl;
  WpProxyPort *proxy_port = NULL;
  struct pw_proxy *proxy = NULL;

  /* Get the proxy port */
  proxy_port = wp_proxy_port_new_finish(initable, res, NULL);
  if (!proxy_port)
    return;

  /* Register the proxy port */
  wp_proxy_register(WP_PROXY(proxy_port));

  /* Forward the proxy port */
  pi->proxy_port = proxy_port;

  /* Get the node proxy */
  proxy = pw_registry_proxy_bind (impl->registry_proxy, pi->node_id,
      PW_TYPE_INTERFACE_Node, PW_VERSION_NODE, 0);
  if (!proxy)
    return;

  /* Create the proxy node asynchronically */
  wp_proxy_node_new(impl->core, proxy, proxy_node_created, pi);
}

static void
handle_node(struct impl *impl, uint32_t id, uint32_t parent_id,
            const struct spa_dict *props)
{
  const gchar *media_class = NULL, *name = NULL;
  struct endpoint_info *ei = NULL;

  /* Make sure the node has properties */
  if (!props) {
    g_warning("node has no properties, skipping...");
    return;
  }

  /* Get the name and media_class */
  name = spa_dict_lookup(props, "node.name");
  media_class = spa_dict_lookup(props, "media.class");

  /* Make sure the media class is non-dsp audio */
  if (!g_str_has_prefix (media_class, "Audio/"))
    return;
  if (g_str_has_prefix (media_class, "Audio/DSP"))
    return;

  /* Create the endpoint info */
  ei = g_slice_new0 (struct endpoint_info);
  ei->name = g_strdup(name);
  ei->media_class = g_strdup(media_class);

  /* Insert the alsa node info in the hash table */
  g_hash_table_insert(impl->alsa_nodes_info, GINT_TO_POINTER (id), ei);
}

static void
handle_port(struct impl *impl, uint32_t id, uint32_t parent_id,
            const struct spa_dict *props)
{
  struct proxy_info *pi = NULL;
  struct pw_proxy *proxy = NULL;

  /* Only handle ports whose parent is an alsa node */
  if (!g_hash_table_contains(impl->alsa_nodes_info, GINT_TO_POINTER (parent_id)))
    return;

  /* Make sure the port has porperties */
  if (!props)
    return;

  /* Get the port proxy */
  proxy = pw_registry_proxy_bind (impl->registry_proxy, id,
      PW_TYPE_INTERFACE_Port, PW_VERSION_PORT, 0);
  if (!proxy)
    return;

  /* Create the port info */
  pi = g_slice_new0 (struct proxy_info);
  pi->impl = impl;
  pi->node_id = parent_id;
  pi->proxy_port = NULL;

  /* Create the proxy port asynchronically */
  wp_proxy_port_new(impl->core, proxy, proxy_port_created, pi);
}

static void
registry_global(void *data, uint32_t id, uint32_t parent_id,
		uint32_t permissions, uint32_t type, uint32_t version,
		const struct spa_dict *props)
{
  struct impl *impl = data;

  switch (type) {
  case PW_TYPE_INTERFACE_Node:
    handle_node(impl, id, parent_id, props);
    break;

  case PW_TYPE_INTERFACE_Port:
    handle_port(impl, id, parent_id, props);
    break;

  default:
    break;
  }
}

static const struct pw_registry_proxy_events registry_events = {
  PW_VERSION_REGISTRY_PROXY_EVENTS,
  .global = registry_global,
};

static void
on_connected (WpRemote *remote, WpRemoteState state, struct impl *impl)
{
  struct pw_core_proxy *core_proxy = NULL;
  struct pw_remote *pw_remote;

  g_object_get (remote, "pw-remote", &pw_remote, NULL);

  core_proxy = pw_remote_get_core_proxy (pw_remote);
  impl->registry_proxy = pw_core_proxy_get_registry (core_proxy,
      PW_TYPE_INTERFACE_Registry, PW_VERSION_REGISTRY, 0);
  pw_registry_proxy_add_listener(impl->registry_proxy,
      &impl->registry_listener, &registry_events, impl);
}

static void
module_destroy (gpointer data)
{
  struct impl *impl = data;

  /* Destroy the hash table */
  g_hash_table_unref (impl->alsa_nodes_info);

  /* Clean up */
  g_slice_free (struct impl, impl);
}

struct impl *
module_create (WpCore * core)
{
  struct impl *impl;
  WpRemote *remote;

  /* Allocate impl */
  impl = g_new0(struct impl, 1);

  /* Set core */
  impl->core = core;

  /* Set remote */
  remote = wp_core_get_global(core, WP_GLOBAL_REMOTE_PIPEWIRE);
  g_signal_connect (remote, "state-changed::connected",
      (GCallback) on_connected, impl);

  /* Create the hash table */
  impl->alsa_nodes_info = g_hash_table_new_full (g_direct_hash,
      g_direct_equal, NULL, endpoint_info_destroy);

  /* Return the module */
  return impl;
}

void
wireplumber__module_init (WpModule * module, WpCore * core, GVariant * args)
{
  /* Create the impl */
  struct impl *impl = module_create (core);

  /* Set destroy callback for impl */
  wp_module_set_destroy_callback (module, module_destroy, impl);
}
