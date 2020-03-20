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

G_BEGIN_DECLS

typedef struct _WpSiStream WpSiStream;



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
};

WP_API
GVariant * wp_si_endpoint_get_registration_info (WpSiEndpoint * self);

WP_API
WpProperties * wp_si_endpoint_get_properties (WpSiEndpoint * self);

WP_API
guint wp_si_endpoint_get_n_streams (WpSiEndpoint * self);

WP_API
WpSiStream * wp_si_endpoint_get_stream (WpSiEndpoint * self, guint index);

/**
 * WP_TYPE_SI_MULTI_ENDPOINT:
 *
 * The #WpSiMultiEndpoint #GType
 */
#define WP_TYPE_SI_MULTI_ENDPOINT (wp_si_multi_endpoint_get_type ())
WP_API
G_DECLARE_INTERFACE (WpSiMultiEndpoint, wp_si_multi_endpoint,
                     WP, SI_MULTI_ENDPOINT, WpSessionItem)

struct _WpSiMultiEndpointInterface
{
  GTypeInterface interface;

  guint (*get_n_endpoints) (WpSiMultiEndpoint * self);
  WpSiEndpoint * (*get_endpoint) (WpSiMultiEndpoint * self, guint index);
};

WP_API
guint wp_si_multi_endpoint_get_n_endpoints (WpSiMultiEndpoint * self);

WP_API
WpSiEndpoint * wp_si_multi_endpoint_get_endpoint (WpSiMultiEndpoint * self,
    guint index);

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

  WpSiStream * (*get_out_stream) (WpSiLink * self);
  WpSiStream * (*get_in_stream) (WpSiLink * self);
};

WP_API
WpSiStream * wp_si_link_get_out_stream (WpSiLink * self);

WP_API
WpSiStream * wp_si_link_get_in_stream (WpSiLink * self);

G_END_DECLS

#endif
