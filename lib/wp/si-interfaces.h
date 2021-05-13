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
#include "spa-pod.h"

G_BEGIN_DECLS

typedef struct _WpSiAcquisition WpSiAcquisition;

/*!
 * @memberof WpSiEndpoint
 *
 * @brief The [WpSiEndpoint](@ref si_endpoint_section)
 * <a href="https://developer.gnome.org/gobject/stable/gobject-Type-Information.html#GType">
 * GType</a>
 *
 * @code
 * #define WP_TYPE_SI_ENDPOINT (wp_si_endpoint_get_type ())
 * @endcode
 *
 */
#define WP_TYPE_SI_ENDPOINT (wp_si_endpoint_get_type ())
WP_API
G_DECLARE_INTERFACE (WpSiEndpoint, wp_si_endpoint,
                     WP, SI_ENDPOINT, WpSessionItem)

/*!
 * @memberof WpSiEndpoint
 *
 * @brief
 * @em interface
 */
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

/*!
 * @memberof WpSiAdapter
 *
 * WP_TYPE_SI_ADAPTER:
 *
 * The #WpSiAdapter #GType
 * @brief The [WpSiAdapter](@ref si_adapter_section)
 * <a href="https://developer.gnome.org/gobject/stable/gobject-Type-Information.html#GType">
 * GType</a>
 *
 * @code
 * #define WP_TYPE_SI_ADAPTER (wp_si_adapter_get_type ())
 * @endcode
 */
#define WP_TYPE_SI_ADAPTER (wp_si_adapter_get_type ())
WP_API
G_DECLARE_INTERFACE (WpSiAdapter, wp_si_adapter,
                     WP, SI_ADAPTER, WpSessionItem)

/*!
 * @memberof WpSiAdapter
 *
 * @brief
 * @em interface
 */
struct _WpSiAdapterInterface
{
  GTypeInterface interface;

  WpSpaPod * (*get_ports_format) (WpSiAdapter * self, const gchar **mode);
  void (*set_ports_format) (WpSiAdapter * self, WpSpaPod *format,
      const gchar *mode, GAsyncReadyCallback callback, gpointer data);
  gboolean (*set_ports_format_finish) (WpSiAdapter * self, GAsyncResult * res,
      GError ** error);
};

WP_API
WpSpaPod *wp_si_adapter_get_ports_format (WpSiAdapter * self,
    const gchar **mode);

WP_API
void wp_si_adapter_set_ports_format (WpSiAdapter * self, WpSpaPod *format,
    const gchar *mode, GAsyncReadyCallback callback, gpointer data);

WP_API
gboolean wp_si_adapter_set_ports_format_finish (WpSiAdapter * self,
    GAsyncResult * res, GError ** error);

/*!
 * @memberof WpSiLinkable
 *
 * WP_TYPE_SI_LINKABLE:
 *
 * The #WpSiLinkable #GType
 * @brief The [WpSiLinkable](@ref si_linkable_section)
 * <a href="https://developer.gnome.org/gobject/stable/gobject-Type-Information.html#GType">
 * GType</a>
 *
 * @code
 * #define WP_TYPE_SI_LINKABLE (wp_si_linkable_get_type ())
 * @endcode
 */
#define WP_TYPE_SI_LINKABLE (wp_si_linkable_get_type ())
WP_API
G_DECLARE_INTERFACE (WpSiLinkable, wp_si_linkable,
                     WP, SI_LINKABLE, WpSessionItem)

/*!
 * @memberof WpSiLinkable
 *
 * @brief
 * @em interface
 */
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

/*!
 * @memberof WpSiLink
 *
 * WP_TYPE_SI_LINK:
 *
 * @brief The [WpSiLink](@ref si_link_section)
 * <a href="https://developer.gnome.org/gobject/stable/gobject-Type-Information.html#GType">
 * GType</a>
 *
 * @code
 * #define WP_TYPE_SI_LINK (wp_si_link_get_type ())
 * @endcode
 *
 */
#define WP_TYPE_SI_LINK (wp_si_link_get_type ())
WP_API
G_DECLARE_INTERFACE (WpSiLink, wp_si_link,
                     WP, SI_LINK, WpSessionItem)

/*!
 * @memberof WpSiLink
 *
 * @brief
 * @em interface
 */
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

/*!
 * @memberof WpSiAcquisition
 *
 * WP_TYPE_SI_ACQUISITION:
 *
 * The [WpSiAcquisition](@ref si_acquisition_section)
 * <a href="https://developer.gnome.org/gobject/stable/gobject-Type-Information.html#GType">
 * GType</a>
 *
 * @code
 * #define WP_TYPE_SI_ACQUISITION (wp_si_acquisition_get_type ())
 * @endcode
 *
 */
#define WP_TYPE_SI_ACQUISITION (wp_si_acquisition_get_type ())
WP_API
G_DECLARE_INTERFACE (WpSiAcquisition, wp_si_acquisition,
                     WP, SI_ACQUISITION, WpSessionItem)

/*!
 * @memberof WpSiAcquisition
 *
 * @brief
 * @em interface
 */
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
