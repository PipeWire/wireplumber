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

#include "properties.h"

G_BEGIN_DECLS

struct pw_proxy;
typedef struct _WpCore WpCore;

typedef enum { /*< flags >*/
  WP_PROXY_FEATURE_PW_PROXY     = (1 << 0),
  WP_PROXY_FEATURE_INFO         = (1 << 1),

  WP_PROXY_FEATURE_LAST         = (1 << 5), /*< skip >*/
} WpProxyFeatures;

#define WP_TYPE_PROXY (wp_proxy_get_type ())
WP_API
G_DECLARE_DERIVABLE_TYPE (WpProxy, wp_proxy, WP, PROXY, GObject)

/* The proxy base class */
struct _WpProxyClass
{
  GObjectClass parent_class;

  void (*augment) (WpProxy *self, WpProxyFeatures features);

  void (*pw_proxy_created) (WpProxy * self, struct pw_proxy * proxy);
  void (*pw_proxy_destroyed) (WpProxy * self);
};

WP_API
WpProxy * wp_proxy_new_wrap (WpCore * core, struct pw_proxy * proxy,
    const char *type, guint32 version, gpointer local_object);

WP_API
void wp_proxy_augment (WpProxy *self,
    WpProxyFeatures wanted_features, GCancellable * cancellable,
    GAsyncReadyCallback callback, gpointer user_data);

WP_API
gboolean wp_proxy_augment_finish (WpProxy * self, GAsyncResult * res,
    GError ** error);

WP_API
WpProxyFeatures wp_proxy_get_features (WpProxy * self);

WP_API
WpCore * wp_proxy_get_core (WpProxy * self);

WP_API
gboolean wp_proxy_is_global (WpProxy * self);

WP_API
guint32 wp_proxy_get_global_id (WpProxy * self);

WP_API
guint32 wp_proxy_get_global_permissions (WpProxy * self);

WP_API
WpProperties * wp_proxy_get_global_properties (WpProxy * self);

WP_API
const char * wp_proxy_get_interface_type (WpProxy * self);

WP_API
GQuark wp_proxy_get_interface_quark (WpProxy * self);

WP_API
guint32 wp_proxy_get_interface_version (WpProxy * self);

WP_API
struct pw_proxy * wp_proxy_get_pw_proxy (WpProxy * self);

G_END_DECLS

#endif
