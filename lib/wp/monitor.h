/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_MONITOR_H__
#define __WIREPLUMBER_MONITOR_H__

#include "core.h"

G_BEGIN_DECLS

typedef enum { /*< flags, prefix=WP_MONITOR_FLAG_ >*/
  WP_MONITOR_FLAG_LOCAL_NODES = (1 << 0),
  WP_MONITOR_FLAG_USE_ADAPTER = (1 << 1),
  WP_MONITOR_FLAG_ACTIVATE_DEVICES = (1 << 2),
} WpMonitorFlags;

#define WP_MONITOR_KEY_OBJECT_ID "wp.monitor.object.id"

#define WP_TYPE_MONITOR (wp_monitor_get_type ())
WP_API
G_DECLARE_FINAL_TYPE (WpMonitor, wp_monitor, WP, MONITOR, GObject)

WP_API
WpMonitor * wp_monitor_new (WpCore * core, const gchar * factory_name,
    WpProperties *props, WpMonitorFlags flags);

WP_API
const gchar * wp_monitor_get_factory_name (WpMonitor *self);

WP_API
WpMonitorFlags wp_monitor_get_flags (WpMonitor *self);

WP_API
gboolean wp_monitor_start (WpMonitor *self, GError **error);

WP_API
void wp_monitor_stop (WpMonitor *self);

G_END_DECLS

#endif
