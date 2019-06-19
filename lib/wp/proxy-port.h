/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_PROXY_PORT_H__
#define __WIREPLUMBER_PROXY_PORT_H__

#include "core.h"
#include "proxy.h"

G_BEGIN_DECLS

#define WP_TYPE_PROXY_PORT (wp_proxy_port_get_type ())
G_DECLARE_FINAL_TYPE (WpProxyPort, wp_proxy_port, WP, PROXY_PORT, WpProxy)

void wp_proxy_port_new (guint global_id, gpointer proxy,
    GAsyncReadyCallback callback, gpointer user_data);
WpProxyPort *wp_proxy_port_new_finish(GObject *initable, GAsyncResult *res,
    GError **error);

const struct pw_port_info *wp_proxy_port_get_info (WpProxyPort * self);
const struct spa_audio_info_raw *wp_proxy_port_get_format (WpProxyPort * self);

G_END_DECLS

#endif
