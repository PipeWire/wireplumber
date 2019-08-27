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
G_DECLARE_FINAL_TYPE (WpProxyNode, wp_proxy_node, WP, PROXY_NODE, WpProxy)

const struct pw_node_info * wp_proxy_node_get_info (WpProxyNode * self);

WpProperties * wp_proxy_node_get_properties (WpProxyNode * self);

void wp_proxy_node_enum_params_collect (WpProxyNode * self,
    guint32 id, const struct spa_pod *filter,
    GCancellable * cancellable, GAsyncReadyCallback callback,
    gpointer user_data);
GPtrArray * wp_proxy_node_enum_params_collect_finish (WpProxyNode * self,
    GAsyncResult * res, GError ** error);
gint wp_proxy_node_enum_params (WpProxyNode * self,
    guint32 id, const struct spa_pod *filter);

void wp_proxy_node_subscribe_params (WpProxyNode * self, guint32 n_ids, ...);
void wp_proxy_node_subscribe_params_array (WpProxyNode * self, guint32 n_ids,
    guint32 *ids);

void wp_proxy_node_set_param (WpProxyNode * self, guint32 id,
    guint32 flags, const struct spa_pod *param);

G_END_DECLS

#endif
