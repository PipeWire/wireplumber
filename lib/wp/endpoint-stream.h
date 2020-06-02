/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_ENDPOINT_STREAM_H__
#define __WIREPLUMBER_ENDPOINT_STREAM_H__

#include "proxy.h"
#include "spa-pod.h"

G_BEGIN_DECLS

/**
 * WP_TYPE_ENDPOINT_STREAM:
 *
 * The #WpEndpointStream #GType
 */
#define WP_TYPE_ENDPOINT_STREAM (wp_endpoint_stream_get_type ())
WP_API
G_DECLARE_DERIVABLE_TYPE (WpEndpointStream, wp_endpoint_stream,
                          WP, ENDPOINT_STREAM, WpProxy)

struct _WpEndpointStreamClass
{
  WpProxyClass parent_class;
};

WP_API
const gchar * wp_endpoint_stream_get_name (WpEndpointStream * self);

G_END_DECLS

#endif
