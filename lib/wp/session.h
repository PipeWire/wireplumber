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
 * @WP_SESSION_FEATURE_DEFAULT_ENDPOINT: enables the use of
 *   wp_session_get_default_endpoint() and wp_session_set_default_endpoint()
 *   to store default endpoint preferences on the session
 * @WP_SESSION_FEATURE_ENDPOINTS: caches information about endpoints, enabling
 *   the use of wp_session_get_n_endpoints(), wp_session_find_endpoint() and
 *   wp_session_iterate_endpoints()
 * @WP_SESSION_FEATURE_LINKS: caches information about endpoint links, enabling
 *   the use of wp_session_get_n_links(), wp_session_find_link() and
 *   wp_session_iterate_links()
 *
 * An extension of #WpProxyFeatures
 */
typedef enum { /*< flags >*/
  WP_SESSION_FEATURE_DEFAULT_ENDPOINT = WP_PROXY_FEATURE_LAST,
  WP_SESSION_FEATURE_ENDPOINTS,
  WP_SESSION_FEATURE_LINKS,
} WpSessionFeatures;

/**
 * WP_SESSION_FEATURES_STANDARD:
 *
 * A constant set of features that contains the standard features that are
 * available in the #WpSession class.
 */
#define WP_SESSION_FEATURES_STANDARD \
    (WP_PROXY_FEATURES_STANDARD | \
     WP_SESSION_FEATURE_DEFAULT_ENDPOINT | \
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

  guint32 (*get_default_endpoint) (WpSession * self, const gchar * type_name);
  void (*set_default_endpoint) (WpSession * self, const gchar * type_name,
      guint32 id);
};

WP_API
guint32 wp_session_get_default_endpoint (WpSession * self,
    const gchar * type_name);

WP_API
void wp_session_set_default_endpoint (WpSession * self, const gchar * type_name,
    guint32 id);

WP_API
guint wp_session_get_n_endpoints (WpSession * self);

WP_API
WpEndpoint * wp_session_find_endpoint (WpSession * self, guint32 bound_id);

WP_API
WpIterator * wp_session_iterate_endpoints (WpSession * self);

WP_API
guint wp_session_get_n_links (WpSession * self);

WP_API
WpEndpointLink * wp_session_find_link (WpSession * self, guint32 bound_id);

WP_API
WpIterator * wp_session_iterate_links (WpSession * self);

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
