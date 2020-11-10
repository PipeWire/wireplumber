/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_ENDPOINT_H__
#define __WIREPLUMBER_ENDPOINT_H__

#include "global-proxy.h"
#include "port.h"
#include "endpoint-stream.h"
#include "iterator.h"
#include "object-interest.h"

G_BEGIN_DECLS

/**
 * WpEndpointFeatures:
 * @WP_ENDPOINT_FEATURE_STREAMS: caches information about streams, enabling
 *   the use of wp_endpoint_get_n_streams(), wp_endpoint_lookup_stream(),
 *   wp_endpoint_iterate_streams() and related methods
 *
 * An extension of #WpProxyFeatures
 */
typedef enum { /*< flags >*/
  WP_ENDPOINT_FEATURE_STREAMS = (WP_PROXY_FEATURE_CUSTOM_START << 0),
} WpEndpointFeatures;

/**
 * WP_TYPE_ENDPOINT:
 *
 * The #WpEndpoint #GType
 */
#define WP_TYPE_ENDPOINT (wp_endpoint_get_type ())
WP_API
G_DECLARE_DERIVABLE_TYPE (WpEndpoint, wp_endpoint, WP, ENDPOINT, WpGlobalProxy)

struct _WpEndpointClass
{
  WpGlobalProxyClass parent_class;
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
WpIterator * wp_endpoint_iterate_streams (WpEndpoint * self);

WP_API
WpIterator * wp_endpoint_iterate_streams_filtered (WpEndpoint * self, ...)
    G_GNUC_NULL_TERMINATED;

WP_API
WpIterator * wp_endpoint_iterate_streams_filtered_full (WpEndpoint * self,
    WpObjectInterest * interest);

WP_API
WpEndpointStream * wp_endpoint_lookup_stream (WpEndpoint * self, ...)
    G_GNUC_NULL_TERMINATED;

WP_API
WpEndpointStream * wp_endpoint_lookup_stream_full (WpEndpoint * self,
    WpObjectInterest * interest);

WP_API
void wp_endpoint_create_link (WpEndpoint * self, WpProperties * props);

G_END_DECLS

#endif
