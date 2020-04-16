/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "reservation-data.h"

#include <spa/pod/builder.h>
#include <pipewire/pipewire.h>

struct _WpMonitorDeviceReservationData
{
  GObject parent;

  /* Props */
  GWeakRef device;
  WpMonitorDbusDeviceReservation *reservation;

  guint n_acquired;
};

enum {
  DEVICE_PROP_0,
  DEVICE_PROP_DEVICE,
  DEVICE_PROP_RESERVATION,
};

G_DEFINE_TYPE (WpMonitorDeviceReservationData,
    wp_monitor_device_reservation_data, G_TYPE_OBJECT)

static void
on_device_done (WpCore *core, GAsyncResult *res,
    WpMonitorDeviceReservationData *self)
{
  if (self->reservation)
    wp_monitor_dbus_device_reservation_complete_release (self->reservation, TRUE);
}

static void
on_reservation_acquired (GObject *obj, GAsyncResult *res, gpointer user_data)
{
  WpMonitorDeviceReservationData *self = user_data;
  WpMonitorDbusDeviceReservation *reserv =
      WP_MONITOR_DBUS_DEVICE_RESERVATION (obj);
  g_autoptr (GError) error = NULL;
  g_autoptr (WpProxy) device = NULL;
  g_autoptr (WpSpaPod) profile = NULL;

  /* Finish */
  if (!wp_monitor_dbus_device_reservation_async_finish (reserv, res, &error)) {
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
  wp_proxy_set_param (device, SPA_PARAM_Profile, 0, profile);
}

static void
on_reservation_release (WpMonitorDbusDeviceReservation *reservation, int forced,
    WpMonitorDeviceReservationData *self)
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
  wp_proxy_set_param (device, SPA_PARAM_Profile, 0, profile);

  /* Complete release on done */
  wp_core_sync (core, NULL, (GAsyncReadyCallback)on_device_done, self);
}

static void
on_device_destroyed (WpProxy *device, WpMonitorDeviceReservationData *self)
{
  wp_monitor_dbus_device_reservation_release (self->reservation);
}

static void
wp_monitor_device_reservation_data_constructed (GObject * object)
{
  WpMonitorDeviceReservationData *self =
      WP_MONITOR_DEVICE_RESERVATION_DATA (object);
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
  wp_monitor_dbus_device_reservation_acquire (self->reservation, NULL,
     on_reservation_acquired, self);

  G_OBJECT_CLASS (wp_monitor_device_reservation_data_parent_class)->constructed (object);
}

static void
wp_monitor_device_reservation_data_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec)
{
  WpMonitorDeviceReservationData *self =
      WP_MONITOR_DEVICE_RESERVATION_DATA (object);

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
wp_monitor_device_reservation_data_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec)
{
  WpMonitorDeviceReservationData *self =
      WP_MONITOR_DEVICE_RESERVATION_DATA (object);

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
wp_monitor_device_reservation_data_finalize (GObject * object)
{
  WpMonitorDeviceReservationData *self =
      WP_MONITOR_DEVICE_RESERVATION_DATA (object);

  wp_monitor_dbus_device_reservation_release (self->reservation);

  /* Props */
  g_weak_ref_clear (&self->device);
  g_clear_object (&self->reservation);

  G_OBJECT_CLASS (wp_monitor_device_reservation_data_parent_class)->finalize (object);
}

static void
wp_monitor_device_reservation_data_init (WpMonitorDeviceReservationData * self)
{
  /* Props */
  g_weak_ref_init (&self->device, NULL);

  self->n_acquired = 0;
}

static void
wp_monitor_device_reservation_data_class_init (
    WpMonitorDeviceReservationDataClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;

  object_class->constructed = wp_monitor_device_reservation_data_constructed;
  object_class->get_property = wp_monitor_device_reservation_data_get_property;
  object_class->set_property = wp_monitor_device_reservation_data_set_property;
  object_class->finalize = wp_monitor_device_reservation_data_finalize;

  /* Props */
  g_object_class_install_property (object_class, DEVICE_PROP_DEVICE,
      g_param_spec_object ("device", "device", "The device", WP_TYPE_PROXY,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class, DEVICE_PROP_RESERVATION,
      g_param_spec_object ("reservation", "reservation",
      "The dbus device reservation", WP_TYPE_MONITOR_DBUS_DEVICE_RESERVATION,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

WpMonitorDeviceReservationData *
wp_monitor_device_reservation_data_new (WpProxy *device,
    WpMonitorDbusDeviceReservation *reservation)
{
  return g_object_new (WP_TYPE_MONITOR_DEVICE_RESERVATION_DATA,
      "device", device,
      "reservation", reservation,
      NULL);
}

void
wp_monitor_device_reservation_data_acquire (
    WpMonitorDeviceReservationData *self)
{
  g_return_if_fail (WP_IS_MONITOR_DEVICE_RESERVATION_DATA (self));
  g_return_if_fail (self->reservation);

  if (self->n_acquired == 0)
    wp_monitor_dbus_device_reservation_acquire (self->reservation, NULL,
        on_reservation_acquired, self);

  self->n_acquired++;
}

void
wp_monitor_device_reservation_data_release (
    WpMonitorDeviceReservationData *self)
{
  g_return_if_fail (WP_IS_MONITOR_DEVICE_RESERVATION_DATA (self));
  g_return_if_fail (self->reservation);

  if (self->n_acquired == 1)
    wp_monitor_dbus_device_reservation_release (self->reservation);

  self->n_acquired--;
}


struct _WpMonitorNodeReservationData
{
  GObject parent;

  /* Props */
  GWeakRef node;
  WpMonitorDeviceReservationData *device_data;

  gboolean acquired;
  GSource *timeout_source;
};

enum {
  NODE_PROP_0,
  NODE_PROP_NODE,
  NODE_PROP_DEVICE_DATA,
};

G_DEFINE_TYPE (WpMonitorNodeReservationData,
    wp_monitor_node_reservation_data, G_TYPE_OBJECT)

static void
on_node_destroyed (WpProxy *node, WpMonitorNodeReservationData *self)
{
  if (self->acquired)
    wp_monitor_device_reservation_data_release (self->device_data);
}

static void
wp_monitor_node_reservation_data_constructed (GObject * object)
{
  WpMonitorNodeReservationData *self =
      WP_MONITOR_NODE_RESERVATION_DATA (object);
  WpProxy *node = g_weak_ref_get (&self->node);

  g_return_if_fail (node);

  /* Make sure the device is released when the pw proxy node is destroyed */
  g_signal_connect_object (node, "pw-proxy-destroyed",
      (GCallback) on_node_destroyed, self, 0);

  G_OBJECT_CLASS (wp_monitor_node_reservation_data_parent_class)->constructed (object);
}

static void
wp_monitor_node_reservation_data_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec)
{
  WpMonitorNodeReservationData *self =
      WP_MONITOR_NODE_RESERVATION_DATA (object);

  switch (property_id) {
  case NODE_PROP_NODE:
    g_value_take_object (value, g_weak_ref_get (&self->node));
    break;
  case NODE_PROP_DEVICE_DATA:
    g_value_set_object (value, self->device_data);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_monitor_node_reservation_data_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec)
{
  WpMonitorNodeReservationData *self =
      WP_MONITOR_NODE_RESERVATION_DATA (object);

  switch (property_id) {
  case NODE_PROP_NODE:
    g_weak_ref_set (&self->node, g_value_get_object (value));
    break;
  case NODE_PROP_DEVICE_DATA:
    self->device_data = g_value_dup_object (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_monitor_node_reservation_data_finalize (GObject * object)
{
  WpMonitorNodeReservationData *self =
      WP_MONITOR_NODE_RESERVATION_DATA (object);

  /* Clear the current timeout release callback */
  if (self->timeout_source)
      g_source_destroy (self->timeout_source);
  g_clear_pointer (&self->timeout_source, g_source_unref);

  /* Release device if acquired */
  if (self->acquired)
    wp_monitor_device_reservation_data_release (self->device_data);

  /* Props */
  g_weak_ref_clear (&self->node);
  g_clear_object (&self->device_data);

  G_OBJECT_CLASS (wp_monitor_node_reservation_data_parent_class)->finalize (object);
}

static void
wp_monitor_node_reservation_data_init (WpMonitorNodeReservationData * self)
{
  /* Props */
  g_weak_ref_init (&self->node, NULL);

  self->acquired = FALSE;
  self->timeout_source = NULL;
}

static void
wp_monitor_node_reservation_data_class_init (
    WpMonitorNodeReservationDataClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;

  object_class->constructed = wp_monitor_node_reservation_data_constructed;
  object_class->get_property = wp_monitor_node_reservation_data_get_property;
  object_class->set_property = wp_monitor_node_reservation_data_set_property;
  object_class->finalize = wp_monitor_node_reservation_data_finalize;

  /* Props */
  g_object_class_install_property (object_class, NODE_PROP_NODE,
      g_param_spec_object ("node", "node", "The node", WP_TYPE_PROXY,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class, NODE_PROP_DEVICE_DATA,
      g_param_spec_object ("device-data", "device-data",
      "The monitor device reservation data", WP_TYPE_MONITOR_DEVICE_RESERVATION_DATA,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

WpMonitorNodeReservationData *
wp_monitor_node_reservation_data_new (WpProxy *node,
    WpMonitorDeviceReservationData *device_data)
{
  return g_object_new (WP_TYPE_MONITOR_NODE_RESERVATION_DATA,
      "node", node,
      "device-data", device_data,
      NULL);
}

static gboolean
timeout_release_callback (gpointer data)
{
  WpMonitorNodeReservationData *self = data;
  g_return_val_if_fail (self, G_SOURCE_REMOVE);

  wp_monitor_device_reservation_data_release (self->device_data);
  self->acquired = FALSE;
  return G_SOURCE_REMOVE;
}

void
wp_monitor_node_reservation_data_timeout_release (
    WpMonitorNodeReservationData *self, guint64 timeout_ms)
{
  g_autoptr (WpProxy) node = NULL;
  g_autoptr (WpCore) core = NULL;
  g_return_if_fail (WP_IS_MONITOR_NODE_RESERVATION_DATA (self));

  node = g_weak_ref_get (&self->node);
  g_return_if_fail (node);
  core = wp_proxy_get_core (node);
  g_return_if_fail (core);

  /* Clear the current timeout release callback */
  if (self->timeout_source)
      g_source_destroy (self->timeout_source);
  g_clear_pointer (&self->timeout_source, g_source_unref);

  /* Add new timeout release callback */
  wp_core_timeout_add (core, &self->timeout_source, timeout_ms,
        timeout_release_callback, g_object_ref (self), g_object_unref);
}

void
wp_monitor_node_reservation_data_acquire (WpMonitorNodeReservationData *self)
{
  g_return_if_fail (WP_IS_MONITOR_NODE_RESERVATION_DATA (self));

  /* Clear the current timeout release callback */
  if (self->timeout_source)
      g_source_destroy (self->timeout_source);
  g_clear_pointer (&self->timeout_source, g_source_unref);

  /* Don't do anything if already acquired */
  if (self->acquired)
    return;

  /* Acquire the device */
  wp_monitor_device_reservation_data_acquire (self->device_data);
  self->acquired = TRUE;
}

