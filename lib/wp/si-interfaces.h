/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_SI_INTERFACES_H__
#define __WIREPLUMBER_SI_INTERFACES_H__

#include "session-item.h"
#include "properties.h"
#include "endpoint.h"
#include "endpoint-link.h"

G_BEGIN_DECLS

typedef struct _WpSiStream WpSiStream;
typedef struct _WpSiStreamAcquisition WpSiStreamAcquisition;

/**
 * WP_TYPE_SI_ENDPOINT:
 *
 * The #WpSiEndpoint #GType
 */
#define WP_TYPE_SI_ENDPOINT (wp_si_endpoint_get_type ())
WP_API
G_DECLARE_INTERFACE (WpSiEndpoint, wp_si_endpoint,
                     WP, SI_ENDPOINT, WpSessionItem)

struct _WpSiEndpointInterface
{
  GTypeInterface interface;

  GVariant * (*get_registration_info) (WpSiEndpoint * self);
  WpProperties * (*get_properties) (WpSiEndpoint * self);

  guint (*get_n_streams) (WpSiEndpoint * self);
  WpSiStream * (*get_stream) (WpSiEndpoint * self, guint index);

  WpSiStreamAcquisition * (*get_stream_acquisition) (WpSiEndpoint * self);
};

WP_API
GVariant * wp_si_endpoint_get_registration_info (WpSiEndpoint * self);

WP_API
WpProperties * wp_si_endpoint_get_properties (WpSiEndpoint * self);

WP_API
guint wp_si_endpoint_get_n_streams (WpSiEndpoint * self);

WP_API
WpSiStream * wp_si_endpoint_get_stream (WpSiEndpoint * self, guint index);

WP_API
WpSiStreamAcquisition * wp_si_endpoint_get_stream_acquisition (WpSiEndpoint * self);

/**
 * WP_TYPE_SI_STREAM:
 *
 * The #WpSiStream #GType
 */
#define WP_TYPE_SI_STREAM (wp_si_stream_get_type ())
WP_API
G_DECLARE_INTERFACE (WpSiStream, wp_si_stream,
                     WP, SI_STREAM, WpSessionItem)

struct _WpSiStreamInterface
{
  GTypeInterface interface;

  GVariant * (*get_registration_info) (WpSiStream * self);
  WpProperties * (*get_properties) (WpSiStream * self);

  WpSiEndpoint * (*get_parent_endpoint) (WpSiStream * self);
};

WP_API
GVariant * wp_si_stream_get_registration_info (WpSiStream * self);

WP_API
WpProperties * wp_si_stream_get_properties (WpSiStream * self);

WP_API
WpSiEndpoint * wp_si_stream_get_parent_endpoint (WpSiStream * self);

/**
 * WP_TYPE_SI_LINK:
 *
 * The #WpSiLink #GType
 */
#define WP_TYPE_SI_LINK (wp_si_link_get_type ())
WP_API
G_DECLARE_INTERFACE (WpSiLink, wp_si_link,
                     WP, SI_LINK, WpSessionItem)

struct _WpSiLinkInterface
{
  GTypeInterface interface;

  GVariant * (*get_registration_info) (WpSiLink * self);
  WpProperties * (*get_properties) (WpSiLink * self);

  WpSiStream * (*get_out_stream) (WpSiLink * self);
  WpSiStream * (*get_in_stream) (WpSiLink * self);
};

WP_API
GVariant * wp_si_link_get_registration_info (WpSiLink * self);

WP_API
WpProperties * wp_si_link_get_properties (WpSiLink * self);

WP_API
WpSiStream * wp_si_link_get_out_stream (WpSiLink * self);

WP_API
WpSiStream * wp_si_link_get_in_stream (WpSiLink * self);

/**
 * WP_TYPE_SI_PORT_INFO:
 *
 * The #WpSiPortInfo #GType
 */
#define WP_TYPE_SI_PORT_INFO (wp_si_port_info_get_type ())
WP_API
G_DECLARE_INTERFACE (WpSiPortInfo, wp_si_port_info,
                     WP, SI_PORT_INFO, WpSessionItem)

struct _WpSiPortInfoInterface
{
  GTypeInterface interface;

  GVariant * (*get_ports) (WpSiPortInfo * self, const gchar * context);
};

WP_API
GVariant * wp_si_port_info_get_ports (WpSiPortInfo * self,
    const gchar * context);

/**
 * WP_TYPE_SI_STREAM_ACQUISITION:
 *
 * The #WpSiStreamAcquisition #GType
 */
#define WP_TYPE_SI_STREAM_ACQUISITION (wp_si_stream_acquisition_get_type ())
WP_API
G_DECLARE_INTERFACE (WpSiStreamAcquisition, wp_si_stream_acquisition,
                     WP, SI_STREAM_ACQUISITION, WpSessionItem)

struct _WpSiStreamAcquisitionInterface
{
  GTypeInterface interface;

  void (*acquire) (WpSiStreamAcquisition * self, WpSiLink * acquisitor,
      WpSiStream * stream, GAsyncReadyCallback callback, gpointer data);
  gboolean (*acquire_finish) (WpSiStreamAcquisition * self,
      GAsyncResult * res, GError ** error);

  void (*release) (WpSiStreamAcquisition * self, WpSiLink * acquisitor,
      WpSiStream * stream);
};

WP_API
void wp_si_stream_acquisition_acquire (WpSiStreamAcquisition * self,
    WpSiLink * acquisitor, WpSiStream * stream,
    GAsyncReadyCallback callback, gpointer data);

WP_API
gboolean wp_si_stream_acquisition_acquire_finish (WpSiStreamAcquisition * self,
    GAsyncResult * res, GError ** error);

WP_API
void wp_si_stream_acquisition_release (WpSiStreamAcquisition * self,
    WpSiLink * acquisitor, WpSiStream * stream);

G_END_DECLS

#endif
