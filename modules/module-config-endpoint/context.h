/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_CONFIG_ENDPOINT_CONTEXT_H__
#define __WIREPLUMBER_CONFIG_ENDPOINT_CONTEXT_H__

#include <wp/wp.h>

G_BEGIN_DECLS

#define WP_TYPE_CONFIG_ENDPOINT_CONTEXT (wp_config_endpoint_context_get_type ())
G_DECLARE_FINAL_TYPE (WpConfigEndpointContext, wp_config_endpoint_context,
    WP, CONFIG_ENDPOINT_CONTEXT, GObject);

WpConfigEndpointContext * wp_config_endpoint_context_new (WpCore *core);

G_END_DECLS

#endif
