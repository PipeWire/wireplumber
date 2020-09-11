/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * SECTION: WpConfiguration
 *
 * The #WpConfiguration class manages configuration files and parsers
 */

#define G_LOG_DOMAIN "wp-configuration"

#include "configuration.h"
#include "debug.h"
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

/**
 * wp_config_parser_add_file: (virtual add_file)
 * @self: the parser
 * @location: path to a configuration file
 *
 * Adds the file at @location on the parser and parses all the information
 * from it, making it available to the code that needs this configuration
 *
 * Returns: %TRUE on success, %FALSE if an error occurred
 */
gboolean
wp_config_parser_add_file (WpConfigParser *self, const char *location)
{
  g_return_val_if_fail (WP_IS_CONFIG_PARSER (self), FALSE);
  g_return_val_if_fail (WP_CONFIG_PARSER_GET_IFACE (self)->add_file, FALSE);

  return WP_CONFIG_PARSER_GET_IFACE (self)->add_file (self, location);
}

/**
 * wp_config_parser_get_matched_data: (virtual get_matched_data)
 * @self: the parser
 * @data: implementation-specific data
 *
 * Returns: the matched data
 */
gconstpointer
wp_config_parser_get_matched_data (WpConfigParser *self, gpointer data)
{
  g_return_val_if_fail (WP_IS_CONFIG_PARSER (self), NULL);
  g_return_val_if_fail (WP_CONFIG_PARSER_GET_IFACE (self)->get_matched_data, NULL);

  return WP_CONFIG_PARSER_GET_IFACE (self)->get_matched_data (self, data);
}

/**
 * wp_config_parser_reset: (virtual reset)
 * @self: the parser
 *
 * Resets the state of the parser
 */
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

/**
 * wp_configuration_get_instance:
 * @core: the core
 *
 * Retrieves (and creates, the first time) the instance of #WpConfiguration
 * that is registered on the specified @core
 *
 * Returns: (transfer full): the @core-specific instance of #WpConfiguration
 */
WpConfiguration *
wp_configuration_get_instance (WpCore *core)
{
  WpConfiguration *self;

  g_return_val_if_fail (WP_IS_CORE (core), NULL);

  self = wp_registry_find_object (wp_core_get_registry (core),
      (GEqualFunc) WP_IS_CONFIGURATION, NULL);
  if (!self) {
    self = g_object_new (WP_TYPE_CONFIGURATION, NULL);
    wp_registry_register_object (wp_core_get_registry (core),
        g_object_ref (self));
  }

  return self;
}

/**
 * wp_configuration_add_path:
 * @self: the configuration
 * @path: path to a directory that contains configuration files
 *
 * Adds the specified @path in the list of directories that are being
 * searched for configuration files. All files in this directory that
 * have a known extension to this #WpConfiguration instance will be parsed
 * and made available through their #WpConfigParser
 */
void
wp_configuration_add_path (WpConfiguration *self, const char *path)
{
  guint i;

  g_return_if_fail (WP_IS_CONFIGURATION (self));

  /* Make sure the path is not already added */
  for (i = 0; i < self->paths->len; i++) {
    const char *p = g_ptr_array_index(self->paths, i);
    if (g_strcmp0(p, path) == 0)
      return;
  }

  g_ptr_array_add (self->paths, g_strdup (path));
}

/**
 * wp_configuration_remove_path:
 * @self: the configuration
 * @path: path to a directory that was previously added with
 *    wp_configuration_add_path()
 *
 * Removes the specified @path from the list of directories that are being
 * searched for configuration files
 */
void
wp_configuration_remove_path (WpConfiguration *self, const char *path)
{
  guint i;

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

/**
 * wp_configuration_find_file:
 * @self: the configuration
 * @filename: the name of the file to find
 *
 * Searches all known configuration directories for a file named @filename
 * and returns the absolute path to it, or %NULL if it was not found
 *
 * Returns: (transfer full) (nullable): the absolute path to the file, if found
 */
gchar *
wp_configuration_find_file (WpConfiguration * self, const gchar * filename)
{
  for (gint i = 0; i < self->paths->len; i++) {
    g_autofree gchar *path = NULL;

    path = g_build_filename (g_ptr_array_index (self->paths, i), filename, NULL);
    if (g_file_test (path, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_REGULAR)) {
      return g_steal_pointer (&path);
    }
  }

  return NULL;
}

/**
 * wp_configuration_add_extension:
 * @self: the configuration
 * @extension: a filename extension
 * @parser_type: a type that implements the #WpConfigParser interface
 *
 * Creates a parser and associates it with the specified filename @extension.
 * All configuration files that match this extension will, upon calling
 * wp_configuration_reload(), be added to this parser
 *
 * Returns: %TRUE if the extension is new, %FALSE if it was already added
 *   or an error occurred
 */
gboolean
wp_configuration_add_extension (WpConfiguration *self, const gchar * extension,
    GType parser_type)
{
  g_return_val_if_fail (WP_IS_CONFIGURATION (self), FALSE);

  /* create the parser */
  g_autoptr (WpConfigParser) parser = g_object_new (parser_type, NULL);
  g_return_val_if_fail (WP_IS_CONFIG_PARSER (parser), FALSE);

  return g_hash_table_insert (self->parsers, g_strdup (extension),
      g_steal_pointer (&parser));

}

/**
 * wp_configuration_remove_extension:
 * @self: the configuration
 * @extension: a filename extension that was previously associated with a
 *    parser using wp_configuration_add_extension()
 *
 * Removes the association of @extension to a parser and destroys the parser
 *
 * Returns: %TRUE if the extension was indeed removed,
 *    %FALSE if it was not added
 */
gboolean
wp_configuration_remove_extension (WpConfiguration *self,
    const gchar * extension)
{
  g_return_val_if_fail (WP_IS_CONFIGURATION (self), FALSE);

  return g_hash_table_remove (self->parsers, extension);
}

/**
 * wp_configuration_get_parser:
 * @self: the configuration
 * @extension: a filename extension that was previously associated with a
 *    parser using wp_configuration_add_extension()
 *
 * Returns: (transfer full) (nullable): the parser associated with @extension
 */
WpConfigParser *
wp_configuration_get_parser (WpConfiguration *self, const char *extension)
{
  WpConfigParser *parser = NULL;

  g_return_val_if_fail (WP_IS_CONFIGURATION (self), NULL);

  parser = g_hash_table_lookup (self->parsers, extension);
  return parser ? g_object_ref (parser) : NULL;
}

/**
 * wp_configuration_reload:
 * @self: the configuration
 * @extension: a filename extension that was previously associated with a
 *    parser using wp_configuration_add_extension()
 *
 * Resets the parser associated with @extension and re-adds (and re-parses)
 * all the configuration files that have this @extension from all the
 * directories that were added with wp_configuration_add_path()
 */
void
wp_configuration_reload (WpConfiguration *self, const char *extension)
{
  guint i;
  const gchar *base_path = NULL;
  const gchar *e = NULL;
  gchar path[1024];
  gchar ext[16];
  GDir *conf_dir = NULL;
  GError *error = NULL;
  const gchar *file_name = NULL;

  g_return_if_fail (WP_IS_CONFIGURATION (self));

  /* Get the parser for the extension */
  WpConfigParser *parser = g_hash_table_lookup (self->parsers, extension);
  if (!parser) {
    wp_warning_object (self, "Could not find parser for extension '%s'",
        extension);
    return;
  }

  /* Reset the parser */
  wp_config_parser_reset (parser);

  /* figure out the actual file extension */
  e = g_strrstr (extension, "/");
  if (e) {
    g_snprintf (ext, sizeof(ext)-1, ".%s", e+1);
  } else {
    g_snprintf (ext, sizeof(ext)-1, ".%s", extension);
  }

  /* Load extension files in all paths */
  for (i = 0; i < self->paths->len; i++) {
    /* Get the path */
    base_path = g_ptr_array_index(self->paths, i);

    /* append subdir, if specified in the extension string */
    if (e) {
      g_snprintf (path, sizeof(path)-1, "%s" G_DIR_SEPARATOR_S "%.*s",
          base_path, (int) (e - extension), extension);
    } else {
      g_snprintf (path, sizeof(path)-1, "%s", base_path);
    }

    /* Open the directory */
    conf_dir = g_dir_open (path, 0, &error);
    if (!conf_dir) {
      wp_message_object (self, "Could not open configuration path '%s': %s",
          path, error->message);
      g_clear_error (&error);
      continue;
    }

    /* Parse each configuration file matching the extension */
    while ((file_name = g_dir_read_name (conf_dir))) {
      /* Only parse files that have the proper extension */
      if (g_str_has_suffix (file_name, ext)) {
        g_autofree gchar * location = g_build_filename (path, file_name, NULL);

        wp_debug_object (self, "loading config file: %s", location);

        if (!wp_config_parser_add_file (parser, location))
          wp_warning_object (self, "Failed to parse file '%s'", location);
      }
    }

    /* Close the directory */
    g_dir_close (conf_dir);
  }
}
