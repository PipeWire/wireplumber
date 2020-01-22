/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_NODE_H__
#define __WIREPLUMBER_NODE_H__

#include "proxy.h"

G_BEGIN_DECLS

#define WP_TYPE_NODE (wp_node_get_type ())
WP_API
G_DECLARE_FINAL_TYPE (WpNode, wp_node, WP, NODE, WpProxy)

G_END_DECLS

#endif
