/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_ANATOLE_CONFIG_PARSER_H__
#define __WIREPLUMBER_ANATOLE_CONFIG_PARSER_H__

#include <wp/wp.h>
#include <anatole.h>

G_BEGIN_DECLS

#define WP_TYPE_ANATOLE_CONFIG_PARSER \
    (wp_anatole_config_parser_get_type ())
G_DECLARE_FINAL_TYPE (WpAnatoleConfigParser, wp_anatole_config_parser,
                      WP, ANATOLE_CONFIG_PARSER, GObject)

AnatoleEngine * wp_anatole_config_parser_get_engine (WpAnatoleConfigParser * self);

G_END_DECLS

#endif
