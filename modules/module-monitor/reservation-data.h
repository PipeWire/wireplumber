/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_MONITOR_RESERVATION_DATA_H__
#define __WIREPLUMBER_MONITOR_RESERVATION_DATA_H__

#include <wp/wp.h>

#include "dbus-device-reservation.h"

G_BEGIN_DECLS

#define WP_TYPE_MONITOR_DEVICE_RESERVATION_DATA (wp_monitor_device_reservation_data_get_type ())

G_DECLARE_FINAL_TYPE (WpMonitorDeviceReservationData,
    wp_monitor_device_reservation_data, WP, MONITOR_DEVICE_RESERVATION_DATA,
    GObject)

WpMonitorDeviceReservationData * wp_monitor_device_reservation_data_new (
    WpProxy *device, WpMonitorDbusDeviceReservation *reservation);

void
wp_monitor_device_reservation_data_acquire (
    WpMonitorDeviceReservationData *self);

void
wp_monitor_device_reservation_data_release (
    WpMonitorDeviceReservationData *self);


#define WP_TYPE_MONITOR_NODE_RESERVATION_DATA (wp_monitor_node_reservation_data_get_type ())

G_DECLARE_FINAL_TYPE (WpMonitorNodeReservationData,
    wp_monitor_node_reservation_data, WP, MONITOR_NODE_RESERVATION_DATA,
    GObject)

WpMonitorNodeReservationData * wp_monitor_node_reservation_data_new (
    WpProxy *node, WpMonitorDeviceReservationData *device_data);

void
wp_monitor_node_reservation_data_timeout_release (
    WpMonitorNodeReservationData *self, guint64 timeout_ms);

void
wp_monitor_node_reservation_data_acquire (WpMonitorNodeReservationData *self);

G_END_DECLS

#endif
