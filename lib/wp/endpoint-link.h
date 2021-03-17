/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_ENDPOINT_LINK_H__
#define __WIREPLUMBER_ENDPOINT_LINK_H__

#include "global-proxy.h"
#include "si-interfaces.h"

G_BEGIN_DECLS

/**
 * WpEndpointLinkState:
 * @WP_ENDPOINT_LINK_STATE_ERROR:
 * @WP_ENDPOINT_LINK_STATE_PREPARING:
 * @WP_ENDPOINT_LINK_STATE_INACTIVE:
 * @WP_ENDPOINT_LINK_STATE_ACTIVE:
 */
typedef enum {
  WP_ENDPOINT_LINK_STATE_ERROR = -1,
  WP_ENDPOINT_LINK_STATE_PREPARING,
  WP_ENDPOINT_LINK_STATE_INACTIVE,
  WP_ENDPOINT_LINK_STATE_ACTIVE,
} WpEndpointLinkState;

/**
 * WP_TYPE_ENDPOINT_LINK:
 *
 * The #WpEndpointLink #GType
 */
#define WP_TYPE_ENDPOINT_LINK (wp_endpoint_link_get_type ())
WP_API
G_DECLARE_DERIVABLE_TYPE (WpEndpointLink, wp_endpoint_link,
                          WP, ENDPOINT_LINK, WpGlobalProxy)

struct _WpEndpointLinkClass
{
  WpGlobalProxyClass parent_class;
};

WP_API
void wp_endpoint_link_get_linked_object_ids (WpEndpointLink * self,
    guint32 * output_endpoint, guint32 * input_endpoint);

WP_API
WpEndpointLinkState wp_endpoint_link_get_state (WpEndpointLink * self,
    const gchar ** error);

WP_API
void wp_endpoint_link_request_state (WpEndpointLink * self,
    WpEndpointLinkState target);

/**
 * WP_TYPE_IMPL_ENDPOINT_LINK:
 *
 * The #WpImplEndpointLink #GType
 */
#define WP_TYPE_IMPL_ENDPOINT_LINK (wp_impl_endpoint_link_get_type ())
WP_API
G_DECLARE_FINAL_TYPE (WpImplEndpointLink, wp_impl_endpoint_link,
                      WP, IMPL_ENDPOINT_LINK, WpEndpointLink)

WP_API
WpImplEndpointLink * wp_impl_endpoint_link_new (WpCore * core, WpSiLink * item);

G_END_DECLS

#endif
