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
 *
 * An extension of #WpProxyFeatures
 */
typedef enum { /*< flags >*/
  WP_ENDPOINT_FEATURE_CONTROLS = WP_PROXY_FEATURE_LAST,
} WpEndpointFeatures;

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

/**
 * WP_TYPE_IMPL_ENDPOINT:
 *
 * The #WpImplEndpoint #GType
 */
#define WP_TYPE_IMPL_ENDPOINT (wp_impl_endpoint_get_type ())
WP_API
G_DECLARE_DERIVABLE_TYPE (WpImplEndpoint, wp_impl_endpoint,
                          WP, IMPL_ENDPOINT, WpEndpoint)

struct _WpImplEndpointClass
{
  WpEndpointClass parent_class;
};

WP_API
WpImplEndpoint * wp_impl_endpoint_new (WpCore * core);

WP_API
void wp_impl_endpoint_set_property (WpImplEndpoint * self,
    const gchar * key, const gchar * value);

WP_API
void wp_impl_endpoint_update_properties (WpImplEndpoint * self,
    WpProperties * updates);

WP_API
void wp_impl_endpoint_set_name (WpImplEndpoint * self,
    const gchar * name);

WP_API
void wp_impl_endpoint_set_media_class (WpImplEndpoint * self,
    const gchar * media_class);

WP_API
void wp_impl_endpoint_set_direction (WpImplEndpoint * self,
    WpDirection dir);

WP_API
void wp_impl_endpoint_register_control (WpImplEndpoint * self,
    WpEndpointControl control);

G_END_DECLS

#endif
