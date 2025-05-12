/* WirePlumber
 *
 * Copyright © 2021 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>
#include "dbus-connection-state.h"

WP_DEFINE_LOCAL_LOG_TOPIC ("m-notification")

#define DBUS_INTERFACE_NAME "org.freedesktop.Notifications"
#define DBUS_OBJECT_PATH "/org/freedesktop/Notifications"

enum
{
  ACTION_GET_DBUS,
  ACTION_SEND,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

struct _WpNotificationsPlugin
{
  WpPlugin parent;

  WpPlugin *dbus;
};

G_DECLARE_FINAL_TYPE (WpNotificationsPlugin,
    wp_notifications_plugin, WP, NOTIFICATIONS_PLUGIN,
    WpPlugin)
G_DEFINE_TYPE (WpNotificationsPlugin, wp_notifications_plugin,
    WP_TYPE_PLUGIN)

static gpointer
wp_notifications_plugin_get_dbus (WpNotificationsPlugin *self)
{
  return self->dbus ? g_object_ref (self->dbus) : NULL;
}

static void
wp_notifications_plugin_send (WpNotificationsPlugin *self,
    const gchar *summary, const gchar *body_message)
{
  g_autoptr (GDBusConnection) conn = NULL;
  g_autoptr (GError) error = NULL;
  g_autoptr (GVariant) res = NULL;
  GVariantBuilder hints;

  g_object_get (self->dbus, "connection", &conn, NULL);
  g_return_if_fail (conn);

  /* Set urgency */
  g_variant_builder_init (&hints, G_VARIANT_TYPE("a{sv}"));
  g_variant_builder_add (&hints, "{sv}", "urgency", g_variant_new_byte (0));

  /* Notify */
  res = g_dbus_connection_call_sync (conn, DBUS_INTERFACE_NAME,
      DBUS_OBJECT_PATH, DBUS_INTERFACE_NAME, "Notify",
      g_variant_new("(susssasa{sv}i)", "wireplumber", 0, "", summary,
          body_message, NULL, &hints, -1), NULL, G_DBUS_CALL_FLAGS_NONE, -1,
          NULL, &error);
  if (error) {
    g_autofree gchar *remote_error = g_dbus_error_get_remote_error (error);
    g_dbus_error_strip_remote_error (error);

    wp_warning_object (self, "Notify: %s (%s)", error->message, remote_error);
    return;
  }
}

static void
wp_notifications_plugin_init (WpNotificationsPlugin * self)
{
}

static void
wp_notifications_plugin_enable (WpPlugin * plugin,
    WpTransition * transition)
{
  WpNotificationsPlugin *self = WP_NOTIFICATIONS_PLUGIN (plugin);
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));

  self->dbus = wp_plugin_find (core, "dbus-connection");
  if (!self->dbus) {
    wp_transition_return_error (transition, g_error_new (WP_DOMAIN_LIBRARY,
        WP_LIBRARY_ERROR_INVARIANT,
        "dbus-connection module must be loaded before notifications"));
    return;
  }

  wp_object_update_features (WP_OBJECT (self), WP_PLUGIN_FEATURE_ENABLED, 0);
}

static void
wp_notifications_plugin_disable (WpPlugin * plugin)
{
  WpNotificationsPlugin *self = WP_NOTIFICATIONS_PLUGIN (plugin);

  g_clear_object (&self->dbus);

  wp_object_update_features (WP_OBJECT (self), 0, WP_PLUGIN_FEATURE_ENABLED);
}

static void
wp_notifications_plugin_class_init (WpNotificationsPluginClass * klass)
{
  WpPluginClass *plugin_class = (WpPluginClass *) klass;

  plugin_class->enable = wp_notifications_plugin_enable;
  plugin_class->disable = wp_notifications_plugin_disable;

  /**
   * WpNotificationsPlugin::get-dbus:
   *
   * Returns: (transfer full): the dbus object
   */
  signals[ACTION_GET_DBUS] = g_signal_new_class_handler (
      "get-dbus", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      (GCallback) wp_notifications_plugin_get_dbus,
      NULL, NULL, NULL,
      G_TYPE_OBJECT, 0);

  /**
   * WpNotificationsPlugin::send:
   *
   * @brief
   * @em summary: the summary
   * @em body_message: The body message
   */
  signals[ACTION_SEND] = g_signal_new_class_handler (
      "send", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      (GCallback) wp_notifications_plugin_send,
      NULL, NULL, NULL, G_TYPE_VARIANT,
      2, G_TYPE_STRING, G_TYPE_STRING);
}

WP_PLUGIN_EXPORT GObject *
wireplumber__module_init (WpCore * core, WpSpaJson * args, GError ** error)
{
  return G_OBJECT (g_object_new (
      wp_notifications_plugin_get_type(),
      "name", "notifications-api",
      "core", core,
      NULL));
}
