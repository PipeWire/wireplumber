/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_PARSER_STREAMS_H__
#define __WIREPLUMBER_PARSER_STREAMS_H__

#include <wp/wp.h>

G_BEGIN_DECLS

#define WP_PARSER_STREAMS_EXTENSION "streams"

/* For simplicity, we limit the number of streams */
#define MAX_STREAMS 32

struct WpParserStreamsStreamData {
  char *name;
  guint priority;
  gboolean enable_control_port;
};

struct WpParserStreamsData {
  char *location;
  struct WpParserStreamsStreamData streams[MAX_STREAMS];
  guint n_streams;
};

/* Helpers */
const struct WpParserStreamsStreamData *wp_parser_streams_find_stream (
    const struct WpParserStreamsData *data, const char *name);
const struct WpParserStreamsStreamData *wp_parser_streams_get_lowest_stream (
    const struct WpParserStreamsData *data);

#define WP_TYPE_PARSER_STREAMS (wp_parser_streams_get_type ())
G_DECLARE_FINAL_TYPE (WpParserStreams, wp_parser_streams,
    WP, PARSER_STREAMS, GObject);

G_END_DECLS

#endif
