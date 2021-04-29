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
 * WP_TYPE_SI_LINKABLE:
 *
 * The #WpSiLinkable #GType
 */
#define WP_TYPE_SI_LINKABLE (wp_si_linkable_get_type ())
WP_API
G_DECLARE_INTERFACE (WpSiLinkable, wp_si_linkable,
                     WP, SI_LINKABLE, WpSessionItem)

struct _WpSiLinkableInterface
{
  GTypeInterface interface;

  GVariant * (*get_ports) (WpSiLinkable * self, const gchar * context);
  WpSiAcquisition * (*get_acquisition) (WpSiLinkable * self);
};

WP_API
GVariant * wp_si_linkable_get_ports (WpSiLinkable * self,
    const gchar * context);

WP_API
WpSiAcquisition * wp_si_linkable_get_acquisition (WpSiLinkable * self);

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

  WpSiLinkable * (*get_out_item) (WpSiLink * self);
  WpSiLinkable * (*get_in_item) (WpSiLink * self);
};

WP_API
GVariant * wp_si_link_get_registration_info (WpSiLink * self);

WP_API
WpProperties * wp_si_link_get_properties (WpSiLink * self);

WP_API
WpSiLinkable * wp_si_link_get_out_item (WpSiLink * self);

WP_API
WpSiLinkable * wp_si_link_get_in_item (WpSiLink * self);

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
      WpSiLinkable * item, GAsyncReadyCallback callback, gpointer data);
  gboolean (*acquire_finish) (WpSiAcquisition * self, GAsyncResult * res,
      GError ** error);

  void (*release) (WpSiAcquisition * self, WpSiLink * acquisitor,
      WpSiLinkable * item);
};

WP_API
void wp_si_acquisition_acquire (WpSiAcquisition * self, WpSiLink * acquisitor,
    WpSiLinkable * item, GAsyncReadyCallback callback, gpointer data);

WP_API
gboolean wp_si_acquisition_acquire_finish (
    WpSiAcquisition * self, GAsyncResult * res, GError ** error);

WP_API
void wp_si_acquisition_release (WpSiAcquisition * self, WpSiLink * acquisitor,
    WpSiLinkable * item);

G_END_DECLS

#endif
