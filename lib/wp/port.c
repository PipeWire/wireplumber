/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * SECTION: WpPort
 *
 * The #WpPort class allows accessing the properties and methods of a
 * PipeWire port object (`struct pw_port`).
 *
 * A #WpPort is constructed internally when a new port appears on the
 * PipeWire registry and it is made available through the #WpObjectManager API.
 */

#define G_LOG_DOMAIN "wp-port"

#include "port.h"
#include "private.h"

#include <pipewire/pipewire.h>

/**
 * WpPort:
 */
struct _WpPort
{
  WpProxy parent;
  struct pw_port_info *info;

  /* The port proxy listener */
  struct spa_hook listener;
};

G_DEFINE_TYPE (WpPort, wp_port, WP_TYPE_PROXY)

static void
wp_port_init (WpPort * self)
{
}

static void
wp_port_finalize (GObject * object)
{
  WpPort *self = WP_PORT (object);

  g_clear_pointer (&self->info, pw_port_info_free);

  G_OBJECT_CLASS (wp_port_parent_class)->finalize (object);
}

static gconstpointer
wp_port_get_info (WpProxy * self)
{
  return WP_PORT (self)->info;
}

static WpProperties *
wp_port_get_properties (WpProxy * self)
{
  return wp_properties_new_wrap_dict (WP_PORT (self)->info->props);
}

static gint
wp_port_enum_params (WpProxy * self, guint32 id, guint32 start,
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
wp_port_subscribe_params (WpProxy * self, guint32 n_ids, guint32 *ids)
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
  WpPort *self = WP_PORT (data);

  self->info = pw_port_info_update (self->info, info);
  wp_proxy_set_feature_ready (WP_PROXY (self), WP_PROXY_FEATURE_INFO);

  g_object_notify (G_OBJECT (self), "info");

  if (info->change_mask & PW_PORT_CHANGE_MASK_PROPS)
    g_object_notify (G_OBJECT (self), "properties");
}

static const struct pw_port_events port_events = {
  PW_VERSION_PORT_EVENTS,
  .info = port_event_info,
  .param = wp_proxy_handle_event_param,
};

static void
wp_port_pw_proxy_created (WpProxy * proxy, struct pw_proxy * pw_proxy)
{
  WpPort *self = WP_PORT (proxy);
  pw_port_add_listener ((struct pw_port *) pw_proxy,
      &self->listener, &port_events, self);
}

static void
wp_port_class_init (WpPortClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpProxyClass *proxy_class = (WpProxyClass *) klass;

  object_class->finalize = wp_port_finalize;

  proxy_class->pw_iface_type = PW_TYPE_INTERFACE_Port;
  proxy_class->pw_iface_version = PW_VERSION_PORT;

  proxy_class->get_info = wp_port_get_info;
  proxy_class->get_properties = wp_port_get_properties;
  proxy_class->enum_params = wp_port_enum_params;
  proxy_class->subscribe_params = wp_port_subscribe_params;

  proxy_class->pw_proxy_created = wp_port_pw_proxy_created;
}
