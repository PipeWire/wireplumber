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

/*
 * This module parses the "wireplumber.settings" section from the .conf file.
 *
 * Creates "sm-settings" metadata and pushes the settings to it. Looks out for
 * changes done in the metadata via the pw-metadata interface.
 *
 * If persistent settings is enabled stores the settings in a state file
 * and retains the settings from there on subsequent reboots ignoring the
 * contents of .conf file.
 */

struct _WpSettingsPlugin
{
  WpPlugin parent;

  WpImplMetadata *impl_metadata;

  WpProperties *settings;

  WpState *state;

  GSource *timeout_source;
  guint save_interval_ms;
  gboolean use_persistent_storage;
};

G_DECLARE_FINAL_TYPE (WpSettingsPlugin, wp_settings_plugin,
                      WP, SETTINGS_PLUGIN, WpPlugin)
G_DEFINE_TYPE (WpSettingsPlugin, wp_settings_plugin, WP_TYPE_PLUGIN)

#define NAME "sm-settings"
#define PERSISTENT_SETTING "persistent.settings"

static void
wp_settings_plugin_init (WpSettingsPlugin * self)
{
}

static gboolean
timeout_save_state_callback (WpSettingsPlugin *self)
{
  g_autoptr (GError) error = NULL;

  if (!self->state)
    self->state = wp_state_new (NAME);

  if (!wp_state_save (self->state, self->settings, &error))
    wp_warning_object (self, "%s", error->message);

  g_clear_pointer (&self->timeout_source, g_source_unref);
  return G_SOURCE_REMOVE;
}

static void
timer_start (WpSettingsPlugin *self)
{
  if (!self->timeout_source && self->use_persistent_storage) {
    g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));
    g_return_if_fail (core);

    /* Add the timeout callback */
    wp_core_timeout_add_closure (core, &self->timeout_source,
        self->save_interval_ms,
        g_cclosure_new_object (
            G_CALLBACK (timeout_save_state_callback), G_OBJECT (self)));
  }
}

static gboolean
settings_available_in_state_file (WpSettingsPlugin * self)
{
  g_autoptr (WpState) state = wp_state_new (NAME);
  g_autoptr (WpProperties) settings = wp_state_load (state);
  guint count = wp_properties_get_count(settings);

  if (count > 0) {

    wp_info_object (self, "%d settings are available in state file", count);
    self->state = g_steal_pointer (&state);
    self->use_persistent_storage = true;

    return true;
  } else
    wp_info_object (self, "no settings are available in state file");

  return false;
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
  timer_start (self);
}

struct data {
  WpTransition *transition;
  int count;
  WpProperties *settings;
};

static int
do_parse_settings (void *data, const char *location,
    const char *section, const char *str, size_t len)
{
  struct data *d = data;
  WpTransition *transition = d->transition;
  WpSettingsPlugin *self = wp_transition_get_source_object (transition);
  g_autoptr (WpSpaJson) json = wp_spa_json_new_from_stringn (str, len);
  g_autoptr (WpIterator) iter = wp_spa_json_new_iterator (json);
  g_auto (GValue) item = G_VALUE_INIT;


  if (!wp_spa_json_is_object (json)) {
    /* "wireplumber.settings" section has to be a JSON object element. */
    wp_transition_return_error (transition, g_error_new (
      WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_OPERATION_FAILED,
        "failed to parse \"wireplumber.settings\" settings cannot be loaded"));
    return -EINVAL;
  }

  while (wp_iterator_next (iter, &item)) {
    WpSpaJson *j = g_value_get_boxed (&item);
    g_autofree gchar *name = wp_spa_json_parse_string (j);
    g_autofree gchar *value = NULL;
    int len = 0;

    g_value_unset (&item);
    wp_iterator_next (iter, &item);
    j = g_value_get_boxed (&item);

    value = wp_spa_json_parse_string (j);
    len = wp_spa_json_get_size (j);
    g_value_unset (&item);

    if (name && value) {
      wp_debug_object (self, "%s(%d) = %s", name, len, value);

      wp_properties_set (d->settings, name, value);

      if (g_str_equal (name, PERSISTENT_SETTING) && spa_atob (value)) {
        self->use_persistent_storage = true;
        wp_info_object (self, "Persistent settings enabled");
      }

      d->count++;
    }
  }

  wp_info_object (self, "parsed %d settings & rules from conf file", d->count);

 return 0;
}


static void
on_metadata_activated (WpMetadata * m, GAsyncResult * res, gpointer user_data)
{
  WpTransition *transition = WP_TRANSITION (user_data);
  WpSettingsPlugin *self = wp_transition_get_source_object (transition);
  g_autoptr (GError) error = NULL;

  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));
  struct pw_context *pw_ctx = wp_core_get_pw_context (core);
  g_autoptr (WpProperties) settings = wp_properties_new_empty();
  struct data data = { .transition = transition,
                       .settings = settings };
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) item = G_VALUE_INIT;


  if (!wp_object_activate_finish (WP_OBJECT (m), res, &error)) {
    g_clear_object (&self->impl_metadata);
    g_prefix_error (&error, "Failed to activate \"sm-settings\": \
        Metadata object ");
    wp_transition_return_error (transition, g_steal_pointer (&error));
    return;
  }

  wp_object_update_features (WP_OBJECT (self), WP_PLUGIN_FEATURE_ENABLED, 0);


  if (pw_context_conf_section_for_each (pw_ctx, "wireplumber.settings",
      do_parse_settings, &data) < 0)
    return;

  if (data.count == 0) {
    /*
     * either the "wireplumber.settings" is not found or not defined as a
     * valid JSON object element.
     */
    wp_transition_return_error (transition, g_error_new (
      WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_OPERATION_FAILED,
      "No settings present in the context conf file: settings"
      "are not loaded"));
    return;
  }

  if (!self->use_persistent_storage) {

    /* use parsed settings from .conf file */
    self->settings = g_steal_pointer (&settings);
    wp_info_object(self, "use settings from .conf file");

    if (settings_available_in_state_file (self)) {
      wp_info_object (self, "persistant storage is disabled clear the"
          " settings in the state file");

      wp_state_clear (self->state);
      g_clear_object (&self->state);
    }

  } else if (self->use_persistent_storage) {

    /* consider using settings from state file */
    if (settings_available_in_state_file (self)) {
      self->settings = wp_state_load (self->state);
      wp_info_object (self, "persistant storage enabled and settings are found"
          " in state file, use them");
    }
    else
    {
      wp_info_object (self, "persistant storage enabled but settings are"
          " not found in state file so load from .conf file");
      self->settings = g_steal_pointer (&settings);

      /* save state after time out */
      timer_start (self);
    }

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
  wp_info_object (self, "loaded settings(%d) to \"sm-settings\" metadata",
      wp_properties_get_count (self->settings));


  /* monitor changes in metadata. */
  g_signal_connect_object (m, "changed", G_CALLBACK (on_metadata_changed),
      self, 0);
}

static void
wp_settings_plugin_enable (WpPlugin * plugin, WpTransition * transition)
{
  WpSettingsPlugin * self = WP_SETTINGS_PLUGIN (plugin);
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (plugin));
  g_return_if_fail (core);

  self->use_persistent_storage = false;

  /* create metadata object */
  self->impl_metadata = wp_impl_metadata_new_full (core, "sm-settings", NULL);
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

  g_clear_pointer (&self->settings, wp_properties_unref);
  g_clear_object (&self->impl_metadata);

  g_clear_object (&self->state);
}

static void
wp_settings_plugin_class_init (WpSettingsPluginClass * klass)
{
  WpPluginClass *plugin_class = (WpPluginClass *) klass;

  plugin_class->enable = wp_settings_plugin_enable;
  plugin_class->disable = wp_settings_plugin_disable;
}

WP_PLUGIN_EXPORT gboolean
wireplumber__module_init (WpCore * core, GVariant * args, GError ** error)
{
  wp_plugin_register (g_object_new (wp_settings_plugin_get_type (),
      "name", "settings",
      "core", core,
      NULL));
  return TRUE;
}
