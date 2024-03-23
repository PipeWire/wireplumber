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
  gchar *metadata_schema_name;
  gchar *metadata_persistent_name;

  WpImplMetadata *impl_metadata;
  WpImplMetadata *schema_impl_metadata;
  WpImplMetadata *persistent_impl_metadata;
  WpState *state;
  WpProperties *persistent_settings;
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

static void
wp_settings_plugin_init (WpSettingsPlugin * self)
{
}

static void
on_persistent_metadata_changed (WpMetadata *m, guint32 subject,
   const gchar *key, const gchar *type, const gchar *value, gpointer d)
{
  WpSettingsPlugin *self = WP_SETTINGS_PLUGIN (d);
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));

  /* Update persistent settings with new value. If key is null it means all
   * settings need to be removed */
  if (key) {
    wp_properties_set (self->persistent_settings, key, value);
    if (value)
      wp_info_object (self, "persistent setting updated: %s = %s", key, value);
    else
      wp_info_object (self, "persistent setting removed: %s", key);
  } else {
    g_clear_pointer (&self->persistent_settings, wp_properties_unref);
    self->persistent_settings = wp_properties_new_empty ();
    wp_info_object (self, "all persistent settings removed");
  }

  /* Save changes */
  wp_state_save_after_timeout (self->state, core, self->persistent_settings);

  /* Also update current settings with new value */
  if (value)
    wp_metadata_set (WP_METADATA (self->impl_metadata), 0, key, type, value);
}

WpProperties *
load_configuration_settings (WpSettingsPlugin *self)
{
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));
  g_autoptr (WpConf) conf = NULL;
  g_autoptr (WpSpaJson) json = NULL;
  g_autoptr (WpProperties) res = wp_properties_new_empty ();

  g_return_val_if_fail (core, NULL);
  conf = wp_core_get_conf (core);
  g_return_val_if_fail (conf, NULL);

  json = wp_conf_get_section (conf, "wireplumber.settings");
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

static void
on_metadata_activated (WpMetadata * m, GAsyncResult * res,
    gpointer user_data)
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

  /* Update the configuration properties with persistent settings */
  wp_properties_update (config_settings, self->persistent_settings);

  /* Populate settings metadata from schema using values from configuration if
   * they are present, otherwise use default values */
  it = wp_metadata_new_iterator (WP_METADATA (self->schema_impl_metadata), 0);
  for (; wp_iterator_next (it, &item); g_value_unset (&item)) {
    WpMetadataItem *mi = g_value_get_boxed (&item);
    const gchar *key = wp_metadata_item_get_key (mi);
    const gchar *spec_str = wp_metadata_item_get_value (mi);
    const gchar *value;
    g_autoptr (WpSpaJson) spec_json = NULL;
    g_autoptr (WpSpaJson) def_value = NULL;

    /* Use configuration value if found, otherwise use default value */
    value = wp_properties_get (config_settings, key);
    if (!value) {
      spec_json = wp_spa_json_new_from_string (spec_str);

      if (!wp_spa_json_is_object (spec_json)) {
        wp_warning_object (self,
            "settings schema spec for %s is not an object: %s", key, spec_str);
        continue;
      }

      if (!wp_spa_json_object_get (spec_json, "default", "J", &def_value,
          NULL)) {
        wp_warning_object (self,
            "settings schema spec for %s does not have default value: %s", key,
            spec_str);
        continue;
      }

      value = wp_spa_json_get_data (def_value);
    }

    /* Add setting in the metadata */
    wp_debug_object (self, "adding setting to %s metadata: %s = %s",
        self->metadata_name, key, value);
    wp_metadata_set (m, 0, key, "Spa:String:JSON", value);
  }

  wp_object_update_features (WP_OBJECT (self), WP_PLUGIN_FEATURE_ENABLED, 0);
}

static void
on_persistent_metadata_activated (WpMetadata * m, GAsyncResult * res,
    gpointer user_data)
{
  WpTransition *transition = WP_TRANSITION (user_data);
  WpSettingsPlugin *self = wp_transition_get_source_object (transition);
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));
  g_autoptr (GError) error = NULL;
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) item = G_VALUE_INIT;

  if (!wp_object_activate_finish (WP_OBJECT (m), res, &error)) {
    g_prefix_error (&error, "Failed to activate \"%s\": "
        "Metadata object ", self->metadata_persistent_name);
    wp_transition_return_error (transition, g_steal_pointer (&error));
    return;
  }

  /* Load the persistent settings */
  self->state = wp_state_new (NAME);
  self->persistent_settings = wp_state_load (self->state);

  /* Set persistent settings in persistent metadata */
  for (it = wp_properties_new_iterator (self->persistent_settings);
      wp_iterator_next (it, &item);
      g_value_unset (&item)) {
    WpPropertiesItem *pi = g_value_get_boxed (&item);
    const gchar *key = wp_properties_item_get_key (pi);
    const gchar *value = wp_properties_item_get_value (pi);

    wp_debug_object (self, "adding persistent setting to %s metadata: %s = %s",
        self->metadata_persistent_name, key, value);
    wp_metadata_set (m, 0, key, "Spa:String:JSON", value);
  }

  /* monitor changes in persistent metadata */
  g_signal_connect_object (m, "changed",
      G_CALLBACK (on_persistent_metadata_changed), self, 0);

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
on_schema_metadata_activated (WpMetadata * m, GAsyncResult * res,
    gpointer user_data)
{
  WpTransition *transition = WP_TRANSITION (user_data);
  WpSettingsPlugin *self = wp_transition_get_source_object (transition);
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));
  g_autoptr (WpConf) conf = wp_core_get_conf (core);
  g_autoptr (GError) error = NULL;
  g_autoptr (WpSpaJson) schema_json = NULL;

  if (!wp_object_activate_finish (WP_OBJECT (m), res, &error)) {
    wp_transition_return_error (transition, g_error_new (
        WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_OPERATION_FAILED,
        "Failed to activate metadata object %s", self->metadata_schema_name));
    return;
  }

  /* Load the schema into metadata if any */
  schema_json = wp_conf_get_section (conf, "wireplumber.settings.schema");
  if (schema_json) {
    g_autoptr (WpIterator) it = NULL;
    g_auto (GValue) item = G_VALUE_INIT;

    if (!wp_spa_json_is_object (schema_json)) {
      wp_transition_return_error (transition, g_error_new (
          WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_OPERATION_FAILED,
          "Settings schema is not a JSON object: %s",
          wp_spa_json_get_data (schema_json)));
      return;
    }

    it = wp_spa_json_new_iterator (schema_json);
    while (wp_iterator_next (it, &item)) {
      WpSpaJson *j = g_value_get_boxed (&item);
      g_autofree gchar *key = wp_spa_json_parse_string (j);
      g_autofree gchar *value = NULL;

      g_value_unset (&item);
      if (!wp_iterator_next (it, &item)) {
        wp_transition_return_error (transition, g_error_new (WP_DOMAIN_LIBRARY,
            WP_LIBRARY_ERROR_INVARIANT, "Malformed settings schema"));
        return;
      }
      j = g_value_get_boxed (&item);
      value = wp_spa_json_to_string (j);
      g_value_unset (&item);

      wp_debug_object (self, "adding schema setting to %s metadata: %s = %s",
          self->metadata_schema_name, key, value);
      wp_metadata_set (m, 0, key, "Spa:String:JSON", value);
    }
  } else {
    wp_warning_object (self, "settings schema not found in configuration");
  }

  /* create persistent metadata object */
  self->persistent_impl_metadata = wp_impl_metadata_new_full (core,
      self->metadata_persistent_name, NULL);
  wp_object_activate (WP_OBJECT (self->persistent_impl_metadata),
      WP_OBJECT_FEATURES_ALL,
      NULL,
      (GAsyncReadyCallback)on_persistent_metadata_activated,
      transition);
}

static void
wp_settings_plugin_enable (WpPlugin * plugin, WpTransition * transition)
{
  WpSettingsPlugin * self = WP_SETTINGS_PLUGIN (plugin);
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (plugin));

  /* create schema metadata object */
  self->schema_impl_metadata = wp_impl_metadata_new_full (core,
      self->metadata_schema_name, NULL);
  wp_object_activate (WP_OBJECT (self->schema_impl_metadata),
      WP_OBJECT_FEATURES_ALL,
      NULL,
      (GAsyncReadyCallback)on_schema_metadata_activated,
      transition);
}

static void
wp_settings_plugin_disable (WpPlugin * plugin)
{
  WpSettingsPlugin * self = WP_SETTINGS_PLUGIN (plugin);

  g_clear_object (&self->impl_metadata);
  g_clear_object (&self->schema_impl_metadata);
  g_clear_object (&self->persistent_impl_metadata);
  g_clear_pointer (&self->persistent_settings, wp_properties_unref);
  g_clear_object (&self->state);

  g_clear_pointer (&self->metadata_name, g_free);
  g_clear_pointer (&self->metadata_schema_name, g_free);
  g_clear_pointer (&self->metadata_persistent_name, g_free);
}

static void
wp_settings_plugin_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpSettingsPlugin *self = WP_SETTINGS_PLUGIN (object);

  switch (property_id) {
  case PROP_METADATA_NAME:
    self->metadata_name = g_value_dup_string (value);
    self->metadata_schema_name = g_strdup_printf (
        WP_SETTINGS_SCHEMA_METADATA_NAME_PREFIX "%s", self->metadata_name);
    self->metadata_persistent_name = g_strdup_printf (
        WP_SETTINGS_PERSISTENT_METADATA_NAME_PREFIX "%s", self->metadata_name);
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
    wp_spa_json_object_get (args, "metadata.name", "s", &metadata_name, NULL);

  return G_OBJECT (g_object_new (wp_settings_plugin_get_type (),
      "name", "settings",
      "core", core,
      "metadata-name", metadata_name ? metadata_name : "sm-settings",
      NULL));
}
