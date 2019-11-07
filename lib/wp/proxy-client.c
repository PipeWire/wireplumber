/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "proxy-client.h"
#include "private.h"

#include <pipewire/pipewire.h>

struct _WpProxyClient
{
  WpProxy parent;
  struct pw_client_info *info;

  /* The client proxy listener */
  struct spa_hook listener;
};

enum {
  PROP_0,
  PROP_INFO,
  PROP_PROPERTIES,
};

G_DEFINE_TYPE (WpProxyClient, wp_proxy_client, WP_TYPE_PROXY)

static void
wp_proxy_client_init (WpProxyClient * self)
{
}

static void
wp_proxy_client_finalize (GObject * object)
{
  WpProxyClient *self = WP_PROXY_CLIENT (object);

  g_clear_pointer (&self->info, pw_client_info_free);

  G_OBJECT_CLASS (wp_proxy_client_parent_class)->finalize (object);
}

static void
wp_proxy_client_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpProxyClient *self = WP_PROXY_CLIENT (object);

  switch (property_id) {
  case PROP_INFO:
    g_value_set_pointer (value, self->info);
    break;
  case PROP_PROPERTIES:
    g_value_take_boxed (value, wp_proxy_client_get_properties (self));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
client_event_info(void *data, const struct pw_client_info *info)
{
  WpProxyClient *self = WP_PROXY_CLIENT (data);

  self->info = pw_client_info_update (self->info, info);
  g_object_notify (G_OBJECT (self), "info");

  if (info->change_mask & PW_CLIENT_CHANGE_MASK_PROPS)
    g_object_notify (G_OBJECT (self), "properties");

  wp_proxy_set_feature_ready (WP_PROXY (self), WP_PROXY_FEATURE_INFO);
}

static const struct pw_client_proxy_events client_events = {
  PW_VERSION_CLIENT_PROXY_EVENTS,
  .info = client_event_info,
};

static void
wp_proxy_client_pw_proxy_created (WpProxy * proxy, struct pw_proxy * pw_proxy)
{
  WpProxyClient *self = WP_PROXY_CLIENT (proxy);
  pw_client_proxy_add_listener ((struct pw_client_proxy *) pw_proxy,
      &self->listener, &client_events, self);
}

static void
wp_proxy_client_class_init (WpProxyClientClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpProxyClass *proxy_class = (WpProxyClass *) klass;

  object_class->finalize = wp_proxy_client_finalize;
  object_class->get_property = wp_proxy_client_get_property;

  proxy_class->pw_proxy_created = wp_proxy_client_pw_proxy_created;

  g_object_class_install_property (object_class, PROP_INFO,
      g_param_spec_pointer ("info", "info", "The struct pw_client_info *",
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_PROPERTIES,
      g_param_spec_boxed ("properties", "properties",
          "The pipewire properties of the proxy", WP_TYPE_PROPERTIES,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
}

const struct pw_client_info *
wp_proxy_client_get_info (WpProxyClient * self)
{
  return self->info;
}

WpProperties *
wp_proxy_client_get_properties (WpProxyClient * self)
{
  return wp_properties_new_wrap_dict (self->info->props);
}

void
wp_proxy_client_update_permissions (WpProxyClient * self,
    guint n_perm, ...)
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

  wp_proxy_client_update_permissions_array (self, n_perm, perm);
}

void
wp_proxy_client_update_permissions_array (WpProxyClient * self,
    guint n_perm, const struct pw_permission *permissions)
{
  struct pw_client_proxy *pwp;
  int client_update_permissions_result;

  g_return_if_fail (WP_IS_PROXY_CLIENT (self));

  pwp = (struct pw_client_proxy *) wp_proxy_get_pw_proxy (WP_PROXY (self));
  g_return_if_fail (pwp != NULL);

  client_update_permissions_result = pw_client_proxy_update_permissions (
      pwp, n_perm, permissions);
  g_warn_if_fail (client_update_permissions_result >= 0);
}
