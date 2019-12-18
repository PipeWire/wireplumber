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

  /* Free the strings */
  g_clear_pointer (&data->mn.props, wp_properties_unref);
  g_clear_pointer (&data->e.name, g_free);
  g_clear_pointer (&data->e.media_class, g_free);
  g_clear_pointer (&data->e.props, wp_properties_unref);
  g_clear_pointer (&data->e.type, g_free);
  g_clear_pointer (&data->e.streams, g_free);

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

static guint
parse_endpoint_direction (const char *direction)
{
  if (g_strcmp0 (direction, "sink") == 0)
    return PW_DIRECTION_INPUT;
  else if (g_strcmp0 (direction, "source") == 0)
    return PW_DIRECTION_OUTPUT;

  g_return_val_if_reached (PW_DIRECTION_INPUT);
}

static struct WpParserEndpointData *
wp_parser_endpoint_data_new (const gchar *location)
{
  g_autoptr (WpTomlFile) file = NULL;
  g_autoptr (WpTomlTable) table = NULL, mn = NULL, e = NULL;
  g_autoptr (WpTomlArray) streams = NULL;
  struct WpParserEndpointData *res = NULL;
  g_autofree char *direction = NULL;

  /* File format:
   * ------------
   * [match-node]
   * priority (uint32)
   * properties (WpProperties)
   *
   * [endpoint]
   * name (string)
   * media_class (string)
   * direction (string)
   * priority (uint32)
   * properties (WpProperties)
   * type (string)
   * streams (string)
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

  /* Get the match-node table */
  mn = wp_toml_table_get_table (table, "match-node");
  if (!mn)
    goto error;

  /* Get the priority from the match-node table */
  res->mn.priority = 0;
  wp_toml_table_get_uint32 (mn, "priority", &res->mn.priority);

  /* Get the match node properties */
  res->mn.props = parse_properties (mn, "properties");

  /* Get the endpoint table */
  e = wp_toml_table_get_table (table, "endpoint");
  if (!e)
    goto error;

  /* Get the name from the endpoint table */
  res->e.name = wp_toml_table_get_string (e, "name");

  /* Get the media class from the endpoint table */
  res->e.media_class = wp_toml_table_get_string (e, "media_class");

  /* Get the direction from the endpoint table */
  direction = wp_toml_table_get_string (e, "direction");
  if (!direction)
    goto error;
  res->e.direction = parse_endpoint_direction (direction);

  /* Get the priority from the endpoint table */
  res->e.priority = 0;
  wp_toml_table_get_uint32 (e, "priority", &res->e.priority);

  /* Get the endpoint properties */
  res->e.props = parse_properties (e, "properties");

  /* Get the endpoint type */
  res->e.type = wp_toml_table_get_string (e, "type");
  if (!res->e.type)
    goto error;

  /* Get the endpoint streams */
  res->e.streams = wp_toml_table_get_string (e, "streams");

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

  return db->mn.priority - da->mn.priority;
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
    g_warning ("Failed to parse configuration file '%s'", name);
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
  WpProxyNode *node = WP_PROXY_NODE (data);
  const struct WpParserEndpointData *d = NULL;
  g_autoptr (WpProperties) props = NULL;

  g_return_val_if_fail (node, NULL);
  props = wp_proxy_node_get_properties (node);

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
