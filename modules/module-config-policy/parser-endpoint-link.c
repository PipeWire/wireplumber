/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wptoml/wptoml.h>

#include <pipewire/pipewire.h>

#include "parser-endpoint-link.h"

static gboolean
wildcard_match (const char *pattern, const char *str)
{
  if(*pattern == '\0' && *str == '\0')
    return TRUE;
  if(*pattern == '?' || *pattern == *str)
    return wildcard_match (pattern+1, str + 1);
  if(*pattern == '*')
    return wildcard_match (pattern + 1, str) ||
        wildcard_match (pattern, str + 1);
  return FALSE;
}

gboolean
wp_parser_endpoint_link_matches_endpoint_data (WpBaseEndpoint *ep,
    const struct WpParserEndpointLinkEndpointData *data)
{
  g_autoptr (WpProperties) props = NULL;

  g_return_val_if_fail (ep, FALSE);
  g_return_val_if_fail (data, FALSE);

  props = wp_base_endpoint_get_properties (ep);
  g_return_val_if_fail (props, FALSE);

  /* Name */
  if (data->name &&
        !wildcard_match (data->name, wp_base_endpoint_get_name (ep)))
    return FALSE;

  /* Media Class */
  if (data->media_class &&
      g_strcmp0 (wp_base_endpoint_get_media_class (ep), data->media_class) != 0)
    return FALSE;

  /* Direction */
  if (wp_base_endpoint_get_direction (ep) != data->direction)
    return FALSE;

  /* Properties */
  if (!wp_properties_matches (props, data->props))
    return FALSE;

  return TRUE;
}

struct _WpParserEndpointLink
{
  GObject parent;

  GPtrArray *datas;
};

static void wp_parser_endpoint_link_config_parser_init (gpointer iface,
    gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (WpParserEndpointLink, wp_parser_endpoint_link,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (WP_TYPE_CONFIG_PARSER,
                         wp_parser_endpoint_link_config_parser_init))

static void
wp_parser_endpoint_link_data_destroy (gpointer p)
{
  struct WpParserEndpointLinkData *data = p;

  /* Free the strings */
  g_clear_pointer (&data->me.endpoint_data.name, g_free);
  g_clear_pointer (&data->me.endpoint_data.media_class, g_free);
  g_clear_pointer (&data->me.endpoint_data.props, wp_properties_unref);
  g_clear_pointer (&data->te.endpoint_data.name, g_free);
  g_clear_pointer (&data->te.endpoint_data.media_class, g_free);
  g_clear_pointer (&data->te.endpoint_data.props, wp_properties_unref);
  g_clear_pointer (&data->te.stream, g_free);

  g_slice_free (struct WpParserEndpointLinkData, data);
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
  if (g_strcmp0 (direction, "input") == 0)
    return PW_DIRECTION_INPUT;
  else if (g_strcmp0 (direction, "output") == 0)
    return PW_DIRECTION_OUTPUT;

  g_return_val_if_reached (PW_DIRECTION_INPUT);
}

static struct WpParserEndpointLinkData *
wp_parser_endpoint_link_data_new (const gchar *location)
{
  g_autoptr (WpTomlFile) file = NULL;
  g_autoptr (WpTomlTable) table = NULL, me = NULL, te = NULL, el = NULL;
  struct WpParserEndpointLinkData *res = NULL;
  g_autofree char *direction = NULL;

  /* File format:
   * ------------
   * [match-endpoint]
   * priority (uint32)
   * name (string)
   * media_class (string)
   * direction (string)
   * properties (WpProperties)
   *
   * [target-endpoint]
   * name (string)
   * media_class (string)
   * direction (string)
   * properties (WpProperties)
   * stream (string)
   *
   * [endpoint-link]
   * keep (bool)
   */

  /* Get the TOML file */
  file = wp_toml_file_new (location);
  if (!file)
    return NULL;

  /* Get the file table */
  table = wp_toml_file_get_table (file);
  if (!table)
    return NULL;

  /* Create the link data */
  res = g_slice_new0(struct WpParserEndpointLinkData);

  /* Get the match-node table */
  me = wp_toml_table_get_table (table, "match-endpoint");
  if (!me)
    goto error;

  /* Get the priority from the endpoint table */
  res->me.priority = 0;
  wp_toml_table_get_uint32 (me, "priority", &res->me.priority);

  /* Get the name from the match endpoint table (Optional) */
  res->me.endpoint_data.name = wp_toml_table_get_string (me, "name");

  /* Get the media class from the match endpoint table (Optional) */
  res->me.endpoint_data.media_class =
      wp_toml_table_get_string (me, "media_class");

  /* Get the direction from the match endpoint table */
  direction = wp_toml_table_get_string (me, "direction");
  if (!direction)
    goto error;
  res->me.endpoint_data.direction = parse_endpoint_direction (direction);

  /* Get the match endpoint properties (Optional) */
  res->me.endpoint_data.props = parse_properties (me, "properties");

  /* Get the target-endpoint table */
  te = wp_toml_table_get_table (table, "target-endpoint");
  if (!te)
    goto error;

  /* Get the name from the match endpoint table (Optional) */
  res->te.endpoint_data.name = wp_toml_table_get_string (te, "name");

  /* Get the media class from the match endpoint table (Optional) */
  res->te.endpoint_data.media_class =
      wp_toml_table_get_string (te, "media_class");

  /* Set the direction to the match endpoint's reverse one */
  res->te.endpoint_data.direction =
      pw_direction_reverse (res->me.endpoint_data.direction);

  /* Get the target endpoint properties (Optional) */
  res->te.endpoint_data.props = parse_properties (te, "properties");

  /* Get the target endpoint stream */
  res->te.stream = wp_toml_table_get_string (te, "stream");

  /* Get the target-endpoint table */
  el = wp_toml_table_get_table (table, "endpoint-link");
  if (!el)
    goto error;

  /* Get the endpoint link keep */
  res->el.keep = FALSE;
  wp_toml_table_get_boolean (el, "keep", &res->el.keep);

  return res;

error:
  g_clear_pointer (&res, wp_parser_endpoint_link_data_destroy);
  return NULL;
}

static gint
compare_datas_func (gconstpointer a, gconstpointer b)
{
  struct WpParserEndpointLinkData *da =
      *(struct WpParserEndpointLinkData *const *)a;
  struct WpParserEndpointLinkData *db =
      *(struct WpParserEndpointLinkData *const *)b;

  return db->me.priority - da->me.priority;
}

static gboolean
wp_parser_endpoint_link_add_file (WpConfigParser *parser,
    const gchar *name)
{
  WpParserEndpointLink *self = WP_PARSER_ENDPOINT_LINK (parser);
  struct WpParserEndpointLinkData *data;

  /* Parse the file */
  data = wp_parser_endpoint_link_data_new (name);
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
wp_parser_endpoint_link_get_matched_data (WpConfigParser *parser, gpointer data)
{
  WpParserEndpointLink *self = WP_PARSER_ENDPOINT_LINK (parser);
  WpBaseEndpoint *ep = WP_BASE_ENDPOINT (data);
  const struct WpParserEndpointLinkData *d = NULL;

  /* Find the first data that matches endpoint */
  for (guint i = 0; i < self->datas->len; i++) {
    d = g_ptr_array_index(self->datas, i);
    if (wp_parser_endpoint_link_matches_endpoint_data (ep,
        &d->me.endpoint_data))
      return d;
  }

  return NULL;
}

static void
wp_parser_endpoint_link_reset (WpConfigParser *parser)
{
  WpParserEndpointLink *self = WP_PARSER_ENDPOINT_LINK (parser);

  g_ptr_array_set_size (self->datas, 0);
}

static void
wp_parser_endpoint_link_config_parser_init (gpointer iface,
    gpointer iface_data)
{
  WpConfigParserInterface *cp_iface = iface;

  cp_iface->add_file = wp_parser_endpoint_link_add_file;
  cp_iface->get_matched_data = wp_parser_endpoint_link_get_matched_data;
  cp_iface->reset = wp_parser_endpoint_link_reset;
}

static void
wp_parser_endpoint_link_finalize (GObject * object)
{
  WpParserEndpointLink *self = WP_PARSER_ENDPOINT_LINK (object);

  g_clear_pointer (&self->datas, g_ptr_array_unref);

  G_OBJECT_CLASS (wp_parser_endpoint_link_parent_class)->finalize (object);
}

static void
wp_parser_endpoint_link_init (WpParserEndpointLink * self)
{
  self->datas = g_ptr_array_new_with_free_func(
      wp_parser_endpoint_link_data_destroy);
}

static void
wp_parser_endpoint_link_class_init (WpParserEndpointLinkClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;

  object_class->finalize = wp_parser_endpoint_link_finalize;
}
