/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_RESERVE_DEVICE_H__
#define __WIREPLUMBER_RESERVE_DEVICE_H__

#include <wp/wp.h>

#include "dbus-device-reservation.h"

G_BEGIN_DECLS

#define WP_TYPE_RESERVE_DEVICE (wp_reserve_device_get_type ())

G_DECLARE_FINAL_TYPE (WpReserveDevice, wp_reserve_device, WP, RESERVE_DEVICE,
    GObject)

WpReserveDevice * wp_reserve_device_new (WpProxy *device,
    WpMonitorDbusDeviceReservation *reservation);

void
wp_reserve_device_acquire (WpReserveDevice *self);

void
wp_reserve_device_release (WpReserveDevice *self);

G_END_DECLS

#endif
