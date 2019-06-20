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
  WpModule *module;
  WpRemotePipewire *remote_pipewire;
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
unregister_endpoint (WpProxy* wp_proxy, WpEndpoint *endpoint)
{
  g_return_if_fail(WP_IS_PROXY(wp_proxy));
  g_return_if_fail(WP_IS_ENDPOINT(endpoint));

  /* Unregister the endpoint */
  wp_endpoint_unregister(endpoint);
}

static void
proxy_node_created(GObject *initable, GAsyncResult *res, gpointer data)
{
  struct proxy_info *pi = data;
  const struct impl *impl = pi->impl;
  g_autoptr (WpCore) core = wp_module_get_core (impl->module);
  g_autoptr(WpProxyNode) proxy_node = NULL;
  struct endpoint_info *ei = NULL;
  GVariantBuilder b;
  g_autoptr (GVariant) endpoint_props = NULL;
  g_autoptr (WpEndpoint) endpoint = NULL;

  /* Get the proxy */
  proxy_node = wp_proxy_node_new_finish(initable, res, NULL);
  if (!proxy_node)
    return;

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
  endpoint = wp_factory_make (core, "pw-audio-softdsp-endpoint",
      WP_TYPE_ENDPOINT, endpoint_props);

  /* Register the endpoint */
  wp_endpoint_register (endpoint);

  /* Set destroy handler to unregister endpoint when the proxy is detroyed */
  g_signal_connect (proxy_node, "destroyed", G_CALLBACK(unregister_endpoint),
      endpoint);

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

  /* Forward the proxy port */
  pi->proxy_port = proxy_port;

  /* Get the node proxy */
  proxy = wp_remote_pipewire_proxy_bind (impl->remote_pipewire, pi->node_id,
      PW_TYPE_INTERFACE_Node);
  if (!proxy)
    return;

  /* Create the proxy node asynchronically */
  wp_proxy_node_new(pi->node_id, proxy, proxy_node_created, pi);
}

static void
handle_node(WpRemotePipewire *rp, guint id, guint parent_id, gconstpointer p,
    gpointer d)
{
  struct impl *impl = d;
  const struct spa_dict *props = p;
  const gchar *media_class = NULL, *name = NULL;
  struct endpoint_info *ei = NULL;

  /* Make sure the node has properties */
  g_return_if_fail(props);

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
handle_port(WpRemotePipewire *rp, guint id, guint parent_id, gconstpointer p,
    gpointer d)
{
  struct impl *impl = d;
  struct proxy_info *pi = NULL;
  struct pw_proxy *proxy = NULL;

  /* Only handle ports whose parent is an alsa node */
  if (!g_hash_table_contains(impl->alsa_nodes_info, GINT_TO_POINTER (parent_id)))
    return;

  /* Get the port proxy */
  proxy = wp_remote_pipewire_proxy_bind (rp, id, PW_TYPE_INTERFACE_Port);
  if (!proxy)
    return;

  /* Create the port info */
  pi = g_slice_new0 (struct proxy_info);
  pi->impl = impl;
  pi->node_id = parent_id;
  pi->proxy_port = NULL;

  /* Create the proxy port asynchronically */
  wp_proxy_port_new(id, proxy, proxy_port_created, pi);
}

static void
module_destroy (gpointer data)
{
  struct impl *impl = data;

  /* Set to NULL module and remote pipewire as we don't own the reference */
  impl->module = NULL;
  impl->remote_pipewire = NULL;

  /* Destroy the hash table */
  g_hash_table_unref (impl->alsa_nodes_info);
  impl->alsa_nodes_info = NULL;

  /* Clean up */
  g_slice_free (struct impl, impl);
}

void
wireplumber__module_init (WpModule * module, WpCore * core, GVariant * args)
{
  struct impl *impl;
  WpRemotePipewire *rp;

  /* Make sure the remote pipewire is valid */
  rp = wp_core_get_global (core, WP_GLOBAL_REMOTE_PIPEWIRE);
  if (!rp) {
    g_critical ("module-pw-alsa-udev cannot be loaded without a registered "
        "WpRemotePipewire object");
    return;
  }

  /* Create the module data */
  impl = g_slice_new0(struct impl);
  impl->module = module;
  impl->remote_pipewire = rp;
  impl->alsa_nodes_info = g_hash_table_new_full (g_direct_hash,
      g_direct_equal, NULL, endpoint_info_destroy);

  /* Set destroy callback for impl */
  wp_module_set_destroy_callback (module, module_destroy, impl);

  /* Register the global addded callbacks */
  g_signal_connect(rp, "global-added::node", (GCallback)handle_node, impl);
  g_signal_connect(rp, "global-added::port", (GCallback)handle_port, impl);
}
