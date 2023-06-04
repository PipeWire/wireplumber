/* WirePlumber
 *
 * Copyright © 2023 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_DBUS_CONNECTION_STATE_H__
#define __WIREPLUMBER_DBUS_CONNECTION_STATE_H__

#include <glib-object.h>

G_BEGIN_DECLS

typedef enum {
  WP_DBUS_CONNECTION_STATE_CLOSED = 0,
  WP_DBUS_CONNECTION_STATE_CONNECTING,
  WP_DBUS_CONNECTION_STATE_CONNECTED,
} WpDBusConnectionState;

G_END_DECLS

#endif
