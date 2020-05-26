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

G_DEFINE_QUARK (wp-reserve-device-jack-n-acquired, jack_n_acquired);

struct _WpReserveDevice
{
  GObject parent;

  /* Props */
  GWeakRef device;
  WpDbusDeviceReservation *reservation;

  /* JACK */
  WpObjectManager *jack_device_om;

  guint n_acquired;
};

enum {
  DEVICE_PROP_0,
  DEVICE_PROP_DEVICE,
  DEVICE_PROP_RESERVATION,
};

G_DEFINE_TYPE (WpReserveDevice, wp_reserve_device, G_TYPE_OBJECT)

static void
set_device_profile (WpProxy *device, gint index)
{
  g_return_if_fail (device);
  g_autoptr (WpSpaPod) profile = wp_spa_pod_new_object (
      "Profile", "Profile",
      "index", "i", index,
      NULL);
  wp_proxy_set_param (device, "Profile", profile);
}

static gint
decrement_jack_n_acquired (WpProxy *device)
{
  gpointer p = g_object_get_qdata (G_OBJECT (device), jack_n_acquired_quark ());
  gint val = p ? GPOINTER_TO_INT (p) : 0;
  g_object_set_qdata_full (G_OBJECT (device), jack_n_acquired_quark (),
        GINT_TO_POINTER (val--), NULL);
  return val;
}

static gint
increment_jack_n_acquired (WpProxy *device)
{
  gpointer p = g_object_get_qdata (G_OBJECT (device), jack_n_acquired_quark ());
  gint val = p ? GPOINTER_TO_INT (p) : 0;
  g_object_set_qdata_full (G_OBJECT (device), jack_n_acquired_quark (),
        GINT_TO_POINTER (val++), NULL);
  return val;
}

static void
on_device_done (WpCore *core, GAsyncResult *res, WpReserveDevice *self)
{
  if (self->reservation)
    wp_dbus_device_reservation_complete_release (self->reservation, TRUE);
  else
    wp_warning_object (self, "release not completed");
}

static void
on_application_name_done (GObject *obj, GAsyncResult *res, gpointer user_data)
{
  WpReserveDevice *self = user_data;
  g_autoptr (GError) e = NULL;
  g_autofree gchar *name = NULL;
  g_autoptr (WpProxy) jack_device = NULL;

  /* Note that the ApplicationName property is optional as described in the
   * specification (http://git.0pointer.net/reserve.git/tree/reserve.txt), so
   * some audio servers can return NULL, and this is not an error */
  name = wp_dbus_device_reservation_async_finish (self->reservation, res, &e);
  if (e) {
    wp_warning_object (self, "could not get application name: %s", e->message);
    return;
  }

  wp_info_object (self, "owner: %s", name ? name : "unknown");

  /* Only enable the JACK device if the owner is the JACK audio server */
  if (!name || g_strcmp0 (name, "Jack audio server") != 0)
    return;

  /* Get the JACK device and increment the jack acquisition. We only enable the
   * JACK device if this is the first acquisition */
  jack_device = wp_object_manager_lookup (self->jack_device_om, WP_TYPE_DEVICE,
      NULL);
  if (jack_device && increment_jack_n_acquired (jack_device) == 1)
    set_device_profile (jack_device, 1);
}

static void
on_reservation_acquired (GObject *obj, GAsyncResult *res, gpointer user_data)
{
  WpReserveDevice *self = user_data;
  g_autoptr (GError) e = NULL;
  g_autoptr (WpProxy) jack_device = NULL;
  g_autoptr (WpProxy) device = NULL;

  /* If the audio device could not be acquired, check who owns it and maybe
   * enable the JACK device */
  if (!wp_dbus_device_reservation_async_finish (self->reservation, res, &e)) {
    wp_info_object (self, "could not own device: %s", e->message);
    wp_dbus_device_reservation_request_property (self->reservation,
        "ApplicationName", NULL, on_application_name_done, self);
    return;
  }

  /* Get the JACK device and decrement the jack acquisition. We only disable the
   * JACK device if there is no acquisitions */
  jack_device = wp_object_manager_lookup (self->jack_device_om, WP_TYPE_DEVICE,
      NULL);
  if (jack_device && decrement_jack_n_acquired (jack_device) == 0)
    set_device_profile (jack_device, 0);

  /* Enable Audio device */
  device = g_weak_ref_get (&self->device);
  if (device)
    set_device_profile (device, 1);
}

static void
on_reservation_release (WpDbusDeviceReservation *reservation, gboolean forced,
    WpReserveDevice *self)
{
  g_autoptr (WpProxy) device = NULL;
  g_autoptr (WpCore) core = NULL;

  /* Release reservation */
  wp_dbus_device_reservation_release (reservation);

  /* Get the device and core */
  device = g_weak_ref_get (&self->device);
  if (!device)
    return;
  core = wp_proxy_get_core (device);
  if (!core)
    return;

  /* Disable device */
  set_device_profile (device, 0);

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
  g_autoptr (WpProxy) device = NULL;
  g_autoptr (WpCore) core = NULL;

  device = g_weak_ref_get (&self->device);
  g_return_if_fail (device);
  core = wp_proxy_get_core (device);
  g_return_if_fail (core);

  /* Create the JACK device object manager */
  self->jack_device_om = wp_object_manager_new ();
  wp_object_manager_add_interest (self->jack_device_om, WP_TYPE_DEVICE,
      WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY, PW_KEY_DEVICE_API, "=s", "jack",
      NULL);
  wp_object_manager_request_proxy_features (self->jack_device_om,
      WP_TYPE_DEVICE, WP_PROXY_FEATURES_STANDARD);
  wp_core_install_object_manager (core, self->jack_device_om);

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

  /* JACK */
  g_clear_object (&self->jack_device_om);

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
