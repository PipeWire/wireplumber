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

G_BEGIN_DECLS

struct pw_impl_node;

/*!
 * \brief The state of the node
 * \ingroup wpnode
 */
typedef enum {
  /*! error state */
  WP_NODE_STATE_ERROR = -1,
  /*! the node is being created */
  WP_NODE_STATE_CREATING = 0,
  /*! the node is suspended, the device might be closed */
  WP_NODE_STATE_SUSPENDED = 1,
  /*! the node is running but there is no active port */
  WP_NODE_STATE_IDLE = 2,
  /*! the node is running */
  WP_NODE_STATE_RUNNING = 3,
} WpNodeState;

/*!
 * \brief The WpNode GType
 * \ingroup wpnode
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
void wp_node_send_command (WpNode * self, const gchar *command);

/*!
 * \brief The WpImplNode GType
 * \ingroup wpimplnode
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
