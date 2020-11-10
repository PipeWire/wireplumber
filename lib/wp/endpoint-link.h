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
    guint32 * output_endpoint, guint32 * output_stream,
    guint32 * input_endpoint, guint32 * input_stream);

WP_API
WpEndpointLinkState wp_endpoint_link_get_state (WpEndpointLink * self,
    const gchar ** error);

WP_API
void wp_endpoint_link_request_state (WpEndpointLink * self,
    WpEndpointLinkState target);

G_END_DECLS

#endif
