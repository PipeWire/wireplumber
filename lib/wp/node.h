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
#include "port.h"
#include "iterator.h"

G_BEGIN_DECLS

struct pw_impl_node;

/**
 * WpNodeState:
 * @WP_NODE_STATE_ERROR: error state
 * @WP_NODE_STATE_CREATING: the node is being created
 * @WP_NODE_STATE_SUSPENDED: the node is suspended, the device might be closed
 * @WP_NODE_STATE_IDLE: the node is running but there is no active port
 * @WP_NODE_STATE_RUNNING: the node is running
 */
typedef enum {
  WP_NODE_STATE_ERROR = -1,
  WP_NODE_STATE_CREATING = 0,
  WP_NODE_STATE_SUSPENDED = 1,
  WP_NODE_STATE_IDLE = 2,
  WP_NODE_STATE_RUNNING = 3,
} WpNodeState;

/**
 * WpNodeFeatures:
 * @WP_NODE_FEATURE_PORTS: caches information about ports, enabling
 *   the use of wp_node_get_n_ports(), wp_node_find_port() and
 *   wp_node_iterate_ports()
 *
 * An extension of #WpProxyFeatures
 */
typedef enum { /*< flags >*/
  WP_NODE_FEATURE_PORTS = WP_PROXY_FEATURE_LAST,
} WpNodeFeatures;

/**
 * WP_NODE_FEATURES_STANDARD:
 *
 * A constant set of features that contains the standard features that are
 * available in the #WpNode class.
 */
#define WP_NODE_FEATURES_STANDARD \
    (WP_PROXY_FEATURES_STANDARD | \
     WP_NODE_FEATURE_PORTS)

/**
 * WP_TYPE_NODE:
 *
 * The #WpNode #GType
 */
#define WP_TYPE_NODE (wp_node_get_type ())
WP_API
G_DECLARE_DERIVABLE_TYPE (WpNode, wp_node, WP, NODE, WpProxy)

struct _WpNodeClass
{
  WpProxyClass parent_class;
};

WP_API
WpNode * wp_node_new_from_factory (WpCore * core,
    const gchar * factory_name, WpProperties * properties);

WP_API
WpNodeState wp_node_get_state (WpNode * self, const gchar ** error);

WP_API
guint wp_node_get_n_input_ports (WpNode * self, guint * max);

WP_API
guint wp_node_get_n_output_ports (WpNode * self, guint * max);

WP_API
guint wp_node_get_n_ports (WpNode * self);

WP_API
WpPort * wp_node_find_port (WpNode * self, guint32 bound_id);

WP_API
WpIterator * wp_node_iterate_ports (WpNode * self);

/**
 * WP_TYPE_IMPL_NODE:
 *
 * The #WpImplNode #GType
 */
#define WP_TYPE_IMPL_NODE (wp_impl_node_get_type ())
WP_API
G_DECLARE_FINAL_TYPE (WpImplNode, wp_impl_node, WP, IMPL_NODE, WpNode)

WP_API
WpImplNode * wp_impl_node_new_wrap (WpCore * core, struct pw_impl_node * node);

WP_API
WpImplNode * wp_impl_node_new_from_pw_factory (WpCore * core,
    const gchar * factory_name, WpProperties * properties);

G_END_DECLS

#endif
