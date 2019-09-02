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
} WpMonitorFlags;

#define WP_MONITOR_KEY_OBJECT_ID "wp.monitor.object.id"

#define WP_TYPE_MONITOR (wp_monitor_get_type ())
G_DECLARE_FINAL_TYPE (WpMonitor, wp_monitor, WP, MONITOR, GObject)

WpMonitor * wp_monitor_new (WpCore * core, const gchar * factory_name,
    WpMonitorFlags flags);

const gchar * wp_monitor_get_factory_name (WpMonitor *self);
WpMonitorFlags wp_monitor_get_flags (WpMonitor *self);

gboolean wp_monitor_start (WpMonitor *self, GError **error);
void wp_monitor_stop (WpMonitor *self);

G_END_DECLS

#endif
