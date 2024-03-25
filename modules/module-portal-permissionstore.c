/* WirePlumber
 *
 * Copyright © 2021 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>
#include "dbus-connection-state.h"

WP_DEFINE_LOCAL_LOG_TOPIC ("m-portal-permissionstore")

#define DBUS_INTERFACE_NAME "org.freedesktop.impl.portal.PermissionStore"
#define DBUS_OBJECT_PATH "/org/freedesktop/impl/portal/PermissionStore"

enum
{
  ACTION_GET_DBUS,
  ACTION_LOOKUP,
  ACTION_SET,
  SIGNAL_CHANGED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

struct _WpPortalPermissionStorePlugin
{
  WpPlugin parent;

  WpPlugin *dbus;
  guint signal_id;
};

G_DECLARE_FINAL_TYPE (WpPortalPermissionStorePlugin,
    wp_portal_permissionstore_plugin, WP, PORTAL_PERMISSIONSTORE_PLUGIN,
    WpPlugin)
G_DEFINE_TYPE (WpPortalPermissionStorePlugin, wp_portal_permissionstore_plugin,
    WP_TYPE_PLUGIN)

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

  g_object_get (self->dbus, "connection", &conn, NULL);
  g_return_val_if_fail (conn, NULL);

  /* Lookup */
  res = g_dbus_connection_call_sync (conn, DBUS_INTERFACE_NAME,
      DBUS_OBJECT_PATH, DBUS_INTERFACE_NAME, "Lookup",
      g_variant_new ("(ss)", table, id), NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL,
      &error);
  if (error) {
    g_autofree gchar *remote_error = g_dbus_error_get_remote_error (error);
    g_dbus_error_strip_remote_error (error);

    /* NotFound is neither unexpected nor important, so log it as INFO */
    if (!g_strcmp0 (remote_error, "org.freedesktop.portal.Error.NotFound")) {
      wp_info_object (self, "Lookup: %s (%s)", error->message, remote_error);
      return NULL;
    }

    wp_warning_object (self, "Lookup: %s (%s)", error->message, remote_error);
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

  g_object_get (self->dbus, "connection", &conn, NULL);
  g_return_if_fail (conn);

  /* Set */
  res = g_dbus_connection_call_sync (conn, DBUS_INTERFACE_NAME,
      DBUS_OBJECT_PATH, DBUS_INTERFACE_NAME, "Set",
      g_variant_new ("(sbs@a{sas}@v)", table, id, permissions, data), NULL,
      G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
  if (error) {
    g_autofree gchar *remote_error = g_dbus_error_get_remote_error (error);
    g_dbus_error_strip_remote_error (error);
    wp_warning_object (self, "Set: %s (%s)", error->message, remote_error);
  }
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

  g_object_get (self->dbus, "connection", &conn, NULL);
  if (conn && self->signal_id > 0) {
    g_dbus_connection_signal_unsubscribe (conn, self->signal_id);
    self->signal_id = 0;
  }
}

static void
on_dbus_state_changed (GObject * dbus, GParamSpec * spec,
    WpPortalPermissionStorePlugin *self)
{
  WpDBusConnectionState state = -1;
  g_object_get (dbus, "state", &state, NULL);

  switch (state) {
    case WP_DBUS_CONNECTION_STATE_CONNECTED: {
      g_autoptr (GDBusConnection) conn = NULL;

      g_object_get (dbus, "connection", &conn, NULL);
      g_return_if_fail (conn);

      self->signal_id = g_dbus_connection_signal_subscribe (conn,
          DBUS_INTERFACE_NAME, DBUS_INTERFACE_NAME, "Changed", NULL, NULL,
          G_DBUS_SIGNAL_FLAGS_NONE, wp_portal_permissionstore_plugin_changed,
          self, NULL);
      break;
    }

    case WP_DBUS_CONNECTION_STATE_CONNECTING:
    case WP_DBUS_CONNECTION_STATE_CLOSED:
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
wp_portal_permissionstore_plugin_enable (WpPlugin * plugin,
    WpTransition * transition)
{
  WpPortalPermissionStorePlugin *self =
      WP_PORTAL_PERMISSIONSTORE_PLUGIN (plugin);
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));

  self->dbus = wp_plugin_find (core, "dbus-connection");
  if (!self->dbus) {
    wp_transition_return_error (transition, g_error_new (WP_DOMAIN_LIBRARY,
        WP_LIBRARY_ERROR_INVARIANT,
        "dbus-connection module must be loaded before portal-permissionstore"));
    return;
  }

  g_signal_connect_object (self->dbus, "notify::state",
       G_CALLBACK (on_dbus_state_changed), self, 0);
  on_dbus_state_changed (G_OBJECT (self->dbus), NULL, self);

  wp_object_update_features (WP_OBJECT (self), WP_PLUGIN_FEATURE_ENABLED, 0);
}

static void
wp_portal_permissionstore_plugin_disable (WpPlugin * plugin)
{
  WpPortalPermissionStorePlugin *self =
      WP_PORTAL_PERMISSIONSTORE_PLUGIN (plugin);

  clear_signal (self);
  g_clear_object (&self->dbus);

  wp_object_update_features (WP_OBJECT (self), 0, WP_PLUGIN_FEATURE_ENABLED);
}

static void
wp_portal_permissionstore_plugin_class_init (
    WpPortalPermissionStorePluginClass * klass)
{
  WpPluginClass *plugin_class = (WpPluginClass *) klass;

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

WP_PLUGIN_EXPORT GObject *
wireplumber__module_init (WpCore * core, WpSpaJson * args, GError ** error)
{
  return G_OBJECT (g_object_new (
      wp_portal_permissionstore_plugin_get_type(),
      "name", "portal-permissionstore",
      "core", core,
      NULL));
}
