/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "proxy-node.h"
#include <pipewire/pipewire.h>

struct _WpProxyNode
{
  WpProxy parent;
  
  /* The node proxy listener */
  struct spa_hook listener;

  /* The node info */
  struct pw_node_info *info;
};

static GAsyncInitableIface *proxy_node_parent_interface = NULL;
static void wp_proxy_node_async_initable_init (gpointer iface,
    gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (WpProxyNode, wp_proxy_node, WP_TYPE_PROXY,
    G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE,
                           wp_proxy_node_async_initable_init))

static void
node_event_info(void *data, const struct pw_node_info *info)
{
  WpProxyNode *self = data;

  /* Update the node info */
  self->info = pw_node_info_update(self->info, info);
}

static const struct pw_node_proxy_events node_events = {
  PW_VERSION_NODE_PROXY_EVENTS,
  .info = node_event_info,
};

static void
wp_proxy_node_finalize (GObject * object)
{
  WpProxyNode *self = WP_PROXY_NODE(object);

  /* Remove the listener */
  spa_hook_remove (&self->listener);
  
  /* Clear the info */
  if (self->info) {
    pw_node_info_free(self->info);
    self->info = NULL;
  }

  G_OBJECT_CLASS (wp_proxy_node_parent_class)->finalize (object);
}

static void
wp_proxy_node_init_async (GAsyncInitable *initable, int io_priority,
    GCancellable *cancellable, GAsyncReadyCallback callback, gpointer data)
{
  WpProxyNode *self = WP_PROXY_NODE(initable);
  WpProxy *wp_proxy = WP_PROXY(initable);
  struct pw_node_proxy *proxy = NULL;

  /* Get the proxy from the base class */
  proxy = wp_proxy_get_pw_proxy(wp_proxy);

  /* Add the node proxy listener */
  pw_node_proxy_add_listener(proxy, &self->listener, &node_events, self);

  /* Call the parent interface */
  proxy_node_parent_interface->init_async (initable, io_priority, cancellable,
      callback, data);
}

static void
wp_proxy_node_async_initable_init (gpointer iface, gpointer iface_data)
{
  GAsyncInitableIface *ai_iface = iface;
  
  /* Set the parent interface */
  proxy_node_parent_interface = g_type_interface_peek_parent (iface);

  /* Only set the init_async */
  ai_iface->init_async = wp_proxy_node_init_async;
}

static void
wp_proxy_node_init (WpProxyNode * self)
{
}

static void
wp_proxy_node_class_init (WpProxyNodeClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;

  object_class->finalize = wp_proxy_node_finalize;
}

void
wp_proxy_node_new (guint global_id, gpointer proxy,
    GAsyncReadyCallback callback, gpointer user_data)
{
  g_async_initable_new_async (
      WP_TYPE_PROXY_NODE, G_PRIORITY_DEFAULT, NULL, callback, user_data,
      "global-id", global_id,
      "pw-proxy", proxy,
      NULL);
}

WpProxyNode *
wp_proxy_node_new_finish(GObject *initable, GAsyncResult *res, GError **error)
{
  GAsyncInitable *ai = G_ASYNC_INITABLE(initable);
  return WP_PROXY_NODE(g_async_initable_new_finish(ai, res, error));
}

const struct pw_node_info *
wp_proxy_node_get_info (WpProxyNode * self)
{
  return self->info;
}
