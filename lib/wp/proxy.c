/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <pipewire/pipewire.h>

#include "proxy.h"

typedef struct _WpProxyPrivate WpProxyPrivate;
struct _WpProxyPrivate
{
  /* The core */
  WpCore *core;

  /* The proxy  */
  struct pw_proxy *proxy;

  /* The proxy listener */
  struct spa_hook listener;

  /* The done info */
  GTask *done_task;
};

enum {
  PROP_0,
  PROP_CORE,
  PROP_PROXY,
};

static void wp_proxy_async_initable_init (gpointer iface, gpointer iface_data);

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (WpProxy, wp_proxy, G_TYPE_OBJECT,
    G_ADD_PRIVATE (WpProxy)
    G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE, wp_proxy_async_initable_init))

static void
proxy_event_destroy (void *data)
{
  WpProxyPrivate *self = wp_proxy_get_instance_private (WP_PROXY(data));

  /* Set the proxy to NULL */
  self->proxy = NULL;

  /* Remove the proxy from core */
  wp_core_remove_global (self->core, WP_GLOBAL_PROXY, data);
}

static void
proxy_event_done (void *data, int seq)
{
  WpProxyPrivate *self = wp_proxy_get_instance_private (WP_PROXY(data));

  /* Make sure the task is valid */
  if (!self->done_task)
    return;

  /* Execute the task */
  g_task_return_boolean (self->done_task, TRUE);

  /* Clean up */
  g_object_unref (self->done_task);
  self->done_task = NULL;
}

static const struct pw_proxy_events proxy_events = {
  PW_VERSION_PROXY_EVENTS,
  .destroy = proxy_event_destroy,
  .done = proxy_event_done,
};

static void
wp_proxy_finalize (GObject * object)
{
  WpProxyPrivate *self = wp_proxy_get_instance_private (WP_PROXY(object));
  
  /* Remove the listener */
  spa_hook_remove (&self->listener);
  
  /* Destroy the proxy */
  if (self->proxy) {
    pw_proxy_destroy (self->proxy);
    self->proxy = NULL;
  }

  G_OBJECT_CLASS (wp_proxy_parent_class)->finalize (object);
}

static void
wp_proxy_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpProxyPrivate *self = wp_proxy_get_instance_private (WP_PROXY(object));

  switch (property_id) {
  case PROP_CORE:
    self->core = g_value_get_pointer (value);
    break;
  case PROP_PROXY:
    self->proxy = g_value_get_pointer (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_proxy_get_property (GObject * object, guint property_id, GValue * value,
    GParamSpec * pspec)
{
  WpProxyPrivate *self = wp_proxy_get_instance_private (WP_PROXY(object));

  switch (property_id) {
  case PROP_CORE:
    g_value_set_pointer (value, self->core);
    break;
  case PROP_PROXY:
    g_value_set_pointer (value, self->proxy);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_proxy_init_async (GAsyncInitable *initable, int io_priority,
    GCancellable *cancellable, GAsyncReadyCallback callback, gpointer data)
{
  WpProxyPrivate *self = wp_proxy_get_instance_private (WP_PROXY(initable));

  /* Create the async task */
  self->done_task = g_task_new (initable, cancellable, callback, data);

  /* Add the event listener */
  pw_proxy_add_listener (self->proxy, &self->listener, &proxy_events, initable);

  /* Trigger the done callback */
  pw_proxy_sync(self->proxy, 0);
}

static gboolean
wp_proxy_init_finish (GAsyncInitable *initable, GAsyncResult *result,
    GError **error)
{
  g_return_val_if_fail (g_task_is_valid (result, initable), FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

static void
wp_proxy_async_initable_init (gpointer iface, gpointer iface_data)
{
  GAsyncInitableIface *ai_iface = iface;

  ai_iface->init_async = wp_proxy_init_async;
  ai_iface->init_finish = wp_proxy_init_finish;
}

static void
wp_proxy_init (WpProxy * self)
{
}

static void
wp_proxy_class_init (WpProxyClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;

  object_class->finalize = wp_proxy_finalize;
  object_class->get_property = wp_proxy_get_property;
  object_class->set_property = wp_proxy_set_property;

  /* Install the properties */
  g_object_class_install_property (object_class, PROP_CORE,
      g_param_spec_pointer ("core", "core", "The wireplumber core",
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class, PROP_PROXY,
      g_param_spec_pointer ("pw-proxy", "pw-proxy", "The pipewire proxy",
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

void
wp_proxy_register(WpProxy * self)
{
  WpProxyPrivate *priv;
  
  g_return_if_fail (WP_IS_PROXY (self));
  
  priv = wp_proxy_get_instance_private (self);
  wp_core_register_global (priv->core, WP_GLOBAL_PROXY, g_object_ref (self),
      g_object_unref);
}

WpCore *
wp_proxy_get_core (WpProxy * self)
{
  WpProxyPrivate *priv;

  g_return_val_if_fail (WP_IS_PROXY (self), NULL);

  priv = wp_proxy_get_instance_private (self);
  return priv->core;
}

gpointer
wp_proxy_get_pw_proxy (WpProxy * self)
{
  WpProxyPrivate *priv;

  g_return_val_if_fail (WP_IS_PROXY (self), NULL);

  priv = wp_proxy_get_instance_private (self);
  return priv->proxy;
}
