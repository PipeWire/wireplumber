/* WirePlumber
 *
 * Copyright © 2021 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "reserve-device.h"
#include "plugin.h"
#include "transitions.h"
#include "reserve-device-interface.h"
#include "reserve-device-enums.h"

/*
 * WpReserveDevice:
 */
G_DEFINE_TYPE (WpReserveDevice, wp_reserve_device, G_TYPE_OBJECT)

enum
{
  ACTION_ACQUIRE,
  ACTION_RELEASE,
  ACTION_DENY_RELEASE,
  SIGNAL_RELEASE_REQUESTED,
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_PLUGIN,
  PROP_NAME,
  PROP_APP_NAME,
  PROP_APP_DEV_NAME,
  PROP_PRIORITY,
  PROP_STATE,
  PROP_OWNER_APP_NAME,
};

static guint signals[LAST_SIGNAL] = { 0 };

static void
wp_reserve_device_init (WpReserveDevice * self)
{
  g_weak_ref_init (&self->plugin, NULL);
  g_weak_ref_init (&self->transition, NULL);
}

static void
on_got_proxy (GObject * src, GAsyncResult * res, WpReserveDevice *self)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (WpOrgFreedesktopReserveDevice1) proxy =
      wp_org_freedesktop_reserve_device1_proxy_new_finish (res, &error);
  if (!proxy) {
    wp_info_object (self, "%s: Could not get proxy of remote reservation: %s",
        self->name, error->message);
    return;
  }

  wp_debug_object (self, "%s owned by: %s", self->name,
      wp_org_freedesktop_reserve_device1_get_application_name (proxy));

  /* ensure that we are still busy and there is no owner_app_name */
  if (self->state == WP_RESERVE_DEVICE_STATE_BUSY && !self->owner_app_name) {
    self->owner_app_name =
        wp_org_freedesktop_reserve_device1_dup_application_name (proxy);
    g_object_notify (G_OBJECT (self), "owner-application-name");
  }
}

static void
update_owner_app_name (WpReserveDevice *self)
{
  if (self->state == WP_RESERVE_DEVICE_STATE_BUSY && !self->owner_app_name) {
    /* create proxy */
    g_autoptr (WpReserveDevicePlugin) plugin = g_weak_ref_get (&self->plugin);
    wp_org_freedesktop_reserve_device1_proxy_new (plugin->connection,
        G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS |
        G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
        self->service_name, self->object_path, NULL,
        (GAsyncReadyCallback) on_got_proxy, self);
  }
  else if (self->state != WP_RESERVE_DEVICE_STATE_BUSY && self->owner_app_name) {
    g_clear_pointer (&self->owner_app_name, g_free);
    g_object_notify (G_OBJECT (self), "owner-application-name");
  }
}

static void
on_name_appeared (GDBusConnection *connection, const gchar *name,
    const gchar *owner, gpointer user_data)
{
  WpReserveDevice *self = WP_RESERVE_DEVICE (user_data);
  g_autoptr (WpTransition) t = g_weak_ref_get (&self->transition);

  if (!t || wp_transition_get_completed (t)) {
    self->state = WP_RESERVE_DEVICE_STATE_BUSY;
    wp_info_object (self, "%s busy (by %s)", name, owner);
    g_object_notify (G_OBJECT (self), "state");
    update_owner_app_name (self);
  }
}

static void
on_name_vanished (GDBusConnection *connection, const gchar *name,
    gpointer user_data)
{
  WpReserveDevice *self = WP_RESERVE_DEVICE (user_data);
  g_autoptr (WpTransition) t = g_weak_ref_get (&self->transition);

  if (!t || wp_transition_get_completed (t)) {
    self->state = WP_RESERVE_DEVICE_STATE_AVAILABLE;
    wp_info_object (self, "%s released", name);
    g_object_notify (G_OBJECT (self), "state");
    update_owner_app_name (self);
  }
}

static void
wp_reserve_device_constructed (GObject * object)
{
  WpReserveDevice *self = WP_RESERVE_DEVICE (object);
  g_autoptr (WpReserveDevicePlugin) plugin = g_weak_ref_get (&self->plugin);

  self->service_name =
      g_strdup_printf (FDO_RESERVE_DEVICE1_SERVICE ".%s", self->name);
  self->object_path =
      g_strdup_printf (FDO_RESERVE_DEVICE1_PATH "/%s", self->name);

  /* Watch for the name */
  self->watcher_id = g_bus_watch_name_on_connection (plugin->connection,
      self->service_name, G_BUS_NAME_WATCHER_FLAGS_NONE,
      on_name_appeared, on_name_vanished, self, NULL);

  G_OBJECT_CLASS (wp_reserve_device_parent_class)->constructed (object);
}

static void
wp_reserve_device_finalize (GObject * object)
{
  WpReserveDevice *self = WP_RESERVE_DEVICE (object);

  if (self->watcher_id > 0)
    g_bus_unwatch_name (self->watcher_id);
  if (self->owner_id > 0)
    g_bus_unown_name (self->owner_id);

  g_weak_ref_clear (&self->plugin);
  g_weak_ref_clear (&self->transition);
  g_clear_pointer (&self->name, g_free);
  g_clear_pointer (&self->app_name, g_free);
  g_clear_pointer (&self->app_dev_name, g_free);
  g_clear_pointer (&self->service_name, g_free);
  g_clear_pointer (&self->object_path, g_free);

  G_OBJECT_CLASS (wp_reserve_device_parent_class)->finalize (object);
}

static void
on_acquire_transition_done (GObject *rd, GAsyncResult *res, gpointer data)
{
  WpReserveDevice *self = WP_RESERVE_DEVICE (data);
  g_autoptr (GError) error = NULL;

  gboolean acquired = wp_reserve_device_acquire_transition_finish (res, &error);
  if (error) {
    wp_message_object (self, "%s: Acquire error: %s", self->name,
        error->message);
  }

  self->state = acquired ?
      WP_RESERVE_DEVICE_STATE_ACQUIRED : WP_RESERVE_DEVICE_STATE_BUSY;
  g_object_notify (G_OBJECT (self), "state");
  update_owner_app_name (self);
}

static void
wp_reserve_device_acquire (WpReserveDevice * self)
{
  g_autoptr (WpTransition) t = g_weak_ref_get (&self->transition);
  if (self->state == WP_RESERVE_DEVICE_STATE_ACQUIRED ||
    (t && !wp_transition_get_completed (t))) {
    wp_debug_object (self, "%s: already acquired or operation in progress",
        self->name);
    return;
  }

  g_autoptr (WpReserveDevicePlugin) plugin = g_weak_ref_get (&self->plugin);
  WpTransition *transition = wp_reserve_device_acquire_transition_new (self,
      plugin->cancellable, on_acquire_transition_done, self);
  g_weak_ref_set (&self->transition, transition);
  wp_transition_advance (transition);
}

static void
wp_reserve_device_release (WpReserveDevice * self)
{
  if (self->state != WP_RESERVE_DEVICE_STATE_ACQUIRED) {
    wp_debug_object (self, "%s: not acquired", self->name);
    return;
  }

  /* set state to AVAILABLE to ensure that on_name_lost()
     does not emit SIGNAL_REQUEST_RELEASE */
  /* on_name_vanished() will emit the state change */
  self->state = WP_RESERVE_DEVICE_STATE_AVAILABLE;
  wp_reserve_device_unown_name (self);

  if (self->req_rel_invocation) {
    wp_org_freedesktop_reserve_device1_complete_request_release (NULL,
        self->req_rel_invocation, TRUE);
    self->req_rel_invocation = NULL;
  }
}

static void
wp_reserve_device_deny_release (WpReserveDevice * self, gboolean success)
{
  if (self->req_rel_invocation) {
    wp_org_freedesktop_reserve_device1_complete_request_release (NULL,
        self->req_rel_invocation, FALSE);
    self->req_rel_invocation = NULL;
  }
}

static gboolean
wp_reserve_device_handle_request_release (WpOrgFreedesktopReserveDevice1 *iface,
    GDBusMethodInvocation *invocation, gint priority, gpointer user_data)
{
  WpReserveDevice *self = WP_RESERVE_DEVICE (user_data);

  /* deny release if the priority is lower than ours */
  if (priority < self->priority) {
    wp_org_freedesktop_reserve_device1_complete_request_release (iface,
        g_object_ref (invocation), FALSE);
    return TRUE;
  }

  /* else, request the release of the device from the implementation;
     if signal handlers are connected, assume the functionality is implemented,
     otherwise return FALSE to let the iface return UnknownMethod */
  if (g_signal_has_handler_pending (self,
          signals[SIGNAL_RELEASE_REQUESTED], 0, FALSE)) {
    self->req_rel_invocation = g_object_ref (invocation);
    g_signal_emit (self, signals[SIGNAL_RELEASE_REQUESTED], 0, FALSE);
    return TRUE;
  }
  return FALSE;
}

static void
wp_reserve_device_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpReserveDevice *self = WP_RESERVE_DEVICE (object);

  switch (property_id) {
  case PROP_NAME:
    g_value_set_string (value, self->name);
    break;
  case PROP_APP_NAME:
    g_value_set_string (value, self->app_name);
    break;
  case PROP_APP_DEV_NAME:
    g_value_set_string (value, self->app_dev_name);
    break;
  case PROP_PRIORITY:
    g_value_set_int (value, self->priority);
    break;
  case PROP_STATE:
    g_value_set_enum (value, self->state);
    break;
  case PROP_OWNER_APP_NAME:
    switch (self->state) {
    case WP_RESERVE_DEVICE_STATE_ACQUIRED:
      g_value_set_string (value, self->app_name);
      break;
    default:
      g_value_set_string (value, self->owner_app_name);
      break;
    }
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_reserve_device_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec)
{
  WpReserveDevice *self = WP_RESERVE_DEVICE (object);

  switch (property_id) {
  case PROP_PLUGIN:
    g_weak_ref_set (&self->plugin, g_value_get_object (value));
    break;
  case PROP_NAME:
    g_clear_pointer (&self->name, g_free);
    self->name = g_value_dup_string (value);
    break;
  case PROP_APP_NAME:
    g_clear_pointer (&self->app_name, g_free);
    self->app_name = g_value_dup_string (value);
    break;
  case PROP_APP_DEV_NAME:
    g_clear_pointer (&self->app_dev_name, g_free);
    self->app_dev_name = g_value_dup_string (value);
    break;
  case PROP_PRIORITY:
    self->priority = g_value_get_int(value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

/**
* wp_reserve_device_class_init
*
* @param klass: the reserve device class
*/
static void
wp_reserve_device_class_init (WpReserveDeviceClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;

  object_class->constructed = wp_reserve_device_constructed;
  object_class->finalize = wp_reserve_device_finalize;
  object_class->get_property = wp_reserve_device_get_property;
  object_class->set_property = wp_reserve_device_set_property;

  g_object_class_install_property (object_class, PROP_PLUGIN,
      g_param_spec_object ("plugin", "plugin",
          "The parent plugin instance", wp_reserve_device_plugin_get_type (),
          G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_NAME,
      g_param_spec_string ("name", "name",
          "The reservation name", NULL,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_APP_NAME,
      g_param_spec_string ("application-name", "application-name",
          "The application name", NULL,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_APP_DEV_NAME,
      g_param_spec_string ("application-device-name", "application-device-name",
          "The application device name", NULL,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_PRIORITY,
      g_param_spec_int ("priority", "priority",
          "The priority", G_MININT, G_MAXINT, 0,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_STATE,
      g_param_spec_enum ("state", "state", "The state",
          WP_TYPE_RESERVE_DEVICE_STATE, WP_RESERVE_DEVICE_STATE_UNKNOWN,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_OWNER_APP_NAME,
      g_param_spec_string ("owner-application-name", "owner-application-name",
          "The owner application name", NULL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /**
   * WpReserveDevice acquire:
   *
   * @section signal_acquire_section acquire
   */
  signals[ACTION_ACQUIRE] = g_signal_new_class_handler (
      "acquire", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      (GCallback) wp_reserve_device_acquire,
      NULL, NULL, NULL, G_TYPE_NONE, 0);

  /**
   * WpReserveDevice release:
   *
   * @section signal_release_section release
   */
  signals[ACTION_RELEASE] = g_signal_new_class_handler (
      "release", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      (GCallback) wp_reserve_device_release,
      NULL, NULL, NULL, G_TYPE_NONE, 0);

  /**
   * WpReserveDevice deny-release:
   *
   * @section signal_deny_release_section deny-release
   */
  signals[ACTION_DENY_RELEASE] = g_signal_new_class_handler (
      "deny-release", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      (GCallback) wp_reserve_device_deny_release,
      NULL, NULL, NULL, G_TYPE_NONE, 0);

  /**
   * WpReserveDevice release-requested:
   *
   * @section signal_release_requested_section release-requested
   *
   * @em forced: %TRUE if the name was forcibly taken from us,
   *    %FALSE if the `RequestRelease()` d-bus method was called
   *
   * @brief Signaled when the device needs to be released. If @em forced is %FALSE,
   * call [release](@ref signal_release_section) to release or
   * [deny-release](@ref signal_deny_release_section)
   * to refuse and return %FALSE from the `RequestRelease()` d-bus method.
   */
  signals[SIGNAL_RELEASE_REQUESTED] = g_signal_new (
      "release-requested", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
      G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
}

void
wp_reserve_device_export_object (WpReserveDevice *self)
{
  g_autoptr (WpReserveDevicePlugin) plugin = g_weak_ref_get (&self->plugin);
  if (plugin) {
    g_autoptr (GDBusObjectSkeleton) skeleton =
        g_dbus_object_skeleton_new (self->object_path);
    g_autoptr (WpOrgFreedesktopReserveDevice1) iface =
        wp_org_freedesktop_reserve_device1_skeleton_new ();
    g_object_set (iface,
        "priority", self->priority,
        "application-name", self->app_name,
        "application-device-name", self->app_dev_name,
        NULL);
    g_signal_connect_object (iface, "handle-request-release",
        (GCallback) wp_reserve_device_handle_request_release, self, 0);
    g_dbus_object_skeleton_add_interface (skeleton,
        G_DBUS_INTERFACE_SKELETON (iface));
    g_dbus_object_manager_server_export (plugin->manager, skeleton);
  }
}

void
wp_reserve_device_unexport_object (WpReserveDevice *self)
{
  g_autoptr (WpReserveDevicePlugin) plugin = g_weak_ref_get (&self->plugin);
  if (plugin) {
    wp_debug_object (self, "unexport %s", self->object_path);
    g_dbus_object_manager_server_unexport (plugin->manager, self->object_path);
  }
}

static void
on_name_acquired (GDBusConnection *connection, const gchar *name,
    gpointer user_data)
{
  WpReserveDevice *self = WP_RESERVE_DEVICE (user_data);
  g_autoptr (WpTransition) t = g_weak_ref_get (&self->transition);

  wp_debug_object (self, "%s acquired", name);

  if (t)
    wp_reserve_device_acquire_transition_name_acquired (t);
}

static void
on_name_lost (GDBusConnection *connection, const gchar *name,
    gpointer user_data)
{
  WpReserveDevice *self = WP_RESERVE_DEVICE (user_data);
  g_autoptr (WpTransition) t = g_weak_ref_get (&self->transition);

  wp_debug_object (self, "%s lost", name);

  if (t) {
    wp_reserve_device_acquire_transition_name_lost (t);
    return;
  }

  if (self->state == WP_RESERVE_DEVICE_STATE_ACQUIRED) {
    /* Emit release signal with forced set to TRUE */
    g_signal_emit (self, signals[SIGNAL_RELEASE_REQUESTED], 0, TRUE);
    wp_reserve_device_unown_name (self);
  }

  wp_reserve_device_unexport_object (self);
}

void
wp_reserve_device_own_name (WpReserveDevice * self, gboolean force)
{
  g_return_if_fail (self->owner_id == 0);

  g_autoptr (WpReserveDevicePlugin) plugin = g_weak_ref_get (&self->plugin);
  if (plugin) {
    GBusNameOwnerFlags flags = G_BUS_NAME_OWNER_FLAGS_DO_NOT_QUEUE;
    if (self->priority != G_MAXINT32)
      flags |= G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT;
    if (force)
      flags |= G_BUS_NAME_OWNER_FLAGS_REPLACE;

    wp_debug_object (self, "request ownership of %s", self->service_name);

    self->owner_id = g_bus_own_name_on_connection (plugin->connection,
        self->service_name, flags, on_name_acquired, on_name_lost, self, NULL);
  }
}

void
wp_reserve_device_unown_name (WpReserveDevice * self)
{
  if (self->owner_id) {
    wp_debug_object (self, "drop ownership of %s", self->service_name);
    g_bus_unown_name (self->owner_id);
    self->owner_id = 0;
  }
}
