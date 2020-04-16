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
 * WpEndpointStreamFeatures:
 * @WP_ENDPOINT_STREAM_FEATURE_CONTROLS: enables the use of the
 *   wp_endpoint_stream_get_control() and wp_endpoint_stream_set_control()
 *   families of functions to be able to work with endpoint-stream-specific
 *   controls
 *
 * An extension of #WpProxyFeatures
 */
typedef enum { /*< flags >*/
  WP_ENDPOINT_STREAM_FEATURE_CONTROLS = WP_PROXY_FEATURE_LAST,
} WpEndpointStreamFeatures;

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

  const gchar * (*get_name) (WpEndpointStream * self);

  WpSpaPod * (*get_control) (WpEndpointStream * self, const gchar * id_name);
  gboolean (*set_control) (WpEndpointStream * self, const gchar * id_name,
      const WpSpaPod * value);
};

WP_API
const gchar * wp_endpoint_stream_get_name (WpEndpointStream * self);

WP_API
WpSpaPod * wp_endpoint_stream_get_control (WpEndpointStream * self,
    const gchar *id_name);

WP_API
gboolean wp_endpoint_stream_set_control (WpEndpointStream * self,
    const gchar *id_name, const WpSpaPod * value);

G_END_DECLS

#endif
