/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * module-pw-bluez provides bluetooth device detection through pipewire
 * and automatically creates pipewire audio nodes to play and capture audio
 */

#include <spa/utils/keys.h>
#include <spa/utils/names.h>
#include <spa/monitor/monitor.h>
#include <pipewire/pipewire.h>
#include <wp/wp.h>

enum wp_bluez_profile {
  WP_BLUEZ_A2DP = 0,
  WP_BLUEZ_HEADUNIT = 1,  /* HSP/HFP Head Unit (Headsets) */
  WP_BLUEZ_GATEWAY = 2    /* HSP/HFP Gateway (Phones) */
};

struct monitor {
  struct spa_handle *handle;
  struct spa_monitor *monitor;
  struct spa_list device_list;
};

struct impl {
  WpModule *module;
  GHashTable *registered_endpoints;

  /* The bluez monitor */
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

  struct pw_node *adapter;
  struct pw_proxy *proxy;
};

static void
on_endpoint_created(GObject *initable, GAsyncResult *res, gpointer d)
{
  struct impl *data = d;
  WpEndpoint *endpoint = NULL;
  guint global_id = 0;
  GError *error = NULL;

  /* Get the endpoint */
  endpoint = wp_endpoint_new_finish(initable, res, NULL);
  g_return_if_fail (endpoint);

  /* Check for error */
  if (error) {
    g_clear_object (&endpoint);
    g_warning ("Failed to create client endpoint: %s", error->message);
    return;
  }

  /* Get the endpoint global id */
  g_object_get (endpoint, "global-id", &global_id, NULL);
  g_debug ("Created bluetooth endpoint for global id %d", global_id);

  /* Register the endpoint and add it to the table */
  wp_endpoint_register (endpoint);
  g_hash_table_insert (data->registered_endpoints, GUINT_TO_POINTER(global_id),
      endpoint);
}


static gboolean
parse_bluez_properties (WpProperties *props, const gchar **name,
    const gchar **media_class, enum pw_direction *direction)
{
  const char *local_name = NULL;
  const char *local_media_class = NULL;
  enum pw_direction local_direction;
  enum wp_bluez_profile profile;

  /* Get the name */
  local_name = wp_properties_get (props, PW_KEY_NODE_NAME);
  if (!local_name)
    return FALSE;

  /* Get the media class */
  local_media_class = wp_properties_get (props, PW_KEY_MEDIA_CLASS);
  if (!local_media_class)
    return FALSE;

  /* Get the direction */
  if (g_str_has_prefix (local_media_class, "Audio/Sink"))
    local_direction = PW_DIRECTION_INPUT;
  else if (g_str_has_prefix (local_media_class, "Audio/Source"))
    local_direction = PW_DIRECTION_OUTPUT;
  else
    return FALSE;

  /* Get the bluez profile */
  if (g_str_has_prefix (local_name, "bluez5.a2dp"))
    profile = WP_BLUEZ_A2DP;
  else if (g_str_has_prefix (local_name, "bluez5.hsp-hs"))
    profile = WP_BLUEZ_HEADUNIT;
  else if (g_str_has_prefix (local_name, "bluez5.hfp-hf"))
    profile = WP_BLUEZ_HEADUNIT;
  else if (g_str_has_prefix (local_name, "bluez5.hsp-ag"))
    profile = WP_BLUEZ_GATEWAY;
  else if (g_str_has_prefix (local_name, "bluez5.hfp-ag"))
    profile = WP_BLUEZ_GATEWAY;
  else
    return FALSE;

  /* Set the name */
  if (name)
    *name = local_name;

  /* Set the media class */
  if (media_class) {
    switch (local_direction) {
    case PW_DIRECTION_INPUT:
      switch (profile) {
      case WP_BLUEZ_A2DP:
        *media_class = "Bluez/Sink/A2dp";
        break;
      case WP_BLUEZ_HEADUNIT:
        *media_class = "Bluez/Sink/Headunit";
        break;
      case WP_BLUEZ_GATEWAY:
        *media_class = "Bluez/Sink/Gateway";
        break;
      default:
        break;
      }
      break;

    case PW_DIRECTION_OUTPUT:
      switch (profile) {
      case WP_BLUEZ_A2DP:
        *media_class = "Bluez/Source/A2dp";
        break;
      case WP_BLUEZ_HEADUNIT:
        *media_class = "Bluez/Source/Headunit";
        break;
      case WP_BLUEZ_GATEWAY:
        *media_class = "Bluez/Source/Gateway";
        break;
      }
      break;

    default:
      break;
    }
  }

  /* Set the direction */
  if (direction)
    *direction = local_direction;

  return TRUE;
}

/* TODO: we need to find a better way to do this */
static gboolean
is_bluez_node (WpProperties *props)
{
  const gchar *name = NULL;

  /* Get the name */
  name = wp_properties_get (props, PW_KEY_NODE_NAME);
  if (!name)
    return FALSE;

  /* Check if it is a bluez device */
  if (!g_str_has_prefix (name, "bluez5."))
    return FALSE;

  return TRUE;
}

static void
on_node_added (WpCore *core, WpProxy *proxy, struct impl *data)
{
  const gchar *name, *media_class;
  enum pw_direction direction;
  GVariantBuilder b;
  g_autoptr (WpProperties) props = NULL;
  g_autoptr (GVariant) endpoint_props = NULL;
  guint32 id = wp_proxy_get_global_id (proxy);

  props = wp_proxy_get_global_properties (proxy);
  g_return_if_fail(props);

  /* Only handle bluez nodes */
  if (!is_bluez_node (props))
    return;

  /* Parse the bluez properties */
  if (!parse_bluez_properties (props, &name, &media_class, &direction)) {
    g_critical ("failed to parse bluez properties");
    return;
  }

  /* Set the properties */
  g_variant_builder_init (&b, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&b, "{sv}",
      "name", g_variant_new_take_string (
          g_strdup_printf ("Bluez %u (%s)", id, name)));
  g_variant_builder_add (&b, "{sv}",
      "media-class", g_variant_new_string (media_class));
  g_variant_builder_add (&b, "{sv}",
      "direction", g_variant_new_uint32 (direction));
  g_variant_builder_add (&b, "{sv}",
      "proxy-node", g_variant_new_uint64 ((guint64) proxy));
  endpoint_props = g_variant_builder_end (&b);

  /* Create the endpoint async */
  wp_factory_make (core, "pipewire-simple-endpoint", WP_TYPE_ENDPOINT,
      endpoint_props, on_endpoint_created, data);
}

static void
on_node_removed (WpCore *core, WpProxy *proxy, struct impl *data)
{
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

static struct node *
create_node(struct impl *impl, struct device *dev, uint32_t id,
    const struct spa_device_object_info *info)
{
  g_autoptr (WpCore) core = wp_module_get_core (impl->module);
  struct node *node;
  const char *name, *profile;
  struct pw_properties *props = NULL;
  struct pw_factory *factory = NULL;
  struct pw_node *adapter = NULL;

  /* Check if the type is a node */
  if (info->type != SPA_TYPE_INTERFACE_Node)
    return NULL;

  /* Get the bluez name */
  name = pw_properties_get(dev->props, SPA_KEY_DEVICE_DESCRIPTION);
  if (name == NULL)
    name = pw_properties_get(dev->props, SPA_KEY_DEVICE_NAME);
  if (name == NULL)
      name = pw_properties_get(dev->props, SPA_KEY_DEVICE_NICK);
  if (name == NULL)
    name = pw_properties_get(dev->props, SPA_KEY_DEVICE_ALIAS);
  if (name == NULL)
    name = "bluetooth-device";

  /* Get the bluez profile */
  profile = spa_dict_lookup(info->props, SPA_KEY_API_BLUEZ5_PROFILE);
  if (!profile)
    profile = "null";

  /* Find the factory */
  factory = pw_core_find_factory (wp_core_get_pw_core (core), "adapter");
  g_return_val_if_fail (factory, NULL);

  /* Create the properties */
  props = pw_properties_new_dict(info->props);
  pw_properties_setf(props, PW_KEY_NODE_NAME, "bluez5.%s.%s", profile, name);
  pw_properties_set(props, PW_KEY_NODE_DESCRIPTION, name);
  pw_properties_set(props, PW_KEY_FACTORY_NAME, info->factory_name);

  /* Create the adapter */
  adapter = pw_factory_create_object(factory, NULL, PW_TYPE_INTERFACE_Node,
      PW_VERSION_NODE_PROXY, props, 0);
  if (!adapter) {
    pw_properties_free(props);
    return NULL;
  }

  /* Create the node */
  node = g_slice_new0(struct node);
  node->impl = impl;
  node->device = dev;
  node->id = id;
  node->props = props;
  node->adapter = adapter;
  node->proxy = pw_remote_export (wp_core_get_pw_remote (core),
      PW_TYPE_INTERFACE_Node, props, adapter, 0);
  if (!node->proxy) {
    pw_properties_free(props);
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

static const struct spa_device_events device_events = {
  SPA_VERSION_DEVICE_EVENTS,
  .object_info = device_object_info
};

static struct device*
create_device(struct impl *impl, uint32_t id,
    const struct spa_monitor_object_info *info)
{
  g_autoptr (WpCore) core = wp_module_get_core (impl->module);
  struct device *dev;
  struct spa_handle *handle;
  int res;
  void *iface;

  /* Check if the type is a device */
  if (info->type != SPA_TYPE_INTERFACE_Device)
    return NULL;

  /* Load the device handle */
  handle = pw_core_load_spa_handle (wp_core_get_pw_core (core),
      info->factory_name, info->props);
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
  dev->proxy = pw_remote_export (wp_core_get_pw_remote (core),
      info->type, dev->props, dev->device, 0);
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
  /* Update the properties of the device */
  pw_properties_update(dev->props, info->props);
}

static void
destroy_device(struct impl *impl, struct device *dev)
{
  struct node *node;

  /* Remove the device from the list */
  spa_list_remove(&dev->link);

  /* Remove the device listener */
  spa_hook_remove(&dev->device_listener);

  /* Destry all the nodes that the device has */
  spa_list_consume(node, &dev->node_list, link)
    destroy_node(impl, dev, node);

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
start_monitor (WpCore *core, WpRemoteState state, gpointer data)
{
  struct impl *impl = data;
  struct spa_handle *handle;
  int res;
  void *iface;

  /* Load the monitor handle */
  handle = pw_core_load_spa_handle (wp_core_get_pw_core (core),
      SPA_NAME_API_BLUEZ5_MONITOR, NULL);
  if (!handle) {
    g_message ("SPA bluez5 plugin could not be loaded; is it installed?");
    return;
  }

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

  /* Set to NULL as we don't own the reference */
  impl->module = NULL;

  /* Destroy the registered endpoints table */
  g_hash_table_unref(impl->registered_endpoints);
  impl->registered_endpoints = NULL;

  /* Clean up */
  g_slice_free (struct impl, impl);
}

void
wireplumber__module_init (WpModule * module, WpCore * core, GVariant * args)
{
  struct impl *impl;

  /* Create the module data */
  impl = g_slice_new0(struct impl);
  impl->module = module;
  impl->registered_endpoints = g_hash_table_new_full (g_direct_hash,
      g_direct_equal, NULL, (GDestroyNotify)g_object_unref);

  /* Set destroy callback for impl */
  wp_module_set_destroy_callback (module, module_destroy, impl);

  /* Add the spa lib */
  pw_core_add_spa_lib (wp_core_get_pw_core (core),
      "api.bluez5.*", "bluez5/libspa-bluez5");

  /* Start the monitor when the connected callback is triggered */
  g_signal_connect(core, "remote-state-changed::connected",
      (GCallback) start_monitor, impl);

  /* Register the global addded/removed callbacks */
  g_signal_connect(core, "remote-global-added::node",
      (GCallback) on_node_added, impl);
  g_signal_connect(core, "remote-global-removed::node",
      (GCallback) on_node_removed, impl);
}
