/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wptoml/wptoml.h>

#include <pipewire/pipewire.h>

#include "parser-streams.h"

struct _WpParserStreams
{
  GObject parent;

  GPtrArray *datas;
};

const struct WpParserStreamsStreamData *
wp_parser_streams_find_stream (const struct WpParserStreamsData *data,
    const char *name)
{
  for (guint i = 0; i < data->n_streams; i++) {
    const struct WpParserStreamsStreamData *s = data->streams + i;
    if (g_strcmp0 (s->name, name) == 0)
      return s;
  }
  return NULL;
}

static void wp_parser_streams_config_parser_init (gpointer iface,
    gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (WpParserStreams, wp_parser_streams,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (WP_TYPE_CONFIG_PARSER,
                         wp_parser_streams_config_parser_init))

static void
wp_parser_streams_data_destroy (gpointer p)
{
  struct WpParserStreamsData *data = p;

  /* Clear the location */
  g_clear_pointer (&data->location, g_free);

  /* Clear the streams */
  for (guint i = 0; i < data->n_streams; i++) {
    struct WpParserStreamsStreamData *s = data->streams + i;
    g_clear_pointer (&s->name, g_free);
  }
  data->n_streams = 0;

  g_slice_free (struct WpParserStreamsData, data);
}

static void
streams_for_each (const WpTomlTable *table, gpointer user_data)
{
  struct WpParserStreamsData *data = user_data;
  struct WpParserStreamsStreamData *stream = NULL;
  g_return_if_fail (data);

  /* Make sure we don't parse more MAX_STREAMS streams */
  if (data->n_streams >= MAX_STREAMS)
    return;

  /* Skip unparsed tables */
  if (!table)
    return;

  /* Parse the mandatory name */
  stream = data->streams + data->n_streams;
  stream->name = wp_toml_table_get_string (table, "name");
  if (!stream->name)
    return;

  /* Parse the optional priority */
  stream->priority = 0;
  wp_toml_table_get_uint32 (table, "priority", &stream->priority);

  /* Increment the number of streams */
  data->n_streams++;
}


static struct WpParserStreamsData *
wp_parser_streams_data_new (const gchar *location)
{
  g_autoptr (WpTomlFile) file = NULL;
  g_autoptr (WpTomlTable) table = NULL;
  g_autoptr (WpTomlTableArray) streams = NULL;
  struct WpParserStreamsData *res = NULL;

  /* File format:
   * ------------
   * [[streams]]
   * name (string)
   * priority (uint32)
   */

  /* Get the TOML file */
  file = wp_toml_file_new (location);
  if (!file)
    return NULL;

  /* Get the file table */
  table = wp_toml_file_get_table (file);
  if (!table)
    return NULL;

  /* Create the streams data */
  res = g_slice_new0 (struct WpParserStreamsData);

  /* Set the location */
  res->location = g_strdup (location);

  /* Parse the streams */
  res->n_streams = 0;
  streams = wp_toml_table_get_array_table (table, "streams");
  if (streams)
    wp_toml_table_array_for_each (streams, streams_for_each, res);

  return res;
}

static gboolean
wp_parser_streams_add_file (WpConfigParser *parser,
    const gchar *name)
{
  WpParserStreams *self = WP_PARSER_STREAMS (parser);
  struct WpParserStreamsData *data;

  /* Parse the file */
  data = wp_parser_streams_data_new (name);
  if (!data) {
    g_warning ("Failed to parse configuration file '%s'", name);
    return FALSE;
  }

  /* Add the data to the array */
  g_ptr_array_add(self->datas, data);

  return TRUE;
}

static gconstpointer
wp_parser_streams_get_matched_data (WpConfigParser *parser, gpointer data)
{
  WpParserStreams *self = WP_PARSER_STREAMS (parser);
  const char *location = data;
  const struct WpParserStreamsData *d = NULL;

  /* Find the first data that matches location */
  for (guint i = 0; i < self->datas->len; i++) {
    d = g_ptr_array_index(self->datas, i);
    if (g_strrstr (d->location, location))
      return d;
  }

  return NULL;
}

static void
wp_parser_streams_reset (WpConfigParser *parser)
{
  WpParserStreams *self = WP_PARSER_STREAMS (parser);

  g_ptr_array_set_size (self->datas, 0);
}

static void
wp_parser_streams_config_parser_init (gpointer iface,
    gpointer iface_data)
{
  WpConfigParserInterface *cp_iface = iface;

  cp_iface->add_file = wp_parser_streams_add_file;
  cp_iface->get_matched_data = wp_parser_streams_get_matched_data;
  cp_iface->reset = wp_parser_streams_reset;
}

static void
wp_parser_streams_finalize (GObject * object)
{
  WpParserStreams *self = WP_PARSER_STREAMS (object);

  g_clear_pointer (&self->datas, g_ptr_array_unref);

  G_OBJECT_CLASS (wp_parser_streams_parent_class)->finalize (object);
}

static void
wp_parser_streams_init (WpParserStreams * self)
{
  self->datas = g_ptr_array_new_with_free_func (wp_parser_streams_data_destroy);
}

static void
wp_parser_streams_class_init (WpParserStreamsClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;

  object_class->finalize = wp_parser_streams_finalize;
}
