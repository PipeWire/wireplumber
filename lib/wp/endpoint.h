/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_ENDPOINT_H__
#define __WIREPLUMBER_ENDPOINT_H__

#include "proxy.h"
#include "endpoint-stream.h"

G_BEGIN_DECLS

/**
 * WpDirection:
 * @WP_DIRECTION_INPUT: a sink, consuming input
 * @WP_DIRECTION_OUTPUT: a source, producing output
 *
 * The different directions the endpoint can have
 */
typedef enum {
  WP_DIRECTION_INPUT,
  WP_DIRECTION_OUTPUT,
} WpDirection;

/**
 * WpEndpointControl:
 * @WP_ENDPOINT_CONTROL_VOLUME: a volume control (type: float)
 * @WP_ENDPOINT_CONTROL_MUTE: a mute control (type: boolean)
 * @WP_ENDPOINT_CONTROL_CHANNEL_VOLUMES:
 */
typedef enum {
  WP_ENDPOINT_CONTROL_VOLUME = 0x10003 /* SPA_PROP_volume */,
  WP_ENDPOINT_CONTROL_MUTE = 0x10004 /* SPA_PROP_mute */,
  WP_ENDPOINT_CONTROL_CHANNEL_VOLUMES = 0x10008 /* SPA_PROP_channelVolumes */,
} WpEndpointControl;

/**
 * WpEndpointFeatures:
 * @WP_ENDPOINT_FEATURE_CONTROLS: enables the use of the
 *   wp_endpoint_get_control() and wp_endpoint_set_control() families of
 *   functions to be able to work with endpoint-specific controls
 * @WP_ENDPOINT_FEATURE_STREAMS: caches information about streams, enabling
 *   the use of wp_endpoint_get_n_streams(), wp_endpoint_get_stream() and
 *   wp_endpoint_get_all_streams()
 *
 * An extension of #WpProxyFeatures
 */
typedef enum { /*< flags >*/
  WP_ENDPOINT_FEATURE_CONTROLS = WP_PROXY_FEATURE_LAST,
  WP_ENDPOINT_FEATURE_STREAMS,
} WpEndpointFeatures;

/**
 * WP_ENDPOINT_FEATURES_STANDARD:
 *
 * A constant set of features that contains the standard features that are
 * available in the #WpEndpoint class.
 */
#define WP_ENDPOINT_FEATURES_STANDARD \
    (WP_PROXY_FEATURES_STANDARD | \
     WP_ENDPOINT_FEATURE_CONTROLS | \
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

  const struct spa_pod * (*get_control) (WpEndpoint * self, guint32 control_id);
  gboolean (*set_control) (WpEndpoint * self, guint32 control_id,
      const struct spa_pod * value);
};

WP_API
const gchar * wp_endpoint_get_name (WpEndpoint * self);

WP_API
const gchar * wp_endpoint_get_media_class (WpEndpoint * self);

WP_API
WpDirection wp_endpoint_get_direction (WpEndpoint * self);

WP_API
const struct spa_pod * wp_endpoint_get_control (WpEndpoint * self,
    guint32 control_id);

WP_API
gboolean wp_endpoint_get_control_boolean (WpEndpoint * self, guint32 control_id,
    gboolean * value);

WP_API
gboolean wp_endpoint_get_control_int (WpEndpoint * self, guint32 control_id,
    gint * value);

WP_API
gboolean wp_endpoint_get_control_float (WpEndpoint * self, guint32 control_id,
    gfloat * value);

WP_API
gboolean wp_endpoint_set_control (WpEndpoint * self, guint32 control_id,
    const struct spa_pod * value);

WP_API
gboolean wp_endpoint_set_control_boolean (WpEndpoint * self, guint32 control_id,
    gboolean value);

WP_API
gboolean wp_endpoint_set_control_int (WpEndpoint * self, guint32 control_id,
    gint value);

WP_API
gboolean wp_endpoint_set_control_float (WpEndpoint * self, guint32 control_id,
    gfloat value);

WP_API
guint wp_endpoint_get_n_streams (WpEndpoint * self);

WP_API
WpEndpointStream * wp_endpoint_get_stream (WpEndpoint * self, guint32 bound_id);

WP_API
GPtrArray * wp_endpoint_get_all_streams (WpEndpoint * self);

G_END_DECLS

#endif
