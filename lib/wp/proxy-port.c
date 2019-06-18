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

  /* The port info */
  struct pw_port_info *info;

  /* The port format */
  uint32_t media_type;
  uint32_t media_subtype;
  struct spa_audio_info_raw format;
};

static GAsyncInitableIface *proxy_port_parent_interface = NULL;
static void wp_proxy_port_async_initable_init (gpointer iface,
    gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (WpProxyPort, wp_proxy_port, WP_TYPE_PROXY,
    G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE,
                           wp_proxy_port_async_initable_init))

static void
port_event_info(void *data, const struct pw_port_info *info)
{
  WpProxyPort *self = data;

  /* Update the port info */
  self->info = pw_port_info_update(self->info, info);
}

static void
port_event_param(void *data, int seq, uint32_t id, uint32_t index,
    uint32_t next, const struct spa_pod *param)
{
  WpProxyPort *self = data;

  /* Only handle EnumFormat */
  if (id != SPA_PARAM_EnumFormat)
    return;

  /* Parse the format */
  spa_format_parse(param, &self->media_type, &self->media_subtype);

  /* Only handle raw audio formats for now */
  if (self->media_type != SPA_MEDIA_TYPE_audio ||
      self->media_subtype != SPA_MEDIA_SUBTYPE_raw)
    return;

  /* Parse the raw audio format */
  spa_pod_fixate((struct spa_pod*)param);
  spa_format_audio_raw_parse(param, &self->format);
}

static const struct pw_port_proxy_events port_events = {
  PW_VERSION_PORT_PROXY_EVENTS,
  .info = port_event_info,
  .param = port_event_param,
};

static void
wp_proxy_port_finalize (GObject * object)
{
  WpProxyPort *self = WP_PROXY_PORT(object);

  /* Remove the listener */
  spa_hook_remove (&self->listener);

  /* Clear the indo */
  if (self->info) {
    pw_port_info_free(self->info);
    self->info = NULL;
  }

  G_OBJECT_CLASS (wp_proxy_port_parent_class)->finalize (object);
}

static void
wp_proxy_port_init_async (GAsyncInitable *initable, int io_priority,
    GCancellable *cancellable, GAsyncReadyCallback callback, gpointer data)
{
  WpProxyPort *self = WP_PROXY_PORT(initable);
  WpProxy *wp_proxy = WP_PROXY(initable);
  struct pw_port_proxy *proxy = NULL;

  /* Get the proxy from the base class */
  proxy = wp_proxy_get_pw_proxy(wp_proxy);

  /* Add the port proxy listener */
  pw_port_proxy_add_listener(proxy, &self->listener, &port_events, self);

  /* Emit the EnumFormat param */
  pw_port_proxy_enum_params((struct pw_port_proxy*)proxy, 0,
          SPA_PARAM_EnumFormat, 0, -1, NULL);

  /* Call the parent interface */
  proxy_port_parent_interface->init_async (initable, io_priority, cancellable,
      callback, data);
}

static void
wp_proxy_port_async_initable_init (gpointer iface, gpointer iface_data)
{
  GAsyncInitableIface *ai_iface = iface;
  
  /* Set the parent interface */
  proxy_port_parent_interface = g_type_interface_peek_parent (iface);

  /* Only set the init_async */
  ai_iface->init_async = wp_proxy_port_init_async;
}

static void
wp_proxy_port_init (WpProxyPort * self)
{
}

static void
wp_proxy_port_class_init (WpProxyPortClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;

  object_class->finalize = wp_proxy_port_finalize;
}

void
wp_proxy_port_new (gpointer proxy, GAsyncReadyCallback callback,
    gpointer user_data)
{
  g_async_initable_new_async (
      WP_TYPE_PROXY_PORT, G_PRIORITY_DEFAULT, NULL, callback, user_data,
      "pw-proxy", proxy,
      NULL);
}

WpProxyPort *
wp_proxy_port_new_finish(GObject *initable, GAsyncResult *res, GError **error)
{
  GAsyncInitable *ai = G_ASYNC_INITABLE(initable);
  return WP_PROXY_PORT(g_async_initable_new_finish(ai, res, error));
}

const struct pw_port_info *
wp_proxy_port_get_info (WpProxyPort * self)
{
  return self->info;
}

const struct spa_audio_info_raw *
wp_proxy_port_get_format (WpProxyPort * self)
{
  return &self->format;
}
