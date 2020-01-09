/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "proxy-device.h"
#include "error.h"
#include "private.h"

#include <pipewire/pipewire.h>

struct _WpProxyDevice
{
  WpProxy parent;
  struct pw_device_info *info;

  /* The device proxy listener */
  struct spa_hook listener;
};

enum {
  PROP_0,
  PROP_INFO,
  PROP_PROPERTIES,
};

G_DEFINE_TYPE (WpProxyDevice, wp_proxy_device, WP_TYPE_PROXY)

static void
wp_proxy_device_init (WpProxyDevice * self)
{
}

static void
wp_proxy_device_finalize (GObject * object)
{
  WpProxyDevice *self = WP_PROXY_DEVICE (object);

  g_clear_pointer (&self->info, pw_device_info_free);

  G_OBJECT_CLASS (wp_proxy_device_parent_class)->finalize (object);
}

static void
wp_proxy_device_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpProxyDevice *self = WP_PROXY_DEVICE (object);

  switch (property_id) {
  case PROP_INFO:
    g_value_set_pointer (value, self->info);
    break;
  case PROP_PROPERTIES:
    g_value_take_boxed (value, wp_proxy_device_get_properties (self));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
device_event_info(void *data, const struct pw_device_info *info)
{
  WpProxyDevice *self = WP_PROXY_DEVICE (data);

  self->info = pw_device_info_update (self->info, info);
  g_object_notify (G_OBJECT (self), "info");

  if (info->change_mask & PW_DEVICE_CHANGE_MASK_PROPS)
    g_object_notify (G_OBJECT (self), "properties");

  wp_proxy_set_feature_ready (WP_PROXY (self), WP_PROXY_FEATURE_INFO);
}

static const struct pw_device_events device_events = {
  PW_VERSION_DEVICE_EVENTS,
  .info = device_event_info,
};

static void
wp_proxy_device_pw_proxy_created (WpProxy * proxy, struct pw_proxy * pw_proxy)
{
  WpProxyDevice *self = WP_PROXY_DEVICE (proxy);
  pw_device_add_listener ((struct pw_device *) pw_proxy,
      &self->listener, &device_events, self);
}

static void
wp_proxy_device_class_init (WpProxyDeviceClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpProxyClass *proxy_class = (WpProxyClass *) klass;

  object_class->finalize = wp_proxy_device_finalize;
  object_class->get_property = wp_proxy_device_get_property;

  proxy_class->pw_proxy_created = wp_proxy_device_pw_proxy_created;

  g_object_class_install_property (object_class, PROP_INFO,
      g_param_spec_pointer ("info", "info", "The struct pw_device_info *",
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_PROPERTIES,
      g_param_spec_boxed ("properties", "properties",
          "The pipewire properties of the proxy", WP_TYPE_PROPERTIES,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
}

WpProperties *
wp_proxy_device_get_properties (WpProxyDevice * self)
{
  return wp_properties_new_wrap_dict (self->info->props);
}
