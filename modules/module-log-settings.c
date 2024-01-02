/* WirePlumber
 *
 * Copyright © 2023 Collabora Ltd.
 * Copyright © 2023 Pauli Virtanen <pav@iki.fi>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>
#include <errno.h>
#include <pipewire/pipewire.h>
#include <pipewire/keys.h>

WP_DEFINE_LOCAL_LOG_TOPIC ("m-log-settings")

struct _WpLogSettingsPlugin
{
  WpPlugin parent;
  WpObjectManager *metadata_om;
};

G_DECLARE_FINAL_TYPE (WpLogSettingsPlugin, wp_log_settings_plugin,
                      WP, LOG_SETTINGS_PLUGIN, WpPlugin)
G_DEFINE_TYPE (WpLogSettingsPlugin, wp_log_settings_plugin, WP_TYPE_PLUGIN)

static void
wp_log_settings_plugin_init (WpLogSettingsPlugin * self)
{
}

static void
on_metadata_changed (WpMetadata *m, guint32 subject,
    const gchar *key, const gchar *type, const gchar *value, gpointer d)
{
  WpLogSettingsPlugin * self = WP_LOG_SETTINGS_PLUGIN (d);
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));
  g_return_if_fail (core);

  if (subject != wp_core_get_own_bound_id (core))
    return;

  if (spa_streq(key, "log.level"))
    wp_log_set_level (value ? value : "2");
}

static void
on_metadata_added (WpObjectManager *om, WpMetadata *metadata, gpointer d)
{
  WpLogSettingsPlugin * self = WP_LOG_SETTINGS_PLUGIN (d);
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));
  g_return_if_fail (core);

  /* Handle the changed signal */
  g_signal_connect_object (metadata, "changed",
      G_CALLBACK (on_metadata_changed), self, 0);
}

static void
wp_log_settings_plugin_enable (WpPlugin * plugin, WpTransition * transition)
{
  WpLogSettingsPlugin * self = WP_LOG_SETTINGS_PLUGIN (plugin);
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (plugin));
  g_return_if_fail (core);

  /* Create the metadata object manager */
  self->metadata_om = wp_object_manager_new ();
  wp_object_manager_add_interest (self->metadata_om, WP_TYPE_METADATA,
      WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY, "metadata.name", "=s", "settings",
      NULL);
  wp_object_manager_request_object_features (self->metadata_om,
      WP_TYPE_METADATA, WP_OBJECT_FEATURES_ALL);
  g_signal_connect_object (self->metadata_om, "object-added",
      G_CALLBACK (on_metadata_added), self, 0);
  wp_core_install_object_manager (core, self->metadata_om);

  wp_object_update_features (WP_OBJECT (self), WP_PLUGIN_FEATURE_ENABLED, 0);
}

static void
wp_log_settings_plugin_disable (WpPlugin * plugin)
{
  WpLogSettingsPlugin * self = WP_LOG_SETTINGS_PLUGIN (plugin);

  g_clear_object (&self->metadata_om);
}

static void
wp_log_settings_plugin_class_init (WpLogSettingsPluginClass * klass)
{
  WpPluginClass *plugin_class = (WpPluginClass *) klass;

  plugin_class->enable = wp_log_settings_plugin_enable;
  plugin_class->disable = wp_log_settings_plugin_disable;
}

WP_PLUGIN_EXPORT GObject *
wireplumber__module_init (WpCore * core, GVariant * args, GError ** error)
{
  return G_OBJECT (g_object_new (wp_log_settings_plugin_get_type (),
          "name", "log-settings",
          "core", core,
          NULL));
}
