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
  struct pw_link_info *info;

  /* The link proxy listener */
  struct spa_hook listener;
};

enum {
  PROP_0,
  PROP_INFO,
  PROP_PROPERTIES,
};

G_DEFINE_TYPE (WpProxyLink, wp_proxy_link, WP_TYPE_PROXY)

static void
wp_proxy_link_init (WpProxyLink * self)
{
}

static void
wp_proxy_link_finalize (GObject * object)
{
  WpProxyLink *self = WP_PROXY_LINK (object);

  g_clear_pointer (&self->info, pw_link_info_free);

  G_OBJECT_CLASS (wp_proxy_link_parent_class)->finalize (object);
}

static void
wp_proxy_link_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpProxyLink *self = WP_PROXY_LINK (object);

  switch (property_id) {
  case PROP_INFO:
    g_value_set_pointer (value, self->info);
    break;
  case PROP_PROPERTIES:
    g_value_take_boxed (value, wp_proxy_link_get_properties (self));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
link_event_info(void *data, const struct pw_link_info *info)
{
  WpProxyLink *self = WP_PROXY_LINK (data);

  self->info = pw_link_info_update (self->info, info);
  g_object_notify (G_OBJECT (self), "info");

  if (info->change_mask & PW_LINK_CHANGE_MASK_PROPS)
    g_object_notify (G_OBJECT (self), "properties");

  wp_proxy_set_feature_ready (WP_PROXY (self), WP_PROXY_FEATURE_INFO);
}

static const struct pw_link_proxy_events link_events = {
  PW_VERSION_LINK_PROXY_EVENTS,
  .info = link_event_info,
};

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
  GObjectClass *object_class = (GObjectClass *) klass;
  WpProxyClass *proxy_class = (WpProxyClass *) klass;

  object_class->finalize = wp_proxy_link_finalize;
  object_class->get_property = wp_proxy_link_get_property;

  proxy_class->pw_proxy_created = wp_proxy_link_pw_proxy_created;

  g_object_class_install_property (object_class, PROP_INFO,
      g_param_spec_pointer ("info", "info", "The struct pw_link_info *",
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_PROPERTIES,
      g_param_spec_boxed ("properties", "properties",
          "The pipewire properties of the proxy", WP_TYPE_PROPERTIES,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
}

const struct pw_link_info *
wp_proxy_link_get_info (WpProxyLink * self)
{
  return self->info;
}

WpProperties *
wp_proxy_link_get_properties (WpProxyLink * self)
{
  return wp_properties_new_wrap_dict (self->info->props);
}
