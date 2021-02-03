/* WirePlumber
 *
 * Copyright © 2021 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_PORTAL_PERMISSIONSTORE_PLUGIN_H__
#define __WIREPLUMBER_PORTAL_PERMISSIONSTORE_PLUGIN_H__

#include <wp/wp.h>

G_BEGIN_DECLS

typedef enum {
  WP_DBUS_CONNECTION_STATUS_CLOSED = 0,
  WP_DBUS_CONNECTION_STATUS_CONNECTING,
  WP_DBUS_CONNECTION_STATUS_CONNECTED,
} WpDBusConnectionStatus;

G_DECLARE_FINAL_TYPE (WpPortalPermissionStorePlugin,
    wp_portal_permissionstore_plugin, WP, PORTAL_PERMISSIONSTORE_PLUGIN,
    WpPlugin)

struct _WpPortalPermissionStorePlugin
{
  WpPlugin parent;

  WpDBusConnectionStatus state;
  guint signal_id;

  GCancellable *cancellable;
  GDBusConnection *connection;
};

G_END_DECLS

#endif
