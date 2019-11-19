/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "configuration.h"
#include "private.h"

struct _WpConfiguration
{
  GObject parent;

  GPtrArray *paths;
  GHashTable *parsers;
};

G_DEFINE_INTERFACE (WpConfigParser, wp_config_parser, G_TYPE_OBJECT)

static void
wp_config_parser_default_init (WpConfigParserInterface *klass)
{
}

gboolean
wp_config_parser_add_file (WpConfigParser *self, const char *location)
{
  g_return_val_if_fail (WP_IS_CONFIG_PARSER (self), FALSE);
  g_return_val_if_fail (WP_CONFIG_PARSER_GET_IFACE (self)->add_file, FALSE);

  return WP_CONFIG_PARSER_GET_IFACE (self)->add_file (self, location);
}

gconstpointer
wp_config_parser_get_matched_data (WpConfigParser *self, gpointer data)
{
  g_return_val_if_fail (WP_IS_CONFIG_PARSER (self), NULL);
  g_return_val_if_fail (WP_CONFIG_PARSER_GET_IFACE (self)->get_matched_data, NULL);

  return WP_CONFIG_PARSER_GET_IFACE (self)->get_matched_data (self, data);
}

void
wp_config_parser_reset (WpConfigParser *self)
{
  g_return_if_fail (WP_IS_CONFIG_PARSER (self));
  g_return_if_fail (WP_CONFIG_PARSER_GET_IFACE (self)->reset);

  WP_CONFIG_PARSER_GET_IFACE (self)->reset (self);
}

G_DEFINE_TYPE (WpConfiguration, wp_configuration, G_TYPE_OBJECT)

static void
wp_configuration_finalize (GObject * obj)
{
  WpConfiguration * self = WP_CONFIGURATION (obj);

  g_clear_pointer (&self->paths, g_ptr_array_unref);
  g_clear_pointer (&self->parsers, g_hash_table_unref);

  G_OBJECT_CLASS (wp_configuration_parent_class)->finalize (obj);
}

static void
wp_configuration_init (WpConfiguration * self)
{
  self->paths = g_ptr_array_new_with_free_func (g_free);
  self->parsers = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
      g_object_unref);
}

static void
wp_configuration_class_init (WpConfigurationClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;

  object_class->finalize = wp_configuration_finalize;
}

WpConfiguration *
wp_configuration_get_instance (WpCore *core)
{
  WpConfiguration *self;

  g_return_val_if_fail (WP_IS_CORE (core), NULL);

  self = wp_core_find_object (core, (GEqualFunc) WP_IS_CONFIGURATION, NULL);
  if (!self) {
    self = g_object_new (WP_TYPE_CONFIGURATION, NULL);
    wp_core_register_object (core, g_object_ref (self));
  }

  return self;
}

void
wp_configuration_add_path (WpConfiguration *self, const char *path)
{
  guint i;

  g_return_if_fail (self);
  g_return_if_fail (WP_IS_CONFIGURATION (self));

  /* Make sure the path is not already added */
  for (i = 0; i < self->paths->len; i++) {
    const char *p = g_ptr_array_index(self->paths, i);
    if (g_strcmp0(p, path) == 0)
      return;
  }

  g_ptr_array_add (self->paths, g_strdup (path));
}

void
wp_configuration_remove_path (WpConfiguration *self, const char *path)
{
  guint i;

  g_return_if_fail (self);
  g_return_if_fail (WP_IS_CONFIGURATION (self));

  /* Find the path index */
  for (i = 0; i < self->paths->len; i++) {
    const char *p = g_ptr_array_index(self->paths, i);
    if (g_strcmp0(p, path) == 0)
      break;
  }

  /* Only remove the path if the index is valid */
  if (i < self->paths->len)
    g_ptr_array_remove_index (self->paths, i);
}

gboolean
wp_configuration_add_extension (WpConfiguration *self, const gchar * extension,
    GType parser_type)
{
  g_return_val_if_fail (self, FALSE);
  g_return_val_if_fail (WP_IS_CONFIGURATION (self), FALSE);

  /* create the parser */
  g_autoptr (WpConfigParser) parser = g_object_new (parser_type, FALSE);
  g_return_val_if_fail (WP_IS_CONFIG_PARSER (parser), FALSE);

  return g_hash_table_insert (self->parsers, g_strdup (extension),
      g_steal_pointer (&parser));

}

gboolean
wp_configuration_remove_extension (WpConfiguration *self,
    const gchar * extension)
{
  g_return_val_if_fail (self, FALSE);
  g_return_val_if_fail (WP_IS_CONFIGURATION (self), FALSE);

  return g_hash_table_remove (self->parsers, extension);
}

WpConfigParser *
wp_configuration_get_parser (WpConfiguration *self, const char *extension)
{
  WpConfigParser *parser = NULL;

  g_return_val_if_fail (self, NULL);
  g_return_val_if_fail (WP_IS_CONFIGURATION (self), NULL);

  parser = g_hash_table_lookup (self->parsers, extension);
  return parser ? g_object_ref (parser) : NULL;
}

void
wp_configuration_reload (WpConfiguration *self, const char *extension)
{
  guint i;
  const char *path = NULL;
  GDir* conf_dir = NULL;
  GError* error = NULL;
  const gchar *file_name = NULL;
  g_autofree gchar *ext = NULL;
  g_autofree gchar *location = NULL;

  g_return_if_fail (self);
  g_return_if_fail (WP_IS_CONFIGURATION (self));

  /* Get the parser for the extension */
  WpConfigParser *parser = g_hash_table_lookup (self->parsers, extension);
  if (!parser) {
    g_warning ("Could not find parser for extension '%s'", extension);
    return;
  }

  /* Reset the parser */
  wp_config_parser_reset (parser);

  /* Load extension files in all paths */
  for (i = 0; i < self->paths->len; i++) {
    /* Get the path */
    path = g_ptr_array_index(self->paths, i);

    /* Open the directory */
    conf_dir = g_dir_open (path, 0, &error);
    if (!conf_dir) {
      g_warning ("Could not open configuration path '%s'", path);
      continue;
    }

    /* Parse each configuration file matching the extension */
    ext = g_strdup_printf (".%s", extension);
    while ((file_name = g_dir_read_name (conf_dir))) {
      /* Only parse files that have the proper extension */
      if (g_str_has_suffix (file_name, ext)) {
        location = g_build_filename (path, file_name, NULL);
        if (!wp_config_parser_add_file (parser, location))
          g_warning ("Failed to parse file '%s'", location);
      }
    }

    /* Close the directory */
    g_dir_close (conf_dir);
  }
}
