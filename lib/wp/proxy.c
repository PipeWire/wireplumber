/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */
/*!
 * @file proxy.c
 */
#define G_LOG_DOMAIN "wp-proxy"

#include "proxy.h"
#include "log.h"
#include "error.h"

#include <pipewire/pipewire.h>
#include <spa/utils/hook.h>
#include <spa/utils/result.h>

typedef struct _WpProxyPrivate WpProxyPrivate;
struct _WpProxyPrivate
{
  struct pw_proxy *pw_proxy;
  struct spa_hook listener;
};

/*!
 * @memberof WpProxy
 *
 * @props @b bound-id
 *
 * @code
 * "bound-id" guint
 * @endcode
 *
 * Flags : Read
 *
 * @props @b pw-proxy
 *
 * @code
 * "pw-proxy" gpointer
 * @endcode
 *
 * Flags : Read
 *
 */
enum {
  PROP_0,
  PROP_BOUND_ID,
  PROP_PW_PROXY,
};

/*!
 * @memberof WpProxy
 *
 * @signal @b bound
 *
 * @code
 * bound_callback (WpProxy * self,
 *                 guint object,
 *                 gpointer user_data)
 * @endcode
 *
 * @b Parameters:
 *
 * @arg `self`
 * @arg `object`
 * @arg `user_data`
 *
 * Flags: Run First
 *
 * @signal @b pw-proxy-created
 *
 * @code
 * pw_proxy_created_callback (WpProxy * self,
 *                            gpointer object,
 *                            gpointer user_data)
 * @endcode
 *
 * @b Parameters:
 *
 * @arg `self`
 * @arg `object`
 * @arg `user_data`
 *
 * Flags: Run First
 *
 * @signal @b pw-proxy-destroyed
 *
 * @code
 * pw_proxy_destroyed_callback (WpProxy * self,
 *                            gpointer user_data)
 * @endcode
 *
 * @b Parameters:
 *
 * @arg `self`
 * @arg `user_data`
 *
 * Flags: Run First
 *
 */
enum
{
  SIGNAL_PW_PROXY_CREATED,
  SIGNAL_PW_PROXY_DESTROYED,
  SIGNAL_BOUND,
  SIGNAL_ERROR,
  LAST_SIGNAL,
};

static guint signals[LAST_SIGNAL] = { 0 };

/*!
 * @struct WpProxy
 * @section proxy_section Pipewire Object Proxy
 *
 * @brief Base class for all objects that expose PipeWire objects using `pw_proxy`
 * underneath.
 *
 * This base class cannot be instantiated. It provides handling of
 * pw_proxy's events and exposes common functionality.
 *
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

static void
proxy_event_error (void *data, int seq, int res, const char *message)
{
  WpProxy *self = WP_PROXY (data);

  wp_trace_object (self, "error seq:%d res:%d (%s) %s",
      seq, res, spa_strerror(res), message);
  g_signal_emit (self, signals[SIGNAL_ERROR], 0, seq, res, message);
}

static const struct pw_proxy_events proxy_events = {
  PW_VERSION_PROXY_EVENTS,
  .destroy = proxy_event_destroy,
  .bound = proxy_event_bound,
  .removed = proxy_event_removed,
  .error = proxy_event_error,
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

  signals[SIGNAL_ERROR] = g_signal_new (
      "error", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_FIRST,
      G_STRUCT_OFFSET (WpProxyClass, error), NULL, NULL, NULL,
      G_TYPE_NONE, 3, G_TYPE_INT, G_TYPE_INT, G_TYPE_STRING);
}

/*!
 * @memberof WpProxy
 * @param self: the proxy
 *
 * @brief Returns the bound id, which is the id that this object has on the
 * pipewire registry (a.k.a. the global id). The object must have the
 * %WP_PROXY_FEATURE_BOUND feature before this method can be called.
 *
 * @returns the bound id of this object
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

/*!
 * @memberof WpProxy
 * @param self: the proxy
 * @param version: (out) (optional): the version of the interface
 *
 * @returns the PipeWire type of the interface that is being proxied
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

/*!
 * @memberof WpProxy
 *
 * @param self: the proxy
 * @returns a pointer to the underlying `pw_proxy` object
 */

struct pw_proxy *
wp_proxy_get_pw_proxy (WpProxy * self)
{
  g_return_val_if_fail (WP_IS_PROXY (self), NULL);

  WpProxyPrivate *priv = wp_proxy_get_instance_private (self);
  return priv->pw_proxy;
}

/*!
 * @memberof WpProxy
 *
 * @brief Private method to be used by subclasses to set the `pw_proxy` pointer
 * when it is available. This can be called only if there is no `pw_proxy`
 * already set. Takes ownership of @em proxy.
 */

void
wp_proxy_set_pw_proxy (WpProxy * self, struct pw_proxy * proxy)
{
  g_return_if_fail (WP_IS_PROXY (self));

  WpProxyPrivate *priv = wp_proxy_get_instance_private (self);

  g_return_if_fail (proxy);

  g_return_if_fail (priv->pw_proxy == NULL);
  priv->pw_proxy = proxy;

  pw_proxy_add_listener (priv->pw_proxy, &priv->listener, &proxy_events,
      self);

  /* inform subclasses and listeners */
  g_signal_emit (self, signals[SIGNAL_PW_PROXY_CREATED], 0, priv->pw_proxy);
}

static void
bind_error (WpProxy * proxy, int seq, int res, const gchar *msg,
    WpTransition * transition)
{
  wp_transition_return_error (transition, g_error_new (WP_DOMAIN_LIBRARY,
          WP_LIBRARY_ERROR_OPERATION_FAILED, "%s", msg));
}

static void
bind_success (WpProxy * proxy, uint32_t global_id, WpTransition * transition)
{
  g_signal_handlers_disconnect_by_func (proxy, bind_error, transition);
  g_signal_handlers_disconnect_by_func (proxy, bind_success, transition);
}

void
wp_proxy_watch_bind_error (WpProxy * proxy, WpTransition * transition)
{
  g_return_if_fail (WP_IS_PROXY (proxy));
  g_return_if_fail (WP_IS_TRANSITION (transition));

  g_signal_connect_object (proxy, "error", G_CALLBACK (bind_error),
      transition, 0);
  g_signal_connect_object (proxy, "bound", G_CALLBACK (bind_success),
      transition, 0);
}
