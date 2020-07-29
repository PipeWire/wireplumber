/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Raghavendra Rao <raghavendra.rao@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>
#include <pipewire/pipewire.h>

struct _WpMetadataSettings
{
  WpPlugin parent;
  WpImplMetadata *metadata;
};

G_DECLARE_FINAL_TYPE (WpMetadataSettings, wp_metadata_settings,
                      WP, METADATA_SETTINGS, WpPlugin)
G_DEFINE_TYPE (WpMetadataSettings, wp_metadata_settings, WP_TYPE_PLUGIN)

static void
wp_metadata_settings_init (WpMetadataSettings * self)
{
}

static void
wp_metadata_settings_activate (WpPlugin * plugin)
{
  WpMetadataSettings * self = WP_METADATA_SETTINGS (plugin);
  g_autoptr (WpCore) core = wp_plugin_get_core (plugin);

  g_return_if_fail (core);

  self->metadata = wp_impl_metadata_new(core);

  wp_proxy_augment (WP_PROXY(self->metadata),
        WP_METADATA_FEATURES_STANDARD, NULL,
        NULL, self);
}

static void
wp_metadata_settings_deactivate (WpPlugin * plugin)
{
  WpMetadataSettings * self = WP_METADATA_SETTINGS (plugin);

  g_clear_object (&self->metadata);
}

static void
wp_metadata_settings_class_init (WpMetadataSettingsClass * klass)
{
  WpPluginClass *plugin_class = (WpPluginClass *) klass;

  plugin_class->activate = wp_metadata_settings_activate;
  plugin_class->deactivate = wp_metadata_settings_deactivate;
}

WP_PLUGIN_EXPORT void
wireplumber__module_init (WpModule * module, WpCore * core, GVariant * args)
{
  wp_plugin_register (g_object_new (wp_metadata_settings_get_type (),
          "module", module,
          NULL));
}