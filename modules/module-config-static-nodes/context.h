/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_CONFIG_STATIC_NODES_CONTEXT_H__
#define __WIREPLUMBER_CONFIG_STATIC_NODES_CONTEXT_H__

#include <wp/wp.h>

G_BEGIN_DECLS

#define WP_TYPE_CONFIG_STATIC_NODES_CONTEXT (wp_config_static_nodes_context_get_type ())
G_DECLARE_FINAL_TYPE (WpConfigStaticNodesContext, wp_config_static_nodes_context,
    WP, CONFIG_STATIC_NODES_CONTEXT, GObject);

WpConfigStaticNodesContext * wp_config_static_nodes_context_new (WpCore *core);

guint wp_config_static_nodes_context_get_length (
    WpConfigStaticNodesContext *self);

G_END_DECLS

#endif
