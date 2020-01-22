/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "client.h"
#include "private.h"

#include <pipewire/pipewire.h>

struct _WpClient
{
  WpProxy parent;
  struct pw_client_info *info;

  /* The client proxy listener */
  struct spa_hook listener;
};

G_DEFINE_TYPE (WpClient, wp_client, WP_TYPE_PROXY)

static void
wp_client_init (WpClient * self)
{
}

static void
wp_client_finalize (GObject * object)
{
  WpClient *self = WP_CLIENT (object);

  g_clear_pointer (&self->info, pw_client_info_free);

  G_OBJECT_CLASS (wp_client_parent_class)->finalize (object);
}

static gconstpointer
wp_client_get_info (WpProxy * self)
{
  return WP_CLIENT (self)->info;
}

WpProperties *
wp_client_get_properties (WpProxy * self)
{
  return wp_properties_new_wrap_dict (WP_CLIENT (self)->info->props);
}

static void
client_event_info(void *data, const struct pw_client_info *info)
{
  WpClient *self = WP_CLIENT (data);

  self->info = pw_client_info_update (self->info, info);
  g_object_notify (G_OBJECT (self), "info");

  if (info->change_mask & PW_CLIENT_CHANGE_MASK_PROPS)
    g_object_notify (G_OBJECT (self), "properties");

  wp_proxy_set_feature_ready (WP_PROXY (self), WP_PROXY_FEATURE_INFO);
}

static const struct pw_client_events client_events = {
  PW_VERSION_CLIENT_EVENTS,
  .info = client_event_info,
};

static void
wp_client_pw_proxy_created (WpProxy * proxy, struct pw_proxy * pw_proxy)
{
  WpClient *self = WP_CLIENT (proxy);
  pw_client_add_listener ((struct pw_client *) pw_proxy,
      &self->listener, &client_events, self);
}

static void
wp_client_class_init (WpClientClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpProxyClass *proxy_class = (WpProxyClass *) klass;

  object_class->finalize = wp_client_finalize;

  proxy_class->get_info = wp_client_get_info;
  proxy_class->get_properties = wp_client_get_properties;

  proxy_class->pw_proxy_created = wp_client_pw_proxy_created;
}

void
wp_client_update_permissions (WpClient * self, guint n_perm, ...)
{
  va_list args;
  struct pw_permission *perm =
      g_alloca (n_perm * sizeof (struct pw_permission));

  va_start (args, n_perm);
  for (gint i = 0; i < n_perm; i++) {
    perm[i].id = va_arg (args, guint32);
    perm[i].permissions = va_arg (args, guint32);
  }
  va_end (args);

  wp_client_update_permissions_array (self, n_perm, perm);
}

void
wp_client_update_permissions_array (WpClient * self,
    guint n_perm, const struct pw_permission *permissions)
{
  struct pw_client *pwp;
  int client_update_permissions_result;

  g_return_if_fail (WP_IS_CLIENT (self));

  pwp = (struct pw_client *) wp_proxy_get_pw_proxy (WP_PROXY (self));
  g_return_if_fail (pwp != NULL);

  client_update_permissions_result = pw_client_update_permissions (
      pwp, n_perm, permissions);
  g_warn_if_fail (client_update_permissions_result >= 0);
}
