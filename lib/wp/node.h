/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_NODE_H__
#define __WIREPLUMBER_NODE_H__

#include "global-proxy.h"
#include "port.h"
#include "iterator.h"
#include "object-interest.h"

G_BEGIN_DECLS

struct pw_impl_node;

/*!
 * @memberof WpNode
 *
 * @brief
 * @arg WP_NODE_STATE_ERROR: error state
 * @arg WP_NODE_STATE_CREATING: the node is being created
 * @arg WP_NODE_STATE_SUSPENDED: the node is suspended, the device might be closed
 * @arg WP_NODE_STATE_IDLE: the node is running but there is no active port
 * @arg WP_NODE_STATE_RUNNING: the node is running
 */
typedef enum {
  WP_NODE_STATE_ERROR = -1,
  WP_NODE_STATE_CREATING = 0,
  WP_NODE_STATE_SUSPENDED = 1,
  WP_NODE_STATE_IDLE = 2,
  WP_NODE_STATE_RUNNING = 3,
} WpNodeState;

/*!
 * @memberof WpNode
 *
 * @brief
 * @arg WP_NODE_FEATURE_PORTS: caches information about ports, enabling
 *   the use of wp_node_get_n_ports(), wp_node_lookup_port(),
 *   wp_node_new_ports_iterator() and related methods
 *
 * An extension of [WpProxyFeatures](@ref proxy_features_section)
 */
typedef enum { /*< flags >*/
  WP_NODE_FEATURE_PORTS = (WP_PROXY_FEATURE_CUSTOM_START << 0),
} WpNodeFeatures;

/*!
 * @memberof WpNode
 *
 * @brief The [WpNode](@ref node_section)
 * <a href="https://developer.gnome.org/gobject/stable/gobject-Type-Information.html#GType">
 * GType</a>
 *
 * @code
 * #define WP_TYPE_NODE (wp_node_get_type ())
 * @endcode
 *
 */
#define WP_TYPE_NODE (wp_node_get_type ())
WP_API
G_DECLARE_FINAL_TYPE (WpNode, wp_node, WP, NODE, WpGlobalProxy)

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
WpIterator * wp_node_new_ports_iterator (WpNode * self);

WP_API
WpIterator * wp_node_new_ports_filtered_iterator (WpNode * self, ...)
    G_GNUC_NULL_TERMINATED;

WP_API
WpIterator * wp_node_new_ports_filtered_iterator_full (WpNode * self,
    WpObjectInterest * interest);

WP_API
WpPort * wp_node_lookup_port (WpNode * self, ...) G_GNUC_NULL_TERMINATED;

WP_API
WpPort * wp_node_lookup_port_full (WpNode * self, WpObjectInterest * interest);

WP_API
void wp_node_send_command (WpNode * self, const gchar *command);

/*!
 * @memberof WpNode
 *
 * @brief The [WpImplNode](@ref impl_node_section)
 * <a href="https://developer.gnome.org/gobject/stable/gobject-Type-Information.html#GType">
 * GType</a>
 *
 * @code
 * #define WP_TYPE_IMPL_NODE (wp_impl_node_get_type ())
 * @endcode
 *
 */
#define WP_TYPE_IMPL_NODE (wp_impl_node_get_type ())
WP_API
G_DECLARE_FINAL_TYPE (WpImplNode, wp_impl_node, WP, IMPL_NODE, WpProxy)

WP_API
WpImplNode * wp_impl_node_new_wrap (WpCore * core, struct pw_impl_node * node);

WP_API
WpImplNode * wp_impl_node_new_from_pw_factory (WpCore * core,
    const gchar * factory_name, WpProperties * properties);

G_END_DECLS

#endif
