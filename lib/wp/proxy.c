/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * SECTION: proxy
 * @title: PipeWire Object Proxy
 */

#define G_LOG_DOMAIN "wp-proxy"

#include "proxy.h"
#include "debug.h"

#include <pipewire/pipewire.h>
#include <spa/utils/hook.h>

typedef struct _WpProxyPrivate WpProxyPrivate;
struct _WpProxyPrivate
{
  struct pw_proxy *pw_proxy;
  struct spa_hook listener;
};

enum {
  PROP_0,
  PROP_BOUND_ID,
  PROP_PW_PROXY,
};

enum
{
  SIGNAL_PW_PROXY_CREATED,
  SIGNAL_PW_PROXY_DESTROYED,
  SIGNAL_BOUND,
  LAST_SIGNAL,
};

static guint signals[LAST_SIGNAL] = { 0 };

/**
 * WpProxy:
 *
 * Base class for all objects that expose PipeWire objects using `pw_proxy`
 * underneath.
 *
 * This base class cannot be instantiated. It provides handling of
 * pw_proxy's events and exposes common functionality.
 */
G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (WpProxy, wp_proxy, WP_TYPE_OBJECT)

static void
proxy_event_destroy (void *data)
{
  WpProxy *self = WP_PROXY (data);
  WpProxyPrivate *priv = wp_proxy_get_instance_private (self);

  wp_trace_object (self, "destroyed pw_proxy %p (%u)", priv->pw_proxy,
      pw_proxy_get_bound_id (priv->pw_proxy));

  spa_hook_remove (&priv->listener);
  priv->pw_proxy = NULL;
  wp_object_update_features (WP_OBJECT (self), 0, WP_PROXY_FEATURE_BOUND);
  g_signal_emit (self, signals[SIGNAL_PW_PROXY_DESTROYED], 0);
}

static void
proxy_event_bound (void *data, uint32_t global_id)
{
  WpProxy *self = WP_PROXY (data);

  wp_trace_object (self, "bound to %u", global_id);

  wp_object_update_features (WP_OBJECT (self), WP_PROXY_FEATURE_BOUND, 0);
  g_signal_emit (self, signals[SIGNAL_BOUND], 0, global_id);
}

static void
proxy_event_removed (void *data)
{
  wp_trace_object (data, "removed");
}

static const struct pw_proxy_events proxy_events = {
  PW_VERSION_PROXY_EVENTS,
  .destroy = proxy_event_destroy,
  .bound = proxy_event_bound,
  .removed = proxy_event_removed,
};

static void
wp_proxy_init (WpProxy * self)
{
}

static void
wp_proxy_get_property (GObject * object, guint property_id, GValue * value,
    GParamSpec * pspec)
{
  WpProxy *self = WP_PROXY (object);

  switch (property_id) {
  case PROP_BOUND_ID:
    g_value_set_uint (value, wp_proxy_get_bound_id (self));
    break;
  case PROP_PW_PROXY:
    g_value_set_pointer (value, wp_proxy_get_pw_proxy (self));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_proxy_deactivate (WpObject * object, WpObjectFeatures features)
{
  if (features & WP_PROXY_FEATURE_BOUND) {
    WpProxyPrivate *priv = wp_proxy_get_instance_private (WP_PROXY (object));
    if (priv->pw_proxy)
      pw_proxy_destroy (priv->pw_proxy);
    wp_object_update_features (object, 0, WP_PROXY_FEATURE_BOUND);
  }
}

static void
wp_proxy_class_init (WpProxyClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpObjectClass *wpobject_class = (WpObjectClass *) klass;

  object_class->get_property = wp_proxy_get_property;

  wpobject_class->deactivate = wp_proxy_deactivate;

  /* Install the properties */

  g_object_class_install_property (object_class, PROP_BOUND_ID,
      g_param_spec_uint ("bound-id", "bound-id",
          "The id that this object has on the registry", 0, G_MAXUINT, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_PW_PROXY,
      g_param_spec_pointer ("pw-proxy", "pw-proxy", "The struct pw_proxy *",
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /* Signals */
  signals[SIGNAL_PW_PROXY_CREATED] = g_signal_new (
      "pw-proxy-created", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_FIRST,
      G_STRUCT_OFFSET (WpProxyClass, pw_proxy_created), NULL, NULL, NULL,
      G_TYPE_NONE, 1, G_TYPE_POINTER);

  signals[SIGNAL_PW_PROXY_DESTROYED] = g_signal_new (
      "pw-proxy-destroyed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_FIRST,
      G_STRUCT_OFFSET (WpProxyClass, pw_proxy_destroyed), NULL, NULL, NULL,
      G_TYPE_NONE, 0);

  signals[SIGNAL_BOUND] = g_signal_new (
      "bound", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_FIRST,
      G_STRUCT_OFFSET (WpProxyClass, bound), NULL, NULL, NULL,
      G_TYPE_NONE, 1, G_TYPE_UINT);
}

/**
 * wp_proxy_get_bound_id:
 * @self: the proxy
 *
 * Returns the bound id, which is the id that this object has on the
 * pipewire registry (a.k.a. the global id). The object must have the
 * %WP_PROXY_FEATURE_BOUND feature before this method can be called.
 *
 * Returns: the bound id of this object
 */
guint32
wp_proxy_get_bound_id (WpProxy * self)
{
  g_return_val_if_fail (WP_IS_PROXY (self), 0);
  g_warn_if_fail (wp_object_get_active_features (WP_OBJECT (self)) &
      WP_PROXY_FEATURE_BOUND);

  WpProxyPrivate *priv = wp_proxy_get_instance_private (self);
  return priv->pw_proxy ? pw_proxy_get_bound_id (priv->pw_proxy) : SPA_ID_INVALID;
}

/**
 * wp_proxy_get_interface_type:
 * @self: the proxy
 * @version: (out) (optional): the version of the interface
 *
 * Returns: the PipeWire type of the interface that is being proxied
 */
const gchar *
wp_proxy_get_interface_type (WpProxy * self, guint32 * version)
{
  g_return_val_if_fail (WP_IS_PROXY (self), NULL);

  WpProxyPrivate *priv = wp_proxy_get_instance_private (self);
  if (priv->pw_proxy)
    return pw_proxy_get_type (priv->pw_proxy, version);
  else {
    WpProxyClass *klass = WP_PROXY_GET_CLASS (self);
    if (version)
      *version = klass->pw_iface_version;
    return klass->pw_iface_type;
  }
}

/**
 * wp_proxy_get_pw_proxy:
 *
 * Returns: a pointer to the underlying `pw_proxy` object
 */
struct pw_proxy *
wp_proxy_get_pw_proxy (WpProxy * self)
{
  g_return_val_if_fail (WP_IS_PROXY (self), NULL);

  WpProxyPrivate *priv = wp_proxy_get_instance_private (self);
  return priv->pw_proxy;
}

/**
 * wp_proxy_set_pw_proxy:
 *
 * Private method to be used by subclasses to set the `pw_proxy` pointer
 * when it is available. This can be called only if there is no `pw_proxy`
 * already set. Takes ownership of @proxy.
 */
void
wp_proxy_set_pw_proxy (WpProxy * self, struct pw_proxy * proxy)
{
  g_return_if_fail (WP_IS_PROXY (self));

  WpProxyPrivate *priv = wp_proxy_get_instance_private (self);

  if (!proxy)
    return;

  g_return_if_fail (priv->pw_proxy == NULL);
  priv->pw_proxy = proxy;

  pw_proxy_add_listener (priv->pw_proxy, &priv->listener, &proxy_events,
      self);

  /* inform subclasses and listeners */
  g_signal_emit (self, signals[SIGNAL_PW_PROXY_CREATED], 0, priv->pw_proxy);
}
