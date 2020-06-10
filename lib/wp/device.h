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
 * WP_TYPE_SPA_DEVICE:
 *
 * The #WpSpaDevice #GType
 */
#define WP_TYPE_SPA_DEVICE (wp_spa_device_get_type ())
WP_API
G_DECLARE_FINAL_TYPE (WpSpaDevice, wp_spa_device, WP, SPA_DEVICE, GObject)

WP_API
WpSpaDevice * wp_spa_device_new_wrap (WpCore * core,
    gpointer spa_device_handle);

WP_API
WpSpaDevice * wp_spa_device_new_from_spa_factory (WpCore * core,
    const gchar * factory_name, WpProperties * properties);

WP_API
guint32 wp_spa_device_get_bound_id (WpSpaDevice * self);

WP_API
void wp_spa_device_export (WpSpaDevice * self, GCancellable * cancellable,
    GAsyncReadyCallback callback, gpointer user_data);

WP_API
gboolean wp_spa_device_export_finish (WpSpaDevice * self, GAsyncResult * res,
    GError ** error);

WP_API
void wp_spa_device_activate (WpSpaDevice * self);

G_END_DECLS

#endif
