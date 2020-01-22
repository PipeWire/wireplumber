/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "proxy-port.h"
#include "private.h"

#include <pipewire/pipewire.h>

struct _WpProxyPort
{
  WpProxy parent;
  struct pw_port_info *info;

  /* The port proxy listener */
  struct spa_hook listener;
};

G_DEFINE_TYPE (WpProxyPort, wp_proxy_port, WP_TYPE_PROXY)

static void
wp_proxy_port_init (WpProxyPort * self)
{
}

static void
wp_proxy_port_finalize (GObject * object)
{
  WpProxyPort *self = WP_PROXY_PORT (object);

  g_clear_pointer (&self->info, pw_port_info_free);

  G_OBJECT_CLASS (wp_proxy_port_parent_class)->finalize (object);
}

static gconstpointer
wp_proxy_port_get_info (WpProxy * self)
{
  return WP_PROXY_PORT (self)->info;
}

static WpProperties *
wp_proxy_port_get_properties (WpProxy * self)
{
  return wp_properties_new_wrap_dict (WP_PROXY_PORT (self)->info->props);
}

static gint
wp_proxy_port_enum_params (WpProxy * self, guint32 id, guint32 start,
    guint32 num, const struct spa_pod *filter)
{
  struct pw_port *pwp;
  int port_enum_params_result;

  pwp = (struct pw_port *) wp_proxy_get_pw_proxy (self);
  port_enum_params_result = pw_port_enum_params (pwp, 0, id, start, num, filter);
  g_warn_if_fail (port_enum_params_result >= 0);

  return port_enum_params_result;
}

static gint
wp_proxy_port_subscribe_params (WpProxy * self, guint32 n_ids, guint32 *ids)
{
  struct pw_port *pwp;
  int port_subscribe_params_result;

  pwp = (struct pw_port *) wp_proxy_get_pw_proxy (self);
  port_subscribe_params_result = pw_port_subscribe_params (pwp, ids, n_ids);
  g_warn_if_fail (port_subscribe_params_result >= 0);

  return port_subscribe_params_result;
}

static void
port_event_info(void *data, const struct pw_port_info *info)
{
  WpProxyPort *self = WP_PROXY_PORT (data);

  self->info = pw_port_info_update (self->info, info);
  g_object_notify (G_OBJECT (self), "info");

  if (info->change_mask & PW_PORT_CHANGE_MASK_PROPS)
    g_object_notify (G_OBJECT (self), "properties");

  wp_proxy_set_feature_ready (WP_PROXY (self), WP_PROXY_FEATURE_INFO);
}

static const struct pw_port_events port_events = {
  PW_VERSION_PORT_EVENTS,
  .info = port_event_info,
  .param = wp_proxy_handle_event_param,
};

static void
wp_proxy_port_pw_proxy_created (WpProxy * proxy, struct pw_proxy * pw_proxy)
{
  WpProxyPort *self = WP_PROXY_PORT (proxy);
  pw_port_add_listener ((struct pw_port *) pw_proxy,
      &self->listener, &port_events, self);
}

static void
wp_proxy_port_class_init (WpProxyPortClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpProxyClass *proxy_class = (WpProxyClass *) klass;

  object_class->finalize = wp_proxy_port_finalize;

  proxy_class->get_info = wp_proxy_port_get_info;
  proxy_class->get_properties = wp_proxy_port_get_properties;
  proxy_class->enum_params = wp_proxy_port_enum_params;
  proxy_class->subscribe_params = wp_proxy_port_subscribe_params;

  proxy_class->pw_proxy_created = wp_proxy_port_pw_proxy_created;
}
