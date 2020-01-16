/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_PROXY_PORT_H__
#define __WIREPLUMBER_PROXY_PORT_H__

#include "proxy.h"

G_BEGIN_DECLS

struct spa_pod;
struct pw_port_info;

#define WP_TYPE_PROXY_PORT (wp_proxy_port_get_type ())
WP_API
G_DECLARE_FINAL_TYPE (WpProxyPort, wp_proxy_port, WP, PROXY_PORT, WpProxy)

WP_API
const struct pw_port_info * wp_proxy_port_get_info (WpProxyPort * self);

WP_API
WpProperties * wp_proxy_port_get_properties (WpProxyPort * self);

WP_API
void wp_proxy_port_enum_params_collect (WpProxyPort * self,
    guint32 id, const struct spa_pod *filter,
    GCancellable * cancellable, GAsyncReadyCallback callback,
    gpointer user_data);

WP_API
GPtrArray * wp_proxy_port_enum_params_collect_finish (WpProxyPort * self,
    GAsyncResult * res, GError ** error);

WP_API
gint wp_proxy_port_enum_params (WpProxyPort * self,
    guint32 id, const struct spa_pod *filter);

WP_API
void wp_proxy_port_subscribe_params (WpProxyPort * self, guint32 n_ids, ...);

WP_API
void wp_proxy_port_subscribe_params_array (WpProxyPort * self, guint32 n_ids,
    guint32 *ids);

G_END_DECLS

#endif
