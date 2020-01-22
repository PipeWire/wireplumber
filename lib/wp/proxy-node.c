/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "proxy-node.h"
#include "private.h"

#include <pipewire/pipewire.h>

struct _WpProxyNode
{
  WpProxy parent;
  struct pw_node_info *info;

  /* The node proxy listener */
  struct spa_hook listener;
};

G_DEFINE_TYPE (WpProxyNode, wp_proxy_node, WP_TYPE_PROXY)

static void
wp_proxy_node_init (WpProxyNode * self)
{
}

static void
wp_proxy_node_finalize (GObject * object)
{
  WpProxyNode *self = WP_PROXY_NODE (object);

  g_clear_pointer (&self->info, pw_node_info_free);

  G_OBJECT_CLASS (wp_proxy_node_parent_class)->finalize (object);
}

static gconstpointer
wp_proxy_node_get_info (WpProxy * self)
{
  return WP_PROXY_NODE (self)->info;
}

static WpProperties *
wp_proxy_node_get_properties (WpProxy * self)
{
  return wp_properties_new_wrap_dict (WP_PROXY_NODE (self)->info->props);
}

static gint
wp_proxy_node_enum_params (WpProxy * self, guint32 id, guint32 start,
    guint32 num, const struct spa_pod *filter)
{
  struct pw_node *pwp;
  int node_enum_params_result;

  pwp = (struct pw_node *) wp_proxy_get_pw_proxy (self);
  node_enum_params_result = pw_node_enum_params (pwp, 0, id, start, num, filter);
  g_warn_if_fail (node_enum_params_result >= 0);

  return node_enum_params_result;
}

static gint
wp_proxy_node_subscribe_params (WpProxy * self, guint32 n_ids, guint32 *ids)
{
  struct pw_node *pwp;
  int node_subscribe_params_result;

  pwp = (struct pw_node *) wp_proxy_get_pw_proxy (self);
  node_subscribe_params_result = pw_node_subscribe_params (pwp, ids, n_ids);
  g_warn_if_fail (node_subscribe_params_result >= 0);

  return node_subscribe_params_result;
}

static gint
wp_proxy_node_set_param (WpProxy * self, guint32 id, guint32 flags,
    const struct spa_pod *param)
{
  struct pw_node *pwp;
  int node_set_param_result;

  pwp = (struct pw_node *) wp_proxy_get_pw_proxy (self);
  node_set_param_result = pw_node_set_param (pwp, id, flags, param);
  g_warn_if_fail (node_set_param_result >= 0);

  return node_set_param_result;
}

static void
node_event_info(void *data, const struct pw_node_info *info)
{
  WpProxyNode *self = WP_PROXY_NODE (data);

  self->info = pw_node_info_update (self->info, info);
  g_object_notify (G_OBJECT (self), "info");

  if (info->change_mask & PW_NODE_CHANGE_MASK_PROPS)
    g_object_notify (G_OBJECT (self), "properties");

  wp_proxy_set_feature_ready (WP_PROXY (self), WP_PROXY_FEATURE_INFO);
}

static const struct pw_node_events node_events = {
  PW_VERSION_NODE_EVENTS,
  .info = node_event_info,
  .param = wp_proxy_handle_event_param,
};

static void
wp_proxy_node_pw_proxy_created (WpProxy * proxy, struct pw_proxy * pw_proxy)
{
  WpProxyNode *self = WP_PROXY_NODE (proxy);
  pw_node_add_listener ((struct pw_node *) pw_proxy,
      &self->listener, &node_events, self);
}

static void
wp_proxy_node_class_init (WpProxyNodeClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpProxyClass *proxy_class = (WpProxyClass *) klass;

  object_class->finalize = wp_proxy_node_finalize;

  proxy_class->get_info = wp_proxy_node_get_info;
  proxy_class->get_properties = wp_proxy_node_get_properties;
  proxy_class->enum_params = wp_proxy_node_enum_params;
  proxy_class->subscribe_params = wp_proxy_node_subscribe_params;
  proxy_class->set_param = wp_proxy_node_set_param;

  proxy_class->pw_proxy_created = wp_proxy_node_pw_proxy_created;
}
