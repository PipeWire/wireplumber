/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wptoml/wptoml.h>

#include <pipewire/pipewire.h>

#include "parser-endpoint.h"

struct _WpParserEndpoint
{
  GObject parent;

  GPtrArray *datas;
};

static void wp_parser_endpoint_config_parser_init (gpointer iface,
    gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (WpParserEndpoint, wp_parser_endpoint,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (WP_TYPE_CONFIG_PARSER,
                         wp_parser_endpoint_config_parser_init))

static void
wp_parser_endpoint_data_destroy (gpointer p)
{
  struct WpParserEndpointData *data = p;

  g_clear_pointer (&data->filename, g_free);
  g_clear_pointer (&data->mn.props, wp_properties_unref);
  g_clear_pointer (&data->e.session, g_free);
  g_clear_pointer (&data->e.type, g_free);
  g_clear_pointer (&data->e.streams, g_free);
  g_clear_pointer (&data->e.c.name, g_free);
  g_clear_pointer (&data->e.c.media_class, g_free);
  g_clear_pointer (&data->e.c.role, g_free);

  g_slice_free (struct WpParserEndpointData, data);
}

static void
parse_properties_for_each (const WpTomlTable *table, gpointer user_data)
{
  WpProperties *props = user_data;
  g_return_if_fail (props);

  /* Skip unparsed tables */
  if (!table)
    return;

  /* Parse the name and value */
  g_autofree gchar *name = wp_toml_table_get_string (table, "name");
  g_autofree gchar *value = wp_toml_table_get_string (table, "value");

  /* Set the property */
  if (name && value)
    wp_properties_set (props, name, value);
}

static WpProperties *
parse_properties (WpTomlTable *table, const char *name)
{
  WpProperties *props = wp_properties_new_empty ();

  g_autoptr (WpTomlTableArray) properties = NULL;
  properties = wp_toml_table_get_array_table (table, name);
  if (properties)
    wp_toml_table_array_for_each (properties, parse_properties_for_each, props);

  return props;
}

static struct WpParserEndpointData *
wp_parser_endpoint_data_new (const gchar *location)
{
  g_autoptr (WpTomlFile) file = NULL;
  g_autoptr (WpTomlTable) table = NULL, mn = NULL, e = NULL, c = NULL;
  struct WpParserEndpointData *res = NULL;

  /* File format:
   * ------------
   * [match-node]
   * properties (WpProperties)
   *
   * [endpoint]
   * session (string)
   * type (string)
   * streams (string)
   *
   * [endpoint.config]
   * name (string)
   * media_class (string)
   * role (string)
   * priority (uint32)
   * enable_control_port (bool)
   * enable_monitor (bool)
   */

  /* Get the TOML file */
  file = wp_toml_file_new (location);
  if (!file)
    return NULL;

  /* Get the file table */
  table = wp_toml_file_get_table (file);
  if (!table)
    return NULL;

  /* Create the endpoint data */
  res = g_slice_new0(struct WpParserEndpointData);

  /* Set the file name */
  res->filename = g_path_get_basename (location);

  /* Get the match-node table */
  mn = wp_toml_table_get_table (table, "match-node");
  if (!mn)
    goto error;

  /* Get the match node properties */
  res->mn.props = parse_properties (mn, "properties");

  /* Get the endpoint table */
  e = wp_toml_table_get_table (table, "endpoint");
  if (!e)
    goto error;

  /* Get the endpoint session */
  res->e.session = wp_toml_table_get_string (e, "session");
  if (!res->e.session)
    goto error;

  /* Get the endpoint type */
  res->e.type = wp_toml_table_get_string (e, "type");
  if (!res->e.type)
    goto error;

  /* Get the optional streams */
  res->e.streams = wp_toml_table_get_string (e, "streams");

  /* Get the optional endpoint config table */
  c = wp_toml_table_get_table (e, "config");
  if (c) {
    res->e.c.name = wp_toml_table_get_string (c, "name");
    res->e.c.media_class = wp_toml_table_get_string (c, "media_class");
    res->e.c.role = wp_toml_table_get_string (c, "role");
    res->e.c.priority = 0;
    wp_toml_table_get_uint32 (c, "priority", &res->e.c.priority);
    res->e.c.enable_control_port = FALSE;
    wp_toml_table_get_boolean (c, "enable-control-port", &res->e.c.enable_control_port);
    res->e.c.enable_monitor = FALSE;
    wp_toml_table_get_boolean (c, "enable-monitor", &res->e.c.enable_monitor);
  }

  return res;

error:
  g_clear_pointer (&res, wp_parser_endpoint_data_destroy);
  return NULL;
}

static gint
compare_datas_func (gconstpointer a, gconstpointer b)
{
  struct WpParserEndpointData *da = *(struct WpParserEndpointData *const *)a;
  struct WpParserEndpointData *db = *(struct WpParserEndpointData *const *)b;

  return g_strcmp0 (db->filename, da->filename);
}

static gboolean
wp_parser_endpoint_add_file (WpConfigParser *parser,
    const gchar *name)
{
  WpParserEndpoint *self = WP_PARSER_ENDPOINT (parser);
  struct WpParserEndpointData *data;

  /* Parse the file */
  data = wp_parser_endpoint_data_new (name);
  if (!data) {
    wp_warning_object (parser, "Failed to parse configuration file '%s'", name);
    return FALSE;
  }

  /* Add the data to the array */
  g_ptr_array_add(self->datas, data);

  /* Sort the array by priority */
  g_ptr_array_sort(self->datas, compare_datas_func);

  return TRUE;
}

static gconstpointer
wp_parser_endpoint_get_matched_data (WpConfigParser *parser, gpointer data)
{
  WpParserEndpoint *self = WP_PARSER_ENDPOINT (parser);
  WpProxy *proxy = WP_PROXY (data);
  const struct WpParserEndpointData *d = NULL;
  g_autoptr (WpProperties) props = NULL;

  g_return_val_if_fail (proxy, NULL);
  props = wp_proxy_get_properties (proxy);

  /* Find the first data that matches node */
  for (guint i = 0; i < self->datas->len; i++) {
    d = g_ptr_array_index(self->datas, i);
    if (wp_properties_matches (props, d->mn.props))
      return d;
  }

  return NULL;
}

static void
wp_parser_endpoint_reset (WpConfigParser *parser)
{
  WpParserEndpoint *self = WP_PARSER_ENDPOINT (parser);

  g_ptr_array_set_size (self->datas, 0);
}

static void
wp_parser_endpoint_config_parser_init (gpointer iface, gpointer iface_data)
{
  WpConfigParserInterface *cp_iface = iface;

  cp_iface->add_file = wp_parser_endpoint_add_file;
  cp_iface->get_matched_data = wp_parser_endpoint_get_matched_data;
  cp_iface->reset = wp_parser_endpoint_reset;
}

static void
wp_parser_endpoint_finalize (GObject * object)
{
  WpParserEndpoint *self = WP_PARSER_ENDPOINT (object);

  g_clear_pointer (&self->datas, g_ptr_array_unref);

  G_OBJECT_CLASS (wp_parser_endpoint_parent_class)->finalize (object);
}

static void
wp_parser_endpoint_init (WpParserEndpoint * self)
{
  self->datas = g_ptr_array_new_with_free_func(
      wp_parser_endpoint_data_destroy);
}

static void
wp_parser_endpoint_class_init (WpParserEndpointClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;

  object_class->finalize = wp_parser_endpoint_finalize;
}
