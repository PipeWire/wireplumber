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


/* private */

struct pw_core_proxy;
struct pw_registry_proxy;

struct pw_core_proxy * wp_core_get_pw_core_proxy (WpCore * self);
struct pw_registry_proxy * wp_core_get_pw_registry_proxy (WpCore * self);

enum {
  WP_CORE_FOREACH_GLOBAL_DONE = FALSE,
  WP_CORE_FOREACH_GLOBAL_CONTINUE = TRUE,
};

typedef gboolean (*WpCoreForeachGlobalFunc) (GQuark key, gpointer global,
    gpointer user_data);

gpointer wp_core_get_global (WpCore * self, GQuark key);
void wp_core_foreach_global (WpCore * self, WpCoreForeachGlobalFunc callback,
    gpointer user_data);

void wp_core_register_global (WpCore * self, GQuark key, gpointer obj,
    GDestroyNotify destroy_obj);
void wp_core_remove_global (WpCore * self, GQuark key, gpointer obj);

#define WP_GLOBAL_ENDPOINT (wp_global_endpoint_quark ())
GQuark wp_global_endpoint_quark (void);

#define WP_GLOBAL_FACTORY (wp_global_factory_quark ())
GQuark wp_global_factory_quark (void);

#define WP_GLOBAL_MODULE (wp_global_module_quark ())
GQuark wp_global_module_quark (void);

#define WP_GLOBAL_POLICY_MANAGER (wp_global_policy_manager_quark ())
GQuark wp_global_policy_manager_quark (void);

G_END_DECLS

#endif
