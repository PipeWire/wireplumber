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

  struct pw_registry_proxy *registry_proxy;
  struct spa_hook registry_listener;

  struct pw_core_proxy *core_proxy;
  struct spa_hook core_listener;
  GQueue *done_queue;
};


typedef void (*WpDoneCallback)(gpointer, gpointer);

struct done_data
{
  WpDoneCallback callback;
  gpointer data;
  GDestroyNotify data_destroy;
};

static void
done_data_destroy(gpointer p)
{
  struct done_data *dd = p;
  if (dd->data_destroy) {
    dd->data_destroy(dd->data);
    dd->data = NULL;
  }
  g_slice_free (struct done_data, dd);
}

static void
sync_core_with_callback(struct module_data* impl, WpDoneCallback callback,
    gpointer data, GDestroyNotify data_destroy)
{
  struct done_data *dd = g_new0(struct done_data, 1);

  /* Set the data */
  dd->callback = callback;
  dd->data = data;
  dd->data_destroy = data_destroy;

  /* Add the data to the queue */
  g_queue_push_tail (impl->done_queue, dd);

  /* Sync the core */
  pw_core_proxy_sync(impl->core_proxy, 0, 0);
}

static void
core_done(void *d, uint32_t id, int seq)
{
  struct module_data * impl = d;
  struct done_data * dd = NULL;

  /* Process all the done_data queue */
  while ((dd = g_queue_pop_head(impl->done_queue))) {
    if (dd->callback)
      dd->callback(impl, dd->data);
    done_data_destroy(dd);
  }
}

static const struct pw_core_proxy_events core_events = {
  PW_VERSION_CORE_EVENTS,
  .done = core_done
};

static void
register_endpoint (struct module_data* data, WpEndpoint *ep)
{
  g_autoptr (WpCore) core = NULL;
  core = wp_module_get_core (data->module);
  g_return_if_fail (core != NULL);
  wp_endpoint_register (ep, core);
}

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
  g_autoptr (WpEndpoint) endpoint = NULL;
  struct spa_audio_info_raw format = { 0, };
  struct spa_pod *param;
  struct spa_pod_builder pod_builder = { 0, };
  char buf[1024];

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

    /* TODO: we need to get this from the EnumFormat event */
    format.format = SPA_AUDIO_FORMAT_F32P;
    format.flags = 1;
    format.rate = 48000;
    format.channels = 2;
    format.position[0] = 0;
    format.position[1] = 0;

    /* Set the profile */
    spa_pod_builder_init(&pod_builder, buf, sizeof(buf));
    param = spa_format_audio_raw_build(&pod_builder, SPA_PARAM_Format, &format);
    param = spa_pod_builder_add_object(&pod_builder,
        SPA_TYPE_OBJECT_ParamProfile, SPA_PARAM_Profile,
        SPA_PARAM_PROFILE_direction,  SPA_POD_Id(PW_DIRECTION_OUTPUT),
        SPA_PARAM_PROFILE_format,     SPA_POD_Pod(param));
    pw_node_proxy_set_param((struct pw_node_proxy*)proxy,
        SPA_PARAM_Profile, 0, param);

    g_variant_builder_init (&b, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add (&b, "{sv}", "node-id", g_variant_new_uint32 (id));
    g_variant_builder_add (&b, "{sv}",
        "name", name ? g_variant_new_string (name) :
            g_variant_new_take_string (g_strdup_printf ("Stream %u", id)));
    g_variant_builder_add (&b, "{sv}",
        "media-class", g_variant_new_string (media_class));
    g_variant_builder_add (&b, "{sv}",
        "node-proxy", g_variant_new_uint64 ((guint64) proxy));
    endpoint_props = g_variant_builder_end (&b);

    core = wp_module_get_core (data->module);
    g_return_if_fail (core != NULL);

    endpoint = wp_factory_make (core, "pipewire-simple-endpoint",
        WP_TYPE_ENDPOINT, endpoint_props);
    sync_core_with_callback (data, (WpDoneCallback) register_endpoint,
        g_steal_pointer (&endpoint), g_object_unref);
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

  core_proxy = data->core_proxy = pw_remote_get_core_proxy (pw_remote);
  pw_core_proxy_add_listener(data->core_proxy, &data->core_listener,
      &core_events, data);
  data->registry_proxy = pw_core_proxy_get_registry (core_proxy,
      PW_TYPE_INTERFACE_Registry, PW_VERSION_REGISTRY, 0);
  pw_registry_proxy_add_listener(data->registry_proxy,
      &data->registry_listener, &registry_events, data);
}

static void
module_destroy (gpointer d)
{
  struct module_data *data = d;

  g_queue_free_full(data->done_queue, done_data_destroy);
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

  data = g_slice_new0 (struct module_data);
  data->module = module;
  data->done_queue = g_queue_new();
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
