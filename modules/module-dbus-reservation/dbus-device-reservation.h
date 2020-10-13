/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_DBUS_DEVICE_RESERVATION_H__
#define __WIREPLUMBER_DBUS_DEVICE_RESERVATION_H__

#include <wp/wp.h>

G_BEGIN_DECLS

#define JACK_APPLICATION_NAME "Jack audio server"
#define PULSEAUDIO_APPLICATION_NAME "PulseAudio Sound Server"
#define PIPEWIRE_APPLICATION_NAME "PipeWire"

#define WP_TYPE_DBUS_DEVICE_RESERVATION (wp_dbus_device_reservation_get_type ())
G_DECLARE_FINAL_TYPE (WpDbusDeviceReservation, wp_dbus_device_reservation, WP,
    DBUS_DEVICE_RESERVATION, GObject)

WpDbusDeviceReservation * wp_dbus_device_reservation_new (
    gint card_id, const char *application_name, gint priority,
    const char *app_dev_name);

void wp_dbus_device_reservation_release (WpDbusDeviceReservation *self);

void wp_dbus_device_reservation_complete_release (WpDbusDeviceReservation *self,
    gboolean res);

gboolean wp_dbus_device_reservation_acquire (WpDbusDeviceReservation *self);

gboolean wp_dbus_device_reservation_request_release (
    WpDbusDeviceReservation *self, GCancellable *cancellable,
    GAsyncReadyCallback callback, gpointer user_data);

gboolean wp_dbus_device_reservation_request_property (
    WpDbusDeviceReservation *self, const char *name, GCancellable *cancellable,
    GAsyncReadyCallback callback, gpointer user_data);

gpointer wp_dbus_device_reservation_async_finish (WpDbusDeviceReservation *self,
    GAsyncResult * res, GError ** error);

G_END_DECLS

#endif
