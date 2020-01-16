/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_PROXY_NODE_H__
#define __WIREPLUMBER_PROXY_NODE_H__

#include "proxy.h"

G_BEGIN_DECLS

struct spa_pod;
struct pw_node_info;

#define WP_TYPE_PROXY_NODE (wp_proxy_node_get_type ())
WP_API
G_DECLARE_FINAL_TYPE (WpProxyNode, wp_proxy_node, WP, PROXY_NODE, WpProxy)

WP_API
const struct pw_node_info * wp_proxy_node_get_info (WpProxyNode * self);

WP_API
WpProperties * wp_proxy_node_get_properties (WpProxyNode * self);

WP_API
void wp_proxy_node_enum_params_collect (WpProxyNode * self,
    guint32 id, const struct spa_pod *filter,
    GCancellable * cancellable, GAsyncReadyCallback callback,
    gpointer user_data);

WP_API
GPtrArray * wp_proxy_node_enum_params_collect_finish (WpProxyNode * self,
    GAsyncResult * res, GError ** error);

WP_API
gint wp_proxy_node_enum_params (WpProxyNode * self,
    guint32 id, const struct spa_pod *filter);

WP_API
void wp_proxy_node_subscribe_params (WpProxyNode * self, guint32 n_ids, ...);

WP_API
void wp_proxy_node_subscribe_params_array (WpProxyNode * self, guint32 n_ids,
    guint32 *ids);

WP_API
void wp_proxy_node_set_param (WpProxyNode * self, guint32 id,
    guint32 flags, const struct spa_pod *param);

G_END_DECLS

#endif
