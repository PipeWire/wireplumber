/* WirePlumber
 *
 * Copyright © 2021 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_RESERVE_DEVICE_PLUGIN_H__
#define __WIREPLUMBER_RESERVE_DEVICE_PLUGIN_H__

#include <wp/wp.h>

G_BEGIN_DECLS

#define WP_LOCAL_LOG_TOPIC log_topic_rd
WP_LOG_TOPIC_EXTERN (log_topic_rd)

#define FDO_RESERVE_DEVICE1_SERVICE "org.freedesktop.ReserveDevice1"
#define FDO_RESERVE_DEVICE1_PATH "/org/freedesktop/ReserveDevice1"

typedef enum {
  WP_DBUS_CONNECTION_STATE_CLOSED = 0,
  WP_DBUS_CONNECTION_STATE_CONNECTING,
  WP_DBUS_CONNECTION_STATE_CONNECTED,
} WpDBusConnectionState;

G_DECLARE_FINAL_TYPE (WpReserveDevicePlugin, wp_reserve_device_plugin,
    WP, RESERVE_DEVICE_PLUGIN, WpPlugin)

struct _WpReserveDevicePlugin
{
  WpPlugin parent;

  WpDbus *dbus;
  GHashTable *reserve_devices;
  GDBusObjectManagerServer *manager;
};

G_END_DECLS

#endif
