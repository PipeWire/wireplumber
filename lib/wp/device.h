/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_DEVICE_H__
#define __WIREPLUMBER_DEVICE_H__

#include "global-proxy.h"

G_BEGIN_DECLS

/* WpDevice */

/*!
 * @memberof WpDevice
 *
 * @brief The [WpDevice](@ref device_section)
 * <a href="https://developer.gnome.org/gobject/stable/gobject-Type-Information.html#GType">
 * GType</a>
 *
 * @code
 * #define WP_TYPE_DEVICE (wp_device_get_type ())
 * @endcode
 *
 */
#define WP_TYPE_DEVICE (wp_device_get_type ())
WP_API
G_DECLARE_FINAL_TYPE (WpDevice, wp_device, WP, DEVICE, WpGlobalProxy)

WP_API
WpDevice * wp_device_new_from_factory (WpCore * core,
    const gchar * factory_name, WpProperties * properties);

/* WpSpaDevice */

/*!
 * @memberof WpDevice
 *
 * @brief
 * @arg WP_SPA_DEVICE_FEATURE_ENABLED: enables a device
 *
 * Flags to be used as [WpObjectFeatures](@ref object_features_section) for
 * [WpSpaDevice](@ref spa_device_section)
 */
typedef enum { /*< flags >*/
  WP_SPA_DEVICE_FEATURE_ENABLED = (WP_PROXY_FEATURE_CUSTOM_START << 0),
} WpSpaDeviceFeatures;

/*!
 * @memberof WpDevice
 *
 * @brief The [WpSpaDevice](@ref spa_device_section)
 * <a href="https://developer.gnome.org/gobject/stable/gobject-Type-Information.html#GType">
 * GType</a>
 *
 * @code
 * #define WP_TYPE_SPA_DEVICE (wp_spa_device_get_type ())
 * @endcode
 *
 */
#define WP_TYPE_SPA_DEVICE (wp_spa_device_get_type ())
WP_API
G_DECLARE_FINAL_TYPE (WpSpaDevice, wp_spa_device, WP, SPA_DEVICE, WpProxy)

WP_API
WpSpaDevice * wp_spa_device_new_wrap (WpCore * core,
    gpointer spa_device_handle, WpProperties * properties);

WP_API
WpSpaDevice * wp_spa_device_new_from_spa_factory (WpCore * core,
    const gchar * factory_name, WpProperties * properties);

WP_API
WpProperties * wp_spa_device_get_properties (WpSpaDevice * self);

WP_API
GObject * wp_spa_device_get_managed_object (WpSpaDevice * self, guint id);

WP_API
void wp_spa_device_store_managed_object (WpSpaDevice * self, guint id,
    GObject * object);

G_END_DECLS

#endif
