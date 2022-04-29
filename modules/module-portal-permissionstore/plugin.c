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
  ACTION_GET_DBUS,
  ACTION_LOOKUP,
  ACTION_SET,
  SIGNAL_CHANGED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

static gpointer
wp_portal_permissionstore_plugin_get_dbus (WpPortalPermissionStorePlugin *self)
{
  return self->dbus ? g_object_ref (self->dbus) : NULL;
}

static GVariant *
wp_portal_permissionstore_plugin_lookup (WpPortalPermissionStorePlugin *self,
    const gchar *table, const gchar *id)
{
  g_autoptr (GDBusConnection) conn = NULL;
  g_autoptr (GError) error = NULL;
  g_autoptr (GVariant) res = NULL;
  GVariant *permissions = NULL, *data = NULL;

  conn = wp_dbus_get_connection (self->dbus);
  g_return_val_if_fail (conn, NULL);

  /* Lookup */
  res = g_dbus_connection_call_sync (conn, DBUS_INTERFACE_NAME,
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
  g_autoptr (GDBusConnection) conn = NULL;
  g_autoptr (GError) error = NULL;
  g_autoptr (GVariant) res = NULL;
  GVariant *data = NULL;

  conn = wp_dbus_get_connection (self->dbus);
  g_return_if_fail (conn);

  /* Set */
  res = g_dbus_connection_call_sync (conn, DBUS_INTERFACE_NAME,
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
clear_signal (WpPortalPermissionStorePlugin *self)
{
  g_autoptr (GDBusConnection) conn = NULL;

  conn = wp_dbus_get_connection (self->dbus);
  if (conn && self->signal_id > 0) {
    g_dbus_connection_signal_unsubscribe (conn, self->signal_id);
    self->signal_id = 0;
  }
}

static void
on_dbus_state_changed (GObject * obj, GParamSpec * spec,
    WpPortalPermissionStorePlugin *self)
{
  WpDBusState state = wp_dbus_get_state (self->dbus);

  switch (state) {
    case WP_DBUS_STATE_CONNECTED: {
      g_autoptr (GDBusConnection) conn = NULL;

      conn = wp_dbus_get_connection (self->dbus);
      g_return_if_fail (conn);

      self->signal_id = g_dbus_connection_signal_subscribe (conn,
          DBUS_INTERFACE_NAME, DBUS_INTERFACE_NAME, "Changed", NULL, NULL,
          G_DBUS_SIGNAL_FLAGS_NONE, wp_portal_permissionstore_plugin_changed,
          self, NULL);
      break;
    }

    case WP_DBUS_STATE_CONNECTING:
    case WP_DBUS_STATE_CLOSED:
      clear_signal (self);
      break;

    default:
      break;
  }
}

static void
wp_portal_permissionstore_plugin_init (WpPortalPermissionStorePlugin * self)
{
}

static void
wp_portal_permissionstore_plugin_constructed (GObject *object)
{
  WpPortalPermissionStorePlugin *self =
      WP_PORTAL_PERMISSIONSTORE_PLUGIN (object);
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));

  self->dbus = wp_dbus_get_instance (core, G_BUS_TYPE_SESSION);
  g_signal_connect_object (self->dbus, "notify::state",
      G_CALLBACK (on_dbus_state_changed), self, 0);

  G_OBJECT_CLASS (wp_portal_permissionstore_plugin_parent_class)->constructed (
      object);
}

static void
wp_portal_permissionstore_plugin_finalize (GObject * object)
{
  WpPortalPermissionStorePlugin *self =
      WP_PORTAL_PERMISSIONSTORE_PLUGIN (object);

  g_clear_object (&self->dbus);

  G_OBJECT_CLASS (wp_portal_permissionstore_plugin_parent_class)->finalize (
      object);
}

static void
on_dbus_activated (GObject * obj, GAsyncResult * res, gpointer user_data)
{
  WpTransition * transition = WP_TRANSITION (user_data);
  WpPortalPermissionStorePlugin * self =
      wp_transition_get_source_object (transition);
  g_autoptr (GError) error = NULL;

  if (!wp_object_activate_finish (WP_OBJECT (obj), res, &error)) {
    wp_transition_return_error (transition, g_steal_pointer (&error));
    return;
  }

  wp_object_update_features (WP_OBJECT (self), WP_PLUGIN_FEATURE_ENABLED, 0);
}

static void
wp_portal_permissionstore_plugin_enable (WpPlugin * plugin,
    WpTransition * transition)
{
  WpPortalPermissionStorePlugin *self =
      WP_PORTAL_PERMISSIONSTORE_PLUGIN (plugin);

  /* make sure dbus always activated */
  g_return_if_fail (self->dbus);
  wp_object_activate (WP_OBJECT (self->dbus), WP_OBJECT_FEATURES_ALL, NULL,
      (GAsyncReadyCallback) on_dbus_activated, transition);
}

static void
wp_portal_permissionstore_plugin_disable (WpPlugin * plugin)
{
  WpPortalPermissionStorePlugin *self =
      WP_PORTAL_PERMISSIONSTORE_PLUGIN (plugin);

  clear_signal (self);

  wp_object_update_features (WP_OBJECT (self), 0, WP_PLUGIN_FEATURE_ENABLED);
}

static void
wp_portal_permissionstore_plugin_class_init (
    WpPortalPermissionStorePluginClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpPluginClass *plugin_class = (WpPluginClass *) klass;

  object_class->constructed = wp_portal_permissionstore_plugin_constructed;
  object_class->finalize = wp_portal_permissionstore_plugin_finalize;

  plugin_class->enable = wp_portal_permissionstore_plugin_enable;
  plugin_class->disable = wp_portal_permissionstore_plugin_disable;

  /**
   * WpPortalPermissionStorePlugin::get-dbus:
   *
   * Returns: (transfer full): the dbus object
   */
  signals[ACTION_GET_DBUS] = g_signal_new_class_handler (
      "get-dbus", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      (GCallback) wp_portal_permissionstore_plugin_get_dbus,
      NULL, NULL, NULL,
      G_TYPE_OBJECT, 0);

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
