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
wp_metadata_plugin_activate (WpPlugin * plugin)
{
  WpMetadataPlugin * self = WP_METADATA_PLUGIN (plugin);
  g_autoptr (WpCore) core = wp_plugin_get_core (plugin);
  g_return_if_fail (core);

  self->metadata = wp_impl_metadata_new (core);
  wp_proxy_augment (WP_PROXY(self->metadata),
        WP_PROXY_FEATURES_STANDARD, NULL,
        NULL, self);
}

static void
wp_metadata_plugin_deactivate (WpPlugin * plugin)
{
  WpMetadataPlugin * self = WP_METADATA_PLUGIN (plugin);

  g_clear_object (&self->metadata);
}

static void
wp_metadata_plugin_class_init (WpMetadataPluginClass * klass)
{
  WpPluginClass *plugin_class = (WpPluginClass *) klass;

  plugin_class->activate = wp_metadata_plugin_activate;
  plugin_class->deactivate = wp_metadata_plugin_deactivate;
}

WP_PLUGIN_EXPORT void
wireplumber__module_init (WpModule * module, WpCore * core, GVariant * args)
{
  wp_plugin_register (g_object_new (wp_metadata_plugin_get_type (),
          "module", module,
          NULL));
}
