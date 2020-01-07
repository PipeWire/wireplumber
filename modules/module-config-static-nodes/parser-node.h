/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_PARSER_NODE_H__
#define __WIREPLUMBER_PARSER_NODE_H__

#include <wp/wp.h>

G_BEGIN_DECLS

#define WP_PARSER_NODE_EXTENSION "node"

struct WpParserNodeData {
  struct MatchDevice {
    guint priority;
    WpProperties *props;
  } md;
  gboolean has_md;
  struct Node {
    char *factory;
    gboolean local;
    WpProperties *props;
  } n;
};

#define WP_TYPE_PARSER_NODE (wp_parser_node_get_type ())
G_DECLARE_FINAL_TYPE (WpParserNode, wp_parser_node, WP, PARSER_NODE, GObject)

typedef gboolean (*WpParserNodeForeachFunction) (
    const struct WpParserNodeData *parser_data, gpointer data);
void wp_parser_node_foreach (WpParserNode *self, WpParserNodeForeachFunction f,
    gpointer data);

G_END_DECLS

#endif
