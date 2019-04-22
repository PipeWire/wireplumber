/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "plugin.h"
#include <pipewire/pipewire.h>

enum {
  PROP_0,
  PROP_RANK,
  PROP_NAME,
  PROP_DESCRIPTION,
  PROP_AUTHOR,
  PROP_LICENSE,
  PROP_VERSION,
  PROP_ORIGIN,
  PROP_CORE,
  PROP_METADATA,
};

typedef struct {
  WpObject *core;
  const WpPluginMetadata *metadata;
} WpPluginPrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (WpPlugin, wp_plugin, G_TYPE_OBJECT);

static void
wp_plugin_init (WpPlugin * self)
{
}

static void
wp_plugin_dispose (GObject * object)
{
  WpPlugin *plugin = WP_PLUGIN (object);
  WpPluginPrivate *priv = wp_plugin_get_instance_private (plugin);

  g_clear_object (&priv->core);

  G_OBJECT_CLASS (wp_plugin_parent_class)->dispose (object);
}

static void
wp_plugin_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpPlugin *plugin = WP_PLUGIN (object);
  WpPluginPrivate *priv = wp_plugin_get_instance_private (plugin);

  switch (property_id) {
  case PROP_CORE:
    priv->core = g_value_get_object (value);
    break;
  case PROP_METADATA:
    priv->metadata = g_value_get_pointer (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_plugin_get_property (GObject * object, guint property_id, GValue * value,
    GParamSpec * pspec)
{
  WpPlugin *plugin = WP_PLUGIN (object);
  WpPluginPrivate *priv = wp_plugin_get_instance_private (plugin);

  switch (property_id) {
  case PROP_RANK:
    g_value_set_uint (value, priv->metadata->rank);
    break;
  case PROP_NAME:
    g_value_set_string (value, priv->metadata->name);
    break;
  case PROP_DESCRIPTION:
    g_value_set_string (value, priv->metadata->description);
    break;
  case PROP_AUTHOR:
    g_value_set_string (value, priv->metadata->author);
    break;
  case PROP_LICENSE:
    g_value_set_string (value, priv->metadata->license);
    break;
  case PROP_VERSION:
    g_value_set_string (value, priv->metadata->version);
    break;
  case PROP_ORIGIN:
    g_value_set_string (value, priv->metadata->origin);
    break;
  case PROP_CORE:
    g_value_set_object (value, priv->core);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static gboolean
default_handle_pw_proxy (WpPlugin * self, WpProxy * proxy)
{
  WpPluginPrivate *priv = wp_plugin_get_instance_private (self);

  switch (wp_proxy_get_spa_type (proxy)) {
  case PW_TYPE_INTERFACE_Device:
    return wp_plugin_handle_pw_device (self, proxy);

  case PW_TYPE_INTERFACE_Client:
    return wp_plugin_handle_pw_client (self, proxy);

  case PW_TYPE_INTERFACE_Node:
    {
      g_autoptr (WpProxy) parent;
      g_autoptr (WpProxyRegistry) reg;

      reg = wp_object_get_interface (priv->core, WP_TYPE_PROXY_REGISTRY);
      parent = wp_proxy_registry_get_proxy (reg, wp_proxy_get_parent_id (proxy));

      switch (wp_proxy_get_spa_type (parent)) {
      case PW_TYPE_INTERFACE_Device:
        return wp_plugin_handle_pw_device_node (self, proxy);

      case PW_TYPE_INTERFACE_Client:
        return wp_plugin_handle_pw_client_node (self, proxy);
      }
    }

  default:
    return FALSE;
  }
}

static void
wp_plugin_class_init (WpPluginClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;

  klass->handle_pw_proxy = default_handle_pw_proxy;

  object_class->dispose = wp_plugin_dispose;
  object_class->get_property = wp_plugin_get_property;
  object_class->set_property = wp_plugin_set_property;

  g_object_class_install_property (object_class, PROP_RANK,
      g_param_spec_uint ("rank", "Rank", "The plugin rank", 0, G_MAXUINT, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_NAME,
      g_param_spec_string ("name", "Name", "The plugin's name", NULL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_DESCRIPTION,
      g_param_spec_string ("description", "Description",
          "The plugin's description", NULL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_AUTHOR,
      g_param_spec_string ("author", "Author", "The plugin's author", NULL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_LICENSE,
      g_param_spec_string ("license", "License", "The plugin's license", NULL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_VERSION,
      g_param_spec_string ("version", "Version", "The plugin's version", NULL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_ORIGIN,
      g_param_spec_string ("origin", "Origin", "The plugin's origin", NULL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_CORE,
      g_param_spec_object ("core", "Core", "The WpCore that owns this plugin",
          WP_TYPE_OBJECT,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_METADATA,
      g_param_spec_pointer ("metadata", "metadata", "metadata",
          G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS |
          G_PARAM_PRIVATE));
}

/**
 * wp_plugin_handle_pw_proxy: (virtual handle_pw_proxy)
 */
gboolean
wp_plugin_handle_pw_proxy (WpPlugin * self, WpProxy * proxy)
{
  if (WP_PLUGIN_GET_CLASS (self)->handle_pw_proxy)
    return WP_PLUGIN_GET_CLASS (self)->handle_pw_proxy (self, proxy);
  else
    return FALSE;
}

/**
 * wp_plugin_handle_pw_device: (virtual handle_pw_device)
 */
gboolean
wp_plugin_handle_pw_device (WpPlugin * self, WpProxy * proxy)
{
  if (WP_PLUGIN_GET_CLASS (self)->handle_pw_device)
    return WP_PLUGIN_GET_CLASS (self)->handle_pw_device (self, proxy);
  else
    return FALSE;
}

/**
 * wp_plugin_handle_pw_device_node: (virtual handle_pw_device_node)
 */
gboolean
wp_plugin_handle_pw_device_node (WpPlugin * self, WpProxy * proxy)
{
  if (WP_PLUGIN_GET_CLASS (self)->handle_pw_device_node)
    return WP_PLUGIN_GET_CLASS (self)->handle_pw_device_node (self, proxy);
  else
    return FALSE;
}

/**
 * wp_plugin_handle_pw_client: (virtual handle_pw_client)
 */
gboolean
wp_plugin_handle_pw_client (WpPlugin * self, WpProxy * proxy)
{
  if (WP_PLUGIN_GET_CLASS (self)->handle_pw_client)
    return WP_PLUGIN_GET_CLASS (self)->handle_pw_client (self, proxy);
  else
    return FALSE;
}

/**
 * wp_plugin_handle_pw_client_node: (virtual handle_pw_client_node)
 */
gboolean
wp_plugin_handle_pw_client_node (WpPlugin * self, WpProxy * proxy)
{
  if (WP_PLUGIN_GET_CLASS (self)->handle_pw_client_node)
    return WP_PLUGIN_GET_CLASS (self)->handle_pw_client_node (self, proxy);
  else
    return FALSE;
}

/**
 * wp_plugin_provide_interfaces: (virtual provide_interfaces)
 */
gboolean
wp_plugin_provide_interfaces (WpPlugin * self, WpObject * object)
{
  if (WP_PLUGIN_GET_CLASS (self)->provide_interfaces)
    return WP_PLUGIN_GET_CLASS (self)->provide_interfaces (self, object);
  else
    return FALSE;
}

/**
 * wp_plugin_get_core: (method)
 * @self: the plugin
 *
 * Returns: (transfer full): the core where this plugin is registered
 */
WpObject *
wp_plugin_get_core (WpPlugin * self)
{
  WpPluginPrivate *priv = wp_plugin_get_instance_private (self);
  return g_object_ref (priv->core);
}

/**
 * wp_plugin_get_metadata: (skip)
 * @self: the plugin
 *
 * This is intended for C/C++ only. Use the #WpPlugin properties in bindings.
 *
 * Returns: the metadata structure associated with this plugin
 */
const WpPluginMetadata *
wp_plugin_get_metadata (WpPlugin * self)
{
  WpPluginPrivate *priv = wp_plugin_get_instance_private (self);
  return priv->metadata;
}
