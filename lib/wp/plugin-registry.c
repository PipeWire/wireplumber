/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "plugin-registry.h"
#include "plugin.h"

typedef struct {
  gsize block_size;
  GType gtype;
  const WpPluginMetadata *metadata;
  WpPlugin *instance;
} PluginData;

struct _WpPluginRegistry
{
  GObject parent;

  GList *plugins;
  GStringChunk *metadata_strings;
};

G_DEFINE_TYPE (WpPluginRegistry, wp_plugin_registry, G_TYPE_OBJECT);

static void
wp_plugin_registry_init (WpPluginRegistry * self)
{
  self->metadata_strings = g_string_chunk_new (200);
}

static void
wp_plugin_registry_dispose (GObject * object)
{
  WpPluginRegistry *self = WP_PLUGIN_REGISTRY (object);
  GList *list;
  PluginData *plugin_data;

  for (list = self->plugins; list != NULL; list = g_list_next (list)) {
    plugin_data = list->data;
    g_clear_object (&plugin_data->instance);
  }

  G_OBJECT_CLASS (wp_plugin_registry_parent_class)->dispose (object);
}

static void
plugin_data_free (PluginData *data)
{
  g_slice_free1 (data->block_size, data);
}

static void
wp_plugin_registry_finalize (GObject * object)
{
  WpPluginRegistry *self = WP_PLUGIN_REGISTRY (object);

  g_list_free_full (self->plugins, (GDestroyNotify) plugin_data_free);
  g_string_chunk_free (self->metadata_strings);

  G_OBJECT_CLASS (wp_plugin_registry_parent_class)->finalize (object);
}

static void
wp_plugin_registry_class_init (WpPluginRegistryClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  object_class->dispose = wp_plugin_registry_dispose;
  object_class->finalize = wp_plugin_registry_finalize;
}

/**
 * wp_plugin_registry_new: (constructor)
 *
 * Create a new registry.
 */
WpPluginRegistry *
wp_plugin_registry_new (void)
{
  return g_object_new (wp_plugin_registry_get_type (), NULL);
}

static gint
compare_ranks (const WpPluginMetadata * a, const WpPluginMetadata * b)
{
  return (gint) b->rank - (gint) a->rank;
}

/**
 * wp_plugin_registry_register_with_metadata: (skip)
 * @plugin_type: the #GType of the #WpPlugin subclass
 * @metadata: the metadata
 * @metadata_size: the sizeof (@metadata), to allow ABI-compatible future
 *   expansion of the structure
 *
 * Registers a plugin in the registry.
 * This method is used internally by WP_PLUGIN_REGISTER().
 * Avoid using it directly.
 */
void
wp_plugin_registry_register_with_metadata (WpPluginRegistry * self,
    GType plugin_type,
    const WpPluginMetadata * metadata,
    gsize metadata_size)
{
  PluginData *data;

  g_return_if_fail (WP_IS_PLUGIN_REGISTRY (self));
  g_return_if_fail (metadata_size == sizeof (WpPluginMetadata));
  g_return_if_fail (g_type_is_a (plugin_type, wp_plugin_get_type ()));
  g_return_if_fail (metadata->name != NULL);
  g_return_if_fail (metadata->description != NULL);
  g_return_if_fail (metadata->author != NULL);
  g_return_if_fail (metadata->license != NULL);
  g_return_if_fail (metadata->version != NULL);
  g_return_if_fail (metadata->origin != NULL);

  data = g_slice_alloc (sizeof (PluginData));
  data->block_size = sizeof (PluginData);
  data->gtype = plugin_type;
  data->metadata = metadata;
  data->instance = NULL;

  self->plugins = g_list_insert_sorted (self->plugins, data,
      (GCompareFunc) compare_ranks);
}

/**
 * wp_plugin_registry_register: (method)
 * @plugin_type: the #GType of the #WpPlugin subclass
 * @rank: the rank of the plugin
 * @name: the name of the plugin
 * @description: plugin description
 * @author: author <email@domain>, author2 <email@domain>
 * @license: a SPDX license ID or "Proprietary"
 * @version: the version of the plugin
 * @origin: URL or short reference of where this plugin came from
 *
 * Registers a plugin in the registry.
 * This method creates a dynamically allocated #WpPluginMetadata and is meant
 * to be used by bindings that have no way of representing #WpPluginMetadata.
 * In C/C++, you should use WP_PLUGIN_REGISTER()
 */
void
wp_plugin_registry_register (WpPluginRegistry * self,
    GType plugin_type,
    guint16 rank,
    const gchar *name,
    const gchar *description,
    const gchar *author,
    const gchar *license,
    const gchar *version,
    const gchar *origin)
{
  PluginData *data;
  WpPluginMetadata *metadata;

  g_return_if_fail (WP_IS_PLUGIN_REGISTRY (self));
  g_return_if_fail (g_type_is_a (plugin_type, wp_plugin_get_type ()));
  g_return_if_fail (name != NULL);
  g_return_if_fail (description != NULL);
  g_return_if_fail (author != NULL);
  g_return_if_fail (license != NULL);
  g_return_if_fail (version != NULL);
  g_return_if_fail (origin != NULL);

  data = g_slice_alloc (sizeof (PluginData) + sizeof (WpPluginMetadata));
  data->block_size = sizeof (PluginData) + sizeof (WpPluginMetadata);
  data->gtype = plugin_type;

  metadata = (WpPluginMetadata *) ((guint8 *) data) + sizeof (PluginData);
  metadata->rank = rank;
  metadata->name = g_string_chunk_insert (self->metadata_strings, name);
  metadata->description = g_string_chunk_insert (self->metadata_strings,
      description);
  metadata->author = g_string_chunk_insert (self->metadata_strings, author);
  metadata->license = g_string_chunk_insert (self->metadata_strings, license);
  metadata->version = g_string_chunk_insert (self->metadata_strings, version);
  metadata->origin = g_string_chunk_insert (self->metadata_strings, origin);

  data->metadata = metadata;
  data->instance = NULL;

  self->plugins = g_list_insert_sorted (self->plugins, data,
      (GCompareFunc) compare_ranks);
}

static inline void
make_plugin (WpPluginRegistry * self, PluginData * plugin_data)
{
  plugin_data->instance = g_object_new (plugin_data->gtype,
      "registry", self, "metadata", plugin_data->metadata, NULL);
}

/**
 * WpPluginFunc: (skip)
 */

/**
 * wp_plugin_registry_invoke_internal: (skip)
 * @self: the registry
 * @func: a vfunc invocation function of #WpPlugin
 * @data: data to pass to @func
 *
 * Used internally only.
 */
gboolean
wp_plugin_registry_invoke_internal (WpPluginRegistry * self, WpPluginFunc func,
    gpointer data)
{
  GList *list;
  PluginData *plugin_data;

  for (list = self->plugins; list != NULL; list = g_list_next (list)) {
    plugin_data = list->data;
    if (!plugin_data->instance)
      make_plugin (self, plugin_data);

    if (func (plugin_data->instance, data))
      return TRUE;
  }

  return FALSE;
}
