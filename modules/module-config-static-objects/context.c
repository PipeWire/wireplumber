/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>
#include <pipewire/keys.h>

#include "parser-device.h"
#include "parser-node.h"
#include "context.h"

struct _WpConfigStaticObjectsContext
{
  GObject parent;

  WpCore *local_core;
  WpObjectManager *devices_om;
  GPtrArray *static_objects;
};

enum {
  SIGNAL_OBJECT_CREATED,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

G_DEFINE_TYPE (WpConfigStaticObjectsContext, wp_config_static_objects_context,
    WP_TYPE_PLUGIN)

static void
on_object_created (GObject * proxy, GAsyncResult * res, gpointer user_data)
{
  WpConfigStaticObjectsContext *self = user_data;
  g_autoptr (GError) error = NULL;

  if (!wp_proxy_augment_finish (WP_PROXY (proxy), res, &error)) {
    wp_warning_object (self, "failed to export object: %s", error->message);
    return;
  }

  g_ptr_array_add (self->static_objects, g_object_ref (proxy));

  /* Emit the node-created signal */
  g_signal_emit (self, signals[SIGNAL_OBJECT_CREATED], 0, proxy);
}

static void
wp_config_static_objects_context_create_node (WpConfigStaticObjectsContext *self,
  const struct WpParserNodeData *node_data)
{
  g_autoptr (GObject) node = NULL;
  g_return_if_fail (self->local_core);

  /* Create the node */
  node = node_data->n.local ?
      (GObject *) wp_impl_node_new_from_pw_factory (self->local_core,
          node_data->n.factory, wp_properties_ref (node_data->n.props)) :
      (GObject *) wp_node_new_from_factory (self->local_core,
          node_data->n.factory, wp_properties_ref (node_data->n.props));
  if (!node) {
    wp_warning_object (self, "failed to create node");
    return;
  }

  /* export */
  if (WP_IS_IMPL_NODE (node)) {
    wp_impl_node_export (WP_IMPL_NODE (node));
    g_ptr_array_add (self->static_objects, g_object_ref (node));
    g_signal_emit (self, signals[SIGNAL_OBJECT_CREATED], 0, node);
  } else {
    wp_proxy_augment (WP_PROXY (node), WP_PROXY_FEATURES_STANDARD, NULL,
        on_object_created, self);
  }
}

static void
on_device_added (WpObjectManager *om, WpProxy *proxy, gpointer p)
{
  WpConfigStaticObjectsContext *self = p;
  g_autoptr (WpProperties) dev_props = NULL;
  g_autoptr (WpCore) core = wp_plugin_get_core (WP_PLUGIN (self));
  g_autoptr (WpConfiguration) config = wp_configuration_get_instance (core);
  g_autoptr (WpConfigParser) parser = NULL;
  const struct WpParserNodeData *node_data = NULL;

  /* Skip devices without info feature */
  if ((wp_proxy_get_features(proxy) & WP_PROXY_FEATURE_INFO) == 0)
    return;

  /* Get the device properties */
  dev_props = wp_proxy_get_properties (proxy);
  g_return_if_fail (dev_props);

  /* Get the parser node data and skip the node if not found */
  parser = wp_configuration_get_parser (config, WP_PARSER_NODE_EXTENSION);
  node_data = wp_config_parser_get_matched_data (parser, dev_props);
  if (!node_data)
    return;

  /* Create the node */
  wp_config_static_objects_context_create_node (self, node_data);
}

static gboolean
parser_device_foreach_func (const struct WpParserDeviceData *device_data,
    gpointer data)
{
  WpConfigStaticObjectsContext *self = data;
  g_autoptr (WpDevice) device = NULL;
  g_return_val_if_fail (self->local_core, FALSE);

  /* Create the device */
  device = wp_device_new_from_factory (self->local_core, device_data->factory,
      device_data->props ? wp_properties_ref (device_data->props) : NULL);
  if (!device) {
    wp_warning_object (self, "failed to create device");
    return TRUE;
  }

  /* export */
  wp_proxy_augment (WP_PROXY (device), WP_PROXY_FEATURES_STANDARD, NULL,
     on_object_created, self);

  return TRUE;
}

static gboolean
parser_node_foreach_func (const struct WpParserNodeData *node_data,
    gpointer data)
{
  WpConfigStaticObjectsContext *self = data;

  /* Only create nodes that don't have match-device info */
  if (!node_data->has_md) {
    wp_config_static_objects_context_create_node (self, node_data);
    return TRUE;
  }

  return TRUE;
}

static void
wp_config_static_objects_context_activate (WpPlugin * plugin)
{
  WpConfigStaticObjectsContext *self = WP_CONFIG_STATIC_OBJECTS_CONTEXT (plugin);
  g_autoptr (WpCore) core = wp_plugin_get_core (plugin);
  g_autoptr (WpConfiguration) config = wp_configuration_get_instance (core);

  /* Create and connect the local core */
  self->local_core = wp_core_clone (core);
  wp_core_update_properties (self->local_core, wp_properties_new (
        PW_KEY_APP_NAME, "WirePlumber (static-objects)",
        NULL));
  if (!wp_core_connect (self->local_core)) {
    wp_warning_object (self, "failed to connect local core");
    return;
  }

  self->static_objects = g_ptr_array_new_with_free_func (g_object_unref);

  /* Create and install the device object manager */
  self->devices_om = wp_object_manager_new ();
  wp_object_manager_add_interest (self->devices_om, WP_TYPE_DEVICE, NULL);
  wp_object_manager_request_proxy_features (self->devices_om, WP_TYPE_DEVICE,
      WP_PROXY_FEATURE_INFO);
  g_signal_connect (self->devices_om, "object-added",
      (GCallback) on_device_added, self);
  wp_core_install_object_manager (self->local_core, self->devices_om);

  /* Add the node parser and parse the node files */
  wp_configuration_add_extension (config, WP_PARSER_NODE_EXTENSION,
      WP_TYPE_PARSER_NODE);
  wp_configuration_reload (config, WP_PARSER_NODE_EXTENSION);

  /* Add the device parser and parse the device files */
  wp_configuration_add_extension (config, WP_PARSER_DEVICE_EXTENSION,
      WP_TYPE_PARSER_DEVICE);
  wp_configuration_reload (config, WP_PARSER_DEVICE_EXTENSION);

  /* Create static devices */
  {
    g_autoptr (WpConfigParser) parser =
        wp_configuration_get_parser (config, WP_PARSER_DEVICE_EXTENSION);
    wp_parser_device_foreach (WP_PARSER_DEVICE (parser),
        parser_device_foreach_func, self);
  }

  /* Create static nodes without match-device */
  {
    g_autoptr (WpConfigParser) parser =
        wp_configuration_get_parser (config, WP_PARSER_NODE_EXTENSION);
    wp_parser_node_foreach (WP_PARSER_NODE (parser),
        parser_node_foreach_func, self);
  }
}

static void
wp_config_static_objects_context_deactivate (WpPlugin *plugin)
{
  WpConfigStaticObjectsContext *self = WP_CONFIG_STATIC_OBJECTS_CONTEXT (plugin);

  g_clear_object (&self->devices_om);
  g_clear_pointer (&self->static_objects, g_ptr_array_unref);

  g_autoptr (WpCore) core = wp_plugin_get_core (plugin);
  if (core) {
    g_autoptr (WpConfiguration) config = wp_configuration_get_instance (core);
    wp_configuration_remove_extension (config, WP_PARSER_DEVICE_EXTENSION);
    wp_configuration_remove_extension (config, WP_PARSER_NODE_EXTENSION);
  }

  g_clear_object (&self->local_core);
}

static void
wp_config_static_objects_context_init (WpConfigStaticObjectsContext *self)
{
}

static void
wp_config_static_objects_context_class_init (WpConfigStaticObjectsContextClass *klass)
{
  WpPluginClass *plugin_class = (WpPluginClass *) klass;

  plugin_class->activate = wp_config_static_objects_context_activate;
  plugin_class->deactivate = wp_config_static_objects_context_deactivate;

  /* Signals */
  signals[SIGNAL_OBJECT_CREATED] = g_signal_new ("object-created",
      G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
      G_TYPE_NONE, 1, WP_TYPE_PROXY);
}

WpConfigStaticObjectsContext *
wp_config_static_objects_context_new (WpModule * module)
{
  return g_object_new (wp_config_static_objects_context_get_type (),
      "module", module,
      NULL);
}
