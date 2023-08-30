/* WirePlumber
 *
 * Copyright © 2022 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_DBUS_H__
#define __WIREPLUMBER_DBUS_H__

#include "object.h"

G_BEGIN_DECLS

/* WpDbus */

/*!
 * \brief Flags to be used as WpObjectFeatures for WpDbus.
 * \ingroup wpdbus
 *
 * \since 0.4.11
 */
typedef enum { /*< flags >*/
  /* main features */
  WP_DBUS_FEATURE_ENABLED = (1 << 0),
} WpDbusFeatures;

/*!
 * \brief The state of the dbus connection
 * \ingroup wpdbus
 *
 * \since 0.4.11
 */
typedef enum {
  WP_DBUS_STATE_CLOSED = 0,
  WP_DBUS_STATE_CONNECTING,
  WP_DBUS_STATE_CONNECTED,
} WpDBusState;

/*!
 * \brief The WpDbus GType
 * \ingroup wpdbus
 */
#define WP_TYPE_DBUS (wp_dbus_get_type ())
WP_API
G_DECLARE_FINAL_TYPE (WpDbus, wp_dbus, WP, DBUS, WpObject)

WP_API
WpDbus *wp_dbus_get_instance (WpCore *core, GBusType bus_type);

WP_API
GBusType wp_dbus_get_bus_type (WpDbus *self);

WP_API
WpDBusState wp_dbus_get_state (WpDbus *self);

WP_API
GDBusConnection *wp_dbus_get_connection (WpDbus *self);

G_END_DECLS

#endif
