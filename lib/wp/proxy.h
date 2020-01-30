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
struct spa_pod;
typedef struct _WpCore WpCore;

/**
 * WpProxyFeatures:
 *
 * Flags that specify functionality that is available on this class.
 * Use wp_proxy_augment() to enable more features and wp_proxy_get_features()
 * to find out which features are already enabled.
 *
 * Subclasses may also specify additional features that can be ORed with these
 * ones and they can also be enabled with wp_proxy_augment().
 */
typedef enum { /*< flags >*/
  WP_PROXY_FEATURE_PW_PROXY     = (1 << 0),
  WP_PROXY_FEATURE_INFO         = (1 << 1),
  WP_PROXY_FEATURE_BOUND        = (1 << 2),

  WP_PROXY_FEATURE_LAST         = (1 << 5), /*< skip >*/
} WpProxyFeatures;

/**
 * WP_PROXY_FEATURES_STANDARD:
 *
 * A constant set of features that contains the standard features that are
 * available in the #WpProxy class. The standard features are usually all
 * enabled at once, even if not requested explicitly. It is a good practice,
 * though, to enable only the features that you actually need. This leaves
 * room for optimizations in the #WpProxy class.
 */
#define WP_PROXY_FEATURES_STANDARD \
    (WP_PROXY_FEATURE_PW_PROXY | WP_PROXY_FEATURE_INFO | WP_PROXY_FEATURE_BOUND)


#define WP_TYPE_PROXY (wp_proxy_get_type ())
WP_API
G_DECLARE_DERIVABLE_TYPE (WpProxy, wp_proxy, WP, PROXY, GObject)

/* The proxy base class */
struct _WpProxyClass
{
  GObjectClass parent_class;

  const gchar * pw_iface_type;
  guint32 pw_iface_version;

  void (*augment) (WpProxy *self, WpProxyFeatures features);

  gconstpointer (*get_info) (WpProxy * self);
  WpProperties * (*get_properties) (WpProxy * self);

  gint (*enum_params) (WpProxy * self, guint32 id, guint32 start, guint32 num,
      const struct spa_pod * filter);
  gint (*subscribe_params) (WpProxy * self, guint32 n_ids, guint32 *ids);
  gint (*set_param) (WpProxy * self, guint32 id, guint32 flags,
      const struct spa_pod * param);

  /* signals */

  void (*pw_proxy_created) (WpProxy * self, struct pw_proxy * proxy);
  void (*pw_proxy_destroyed) (WpProxy * self);
  void (*param) (WpProxy * self, gint seq, guint32 id, guint32 index,
      guint32 next, const struct spa_pod *param);
};

/* features API */

WP_API
void wp_proxy_augment (WpProxy *self,
    WpProxyFeatures wanted_features, GCancellable * cancellable,
    GAsyncReadyCallback callback, gpointer user_data);

WP_API
gboolean wp_proxy_augment_finish (WpProxy * self, GAsyncResult * res,
    GError ** error);

WP_API
WpProxyFeatures wp_proxy_get_features (WpProxy * self);

/* the owner core */

WP_API
WpCore * wp_proxy_get_core (WpProxy * self);

/* global object API */

WP_API
guint32 wp_proxy_get_global_permissions (WpProxy * self);

WP_API
WpProperties * wp_proxy_get_global_properties (WpProxy * self);

/* native pw_proxy object getter (requires FEATURE_PW_PROXY) */

WP_API
struct pw_proxy * wp_proxy_get_pw_proxy (WpProxy * self);

/* native info structure + wrappers (requires FEATURE_INFO) */

WP_API
gconstpointer wp_proxy_get_info (WpProxy * self);

WP_API
WpProperties * wp_proxy_get_properties (WpProxy * self);

/* the bound id (aka global id, requires FEATURE_BOUND) */

WP_API
guint32 wp_proxy_get_bound_id (WpProxy * self);

/* common API of most proxied objects */

WP_API
gint wp_proxy_enum_params (WpProxy * self, guint32 id, guint32 start,
    guint32 num, const struct spa_pod *filter);

WP_API
void wp_proxy_enum_params_collect (WpProxy * self,
    guint32 id, guint32 start, guint32 num, const struct spa_pod *filter,
    GCancellable * cancellable, GAsyncReadyCallback callback,
    gpointer user_data);

WP_API
GPtrArray * wp_proxy_enum_params_collect_finish (WpProxy * self,
    GAsyncResult * res, GError ** error);

WP_API
gint wp_proxy_subscribe_params (WpProxy * self, guint32 n_ids, ...);

WP_API
gint wp_proxy_subscribe_params_array (WpProxy * self, guint32 n_ids,
    guint32 *ids);

WP_API
gint wp_proxy_set_param (WpProxy * self, guint32 id, guint32 flags,
    const struct spa_pod *param);

G_END_DECLS

#endif
