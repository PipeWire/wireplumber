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
#include <spa/param/audio/format-utils.h>

void remote_endpoint_init (WpCore * core, struct pw_core * pw_core,
    struct pw_remote * remote);
gpointer simple_endpoint_factory (WpFactory * factory, GType type,
    GVariant * properties);
gpointer simple_endpoint_link_factory (WpFactory * factory, GType type,
    GVariant * properties);

struct module_data
{
  WpModule *module;

  /* Registry */
  struct pw_registry_proxy *registry_proxy;
  struct spa_hook registry_listener;

  /* Client nodes info */
  GHashTable *client_nodes_info;
};

struct endpoint_info
{
  gchar *name;
  gchar *media_class;
  const struct pw_proxy *proxy;
};

struct proxy_info
{
  const struct module_data *data;
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
proxy_node_created(GObject *initable, GAsyncResult *res, gpointer d)
{
  struct proxy_info *pi = d;
  const struct module_data *data = pi->data;
  g_autoptr (WpCore) core = wp_module_get_core (data->module);
  g_autoptr (WpProxyNode) proxy_node = NULL;
  struct endpoint_info *ei = NULL;
  WpEndpoint *endpoint = NULL;
  g_autoptr (GVariant) endpoint_props = NULL;
  GVariantBuilder b;

  /* Get the proxy */
  proxy_node = wp_proxy_node_new_finish(initable, res, NULL);
  if (!proxy_node)
    return;

  /* Get the client node info */
  ei = g_hash_table_lookup(data->client_nodes_info,
      GINT_TO_POINTER(pi->node_id));
  if (!ei)
    return;

  /* Set the properties */
  g_variant_builder_init (&b, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&b, "{sv}",
      "name", ei->name ? g_variant_new_string (ei->name) :
          g_variant_new_take_string (
              g_strdup_printf ("Stream %u", pi->node_id)));
  g_variant_builder_add (&b, "{sv}",
      "media-class", g_variant_new_string (ei->media_class));
  g_variant_builder_add (&b, "{sv}",
      "proxy-node", g_variant_new_uint64 ((guint64) proxy_node));
  g_variant_builder_add (&b, "{sv}",
      "proxy-port", g_variant_new_uint64 ((guint64) pi->proxy_port));
  endpoint_props = g_variant_builder_end (&b);

  /* Create the endpoint */
  endpoint = wp_factory_make (core, "pipewire-simple-endpoint",
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
proxy_port_created(GObject *initable, GAsyncResult *res, gpointer d)
{
  struct proxy_info *pi = d;
  const struct module_data *data = pi->data;
  WpProxyPort *proxy_port = NULL;
  struct pw_proxy *proxy = NULL;

  /* Get the proxy port */
  proxy_port = wp_proxy_port_new_finish(initable, res, NULL);
  if (!proxy_port)
    return;

  /* Forward the proxy port */
  pi->proxy_port = proxy_port;

  /* Get the node proxy */
  proxy = pw_registry_proxy_bind (data->registry_proxy, pi->node_id,
      PW_TYPE_INTERFACE_Node, PW_VERSION_NODE, 0);
  if (!proxy)
    return;

  /* Create the proxy node asynchronically */
  wp_proxy_node_new(proxy, proxy_node_created, pi);
}

static void
handle_node (struct module_data *data, uint32_t id, uint32_t parent_id,
    const struct spa_dict * props)
{
  struct endpoint_info *ei = NULL;
  const gchar *name;
  const gchar *media_class;
  struct pw_proxy *proxy;
  struct spa_audio_info_raw format = { 0, };
  struct spa_pod *param;
  struct spa_pod_builder pod_builder = { 0, };
  char buf[1024];

  /* Make sure the node has properties */
  if (!props) {
    g_warning("node has no properties, skipping...");
    return;
  }

  /* Get the media_class */
  media_class = spa_dict_lookup(props, "media.class");

  /* Only handle client Stream nodes */
  if (!g_str_has_prefix (media_class, "Stream/"))
    return;

  /* Get the name */
  name = spa_dict_lookup (props, "media.name");
  if (!name)
    name = spa_dict_lookup (props, "node.name");

  g_debug ("found stream node: id:%u ; name:%s ; media_class:%s", id, name,
      media_class);

  /* Get the proxy */
  proxy = pw_registry_proxy_bind (data->registry_proxy, id,
      PW_TYPE_INTERFACE_Node, PW_VERSION_NODE, 0);

  /* TODO: Assume all clients have this format for now */
  format.format = SPA_AUDIO_FORMAT_F32P;
  format.flags = 1;
  format.rate = 48000;
  format.channels = 1;
  format.position[0] = 0;

  /* Set the profile */
  spa_pod_builder_init(&pod_builder, buf, sizeof(buf));
  param = spa_format_audio_raw_build(&pod_builder, SPA_PARAM_Format, &format);
  param = spa_pod_builder_add_object(&pod_builder,
      SPA_TYPE_OBJECT_ParamProfile, SPA_PARAM_Profile,
      SPA_PARAM_PROFILE_direction,  SPA_POD_Id(PW_DIRECTION_OUTPUT),
      SPA_PARAM_PROFILE_format,     SPA_POD_Pod(param));
  pw_node_proxy_set_param((struct pw_node_proxy*)proxy,
      SPA_PARAM_Profile, 0, param);

  /* Create the endpoint info */
  ei = g_slice_new0 (struct endpoint_info);
  ei->name = g_strdup(name);
  ei->media_class = g_strdup(media_class);
  ei->proxy = proxy;

  /* Insert the client node info in the hash table */
  g_hash_table_insert(data->client_nodes_info, GINT_TO_POINTER (id), ei);
}

static void
handle_port(struct module_data *data, uint32_t id, uint32_t parent_id,
            const struct spa_dict *props)
{
  struct proxy_info *pi = NULL;
  struct pw_proxy *proxy = NULL;

  /* Only handle ports whose parent is an alsa node */
  if (!g_hash_table_contains(data->client_nodes_info,
      GINT_TO_POINTER (parent_id)))
    return;

  /* Get the port proxy */
  proxy = pw_registry_proxy_bind (data->registry_proxy, id,
      PW_TYPE_INTERFACE_Port, PW_VERSION_PORT, 0);
  if (!proxy)
    return;

  /* Create the port info */
  pi = g_slice_new0 (struct proxy_info);
  pi->data = data;
  pi->node_id = parent_id;
  pi->proxy_port = NULL;

  /* Create the proxy port asynchronically */
  wp_proxy_port_new(proxy, proxy_port_created, pi);
}

static void
registry_global(void *d, uint32_t id, uint32_t parent_id,
		uint32_t permissions, uint32_t type, uint32_t version,
		const struct spa_dict *props)
{
  struct module_data *data = d;

  switch (type) {
  case PW_TYPE_INTERFACE_Node:
    handle_node(data, id, parent_id, props);
    break;

  case PW_TYPE_INTERFACE_Port:
    handle_port(data, id, parent_id, props);
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
on_remote_connected (WpRemote *remote, WpRemoteState state,
    struct module_data *data)
{
  struct pw_core_proxy *core_proxy;
  struct pw_remote *pw_remote;

  g_object_get (remote, "pw-remote", &pw_remote, NULL);

  core_proxy = pw_remote_get_core_proxy (pw_remote);
  data->registry_proxy = pw_core_proxy_get_registry (core_proxy,
      PW_TYPE_INTERFACE_Registry, PW_VERSION_REGISTRY, 0);
  pw_registry_proxy_add_listener(data->registry_proxy,
      &data->registry_listener, &registry_events, data);
}

static void
module_destroy (gpointer d)
{
  struct module_data *data = d;

  /* Destroy the hash table */
  g_hash_table_unref (data->client_nodes_info);

  /* Clean up */
  g_slice_free (struct module_data, data);
}

void
wireplumber__module_init (WpModule * module, WpCore * core, GVariant * args)
{
  struct module_data *data;
  WpRemote *remote;
  struct pw_core *pw_core;
  struct pw_remote *pw_remote;

  remote = wp_core_get_global (core, WP_GLOBAL_REMOTE_PIPEWIRE);
  if (!remote) {
    g_critical ("module-pipewire cannot be loaded without a registered "
        "WpRemotePipewire object");
    return;
  }

  /* Create the module data */
  data = g_slice_new0 (struct module_data);
  data->module = module;
  data->client_nodes_info = g_hash_table_new_full (g_direct_hash,
      g_direct_equal, NULL, endpoint_info_destroy);

  wp_module_set_destroy_callback (module, module_destroy, data);

  g_signal_connect (remote, "state-changed::connected",
      (GCallback) on_remote_connected, data);

  g_object_get (remote,
      "pw-core", &pw_core,
      "pw-remote", &pw_remote,
      NULL);
  remote_endpoint_init (core, pw_core, pw_remote);

  wp_factory_new (core, "pipewire-simple-endpoint", simple_endpoint_factory);
  wp_factory_new (core, "pipewire-simple-endpoint-link",
      simple_endpoint_link_factory);
}
