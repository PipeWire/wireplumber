/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_PROXY_LINK_H__
#define __WIREPLUMBER_PROXY_LINK_H__

#include "core.h"
#include "proxy.h"

G_BEGIN_DECLS

#define WP_TYPE_PROXY_LINK (wp_proxy_link_get_type ())
G_DECLARE_FINAL_TYPE (WpProxyLink, wp_proxy_link, WP, PROXY_LINK, WpProxy)

void wp_proxy_link_new (guint global_id, gpointer proxy,
    GAsyncReadyCallback callback, gpointer user_data);
WpProxyLink *wp_proxy_link_new_finish(GObject *initable, GAsyncResult *res,
    GError **error);

const struct pw_link_info *wp_proxy_link_get_info (WpProxyLink * self);

G_END_DECLS

#endif
