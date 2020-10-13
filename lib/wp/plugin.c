/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * SECTION: plugin
 * @title: WirePlumber Daemon Plugins
 */

#define G_LOG_DOMAIN "wp-plugin"

#include "plugin.h"
#include "private.h"

enum {
  PROP_0,
  PROP_NAME,
  PROP_MODULE,
};

typedef struct _WpPluginPrivate WpPluginPrivate;
struct _WpPluginPrivate
{
  gchar *name;
  GWeakRef module;
};

/**
 * WpPlugin:
 *
 * #WpPlugin is a base class for objects that provide functionality to the
 * WirePlumber daemon.
 *
 * Typically, a plugin is created within a module and then registered to
 * make it available for use by the daemon. The daemon is responsible for
 * calling #WpPluginClass.activate() after all modules have been loaded,
 * the core is connected and the initial discovery of global objects is
 * done.
 */
G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (WpPlugin, wp_plugin, G_TYPE_OBJECT)

static void
wp_plugin_init (WpPlugin * self)
{
  WpPluginPrivate *priv = wp_plugin_get_instance_private (self);
  g_weak_ref_init (&priv->module, NULL);
}

static void
wp_plugin_dispose (GObject * object)
{
  WpPlugin *self = WP_PLUGIN (object);

  wp_plugin_deactivate (self);

  G_OBJECT_CLASS (wp_plugin_parent_class)->dispose (object);
}

static void
wp_plugin_finalize (GObject * object)
{
  WpPlugin *self = WP_PLUGIN (object);
  WpPluginPrivate *priv = wp_plugin_get_instance_private (self);

  g_weak_ref_clear (&priv->module);
  g_clear_pointer (&priv->name, g_free);

  G_OBJECT_CLASS (wp_plugin_parent_class)->finalize (object);
}

static void
wp_plugin_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpPlugin *self = WP_PLUGIN (object);
  WpPluginPrivate *priv = wp_plugin_get_instance_private (self);

  switch (property_id) {
  case PROP_NAME:
    g_clear_pointer (&priv->name, g_free);
    priv->name = g_value_dup_string (value);
    break;
  case PROP_MODULE:
    g_weak_ref_set (&priv->module, g_value_get_object (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_plugin_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpPlugin *self = WP_PLUGIN (object);
  WpPluginPrivate *priv = wp_plugin_get_instance_private (self);

  switch (property_id) {
  case PROP_NAME:
    g_value_set_string (value, priv->name);
    break;
  case PROP_MODULE:
    g_value_take_object (value, g_weak_ref_get (&priv->module));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_plugin_class_init (WpPluginClass * klass)
{
  GObjectClass * object_class = (GObjectClass *) klass;

  object_class->dispose = wp_plugin_dispose;
  object_class->finalize = wp_plugin_finalize;
  object_class->set_property = wp_plugin_set_property;
  object_class->get_property = wp_plugin_get_property;

  /**
   * WpPlugin:name:
   * The name of this plugin.
   * Implementations should initialize this in the constructor.
   */
  g_object_class_install_property (object_class, PROP_NAME,
      g_param_spec_string ("name", "name",
          "The name of this plugin", NULL,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  /**
   * WpPlugin:module:
   * The module that created this plugin.
   * Implementations should initialize this in the constructor.
   */
  g_object_class_install_property (object_class, PROP_MODULE,
      g_param_spec_object ("module", "module",
          "The module that owns this plugin", WP_TYPE_MODULE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

/**
 * wp_plugin_register:
 * @plugin: (transfer full): the plugin
 *
 * Registers the plugin to its associated core, making it available for use
 */
void
wp_plugin_register (WpPlugin * plugin)
{
  g_autoptr (WpCore) core = wp_plugin_get_core (plugin);
  g_return_if_fail (WP_IS_CORE (core));

  wp_registry_register_object (wp_core_get_registry (core), plugin);
}

/**
 * wp_plugin_get_name:
 * @self: the plugin
 *
 * Returns: the name of this plugin
 */
const gchar *
wp_plugin_get_name (WpPlugin * self)
{
  g_return_val_if_fail (WP_IS_PLUGIN (self), NULL);

  WpPluginPrivate *priv = wp_plugin_get_instance_private (self);
  return priv->name;
}

/**
 * wp_plugin_get_module:
 * @self: the plugin
 *
 * Returns: (transfer full): the module associated with this plugin
 */
WpModule *
wp_plugin_get_module (WpPlugin * self)
{
  g_return_val_if_fail (WP_IS_PLUGIN (self), NULL);

  WpPluginPrivate *priv = wp_plugin_get_instance_private (self);
  return g_weak_ref_get (&priv->module);
}

/**
 * wp_plugin_get_core:
 * @self: the plugin
 *
 * Returns: (transfer full): the core associated with this plugin
 */
WpCore *
wp_plugin_get_core (WpPlugin * self)
{
  g_autoptr (WpModule) module = wp_plugin_get_module (self);
  return module ? wp_module_get_core (module) : NULL;
}

/**
 * wp_plugin_activate: (virtual activate)
 * @self: the plugin
 *
 * Activates the plugin. The plugin is required to start any operations only
 * when this method is called and not before.
 */
void
wp_plugin_activate (WpPlugin * self)
{
  g_return_if_fail (WP_IS_PLUGIN (self));
  g_return_if_fail (WP_PLUGIN_GET_CLASS (self)->activate);

  WP_PLUGIN_GET_CLASS (self)->activate (self);
}

/**
 * wp_plugin_deactivate: (virtual deactivate)
 * @self: the plugin
 *
 * Deactivates the plugin. The plugin is required to stop all operations and
 * release all resources associated with it.
 */
void
wp_plugin_deactivate (WpPlugin * self)
{
  g_return_if_fail (WP_IS_PLUGIN (self));
  g_return_if_fail (WP_PLUGIN_GET_CLASS (self)->deactivate);

  WP_PLUGIN_GET_CLASS (self)->deactivate (self);
}
