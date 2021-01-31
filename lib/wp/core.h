/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_CORE_H__
#define __WIREPLUMBER_CORE_H__

#include <gio/gio.h>
#include "defs.h"
#include "properties.h"

G_BEGIN_DECLS

struct pw_context;
struct pw_core;
typedef struct _WpObjectManager WpObjectManager;

#define WP_TYPE_CORE (wp_core_get_type ())
WP_API
G_DECLARE_FINAL_TYPE (WpCore, wp_core, WP, CORE, GObject)

/* Basic */

WP_API
WpCore * wp_core_new (GMainContext *context, WpProperties * properties);

WP_API
WpCore * wp_core_clone (WpCore * self);

WP_API
GMainContext * wp_core_get_g_main_context (WpCore * self);

WP_API
struct pw_context * wp_core_get_pw_context (WpCore * self);

WP_API
struct pw_core * wp_core_get_pw_core (WpCore * self);

WP_API
gboolean wp_core_load_component (WpCore * self, const gchar * component,
    const gchar * type, GVariant * args, GError ** error);

/* Connection */

WP_API
gboolean wp_core_connect (WpCore *self);

WP_API
void wp_core_disconnect (WpCore *self);

WP_API
gboolean wp_core_is_connected (WpCore * self);

/* Properties */

WP_API
guint32 wp_core_get_remote_cookie (WpCore * self);

WP_API
const gchar * wp_core_get_remote_name (WpCore * self);

WP_API
const gchar * wp_core_get_remote_user_name (WpCore * self);

WP_API
const gchar * wp_core_get_remote_host_name (WpCore * self);

WP_API
const gchar * wp_core_get_remote_version (WpCore * self);

WP_API
WpProperties * wp_core_get_remote_properties (WpCore * self);

WP_API
WpProperties * wp_core_get_properties (WpCore * self);

WP_API
void wp_core_update_properties (WpCore * self, WpProperties * updates);

/* Callback */

WP_API
void wp_core_idle_add (WpCore * self, GSource **source, GSourceFunc function,
    gpointer data, GDestroyNotify destroy);

WP_API
void wp_core_idle_add_closure (WpCore * self, GSource **source,
    GClosure * closure);

WP_API
void wp_core_timeout_add (WpCore * self, GSource **source, guint timeout_ms,
    GSourceFunc function, gpointer data, GDestroyNotify destroy);

WP_API
void wp_core_timeout_add_closure (WpCore * self, GSource **source,
    guint timeout_ms, GClosure * closure);

WP_API
gboolean wp_core_sync (WpCore * self, GCancellable * cancellable,
    GAsyncReadyCallback callback, gpointer user_data);

WP_API
gboolean wp_core_sync_finish (WpCore * self, GAsyncResult * res,
    GError ** error);

/* Object Manager */

WP_API
void wp_core_install_object_manager (WpCore * self, WpObjectManager * om);

G_END_DECLS

#endif
