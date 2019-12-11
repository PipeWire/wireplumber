/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_PARSER_ENDPOINT_H__
#define __WIREPLUMBER_PARSER_ENDPOINT_H__

#include <wp/wp.h>

G_BEGIN_DECLS

#define WP_PARSER_ENDPOINT_EXTENSION "endpoint"

struct WpParserEndpointData {
  struct MatchNode {
    guint priority;
    WpProperties *props;
  } mn;
  struct Endpoint {
    char *name;
    char *media_class;
    guint direction;
    guint priority;
    WpProperties *props;
    char *type;
    char *streams;
  } e;
};

#define WP_TYPE_PARSER_ENDPOINT (wp_parser_endpoint_get_type ())
G_DECLARE_FINAL_TYPE (WpParserEndpoint, wp_parser_endpoint,
    WP, PARSER_ENDPOINT, GObject)

G_END_DECLS

#endif
