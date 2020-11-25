/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * SECTION: global-proxy
 * @title: PipeWire Global Object Proxy
 */

#define G_LOG_DOMAIN "wp-global-proxy"

#include "global-proxy.h"
#include "private/registry.h"
#include "core.h"
#include "error.h"

typedef struct _WpGlobalProxyPrivate WpGlobalProxyPrivate;
struct _WpGlobalProxyPrivate
{
  WpGlobal *global;
};

enum {
  PROP_0,
  PROP_GLOBAL,
  PROP_PERMISSIONS,
  PROP_GLOBAL_PROPERTIES,
};

/**
 * WpGlobalProxy:
 *
 * A proxy that represents a PipeWire global object, i.e. an object that is
 * made available through the PipeWire registry.
 */
G_DEFINE_TYPE_WITH_PRIVATE (WpGlobalProxy, wp_global_proxy, WP_TYPE_PROXY)

static void
wp_global_proxy_init (WpGlobalProxy * self)
{
}

static void
wp_global_proxy_dispose (GObject * object)
{
  WpGlobalProxy *self = WP_GLOBAL_PROXY (object);
  WpGlobalProxyPrivate *priv =
      wp_global_proxy_get_instance_private (self);

  if (priv->global)
    wp_global_rm_flag (priv->global, WP_GLOBAL_FLAG_OWNED_BY_PROXY);

  G_OBJECT_CLASS (wp_global_proxy_parent_class)->dispose (object);
}

static void
wp_global_proxy_finalize (GObject * object)
{
  WpGlobalProxy *self = WP_GLOBAL_PROXY (object);
  WpGlobalProxyPrivate *priv =
      wp_global_proxy_get_instance_private (self);

  g_clear_pointer (&priv->global, wp_global_unref);

  G_OBJECT_CLASS (wp_global_proxy_parent_class)->finalize (object);
}

static void
wp_global_proxy_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpGlobalProxy *self = WP_GLOBAL_PROXY (object);
  WpGlobalProxyPrivate *priv =
      wp_global_proxy_get_instance_private (self);

  switch (property_id) {
  case PROP_GLOBAL:
    priv->global = g_value_dup_boxed (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_global_proxy_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpGlobalProxy *self = WP_GLOBAL_PROXY (object);

  switch (property_id) {
  case PROP_PERMISSIONS:
    g_value_set_uint (value, wp_global_proxy_get_permissions (self));
    break;
  case PROP_GLOBAL_PROPERTIES:
    g_value_set_boxed (value, wp_global_proxy_get_global_properties (self));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static WpObjectFeatures
wp_global_proxy_get_supported_features (WpObject * object)
{
  return WP_PROXY_FEATURE_BOUND;
}

enum {
  STEP_BIND = WP_TRANSITION_STEP_CUSTOM_START,
};

static guint
wp_global_proxy_activate_get_next_step (WpObject * object,
    WpFeatureActivationTransition * transition, guint step,
    WpObjectFeatures missing)
{
  /* we only support BOUND, so this is the only
     feature that can be in @missing */
  g_return_val_if_fail (missing == WP_PROXY_FEATURE_BOUND,
      WP_TRANSITION_STEP_ERROR);

  return STEP_BIND;
}

static void
wp_global_proxy_activate_execute_step (WpObject * object,
    WpFeatureActivationTransition * transition, guint step,
    WpObjectFeatures missing)
{
  WpGlobalProxy *self = WP_GLOBAL_PROXY (object);

  switch (step) {
  case STEP_BIND:
    if (wp_proxy_get_pw_proxy (WP_PROXY (self)) == NULL) {
      if (!wp_global_proxy_bind (self)) {
        wp_transition_return_error (WP_TRANSITION (transition), g_error_new (
                WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVALID_ARGUMENT,
                "No global specified; cannot bind proxy"));
      }
    }
    break;
  case WP_TRANSITION_STEP_ERROR:
    break;
  default:
    g_assert_not_reached ();
  }
}

static void
wp_global_proxy_bound (WpProxy * proxy, guint32 global_id)
{
  WpGlobalProxy *self = WP_GLOBAL_PROXY (proxy);
  WpGlobalProxyPrivate *priv =
      wp_global_proxy_get_instance_private (self);
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));

  if (!priv->global) {
    wp_registry_prepare_new_global (wp_core_get_registry (core),
        global_id, PW_PERM_ALL, WP_GLOBAL_FLAG_OWNED_BY_PROXY,
        G_TYPE_FROM_INSTANCE (self), self, NULL, &priv->global);
  }
}

static void
wp_global_proxy_class_init (WpGlobalProxyClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpObjectClass *wpobject_class = (WpObjectClass *) klass;
  WpProxyClass *proxy_class = (WpProxyClass *) klass;

  object_class->finalize = wp_global_proxy_finalize;
  object_class->dispose = wp_global_proxy_dispose;
  object_class->set_property = wp_global_proxy_set_property;
  object_class->get_property = wp_global_proxy_get_property;

  wpobject_class->get_supported_features =
      wp_global_proxy_get_supported_features;
  wpobject_class->activate_get_next_step =
      wp_global_proxy_activate_get_next_step;
  wpobject_class->activate_execute_step =
      wp_global_proxy_activate_execute_step;

  proxy_class->bound = wp_global_proxy_bound;

  g_object_class_install_property (object_class, PROP_GLOBAL,
      g_param_spec_boxed ("global", "global", "Internal WpGlobal object",
          wp_global_get_type (),
          G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_PERMISSIONS,
      g_param_spec_uint ("permissions", "permissions",
          "The pipewire global permissions", 0, G_MAXUINT, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_GLOBAL_PROPERTIES,
      g_param_spec_boxed ("global-properties", "global-properties",
          "The pipewire global properties", WP_TYPE_PROPERTIES,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
}

/**
 * wp_global_proxy_request_destroy:
 * @self: the pipewire global
 *
 * Requests the PipeWire server to destroy the object represented by this proxy.
 * If the server allows it, the object will be destroyed and the
 * WpProxy::pw-proxy-destroyed signal will be emitted. If the server does
 * not allow it, nothing will happen.
 *
 * This is mostly useful for destroying #WpLink and #WpEndpointLink objects.
 */
void
wp_global_proxy_request_destroy (WpGlobalProxy * self)
{
  g_return_if_fail (WP_IS_GLOBAL_PROXY (self));

  WpGlobalProxyPrivate *priv =
      wp_global_proxy_get_instance_private (self);
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));

  if (priv->global && core) {
    WpRegistry *reg = wp_core_get_registry (core);
    pw_registry_destroy (reg->pw_registry, priv->global->id);
  }
}

/**
 * wp_global_proxy_get_permissions:
 * @self: the pipewire global
 *
 * Returns: the permissions that wireplumber has on this object
 */
guint32
wp_global_proxy_get_permissions (WpGlobalProxy * self)
{
  g_return_val_if_fail (WP_IS_GLOBAL_PROXY (self), 0);

  WpGlobalProxyPrivate *priv =
      wp_global_proxy_get_instance_private (self);

  return priv->global ? priv->global->permissions : PW_PERM_ALL;
}

/**
 * wp_global_proxy_get_global_properties:
 * @self: the pipewire global
 *
 * Returns: (transfer full): the global (immutable) properties of this
 *   pipewire object
 */
WpProperties *
wp_global_proxy_get_global_properties (WpGlobalProxy * self)
{
  g_return_val_if_fail (WP_IS_GLOBAL_PROXY (self), NULL);

  WpGlobalProxyPrivate *priv =
      wp_global_proxy_get_instance_private (self);

  if (priv->global && priv->global->properties)
    return wp_properties_ref (priv->global->properties);
  return NULL;
}

/**
 * wp_global_proxy_bind:
 * @self: the pipewire global
 *
 * Binds to the global and creates the underlying `pw_proxy`. This may only
 * be called if there is no `pw_proxy` associated with this object yet.
 *
 * This is mostly meant to be called internally. It will create the `pw_proxy`
 * and will activate the %WP_PROXY_FEATURE_BOUND feature.
 *
 * Returns: %TRUE on success, %FALSE if there is no global to bind to
 */
gboolean
wp_global_proxy_bind (WpGlobalProxy * self)
{
  g_return_val_if_fail (WP_IS_GLOBAL_PROXY (self), FALSE);
  g_return_val_if_fail (wp_proxy_get_pw_proxy (WP_PROXY (self)) == NULL, FALSE);

  WpGlobalProxyPrivate *priv =
      wp_global_proxy_get_instance_private (self);

  if (priv->global)
    wp_proxy_set_pw_proxy (WP_PROXY (self), wp_global_bind (priv->global));
  return !!priv->global;
}
