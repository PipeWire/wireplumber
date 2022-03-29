/* WirePlumber
 *
 * Copyright © 2022 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#define G_LOG_DOMAIN "wp-dbus"

#include "private/registry.h"
#include "log.h"
#include "wpenums.h"
#include "dbus.h"

enum {
  STEP_DBUS_ENABLE = WP_TRANSITION_STEP_CUSTOM_START,
};

enum
{
  PROP_DBUS_0,
  PROP_DBUS_BUS_TYPE,
  PROP_DBUS_STATE,
};

struct _WpDbus
{
  WpObject parent;

  /* Props */
  GBusType bus_type;
  WpDBusState state;

  GCancellable *cancellable;
  GDBusConnection *connection;
};

static void on_connection_closed (GDBusConnection *connection, gboolean
    remote_peer_vanished, GError *error, gpointer data);

G_DEFINE_TYPE (WpDbus, wp_dbus, WP_TYPE_OBJECT)

static void
wp_dbus_init (WpDbus * self)
{
}

static void
wp_dbus_set_state (WpDbus *self, WpDBusState new_state)
{
  if (self->state != new_state) {
    self->state = new_state;
    g_object_notify (G_OBJECT (self), "state");
  }
}

static void
on_got_bus (GObject * obj, GAsyncResult * res, gpointer data)
{
  WpTransition *transition = WP_TRANSITION (data);
  WpDbus *self = wp_transition_get_source_object (transition);
  g_autoptr (GError) error = NULL;

  self->connection = g_dbus_connection_new_for_address_finish (res, &error);
  if (!self->connection) {
    g_prefix_error (&error, "Failed to connect to bus: ");
    wp_transition_return_error (transition, g_steal_pointer (&error));
    return;
  }

  wp_debug_object (self, "Connected to bus");

  /* set up connection */
  g_signal_connect_object (self->connection, "closed",
      G_CALLBACK (on_connection_closed), self, 0);
  g_dbus_connection_set_exit_on_close (self->connection, FALSE);

  wp_dbus_set_state (self, WP_DBUS_STATE_CONNECTED);

  wp_object_update_features (WP_OBJECT (self), WP_DBUS_FEATURE_ENABLED, 0);
}

static gboolean
do_connect (WpDbus *self, GAsyncReadyCallback callback, gpointer data,
    GError **error)
{
  g_autofree gchar *address = NULL;

  address = g_dbus_address_get_for_bus_sync (self->bus_type, NULL, error);
  if (!address) {
    g_prefix_error (error, "Error acquiring bus address: ");
    return FALSE;
  }

  wp_dbus_set_state (self, WP_DBUS_STATE_CONNECTING);

  wp_debug_object (self, "Connecting to bus: %s", address);
  g_dbus_connection_new_for_address (address,
      G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
      G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,
      NULL, self->cancellable, callback, data);
  return TRUE;
}

static void
on_sync_reconnect (WpCore * core, GAsyncResult * res, WpDbus * self)
{
  g_autoptr (GError) error = NULL;

  if (!wp_core_sync_finish (core, res, &error)) {
    wp_warning_object (self, "core sync error: %s", error->message);
    return;
  }

  if (!do_connect (self, on_got_bus, self, &error))
    wp_info_object (self, "Cannot reconnect on sync: %s", error->message);
}

static void
on_connection_closed (GDBusConnection *connection,
    gboolean remote_peer_vanished, GError *error, gpointer data)
{
  WpDbus *self = WP_DBUS (data);
  g_autoptr (WpCore) core = NULL;

  wp_info_object (self, "DBus connection closed: %s", error->message);

  g_clear_object (&self->connection);
  wp_dbus_set_state (self, WP_DBUS_STATE_CLOSED);

  /* try to reconnect on idle if connection was closed */
  core = wp_object_get_core (WP_OBJECT (self));
  if (core && wp_core_is_connected (core)) {
    wp_info_object (self, "Trying to reconnect on sync");
    wp_core_sync_closure (core, NULL, g_cclosure_new_object (
        G_CALLBACK (on_sync_reconnect), G_OBJECT (self)));
  }
}

static void
wp_dbus_enable (WpDbus *self, WpTransition *transition)
{
  g_autoptr (GError) error = NULL;
  if (!do_connect (self, on_got_bus, transition, &error)) {
    wp_transition_return_error (transition, g_steal_pointer (&error));
    return;
  }
}

static void
wp_dbus_disable (WpDbus *self)
{
  g_cancellable_cancel (self->cancellable);

  g_clear_object (&self->connection);
  wp_dbus_set_state (self, WP_DBUS_STATE_CLOSED);

  g_clear_object (&self->cancellable);
  self->cancellable = g_cancellable_new ();

  wp_object_update_features (WP_OBJECT (self), 0, WP_DBUS_FEATURE_ENABLED);
}

static WpObjectFeatures
wp_dbus_get_supported_features (WpObject * self)
{
  return WP_DBUS_FEATURE_ENABLED;
}

static guint
wp_dbus_activate_get_next_step (WpObject * object,
     WpFeatureActivationTransition * t, guint step, WpObjectFeatures missing)
{
  switch (step) {
    case WP_TRANSITION_STEP_NONE:
      return STEP_DBUS_ENABLE;
    case STEP_DBUS_ENABLE:
      return WP_TRANSITION_STEP_NONE;
    default:
      return WP_TRANSITION_STEP_ERROR;
  }
}

static void
wp_dbus_activate_execute_step (WpObject * object,
    WpFeatureActivationTransition * t, guint step, WpObjectFeatures missing)
{
  WpDbus *self = WP_DBUS (object);
  WpTransition *transition = WP_TRANSITION (t);

  switch (step) {
    case STEP_DBUS_ENABLE:
      wp_dbus_enable (self, transition);
      break;

    case WP_TRANSITION_STEP_ERROR:
      break;

    default:
      g_return_if_reached ();
  }
}

static void
wp_dbus_deactivate (WpObject * object, WpObjectFeatures features)
{
  WpDbus *self = WP_DBUS (object);
  guint current = wp_object_get_active_features (object);

  if (features & current & WP_DBUS_FEATURE_ENABLED)
    wp_dbus_disable (self);
}

static void
wp_dbus_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpDbus *self = WP_DBUS (object);

  switch (property_id) {
  case PROP_DBUS_BUS_TYPE:
    g_value_set_enum (value, self->bus_type);
    break;
  case PROP_DBUS_STATE:
    g_value_set_enum (value, self->state);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_dbus_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpDbus *self = WP_DBUS (object);

  switch (property_id) {
  case PROP_DBUS_BUS_TYPE:
    self->bus_type = g_value_get_enum (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_dbus_class_init (WpDbusClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpObjectClass *wpobject_class = (WpObjectClass *) klass;

  object_class->set_property = wp_dbus_set_property;
  object_class->get_property = wp_dbus_get_property;

  wpobject_class->get_supported_features = wp_dbus_get_supported_features;
  wpobject_class->activate_get_next_step = wp_dbus_activate_get_next_step;
  wpobject_class->activate_execute_step = wp_dbus_activate_execute_step;
  wpobject_class->deactivate = wp_dbus_deactivate;

  g_object_class_install_property (object_class, PROP_DBUS_BUS_TYPE,
      g_param_spec_enum ("bus-type", "bus-type", "The bus type",
          G_TYPE_BUS_TYPE, G_BUS_TYPE_NONE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_DBUS_STATE,
      g_param_spec_enum ("state", "state", "The dbus connection state",
          WP_TYPE_DBUS_STATE, WP_DBUS_STATE_CLOSED,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
}

static gboolean
find_dbus_func (gpointer object, gpointer p)
{
  GBusType *bus_type = p;

  if (!WP_IS_DBUS (object) || !bus_type)
    return FALSE;

  return wp_dbus_get_bus_type (WP_DBUS (object)) == *bus_type;
}

/*!
 * \brief Returns the dbus instance that is associated with the given core and
 * bus type.
 *
 * This method will also create the instance and register it with the core
 * if it had not been created before.
 *
 * \param core the core
 * \param bus_type the bus type
 * \return (transfer full): the dbus instance
 */
WpDbus *
wp_dbus_get_instance (WpCore *core, GBusType bus_type)
{
  WpRegistry *registry;
  WpDbus *dbus;

  g_return_val_if_fail (core, NULL);
  g_return_val_if_fail (
      bus_type != G_BUS_TYPE_NONE || bus_type != G_BUS_TYPE_STARTER, NULL);

  registry = wp_core_get_registry (core);
  dbus = wp_registry_find_object (registry, (GEqualFunc) find_dbus_func,
      &bus_type);
  if (G_UNLIKELY (!dbus)) {
    dbus = g_object_new (WP_TYPE_DBUS,
        "core", core,
        "bus-type", bus_type,
        NULL);
    wp_registry_register_object (registry, g_object_ref (dbus));
  }

  return dbus;
}

/*!
 * \brief Returns the bus type of the dbus object.
 *
 * \param self the DBus object
 * \returns the bus type
 */
GBusType
wp_dbus_get_bus_type (WpDbus *self)
{
  g_return_val_if_fail (WP_IS_DBUS (self), G_BUS_TYPE_NONE);

  return self->bus_type;
}

/*!
 * \brief Returns the dbus connection state of the dbus object.
 *
 * \param self the DBus object
 * \returns the dbus connection state
 */
WpDBusState
wp_dbus_get_state (WpDbus *self)
{
  g_return_val_if_fail (WP_IS_DBUS (self), WP_DBUS_STATE_CLOSED);

  return self->state;
}

/*!
 * \brief Returns the DBus connection object of the dbus object.
 *
 * \param self the DBus object
 * \return (transfer full): the DBus connection object
 */
GDBusConnection *
wp_dbus_get_connection (WpDbus *self)
{
  g_return_val_if_fail (WP_IS_DBUS (self), NULL);

  return self->connection ? g_object_ref (self->connection) : NULL;
}
