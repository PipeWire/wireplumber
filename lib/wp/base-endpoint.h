/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_BASE_ENDPOINT_H__
#define __WIREPLUMBER_BASE_ENDPOINT_H__

#include <gio/gio.h>

#include "core.h"

G_BEGIN_DECLS

static const guint32 WP_STREAM_ID_NONE = 0xffffffff;
static const guint32 WP_CONTROL_ID_NONE = 0xffffffff;

#define WP_TYPE_BASE_ENDPOINT (wp_base_endpoint_get_type ())
G_DECLARE_DERIVABLE_TYPE (WpBaseEndpoint, wp_base_endpoint,
                          WP, BASE_ENDPOINT, GObject)

#define WP_TYPE_BASE_ENDPOINT_LINK (wp_base_endpoint_link_get_type ())
G_DECLARE_DERIVABLE_TYPE (WpBaseEndpointLink, wp_base_endpoint_link,
                          WP, BASE_ENDPOINT_LINK, GObject)

struct _WpBaseEndpointClass
{
  GObjectClass parent_class;

  WpProperties * (*get_properties) (WpBaseEndpoint * self);
  const char * (*get_role) (WpBaseEndpoint * self);
  guint32 (*get_global_id) (WpBaseEndpoint * self);

  GVariant * (*get_control_value) (WpBaseEndpoint * self, guint32 control_id);
  gboolean (*set_control_value) (WpBaseEndpoint * self, guint32 control_id,
      GVariant * value);

  gboolean (*prepare_link) (WpBaseEndpoint * self, guint32 stream_id,
      WpBaseEndpointLink * link, GVariant ** properties, GError ** error);
  void (*release_link) (WpBaseEndpoint * self, WpBaseEndpointLink * link);

  const gchar * (*get_endpoint_link_factory) (WpBaseEndpoint * self);
};

WpBaseEndpoint * wp_base_endpoint_new_finish (GObject *initable, GAsyncResult *res,
  GError **error);
void wp_base_endpoint_register (WpBaseEndpoint * self);
void wp_base_endpoint_unregister (WpBaseEndpoint * self);

WpCore *wp_base_endpoint_get_core (WpBaseEndpoint * self);
const gchar * wp_base_endpoint_get_name (WpBaseEndpoint * self);
const gchar * wp_base_endpoint_get_media_class (WpBaseEndpoint * self);
guint wp_base_endpoint_get_direction (WpBaseEndpoint * self);
guint64 wp_base_endpoint_get_creation_time (WpBaseEndpoint * self);
guint32 wp_base_endpoint_get_priority (WpBaseEndpoint * self);
WpProperties * wp_base_endpoint_get_properties (WpBaseEndpoint * self);
const char * wp_base_endpoint_get_role (WpBaseEndpoint * self);
guint32 wp_base_endpoint_get_global_id (WpBaseEndpoint * self);

void wp_base_endpoint_register_stream (WpBaseEndpoint * self, GVariant * stream);
GVariant * wp_base_endpoint_get_stream (WpBaseEndpoint * self, guint32 stream_id);
GVariant * wp_base_endpoint_list_streams (WpBaseEndpoint * self);
guint32 wp_base_endpoint_find_stream (WpBaseEndpoint * self, const gchar * name);

void wp_base_endpoint_register_control (WpBaseEndpoint * self, GVariant * control);
GVariant * wp_base_endpoint_get_control (WpBaseEndpoint * self, guint32 control_id);
GVariant * wp_base_endpoint_list_controls (WpBaseEndpoint * self);
guint32 wp_base_endpoint_find_control (WpBaseEndpoint * self, guint32 stream_id,
    const gchar * name);

GVariant * wp_base_endpoint_get_control_value (WpBaseEndpoint * self,
    guint32 control_id);
gboolean wp_base_endpoint_set_control_value (WpBaseEndpoint * self,
    guint32 control_id, GVariant * value);
void wp_base_endpoint_notify_control_value (WpBaseEndpoint * self,
    guint32 control_id);

gboolean wp_base_endpoint_is_linked (WpBaseEndpoint * self);
GPtrArray * wp_base_endpoint_get_links (WpBaseEndpoint * self);
void wp_base_endpoint_unlink (WpBaseEndpoint * self);

struct _WpBaseEndpointLinkClass
{
  GObjectClass parent_class;

  gboolean (*create) (WpBaseEndpointLink * self, GVariant * src_data,
      GVariant * sink_data, GError ** error);
  void (*destroy) (WpBaseEndpointLink * self);
};

WpBaseEndpoint * wp_base_endpoint_link_get_source_endpoint (
    WpBaseEndpointLink * self);
guint32 wp_base_endpoint_link_get_source_stream (WpBaseEndpointLink * self);
WpBaseEndpoint * wp_base_endpoint_link_get_sink_endpoint (
  WpBaseEndpointLink * self);
guint32 wp_base_endpoint_link_get_sink_stream (WpBaseEndpointLink * self);
gboolean wp_base_endpoint_link_is_kept (WpBaseEndpointLink * self);

void wp_base_endpoint_link_new (WpCore * core, WpBaseEndpoint * src,
    guint32 src_stream, WpBaseEndpoint * sink, guint32 sink_stream,
    gboolean keep, GAsyncReadyCallback ready, gpointer data);
WpBaseEndpointLink * wp_base_endpoint_link_new_finish (GObject *initable,
    GAsyncResult *res, GError **error);
void wp_base_endpoint_link_destroy (WpBaseEndpointLink * self);

G_END_DECLS

#endif
