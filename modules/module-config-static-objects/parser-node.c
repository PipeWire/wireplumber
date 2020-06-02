/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wptoml/wptoml.h>

#include <pipewire/pipewire.h>

#include "parser-node.h"

struct _WpParserNode
{
  GObject parent;

  GPtrArray *datas;
};

static void wp_parser_node_config_parser_init (gpointer iface,
    gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (WpParserNode, wp_parser_node,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (WP_TYPE_CONFIG_PARSER,
                           wp_parser_node_config_parser_init))

static void
wp_parser_node_data_destroy (gpointer p)
{
  struct WpParserNodeData *data = p;

  /* Free the strings */
  g_clear_pointer (&data->md.props, wp_properties_unref);
  g_clear_pointer (&data->n.factory, g_free);
  g_clear_pointer (&data->n.props, wp_properties_unref);

  g_slice_free (struct WpParserNodeData, data);
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

static struct WpParserNodeData *
wp_parser_node_data_new (const gchar *location)
{
  g_autoptr (WpTomlFile) file = NULL;
  g_autoptr (WpTomlTable) table = NULL, md = NULL, n = NULL;
  struct WpParserNodeData *res = NULL;

  /* File format:
   * ------------
   * [match-device]
   * priority (uint32)
   * properties (WpProperties)
   *
   * [node]
   * factory (string)
   * local (boolean)
   * properties (WpProperties)
   */

  /* Get the TOML file */
  file = wp_toml_file_new (location);
  if (!file)
    return NULL;

  /* Get the file table */
  table = wp_toml_file_get_table (file);
  if (!table)
    return NULL;

  /* Create the node data */
  res = g_slice_new0(struct WpParserNodeData);

  /* Get the match-device table */
  res->has_md = FALSE;
  md = wp_toml_table_get_table (table, "match-device");
  if (md) {
    res->has_md = TRUE;

    /* Get the priority from the match-device table */
    res->md.priority = 0;
    wp_toml_table_get_uint32 (md, "priority", &res->md.priority);

    /* Get the match device properties */
    res->md.props = parse_properties (md, "properties");
  }

  /* Get the node table */
  n = wp_toml_table_get_table (table, "node");
  if (!n)
    goto error;

  /* Get factory from the node table */
  res->n.factory = wp_toml_table_get_string (n, "factory");

  /* Get local from the node table */
  res->n.local = FALSE;
  wp_toml_table_get_boolean (n, "local", &res->n.local);

  /* Get the node properties */
  res->n.props = parse_properties (n, "properties");

  return res;

error:
  g_clear_pointer (&res, wp_parser_node_data_destroy);
  return NULL;
}

static gint
compare_datas_func (gconstpointer a, gconstpointer b)
{
  struct WpParserNodeData *da = *(struct WpParserNodeData *const *)a;
  struct WpParserNodeData *db = *(struct WpParserNodeData *const *)b;

  return db->md.priority - da->md.priority;
}

static gboolean
wp_parser_node_add_file (WpConfigParser *parser,
    const gchar *name)
{
  WpParserNode *self = WP_PARSER_NODE (parser);
  struct WpParserNodeData *data;

  /* Parse the file */
  data = wp_parser_node_data_new (name);
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
wp_parser_node_get_matched_data (WpConfigParser *parser, gpointer data)
{
  WpParserNode *self = WP_PARSER_NODE (parser);
  WpProperties *props = data;
  const struct WpParserNodeData *d = NULL;

  g_return_val_if_fail (props, NULL);

  /* Find the first data that matches device properties */
  for (guint i = 0; i < self->datas->len; i++) {
    d = g_ptr_array_index(self->datas, i);
    if (d->has_md && wp_properties_matches (props, d->md.props))
      return d;
  }

  return NULL;
}

static void
wp_parser_node_reset (WpConfigParser *parser)
{
  WpParserNode *self = WP_PARSER_NODE (parser);

  g_ptr_array_set_size (self->datas, 0);
}

static void
wp_parser_node_config_parser_init (gpointer iface, gpointer iface_data)
{
  WpConfigParserInterface *cp_iface = iface;

  cp_iface->add_file = wp_parser_node_add_file;
  cp_iface->get_matched_data = wp_parser_node_get_matched_data;
  cp_iface->reset = wp_parser_node_reset;
}

static void
wp_parser_node_finalize (GObject * object)
{
  WpParserNode *self = WP_PARSER_NODE (object);

  g_clear_pointer (&self->datas, g_ptr_array_unref);

  G_OBJECT_CLASS (wp_parser_node_parent_class)->finalize (object);
}

static void
wp_parser_node_init (WpParserNode * self)
{
  self->datas = g_ptr_array_new_with_free_func (wp_parser_node_data_destroy);
}

static void
wp_parser_node_class_init (WpParserNodeClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;

  object_class->finalize = wp_parser_node_finalize;
}

void
wp_parser_node_foreach (WpParserNode *self, WpParserNodeForeachFunction f,
    gpointer data)
{
  const struct WpParserNodeData *d;

  for (guint i = 0; i < self->datas->len; i++) {
    d = g_ptr_array_index(self->datas, i);
    if (!f (d, data))
      break;
  }
}
