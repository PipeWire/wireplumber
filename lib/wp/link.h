/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_LINK_H__
#define __WIREPLUMBER_LINK_H__

#include "global-proxy.h"

G_BEGIN_DECLS

/**
 * WP_TYPE_LINK:
 *
 * The #WpLink #GType
 */
#define WP_TYPE_LINK (wp_link_get_type ())
WP_API
G_DECLARE_FINAL_TYPE (WpLink, wp_link, WP, LINK, WpGlobalProxy)

WP_API
WpLink * wp_link_new_from_factory (WpCore * core,
    const gchar * factory_name, WpProperties * properties);

WP_API
void wp_link_get_linked_object_ids (WpLink * self,
    guint32 * output_node, guint32 * output_port,
    guint32 * input_node, guint32 * input_port);

G_END_DECLS

#endif
