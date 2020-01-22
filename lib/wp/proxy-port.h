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

#define WP_TYPE_PROXY_PORT (wp_proxy_port_get_type ())
WP_API
G_DECLARE_FINAL_TYPE (WpProxyPort, wp_proxy_port, WP, PROXY_PORT, WpProxy)

G_END_DECLS

#endif
