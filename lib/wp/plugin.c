/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "plugin.h"
#include "log.h"
#include "private/registry.h"

WP_DEFINE_LOCAL_LOG_TOPIC ("wp-plugin")

/*! \defgroup wpplugin WpPlugin */
/*!
 * \struct WpPlugin
 *
 * WpPlugin is a base class for objects that provide functionality to the
 * WirePlumber daemon.
 *
 * Typically, a plugin is created within a module and then registered to
 * make it available for use by the daemon. The daemon is responsible for
 * calling wp_object_activate() on it after all modules have been loaded,
 * the core is connected and the initial discovery of global objects is
 * done.
 *
 * Being a WpObject subclass, the plugin inherits WpObject's activation system.
 * For most implementations, there is only need for activating one
 * feature, WP_PLUGIN_FEATURE_ENABLED, and this can be done by implementing
 * only WpPluginClass::enable() and WpPluginClass::disable().
 * For more advanced plugins that need to have more features, you may
 * implement directly the functions of WpObjectClass and ignore the ones of
 * WpPluginClass.
 *
 * \gproperties
 *
 * \gproperty{name, gchar *, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY,
 *   The name of this plugin}
 */

enum {
  PROP_0,
  PROP_NAME,
};

typedef struct _WpPluginPrivate WpPluginPrivate;
struct _WpPluginPrivate
{
  GQuark name_quark;
};

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (WpPlugin, wp_plugin, WP_TYPE_OBJECT)

static void
wp_plugin_init (WpPlugin * self)
{
}

static void
wp_plugin_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpPlugin *self = WP_PLUGIN (object);
  WpPluginPrivate *priv = wp_plugin_get_instance_private (self);

  switch (property_id) {
  case PROP_NAME:
    priv->name_quark = g_quark_from_string (g_value_get_string (value));
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
    g_value_set_string (value, g_quark_to_string (priv->name_quark));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static WpObjectFeatures
wp_plugin_get_supported_features (WpObject * self)
{
  return WP_PLUGIN_FEATURE_ENABLED;
}

enum {
  STEP_ENABLE = WP_TRANSITION_STEP_CUSTOM_START,
};

static guint
wp_plugin_activate_get_next_step (WpObject * object,
    WpFeatureActivationTransition * transition, guint step,
    WpObjectFeatures missing)
{
  /* we only support ENABLED, so this is the only
     feature that can be in @em missing */
  g_return_val_if_fail (missing == WP_PLUGIN_FEATURE_ENABLED,
      WP_TRANSITION_STEP_ERROR);

  return STEP_ENABLE;
}

static void
wp_plugin_activate_execute_step (WpObject * object,
    WpFeatureActivationTransition * transition, guint step,
    WpObjectFeatures missing)
{
  switch (step) {
  case STEP_ENABLE: {
    WpPlugin *self = WP_PLUGIN (object);
    wp_info_object (self, "enabling plugin '%s'", wp_plugin_get_name (self));
    g_return_if_fail (WP_PLUGIN_GET_CLASS (self)->enable);
    WP_PLUGIN_GET_CLASS (self)->enable (self, WP_TRANSITION (transition));
    break;
  }
  case WP_TRANSITION_STEP_ERROR:
    break;
  default:
    g_assert_not_reached ();
  }
}

static void
wp_plugin_deactivate (WpObject * object, WpObjectFeatures features)
{
  if (features & WP_PLUGIN_FEATURE_ENABLED) {
    WpPlugin *self = WP_PLUGIN (object);
    wp_info_object (self, "disabling plugin '%s'", wp_plugin_get_name (self));
    if (WP_PLUGIN_GET_CLASS (self)->disable)
      WP_PLUGIN_GET_CLASS (self)->disable (self);
    wp_object_update_features (WP_OBJECT (self), 0, WP_PLUGIN_FEATURE_ENABLED);
  }
}

static void
wp_plugin_class_init (WpPluginClass * klass)
{
  GObjectClass * object_class = (GObjectClass *) klass;
  WpObjectClass * wpobject_class = (WpObjectClass *) klass;

  object_class->set_property = wp_plugin_set_property;
  object_class->get_property = wp_plugin_get_property;

  wpobject_class->get_supported_features = wp_plugin_get_supported_features;
  wpobject_class->activate_get_next_step = wp_plugin_activate_get_next_step;
  wpobject_class->activate_execute_step = wp_plugin_activate_execute_step;
  wpobject_class->deactivate = wp_plugin_deactivate;

  g_object_class_install_property (object_class, PROP_NAME,
      g_param_spec_string ("name", "name",
          "The name of this plugin", NULL,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

/*!
 * \brief Registers the plugin to its associated core, making it available for use
 *
 * \ingroup wpplugin
 * \param plugin (transfer full): the plugin
 */
void
wp_plugin_register (WpPlugin * plugin)
{
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (plugin));
  g_return_if_fail (WP_IS_CORE (core));

  wp_registry_register_object (wp_core_get_registry (core), plugin);
}

static gboolean
find_plugin_func (gpointer plugin, gpointer name_quark)
{
  if (!WP_IS_PLUGIN (plugin))
    return FALSE;

  WpPluginPrivate *priv = wp_plugin_get_instance_private (plugin);
  return priv->name_quark == GPOINTER_TO_UINT (name_quark);
}

/*!
 * \brief Looks up a plugin.
 *
 * \ingroup wpplugin
 * \param core the core
 * \param plugin_name the lookup name
 * \returns (transfer full) (nullable): the plugin matching the lookup name
 */
WpPlugin *
wp_plugin_find (WpCore * core, const gchar * plugin_name)
{
  g_return_val_if_fail (WP_IS_CORE (core), NULL);

  GQuark q = g_quark_try_string (plugin_name);
  if (q == 0)
    return NULL;
  GObject *p = wp_registry_find_object (wp_core_get_registry (core),
      (GEqualFunc) find_plugin_func, GUINT_TO_POINTER (q));
  return p ? WP_PLUGIN (p) : NULL;
}

/*!
 * \brief Retreives the name of a plugin.
 *
 * \ingroup wpplugin
 * \param self the plugin
 * \returns the name of this plugin
 */
const gchar *
wp_plugin_get_name (WpPlugin * self)
{
  g_return_val_if_fail (WP_IS_PLUGIN (self), NULL);

  WpPluginPrivate *priv = wp_plugin_get_instance_private (self);
  return g_quark_to_string (priv->name_quark);
}

/**
 * \var _WpPluginClass::enable
 *
 * \brief Enables the plugin. The plugin is required to start any operations
 * only when this method is called and not before.
 *
 * When enabling the plugin is done, you must call wp_object_update_features()
 * with WP_PLUGIN_FEATURE_ENABLED marked as activated, or return an error
 * on \a transition.
 *
 * \param self the plugin
 * \param transition the activation transition
 */

/**
 * \var _WpPluginClass::disable
 *
 * \brief Disables the plugin. The plugin is required to stop all operations
 * and release all resources associated with it.
 *
 * \param self the plugin
 */
