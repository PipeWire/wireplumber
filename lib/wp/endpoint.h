/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_ENDPOINT_H__
#define __WIREPLUMBER_ENDPOINT_H__

#include "spa-pod.h"
#include "proxy.h"
#include "port.h"
#include "endpoint-stream.h"
#include "iterator.h"

G_BEGIN_DECLS

/**
 * WpEndpointFeatures:
 * @WP_ENDPOINT_FEATURE_STREAMS: caches information about streams, enabling
 *   the use of wp_endpoint_get_n_streams(), wp_endpoint_find_stream() and
 *   wp_endpoint_iterate_streams()
 *
 * An extension of #WpProxyFeatures
 */
typedef enum { /*< flags >*/
  WP_ENDPOINT_FEATURE_STREAMS = WP_PROXY_FEATURE_LAST,
} WpEndpointFeatures;

/**
 * WP_ENDPOINT_FEATURES_STANDARD:
 *
 * A constant set of features that contains the standard features that are
 * available in the #WpEndpoint class.
 */
#define WP_ENDPOINT_FEATURES_STANDARD \
    (WP_PROXY_FEATURES_STANDARD | \
     WP_PROXY_FEATURE_CONTROLS | \
     WP_ENDPOINT_FEATURE_STREAMS)

/**
 * WP_TYPE_ENDPOINT:
 *
 * The #WpEndpoint #GType
 */
#define WP_TYPE_ENDPOINT (wp_endpoint_get_type ())
WP_API
G_DECLARE_DERIVABLE_TYPE (WpEndpoint, wp_endpoint, WP, ENDPOINT, WpProxy)

struct _WpEndpointClass
{
  WpProxyClass parent_class;

  const gchar * (*get_name) (WpEndpoint * self);
  const gchar * (*get_media_class) (WpEndpoint * self);
  WpDirection (*get_direction) (WpEndpoint * self);
};

WP_API
const gchar * wp_endpoint_get_name (WpEndpoint * self);

WP_API
const gchar * wp_endpoint_get_media_class (WpEndpoint * self);

WP_API
WpDirection wp_endpoint_get_direction (WpEndpoint * self);

WP_API
guint wp_endpoint_get_n_streams (WpEndpoint * self);

WP_API
WpEndpointStream * wp_endpoint_find_stream (WpEndpoint * self, guint32 bound_id);

WP_API
WpIterator * wp_endpoint_iterate_streams (WpEndpoint * self);

G_END_DECLS

#endif
