/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_RESERVE_NODE_H__
#define __WIREPLUMBER_RESERVE_NODE_H__

#include <wp/wp.h>

#include "reserve-device.h"

G_BEGIN_DECLS

#define WP_TYPE_RESERVE_NODE (wp_reserve_node_get_type ())

G_DECLARE_FINAL_TYPE (WpReserveNode, wp_reserve_node, WP, RESERVE_NODE, GObject)

WpReserveNode * wp_reserve_node_new (WpProxy *node,
    WpReserveDevice *device_data);

void
wp_reserve_node_timeout_release (WpReserveNode *self, guint64 timeout_ms);

void
wp_reserve_node_acquire (WpReserveNode *self);

G_END_DECLS

#endif
