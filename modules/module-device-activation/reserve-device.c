/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "reserve-device.h"

#include <spa/pod/builder.h>
#include <pipewire/pipewire.h>

struct _WpReserveDevice
{
  GObject parent;

  /* Props */
  GWeakRef device;
  WpDbusDeviceReservation *reservation;

  guint n_acquired;
};

enum {
  DEVICE_PROP_0,
  DEVICE_PROP_DEVICE,
  DEVICE_PROP_RESERVATION,
};

G_DEFINE_TYPE (WpReserveDevice, wp_reserve_device, G_TYPE_OBJECT)

static void
on_device_done (WpCore *core, GAsyncResult *res, WpReserveDevice *self)
{
  if (self->reservation)
    wp_dbus_device_reservation_complete_release (self->reservation, TRUE);
  else
    wp_warning_object (self, "release not completed");
}

static void
on_reservation_acquired (GObject *obj, GAsyncResult *res, gpointer user_data)
{
  WpReserveDevice *self = user_data;
  WpDbusDeviceReservation *reserv = WP_DBUS_DEVICE_RESERVATION (obj);
  g_autoptr (GError) error = NULL;
  g_autoptr (WpProxy) device = NULL;
  g_autoptr (WpSpaPod) profile = NULL;

  /* Finish */
  if (!wp_dbus_device_reservation_async_finish (reserv, res, &error)) {
    g_warning ("%s", error->message);
    return;
  }

  /* Get the device */
  device = g_weak_ref_get (&self->device);
  if (!device)
    return;

  /* Set profile 1 */
  profile = wp_spa_pod_new_object (
      "Profile", "Profile",
      "index", "i", 1,
      NULL);
  wp_proxy_set_param (device, "Profile", profile);
}

static void
on_reservation_release (WpDbusDeviceReservation *reservation, gboolean forced,
    WpReserveDevice *self)
{
  g_autoptr (WpProxy) device = NULL;
  g_autoptr (WpCore) core = NULL;
  g_autoptr (WpSpaPod) profile = NULL;

  /* Get the device and core */
  device = g_weak_ref_get (&self->device);
  if (!device)
    return;
  core = wp_proxy_get_core (device);
  if (!core)
    return;

  /* Set profile 0 */
  profile = wp_spa_pod_new_object (
      "Profile", "Profile",
      "index", "i", 0,
      NULL);
  wp_proxy_set_param (device, "Profile", profile);

  /* Only complete the release if not forced */
  if (!forced)
    wp_core_sync (core, NULL, (GAsyncReadyCallback)on_device_done, self);
}

static void
on_device_destroyed (WpProxy *device, WpReserveDevice *self)
{
  wp_dbus_device_reservation_release (self->reservation);
}

static void
wp_reserve_device_constructed (GObject * object)
{
  WpReserveDevice *self = WP_RESERVE_DEVICE (object);
  g_autoptr (WpProxy) device = g_weak_ref_get (&self->device);

  /* Make sure the device is released when the pw proxy device is destroyed */
  g_return_if_fail (device);
  g_signal_connect_object (device, "pw-proxy-destroyed",
      (GCallback) on_device_destroyed, self, 0);

  /* Handle the reservation signals */
  g_return_if_fail (self->reservation);
  g_signal_connect_object (self->reservation, "release",
    (GCallback) on_reservation_release, self, 0);

  /* Try to acquire the device */
  wp_dbus_device_reservation_acquire (self->reservation, NULL,
     on_reservation_acquired, self);

  G_OBJECT_CLASS (wp_reserve_device_parent_class)->constructed (object);
}

static void
wp_reserve_device_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec)
{
  WpReserveDevice *self = WP_RESERVE_DEVICE (object);

  switch (property_id) {
  case DEVICE_PROP_DEVICE:
    g_value_take_object (value, g_weak_ref_get (&self->device));
    break;
  case DEVICE_PROP_RESERVATION:
    g_value_set_object (value, self->reservation);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_reserve_device_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec)
{
  WpReserveDevice *self = WP_RESERVE_DEVICE (object);

  switch (property_id) {
  case DEVICE_PROP_DEVICE:
    g_weak_ref_set (&self->device, g_value_get_object (value));
    break;
  case DEVICE_PROP_RESERVATION:
    self->reservation = g_value_dup_object (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_reserve_device_finalize (GObject * object)
{
  WpReserveDevice *self = WP_RESERVE_DEVICE (object);

  wp_dbus_device_reservation_release (self->reservation);

  /* Props */
  g_weak_ref_clear (&self->device);
  g_clear_object (&self->reservation);

  G_OBJECT_CLASS (wp_reserve_device_parent_class)->finalize (object);
}

static void
wp_reserve_device_init (WpReserveDevice * self)
{
  /* Props */
  g_weak_ref_init (&self->device, NULL);

  self->n_acquired = 0;
}

static void
wp_reserve_device_class_init (WpReserveDeviceClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;

  object_class->constructed = wp_reserve_device_constructed;
  object_class->get_property = wp_reserve_device_get_property;
  object_class->set_property = wp_reserve_device_set_property;
  object_class->finalize = wp_reserve_device_finalize;

  /* Props */
  g_object_class_install_property (object_class, DEVICE_PROP_DEVICE,
      g_param_spec_object ("device", "device", "The device", WP_TYPE_PROXY,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class, DEVICE_PROP_RESERVATION,
      g_param_spec_object ("reservation", "reservation",
      "The dbus device reservation", WP_TYPE_DBUS_DEVICE_RESERVATION,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

WpReserveDevice *
wp_reserve_device_new (WpProxy *device, WpDbusDeviceReservation *reservation)
{
  return g_object_new (WP_TYPE_RESERVE_DEVICE,
      "device", device,
      "reservation", reservation,
      NULL);
}

void
wp_reserve_device_acquire (WpReserveDevice *self)
{
  g_return_if_fail (WP_IS_RESERVE_DEVICE (self));
  g_return_if_fail (self->reservation);

  if (self->n_acquired == 0)
    wp_dbus_device_reservation_acquire (self->reservation, NULL,
        on_reservation_acquired, self);

  self->n_acquired++;
}

void
wp_reserve_device_release (WpReserveDevice *self)
{
  g_return_if_fail (WP_IS_RESERVE_DEVICE (self));
  g_return_if_fail (self->reservation);

  if (self->n_acquired == 1)
    wp_dbus_device_reservation_release (self->reservation);

  self->n_acquired--;
}
