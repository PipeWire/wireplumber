/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "link.h"
#include "private.h"

#include <pipewire/pipewire.h>

struct _WpLink
{
  WpProxy parent;
  struct pw_link_info *info;

  /* The link proxy listener */
  struct spa_hook listener;
};

G_DEFINE_TYPE (WpLink, wp_link, WP_TYPE_PROXY)

static void
wp_link_init (WpLink * self)
{
}

static void
wp_link_finalize (GObject * object)
{
  WpLink *self = WP_LINK (object);

  g_clear_pointer (&self->info, pw_link_info_free);

  G_OBJECT_CLASS (wp_link_parent_class)->finalize (object);
}

static gconstpointer
wp_link_get_info (WpProxy * self)
{
  return WP_LINK (self)->info;
}

static WpProperties *
wp_link_get_properties (WpProxy * self)
{
  return wp_properties_new_wrap_dict (WP_LINK (self)->info->props);
}

static void
link_event_info(void *data, const struct pw_link_info *info)
{
  WpLink *self = WP_LINK (data);

  self->info = pw_link_info_update (self->info, info);
  g_object_notify (G_OBJECT (self), "info");

  if (info->change_mask & PW_LINK_CHANGE_MASK_PROPS)
    g_object_notify (G_OBJECT (self), "properties");

  wp_proxy_set_feature_ready (WP_PROXY (self), WP_PROXY_FEATURE_INFO);
}

static const struct pw_link_events link_events = {
  PW_VERSION_LINK_EVENTS,
  .info = link_event_info,
};

static void
wp_link_pw_proxy_created (WpProxy * proxy, struct pw_proxy * pw_proxy)
{
  WpLink *self = WP_LINK (proxy);
  pw_link_add_listener ((struct pw_link *) pw_proxy,
      &self->listener, &link_events, self);
}

static void
wp_link_class_init (WpLinkClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpProxyClass *proxy_class = (WpProxyClass *) klass;

  object_class->finalize = wp_link_finalize;

  proxy_class->get_info = wp_link_get_info;
  proxy_class->get_properties = wp_link_get_properties;

  proxy_class->pw_proxy_created = wp_link_pw_proxy_created;
}
