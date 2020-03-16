/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_MONITOR_DBUS_DEVICE_RESERVATION_H__
#define __WIREPLUMBER_MONITOR_DBUS_DEVICE_RESERVATION_H__

#include <wp/wp.h>

G_BEGIN_DECLS

#define WP_TYPE_MONITOR_DBUS_DEVICE_RESERVATION (wp_monitor_dbus_device_reservation_get_type ())
G_DECLARE_FINAL_TYPE (WpMonitorDbusDeviceReservation,
    wp_monitor_dbus_device_reservation, WP, MONITOR_DBUS_DEVICE_RESERVATION,
    GObject)

WpMonitorDbusDeviceReservation * wp_monitor_dbus_device_reservation_new (
    gint card_id, const char *application_name, gint priority,
    const char *app_dev_name);

void wp_monitor_dbus_device_reservation_release (
    WpMonitorDbusDeviceReservation *self);

void wp_monitor_dbus_device_reservation_complete_release (
    WpMonitorDbusDeviceReservation *self, gboolean res);

gboolean wp_monitor_dbus_device_reservation_acquire (
    WpMonitorDbusDeviceReservation *self, GCancellable *cancellable,
    GAsyncReadyCallback callback, gpointer user_data);

gboolean wp_monitor_dbus_device_reservation_request_release (
    WpMonitorDbusDeviceReservation *self, GCancellable *cancellable,
    GAsyncReadyCallback callback, gpointer user_data);

gboolean wp_monitor_dbus_device_reservation_request_property (
    WpMonitorDbusDeviceReservation *self, const char *name,
    GCancellable *cancellable, GAsyncReadyCallback callback,
    gpointer user_data);

gpointer wp_monitor_dbus_device_reservation_async_finish (
    WpMonitorDbusDeviceReservation *self, GAsyncResult * res, GError ** error);

G_END_DECLS

#endif
