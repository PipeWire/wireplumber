/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_PROXY_DEVICE_H__
#define __WIREPLUMBER_PROXY_DEVICE_H__

#include "proxy.h"

G_BEGIN_DECLS

#define WP_TYPE_PROXY_DEVICE (wp_proxy_device_get_type ())
WP_API
G_DECLARE_FINAL_TYPE (WpProxyDevice, wp_proxy_device, WP, PROXY_DEVICE, WpProxy)

WP_API
WpProperties * wp_proxy_device_get_properties (WpProxyDevice * self);

G_END_DECLS

#endif
