/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_ENDPOINT_H__
#define __WIREPLUMBER_ENDPOINT_H__

#include "exported.h"
#include "proxy.h"

G_BEGIN_DECLS

#define WP_TYPE_ENDPOINT (wp_endpoint_get_type ())
WP_API
G_DECLARE_INTERFACE (WpEndpoint, wp_endpoint, WP, ENDPOINT, GObject)

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

typedef enum {
  WP_ENDPOINT_CONTROL_VOLUME = 0x10003 /* SPA_PROP_volume */,
  WP_ENDPOINT_CONTROL_MUTE = 0x10004 /* SPA_PROP_mute */,
  WP_ENDPOINT_CONTROL_CHANNEL_VOLUMES = 0x10008 /* SPA_PROP_channelVolumes */,
} WpEndpointControl;

struct _WpEndpointInterface
{
  GTypeInterface parent;

  const gchar * (*get_name) (WpEndpoint * self);
  const gchar * (*get_media_class) (WpEndpoint * self);
  WpDirection (*get_direction) (WpEndpoint * self);

  const struct spa_pod * (*get_control) (WpEndpoint * self, guint32 control_id);
  gboolean (*set_control) (WpEndpoint * self, guint32 control_id,
      const struct spa_pod * value);

  // void (*create_link) (WpEndpoint * self, WpProperties * props);
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

// void wp_endpoint_create_link (WpEndpoint * self, WpProperties * props);

/* proxy */

typedef enum { /*< flags >*/
  WP_PROXY_ENDPOINT_FEATURE_CONTROLS = WP_PROXY_FEATURE_LAST,
} WpProxyEndpointFeatures;

#define WP_TYPE_PROXY_ENDPOINT (wp_proxy_endpoint_get_type ())
WP_API
G_DECLARE_FINAL_TYPE (WpProxyEndpoint, wp_proxy_endpoint,
                      WP, PROXY_ENDPOINT, WpProxy)

/* exported */

#define WP_TYPE_EXPORTED_ENDPOINT (wp_exported_endpoint_get_type ())
WP_API
G_DECLARE_DERIVABLE_TYPE (WpExportedEndpoint, wp_exported_endpoint,
                          WP, EXPORTED_ENDPOINT, WpExported)

struct _WpExportedEndpointClass
{
  WpExportedClass parent_class;
};

WP_API
WpExportedEndpoint * wp_exported_endpoint_new (WpCore * core);

WP_API
guint32 wp_exported_endpoint_get_global_id (WpExportedEndpoint * self);

WP_API
WpProperties * wp_exported_endpoint_get_properties (WpExportedEndpoint * self);

WP_API
void wp_exported_endpoint_set_property (WpExportedEndpoint * self,
    const gchar * key, const gchar * value);

WP_API
void wp_exported_endpoint_update_properties (WpExportedEndpoint * self,
    WpProperties * updates);

WP_API
void wp_exported_endpoint_set_name (WpExportedEndpoint * self,
    const gchar * name);

WP_API
void wp_exported_endpoint_set_media_class (WpExportedEndpoint * self,
    const gchar * media_class);

WP_API
void wp_exported_endpoint_set_direction (WpExportedEndpoint * self,
    WpDirection dir);

WP_API
void wp_exported_endpoint_register_control (WpExportedEndpoint * self,
    WpEndpointControl control);

// void wp_exported_endpoint_register_stream (WpExportedEndpoint * self,
//     WpExportedEndpointStream * stream);
// void wp_exported_endpoint_remove_stream (WpExportedEndpoint * self,
//     WpExportedEndpointStream * stream);
// GPtrArray * wp_exported_endpoint_list_streams (WpExportedEndpoint * self);

G_END_DECLS

#endif
