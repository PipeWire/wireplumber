/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author Raghavendra Rao <raghavendra.rao@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>
#include <pipewire/pipewire.h>

struct _WpMetadataPlugin
{
  WpPlugin parent;
  WpImplMetadata *metadata;
};

G_DECLARE_FINAL_TYPE (WpMetadataPlugin, wp_metadata_plugin,
                      WP, METADATA_PLUGIN, WpPlugin)
G_DEFINE_TYPE (WpMetadataPlugin, wp_metadata_plugin, WP_TYPE_PLUGIN)

static void
wp_metadata_plugin_init (WpMetadataPlugin * self)
{
}

static void
on_metadata_activated (GObject * obj, GAsyncResult * res, gpointer user_data)
{
  WpTransition * transition = WP_TRANSITION (user_data);
  WpMetadataPlugin * self = wp_transition_get_source_object (transition);
  g_autoptr (GError) error = NULL;

  if (!wp_object_activate_finish (WP_OBJECT (obj), res, &error)) {
    g_clear_object (&self->metadata);
    g_prefix_error (&error, "Failed to activate WpImplMetadata: ");
    wp_transition_return_error (transition, g_steal_pointer (&error));
    return;
  }

  wp_object_update_features (WP_OBJECT (self), WP_PLUGIN_FEATURE_ENABLED, 0);
}

static void
wp_metadata_plugin_enable (WpPlugin * plugin, WpTransition * transition)
{
  WpMetadataPlugin * self = WP_METADATA_PLUGIN (plugin);
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (plugin));
  g_return_if_fail (core);

  self->metadata = wp_impl_metadata_new (core);
  wp_object_activate (WP_OBJECT (self->metadata),
        WP_OBJECT_FEATURES_ALL, NULL, on_metadata_activated, transition);
}

static void
wp_metadata_plugin_disable (WpPlugin * plugin)
{
  WpMetadataPlugin * self = WP_METADATA_PLUGIN (plugin);

  g_clear_object (&self->metadata);
}

static void
wp_metadata_plugin_class_init (WpMetadataPluginClass * klass)
{
  WpPluginClass *plugin_class = (WpPluginClass *) klass;

  plugin_class->enable = wp_metadata_plugin_enable;
  plugin_class->disable = wp_metadata_plugin_disable;
}

WP_PLUGIN_EXPORT void
wireplumber__module_init (WpModule * module, WpCore * core, GVariant * args)
{
  wp_plugin_register (g_object_new (wp_metadata_plugin_get_type (),
          "name", "metadata",
          "core", core,
          NULL));
}
