/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/**
 * module-pipewire provides basic integration between wireplumber and pipewire.
 * It provides the pipewire core and remote, connects to pipewire and provides
 * the most primitive implementations of WpEndpoint and WpEndpointLink
 */

#include <wp/wp.h>
#include <pipewire/pipewire.h>

#include "module-pipewire/loop-source.h"

gpointer simple_endpoint_factory (WpFactory * factory, GType type,
    GVariant * properties);
gpointer simple_endpoint_link_factory (WpFactory * factory, GType type,
    GVariant * properties);

struct module_data
{
  WpModule *module;

  struct pw_core *core;

  struct pw_remote *remote;
  struct spa_hook remote_listener;

  struct pw_registry_proxy *registry_proxy;
  struct spa_hook registry_listener;
};

static void
registry_global (void * d, uint32_t id, uint32_t parent_id,
    uint32_t permissions, uint32_t type, uint32_t version,
    const struct spa_dict * props)
{
  struct module_data *data = d;
  const gchar *name;
  const gchar *media_class;
  struct pw_proxy *proxy;
  GVariantBuilder b;
  g_autoptr (GVariant) endpoint_props = NULL;
  g_autoptr (WpCore) core = NULL;

  /* listen for client "Stream" nodes and create endpoints for them */
  if (type == PW_TYPE_INTERFACE_Node &&
      props && (media_class = spa_dict_lookup(props, "media.class")) &&
      g_str_has_prefix (media_class, "Stream/"))
  {
    name = spa_dict_lookup (props, "media.name");
    if (!name)
      name = spa_dict_lookup (props, "node.name");

    g_debug ("found stream node: id:%u ; name:%s ; media_class:%s", id, name,
        media_class);

    proxy = pw_registry_proxy_bind (data->registry_proxy,
        id, type, PW_VERSION_NODE, 0);

    g_variant_builder_init (&b, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add (&b, "{sv}",
        "name", name ? g_variant_new_string (name) :
            g_variant_new_take_string (g_strdup_printf ("Stream %u", id)));
    g_variant_builder_add (&b, "{sv}",
        "media-class", g_variant_new_string (media_class));
    g_variant_builder_add (&b, "{sv}",
        "node-proxy", g_variant_new_uint64 ((guint64) proxy));
    endpoint_props = g_variant_builder_end (&b);

    core = wp_module_get_core (data->module);
    wp_factory_make (core, "pipewire-simple-endpoint", WP_TYPE_ENDPOINT,
        endpoint_props);
  }
}

static const struct pw_registry_proxy_events registry_events = {
  PW_VERSION_REGISTRY_PROXY_EVENTS,
  .global = registry_global,
};

static void
on_remote_state_changed (void *d, enum pw_remote_state old_state,
    enum pw_remote_state new_state, const char *error)
{
  struct module_data *data = d;
  struct pw_core_proxy *core_proxy;

  g_debug ("remote state changed, old:%s new:%s",
      pw_remote_state_as_string (old_state),
      pw_remote_state_as_string (new_state));

  switch (new_state) {
  case PW_REMOTE_STATE_CONNECTED:
    core_proxy = pw_remote_get_core_proxy (data->remote);
    data->registry_proxy = pw_core_proxy_get_registry (core_proxy,
        PW_TYPE_INTERFACE_Registry, PW_VERSION_REGISTRY, 0);
    pw_registry_proxy_add_listener(data->registry_proxy,
        &data->registry_listener, &registry_events, data);
    break;

  case PW_REMOTE_STATE_UNCONNECTED:
    // TODO quit wireplumber
    break;

  case PW_REMOTE_STATE_ERROR:
    // TODO quit wireplumber
    break;

  default:
    break;
  }
}

static const struct pw_remote_events remote_events = {
  PW_VERSION_REMOTE_EVENTS,
  .state_changed = on_remote_state_changed,
};

static gboolean
connect_in_idle (struct pw_remote *remote)
{
  pw_remote_connect (remote);
  return G_SOURCE_REMOVE;
}

static void
module_destroy (gpointer d)
{
  struct module_data *data = d;

  g_debug ("module-pipewire destroy");

  pw_remote_destroy (data->remote);
  pw_core_destroy (data->core);
  g_slice_free (struct module_data, data);
}

void
wireplumber__module_init (WpModule * module, WpCore * core, GVariant * args)
{
  GSource *source;
  struct module_data *data;

  g_debug ("module-pipewire init");

  pw_init (NULL, NULL);

  data = g_slice_new0 (struct module_data);
  data->module = module;
  wp_module_set_destroy_callback (module, module_destroy, data);

  source = wp_loop_source_new ();
  g_source_attach (source, NULL);

  data->core = pw_core_new (WP_LOOP_SOURCE (source)->loop, NULL, 0);
  wp_core_register_global (core, WP_GLOBAL_PW_CORE, data->core, NULL);

  data->remote = pw_remote_new (data->core, NULL, 0);
  pw_remote_add_listener (data->remote, &data->remote_listener, &remote_events,
      data);
  wp_core_register_global (core, WP_GLOBAL_PW_REMOTE, data->remote, NULL);

  wp_factory_new (core, "pipewire-simple-endpoint", simple_endpoint_factory);
  wp_factory_new (core, "pipewire-simple-endpoint-link",
      simple_endpoint_link_factory);

  g_idle_add ((GSourceFunc) connect_in_idle, data->remote);
}
