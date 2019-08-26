/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "error.h"
#include "proxy-node.h"
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>

struct _WpProxyNode
{
  WpProxy parent;

  /* The task to signal the proxy is initialized */
  GTask *init_task;

  /* The node proxy listener */
  struct spa_hook listener;

  /* The node info */
  struct pw_node_info *info;

  /* The node format, if any */
  uint32_t media_type;
  uint32_t media_subtype;
  struct spa_audio_info_raw format;
};

static void wp_proxy_node_async_initable_init (gpointer iface,
    gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (WpProxyNode, wp_proxy_node, WP_TYPE_PROXY,
    G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE,
                           wp_proxy_node_async_initable_init))

static void
node_event_info(void *data, const struct pw_node_info *info)
{
  WpProxyNode *self = data;

  /* Make sure the task is valid */
  if (!self->init_task)
    return;

  /* Update the node info */
  self->info = pw_node_info_update(self->info, info);

  /* Finish the creation of the proxy */
  g_task_return_boolean (self->init_task, TRUE);
  g_clear_object (&self->init_task);
}

static void
node_event_param(void *data, int seq, uint32_t id, uint32_t index,
    uint32_t next, const struct spa_pod *param)
{
  WpProxyNode *self = data;

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

static const struct pw_node_proxy_events node_events = {
  PW_VERSION_NODE_PROXY_EVENTS,
  .info = node_event_info,
  .param = node_event_param,
};

static void
wp_proxy_node_finalize (GObject * object)
{
  WpProxyNode *self = WP_PROXY_NODE(object);

  /* Destroy the init task */
  g_clear_object (&self->init_task);

  /* Clear the info */
  if (self->info) {
    pw_node_info_free(self->info);
    self->info = NULL;
  }

  G_OBJECT_CLASS (wp_proxy_node_parent_class)->finalize (object);
}

static void
wp_proxy_node_destroy (WpProxy * proxy)
{
  WpProxyNode *self = WP_PROXY_NODE(proxy);
  GError *error = NULL;

  /* Return error if the pipewire destruction happened while the async creation
   * of this proxy node object has not finished */
  if (self->init_task) {
    g_set_error (&error, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_OPERATION_FAILED,
        "pipewire node proxy destroyed before finishing");
    g_task_return_error (self->init_task, error);
    g_clear_object (&self->init_task);
  }
}

static void
wp_proxy_node_init_async (GAsyncInitable *initable, int io_priority,
    GCancellable *cancellable, GAsyncReadyCallback callback, gpointer data)
{
  WpProxyNode *self = WP_PROXY_NODE(initable);
  WpProxy *wp_proxy = WP_PROXY(initable);
  struct pw_node_proxy *proxy = NULL;

  /* Create the async task */
  self->init_task = g_task_new (initable, cancellable, callback, data);

  /* Get the proxy from the base class */
  proxy = wp_proxy_get_pw_proxy(wp_proxy);

  /* Add the node proxy listener */
  pw_node_proxy_add_listener(proxy, &self->listener, &node_events, self);
}

static void
wp_proxy_node_async_initable_init (gpointer iface, gpointer iface_data)
{
  GAsyncInitableIface *ai_iface = iface;

  /* Only set the init_async */
  ai_iface->init_async = wp_proxy_node_init_async;
}

static void
wp_proxy_node_init (WpProxyNode * self)
{
}

static void
wp_proxy_node_class_init (WpProxyNodeClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpProxyClass *proxy_class = (WpProxyClass *) klass;

  object_class->finalize = wp_proxy_node_finalize;

  proxy_class->destroy = wp_proxy_node_destroy;
}

void
wp_proxy_node_new (guint global_id, gpointer proxy,
    GAsyncReadyCallback callback, gpointer user_data)
{
  g_async_initable_new_async (
      WP_TYPE_PROXY_NODE, G_PRIORITY_DEFAULT, NULL, callback, user_data,
      "global-id", global_id,
      "pw-proxy", proxy,
      NULL);
}

WpProxyNode *
wp_proxy_node_new_finish(GObject *initable, GAsyncResult *res, GError **error)
{
  GAsyncInitable *ai = G_ASYNC_INITABLE(initable);
  return WP_PROXY_NODE(g_async_initable_new_finish(ai, res, error));
}

const struct pw_node_info *
wp_proxy_node_get_info (WpProxyNode * self)
{
  return self->info;
}

const struct spa_audio_info_raw *
wp_proxy_node_get_format (WpProxyNode * self)
{
  return &self->format;
}

void wp_proxy_node_enum_params(WpProxyNode * self, guint id, guint index,
    guint num, guint next, gconstpointer filter)
{
  pw_port_proxy_enum_params (wp_proxy_get_pw_proxy (WP_PROXY (self)), id,
          index, num, next, filter);
}
