/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * SECTION: session-item
 * @title: Session Items
 */

#define G_LOG_DOMAIN "wp-si"

#include "session-item.h"
#include "log.h"
#include "error.h"
#include "private/registry.h"

enum {
  STEP_ACTIVATE = WP_TRANSITION_STEP_CUSTOM_START,
  STEP_EXPORT,
};

typedef struct _WpSessionItemPrivate WpSessionItemPrivate;
struct _WpSessionItemPrivate
{
  guint id;
  GWeakRef parent;
  WpProperties *properties;
};

enum {
  PROP_0,
  PROP_ID,
  PROP_PROPERTIES,
};

/**
 * WpSessionItem:
 */
G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (WpSessionItem, wp_session_item,
    WP_TYPE_OBJECT)

static guint
get_next_id ()
{
  static guint next_id = 0;
  g_atomic_int_inc (&next_id);
  return next_id;
}

static void
wp_session_item_init (WpSessionItem * self)
{
  WpSessionItemPrivate *priv = wp_session_item_get_instance_private (self);

  priv->id = get_next_id ();
  g_weak_ref_init (&priv->parent, NULL);
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
wp_session_item_finalize (GObject * object)
{
  WpSessionItem * self = WP_SESSION_ITEM (object);
  WpSessionItemPrivate *priv = wp_session_item_get_instance_private (self);

  g_weak_ref_clear (&priv->parent);

  G_OBJECT_CLASS (wp_session_item_parent_class)->finalize (object);
}

static void
wp_session_item_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpSessionItem *self = WP_SESSION_ITEM (object);
  WpSessionItemPrivate *priv = wp_session_item_get_instance_private (self);

  switch (property_id) {
  case PROP_ID:
    g_value_set_uint (value, priv->id);
    break;
  case PROP_PROPERTIES:
    g_value_set_boxed (value, wp_session_item_get_properties (self));
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
  object_class->finalize = wp_session_item_finalize;
  object_class->get_property = wp_session_item_get_property;

  wpobject_class->get_supported_features =
      session_item_default_get_supported_features;
  wpobject_class->activate_get_next_step =
      session_item_default_activate_get_next_step;
  wpobject_class->activate_execute_step =
      session_item_default_activate_execute_step;
  wpobject_class->deactivate = session_item_default_deactivate;

  klass->reset = wp_session_item_default_reset;

  g_object_class_install_property (object_class, PROP_ID,
      g_param_spec_uint ("id", "id",
          "The session item unique id", 0, G_MAXUINT, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_PROPERTIES,
      g_param_spec_boxed ("properties", "properties",
          "The session item properties", WP_TYPE_PROPERTIES,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
}

/**
 * wp_session_item_get_parent:
 * @self: the session item
 *
 * Gets the item's parent, which is the #WpSessionBin this item has been added
 * to, or NULL if the item does not belong to a session bin.
 *
 * Returns: (nullable) (transfer full): the item's parent.
 */
WpSessionItem *
wp_session_item_get_parent (WpSessionItem * self)
{
  g_return_val_if_fail (WP_IS_SESSION_ITEM (self), NULL);
  WpSessionItemPrivate *priv =
      wp_session_item_get_instance_private (self);

  return g_weak_ref_get (&priv->parent);
}

/**
 * wp_session_item_set_parent:
 * @self: the session item
 * @parent: (transfer none): the parent item
 *
 * Private API.
 * Sets the item's parent; used internally by #WpSessionBin.
 */
void
wp_session_item_set_parent (WpSessionItem *self, WpSessionItem *parent)
{
  WpSessionItemPrivate *priv = NULL;

  g_return_if_fail (WP_IS_SESSION_ITEM (self));

  priv = wp_session_item_get_instance_private (self);
  g_weak_ref_set (&priv->parent, parent);
}

/**
 * wp_session_item_get_id:
 * @self: the session item
 *
 * Gets the unique Id of the session item
 */
guint
wp_session_item_get_id (WpSessionItem * self)
{
  WpSessionItemPrivate *priv = NULL;

  g_return_val_if_fail (WP_IS_SESSION_ITEM (self), SPA_ID_INVALID);

  priv = wp_session_item_get_instance_private (self);
  return priv->id;
}

/**
 * wp_session_item_reset: (virtual reset)
 * @self: the session item
 *
 * Resets the session item. This essentially removes the configuration and
 * and deactivates all active features.
 */
void
wp_session_item_reset (WpSessionItem * self)
{
  g_return_if_fail (WP_IS_SESSION_ITEM (self));
  g_return_if_fail (WP_SESSION_ITEM_GET_CLASS (self)->reset);

  return WP_SESSION_ITEM_GET_CLASS (self)->reset (self);
}

/**
 * wp_session_item_configure: (virtual configure)
 * @self: the session item
 * @props: (transfer full): the properties used to configure the item
 *
 * Returns: %TRUE on success, %FALSE if the item could not be configured
 */
gboolean
wp_session_item_configure (WpSessionItem * self, WpProperties * props)
{
  g_return_val_if_fail (WP_IS_SESSION_ITEM (self), FALSE);
  g_return_val_if_fail (WP_SESSION_ITEM_GET_CLASS (self)->configure,
      FALSE);

  return WP_SESSION_ITEM_GET_CLASS (self)->configure (self, props);
}

/**
 * wp_session_item_is_configured:
 * @self: the session item
 *
 * Returns: %TRUE if the item is configured, %FALSE otherwise
 */
gboolean
wp_session_item_is_configured (WpSessionItem * self)
{
  WpSessionItemPrivate *priv = NULL;

  g_return_val_if_fail (WP_IS_SESSION_ITEM (self), FALSE);

  priv = wp_session_item_get_instance_private (self);
  return priv->properties != NULL;
}

/**
 * wp_session_item_get_associated_proxy: (virtual get_associated_proxy)
 * @self: the session item
 * @proxy_type: a #WpProxy subclass #GType
 *
 * An associated proxy is a #WpProxy subclass instance that is somehow related
 * to this item. For example:
 *  - An exported #WpSiEndpoint should have at least:
 *      - an associated #WpEndpoint
 *      - an associated #WpSession
 *  - In cases where the item wraps a single PipeWire node, it should also
 *    have an associated #WpNode
 *
 * Returns: (nullable) (transfer full) (type WpProxy): the associated proxy
 *   of the specified @proxy_type, or %NULL if there is no association to
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

/**
 * wp_session_item_get_associated_proxy_id:
 * @self: the session item
 * @proxy_type: a #WpProxy subclass #GType
 *
 * Returns: the bound id of the associated proxy of the specified @proxy_type,
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

/**
 * wp_session_item_register:
 * @self: (transfer full): the session item
 *
 * Registers the session item to its associated core
 */
void
wp_session_item_register (WpSessionItem * self)
{
  g_autoptr (WpCore) core = NULL;

  g_return_if_fail (WP_IS_SESSION_ITEM (self));

  core = wp_object_get_core (WP_OBJECT (self));
  wp_registry_register_object (wp_core_get_registry (core), self);
}

/**
 * wp_session_item_remove:
 * @self: (transfer none): the session item
 *
 * Removes the session item from the registry
 */
void
wp_session_item_remove (WpSessionItem * self)
{
  g_autoptr (WpCore) core = NULL;

  g_return_if_fail (WP_IS_SESSION_ITEM (self));

  core = wp_object_get_core (WP_OBJECT (self));
  wp_registry_remove_object (wp_core_get_registry (core), self);
}

/**
 * wp_session_item_get_properties:
 * @self: the session item
 *
 * Returns: (transfer full): the item's properties.
 */
WpProperties *
wp_session_item_get_properties (WpSessionItem * self)
{
  WpSessionItemPrivate *priv = NULL;

  g_return_val_if_fail (WP_IS_SESSION_ITEM (self), NULL);

  priv = wp_session_item_get_instance_private (self);
  return priv->properties ? wp_properties_ref (priv->properties) : NULL;
}

/**
 * wp_session_item_set_properties:
 * @self: the session item
 * @props: (transfer full): the new properties to set
 *
 * Sets the item's properties. This should only be done by sub-classes after
 * the configuration has been done.
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

/**
 * wp_session_item_handle_proxy_destroyed:
 * @proxy: the proxy that was destroyed by the server
 * @item: the associated session item
 *
 * Helper callback for sub-classes that deffers and unexports the session item.
 * Only meant to be used when the pipewire proxy destroyed signal is triggered.
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
