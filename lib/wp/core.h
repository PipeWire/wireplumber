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
#include "object-manager.h"
#include "proxy.h"

G_BEGIN_DECLS

struct pw_context;

#define WP_TYPE_CORE (wp_core_get_type ())
G_DECLARE_FINAL_TYPE (WpCore, wp_core, WP, CORE, GObject)

/* Basic */
WpCore * wp_core_new (GMainContext *context, WpProperties * properties);
GMainContext * wp_core_get_context (WpCore * self);
struct pw_context * wp_core_get_pw_context (WpCore * self);

/* Connection */
gboolean wp_core_connect (WpCore *self);
void wp_core_disconnect (WpCore *self);
gboolean wp_core_is_connected (WpCore * self);

/* Callback */
guint wp_core_idle_add (WpCore * self, GSourceFunc function, gpointer data,
    GDestroyNotify destroy);
gboolean wp_core_sync (WpCore * self, GCancellable * cancellable,
    GAsyncReadyCallback callback, gpointer user_data);

/* Object */
WpProxy * wp_core_export_object (WpCore * self, const gchar * interface_type,
    gpointer local_object, WpProperties * properties);
WpProxy * wp_core_create_local_object (WpCore * self,
    const gchar *factory_name, const gchar * interface_type,
    guint32 interface_version, WpProperties * properties);
WpProxy * wp_core_create_remote_object (WpCore * self,
    const gchar * factory_name, const gchar * interface_type,
    guint32 interface_version, WpProperties * properties);

/* Object Manager */
void wp_core_install_object_manager (WpCore * self, WpObjectManager * om);

G_END_DECLS

#endif
