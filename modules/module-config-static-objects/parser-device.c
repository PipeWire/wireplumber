/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wptoml/wptoml.h>

#include <pipewire/pipewire.h>

#include "parser-device.h"

struct _WpParserDevice
{
  GObject parent;

  GPtrArray *datas;
};

static void wp_parser_device_config_parser_init (gpointer iface,
    gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (WpParserDevice, wp_parser_device,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (WP_TYPE_CONFIG_PARSER,
                           wp_parser_device_config_parser_init))

static void
wp_parser_device_data_destroy (gpointer p)
{
  struct WpParserDeviceData *data = p;

  /* Free the strings */
  g_clear_pointer (&data->filename, g_free);
  g_clear_pointer (&data->factory, g_free);
  g_clear_pointer (&data->props, wp_properties_unref);

  g_slice_free (struct WpParserDeviceData, data);
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

static struct WpParserDeviceData *
wp_parser_device_data_new (const gchar *location)
{
  g_autoptr (WpTomlFile) file = NULL;
  g_autoptr (WpTomlTable) table = NULL;
  struct WpParserDeviceData *res = NULL;

  /* File format:
   * ------------
   * factory (string)
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

  /* Create the endpoint data */
  res = g_slice_new0 (struct WpParserDeviceData);

  /* Set the file name */
  res->filename = g_path_get_basename (location);

  /* Get factory */
  res->factory = wp_toml_table_get_string (table, "factory");
  if (!res->factory)
    goto error;

  /* Get properties */
  res->props = parse_properties (table, "properties");

  return res;

error:
  g_clear_pointer (&res, wp_parser_device_data_destroy);
  return NULL;
}

static gint
compare_datas_func (gconstpointer a, gconstpointer b)
{
  struct WpParserDeviceData *da = *(struct WpParserDeviceData *const *)a;
  struct WpParserDeviceData *db = *(struct WpParserDeviceData *const *)b;

  return g_strcmp0 (db->filename, da->filename);
}

static gboolean
wp_parser_device_add_file (WpConfigParser *parser, const gchar *name)
{
  WpParserDevice *self = WP_PARSER_DEVICE (parser);
  struct WpParserDeviceData *data;

  /* Parse the file */
  data = wp_parser_device_data_new (name);
  if (!data) {
    wp_warning_object (parser, "failed to parse file '%s'", name);
    return FALSE;
  }

  /* Add the data to the array */
  g_ptr_array_add (self->datas, data);

  /* Sort the array by priority */
  g_ptr_array_sort (self->datas, compare_datas_func);

  return TRUE;
}

static gconstpointer
wp_parser_device_get_matched_data (WpConfigParser *parser, gpointer data)
{
  WpParserDevice *self = WP_PARSER_DEVICE (parser);
  WpProperties *props = data;
  const struct WpParserDeviceData *d = NULL;

  g_return_val_if_fail (props, NULL);

  /* Find the first data that matches device properties */
  for (guint i = 0; i < self->datas->len; i++) {
    d = g_ptr_array_index (self->datas, i);
    if (d->props && wp_properties_matches (props, d->props))
      return d;
  }

  return NULL;
}

static void
wp_parser_device_reset (WpConfigParser *parser)
{
  WpParserDevice *self = WP_PARSER_DEVICE (parser);

  g_ptr_array_set_size (self->datas, 0);
}

static void
wp_parser_device_config_parser_init (gpointer iface, gpointer iface_data)
{
  WpConfigParserInterface *cp_iface = iface;

  cp_iface->add_file = wp_parser_device_add_file;
  cp_iface->get_matched_data = wp_parser_device_get_matched_data;
  cp_iface->reset = wp_parser_device_reset;
}

static void
wp_parser_device_finalize (GObject * object)
{
  WpParserDevice *self = WP_PARSER_DEVICE (object);

  g_clear_pointer (&self->datas, g_ptr_array_unref);

  G_OBJECT_CLASS (wp_parser_device_parent_class)->finalize (object);
}

static void
wp_parser_device_init (WpParserDevice * self)
{
  self->datas = g_ptr_array_new_with_free_func (wp_parser_device_data_destroy);
}

static void
wp_parser_device_class_init (WpParserDeviceClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;

  object_class->finalize = wp_parser_device_finalize;
}

void
wp_parser_device_foreach (WpParserDevice *self, WpParserDeviceForeachFunction f,
    gpointer data)
{
  const struct WpParserDeviceData *d;

  for (guint i = 0; i < self->datas->len; i++) {
    d = g_ptr_array_index(self->datas, i);
    if (!f (d, data))
      break;
  }
}
