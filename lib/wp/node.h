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

struct pw_impl_node;

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
