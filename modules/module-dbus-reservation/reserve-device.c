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
  GWeakRef core;
  WpDbusDeviceReservation *reservation;
  GClosure *manager_closure;

  /* JACK */
  WpObjectManager *jack_device_om;

  GSource *timeout_source;
};

enum {
  PROP_0,
  PROP_CORE,
  PROP_RESERVATION,
  PROP_MANAGER_CLOSURE,
};

G_DEFINE_TYPE (WpReserveDevice, wp_reserve_device, G_TYPE_OBJECT)

static void
set_device_profile (WpPipewireObject *device, gint index)
{
  g_return_if_fail (device);
  g_autoptr (WpSpaPod) profile = wp_spa_pod_new_object (
      "Spa:Pod:Object:Param:Profile", "Profile",
      "index", "i", index,
      NULL);
  wp_debug_object (device, "set profile %d", index);
  wp_pipewire_object_set_param (device, "Profile", 0, profile);
}

static gint
decrement_jack_n_acquired (WpPipewireObject *device)
{
  gpointer p = g_object_get_qdata (G_OBJECT (device), jack_n_acquired_quark ());
  gint val = GPOINTER_TO_INT (p);
  if (val == 0)
    return -1;
  g_object_set_qdata (G_OBJECT (device), jack_n_acquired_quark (),
      GINT_TO_POINTER (--val));
  return val;
}

static gint
increment_jack_n_acquired (WpPipewireObject *device)
{
  gpointer p = g_object_get_qdata (G_OBJECT (device), jack_n_acquired_quark ());
  gint val = GPOINTER_TO_INT (p);
  g_object_set_qdata (G_OBJECT (device), jack_n_acquired_quark (),
      GINT_TO_POINTER (++val));
  return val;
}

static void
enable_jack_device (WpReserveDevice *self) {
  g_autoptr (WpPipewireObject) jack_device = NULL;
  g_return_if_fail (self);

  /* Get the JACK device and increment the jack acquisition. We only enable the
   * JACK device if this is the first acquisition */
  jack_device = wp_object_manager_lookup (self->jack_device_om, WP_TYPE_DEVICE,
      NULL);
  if (jack_device && increment_jack_n_acquired (jack_device) == 1) {
    set_device_profile (jack_device, 1);
    wp_info_object (self, "jack device enabled");
  }
}

static void
disable_jack_device (WpReserveDevice *self) {
  g_autoptr (WpPipewireObject) jack_device = NULL;
  g_return_if_fail (self);

  /* Get the JACK device and decrement the jack acquisition. We only disable the
   * JACK device if there is no more acquisitions */
  jack_device = wp_object_manager_lookup (self->jack_device_om, WP_TYPE_DEVICE,
      NULL);
  if (jack_device && decrement_jack_n_acquired (jack_device) == 0) {
    set_device_profile (jack_device, 0);
    wp_info_object (self, "jack device disabled");
  }
}

static void
invoke_manager_closure (WpReserveDevice *self, gboolean create)
{
  GValue values[2] = { G_VALUE_INIT, G_VALUE_INIT };

  g_return_if_fail (self->manager_closure);

  g_value_init (&values[0], G_TYPE_OBJECT);
  g_value_init (&values[1], G_TYPE_BOOLEAN);
  g_value_set_object (&values[0], self);
  g_value_set_boolean (&values[1], create);

  if (G_CLOSURE_NEEDS_MARSHAL (self->manager_closure))
    g_closure_set_marshal (self->manager_closure,
        g_cclosure_marshal_VOID__BOOLEAN);
  g_closure_invoke (self->manager_closure, NULL, 2, values, NULL);

  g_value_unset (&values[0]);
  g_value_unset (&values[1]);
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
on_application_name_appeared (GObject *obj, GAsyncResult *res, gpointer data)
{
  WpReserveDevice *self = data;
  g_autoptr (GError) e = NULL;
  g_autofree gchar *name = NULL;

  /* Note that the ApplicationName property is optional as described in the
   * specification (http://git.0pointer.net/reserve.git/tree/reserve.txt), so
   * some audio servers can return NULL, and this is not an error */
  name = wp_dbus_device_reservation_async_finish (self->reservation, res, &e);
  if (e) {
    wp_warning_object (self, "could not get application name: %s", e->message);
    return;
  }

  wp_info_object (self, "owner appeared: %s", name ? name : "unknown");

  /* If the JACK server owns the audio device, we disable the audio device and
   * enable the JACK device */
  if (name && g_strcmp0 (name, JACK_APPLICATION_NAME) == 0) {
    invoke_manager_closure (self, FALSE);
    enable_jack_device (self);
    return;
  }

  /* If we (PipeWire) own the audio device, we enable the audio device and
   * disable the JACK device */
  else if (name && g_strcmp0 (name, PIPEWIRE_APPLICATION_NAME) == 0) {
    disable_jack_device (self);
    invoke_manager_closure (self, TRUE);
    return;
  }

  /* If another server different to JACK and PipeWire (ie PulseAudio) owns the
   * device, we disable both the audio device and the JACK device */
  else {
    disable_jack_device (self);
    invoke_manager_closure (self, FALSE);
  }
}

static void
on_reservation_owner_appeared (WpDbusDeviceReservation *reservation,
    const gchar *owner, WpReserveDevice *self)
{
  /* Clear the current timeout acquire callback */
  if (self->timeout_source)
      g_source_destroy (self->timeout_source);
  g_clear_pointer (&self->timeout_source, g_source_unref);

  /* Request the application name to know who is the new owner */
  wp_dbus_device_reservation_request_property (self->reservation,
      "ApplicationName", NULL, on_application_name_appeared, self);
}

static gboolean
timeout_acquire_callback (WpReserveDevice *self)
{
  g_return_val_if_fail (self, G_SOURCE_REMOVE);
  wp_dbus_device_reservation_acquire (self->reservation);
  return G_SOURCE_REMOVE;
}

static void
on_reservation_owner_vanished (WpDbusDeviceReservation *reservation,
    WpReserveDevice *self)
{
  g_autoptr (WpCore) core = g_weak_ref_get (&self->core);

  wp_info_object (self, "owner vanished");

  /* Always disable JACK device and destroy audio device when owner vanishes.
   * The devices will be enabled/created later when a new owner appears */
  disable_jack_device (self);
  invoke_manager_closure (self, FALSE);

  /* Clear the current timeout acquire callback */
  if (self->timeout_source)
      g_source_destroy (self->timeout_source);
  g_clear_pointer (&self->timeout_source, g_source_unref);

  /* Try to acquire the device if it has no owner for at least 3 seconds */
  g_return_if_fail (core);
  wp_core_timeout_add_closure (core, &self->timeout_source, 3000,
      g_cclosure_new_object (G_CALLBACK (timeout_acquire_callback),
      G_OBJECT (self)));
}

static void
on_reservation_release (WpDbusDeviceReservation *reservation, gboolean forced,
    WpReserveDevice *self)
{
  /* Release reservation */
  wp_dbus_device_reservation_release (reservation);

  /* Destroy the device */
  invoke_manager_closure (self, FALSE);

  /* Only complete the release if not forced */
  if (!forced) {
    g_autoptr (WpCore) core = g_weak_ref_get (&self->core);
    g_return_if_fail (core);
    wp_core_sync (core, NULL, (GAsyncReadyCallback)on_device_done, self);
  }
}

static void
wp_reserve_device_constructed (GObject * object)
{
  WpReserveDevice *self = WP_RESERVE_DEVICE (object);
  g_autoptr (WpProxy) device = NULL;
  g_autoptr (WpCore) core = g_weak_ref_get (&self->core);
  g_return_if_fail (core);

  /* Create the JACK device object manager */
  self->jack_device_om = wp_object_manager_new ();
  wp_object_manager_add_interest (self->jack_device_om, WP_TYPE_DEVICE,
      WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY, PW_KEY_DEVICE_API, "=s", "jack",
      NULL);
  wp_object_manager_request_object_features (self->jack_device_om,
      WP_TYPE_DEVICE, WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL);
  wp_core_install_object_manager (core, self->jack_device_om);

  /* Handle the reservation signals */
  g_return_if_fail (self->reservation);
  g_signal_connect_object (self->reservation, "owner-appeared",
    (GCallback) on_reservation_owner_appeared, self, 0);
  g_signal_connect_object (self->reservation, "owner-vanished",
    (GCallback) on_reservation_owner_vanished, self, 0);
  g_signal_connect_object (self->reservation, "release",
    (GCallback) on_reservation_release, self, 0);

  /* Try to acquire the device */
  wp_dbus_device_reservation_acquire (self->reservation);

  G_OBJECT_CLASS (wp_reserve_device_parent_class)->constructed (object);
}

static void
wp_reserve_device_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec)
{
  WpReserveDevice *self = WP_RESERVE_DEVICE (object);

  switch (property_id) {
  case PROP_CORE:
    g_value_take_object (value, g_weak_ref_get (&self->core));
    break;
  case PROP_RESERVATION:
    g_value_set_object (value, self->reservation);
    break;
  case PROP_MANAGER_CLOSURE:
    g_value_set_boxed (value, self->manager_closure);
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
  case PROP_CORE:
    g_weak_ref_set (&self->core, g_value_get_object (value));
    break;
  case PROP_RESERVATION:
    self->reservation = g_value_dup_object (value);
    break;
  case PROP_MANAGER_CLOSURE:
    self->manager_closure = g_value_dup_boxed (value);
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

  /* Clear the current timeout acquire callback */
  if (self->timeout_source)
      g_source_destroy (self->timeout_source);
  g_clear_pointer (&self->timeout_source, g_source_unref);

  wp_dbus_device_reservation_release (self->reservation);

  /* JACK */
  g_clear_object (&self->jack_device_om);

  /* Props */
  g_weak_ref_clear (&self->core);
  g_clear_object (&self->reservation);
  g_clear_pointer (&self->manager_closure, g_closure_unref);

  G_OBJECT_CLASS (wp_reserve_device_parent_class)->finalize (object);
}

static void
wp_reserve_device_init (WpReserveDevice * self)
{
  /* Props */
  g_weak_ref_init (&self->core, NULL);
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
  g_object_class_install_property (object_class, PROP_CORE,
      g_param_spec_object ("core", "core",
      "The wireplumber core", WP_TYPE_CORE,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class, PROP_RESERVATION,
      g_param_spec_object ("reservation", "reservation",
      "The dbus device reservation", WP_TYPE_DBUS_DEVICE_RESERVATION,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class, PROP_MANAGER_CLOSURE,
      g_param_spec_boxed ("manager-closure", "manager-closure",
      "The closure that manages the device", G_TYPE_CLOSURE,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

WpReserveDevice *
wp_reserve_device_new (WpCore *core, WpDbusDeviceReservation *reservation,
    GClosure *manager_closure)
{
  return g_object_new (WP_TYPE_RESERVE_DEVICE,
      "core", core,
      "reservation", reservation,
      "manager-closure", manager_closure,
      NULL);
}
