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
};

G_DEFINE_TYPE (WpProxyNode, wp_proxy_node, WP_TYPE_PROXY)

static void
node_event_info(void *data, const struct pw_node_info *info)
{
  WpProxy *proxy = WP_PROXY (data);

  wp_proxy_update_native_info (proxy, info,
      (WpProxyNativeInfoUpdate) pw_node_info_update,
      (GDestroyNotify) pw_node_info_free);
  wp_proxy_set_feature_ready (proxy, WP_PROXY_FEATURE_INFO);
}

static const struct pw_node_proxy_events node_events = {
  PW_VERSION_NODE_PROXY_EVENTS,
  .info = node_event_info,
};

static void
wp_proxy_node_init (WpProxyNode * self)
{
}

static void
wp_proxy_node_pw_proxy_created (WpProxy * proxy, struct pw_proxy * pw_proxy)
{
  WpProxyNode *self = WP_PROXY_NODE (proxy);
  pw_node_proxy_add_listener ((struct pw_node_proxy *) pw_proxy,
      &self->listener, &node_events, self);
}

static void
wp_proxy_node_class_init (WpProxyNodeClass * klass)
{
  WpProxyClass *proxy_class = (WpProxyClass *) klass;

  proxy_class->pw_proxy_created = wp_proxy_node_pw_proxy_created;
}
