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

  /* The task to signal the proxy is initialized */
  GTask *init_task;
  
  /* The link proxy listener */
  struct spa_hook listener;

  /* The link info */
  struct pw_link_info *info;
};

static void wp_proxy_link_async_initable_init (gpointer iface,
    gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (WpProxyLink, wp_proxy_link, WP_TYPE_PROXY,
    G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE,
                           wp_proxy_link_async_initable_init))

static void
link_event_info(void *data, const struct pw_link_info *info)
{
  WpProxyLink *self = data;

  /* Make sure the task is valid */
  if (!self->init_task)
    return;

  /* Update the link info */
  self->info = pw_link_info_update(self->info, info);

  /* Finish the creation of the proxy */
  g_task_return_boolean (self->init_task, TRUE);
  g_clear_object (&self->init_task);
}

static const struct pw_link_proxy_events link_events = {
  PW_VERSION_LINK_PROXY_EVENTS,
  .info = link_event_info,
};

static void
wp_proxy_link_finalize (GObject * object)
{
  WpProxyLink *self = WP_PROXY_LINK(object);

  /* Destroy the init task */
  g_clear_object (&self->init_task);

  /* Clear the info */
  if (self->info) {
    pw_link_info_free(self->info);
    self->info = NULL;
  }

  G_OBJECT_CLASS (wp_proxy_link_parent_class)->finalize (object);
}

static void
wp_proxy_link_init_async (GAsyncInitable *initable, int io_priority,
    GCancellable *cancellable, GAsyncReadyCallback callback, gpointer data)
{
  WpProxyLink *self = WP_PROXY_LINK(initable);
  WpProxy *wp_proxy = WP_PROXY(initable);
  struct pw_link_proxy *proxy = NULL;

  /* Create the async task */
  self->init_task = g_task_new (initable, cancellable, callback, data);

  /* Get the proxy from the base class */
  proxy = wp_proxy_get_pw_proxy(wp_proxy);

  /* Add the link proxy listener */
  pw_link_proxy_add_listener(proxy, &self->listener, &link_events, self);
}

static void
wp_proxy_link_async_initable_init (gpointer iface, gpointer iface_data)
{
  GAsyncInitableIface *ai_iface = iface;

  /* Only set the init_async */
  ai_iface->init_async = wp_proxy_link_init_async;
}

static void
wp_proxy_link_init (WpProxyLink * self)
{
}

static void
wp_proxy_link_class_init (WpProxyLinkClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;

  object_class->finalize = wp_proxy_link_finalize;
}

void
wp_proxy_link_new (guint global_id, gpointer proxy,
    GAsyncReadyCallback callback, gpointer user_data)
{
  g_async_initable_new_async (
      WP_TYPE_PROXY_LINK, G_PRIORITY_DEFAULT, NULL, callback, user_data,
      "global-id", global_id,
      "pw-proxy", proxy,
      NULL);
}

WpProxyLink *
wp_proxy_link_new_finish(GObject *initable, GAsyncResult *res, GError **error)
{
  GAsyncInitable *ai = G_ASYNC_INITABLE(initable);
  return WP_PROXY_LINK(g_async_initable_new_finish(ai, res, error));
}

const struct pw_link_info *
wp_proxy_link_get_info (WpProxyLink * self)
{
  return self->info;
}
