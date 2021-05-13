/* WirePlumber
 *
 * Copyright © 2021 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "plugin.h"
#include "portal-permissionstore-enums.h"

#define DBUS_INTERFACE_NAME "org.freedesktop.impl.portal.PermissionStore"
#define DBUS_OBJECT_PATH "/org/freedesktop/impl/portal/PermissionStore"

G_DEFINE_TYPE (WpPortalPermissionStorePlugin, wp_portal_permissionstore_plugin,
    WP_TYPE_PLUGIN)

enum
{
  ACTION_LOOKUP,
  ACTION_SET,
  SIGNAL_CHANGED,
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_STATE,
};

static guint signals[LAST_SIGNAL] = { 0 };

static GVariant *
wp_portal_permissionstore_plugin_lookup (WpPortalPermissionStorePlugin *self,
    const gchar *table, const gchar *id)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (GVariant) res = NULL;
  GVariant *permissions = NULL, *data = NULL;

  g_return_val_if_fail (self->connection, NULL);

  /* Lookup */
  res = g_dbus_connection_call_sync (self->connection, DBUS_INTERFACE_NAME,
      DBUS_OBJECT_PATH, DBUS_INTERFACE_NAME, "Lookup",
      g_variant_new ("(ss)", table, id), NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL,
      &error);
  if (error) {
    wp_warning_object (self, "Failed to call Lookup: %s", error->message);
    return NULL;
  }

  /* Get the permissions */
  g_variant_get (res, "(@a{sas}@v)", &permissions, &data);

  return permissions ? g_variant_ref (permissions) : NULL;
}

static void
wp_portal_permissionstore_plugin_set (WpPortalPermissionStorePlugin *self,
    const gchar *table, gboolean create, const gchar *id, GVariant *permissions)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (GVariant) res = NULL;
  GVariant *data = NULL;

  g_return_if_fail (self->connection);

  /* Set */
  res = g_dbus_connection_call_sync (self->connection, DBUS_INTERFACE_NAME,
      DBUS_OBJECT_PATH, DBUS_INTERFACE_NAME, "Set",
      g_variant_new ("(sbs@a{sas}@v)", table, id, permissions, data), NULL,
      G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
  if (error)
    wp_warning_object (self, "Failed to call Set: %s", error->message);
}

static void
wp_portal_permissionstore_plugin_changed (GDBusConnection *connection,
    const gchar *sender_name, const gchar *object_path,
    const gchar *interface_name, const gchar *signal_name,
    GVariant *parameters, gpointer user_data)
{
  WpPortalPermissionStorePlugin *self =
      WP_PORTAL_PERMISSIONSTORE_PLUGIN (user_data);
  const char *table = NULL, *id = NULL;
  gboolean deleted = FALSE;
  GVariant *permissions = NULL, *data = NULL;

  g_return_if_fail (parameters);
  g_variant_get (parameters, "(ssb@v@a{sas})", &table, &id, &deleted, &data,
      &permissions);

  g_signal_emit (self, signals[SIGNAL_CHANGED], 0, table, id, deleted,
      permissions);
}

static void
wp_portal_permissionstore_plugin_init (WpPortalPermissionStorePlugin * self)
{
  self->cancellable = g_cancellable_new ();
}

static void
wp_portal_permissionstore_plugin_finalize (GObject * object)
{
  WpPortalPermissionStorePlugin *self =
      WP_PORTAL_PERMISSIONSTORE_PLUGIN (object);

  g_clear_object (&self->cancellable);

  G_OBJECT_CLASS (wp_portal_permissionstore_plugin_parent_class)->finalize (
      object);
}

static void
wp_portal_permissionstore_plugin_disable_internal (
    WpPortalPermissionStorePlugin *self)
{
  if (self->connection && self->signal_id > 0)
    g_dbus_connection_signal_unsubscribe (self->connection, self->signal_id);
  g_clear_object (&self->connection);

  if (self->state != WP_DBUS_CONNECTION_STATUS_CLOSED) {
    self->state = WP_DBUS_CONNECTION_STATUS_CLOSED;
    g_object_notify (G_OBJECT (self), "state");
  }

  wp_object_update_features (WP_OBJECT (self), 0, WP_PLUGIN_FEATURE_ENABLED);
}

static void
on_connection_closed (GDBusConnection *connection,
    gboolean remote_peer_vanished, GError *error, gpointer data)
{
  WpPortalPermissionStorePlugin *self =
      WP_PORTAL_PERMISSIONSTORE_PLUGIN (data);
  wp_info_object (self, "D-Bus connection closed: %s", error->message);
  wp_portal_permissionstore_plugin_disable_internal (self);
}

static void
got_bus (GObject * obj, GAsyncResult * res, gpointer data)
{
  WpTransition *transition = WP_TRANSITION (data);
  WpPortalPermissionStorePlugin *self =
      wp_transition_get_source_object (transition);
  g_autoptr (GError) error = NULL;

  self->connection = g_dbus_connection_new_for_address_finish (res, &error);
  if (!self->connection) {
    wp_portal_permissionstore_plugin_disable_internal (self);
    g_prefix_error (&error, "Failed to connect to session bus: ");
    wp_transition_return_error (transition, g_steal_pointer (&error));
    return;
  }

  wp_debug_object (self, "Connected to bus");

  g_signal_connect_object (self->connection, "closed",
      G_CALLBACK (on_connection_closed), self, 0);
  g_dbus_connection_set_exit_on_close (self->connection, FALSE);

  self->state = WP_DBUS_CONNECTION_STATUS_CONNECTED;
  g_object_notify (G_OBJECT (self), "state");

  /* Listen for the changed signal */
  self->signal_id = g_dbus_connection_signal_subscribe (self->connection,
      DBUS_INTERFACE_NAME, DBUS_INTERFACE_NAME, "Changed", NULL, NULL,
      G_DBUS_SIGNAL_FLAGS_NONE, wp_portal_permissionstore_plugin_changed, self,
      NULL);

  wp_object_update_features (WP_OBJECT (self), WP_PLUGIN_FEATURE_ENABLED, 0);
}

static void
wp_portal_permissionstore_plugin_enable (WpPlugin * plugin,
    WpTransition * transition)
{
  WpPortalPermissionStorePlugin *self =
      WP_PORTAL_PERMISSIONSTORE_PLUGIN (plugin);
  g_autoptr (GError) error = NULL;
  g_autofree gchar *address = NULL;

  g_return_if_fail (self->state == WP_DBUS_CONNECTION_STATUS_CLOSED);

  address = g_dbus_address_get_for_bus_sync (G_BUS_TYPE_SESSION, NULL, &error);
  if (!address) {
    g_prefix_error (&error, "Error acquiring session bus address: ");
    wp_transition_return_error (transition, g_steal_pointer (&error));
    return;
  }

  wp_debug_object (self, "Connecting to bus: %s", address);

  self->state = WP_DBUS_CONNECTION_STATUS_CONNECTING;
  g_object_notify (G_OBJECT (self), "state");

  g_dbus_connection_new_for_address (address,
      G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
      G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,
      NULL, self->cancellable, got_bus, transition);
}

static void
wp_portal_permissionstore_plugin_disable (WpPlugin * plugin)
{
  WpPortalPermissionStorePlugin *self =
      WP_PORTAL_PERMISSIONSTORE_PLUGIN (plugin);

  g_cancellable_cancel (self->cancellable);
  wp_portal_permissionstore_plugin_disable_internal (self);
  g_clear_object (&self->cancellable);
  self->cancellable = g_cancellable_new ();
}

static void
wp_portal_permissionstore_plugin_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec)
{
  WpPortalPermissionStorePlugin *self =
      WP_PORTAL_PERMISSIONSTORE_PLUGIN (object);

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
wp_portal_permissionstore_plugin_class_init (
    WpPortalPermissionStorePluginClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpPluginClass *plugin_class = (WpPluginClass *) klass;

  object_class->finalize = wp_portal_permissionstore_plugin_finalize;
  object_class->get_property = wp_portal_permissionstore_plugin_get_property;

  plugin_class->enable = wp_portal_permissionstore_plugin_enable;
  plugin_class->disable = wp_portal_permissionstore_plugin_disable;

  g_object_class_install_property (object_class, PROP_STATE,
      g_param_spec_enum ("state", "state", "The state",
          WP_TYPE_DBUS_CONNECTION_STATUS, WP_DBUS_CONNECTION_STATUS_CLOSED,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /**
   * WpPortalPermissionStorePlugin::lookup:
   *
   * @brief
   * @em table: the table name
   * @em id: the Id name
   *
   * Returns: (transfer full): the GVariant with permissions
   */
  signals[ACTION_LOOKUP] = g_signal_new_class_handler (
      "lookup", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      (GCallback) wp_portal_permissionstore_plugin_lookup,
      NULL, NULL, NULL, G_TYPE_VARIANT,
      2, G_TYPE_STRING, G_TYPE_STRING);

  /**
   * WpPortalPermissionStorePlugin::set:
   *
   * @brief
   * @em table: the table name
   * @em create: whether to create the table if it does not exist
   * @em id: the Id name
   * @em permissions: the permissions
   *
   * Sets the permissions in the permission store
   */
  signals[ACTION_SET] = g_signal_new_class_handler (
      "set", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      (GCallback) wp_portal_permissionstore_plugin_set,
      NULL, NULL, NULL, G_TYPE_NONE,
      4, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_VARIANT);

  /**
   * WpPortalPermissionStorePlugin::changed:
   *
   * @brief
   * @em table: the table name
   * @em id: the Id name
   * @em deleted: whether the permission was deleted or not
   * @em permissions: the GVariant with permissions
   *
   * Signaled when the permissions changed
   */
  signals[SIGNAL_CHANGED] = g_signal_new (
      "changed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0,
      NULL, NULL, NULL, G_TYPE_NONE, 4,
      G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_VARIANT);
}

WP_PLUGIN_EXPORT gboolean
wireplumber__module_init (WpCore * core, GVariant * args, GError ** error)
{
  wp_plugin_register (g_object_new (wp_portal_permissionstore_plugin_get_type(),
      "name", "portal-permissionstore",
      "core", core,
      NULL));
  return TRUE;
}
