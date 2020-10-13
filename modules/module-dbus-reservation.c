/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>

#include "module-dbus-reservation/dbus-device-reservation.h"
#include "module-dbus-reservation/reserve-device.h"

/* Signals */
enum
{
  SIGNAL_CREATE_RESERVATION,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

G_DECLARE_DERIVABLE_TYPE (WpDbusReservation, wp_dbus_reservation, WP,
    DBUS_RESERVATION, WpPlugin)

struct _WpDbusReservationClass
{
  WpPluginClass parent_class;

  void (*create_reservation) (WpDbusReservation *self, gint card_id,
      const gchar *app_dev_name, GClosure *manager_closure);
};

typedef struct _WpDbusReservationPrivate WpDbusReservationPrivate;
struct _WpDbusReservationPrivate
{
  GHashTable *device_reservations;
};

G_DEFINE_TYPE_WITH_PRIVATE (WpDbusReservation, wp_dbus_reservation,
    WP_TYPE_PLUGIN)

static void
wp_dbus_reservation_create_reservation (WpDbusReservation *self, gint card_id,
    const gchar *app_dev_name, GClosure *manager_closure)
{
  WpDbusReservationPrivate *priv;
  g_autoptr (WpDbusDeviceReservation) r = NULL;
  g_autoptr (WpReserveDevice) rd = NULL;
  g_autoptr (WpCore) core = NULL;

  g_return_if_fail (WP_IS_DBUS_RESERVATION (self));
  priv = wp_dbus_reservation_get_instance_private (self);

  wp_info_object (self, "creating dbus reservation for card %d", card_id);

  /* Check if the card has already dbus reservation */
  if (g_hash_table_contains (priv->device_reservations,
      GINT_TO_POINTER (card_id))) {
    wp_warning_object (self, "card %d has already dbus reservation", card_id);
    return;
  }

  /* Create the reservation */
  r = wp_dbus_device_reservation_new (card_id, PIPEWIRE_APPLICATION_NAME, 10,
      app_dev_name);

  /* Create the reserve device data */
  core = wp_plugin_get_core (WP_PLUGIN (self));
  rd = wp_reserve_device_new (core, g_steal_pointer (&r), manager_closure);

  /* Add the device */
  g_hash_table_insert (priv->device_reservations, GINT_TO_POINTER (card_id),
      g_steal_pointer (&rd));
}

static void
wp_dbus_reservation_activate (WpPlugin * plugin)
{
}

static void
wp_dbus_reservation_deactivate (WpPlugin * plugin)
{
}

static void
wp_dbus_reservation_finalize (GObject * object)
{
  WpDbusReservation *self = WP_DBUS_RESERVATION (object);
  WpDbusReservationPrivate *priv =
      wp_dbus_reservation_get_instance_private (self);

  g_clear_pointer (&priv->device_reservations, g_hash_table_unref);
}

static void
wp_dbus_reservation_init (WpDbusReservation * self)
{
  WpDbusReservationPrivate *priv =
      wp_dbus_reservation_get_instance_private (self);

  priv->device_reservations = g_hash_table_new_full (g_direct_hash,
      g_direct_equal, NULL, g_object_unref);
}

static void
wp_dbus_reservation_class_init (WpDbusReservationClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpPluginClass *plugin_class = (WpPluginClass *) klass;

  object_class->finalize = wp_dbus_reservation_finalize;
  plugin_class->activate = wp_dbus_reservation_activate;
  plugin_class->deactivate = wp_dbus_reservation_deactivate;

  klass->create_reservation = wp_dbus_reservation_create_reservation;

  /* Signals */
  signals[SIGNAL_CREATE_RESERVATION] = g_signal_new ("create-reservation",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET (WpDbusReservationClass, create_reservation), NULL, NULL,
      NULL, G_TYPE_NONE, 3, G_TYPE_INT, G_TYPE_STRING, G_TYPE_CLOSURE);
}

WP_PLUGIN_EXPORT void
wireplumber__module_init (WpModule * module, WpCore * core, GVariant * args)
{
  wp_plugin_register (g_object_new (wp_dbus_reservation_get_type (),
      "name", "dbus-reservation",
      "module", module,
      NULL));
}
