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

G_BEGIN_DECLS

typedef struct _WpSiEndpointAcquisition WpSiEndpointAcquisition;

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

  WpSiEndpointAcquisition * (*get_endpoint_acquisition) (WpSiEndpoint * self);
};

WP_API
GVariant * wp_si_endpoint_get_registration_info (WpSiEndpoint * self);

WP_API
WpProperties * wp_si_endpoint_get_properties (WpSiEndpoint * self);

WP_API
WpSiEndpointAcquisition * wp_si_endpoint_get_endpoint_acquisition (
    WpSiEndpoint * self);

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

  WpSiEndpoint * (*get_out_endpoint) (WpSiLink * self);
  WpSiEndpoint * (*get_in_endpoint) (WpSiLink * self);
};

WP_API
GVariant * wp_si_link_get_registration_info (WpSiLink * self);

WP_API
WpProperties * wp_si_link_get_properties (WpSiLink * self);

WP_API
WpSiEndpoint * wp_si_link_get_out_endpoint (WpSiLink * self);

WP_API
WpSiEndpoint * wp_si_link_get_in_endpoint (WpSiLink * self);

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
 * WP_TYPE_SI_ENDPOINT_ACQUISITION:
 *
 * The #WpSiEndpointAcquisition #GType
 */
#define WP_TYPE_SI_ENDPOINT_ACQUISITION (wp_si_endpoint_acquisition_get_type ())
WP_API
G_DECLARE_INTERFACE (WpSiEndpointAcquisition, wp_si_endpoint_acquisition,
                     WP, SI_ENDPOINT_ACQUISITION, WpSessionItem)

struct _WpSiEndpointAcquisitionInterface
{
  GTypeInterface interface;

  void (*acquire) (WpSiEndpointAcquisition * self, WpSiLink * acquisitor,
      WpSiEndpoint * endpoint, GAsyncReadyCallback callback, gpointer data);
  gboolean (*acquire_finish) (WpSiEndpointAcquisition * self,
      GAsyncResult * res, GError ** error);

  void (*release) (WpSiEndpointAcquisition * self, WpSiLink * acquisitor,
      WpSiEndpoint * endpoint);
};

WP_API
void wp_si_endpoint_acquisition_acquire (WpSiEndpointAcquisition * self,
    WpSiLink * acquisitor, WpSiEndpoint * endpoint,
    GAsyncReadyCallback callback, gpointer data);

WP_API
gboolean wp_si_endpoint_acquisition_acquire_finish (
    WpSiEndpointAcquisition * self, GAsyncResult * res, GError ** error);

WP_API
void wp_si_endpoint_acquisition_release (WpSiEndpointAcquisition * self,
    WpSiLink * acquisitor, WpSiEndpoint * endpoint);

G_END_DECLS

#endif
