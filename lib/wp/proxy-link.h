/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_PROXY_LINK_H__
#define __WIREPLUMBER_PROXY_LINK_H__

#include "proxy.h"

G_BEGIN_DECLS

struct pw_link_info;

#define WP_TYPE_PROXY_LINK (wp_proxy_link_get_type ())
WP_API
G_DECLARE_FINAL_TYPE (WpProxyLink, wp_proxy_link, WP, PROXY_LINK, WpProxy)

WP_API
const struct pw_link_info * wp_proxy_link_get_info (WpProxyLink * self);

WP_API
WpProperties * wp_proxy_link_get_properties (WpProxyLink * self);

G_END_DECLS

#endif
