/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_PROXY_CLIENT_H__
#define __WIREPLUMBER_PROXY_CLIENT_H__

#include "proxy.h"

G_BEGIN_DECLS

struct pw_permission;

#define WP_TYPE_PROXY_CLIENT (wp_proxy_client_get_type ())
WP_API
G_DECLARE_FINAL_TYPE (WpProxyClient, wp_proxy_client, WP, PROXY_CLIENT, WpProxy)

WP_API
void wp_proxy_client_update_permissions (WpProxyClient * self,
    guint n_perm, ...);

WP_API
void wp_proxy_client_update_permissions_array (WpProxyClient * self,
    guint n_perm, const struct pw_permission *permissions);

G_END_DECLS

#endif
