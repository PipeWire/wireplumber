/* WirePlumber
 *
 * Copyright © 2026 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_CONNECTION_H__
#define __WIREPLUMBER_CONNECTION_H__

#include "properties.h"
#include "context.h"

G_BEGIN_DECLS

struct pw_core;

/*!
 * \brief The WpConnection GType
 * \ingroup wpconnection
 */
#define WP_TYPE_CONNECTION (wp_connection_get_type ())
WP_API
G_DECLARE_FINAL_TYPE (WpConnection, wp_connection, WP, CONNECTION, GObject)

WP_API
WpCore * wp_connection_get_core (WpConnection * self);

WP_API
struct pw_core * wp_connection_get_pw_core (WpConnection * self);

/* Properties */

WP_API
guint32 wp_connection_get_own_bound_id (WpConnection * self);

WP_API
guint32 wp_connection_get_remote_cookie (WpConnection * self);

WP_API
const gchar * wp_connection_get_remote_name (WpConnection * self);

WP_API
const gchar * wp_connection_get_remote_user_name (WpConnection * self);

WP_API
const gchar * wp_connection_get_remote_host_name (WpConnection * self);

WP_API
const gchar * wp_connection_get_remote_version (WpConnection * self);

WP_API
WpProperties * wp_connection_get_remote_properties (WpConnection * self);

WP_API
WpProperties * wp_connection_get_properties (WpConnection * self);

WP_API
void wp_connection_update_properties (WpConnection * self,
    WpProperties * updates);

/* Sync */

WP_API
gboolean wp_connection_sync (WpConnection * self, GCancellable * cancellable,
    GAsyncReadyCallback callback, gpointer user_data);

WP_API
gboolean wp_connection_sync_closure (WpConnection * self,
    GCancellable * cancellable, GClosure * closure);

WP_API
gboolean wp_connection_sync_finish (WpConnection * self, GAsyncResult * res,
    GError ** error);

G_END_DECLS

#endif
