/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_SESSION_H__
#define __WIREPLUMBER_SESSION_H__

#include "proxy.h"
#include "endpoint.h"
#include "endpoint-link.h"

G_BEGIN_DECLS

/**
 * WpSessionFeatures:
 * @WP_SESSION_FEATURE_ENDPOINTS: caches information about endpoints, enabling
 *   the use of wp_session_get_n_endpoints(), wp_session_lookup_endpoint(),
 *   wp_session_iterate_endpoints() and related methods
 * @WP_SESSION_FEATURE_LINKS: caches information about endpoint links, enabling
 *   the use of wp_session_get_n_links(), wp_session_lookup_link(),
 *   wp_session_iterate_links() and related methods
 *
 * An extension of #WpProxyFeatures
 */
typedef enum { /*< flags >*/
  WP_SESSION_FEATURE_ENDPOINTS = (WP_PROXY_FEATURE_LAST << 0),
  WP_SESSION_FEATURE_LINKS = (WP_PROXY_FEATURE_LAST << 1),
} WpSessionFeatures;

/**
 * WP_SESSION_FEATURES_STANDARD:
 *
 * A constant set of features that contains the standard features that are
 * available in the #WpSession class.
 */
#define WP_SESSION_FEATURES_STANDARD \
    (WP_PROXY_FEATURES_STANDARD | \
     WP_PROXY_FEATURE_PROPS | \
     WP_SESSION_FEATURE_ENDPOINTS | \
     WP_SESSION_FEATURE_LINKS)

/**
 * WP_TYPE_SESSION:
 *
 * The #WpSession #GType
 */
#define WP_TYPE_SESSION (wp_session_get_type ())
WP_API
G_DECLARE_DERIVABLE_TYPE (WpSession, wp_session, WP, SESSION, WpProxy)

struct _WpSessionClass
{
  WpProxyClass parent_class;

  guint32 (*get_default_endpoint) (WpSession * self, const gchar * id_name);
  void (*set_default_endpoint) (WpSession * self, const gchar * id_name,
      guint32 id);
};

WP_API
const gchar * wp_session_get_name (WpSession * self);

WP_API
guint32 wp_session_get_default_endpoint (WpSession * self,
    const gchar * id_name);

WP_API
void wp_session_set_default_endpoint (WpSession * self, const gchar * id_name,
    guint32 id);

/* endpoints */

WP_API
guint wp_session_get_n_endpoints (WpSession * self);

WP_API
WpIterator * wp_session_iterate_endpoints (WpSession * self);

WP_API
WpIterator * wp_session_iterate_endpoints_filtered (WpSession * self, ...)
    G_GNUC_NULL_TERMINATED;

WP_API
WpIterator * wp_session_iterate_endpoints_filtered_full (WpSession * self,
    WpObjectInterest * interest);

WP_API
WpEndpoint * wp_session_lookup_endpoint (WpSession * self, ...)
    G_GNUC_NULL_TERMINATED;

WP_API
WpEndpoint * wp_session_lookup_endpoint_full (WpSession * self,
    WpObjectInterest * interest);

/* links */

WP_API
guint wp_session_get_n_links (WpSession * self);

WP_API
WpIterator * wp_session_iterate_links (WpSession * self);

WP_API
WpIterator * wp_session_iterate_links_filtered (WpSession * self, ...)
    G_GNUC_NULL_TERMINATED;

WP_API
WpIterator * wp_session_iterate_links_filtered_full (WpSession * self,
    WpObjectInterest * interest);

WP_API
WpEndpointLink * wp_session_lookup_link (WpSession * self, ...)
    G_GNUC_NULL_TERMINATED;

WP_API
WpEndpointLink * wp_session_lookup_link_full (WpSession * self,
    WpObjectInterest * interest);

/**
 * WP_TYPE_IMPL_SESSION:
 *
 * The #WpImplSession #GType
 */
#define WP_TYPE_IMPL_SESSION (wp_impl_session_get_type ())
WP_API
G_DECLARE_FINAL_TYPE (WpImplSession, wp_impl_session, WP, IMPL_SESSION, WpSession)

WP_API
WpImplSession * wp_impl_session_new (WpCore * core);

WP_API
void wp_impl_session_set_property (WpImplSession * self,
    const gchar * key, const gchar * value);

WP_API
void wp_impl_session_update_properties (WpImplSession * self,
    WpProperties * updates);

G_END_DECLS

#endif
