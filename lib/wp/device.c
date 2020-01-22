/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "device.h"
#include "private.h"

#include <pipewire/pipewire.h>

struct _WpDevice
{
  WpProxy parent;
  struct pw_device_info *info;

  /* The device proxy listener */
  struct spa_hook listener;
};

G_DEFINE_TYPE (WpDevice, wp_device, WP_TYPE_PROXY)

static void
wp_device_init (WpDevice * self)
{
}

static void
wp_device_finalize (GObject * object)
{
  WpDevice *self = WP_DEVICE (object);

  g_clear_pointer (&self->info, pw_device_info_free);

  G_OBJECT_CLASS (wp_device_parent_class)->finalize (object);
}

static gconstpointer
wp_device_get_info (WpProxy * self)
{
  return WP_DEVICE (self)->info;
}

static WpProperties *
wp_device_get_properties (WpProxy * self)
{
  return wp_properties_new_wrap_dict (WP_DEVICE (self)->info->props);
}

static gint
wp_device_enum_params (WpProxy * self, guint32 id, guint32 start,
    guint32 num, const struct spa_pod *filter)
{
  struct pw_device *pwp;
  int device_enum_params_result;

  pwp = (struct pw_device *) wp_proxy_get_pw_proxy (self);
  device_enum_params_result = pw_device_enum_params (pwp, 0, id, start, num,
      filter);
  g_warn_if_fail (device_enum_params_result >= 0);

  return device_enum_params_result;
}

static gint
wp_device_set_param (WpProxy * self, guint32 id, guint32 flags,
    const struct spa_pod *param)
{
  struct pw_device *pwp;
  int device_set_param_result;

  pwp = (struct pw_device *) wp_proxy_get_pw_proxy (self);
  device_set_param_result = pw_device_set_param (pwp, id, flags, param);
  g_warn_if_fail (device_set_param_result >= 0);

  return device_set_param_result;
}

static void
device_event_info(void *data, const struct pw_device_info *info)
{
  WpDevice *self = WP_DEVICE (data);

  self->info = pw_device_info_update (self->info, info);
  g_object_notify (G_OBJECT (self), "info");

  if (info->change_mask & PW_DEVICE_CHANGE_MASK_PROPS)
    g_object_notify (G_OBJECT (self), "properties");

  wp_proxy_set_feature_ready (WP_PROXY (self), WP_PROXY_FEATURE_INFO);
}

static const struct pw_device_events device_events = {
  PW_VERSION_DEVICE_EVENTS,
  .info = device_event_info,
  .param = wp_proxy_handle_event_param,
};

static void
wp_device_pw_proxy_created (WpProxy * proxy, struct pw_proxy * pw_proxy)
{
  WpDevice *self = WP_DEVICE (proxy);
  pw_device_add_listener ((struct pw_device *) pw_proxy,
      &self->listener, &device_events, self);
}

static void
wp_device_class_init (WpDeviceClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpProxyClass *proxy_class = (WpProxyClass *) klass;

  object_class->finalize = wp_device_finalize;

  proxy_class->get_info = wp_device_get_info;
  proxy_class->get_properties = wp_device_get_properties;
  proxy_class->enum_params = wp_device_enum_params;
  proxy_class->set_param = wp_device_set_param;

  proxy_class->pw_proxy_created = wp_device_pw_proxy_created;
}
