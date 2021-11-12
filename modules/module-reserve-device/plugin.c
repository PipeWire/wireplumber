/* WirePlumber
 *
 * Copyright © 2021 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "plugin.h"
#include "reserve-device.h"
#include "reserve-device-enums.h"

static void setup_connection (WpReserveDevicePlugin *self);

G_DEFINE_TYPE (WpReserveDevicePlugin, wp_reserve_device_plugin, WP_TYPE_PLUGIN)

enum
{
  ACTION_CREATE_RESERVATION,
  ACTION_DESTROY_RESERVATION,
  ACTION_GET_RESERVATION,
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_STATE,
};

static guint signals[LAST_SIGNAL] = { 0 };

static void
rd_unref (gpointer data)
{
  WpReserveDevice *rd = data;
  g_signal_emit_by_name (rd, "release");
  g_object_unref (rd);
}

static void
wp_reserve_device_plugin_init (WpReserveDevicePlugin * self)
{
  self->cancellable = g_cancellable_new ();
  self->reserve_devices = g_hash_table_new_full (g_str_hash, g_str_equal,
      NULL, rd_unref);
}

static void
wp_reserve_device_plugin_finalize (GObject * object)
{
  WpReserveDevicePlugin *self = WP_RESERVE_DEVICE_PLUGIN (object);

  g_clear_pointer (&self->reserve_devices, g_hash_table_unref);
  g_clear_object (&self->cancellable);

  G_OBJECT_CLASS (wp_reserve_device_plugin_parent_class)->finalize (object);
}

static void
clear_connection (WpReserveDevicePlugin *self)
{
  g_hash_table_remove_all (self->reserve_devices);
  g_clear_object (&self->manager);
  g_clear_object (&self->connection);

  if (self->state != WP_DBUS_CONNECTION_STATE_CLOSED) {
    self->state = WP_DBUS_CONNECTION_STATE_CLOSED;
    g_object_notify (G_OBJECT (self), "state");
  }
}

static gboolean
do_connect (WpReserveDevicePlugin *self, GAsyncReadyCallback callback,
    gpointer data, GError **error)
{
  g_autofree gchar *address = NULL;

  address = g_dbus_address_get_for_bus_sync (G_BUS_TYPE_SESSION, NULL, error);
  if (!address) {
    g_prefix_error (error, "Error acquiring session bus address: ");
    return FALSE;
  }

  wp_debug_object (self, "Connecting to bus: %s", address);

  self->state = WP_DBUS_CONNECTION_STATE_CONNECTING;
  g_object_notify (G_OBJECT (self), "state");

  g_dbus_connection_new_for_address (address,
      G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
      G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,
      NULL, self->cancellable, callback, data);
  return TRUE;
}

static void
on_reconnect_got_bus (GObject * obj, GAsyncResult * res, gpointer data)
{
  WpReserveDevicePlugin *self = WP_RESERVE_DEVICE_PLUGIN (data);
  g_autoptr (GError) error = NULL;

  self->connection = g_dbus_connection_new_for_address_finish (res, &error);
  if (!self->connection) {
    clear_connection (self);
    wp_info_object (self, "Could not reconnect to session bus: %s",
        error->message);
    return;
  }

  wp_debug_object (self, "Reconnected to bus");
  setup_connection (self);
}

static gboolean
idle_connect (WpReserveDevicePlugin * self)
{
  g_autoptr (GError) error = NULL;

  if (!do_connect (self, on_reconnect_got_bus, self, &error))
    wp_info_object (self, "Cannot reconnect: %s", error->message);

  return G_SOURCE_REMOVE;
}

static void
on_connection_closed (GDBusConnection *connection,
    gboolean remote_peer_vanished, GError *error, gpointer data)
{
  WpReserveDevicePlugin *self = WP_RESERVE_DEVICE_PLUGIN (data);
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));

  wp_info_object (self, "D-Bus connection closed: %s", error->message);

  clear_connection (self);

  /* try to reconnect on idle if connection was closed */
  if (core)
    wp_core_idle_add_closure (core, NULL, g_cclosure_new_object (
        G_CALLBACK (idle_connect), G_OBJECT (self)));
}

static void
setup_connection (WpReserveDevicePlugin *self)
{
  g_signal_connect_object (self->connection, "closed",
      G_CALLBACK (on_connection_closed), self, 0);
  g_dbus_connection_set_exit_on_close (self->connection, FALSE);

  self->manager = g_dbus_object_manager_server_new (FDO_RESERVE_DEVICE1_PATH);
  g_dbus_object_manager_server_set_connection (self->manager, self->connection);

  self->state = WP_DBUS_CONNECTION_STATE_CONNECTED;
  g_object_notify (G_OBJECT (self), "state");
}

static void
on_enable_got_bus (GObject * obj, GAsyncResult * res, gpointer data)
{
  WpTransition *transition = WP_TRANSITION (data);
  WpReserveDevicePlugin *self = wp_transition_get_source_object (transition);
  g_autoptr (GError) error = NULL;

  self->connection = g_dbus_connection_new_for_address_finish (res, &error);
  if (!self->connection) {
    clear_connection (self);
    g_prefix_error (&error, "Failed to connect to session bus: ");
    wp_transition_return_error (transition, g_steal_pointer (&error));
    return;
  }

  wp_debug_object (self, "Connected to bus");
  setup_connection (self);

  wp_object_update_features (WP_OBJECT (self), WP_PLUGIN_FEATURE_ENABLED, 0);
}

static void
wp_reserve_device_plugin_enable (WpPlugin * plugin, WpTransition * transition)
{
  WpReserveDevicePlugin *self = WP_RESERVE_DEVICE_PLUGIN (plugin);
  g_autoptr (GError) error = NULL;

  g_return_if_fail (self->state == WP_DBUS_CONNECTION_STATE_CLOSED);

  if (!do_connect (self, on_enable_got_bus, transition, &error)) {
    wp_transition_return_error (transition, g_steal_pointer (&error));
    return;
  }
}

static void
wp_reserve_device_plugin_disable (WpPlugin * plugin)
{
  WpReserveDevicePlugin *self = WP_RESERVE_DEVICE_PLUGIN (plugin);

  g_cancellable_cancel (self->cancellable);
  clear_connection (self);
  g_clear_object (&self->cancellable);
  self->cancellable = g_cancellable_new ();

  wp_object_update_features (WP_OBJECT (self), 0, WP_PLUGIN_FEATURE_ENABLED);
}

static gpointer
wp_reserve_device_plugin_create_reservation (WpReserveDevicePlugin *self,
    const gchar *name, const gchar *app_name, const gchar *app_dev_name,
    gint priority)
{
  if (self->state != WP_DBUS_CONNECTION_STATE_CONNECTED) {
    wp_message_object (self, "not connected to D-Bus");
    return NULL;
  }

  WpReserveDevice *rd = g_object_new (wp_reserve_device_get_type (),
      "plugin", self,
      "name", name,
      "application-name", app_name,
      "application-device-name", app_dev_name,
      "priority", priority,
      NULL);

  /* use rd->name to avoid copying @em name again */
  g_hash_table_replace (self->reserve_devices, rd->name, rd);

  return g_object_ref (rd);
}

static void
wp_reserve_device_plugin_destroy_reservation (WpReserveDevicePlugin *self,
    const gchar *name)
{
  if (self->state != WP_DBUS_CONNECTION_STATE_CONNECTED) {
    wp_message_object (self, "not connected to D-Bus");
    return;
  }
  g_hash_table_remove (self->reserve_devices, name);
}

static gpointer
wp_reserve_device_plugin_get_reservation (WpReserveDevicePlugin *self,
    const gchar *name)
{
  if (self->state != WP_DBUS_CONNECTION_STATE_CONNECTED) {
    wp_message_object (self, "not connected to D-Bus");
    return NULL;
  }

  WpReserveDevice *rd = g_hash_table_lookup (self->reserve_devices, name);
  return rd ? g_object_ref (rd) : NULL;
}

static void
wp_reserve_device_plugin_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpReserveDevicePlugin *self = WP_RESERVE_DEVICE_PLUGIN (object);

  switch (property_id) {
  case PROP_STATE:
    g_value_set_enum (value, self->state);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_reserve_device_plugin_class_init (WpReserveDevicePluginClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpPluginClass *plugin_class = (WpPluginClass *) klass;

  object_class->finalize = wp_reserve_device_plugin_finalize;
  object_class->get_property = wp_reserve_device_plugin_get_property;

  plugin_class->enable = wp_reserve_device_plugin_enable;
  plugin_class->disable = wp_reserve_device_plugin_disable;

  g_object_class_install_property (object_class, PROP_STATE,
      g_param_spec_enum ("state", "state", "The state",
          WP_TYPE_DBUS_CONNECTION_STATE, WP_DBUS_CONNECTION_STATE_CLOSED,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /**
   * WpReserveDevicePlugin::create-reservation:
   *
   * @brief
   * @em name:
   * @em app_name:
   * @em app_dev_name:
   * @em priority:
   *
   * Returns: (transfer full): the reservation object
   */
  signals[ACTION_CREATE_RESERVATION] = g_signal_new_class_handler (
      "create-reservation", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      (GCallback) wp_reserve_device_plugin_create_reservation,
      NULL, NULL, NULL,
      G_TYPE_OBJECT, 4, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT);

  /**
   * WpReserveDevicePlugin::destroy-reservation:
   *
   * @brief
   * @em name:
   *
   */
  signals[ACTION_DESTROY_RESERVATION] = g_signal_new_class_handler (
      "destroy-reservation", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      (GCallback) wp_reserve_device_plugin_destroy_reservation,
      NULL, NULL, NULL,
      G_TYPE_NONE, 1, G_TYPE_STRING);

  /**
   * WpReserveDevicePlugin::get-reservation:
   *
   * @brief
   * @em name:
   *
   * Returns: (transfer full): the reservation object
   */
  signals[ACTION_GET_RESERVATION] = g_signal_new_class_handler (
      "get-reservation", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      (GCallback) wp_reserve_device_plugin_get_reservation,
      NULL, NULL, NULL,
      G_TYPE_OBJECT, 1, G_TYPE_STRING);

}

WP_PLUGIN_EXPORT gboolean
wireplumber__module_init (WpCore * core, GVariant * args, GError ** error)
{
  wp_plugin_register (g_object_new (wp_reserve_device_plugin_get_type (),
      "name", "reserve-device",
      "core", core,
      NULL));
  return TRUE;
}
