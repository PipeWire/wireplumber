/* WirePlumber
 *
 * Copyright © 2021 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_RESERVE_DEVICE_H__
#define __WIREPLUMBER_RESERVE_DEVICE_H__

#include <wp/wp.h>

G_BEGIN_DECLS

typedef enum {
  WP_RESERVE_DEVICE_STATE_UNKNOWN = 0,
  WP_RESERVE_DEVICE_STATE_BUSY,
  WP_RESERVE_DEVICE_STATE_AVAILABLE,
  WP_RESERVE_DEVICE_STATE_ACQUIRED,
} WpReserveDeviceState;

G_DECLARE_FINAL_TYPE (WpReserveDevice, wp_reserve_device,
    WP, RESERVE_DEVICE, GObject)

struct _WpReserveDevice
{
  GObject parent;

  GWeakRef plugin;
  gchar *name;
  gchar *app_name;
  gchar *app_dev_name;
  gint priority;
  gchar *owner_app_name;

  gchar *service_name;
  gchar *object_path;

  WpTransition *transition;
  GDBusMethodInvocation *req_rel_invocation;
  WpReserveDeviceState state;
  guint watcher_id;
  guint owner_id;
};

void wp_reserve_device_export_object (WpReserveDevice *self);
void wp_reserve_device_unexport_object (WpReserveDevice *self);

void wp_reserve_device_own_name (WpReserveDevice * self, gboolean force);
void wp_reserve_device_unown_name (WpReserveDevice * self);

G_END_DECLS

#endif
