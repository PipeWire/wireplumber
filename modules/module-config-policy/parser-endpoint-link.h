/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_PARSER_ENDPOINT_LINK_H__
#define __WIREPLUMBER_PARSER_ENDPOINT_LINK_H__

#include <wp/wp.h>

G_BEGIN_DECLS

#define WP_PARSER_ENDPOINT_LINK_EXTENSION "endpoint-link"

struct WpParserEndpointLinkEndpointData {
  char *name;
  char *media_class;
  guint direction;
  WpProperties *props;
};

struct WpParserEndpointLinkData {
  char *filename;
  struct MatchEndpoint {
    struct WpParserEndpointLinkEndpointData endpoint_data;
  } me;
  gboolean has_te;
  struct TargetEndpoint {
    struct WpParserEndpointLinkEndpointData endpoint_data;
    char *stream;
  } te;
};

/* Helpers */
gboolean wp_parser_endpoint_link_matches_endpoint_data (WpEndpoint *ep,
    const struct WpParserEndpointLinkEndpointData *data);

#define WP_TYPE_PARSER_ENDPOINT_LINK (wp_parser_endpoint_link_get_type ())
G_DECLARE_FINAL_TYPE (WpParserEndpointLink, wp_parser_endpoint_link,
    WP, PARSER_ENDPOINT_LINK, GObject)

G_END_DECLS

#endif
