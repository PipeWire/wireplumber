/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_PRIVATE_IMPL_ENDPOINT_H__
#define __WIREPLUMBER_PRIVATE_IMPL_ENDPOINT_H__

#include "endpoint.h"
#include "endpoint-stream.h"
#include "endpoint-link.h"
#include "si-interfaces.h"

G_BEGIN_DECLS

/* impl endpoint */

#define WP_TYPE_IMPL_ENDPOINT (wp_impl_endpoint_get_type ())
G_DECLARE_FINAL_TYPE (WpImplEndpoint, wp_impl_endpoint,
                      WP, IMPL_ENDPOINT, WpEndpoint)

WpImplEndpoint * wp_impl_endpoint_new (WpCore * core, WpSiEndpoint * item);

/* impl endpoint stream */

#define WP_TYPE_IMPL_ENDPOINT_STREAM (wp_impl_endpoint_stream_get_type ())
G_DECLARE_FINAL_TYPE (WpImplEndpointStream, wp_impl_endpoint_stream,
                      WP, IMPL_ENDPOINT_STREAM, WpEndpointStream)

WpImplEndpointStream * wp_impl_endpoint_stream_new (WpCore * core,
    WpSiStream * item);

/* impl endpoint link */

#define WP_TYPE_IMPL_ENDPOINT_LINK (wp_impl_endpoint_link_get_type ())
G_DECLARE_FINAL_TYPE (WpImplEndpointLink, wp_impl_endpoint_link,
                      WP, IMPL_ENDPOINT_LINK, WpEndpointLink)

WpImplEndpointLink * wp_impl_endpoint_link_new (WpCore * core, WpSiLink * item);

G_END_DECLS

#endif
