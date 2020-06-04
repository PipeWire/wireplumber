/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

/* Generated with gdbus-codegen */
#include "reserve-device-interface.h"

#include <pipewire/pipewire.h>

#include "dbus-device-reservation.h"

#define DEVICE_RESERVATION_SERVICE_PREFIX "org.freedesktop.ReserveDevice1."
#define DEVICE_RESERVATION_OBJECT_PREFIX "/org/freedesktop/ReserveDevice1/"

struct _WpDbusDeviceReservation
{
  GObject parent;

  /* Props */
  gint card_id;
  char *application_name;
  gint priority;
  char *app_dev_name;

  char *service_name;
  char *object_path;
  GDBusConnection *connection;
  guint watcher_id;
  guint owner_id;
  guint registered_id;
  GDBusMethodInvocation *pending_release;

  GTask *pending_task;
  char *pending_property_name;
};

enum {
  PROP_0,
  PROP_CARD_ID,
  PROP_APPLICATION_NAME,
  PROP_PRIORITY,
  PROP_APP_DEV_NAME,
};

enum
{
  SIGNAL_RELEASE,
  SIGNAL_OWNER_APPEARED,
  SIGNAL_OWNER_VANISHED,
  SIGNAL_LAST,
};

static guint device_reservation_signals[SIGNAL_LAST] = { 0 };

G_DEFINE_TYPE (WpDbusDeviceReservation, wp_dbus_device_reservation,
    G_TYPE_OBJECT)

static void
handle_method_call (GDBusConnection *connection, const char *sender,
    const char *object_path, const char *interface_name,
    const char *method_name, GVariant *parameters,
    GDBusMethodInvocation *invocation, gpointer data)
{
  WpDbusDeviceReservation *self = data;

  if (g_strcmp0 (method_name, "RequestRelease") == 0) {
    gint priority;
    g_variant_get (parameters, "(i)", &priority);

    if (priority > self->priority) {
      if (self->pending_release)
        wp_dbus_device_reservation_complete_release (self, FALSE);
      self->pending_release = g_object_ref (invocation);
      g_signal_emit (self, device_reservation_signals[SIGNAL_RELEASE], 0, FALSE);
    } else {
      wp_dbus_device_reservation_complete_release (self, FALSE);
    }
  }
}

static GVariant *
handle_get_property (GDBusConnection *connection, const char *sender,
    const char *object_path, const char *interface_name,
    const char *property, GError **error, gpointer data)
{
  WpDbusDeviceReservation *self = data;
  GVariant *ret = NULL;

  if (g_strcmp0 (property, "ApplicationName") == 0)
    ret = g_variant_new_string (self->application_name ? self->application_name : "");
  else if (g_strcmp0 (property, "ApplicationDeviceName") == 0)
    ret = g_variant_new_string (self->app_dev_name ? self->app_dev_name : "");
  else if (g_strcmp0 (property, "Priority") == 0)
    ret = g_variant_new_int32 (self->priority);

  return ret;
}

static void
on_bus_acquired (GDBusConnection *connection, const gchar *name,
    gpointer user_data)
{
  WpDbusDeviceReservation *self = user_data;
  g_autoptr (GError) error = NULL;
  static const GDBusInterfaceVTable interface_vtable = {
    handle_method_call,
    handle_get_property,
    NULL,  /* Don't allow setting a property */
  };

  wp_debug_object (self, "bus acquired");

  self->registered_id = g_dbus_connection_register_object (connection,
      self->object_path,
      wp_org_freedesktop_reserve_device1_interface_info (),
      &interface_vtable,
      g_object_ref (self),
      g_object_unref,
      &error);
  g_return_if_fail (!error);
  g_return_if_fail (self->registered_id > 0);
}

static void
on_name_acquired (GDBusConnection *connection, const gchar *name,
    gpointer user_data)
{
  WpDbusDeviceReservation *self = user_data;
  wp_debug_object (self, "name acquired");

  self->connection = connection;
}

static void
wp_dbus_device_reservation_unregister_object (WpDbusDeviceReservation *self)
{
  if (self->connection && self->registered_id > 0) {
    g_dbus_connection_unregister_object (self->connection, self->registered_id);
    self->registered_id = 0;
    self->connection = NULL;
  }
}

static void
on_name_lost (GDBusConnection *connection, const gchar *name,
    gpointer user_data)
{
  WpDbusDeviceReservation *self = user_data;
  wp_debug_object (self, "name lost");

  self->connection = connection;

  /* Unregister object */
  wp_dbus_device_reservation_unregister_object (self);

  /* Emit release signal with forced set to TRUE */
  g_signal_emit (self, device_reservation_signals[SIGNAL_RELEASE], 0, TRUE);
}

static void
on_name_appeared (GDBusConnection *connection, const gchar *name,
    const gchar *owner, gpointer user_data)
{
  WpDbusDeviceReservation *self = user_data;
  g_signal_emit (self, device_reservation_signals[SIGNAL_OWNER_APPEARED], 0,
      owner);
}

static void
on_name_vanished (GDBusConnection *connection, const gchar *name,
    gpointer user_data)
{
  WpDbusDeviceReservation *self = user_data;
  g_signal_emit (self, device_reservation_signals[SIGNAL_OWNER_VANISHED], 0);
}

static void
wp_dbus_device_reservation_constructed (GObject * object)
{
  WpDbusDeviceReservation *self = WP_DBUS_DEVICE_RESERVATION (object);

  /* Set service name and object path */
  self->service_name = g_strdup_printf (DEVICE_RESERVATION_SERVICE_PREFIX
      "Audio%d", self->card_id);
  self->object_path = g_strdup_printf (DEVICE_RESERVATION_OBJECT_PREFIX
      "Audio%d", self->card_id);

  /* Watch for the name */
  self->watcher_id = g_bus_watch_name (G_BUS_TYPE_SESSION, self->service_name,
      G_BUS_NAME_WATCHER_FLAGS_NONE, on_name_appeared, on_name_vanished, self,
      NULL);
  g_return_if_fail (self->watcher_id > 0);

  G_OBJECT_CLASS (wp_dbus_device_reservation_parent_class)->constructed (object);
}

static void
wp_dbus_device_reservation_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpDbusDeviceReservation *self = WP_DBUS_DEVICE_RESERVATION (object);

  switch (property_id) {
  case PROP_CARD_ID:
    g_value_set_int (value, self->card_id);
    break;
  case PROP_APPLICATION_NAME:
    g_value_set_string (value, self->application_name);
    break;
  case PROP_PRIORITY:
    g_value_set_int (value, self->priority);
    break;
  case PROP_APP_DEV_NAME:
    g_value_set_string (value, self->app_dev_name);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_dbus_device_reservation_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec)
{
  WpDbusDeviceReservation *self = WP_DBUS_DEVICE_RESERVATION (object);

  switch (property_id) {
  case PROP_CARD_ID:
    self->card_id = g_value_get_int (value);
    break;
  case PROP_APPLICATION_NAME:
    g_clear_pointer (&self->application_name, g_free);
    self->application_name = g_value_dup_string (value);
    break;
  case PROP_PRIORITY:
    self->priority = g_value_get_int(value);
    break;
  case PROP_APP_DEV_NAME:
    g_clear_pointer (&self->app_dev_name, g_free);
    self->app_dev_name = g_value_dup_string (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_dbus_device_reservation_finalize (GObject * object)
{
  WpDbusDeviceReservation *self = WP_DBUS_DEVICE_RESERVATION (object);

  /* Finish pending task */
  if (self->pending_task) {
    g_autoptr (GTask) task = g_steal_pointer (&self->pending_task);
    GError *error = g_error_new (WP_DOMAIN_LIBRARY,
        WP_LIBRARY_ERROR_OPERATION_FAILED, "finishing before task is done");
    g_task_return_error (task, error);
  }
  g_clear_pointer (&self->pending_property_name, g_free);

  /* Unwatch the name */
  if (self->watcher_id > 0)
    g_bus_unwatch_name (self->watcher_id);

  /* Unregister and release */
  wp_dbus_device_reservation_unregister_object (self);
  wp_dbus_device_reservation_release (self);

  g_clear_object (&self->pending_release);
  g_clear_pointer (&self->service_name, g_free);
  g_clear_pointer (&self->object_path, g_free);

  /* Props */
  g_clear_pointer (&self->application_name, g_free);
  g_clear_pointer (&self->app_dev_name, g_free);

  G_OBJECT_CLASS ( wp_dbus_device_reservation_parent_class)->finalize (object);
}

static void
wp_dbus_device_reservation_init (WpDbusDeviceReservation * self)
{
}

static void
wp_dbus_device_reservation_class_init (WpDbusDeviceReservationClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;

  object_class->constructed = wp_dbus_device_reservation_constructed;
  object_class->get_property = wp_dbus_device_reservation_get_property;
  object_class->set_property = wp_dbus_device_reservation_set_property;
  object_class->finalize = wp_dbus_device_reservation_finalize;

  /* Properties */
  g_object_class_install_property (object_class, PROP_CARD_ID,
      g_param_spec_int ("card-id", "card-id",
          "The card Id", G_MININT, G_MAXINT, -1,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class, PROP_APPLICATION_NAME,
      g_param_spec_string ("application-name", "application-name",
          "The application name", NULL,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class, PROP_PRIORITY,
      g_param_spec_int ("priority", "priority",
          "The priority", G_MININT, G_MAXINT, 0,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class, PROP_APP_DEV_NAME,
      g_param_spec_string ("app-dev-name", "app-dev-name",
          "The application device name", NULL,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  /* Signals */
  device_reservation_signals[SIGNAL_RELEASE] = g_signal_new (
      "release", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_FIRST,
      0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
  device_reservation_signals[SIGNAL_OWNER_APPEARED] = g_signal_new (
      "owner-appeared", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_FIRST,
      0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_STRING);
  device_reservation_signals[SIGNAL_OWNER_VANISHED] = g_signal_new (
      "owner-vanished", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_FIRST,
      0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

WpDbusDeviceReservation *
wp_dbus_device_reservation_new (gint card_id, const char *application_name,
    gint priority, const char *app_dev_name)
{
  return g_object_new (WP_TYPE_DBUS_DEVICE_RESERVATION,
      "card-id", card_id,
      "application-name", application_name,
      "priority", priority,
      "app-dev-name", app_dev_name,
      NULL);
}

void
wp_dbus_device_reservation_release (WpDbusDeviceReservation *self)
{
  g_return_if_fail (WP_IS_DBUS_DEVICE_RESERVATION (self));
  if (self->owner_id == 0)
    return;

  /* Release */
  g_bus_unown_name (self->owner_id);
  self->owner_id = 0;
}

void
wp_dbus_device_reservation_complete_release (WpDbusDeviceReservation *self,
    gboolean res)
{
  g_return_if_fail (WP_IS_DBUS_DEVICE_RESERVATION (self));

  if (!self->pending_release)
    return;

  g_dbus_method_invocation_return_value (self->pending_release,
      g_variant_new ("(b)", res));
  g_clear_object (&self->pending_release);
}

static void
on_unowned (gpointer user_data)
{
  WpDbusDeviceReservation *self = user_data;
  wp_dbus_device_reservation_unregister_object (self);
  g_object_unref (self);
}

gboolean
wp_dbus_device_reservation_acquire (WpDbusDeviceReservation *self)
{
  g_return_val_if_fail (WP_IS_DBUS_DEVICE_RESERVATION (self), FALSE);
  g_return_val_if_fail (!self->pending_task, FALSE);
  if (self->owner_id > 0)
    return FALSE;

  /* Aquire */
  self->owner_id = g_bus_own_name (G_BUS_TYPE_SESSION, self->service_name,
      self->priority < INT32_MAX ?
      G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT : G_BUS_NAME_OWNER_FLAGS_NONE,
      on_bus_acquired, on_name_acquired, on_name_lost, g_object_ref (self),
      on_unowned);
  return self->owner_id > 0;
}

static void
on_request_release_done (GObject *proxy, GAsyncResult *res, gpointer user_data)
{
  WpDbusDeviceReservation *self = user_data;
  g_autoptr (GError) error = NULL;
  g_autoptr (GTask) task = g_steal_pointer (&self->pending_task);
  gboolean ret;

  g_return_if_fail (task);

  /* Finish */
  wp_org_freedesktop_reserve_device1_call_request_release_finish (
      WP_ORG_FREEDESKTOP_RESERVE_DEVICE1 (proxy),
      &ret, res, &error);
  if (error)
    g_task_return_error (task, g_steal_pointer (&error));
  else
    g_task_return_pointer (task, GUINT_TO_POINTER (ret), NULL);
}

static void
on_proxy_done_request_release (GObject *obj, GAsyncResult *res, gpointer data)
{
  WpDbusDeviceReservation *self = data;
  g_autoptr (WpOrgFreedesktopReserveDevice1) proxy = NULL;
  g_autoptr (GError) error = NULL;
  g_return_if_fail (self->pending_task);

  /* Finish */
  proxy = wp_org_freedesktop_reserve_device1_proxy_new_for_bus_finish (
      res, &error);
  if (error) {
    g_autoptr (GTask) task = g_steal_pointer (&self->pending_task);
    g_task_return_error (task, g_steal_pointer (&error));
    return;
  }

  /* Request release */
  g_return_if_fail (proxy);
  wp_org_freedesktop_reserve_device1_call_request_release (proxy,
     self->priority, NULL, on_request_release_done, self);
}

gboolean
wp_dbus_device_reservation_request_release (WpDbusDeviceReservation *self,
    GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
  g_return_val_if_fail (WP_IS_DBUS_DEVICE_RESERVATION (self), FALSE);
  g_return_val_if_fail (!self->pending_task, FALSE);

  /* Set the new task */
  self->pending_task = g_task_new (self, cancellable, callback, user_data);

  /* Get the proxy */
  wp_org_freedesktop_reserve_device1_proxy_new_for_bus (
      G_BUS_TYPE_SESSION, G_BUS_NAME_OWNER_FLAGS_NONE, self->service_name,
      self->object_path, NULL, on_proxy_done_request_release, self);
  return TRUE;
}

static void
on_request_property_done (GObject *proxy, GAsyncResult *res, gpointer data)
{
  WpDbusDeviceReservation *self = data;
  g_autoptr (GError) error = NULL;
  g_autoptr (GTask) task = g_steal_pointer (&self->pending_task);
  g_autoptr (GVariant) ret = NULL;
  g_autoptr (GVariant) var = NULL;
  g_return_if_fail (task);

  /* Finish */
  ret = g_dbus_proxy_call_finish (G_DBUS_PROXY (proxy), res, &error);
  if (!ret) {
    GError *error = g_error_new (WP_DOMAIN_LIBRARY,
        WP_LIBRARY_ERROR_OPERATION_FAILED, "failed to get property '%s' on proxy",
        self->pending_property_name);
    g_task_return_error (task, error);
  }

  /* Get the property value */
  g_variant_get (ret, "(v)", &var);
  g_return_if_fail (var);
  if (g_strcmp0 (self->pending_property_name, "ApplicationName") == 0) {
    g_task_return_pointer (task, g_variant_dup_string (var, NULL), NULL);
  }
  else if (g_strcmp0 (self->pending_property_name, "ApplicationDeviceName") == 0) {
    gchar *v = NULL;
    g_variant_get (ret, "(v)", &v);
    g_task_return_pointer (task, g_variant_dup_string (var, NULL), NULL);
  }
  else if (g_strcmp0 (self->pending_property_name, "Priority") == 0) {
    g_task_return_pointer (task, GINT_TO_POINTER (g_variant_get_int32 (var)), NULL);
  }
  else {
    GError *error = g_error_new (WP_DOMAIN_LIBRARY,
        WP_LIBRARY_ERROR_OPERATION_FAILED, "invalid property '%s' on proxy",
        self->pending_property_name);
    g_task_return_error (task, error);
  }
}

static void
on_proxy_done_request_property (GObject *obj, GAsyncResult *res, gpointer data)
{
  WpDbusDeviceReservation *self = data;
  g_autoptr (WpOrgFreedesktopReserveDevice1) proxy = NULL;
  g_autoptr (GError) error = NULL;
  g_return_if_fail (self->pending_task);

  /* Finish */
  proxy = wp_org_freedesktop_reserve_device1_proxy_new_for_bus_finish (res,
      &error);
  if (error) {
    g_autoptr (GTask) task = g_steal_pointer (&self->pending_task);
    g_task_return_error (task, g_steal_pointer (&error));
    return;
  }

  /* Request the property call the Get method */
  g_return_if_fail (proxy);
  g_return_if_fail (self->pending_property_name);
  g_dbus_proxy_call (
      G_DBUS_PROXY (proxy),
      "org.freedesktop.DBus.Properties.Get",
      g_variant_new ("(ss)", "org.freedesktop.ReserveDevice1", self->pending_property_name),
      G_DBUS_CALL_FLAGS_NONE,
      -1,
      NULL,
      on_request_property_done,
      self);
}

gboolean
wp_dbus_device_reservation_request_property (WpDbusDeviceReservation *self,
    const char *name, GCancellable *cancellable, GAsyncReadyCallback callback,
    gpointer user_data)
{
  g_return_val_if_fail (WP_IS_DBUS_DEVICE_RESERVATION (self), FALSE);
  g_return_val_if_fail (!self->pending_task, FALSE);

  /* Set the new task and property name */
  self->pending_task = g_task_new (self, cancellable, callback, user_data);
  g_clear_pointer (&self->pending_property_name, g_free);
  self->pending_property_name = g_strdup (name);

  /* Get the proxy */
  wp_org_freedesktop_reserve_device1_proxy_new_for_bus (
      G_BUS_TYPE_SESSION, G_BUS_NAME_OWNER_FLAGS_NONE, self->service_name,
      self->object_path, NULL, on_proxy_done_request_property, self);
  return TRUE;
}

gpointer
wp_dbus_device_reservation_async_finish (WpDbusDeviceReservation *self,
    GAsyncResult * res, GError ** error)
{
  g_return_val_if_fail (WP_IS_DBUS_DEVICE_RESERVATION (self), FALSE);
  g_return_val_if_fail (g_task_is_valid (res, self), FALSE);

  return g_task_propagate_pointer (G_TASK (res), error);
}
