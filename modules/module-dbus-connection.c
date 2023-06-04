/* WirePlumber
 *
 * Copyright © 2022-2023 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>
#include "dbus-connection-state.h"
#include "dbus-connection-enums.h"

WP_DEFINE_LOCAL_LOG_TOPIC ("m-dbus-connection")

enum
{
  PROP_0,
  PROP_BUS_TYPE,
  PROP_STATE,
  PROP_CONNECTION,
};

struct _WpDBusConnection
{
  WpPlugin parent;

  /* Props */
  GBusType bus_type;
  WpDBusConnectionState state;
  GDBusConnection *connection;

  GCancellable *cancellable;
};

static void on_connection_closed (GDBusConnection * connection,
    gboolean remote_peer_vanished, GError * error, gpointer data);

G_DECLARE_FINAL_TYPE (WpDBusConnection, wp_dbus_connection,
                      WP, DBUS_CONNECTION, WpPlugin)
G_DEFINE_TYPE (WpDBusConnection, wp_dbus_connection, WP_TYPE_PLUGIN)

static void
wp_dbus_connection_init (WpDBusConnection * self)
{
}

static void
wp_dbus_connection_set_state (WpDBusConnection * self, WpDBusConnectionState new_state)
{
  if (self->state != new_state) {
    self->state = new_state;
    g_object_notify (G_OBJECT (self), "state");
  }
}

static void
on_got_bus (GObject * obj, GAsyncResult * res, gpointer data)
{
  WpTransition *transition;
  WpDBusConnection *self;
  g_autoptr (GError) error = NULL;

  if (WP_IS_TRANSITION (data)) {
    // coming from wp_dbus_connection_enable
    transition = WP_TRANSITION (data);
    self = wp_transition_get_source_object (transition);
  } else {
    // coming from on_sync_reconnect
    transition = NULL;
    self = WP_DBUS_CONNECTION (data);
  }

  self->connection = g_dbus_connection_new_for_address_finish (res, &error);
  if (!self->connection) {
    if (transition) {
      g_prefix_error (&error, "Failed to connect to bus: ");
      wp_transition_return_error (transition, g_steal_pointer (&error));
    }
    return;
  }

  wp_debug_object (self, "Connected to bus");

  /* set up connection */
  g_signal_connect_object (self->connection, "closed",
      G_CALLBACK (on_connection_closed), self, 0);
  g_dbus_connection_set_exit_on_close (self->connection, FALSE);

  wp_dbus_connection_set_state (self, WP_DBUS_CONNECTION_STATE_CONNECTED);

  if (transition)
    wp_object_update_features (WP_OBJECT (self), WP_PLUGIN_FEATURE_ENABLED, 0);
}

static gboolean
do_connect (WpDBusConnection * self, GAsyncReadyCallback callback,
    gpointer data, GError ** error)
{
  g_autofree gchar *address = NULL;

  address = g_dbus_address_get_for_bus_sync (self->bus_type, NULL, error);
  if (!address) {
    g_prefix_error (error, "Error acquiring bus address: ");
    return FALSE;
  }

  wp_dbus_connection_set_state (self, WP_DBUS_CONNECTION_STATE_CONNECTING);

  wp_debug_object (self, "Connecting to bus: %s", address);
  g_dbus_connection_new_for_address (address,
      G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
      G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,
      NULL, self->cancellable, callback, data);
  return TRUE;
}

static void
on_sync_reconnect (WpCore * core, GAsyncResult * res, WpDBusConnection * self)
{
  g_autoptr (GError) error = NULL;

  if (!wp_core_sync_finish (core, res, &error)) {
    wp_warning_object (self, "core sync error: %s", error->message);
    return;
  }

  if (!do_connect (self, on_got_bus, self, &error))
    wp_notice_object (self, "Cannot reconnect: %s", error->message);
}

static void
on_connection_closed (GDBusConnection * connection,
    gboolean remote_peer_vanished, GError * error, gpointer data)
{
  WpDBusConnection *self = WP_DBUS_CONNECTION (data);
  g_autoptr (WpCore) core = NULL;

  wp_notice_object (self, "DBus connection closed: %s", error->message);

  g_clear_object (&self->connection);
  wp_dbus_connection_set_state (self, WP_DBUS_CONNECTION_STATE_CLOSED);

  /* try to reconnect on idle if connection was closed */
  core = wp_object_get_core (WP_OBJECT (self));
  if (core && wp_core_is_connected (core)) {
    wp_notice_object (self, "Trying to reconnect after core sync");
    wp_core_sync_closure (core, NULL, g_cclosure_new_object (
        G_CALLBACK (on_sync_reconnect), G_OBJECT (self)));
  }
}

static void
wp_dbus_connection_enable (WpPlugin * plugin, WpTransition * transition)
{
  WpDBusConnection *self = WP_DBUS_CONNECTION (plugin);
  g_autoptr (GError) error = NULL;
  if (!do_connect (self, on_got_bus, transition, &error)) {
    wp_transition_return_error (transition, g_steal_pointer (&error));
  }
}

static void
wp_dbus_connection_disable (WpPlugin * plugin)
{
  WpDBusConnection *self = WP_DBUS_CONNECTION (plugin);

  g_cancellable_cancel (self->cancellable);

  g_clear_object (&self->connection);
  wp_dbus_connection_set_state (self, WP_DBUS_CONNECTION_STATE_CLOSED);

  g_clear_object (&self->cancellable);
  self->cancellable = g_cancellable_new ();
}

static void
wp_dbus_connection_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpDBusConnection *self = WP_DBUS_CONNECTION (object);

  switch (property_id) {
  case PROP_BUS_TYPE:
    g_value_set_enum (value, self->bus_type);
    break;
  case PROP_STATE:
    g_value_set_enum (value, self->state);
    break;
  case PROP_CONNECTION:
    g_value_set_object (value, self->connection);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_dbus_connection_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpDBusConnection *self = WP_DBUS_CONNECTION (object);

  switch (property_id) {
  case PROP_BUS_TYPE:
    self->bus_type = g_value_get_enum (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_dbus_connection_class_init (WpDBusConnectionClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpPluginClass *plugin_class = (WpPluginClass *) klass;

  object_class->get_property = wp_dbus_connection_get_property;
  object_class->set_property = wp_dbus_connection_set_property;

  plugin_class->enable = wp_dbus_connection_enable;
  plugin_class->disable = wp_dbus_connection_disable;

  g_object_class_install_property (object_class, PROP_BUS_TYPE,
      g_param_spec_enum ("bus-type", "bus-type", "The bus type",
          G_TYPE_BUS_TYPE, G_BUS_TYPE_NONE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_STATE,
      g_param_spec_enum ("state", "state", "The dbus connection state",
          WP_TYPE_DBUS_CONNECTION_STATE, WP_DBUS_CONNECTION_STATE_CLOSED,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_CONNECTION,
      g_param_spec_object ("connection", "connection", "The dbus connection",
          G_TYPE_DBUS_CONNECTION, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
}

WP_PLUGIN_EXPORT GObject *
wireplumber__module_init (WpCore * core, WpSpaJson * args, GError ** error)
{
  return G_OBJECT (g_object_new (
      wp_dbus_connection_get_type(),
      "name", "dbus-connection",
      "core", core,
      "bus-type", G_BUS_TYPE_SESSION,
      NULL));
}
