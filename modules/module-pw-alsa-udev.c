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

#include <spa/utils/names.h>
#include <spa/monitor/monitor.h>
#include <pipewire/pipewire.h>
#include <wp/wp.h>

struct monitor {
  struct spa_handle *handle;
  struct spa_monitor *monitor;
  struct spa_list device_list;
};

struct impl
{
  WpModule *module;
  WpRemotePipewire *remote_pipewire;
  GHashTable *registered_endpoints;
  GVariant *streams;

  /* The alsa monitor */
  struct monitor monitor;
};

struct device {
  struct impl *impl;
  struct spa_list link;
  uint32_t id;

  struct pw_properties *props;

  struct spa_handle *handle;
  struct pw_proxy *proxy;
  struct spa_device *device;
  struct spa_hook device_listener;

  struct spa_list node_list;
};

struct node {
  struct impl *impl;
  struct device *device;
  struct spa_list link;
  uint32_t id;

  struct pw_properties *props;

  struct pw_proxy *proxy;
  struct spa_node *node;
};

static void
on_endpoint_created(GObject *initable, GAsyncResult *res, gpointer d)
{
  struct impl *impl = d;
  WpEndpoint *endpoint = NULL;
  guint global_id = 0;
  GError *error = NULL;

  /* Get the endpoint */
  endpoint = wp_endpoint_new_finish(initable, res, NULL);
  g_return_if_fail (endpoint);

  /* Check for error */
  if (error) {
    g_clear_object (&endpoint);
    g_warning ("Failed to create alsa endpoint: %s", error->message);
    return;
  }

  /* Get the endpoint global id */
  g_object_get (endpoint, "global-id", &global_id, NULL);
  g_debug ("Created alsa endpoint for global id %d", global_id);

  /* Register the endpoint and add it to the table */
  wp_endpoint_register (endpoint);
  g_hash_table_insert (impl->registered_endpoints, GUINT_TO_POINTER(global_id),
      endpoint);
}

static void
on_node_added(WpRemotePipewire *rp, guint id, guint parent_id, gconstpointer p,
    gpointer d)
{
  struct impl *impl = d;
  const struct spa_dict *props = p;
  g_autoptr (WpCore) core = wp_module_get_core (impl->module);
  const gchar *media_class, *name;
  GVariantBuilder b;
  g_autoptr (GVariant) endpoint_props = NULL;

  /* Make sure the node has properties */
  g_return_if_fail(props);

  /* Get the name and media_class */
  media_class = spa_dict_lookup(props, "media.class");

  /* Make sure the media class is non-dsp audio */
  if (!g_str_has_prefix (media_class, "Audio/"))
    return;
  if (g_str_has_prefix (media_class, "Audio/DSP"))
    return;

  /* Get the name */
  name = spa_dict_lookup (props, "media.name");
  if (!name)
    name = spa_dict_lookup (props, "node.name");

  /* Set the properties */
  g_variant_builder_init (&b, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&b, "{sv}",
      "name", g_variant_new_string (name));
  g_variant_builder_add (&b, "{sv}",
      "media-class", g_variant_new_string (media_class));
  g_variant_builder_add (&b, "{sv}",
      "global-id", g_variant_new_uint32 (id));
  g_variant_builder_add (&b, "{sv}",
      "streams", impl->streams);
  endpoint_props = g_variant_builder_end (&b);

  /* Create the endpoint async */
  wp_factory_make (core, "pw-audio-softdsp-endpoint", WP_TYPE_ENDPOINT,
      endpoint_props, on_endpoint_created, impl);
}

static void
on_global_removed (WpRemotePipewire *rp, guint id, gpointer d)
{
  struct impl *impl = d;
  WpEndpoint *endpoint = NULL;

  /* Get the endpoint */
  endpoint = g_hash_table_lookup (impl->registered_endpoints,
      GUINT_TO_POINTER(id));
  if (!endpoint)
    return;

  /* Unregister the endpoint and remove it from the table */
  wp_endpoint_unregister (endpoint);
  g_hash_table_remove (impl->registered_endpoints, GUINT_TO_POINTER(id));
}

static struct node *
create_node(struct impl *impl, struct device *dev, uint32_t id,
    const struct spa_device_object_info *info)
{
  struct node *node;
  const char *str;

  /* Check if the type is a node */
  if (info->type != SPA_TYPE_INTERFACE_Node)
    return NULL;

  /* Create the node */
  node = g_slice_new0(struct node);

  /* Set the node properties */
  node->props = pw_properties_copy(dev->props);
  pw_properties_update(node->props, info->props);
  str = pw_properties_get(dev->props, SPA_KEY_DEVICE_NICK);
  if (str == NULL)
    str = pw_properties_get(dev->props, SPA_KEY_DEVICE_NAME);
  if (str == NULL)
    str = pw_properties_get(dev->props, SPA_KEY_DEVICE_ALIAS);
  if (str == NULL)
    str = "alsa-device";
  pw_properties_set(node->props, PW_KEY_NODE_NAME, str);
  pw_properties_set(node->props, "factory.name", info->factory_name);
  pw_properties_set(node->props, "merger.monitor", "1");

  /* Set the node info */
  node->impl = impl;
  node->device = dev;
  node->id = id;
  node->proxy = wp_remote_pipewire_create_object(impl->remote_pipewire,
      "adapter", PW_TYPE_INTERFACE_Node, &node->props->dict);
  if (!node->proxy) {
    g_slice_free (struct node, node);
    return NULL;
  }

  /* Add the node to the list */
  spa_list_append(&dev->node_list, &node->link);

  return node;
}

static void
update_node(struct impl *impl, struct device *dev, struct node *node,
    const struct spa_device_object_info *info)
{
  /* Just update the properties */
  pw_properties_update(node->props, info->props);
}

static void destroy_node(struct impl *impl, struct device *dev, struct node *node)
{
  /* Remove the node from the list */
  spa_list_remove(&node->link);

  /* Destroy the proxy node */
  pw_proxy_destroy(node->proxy);

  /* Destroy the node */
  g_slice_free (struct node, node);
}

static struct node *
find_node(struct device *dev, uint32_t id)
{
  struct node *node;

  /* Find the node in the list */
  spa_list_for_each(node, &dev->node_list, link) {
    if (node->id == id)
      return node;
  }

  return NULL;
}

static void
device_object_info(void *data, uint32_t id,
  const struct spa_device_object_info *info)
{
  struct device *dev = data;
  struct impl *impl = dev->impl;
  struct node *node = NULL;

  /* Find the node */
  node = find_node(dev, id);

  if (info) {
    /* Just update the node if it already exits, otherwise create it */
    if (node)
      update_node(impl, dev, node, info);
    else
      create_node(impl, dev, id, info);
  } else {
    /* Just remove the node if it already exists */
    if (node)
      destroy_node(impl, dev, node);
  }
}

static void
device_info(void *data, const struct spa_device_info *info)
{
  struct device *dev = data;
  pw_properties_update(dev->props, info->props);
}

static const struct spa_device_events device_events = {
  SPA_VERSION_DEVICE_EVENTS,
  .info = device_info,
  .object_info = device_object_info
};

static struct device*
create_device(struct impl *impl, uint32_t id,
  const struct spa_monitor_object_info *info) {

  struct device *dev;
  struct spa_handle *handle;
  int res;
  void *iface;

  /* Check if the type is a device */
  if (info->type != SPA_TYPE_INTERFACE_Device)
    return NULL;

  /* Load the device handle */
  handle = (struct spa_handle *)wp_remote_pipewire_load_spa_handle (
      impl->remote_pipewire, info->factory_name, info->props);
  if (!handle)
    return NULL;

  /* Get the handle interface */
  res = spa_handle_get_interface(handle, info->type, &iface);
  if (res < 0) {
    pw_unload_spa_handle(handle);
    return NULL;
  }

  /* Create the device */
  dev = g_slice_new0(struct device);
  dev->impl = impl;
  dev->id = id;
  dev->handle = handle;
  dev->device = iface;
  dev->props = pw_properties_new_dict(info->props);
  dev->proxy = wp_remote_pipewire_export (impl->remote_pipewire, info->type, dev->props, dev->device, 0);
  if (!dev->proxy) {
    pw_unload_spa_handle(handle);
    return NULL;
  }
  spa_list_init(&dev->node_list);

  /* Add device listener for events */
  spa_device_add_listener(dev->device, &dev->device_listener, &device_events,
      dev);

  /* Add the device to the list */
  spa_list_append(&impl->monitor.device_list, &dev->link);

  return dev;
}

static void
update_device(struct impl *impl, struct device *dev,
    const struct spa_monitor_object_info *info)
{
  /* Make sure the device and its info are valid */
  g_return_if_fail (dev);
  g_return_if_fail (info);

  /* Update the properties of the device */
  pw_properties_update(dev->props, info->props);
}

static void
destroy_device(struct impl *impl, struct device *dev)
{
  /* Remove the device from the list */
  spa_list_remove(&dev->link);

  /* Remove the device listener */
  spa_hook_remove(&dev->device_listener);

  /* Destroy the device proxy */
  pw_proxy_destroy(dev->proxy);

  /* Unload the device handle */
  pw_unload_spa_handle(dev->handle);

  /* Destroy the object */
  g_slice_free (struct device, dev);
}

static struct device *
find_device(struct impl *impl, uint32_t id)
{
  struct device *dev;

  /* Find the device in the list */
  spa_list_for_each(dev, &impl->monitor.device_list, link) {
    if (dev->id == id)
      return dev;
  }

  return NULL;
}

static int
monitor_object_info(gpointer data, uint32_t id,
    const struct spa_monitor_object_info *info)
{
  struct impl *impl = data;
  struct device *dev = NULL;

  /* Find the device */
  dev = find_device(impl, id);

  if (info) {
    /* Just update the device if it already exits, otherwise create it */
    if (dev)
      update_device(impl, dev, info);
    else
      if (!create_device(impl, id, info))
        return -ENOMEM;
  } else {
    /* Just remove the device if it already exists, otherwise return error */
    if (dev)
      destroy_device(impl, dev);
    else
      return -ENODEV;
  }

  return 0;
}

static const struct spa_monitor_callbacks monitor_callbacks =
{
  SPA_VERSION_MONITOR_CALLBACKS,
  .object_info = monitor_object_info,
};

static void
start_monitor (WpRemotePipewire *remote, WpRemoteState state, gpointer data)
{
  struct impl *impl = data;
  struct spa_handle *handle;
  int res;
  void *iface;

  /* Load the monitor handle */
  handle = (struct spa_handle *)wp_remote_pipewire_load_spa_handle (
      impl->remote_pipewire, SPA_NAME_API_ALSA_MONITOR, NULL);
  g_return_if_fail (handle);

  /* Get the handle interface */
  res = spa_handle_get_interface(handle, SPA_TYPE_INTERFACE_Monitor, &iface);
  if (res < 0) {
    g_critical ("module-pw-alsa-udev cannot get monitor interface");
    pw_unload_spa_handle(handle);
    return;
  }

  /* Init the monitor data */
  impl->monitor.handle = handle;
  impl->monitor.monitor = iface;
  spa_list_init(&impl->monitor.device_list);

  /* Set the monitor callbacks */
  spa_monitor_set_callbacks(impl->monitor.monitor, &monitor_callbacks, impl);
}

static void
module_destroy (gpointer data)
{
  struct impl *impl = data;

  /* Set to NULL module and remote pipewire as we don't own the reference */
  impl->module = NULL;
  impl->remote_pipewire = NULL;

  /* Destroy the registered endpoints table */
  g_hash_table_unref(impl->registered_endpoints);
  impl->registered_endpoints = NULL;

  g_clear_pointer (&impl->streams, g_variant_unref);

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
  impl->registered_endpoints = g_hash_table_new_full (g_direct_hash,
      g_direct_equal, NULL, (GDestroyNotify)g_object_unref);
  impl->streams = g_variant_lookup_value (args, "streams",
      G_VARIANT_TYPE ("as"));

  /* Set destroy callback for impl */
  wp_module_set_destroy_callback (module, module_destroy, impl);

  /* Add the spa lib */
  wp_remote_pipewire_add_spa_lib (rp, "api.alsa.*", "alsa/libspa-alsa");

  /* Start the monitor when the connected callback is triggered */
  g_signal_connect(rp, "state-changed::connected", (GCallback)start_monitor, impl);

  /* Register the global addded/removed callbacks */
  g_signal_connect(rp, "global-added::node", (GCallback)on_node_added, impl);
  g_signal_connect(rp, "global-removed", (GCallback)on_global_removed, impl);
}
