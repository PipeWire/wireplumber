/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "session-item.h"
#include "core.h"
#include "log.h"
#include "error.h"
#include <spa/utils/defs.h>

WP_DEFINE_LOCAL_LOG_TOPIC ("wp-si")

/*! \defgroup wpsessionitem WpSessionItem */
/*!
 * \struct WpSessionItem
 *
 * \gproperties
 *
 * \gproperty{id, guint, G_PARAM_READABLE,
 *   The session item unique id}
 *
 * \gproperty{properties, WpProperties *, G_PARAM_READABLE,
 *   The session item properties}
 */

enum {
  STEP_ACTIVATE = WP_TRANSITION_STEP_CUSTOM_START,
  STEP_EXPORT,
};

typedef struct _WpSessionItemPrivate WpSessionItemPrivate;
struct _WpSessionItemPrivate
{
  WpProperties *properties;
};

enum {
  PROP_0,
  PROP_PROPERTIES,
};

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (WpSessionItem, wp_session_item,
    WP_TYPE_OBJECT)

static void
wp_session_item_init (WpSessionItem * self)
{
  WpSessionItemPrivate *priv = wp_session_item_get_instance_private (self);

  priv->properties = NULL;
}

static void
wp_session_item_default_reset (WpSessionItem * self)
{
  WpSessionItemPrivate *priv = wp_session_item_get_instance_private (self);

  g_clear_pointer (&priv->properties, wp_properties_unref);
}

static void
wp_session_item_dispose (GObject * object)
{
  WpSessionItem * self = WP_SESSION_ITEM (object);

  wp_trace_object (self, "dispose");

  wp_session_item_reset (self);

  G_OBJECT_CLASS (wp_session_item_parent_class)->dispose (object);
}

static void
wp_session_item_get_gobject_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpSessionItem *self = WP_SESSION_ITEM (object);

  switch (property_id) {
  case PROP_PROPERTIES:
    g_value_take_boxed (value, wp_session_item_get_properties (self));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static WpObjectFeatures
session_item_default_get_supported_features (WpObject * self)
{
  return WP_SESSION_ITEM_FEATURE_ACTIVE | WP_SESSION_ITEM_FEATURE_EXPORTED;
}

static guint
session_item_default_activate_get_next_step (WpObject * object,
     WpFeatureActivationTransition * t, guint step, WpObjectFeatures missing)
{
  switch (step) {
    case WP_TRANSITION_STEP_NONE:
      if (missing & WP_SESSION_ITEM_FEATURE_ACTIVE)
        return STEP_ACTIVATE;
      else
        if (missing & WP_SESSION_ITEM_FEATURE_EXPORTED)
          return STEP_EXPORT;
        else
          return WP_TRANSITION_STEP_NONE;

    case STEP_ACTIVATE:
      if (missing & WP_SESSION_ITEM_FEATURE_EXPORTED)
        return STEP_EXPORT;
      else
        return WP_TRANSITION_STEP_NONE;

    case STEP_EXPORT:
      return WP_TRANSITION_STEP_NONE;

    default:
      return WP_TRANSITION_STEP_ERROR;
  }
}

static void
session_item_default_activate_execute_step (WpObject * object,
    WpFeatureActivationTransition * t, guint step, WpObjectFeatures missing)
{
  WpSessionItem *self = WP_SESSION_ITEM (object);
  WpTransition *transition = WP_TRANSITION (t);

  switch (step) {
    case STEP_ACTIVATE:
      if (!WP_SESSION_ITEM_GET_CLASS (self)->enable_active) {
        wp_transition_return_error (transition,
            g_error_new (WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVARIANT,
            "session-item: virtual enable_active method is not defined"));
        return;
      }
      WP_SESSION_ITEM_GET_CLASS (self)->enable_active (self, transition);
      break;

    case STEP_EXPORT:
      if (!WP_SESSION_ITEM_GET_CLASS (self)->enable_exported) {
        wp_transition_return_error (transition,
            g_error_new (WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVARIANT,
            "session-item: virtual enable_exported method is not defined"));
        return;
      }
      WP_SESSION_ITEM_GET_CLASS (self)->enable_exported (self, transition);
      break;

    case WP_TRANSITION_STEP_ERROR:
      break;

    default:
      g_return_if_reached ();
  }
}

static void
session_item_default_deactivate (WpObject * object, WpObjectFeatures features)
{
  WpSessionItem *self = WP_SESSION_ITEM (object);
  guint current = wp_object_get_active_features (object);

  if (features & current & WP_SESSION_ITEM_FEATURE_ACTIVE) {
    g_return_if_fail (WP_SESSION_ITEM_GET_CLASS (self)->disable_active);
    WP_SESSION_ITEM_GET_CLASS (self)->disable_active (self);
  }

  if (features & current & WP_SESSION_ITEM_FEATURE_EXPORTED) {
    g_return_if_fail (WP_SESSION_ITEM_GET_CLASS (self)->disable_exported);
    WP_SESSION_ITEM_GET_CLASS (self)->disable_exported (self);
  }
}

static void
wp_session_item_class_init (WpSessionItemClass * klass)
{
  WpObjectClass * wpobject_class = (WpObjectClass *) klass;
  GObjectClass * object_class = (GObjectClass *) klass;

  object_class->dispose = wp_session_item_dispose;
  object_class->get_property = wp_session_item_get_gobject_property;

  wpobject_class->get_supported_features =
      session_item_default_get_supported_features;
  wpobject_class->activate_get_next_step =
      session_item_default_activate_get_next_step;
  wpobject_class->activate_execute_step =
      session_item_default_activate_execute_step;
  wpobject_class->deactivate = session_item_default_deactivate;

  klass->reset = wp_session_item_default_reset;

  g_object_class_install_property (object_class, PROP_PROPERTIES,
      g_param_spec_boxed ("properties", "properties",
          "The session item properties", WP_TYPE_PROPERTIES,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
}

/*!
 * \brief Resets the session item.
 *
 * This essentially removes the configuration and deactivates all active features.
 *
 * \ingroup wpsessionitem
 * \param self the session item
 */
void
wp_session_item_reset (WpSessionItem * self)
{
  g_return_if_fail (WP_IS_SESSION_ITEM (self));
  g_return_if_fail (WP_SESSION_ITEM_GET_CLASS (self)->reset);

  return WP_SESSION_ITEM_GET_CLASS (self)->reset (self);
}

/*!
 * \brief Configures the session item with a set of properties
 *
 * \ingroup wpsessionitem
 * \param self the session item
 * \param props (transfer full): the properties used to configure the item
 * \returns TRUE on success, FALSE if the item could not be configured
 */
gboolean
wp_session_item_configure (WpSessionItem * self, WpProperties * props)
{
  g_return_val_if_fail (WP_IS_SESSION_ITEM (self), FALSE);
  g_return_val_if_fail (WP_SESSION_ITEM_GET_CLASS (self)->configure,
      FALSE);

  return WP_SESSION_ITEM_GET_CLASS (self)->configure (self, props);
}

/*!
 * \brief Checks if the session item is configured
 *
 * \ingroup wpsessionitem
 * \param self the session item
 * \returns TRUE if the item is configured, FALSE otherwise
 */
gboolean
wp_session_item_is_configured (WpSessionItem * self)
{
  WpSessionItemPrivate *priv = NULL;

  g_return_val_if_fail (WP_IS_SESSION_ITEM (self), FALSE);

  priv = wp_session_item_get_instance_private (self);
  return priv->properties != NULL;
}

/*!
 * \brief An associated proxy is a WpProxy subclass instance that
 * is somehow related to this item.
 *
 * \ingroup wpsessionitem
 * \param self the session item
 * \param proxy_type a WpProxy subclass GType
 * \returns (nullable) (transfer full) (type WpProxy): the associated proxy
 *   of the specified @em proxy_type, or NULL if there is no association to
 *   such a proxy
 */
gpointer
wp_session_item_get_associated_proxy (WpSessionItem * self, GType proxy_type)
{
  g_return_val_if_fail (WP_IS_SESSION_ITEM (self), NULL);
  g_return_val_if_fail (WP_SESSION_ITEM_GET_CLASS (self)->get_associated_proxy,
      NULL);
  g_return_val_if_fail (g_type_is_a (proxy_type, WP_TYPE_PROXY), NULL);

  return WP_SESSION_ITEM_GET_CLASS (self)->get_associated_proxy (self, proxy_type);
}

/*!
 * \brief Gets the bound id of a proxy associated with the session item
 *
 * \ingroup wpsessionitem
 * \param self the session item
 * \param proxy_type a WpProxy subclass GType
 * \returns the bound id of the associated proxy of the specified @em proxy_type,
 *   or `SPA_ID_INVALID` if there is no association to such a proxy
 */
guint32
wp_session_item_get_associated_proxy_id (WpSessionItem * self, GType proxy_type)
{
  g_autoptr (WpProxy) proxy = wp_session_item_get_associated_proxy (self,
      proxy_type);
  if (!proxy)
    return SPA_ID_INVALID;

  return wp_proxy_get_bound_id (proxy);
}

/*!
 * \brief Registers the session item to its associated core
 *
 * \ingroup wpsessionitem
 * \param self (transfer full): the session item
 */
void
wp_session_item_register (WpSessionItem * self)
{
  g_autoptr (WpCore) core = NULL;

  g_return_if_fail (WP_IS_SESSION_ITEM (self));

  core = wp_object_get_core (WP_OBJECT (self));
  wp_core_register_object (core, self);
}

/*!
 * \brief Removes the session item from its associated core
 *
 * \ingroup wpsessionitem
 * \param self (transfer none): the session item
 */
void
wp_session_item_remove (WpSessionItem * self)
{
  g_autoptr (WpCore) core = NULL;

  g_return_if_fail (WP_IS_SESSION_ITEM (self));

  core = wp_object_get_core (WP_OBJECT (self));
  wp_core_remove_object (core, self);
}

/*!
 * \brief Gets the properties of a session item.
 *
 * \ingroup wpsessionitem
 * \param self the session item
 * \returns (transfer full): the item's properties.
 */
WpProperties *
wp_session_item_get_properties (WpSessionItem * self)
{
  WpSessionItemPrivate *priv = NULL;

  g_return_val_if_fail (WP_IS_SESSION_ITEM (self), NULL);

  priv = wp_session_item_get_instance_private (self);
  return priv->properties ? wp_properties_ref (priv->properties) : NULL;
}

/*!
 * \brief Looks up a named session item property value for a given key.
 *
 * \ingroup wpsessionitem
 * \param self the session item
 * \param key the property key
 * \returns the item property value for the given key.
 */
const gchar *
wp_session_item_get_property (WpSessionItem * self, const gchar *key)
{
  WpSessionItemPrivate *priv = NULL;

  g_return_val_if_fail (WP_IS_SESSION_ITEM (self), NULL);

  priv = wp_session_item_get_instance_private (self);
  return priv->properties ? wp_properties_get (priv->properties, key) : NULL;
}

/*!
 * \brief Sets the item's properties.
 *
 * This should only be done by sub-classes after the configuration has been done.
 *
 * \ingroup wpsessionitem
 * \param self the session item
 * \param props (transfer full): the new properties to set
 */
void
wp_session_item_set_properties (WpSessionItem * self,
    WpProperties *props)
{
  WpSessionItemPrivate *priv = NULL;

  g_return_if_fail (WP_IS_SESSION_ITEM (self));

  priv = wp_session_item_get_instance_private (self);
  g_clear_pointer (&priv->properties, wp_properties_unref);
  priv->properties = wp_properties_ensure_unique_owner (props);
}

static gboolean
on_session_item_proxy_destroyed_deferred (WpSessionItem * item)
{
  wp_info_object (item, "destroying session item upon request by the server");
  wp_object_deactivate (WP_OBJECT (item), WP_SESSION_ITEM_FEATURE_EXPORTED);
  return G_SOURCE_REMOVE;
}

/*!
 * \brief Helper callback for sub-classes that deffers and unexports
 * the session item.
 *
 * Only meant to be used when the pipewire proxy destroyed signal is triggered.
 *
 * \ingroup wpsessionitem
 * \param proxy the proxy that was destroyed by the server
 * \param item the associated session item
 */
void
wp_session_item_handle_proxy_destroyed (WpProxy * proxy, WpSessionItem * item)
{
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (proxy));
  if (core)
    wp_core_idle_add_closure (core, NULL, g_cclosure_new_object (
        G_CALLBACK (on_session_item_proxy_destroyed_deferred),
        G_OBJECT (item)));
}
