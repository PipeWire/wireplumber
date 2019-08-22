/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "proxy-link.h"

#include <pipewire/pipewire.h>

struct _WpProxyLink
{
  WpProxy parent;

  /* The link proxy listener */
  struct spa_hook listener;
};

G_DEFINE_TYPE (WpProxyLink, wp_proxy_link, WP_TYPE_PROXY)

static void
link_event_info(void *data, const struct pw_link_info *info)
{
  WpProxy *proxy = WP_PROXY (data);

  wp_proxy_update_native_info (proxy, info,
      (WpProxyNativeInfoUpdate) pw_link_info_update,
      (GDestroyNotify) pw_link_info_free);
  wp_proxy_set_feature_ready (proxy, WP_PROXY_FEATURE_INFO);
}

static const struct pw_link_proxy_events link_events = {
  PW_VERSION_LINK_PROXY_EVENTS,
  .info = link_event_info,
};

static void
wp_proxy_link_init (WpProxyLink * self)
{
}

static void
wp_proxy_link_pw_proxy_created (WpProxy * proxy, struct pw_proxy * pw_proxy)
{
  WpProxyLink *self = WP_PROXY_LINK (proxy);
  pw_link_proxy_add_listener ((struct pw_link_proxy *) pw_proxy,
      &self->listener, &link_events, self);
}

static void
wp_proxy_link_class_init (WpProxyLinkClass * klass)
{
  WpProxyClass *proxy_class = (WpProxyClass *) klass;

  proxy_class->pw_proxy_created = wp_proxy_link_pw_proxy_created;
}
