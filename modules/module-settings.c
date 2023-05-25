/* WirePlumber
 *
 * Copyright © 2022 Collabora Ltd.
 *    @author Ashok Sidipotu <ashok.sidipotu@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>
#include <pipewire/pipewire.h>
#include <spa/utils/json.h>
#include <spa/utils/defs.h>

WP_DEFINE_LOCAL_LOG_TOPIC ("m-settings")

/*
 * This module parses the "wireplumber.settings" section from the .conf file.
 *
 * Creates "sm-settings"(default) metadata and pushes the settings to it.
 * Looks out for changes done in the metadata via the pw-metadata interface.
 *
 * If persistent settings is enabled stores the settings in a state file
 * and retains the settings from there on subsequent reboots ignoring the
 * contents of .conf file.
 */

struct _WpSettingsPlugin
{
  WpPlugin parent;

  /* Props */
  gchar *metadata_name;

  WpImplMetadata *impl_metadata;
  WpState *state;
  WpProperties *settings;
  GSource *timeout_source;
};

enum {
  PROP_0,
  PROP_METADATA_NAME,
  PROP_PROPERTIES,
};

G_DECLARE_FINAL_TYPE (WpSettingsPlugin, wp_settings_plugin,
                      WP, SETTINGS_PLUGIN, WpPlugin)
G_DEFINE_TYPE (WpSettingsPlugin, wp_settings_plugin, WP_TYPE_PLUGIN)

#define NAME "sm-settings"
#define PERSISTENT_SETTING "persistent.settings"
#define SAVE_INTERVAL_MS 1000

static void
wp_settings_plugin_init (WpSettingsPlugin * self)
{
}

static gboolean
timeout_save_state_callback (WpSettingsPlugin *self)
{
  g_autoptr (GError) error = NULL;

  if (!wp_state_save (self->state, self->settings, &error))
    wp_warning_object (self, "%s", error->message);

  return G_SOURCE_REMOVE;
}

static void
timeout_save_settings (WpSettingsPlugin *self, guint ms)
{
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));
  g_return_if_fail (core);

  /* Clear the current timeout callback */
  if (self->timeout_source)
    g_source_destroy (self->timeout_source);
  g_clear_pointer (&self->timeout_source, g_source_unref);

  /* Add the timeout callback */
  wp_core_timeout_add_closure (core, &self->timeout_source, ms,
      g_cclosure_new_object (G_CALLBACK (timeout_save_state_callback),
      G_OBJECT (self)));
}

static void
on_metadata_changed (WpMetadata *m, guint32 subject,
   const gchar *setting, const gchar *type, const gchar *new_value, gpointer d)
{
  WpSettingsPlugin *self = WP_SETTINGS_PLUGIN(d);
  const gchar *old_value = wp_properties_get (self->settings, setting);

  if (!old_value) {
    wp_info_object (self, "new setting defined \"%s\" = \"%s\"",
        setting, new_value);
  } else {
    wp_info_object (self, "setting \"%s\" new_value changed from \"%s\" ->"
        " \"%s\"", setting, old_value, new_value);
  }

  wp_properties_set (self->settings, setting, new_value);

  /* update the state */
  timeout_save_settings (self, SAVE_INTERVAL_MS);
}

WpProperties *
load_configuration_settings (WpSettingsPlugin *self)
{
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));
  g_autoptr (WpConf) conf = NULL;
  g_autoptr (WpSpaJson) json = NULL;
  g_autoptr (WpProperties) res = wp_properties_new_empty ();

  g_return_val_if_fail (core, NULL);
  conf = wp_conf_get_instance (core);
  g_return_val_if_fail (conf, NULL);

  json = wp_conf_get_section (conf, "wireplumber.settings", NULL);
  if (!json)
    return g_steal_pointer (&res);

  if (!wp_spa_json_is_object (json)) {
    wp_warning_object (self,
        "ignoring wireplumber.settings from conf as it isn't a JSON object");
    return g_steal_pointer (&res);
  }

  {
    g_autoptr (WpIterator) iter = wp_spa_json_new_iterator (json);
    g_auto (GValue) item = G_VALUE_INIT;
    while (wp_iterator_next (iter, &item)) {
      WpSpaJson *j = g_value_get_boxed (&item);
      g_autofree gchar *name = wp_spa_json_parse_string (j);
      g_autofree gchar *value = NULL;

      g_value_unset (&item);
      if (!wp_iterator_next (iter, &item)) {
        wp_warning_object (self, "malformed wireplumber.settings from conf");
        return res;
      }
      j = g_value_get_boxed (&item);

      value = wp_spa_json_to_string (j);
      g_value_unset (&item);

      if (name && value)
        wp_properties_set (res, name, value);
    }
  }

  return g_steal_pointer (&res);
}

static gboolean
is_persistent_settings_enabled (WpProperties *settings) {
  const gchar *val_str;
  g_autoptr (WpSpaJson) val = NULL;
  gboolean res = FALSE;

  val_str = wp_properties_get (settings, PERSISTENT_SETTING);
  if (!val_str)
    return FALSE;

  val = wp_spa_json_new_from_string (val_str);
  if (val && !wp_spa_json_parse_boolean (val, &res))
    wp_warning ("Could not parse " PERSISTENT_SETTING " in main configuration");

  return res;
}

static void
on_settings_ready (WpSettings *s, GAsyncResult *res, gpointer data)
{
  WpSettingsPlugin *self = WP_SETTINGS_PLUGIN (data);
  g_autoptr (GError) error = NULL;

  wp_info_object (self, "wpsettings object ready");

  if (!wp_object_activate_finish (WP_OBJECT (s), res, &error)) {
    wp_debug_object (self, "wpsettings activation failed: %s", error->message);
    return;
  }

  wp_object_update_features (WP_OBJECT (self), WP_PLUGIN_FEATURE_ENABLED, 0);
}

static void
on_metadata_activated (WpMetadata * m, GAsyncResult * res, gpointer user_data)
{
  WpTransition *transition = WP_TRANSITION (user_data);
  WpSettingsPlugin *self = wp_transition_get_source_object (transition);
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));
  g_autoptr (WpProperties) config_settings = NULL;
  g_autoptr (GError) error = NULL;
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) item = G_VALUE_INIT;

  if (!wp_object_activate_finish (WP_OBJECT (m), res, &error)) {
    g_prefix_error (&error, "Failed to activate \"%s\": "
        "Metadata object ", self->metadata_name);
    wp_transition_return_error (transition, g_steal_pointer (&error));
    return;
  }

  /* Load settings from configuration */
  config_settings = load_configuration_settings (self);
  if (!config_settings) {
    wp_transition_return_error (transition, g_error_new (
        WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_OPERATION_FAILED,
        "failed to parse settings"));
    return;
  }

  /* Don't use configuration settings if persistent settings is enabled */
  if (!is_persistent_settings_enabled (config_settings)) {
    wp_info_object (self, PERSISTENT_SETTING
       " is disabled, current configuration file settings will be used");

    g_clear_pointer (&self->settings, wp_properties_unref);
    self->settings = g_steal_pointer (&config_settings);
    timeout_save_settings (self, SAVE_INTERVAL_MS);
  } else {
    wp_info_object (self, PERSISTENT_SETTING
       " is enabled, current saved settings will be used");
  }

  for (it = wp_properties_new_iterator (self->settings);
      wp_iterator_next (it, &item);
      g_value_unset (&item)) {
    WpPropertiesItem *pi = g_value_get_boxed (&item);

    const gchar *setting = wp_properties_item_get_key (pi);
    const gchar *value = wp_properties_item_get_value (pi);

    wp_debug_object (self, "%s(%lu) = %s", setting, strlen(value), value);
    wp_metadata_set (m, 0, setting, "Spa:String:JSON", value);
  }
  wp_info_object (self, "loaded settings(%d) to \"%s\" metadata",
      wp_properties_get_count (self->settings), self->metadata_name);

  /* monitor changes in metadata. */
  g_signal_connect_object (m, "changed", G_CALLBACK (on_metadata_changed),
      self, 0);

  g_autoptr (WpSettings) settings = wp_settings_get_instance (core,
      self->metadata_name);

  wp_object_activate (WP_OBJECT (settings), WP_OBJECT_FEATURES_ALL, NULL,
    (GAsyncReadyCallback) on_settings_ready, self);

}

static void
wp_settings_plugin_enable (WpPlugin * plugin, WpTransition * transition)
{
  WpSettingsPlugin * self = WP_SETTINGS_PLUGIN (plugin);
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (plugin));
  g_return_if_fail (core);

  self->state = wp_state_new (NAME);
  self->settings = wp_state_load (self->state);

  /* create metadata object */
  self->impl_metadata = wp_impl_metadata_new_full (core, self->metadata_name,
      NULL);
  wp_object_activate (WP_OBJECT (self->impl_metadata),
      WP_OBJECT_FEATURES_ALL,
      NULL,
      (GAsyncReadyCallback)on_metadata_activated,
      transition);
}

static void
wp_settings_plugin_disable (WpPlugin * plugin)
{
  WpSettingsPlugin * self = WP_SETTINGS_PLUGIN (plugin);

  if (self->timeout_source)
    g_source_destroy (self->timeout_source);
  g_clear_pointer (&self->timeout_source, g_source_unref);

  g_clear_object (&self->impl_metadata);
  g_clear_pointer (&self->settings, wp_properties_unref);
  g_clear_object (&self->state);

  g_clear_pointer (&self->metadata_name, g_free);
}

static void
wp_settings_plugin_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpSettingsPlugin *self = WP_SETTINGS_PLUGIN (object);

  switch (property_id) {
  case PROP_METADATA_NAME:
    self->metadata_name = g_strdup (g_value_get_string (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_settings_plugin_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpSettingsPlugin *self = WP_SETTINGS_PLUGIN (object);

  switch (property_id) {
  case PROP_METADATA_NAME:
    g_value_set_string (value, self->metadata_name);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_settings_plugin_class_init (WpSettingsPluginClass * klass)
{
  WpPluginClass *plugin_class = (WpPluginClass *) klass;
  GObjectClass *object_class = (GObjectClass *) klass;

  plugin_class->enable = wp_settings_plugin_enable;
  plugin_class->disable = wp_settings_plugin_disable;

  object_class->set_property = wp_settings_plugin_set_property;
  object_class->get_property = wp_settings_plugin_get_property;

  g_object_class_install_property (object_class, PROP_METADATA_NAME,
      g_param_spec_string ("metadata-name", "metadata-name",
          "The metadata object to look after", NULL,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

WP_PLUGIN_EXPORT GObject *
wireplumber__module_init (WpCore * core, WpSpaJson * args, GError ** error)
{
  g_autofree gchar *metadata_name = NULL;
  if (args)
    wp_spa_json_object_get (args, "metadata-name", "s", &metadata_name, NULL);

  return G_OBJECT (g_object_new (wp_settings_plugin_get_type (),
      "name", "settings",
      "core", core,
      "metadata-name", metadata_name ? metadata_name : "sm-settings",
      NULL));
}
