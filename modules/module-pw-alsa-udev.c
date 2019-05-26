/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/**
 * module-pw-alsa-udev provides alsa device detection through pipewire
 * and automatically creates endpoints for all alsa device nodes that appear
 */

#include <wp/wp.h>
#include <pipewire/pipewire.h>

struct impl {
  WpCore *wp_core;

  struct pw_remote *remote;
  struct spa_hook remote_listener;

  struct pw_registry_proxy *registry_proxy;
  struct spa_hook registry_listener;
};

static void
handle_node(struct impl *impl, uint32_t id, uint32_t parent_id,
            const struct spa_dict *props)
{
  const gchar *media_class = NULL, *node_name = NULL;
  struct spa_proxy *proxy = NULL;
  GVariantBuilder b;
  g_autoptr(GVariant) endpoint_props = NULL;
  g_autoptr (WpEndpoint) endpoint = NULL;

  /* Make sure the node has properties */
  if (!props) {
    g_warning("node has no properties, skipping...");
    return;
  }

  /* Make sure the media class is audio */
  /* FIXME: need to handle only alsa nodes */
  media_class = spa_dict_lookup(props, "media.class");
  if (!g_str_has_prefix (media_class, "Audio/"))
    return;

  /* Get the device name */
  node_name = spa_dict_lookup(props, "node.name");

  /* Get the proxy */
  proxy = pw_registry_proxy_bind (impl->registry_proxy, id,
      PW_TYPE_INTERFACE_Node, PW_VERSION_NODE, 0);

  /* Build the GVariant properties for the endpoint */
  g_variant_builder_init (&b, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&b, "{sv}", "name",
      g_variant_new_take_string (g_strdup_printf ("Endpoint %u: %s", id, node_name)));
  g_variant_builder_add (&b, "{sv}",
      "media-class", g_variant_new_string (media_class));
  g_variant_builder_add (&b, "{sv}",
      "node-proxy", g_variant_new_uint64 ((guint64) proxy));
  endpoint_props = g_variant_builder_end (&b);

  /* Create the endpoint */
  endpoint = wp_factory_make (impl->wp_core, "pw-audio-softdsp-endpoint",
      WP_TYPE_ENDPOINT, endpoint_props);
  wp_endpoint_register (endpoint, impl->wp_core);
}

static void
registry_global(void *data,uint32_t id, uint32_t parent_id,
		uint32_t permissions, uint32_t type, uint32_t version,
		const struct spa_dict *props)
{
  struct impl *impl = data;

  /* Only handle nodes */
  switch (type) {
  case PW_TYPE_INTERFACE_Node:
    handle_node(impl, id, parent_id, props);
    break;

  default:
    break;
  }
}

static const struct pw_registry_proxy_events registry_events = {
  PW_VERSION_REGISTRY_PROXY_EVENTS,
  .global = registry_global,
};

static void on_state_changed(void *_data, enum pw_remote_state old,
    enum pw_remote_state state, const char *error)
{
  struct impl *impl = _data;
  struct pw_core_proxy *core_proxy;

  switch (state) {
  case PW_REMOTE_STATE_CONNECTED:
    core_proxy = pw_remote_get_core_proxy (impl->remote);
    impl->registry_proxy = pw_core_proxy_get_registry (core_proxy,
        PW_TYPE_INTERFACE_Registry, PW_VERSION_REGISTRY, 0);
    pw_registry_proxy_add_listener(impl->registry_proxy,
        &impl->registry_listener, &registry_events, impl);
    break;

  default:
    break;
  }
}

static const struct pw_remote_events remote_events = {
  PW_VERSION_REMOTE_EVENTS,
  .state_changed = on_state_changed,
};

static void
module_destroy (gpointer data)
{
  struct impl *impl = data;
  g_slice_free (struct impl, impl);
}

void
wireplumber__module_init (WpModule * module, WpCore * core, GVariant * args)
{
  /* This needs to create the alsa sink and alsa source nodes, but since
   * this is already implemented in the alsa-module of pipewire, for now
   * we just listen for the alsa nodes created by pipewire. We eventually
   * need to move all the node creation logic here */

  /* Create the impl */
  struct impl *impl = g_new0(struct impl, 1);
  impl->wp_core = core;
  impl->remote = wp_core_get_global(core, WP_GLOBAL_PW_REMOTE);

  /* Set destroy callback for impl */
  wp_module_set_destroy_callback (module, module_destroy, impl);

  /* Add a state changed listener */
  pw_remote_add_listener(impl->remote, &impl->remote_listener, &remote_events, impl);
}
