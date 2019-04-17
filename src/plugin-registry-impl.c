/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "plugin-registry-impl.h"
#include <wp/plugin.h>

typedef struct {
  gsize block_size;
  GType gtype;
  const WpPluginMetadata *metadata;
  WpPlugin *instance;
} PluginData;

struct _WpPluginRegistryImpl
{
  WpInterfaceImpl parent;

  GList *plugins;
  GStringChunk *metadata_strings;
};

static void wp_plugin_registry_impl_iface_init (WpPluginRegistryInterface * iface);

G_DEFINE_TYPE_WITH_CODE (WpPluginRegistryImpl, wp_plugin_registry_impl, WP_TYPE_INTERFACE_IMPL,
    G_IMPLEMENT_INTERFACE (WP_TYPE_PLUGIN_REGISTRY, wp_plugin_registry_impl_iface_init);)

static void
wp_plugin_registry_impl_init (WpPluginRegistryImpl * self)
{
  self->metadata_strings = g_string_chunk_new (200);
}

static void
plugin_data_free (PluginData *data)
{
  g_slice_free1 (data->block_size, data);
}

static void
wp_plugin_registry_impl_finalize (GObject * object)
{
  WpPluginRegistryImpl *self = WP_PLUGIN_REGISTRY_IMPL (object);

  g_list_free_full (self->plugins, (GDestroyNotify) plugin_data_free);
  g_string_chunk_free (self->metadata_strings);

  G_OBJECT_CLASS (wp_plugin_registry_impl_parent_class)->finalize (object);
}

static void
wp_plugin_registry_impl_class_init (WpPluginRegistryImplClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  object_class->finalize = wp_plugin_registry_impl_finalize;
}

static gint
compare_ranks (const WpPluginMetadata * a, const WpPluginMetadata * b)
{
  return (gint) b->rank - (gint) a->rank;
}

static void
wp_plugin_registry_impl_register_plugin (WpPluginRegistry * r,
    GType plugin_type,
    const WpPluginMetadata * metadata,
    gsize metadata_size,
    gboolean static_data)
{
  WpPluginRegistryImpl *self = WP_PLUGIN_REGISTRY_IMPL (r);
  PluginData *data;

  g_return_if_fail (metadata_size == sizeof (WpPluginMetadata));

  if (static_data) {
    data = g_slice_alloc (sizeof (PluginData));
    data->block_size = sizeof (PluginData);
  } else {
    data = g_slice_alloc (sizeof (PluginData) + sizeof (WpPluginMetadata));
    data->block_size = sizeof (PluginData) + sizeof (WpPluginMetadata);
  }

  data->gtype = plugin_type;
  data->instance = NULL;

  if (!static_data) {
    WpPluginMetadata *m;
    m = (WpPluginMetadata *) ((guint8 *) data) + sizeof (PluginData);
    m->rank = metadata->rank;
    m->name = g_string_chunk_insert (self->metadata_strings, metadata->name);
    m->description = g_string_chunk_insert (self->metadata_strings,
        metadata->description);
    m->author = g_string_chunk_insert (self->metadata_strings,
        metadata->author);
    m->license = g_string_chunk_insert (self->metadata_strings,
        metadata->license);
    m->version = g_string_chunk_insert (self->metadata_strings,
        metadata->version);
    m->origin = g_string_chunk_insert (self->metadata_strings,
        metadata->origin);
    data->metadata = m;
  } else {
    data->metadata = metadata;
  }

  self->plugins = g_list_insert_sorted (self->plugins, data,
      (GCompareFunc) compare_ranks);
}

static void
wp_plugin_registry_impl_iface_init (WpPluginRegistryInterface * iface)
{
  iface->register_plugin = wp_plugin_registry_impl_register_plugin;
}

WpPluginRegistryImpl *
wp_plugin_registry_impl_new (void)
{
  return g_object_new (wp_plugin_registry_impl_get_type (), NULL);
}

void
wp_plugin_registry_impl_unload (WpPluginRegistryImpl * self)
{
  GList *list;
  PluginData *plugin_data;

  for (list = self->plugins; list != NULL; list = g_list_next (list)) {
    plugin_data = list->data;
    g_clear_object (&plugin_data->instance);
  }
}

static inline void
make_plugin (WpPluginRegistryImpl * self, PluginData * plugin_data)
{
  plugin_data->instance = g_object_new (plugin_data->gtype,
      "registry", self, "metadata", plugin_data->metadata, NULL);
}

gboolean
wp_plugin_registry_impl_invoke_internal (WpPluginRegistryImpl * self,
    WpPluginFunc func, gpointer data)
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
