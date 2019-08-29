/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_PROXY_H__
#define __WIREPLUMBER_PROXY_H__

#include <gio/gio.h>

#include "remote.h"
#include "properties.h"

G_BEGIN_DECLS

struct pw_proxy;

typedef enum { /*< flags >*/
  WP_PROXY_FEATURE_PW_PROXY     = (1 << 0),
  WP_PROXY_FEATURE_INFO         = (1 << 1),

  WP_PROXY_FEATURE_LAST         = (1 << 5), /*< skip >*/
} WpProxyFeatures;

#define WP_TYPE_PROXY (wp_proxy_get_type ())
G_DECLARE_DERIVABLE_TYPE (WpProxy, wp_proxy, WP, PROXY, GObject)

/* The proxy base class */
struct _WpProxyClass
{
  GObjectClass parent_class;

  void (*augment) (WpProxy *self, WpProxyFeatures features);

  void (*pw_proxy_created) (WpProxy * self, struct pw_proxy * proxy);
  void (*pw_proxy_destroyed) (WpProxy * self);
};

WpProxy * wp_proxy_new_global (WpRemote * remote,
    guint32 id, guint32 permissions, WpProperties * properties,
    guint32 type, guint32 version);
WpProxy * wp_proxy_new_wrap (WpRemote * remote,
    struct pw_proxy * proxy, guint32 type, guint32 version);

void wp_proxy_augment (WpProxy *self,
    WpProxyFeatures wanted_features, GCancellable * cancellable,
    GAsyncReadyCallback callback, gpointer user_data);
gboolean wp_proxy_augment_finish (WpProxy * self, GAsyncResult * res,
    GError ** error);

WpProxyFeatures wp_proxy_get_features (WpProxy * self);

WpRemote * wp_proxy_get_remote (WpProxy * self);

gboolean wp_proxy_is_global (WpProxy * self);
guint32 wp_proxy_get_global_id (WpProxy * self);
guint32 wp_proxy_get_global_permissions (WpProxy * self);
WpProperties * wp_proxy_get_global_properties (WpProxy * self);

guint32 wp_proxy_get_interface_type (WpProxy * self);
const gchar * wp_proxy_get_interface_name (WpProxy * self);
GQuark wp_proxy_get_interface_quark (WpProxy * self);
guint32 wp_proxy_get_interface_version (WpProxy * self);

struct pw_proxy * wp_proxy_get_pw_proxy (WpProxy * self);

void wp_proxy_sync (WpProxy * self, GCancellable * cancellable,
    GAsyncReadyCallback callback, gpointer user_data);
gboolean wp_proxy_sync_finish (WpProxy * self, GAsyncResult * res,
    GError ** error);

/* for subclasses only */

void wp_proxy_set_feature_ready (WpProxy * self, WpProxyFeatures feature);
void wp_proxy_augment_error (WpProxy * self, GError * error);

void wp_proxy_register_async_task (WpProxy * self, int seq, GTask * task);
GTask * wp_proxy_find_async_task (WpProxy * self, int seq, gboolean steal);

G_END_DECLS

#endif
