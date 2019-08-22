/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "proxy-port.h"

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>

struct _WpProxyPort
{
  WpProxy parent;

  /* The port proxy listener */
  struct spa_hook listener;

  /* The port format */
  uint32_t media_type;
  uint32_t media_subtype;
  struct spa_audio_info_raw format;
};

G_DEFINE_TYPE (WpProxyPort, wp_proxy_port, WP_TYPE_PROXY)

static void
port_event_info(void *data, const struct pw_port_info *info)
{
  WpProxy *proxy = WP_PROXY (data);

  wp_proxy_update_native_info (proxy, info,
      (WpProxyNativeInfoUpdate) pw_port_info_update,
      (GDestroyNotify) pw_port_info_free);
  wp_proxy_set_feature_ready (proxy, WP_PROXY_FEATURE_INFO);
}

static void
port_event_param(void *data, int seq, uint32_t id, uint32_t index,
    uint32_t next, const struct spa_pod *param)
{
  WpProxyPort *self = WP_PROXY_PORT (data);

  /* Only handle EnumFormat */
  if (id != SPA_PARAM_EnumFormat)
    return;

  /* Parse the format */
  spa_format_parse(param, &self->media_type, &self->media_subtype);

  /* Only handle raw audio formats for now */
  if (self->media_type == SPA_MEDIA_TYPE_audio &&
      self->media_subtype == SPA_MEDIA_SUBTYPE_raw) {
    /* Parse the raw audio format */
    spa_pod_fixate ((struct spa_pod *) param);
    spa_format_audio_raw_parse (param, &self->format);
  }

  wp_proxy_set_feature_ready (WP_PROXY (self), WP_PROXY_PORT_FEATURE_FORMAT);
}

static const struct pw_port_proxy_events port_events = {
  PW_VERSION_PORT_PROXY_EVENTS,
  .info = port_event_info,
  .param = port_event_param,
};

static void
wp_proxy_port_init (WpProxyPort * self)
{
}

static void
wp_proxy_port_augment (WpProxy * proxy, WpProxyFeatures features)
{
  /* call the default implementation to ensure we have a proxy, if necessary */
  WP_PROXY_CLASS (wp_proxy_port_parent_class)->augment (proxy, features);

  if (features & WP_PROXY_PORT_FEATURE_FORMAT) {
    struct pw_proxy *pwp = wp_proxy_get_pw_proxy (proxy);
    g_return_if_fail (pwp != NULL);

    pw_port_proxy_enum_params ((struct pw_port_proxy *) pwp, 0,
        SPA_PARAM_EnumFormat, 0, -1, NULL);
  }
}

static void
wp_proxy_port_pw_proxy_created (WpProxy * proxy, struct pw_proxy * pw_proxy)
{
  WpProxyPort *self = WP_PROXY_PORT (proxy);
  pw_port_proxy_add_listener ((struct pw_port_proxy *) pw_proxy,
      &self->listener, &port_events, self);
}

static void
wp_proxy_port_class_init (WpProxyPortClass * klass)
{
  WpProxyClass *proxy_class = (WpProxyClass *) klass;

  proxy_class->augment = wp_proxy_port_augment;
  proxy_class->pw_proxy_created = wp_proxy_port_pw_proxy_created;
}

const struct spa_audio_info_raw *
wp_proxy_port_get_format (WpProxyPort * self)
{
  return &self->format;
}
