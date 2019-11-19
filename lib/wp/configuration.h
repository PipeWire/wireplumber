/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_CONFIGURATION_H__
#define __WIREPLUMBER_CONFIGURATION_H__

#include "core.h"

G_BEGIN_DECLS

#define WP_TYPE_CONFIG_PARSER (wp_config_parser_get_type ())
G_DECLARE_INTERFACE (WpConfigParser, wp_config_parser, WP,
  CONFIG_PARSER, GObject)

struct _WpConfigParserInterface
{
  GTypeInterface parent;

  gboolean (*add_file) (WpConfigParser *parser, const gchar *name);
  gconstpointer (*get_matched_data) (WpConfigParser *parser, gpointer data);
  void (*reset) (WpConfigParser *parser);
};

gboolean wp_config_parser_add_file (WpConfigParser *self, const char *location);
gconstpointer wp_config_parser_get_matched_data (WpConfigParser *self,
    gpointer data);
void wp_config_parser_reset (WpConfigParser *self);

#define WP_TYPE_CONFIGURATION (wp_configuration_get_type ())
G_DECLARE_FINAL_TYPE (WpConfiguration, wp_configuration, WP, CONFIGURATION,
  GObject)

WpConfiguration * wp_configuration_get_instance (WpCore *core);
void wp_configuration_add_path (WpConfiguration *self, const char *path);
void wp_configuration_remove_path (WpConfiguration *self, const char *path);
gboolean wp_configuration_add_extension (WpConfiguration *self,
    const gchar * extension, GType parser_type);
gboolean wp_configuration_remove_extension (WpConfiguration *self,
    const gchar * extension);
WpConfigParser *wp_configuration_get_parser (WpConfiguration *self,
    const char *extension);
void wp_configuration_reload (WpConfiguration *self, const char *extension);

G_END_DECLS

#endif
