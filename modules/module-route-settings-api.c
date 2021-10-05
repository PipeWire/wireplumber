/* WirePlumber
 *
 * Copyright © 2021 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>
#include <pipewire/keys.h>
#include <spa/utils/json.h>

struct _WpRouteSettingsApi
{
  WpPlugin parent;

  WpImplMetadata *metadata;
};

enum {
  ACTION_CONVERT,
  N_SIGNALS
};

static guint signals[N_SIGNALS] = {0};

G_DECLARE_FINAL_TYPE (WpRouteSettingsApi, wp_route_settings_api,
                      WP, ROUTE_SETTINGS_API, WpPlugin)
G_DEFINE_TYPE (WpRouteSettingsApi, wp_route_settings_api, WP_TYPE_PLUGIN)

static void
wp_route_settings_api_init (WpRouteSettingsApi * self)
{
}

static void
on_metadata_activated (GObject * obj, GAsyncResult * res, gpointer user_data)
{
  WpTransition * transition = WP_TRANSITION (user_data);
  WpRouteSettingsApi * self = wp_transition_get_source_object (transition);
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
wp_route_settings_api_enable (WpPlugin * plugin, WpTransition * transition)
{
  WpRouteSettingsApi * self = WP_ROUTE_SETTINGS_API (plugin);
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (plugin));
  g_return_if_fail (core);

  self->metadata = wp_impl_metadata_new_full (core, "route-settings", NULL);
  wp_object_activate (WP_OBJECT (self->metadata),
        WP_OBJECT_FEATURES_ALL, NULL, on_metadata_activated, transition);
}

static void
wp_route_settings_api_disable (WpPlugin * plugin)
{
  WpRouteSettingsApi * self = WP_ROUTE_SETTINGS_API (plugin);
  g_clear_object (&self->metadata);
}

static gchar *
wp_route_settings_api_convert (WpRouteSettingsApi * self,
    const gchar * json, const gchar *field)
{
  struct spa_json it[3];
  char k[128];

  spa_json_init(&it[0], json, strlen(json));
  if (spa_json_enter_object(&it[0], &it[1]) <= 0)
    return NULL;

  while (spa_json_get_string(&it[1], k, sizeof(k)-1) > 0) {
    int len;
    const char *value;

    if (strcmp(k, field) != 0)
      continue;

    if ((len = spa_json_next(&it[1], &value)) <= 0)
      break;

    if (spa_json_is_null(value, len))
      return NULL;
    else if (spa_json_is_array(value, len)) {
      GString *str;
      spa_json_enter(&it[1], &it[2]);
      str = g_string_new("");
      while ((len = spa_json_next(&it[2], &value)) > 0) {
        char v[1024];
	if (len > 1023)
          continue;
        spa_json_parse_string(value, len, v);
        g_string_append_printf(str, "%s;", v);
      }
      return g_string_free(str, false);
    }
    else
      return g_strndup(value, len);
  }
  return NULL;
}

static void
wp_route_settings_api_class_init (WpRouteSettingsApiClass * klass)
{
  WpPluginClass *plugin_class = (WpPluginClass *) klass;

  plugin_class->enable = wp_route_settings_api_enable;
  plugin_class->disable = wp_route_settings_api_disable;

  signals[ACTION_CONVERT] = g_signal_new_class_handler (
      "convert", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      (GCallback) wp_route_settings_api_convert,
      NULL, NULL, NULL,
      G_TYPE_STRING, 2, G_TYPE_STRING, G_TYPE_STRING);
}

WP_PLUGIN_EXPORT gboolean
wireplumber__module_init (WpCore * core, GVariant * args, GError ** error)
{
  wp_plugin_register (g_object_new (wp_route_settings_api_get_type (),
          "name", "route-settings-api",
          "core", core,
          NULL));
  return TRUE;
}
