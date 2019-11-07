/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_CORE_H__
#define __WIREPLUMBER_CORE_H__

#include <glib-object.h>
#include "proxy.h"

G_BEGIN_DECLS

struct pw_core;
struct pw_remote;

/**
 * WpRemoteState:
 * @WP_REMOTE_STATE_ERROR: remote is in error
 * @WP_REMOTE_STATE_UNCONNECTED: not connected
 * @WP_REMOTE_STATE_CONNECTING: connecting to remote service
 * @WP_REMOTE_STATE_CONNECTED: remote is connected and ready
 *
 * The different states the remote can be
 */
typedef enum {
  WP_REMOTE_STATE_ERROR = -1,
  WP_REMOTE_STATE_UNCONNECTED = 0,
  WP_REMOTE_STATE_CONNECTING = 1,
  WP_REMOTE_STATE_CONNECTED = 2,
} WpRemoteState;

#define WP_TYPE_CORE (wp_core_get_type ())
G_DECLARE_FINAL_TYPE (WpCore, wp_core, WP, CORE, GObject)

WpCore * wp_core_new (GMainContext *context, WpProperties * properties);

GMainContext * wp_core_get_context (WpCore * self);
struct pw_core * wp_core_get_pw_core (WpCore * self);
struct pw_remote * wp_core_get_pw_remote (WpCore * self);

gboolean wp_core_connect (WpCore * self);
WpRemoteState wp_core_get_remote_state (WpCore * self, const gchar ** error);

void wp_core_set_default_proxy_features (
    WpCore * self, GType proxy_type, WpProxyFeatures features);

WpProxy * wp_core_create_remote_object (WpCore * self,
    const gchar * factory_name, guint32 interface_type,
    guint32 interface_version, WpProperties * properties);

G_END_DECLS

#endif
