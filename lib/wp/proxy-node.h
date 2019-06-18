/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_PROXY_NODE_H__
#define __WIREPLUMBER_PROXY_NODE_H__

#include "core.h"
#include "proxy.h"

G_BEGIN_DECLS

#define WP_TYPE_PROXY_NODE (wp_proxy_node_get_type ())
G_DECLARE_FINAL_TYPE (WpProxyNode, wp_proxy_node, WP, PROXY_NODE, WpProxy)

void wp_proxy_node_new (gpointer proxy, GAsyncReadyCallback callback,
    gpointer user_data);
WpProxyNode *wp_proxy_node_new_finish(GObject *initable, GAsyncResult *res,
    GError **error);

const struct pw_node_info *wp_proxy_node_get_info (WpProxyNode * self);

G_END_DECLS

#endif
