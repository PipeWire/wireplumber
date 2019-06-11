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

#include "module-pipewire/port.h"

typedef void (*WpDoneCallback)(gpointer);

struct done_data {
  WpDoneCallback callback;
  gpointer data;
  GDestroyNotify data_destroy;
};

struct impl {
  WpCore *wp_core;

  /* Remote */
  struct pw_remote *remote;
  struct spa_hook remote_listener;

  /* Core */
  struct pw_core_proxy *core_proxy;
  struct spa_hook core_listener;
  int core_seq;
  GQueue *done_queue;

  /* Registry */
  struct pw_registry_proxy *registry_proxy;
  struct spa_hook registry_listener;

  /* Ports */
  struct spa_list port_list;
};

struct endpoint_info {
  struct impl *impl;
  uint32_t id;
  uint32_t parent_id;
  gchar *name;
  gchar *media_class;
};

static void endpoint_info_destroy(gpointer p) {
  struct endpoint_info *ei = p;
  if (ei->name) {
    g_free (ei->name);
    ei->name = NULL;
  }
  if (ei->media_class) {
    g_free (ei->media_class);
    ei->media_class = NULL;
  }
  g_slice_free (struct endpoint_info, p);
}

static void done_data_destroy(gpointer p) {
  struct done_data *dd = p;
  if (dd->data_destroy) {
    dd->data_destroy(dd->data);
    dd->data = NULL;
  }
  g_slice_free (struct done_data, dd);
}

static void sync_core_with_callabck(struct impl* impl,
    WpDoneCallback callback, gpointer data, GDestroyNotify data_destroy) {
  struct done_data *dd = g_new0(struct done_data, 1);

  /* Set the data */
  dd->callback = callback;
  dd->data = data;
  dd->data_destroy = data_destroy;

  /* Add the data to the queue */
  g_queue_push_tail (impl->done_queue, dd);

  /* Sync the core */
  impl->core_seq = pw_core_proxy_sync(impl->core_proxy, 0, impl->core_seq);
}

static void create_endpoint(gpointer p) {
  struct endpoint_info *ei = p;
  struct spa_proxy *proxy = NULL;
  GVariantBuilder b;
  g_autoptr(GVariant) endpoint_props = NULL;
  WpEndpoint *endpoint = NULL;

  /* Make sure the endpoint info is valid */
  if (!ei)
    return;

  /* Register the proxy */
  proxy = pw_registry_proxy_bind (ei->impl->registry_proxy,
      ei->id, PW_TYPE_INTERFACE_Node, PW_VERSION_NODE, 0);

  /* Build the GVariant properties for the endpoint */
  g_variant_builder_init (&b, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&b, "{sv}", "name",
      g_variant_new_take_string (g_strdup_printf ("Endpoint %u: %s", ei->id,
      ei->name)));
  g_variant_builder_add (&b, "{sv}",
      "media-class", g_variant_new_string (ei->media_class));
  g_variant_builder_add (&b, "{sv}",
      "node-proxy", g_variant_new_uint64 ((guint64) proxy));
  g_variant_builder_add (&b, "{sv}",
      "port-list", g_variant_new_uint64 ((guint64) &ei->impl->port_list));
  endpoint_props = g_variant_builder_end (&b);

  /* Create and register the endpoint */
  endpoint = wp_factory_make (ei->impl->wp_core, "pw-audio-softdsp-endpoint",
      WP_TYPE_ENDPOINT, endpoint_props);
  wp_endpoint_register (endpoint, ei->impl->wp_core);
}

static void enum_format_and_create_endpoint(gpointer p) {
  struct endpoint_info *ei = p, *ei_copy = NULL;
  WpPort *port = NULL;

  /* Make sure the endpoint info is valid */
  if (!ei)
    return;

  /* Find the unique alsa port */
  spa_list_for_each(port, &ei->impl->port_list, l) {
    if (port->parent_id == ei->id)
      break;
  }

  /* Emit the port EnumFormat */
  pw_port_proxy_enum_params((struct pw_port_proxy*)port->proxy, 0,
          SPA_PARAM_EnumFormat, 0, -1, NULL);

  /* Copy endpoint info */
  ei_copy = g_new0(struct endpoint_info, 1);
  ei_copy->impl = ei->impl;
  ei_copy->id = ei->id;
  ei_copy->name = g_strdup(ei->name);
  ei_copy->media_class = g_strdup(ei->media_class);

  /* Forward the endpoint creation until the port EnumFormat is emitted */
  sync_core_with_callabck(ei->impl, create_endpoint, ei_copy,
      endpoint_info_destroy);
}

static void
handle_node(struct impl *impl, uint32_t id, uint32_t parent_id,
            const struct spa_dict *props)
{
  struct endpoint_info *ei = NULL;
  const gchar *media_class = NULL, *name = NULL;

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
  ei = g_new0(struct endpoint_info, 1);
  ei->impl = impl;
  ei->id = id;
  ei->name = g_strdup(name);
  ei->media_class = g_strdup(media_class);

  /* Delay the creation of the endpoint until all ports have been created */
  sync_core_with_callabck(impl, enum_format_and_create_endpoint, ei,
      endpoint_info_destroy);
}

static void port_event_info(void *data, const struct pw_port_info *info)
{
  WpPort *port = data;
  port->info  = pw_port_info_update(port->info, info);
}

static void port_event_param(void *data, int seq, uint32_t id, uint32_t index,
  uint32_t next, const struct spa_pod *param)
{
  WpPort *port = data;

  /* Only handle EnumFormat */
  if (id != SPA_PARAM_EnumFormat)
    return;

  /* Parse the format */
  if (spa_format_parse(param, &port->media_type, &port->media_subtype) < 0)
    return;

  /* Only handle RAW audio types */
  if (port->media_type != SPA_MEDIA_TYPE_audio ||
      port->media_subtype != SPA_MEDIA_SUBTYPE_raw)
    return;

  /* Parse the raw audio format */
  spa_pod_fixate((struct spa_pod*)param);
  spa_format_audio_raw_parse(param, &port->format);
}

static const struct pw_port_proxy_events port_events = {
  PW_VERSION_PORT_PROXY_EVENTS,
  .info = port_event_info,
  .param = port_event_param,
};

static void
handle_port(struct impl *impl, uint32_t id, uint32_t parent_id,
            const struct spa_dict *props)
{
  struct pw_proxy *proxy;
  WpPort *port;
  const char *direction_prop;

  /* Make sure the port has porperties */
  if (!props)
    return;

  /* Get the direction property */
  direction_prop = spa_dict_lookup(props, "port.direction");
  if (!direction_prop)
    return;

  /* Get the proxy */
  proxy = pw_registry_proxy_bind (impl->registry_proxy, id,
      PW_TYPE_INTERFACE_Port, PW_VERSION_NODE, sizeof(WpPort));
  if (!proxy)
    return;

  /* Get the port */
  port = pw_proxy_get_user_data(proxy);

  /* Set the info */
  port->id = id;
  port->parent_id = parent_id;
  port->direction =
      !strcmp(direction_prop, "out") ? PW_DIRECTION_OUTPUT : PW_DIRECTION_INPUT;

  /* Set the proxy and listener */
  port->proxy = proxy;
  pw_proxy_add_proxy_listener(proxy, &port->listener, &port_events, port);

  /* Add the port to the list */
  spa_list_append(&impl->port_list, &port->l);
}

static void
registry_global(void *data,uint32_t id, uint32_t parent_id,
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

static void core_done(void *d, uint32_t id, int seq)
{
  struct impl * impl = d;
  struct done_data * dd = NULL;

  /* Process all the done_data queue */
  while ((dd = g_queue_pop_head(impl->done_queue))) {
    if (dd->callback)
      dd->callback(dd->data);
    done_data_destroy(dd);
  }
}

static const struct pw_core_proxy_events core_events = {
  PW_VERSION_CORE_EVENTS,
  .done = core_done
};

static void on_state_changed(void *_data, enum pw_remote_state old,
    enum pw_remote_state state, const char *error)
{
  struct impl *impl = _data;

  switch (state) {
  case PW_REMOTE_STATE_CONNECTED:
    impl->core_proxy = pw_remote_get_core_proxy (impl->remote);
    pw_core_proxy_add_listener(impl->core_proxy, &impl->core_listener,
        &core_events, impl);
    impl->registry_proxy = pw_core_proxy_get_registry (impl->core_proxy,
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
  g_queue_free_full(impl->done_queue, done_data_destroy);
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
  impl->done_queue = g_queue_new();
  spa_list_init(&impl->port_list);

  /* Set destroy callback for impl */
  wp_module_set_destroy_callback (module, module_destroy, impl);

  /* Add a state changed listener */
  pw_remote_add_listener(impl->remote, &impl->remote_listener, &remote_events,
      impl);
}
