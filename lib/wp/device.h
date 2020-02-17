/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_DEVICE_H__
#define __WIREPLUMBER_DEVICE_H__

#include "proxy.h"

G_BEGIN_DECLS

/* WpDevice */

/**
 * WP_TYPE_DEVICE:
 *
 * The #WpDevice #GType
 */
#define WP_TYPE_DEVICE (wp_device_get_type ())
WP_API
G_DECLARE_FINAL_TYPE (WpDevice, wp_device, WP, DEVICE, WpProxy)

WP_API
WpDevice * wp_device_new_from_factory (WpCore * core,
    const gchar * factory_name, WpProperties * properties);

/* WpSpaDevice */

/**
 * WpSpaDeviceFeatures:
 * @WP_SPA_DEVICE_FEATURE_ACTIVE: activates the device, making it query the
 *    hardware and emit the #WpSpaDevice::object-info signal
 *
 * An extension of #WpProxyFeatures
 */
typedef enum { /*< flags >*/
  WP_SPA_DEVICE_FEATURE_ACTIVE = WP_PROXY_FEATURE_LAST,
} WpSpaDeviceFeatures;

/**
 * WP_TYPE_SPA_DEVICE:
 *
 * The #WpSpaDevice #GType
 */
#define WP_TYPE_SPA_DEVICE (wp_spa_device_get_type ())
WP_API
G_DECLARE_FINAL_TYPE (WpSpaDevice, wp_spa_device, WP, SPA_DEVICE, WpProxy)

WP_API
WpSpaDevice * wp_spa_device_new_from_spa_factory (WpCore * core,
    const gchar * factory_name, WpProperties * properties);

G_END_DECLS

#endif
