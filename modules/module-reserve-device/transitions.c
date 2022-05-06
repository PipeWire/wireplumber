/* WirePlumber
 *
 * Copyright © 2021 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "transitions.h"
#include "plugin.h"
#include "reserve-device.h"
#include "reserve-device-interface.h"

struct _WpReserveDeviceAcquireTransition
{
  WpTransition parent;
  gint owner_state;
  WpOrgFreedesktopReserveDevice1 *proxy;
};

enum {
  OWNER_STATE_NONE = 0,
  OWNER_STATE_ACQUIRED,
  OWNER_STATE_LOST,
};

enum {
  STEP_EXPORT_OBJECT = WP_TRANSITION_STEP_CUSTOM_START,
  STEP_ACQUIRE_NO_FORCE,
  STEP_GET_PROXY,
  STEP_REQUEST_RELEASE,
  STEP_ACQUIRE_WITH_FORCE,
  STEP_UNEXPORT_OBJECT,
};

G_DEFINE_TYPE (WpReserveDeviceAcquireTransition,
    wp_reserve_device_acquire_transition, WP_TYPE_TRANSITION)

static void
wp_reserve_device_acquire_transition_init (
    WpReserveDeviceAcquireTransition * self)
{
}

static void
wp_reserve_device_acquire_transition_finalize (GObject * object)
{
  WpReserveDeviceAcquireTransition *self =
      WP_RESERVE_DEVICE_ACQUIRE_TRANSITION (object);

  g_clear_object (&self->proxy);

  G_OBJECT_CLASS (wp_reserve_device_acquire_transition_parent_class)->
      finalize (object);
}

static guint
wp_reserve_device_acquire_transition_get_next_step (
    WpTransition * transition, guint step)
{
  WpReserveDeviceAcquireTransition *self =
        WP_RESERVE_DEVICE_ACQUIRE_TRANSITION (transition);

  switch (step) {
  case WP_TRANSITION_STEP_NONE:
    return STEP_EXPORT_OBJECT;

  case STEP_EXPORT_OBJECT:
    return STEP_ACQUIRE_NO_FORCE;

  case STEP_ACQUIRE_NO_FORCE:
    switch (self->owner_state) {
      case OWNER_STATE_ACQUIRED:
        return WP_TRANSITION_STEP_NONE;

      case OWNER_STATE_LOST:
        return STEP_GET_PROXY;

      default:
        return WP_TRANSITION_STEP_ERROR;
    }

  case STEP_GET_PROXY:
    if (self->proxy)
      return STEP_REQUEST_RELEASE;
    else
      return STEP_ACQUIRE_WITH_FORCE;

  case STEP_REQUEST_RELEASE:
    switch (self->owner_state) {
      case OWNER_STATE_ACQUIRED:
        return STEP_ACQUIRE_WITH_FORCE;

      case OWNER_STATE_LOST:
        return STEP_UNEXPORT_OBJECT;

      default:
        return WP_TRANSITION_STEP_ERROR;
    }

  case STEP_ACQUIRE_WITH_FORCE:
    return WP_TRANSITION_STEP_NONE;

  case STEP_UNEXPORT_OBJECT:
    return WP_TRANSITION_STEP_NONE;

  default:
    return WP_TRANSITION_STEP_ERROR;
  }
}

static void
on_got_proxy (GObject * src, GAsyncResult * res, WpTransition * transition)
{
  WpReserveDeviceAcquireTransition *self =
      WP_RESERVE_DEVICE_ACQUIRE_TRANSITION (transition);
  g_autoptr (GError) error = NULL;

  self->proxy =
      wp_org_freedesktop_reserve_device1_proxy_new_finish (res, &error);
  if (!self->proxy) {
    WpReserveDevice *rd = wp_transition_get_source_object (transition);
    wp_info_object (rd, "%s: Could not get proxy of remote reservation: %s",
        rd->name, error->message);
  }

  wp_transition_advance (transition);
}

static void
on_request_release_done (GObject * src, GAsyncResult * res,
    WpTransition * transition)
{
  WpReserveDeviceAcquireTransition *self =
      WP_RESERVE_DEVICE_ACQUIRE_TRANSITION (transition);
  g_autoptr (GError) error = NULL;
  gboolean released = FALSE;

  if (!wp_org_freedesktop_reserve_device1_call_request_release_finish (
          self->proxy, &released, res, &error)) {
    WpReserveDevice *rd = wp_transition_get_source_object (transition);
    wp_info_object (rd, "%s: Could not call RequestRelease: %s",
        rd->name, error->message);
  }

  self->owner_state = released ? OWNER_STATE_ACQUIRED : OWNER_STATE_LOST;
  wp_transition_advance (transition);
}

static void
wp_reserve_device_acquire_transition_execute_step (
    WpTransition * transition, guint step)
{
  WpReserveDeviceAcquireTransition *self =
      WP_RESERVE_DEVICE_ACQUIRE_TRANSITION (transition);
  WpReserveDevice *rd = wp_transition_get_source_object (transition);
  g_autoptr (WpReserveDevicePlugin) plugin = g_weak_ref_get (&rd->plugin);

  if (G_UNLIKELY (!plugin && step != WP_TRANSITION_STEP_ERROR)) {
    wp_transition_return_error (transition, g_error_new (
            WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_OPERATION_FAILED,
            "plugin destroyed while Acquire was in progress"));
    return;
  }

  switch (step) {
  case STEP_EXPORT_OBJECT:
    wp_reserve_device_export_object (rd);
    wp_transition_advance (transition);
    break;

  case STEP_ACQUIRE_NO_FORCE:
    wp_reserve_device_own_name (rd, FALSE);
    break;

  case STEP_GET_PROXY: {
    g_autoptr (GDBusConnection) conn = wp_dbus_get_connection (plugin->dbus);
    wp_org_freedesktop_reserve_device1_proxy_new (conn,
        G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES |
        G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS |
        G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
        rd->service_name, rd->object_path, NULL,
        (GAsyncReadyCallback) on_got_proxy, self);
    break;
  }

  case STEP_REQUEST_RELEASE:
    self->owner_state = OWNER_STATE_NONE;
    wp_org_freedesktop_reserve_device1_call_request_release (
        self->proxy, rd->priority, NULL,
        (GAsyncReadyCallback) on_request_release_done, self);
    break;

  case STEP_ACQUIRE_WITH_FORCE:
    wp_reserve_device_unown_name (rd);
    self->owner_state = OWNER_STATE_NONE;
    wp_reserve_device_own_name (rd, TRUE);
    break;

  case STEP_UNEXPORT_OBJECT:
    wp_reserve_device_unown_name (rd);
    wp_reserve_device_unexport_object (rd);
    wp_transition_advance (transition);
    break;

  case WP_TRANSITION_STEP_ERROR:
    wp_reserve_device_unown_name (rd);
    break;

  default:
    g_return_if_reached ();
  }
}

static void
wp_reserve_device_acquire_transition_class_init (
    WpReserveDeviceAcquireTransitionClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpTransitionClass *transition_class = (WpTransitionClass *) klass;

  object_class->finalize =
      wp_reserve_device_acquire_transition_finalize;

  transition_class->get_next_step =
      wp_reserve_device_acquire_transition_get_next_step;
  transition_class->execute_step =
      wp_reserve_device_acquire_transition_execute_step;
}

WpTransition *
wp_reserve_device_acquire_transition_new (WpReserveDevice *rd,
    GCancellable * cancellable, GAsyncReadyCallback callback,
    gpointer callback_data)
{
  return wp_transition_new (wp_reserve_device_acquire_transition_get_type (),
      rd, cancellable, callback, callback_data);
}

void
wp_reserve_device_acquire_transition_name_acquired (WpTransition * tr)
{
  WpReserveDeviceAcquireTransition *self =
      WP_RESERVE_DEVICE_ACQUIRE_TRANSITION (tr);
  self->owner_state = OWNER_STATE_ACQUIRED;
  wp_transition_advance (tr);
}

void
wp_reserve_device_acquire_transition_name_lost (WpTransition * tr)
{
  WpReserveDeviceAcquireTransition *self =
      WP_RESERVE_DEVICE_ACQUIRE_TRANSITION (tr);
  self->owner_state = OWNER_STATE_LOST;
  wp_transition_advance (tr);
}

gboolean
wp_reserve_device_acquire_transition_finish (GAsyncResult * res,
    GError ** error)
{
  if (!wp_transition_finish (res, error))
    return FALSE;

  WpReserveDeviceAcquireTransition *self =
      WP_RESERVE_DEVICE_ACQUIRE_TRANSITION (res);
  return (self->owner_state == OWNER_STATE_ACQUIRED) ? TRUE : FALSE;
}
