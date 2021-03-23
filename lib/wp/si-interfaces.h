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

typedef struct _WpSiAcquisition WpSiAcquisition;

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
};

WP_API
GVariant * wp_si_endpoint_get_registration_info (WpSiEndpoint * self);

WP_API
WpProperties * wp_si_endpoint_get_properties (WpSiEndpoint * self);

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
  WpSiAcquisition * (*get_acquisition) (WpSiPortInfo * self);
};

WP_API
GVariant * wp_si_port_info_get_ports (WpSiPortInfo * self,
    const gchar * context);

WP_API
WpSiAcquisition * wp_si_port_info_get_acquisition (WpSiPortInfo * self);

/**
 * WP_TYPE_SI_ACQUISITION:
 *
 * The #WpSiAcquisition #GType
 */
#define WP_TYPE_SI_ACQUISITION (wp_si_acquisition_get_type ())
WP_API
G_DECLARE_INTERFACE (WpSiAcquisition, wp_si_acquisition,
                     WP, SI_ACQUISITION, WpSessionItem)

struct _WpSiAcquisitionInterface
{
  GTypeInterface interface;

  void (*acquire) (WpSiAcquisition * self, WpSiLink * acquisitor,
      WpSiPortInfo * item, GAsyncReadyCallback callback, gpointer data);
  gboolean (*acquire_finish) (WpSiAcquisition * self, GAsyncResult * res,
      GError ** error);

  void (*release) (WpSiAcquisition * self, WpSiLink * acquisitor,
      WpSiPortInfo * item);
};

WP_API
void wp_si_acquisition_acquire (WpSiAcquisition * self, WpSiLink * acquisitor,
    WpSiPortInfo * item, GAsyncReadyCallback callback, gpointer data);

WP_API
gboolean wp_si_acquisition_acquire_finish (
    WpSiAcquisition * self, GAsyncResult * res, GError ** error);

WP_API
void wp_si_acquisition_release (WpSiAcquisition * self, WpSiLink * acquisitor,
    WpSiPortInfo * item);

G_END_DECLS

#endif
