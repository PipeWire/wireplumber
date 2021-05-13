/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_ENDPOINT_H__
#define __WIREPLUMBER_ENDPOINT_H__

#include "global-proxy.h"
#include "port.h"
#include "iterator.h"
#include "object-interest.h"
#include "si-interfaces.h"

G_BEGIN_DECLS

/*!
 * @memberof WpEndpoint
 *
 * @brief The [WpEndpoint](@ref endpoint_section)
 * <a href="https://developer.gnome.org/gobject/stable/gobject-Type-Information.html#GType">
 * GType</a>
 *
 * @code
 * #define WP_TYPE_ENDPOINT (wp_endpoint_get_type ())
 * @endcode
 *
 */
#define WP_TYPE_ENDPOINT (wp_endpoint_get_type ())
WP_API
G_DECLARE_DERIVABLE_TYPE (WpEndpoint, wp_endpoint, WP, ENDPOINT, WpGlobalProxy)

/*!
 * @memberof WpEndpoint
 *
 * @brief
 * @em parent_class
 */
struct _WpEndpointClass
{
  WpGlobalProxyClass parent_class;
};

WP_API
const gchar * wp_endpoint_get_name (WpEndpoint * self);

WP_API
const gchar * wp_endpoint_get_media_class (WpEndpoint * self);

WP_API
WpDirection wp_endpoint_get_direction (WpEndpoint * self);

/*!
 * @memberof WpEndpoint
 *
 * @brief The [WpImplEndpoint](@ref impl_endpoint_section)
 * <a href="https://developer.gnome.org/gobject/stable/gobject-Type-Information.html#GType">
 * GType</a>
 * @code
 * #define WP_TYPE_IMPL_ENDPOINT (wp_impl_endpoint_get_type ())
 * @endcode
 *
 */
#define WP_TYPE_IMPL_ENDPOINT (wp_impl_endpoint_get_type ())
WP_API
G_DECLARE_FINAL_TYPE (WpImplEndpoint, wp_impl_endpoint,
                      WP, IMPL_ENDPOINT, WpEndpoint)

WP_API
WpImplEndpoint * wp_impl_endpoint_new (WpCore * core, WpSiEndpoint * item);

G_END_DECLS

#endif
