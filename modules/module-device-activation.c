/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>

#include <pipewire/pipewire.h>
#include <spa/utils/keys.h>
#include <spa/utils/names.h>

struct _WpDeviceActivation
{
  WpPlugin parent;

  GWeakRef default_profile;
  GWeakRef default_routes;
  WpObjectManager *plugins_om;
  WpObjectManager *devices_om;
};

G_DECLARE_FINAL_TYPE (WpDeviceActivation, wp_device_activation, WP,
    DEVICE_ACTIVATION, WpPlugin)
G_DEFINE_TYPE (WpDeviceActivation, wp_device_activation, WP_TYPE_PLUGIN)

static void
set_device_route (WpDeviceActivation *self, WpPipewireObject *device,
    gint index, gint device_id)
{
  g_return_if_fail (device);

  /* Set route */
  wp_pipewire_object_set_param (device, "Route", 0,
      wp_spa_pod_new_object (
          "Spa:Pod:Object:Param:Route", "Route",
          "index", "i", index,
          "device", "i", device_id,
          NULL));
  wp_info_object (self, "route %d set on device " WP_OBJECT_FORMAT, index,
      WP_OBJECT_ARGS (device));
}

static void
on_device_enum_route_done (WpPipewireObject *proxy, GAsyncResult *res,
    WpDeviceActivation *self)
{
  g_autoptr (WpIterator) routes = NULL;
  g_autoptr (GError) error = NULL;
  g_autoptr (WpPlugin) dr = g_weak_ref_get (&self->default_routes);
  GHashTable *default_routes = NULL;

  /* Finish */
  routes = wp_pipewire_object_enum_params_finish (proxy, res, &error);
  if (error) {
    wp_warning_object (self, "failed to enum routes on device");
    return;
  }

  /* Get the default route name if default-route module is loaded */
  if (dr)
    g_signal_emit_by_name (dr, "get-routes", WP_DEVICE (proxy), &default_routes);

  /* Find the route index and device ID */
  if (default_routes) {
    g_auto (GValue) item = G_VALUE_INIT;
    for (; wp_iterator_next (routes, &item); g_value_unset (&item)) {
      WpSpaPod *pod = g_value_get_boxed (&item);
      const gchar *n = NULL;
      gpointer device_id;
      gint i = 0;

      /* Parse route */
      if (!wp_spa_pod_get_object (pod, NULL,
          "index", "i", &i,
          "name", "s", &n,
          NULL)) {
        continue;
      }

      /* Set the route */
      if (g_hash_table_lookup_extended (default_routes, n, NULL, &device_id))
        set_device_route (self, proxy, i, GPOINTER_TO_INT (device_id));
    }
  }
}

static void
set_device_profile (WpDeviceActivation *self, WpPipewireObject *device, gint index)
{
  g_return_if_fail (device);

  /* Set profile */
  wp_pipewire_object_set_param (device, "Profile", 0,
      wp_spa_pod_new_object (
          "Spa:Pod:Object:Param:Profile", "Profile",
          "index", "i", index,
          NULL));

  wp_info_object (self, "profile %d set on device " WP_OBJECT_FORMAT, index,
      WP_OBJECT_ARGS (device));
}

static void
on_device_enum_profile_done (WpPipewireObject *proxy, GAsyncResult *res,
    WpDeviceActivation *self)
{
  g_autoptr (WpPlugin) dp = g_weak_ref_get (&self->default_profile);
  g_autoptr (WpIterator) profiles = NULL;
  g_autoptr (GError) error = NULL;
  const gchar *name = NULL;
  gint index = -1;

  /* Finish */
  profiles = wp_pipewire_object_enum_params_finish (proxy, res, &error);
  if (error) {
    wp_warning_object (self, "failed to enum profiles on device");
    return;
  }

  /* Get the default profile name if default-profile module is loaded */
  if (dp)
    g_signal_emit_by_name (dp, "get-profile", WP_DEVICE (proxy), &name);

  /* Find the profile index */
  if (name) {
    g_auto (GValue) item = G_VALUE_INIT;
    for (; wp_iterator_next (profiles, &item); g_value_unset (&item)) {
      WpSpaPod *pod = g_value_get_boxed (&item);
      gint i = 0;
      const gchar *n = NULL;

      /* Parse */
      if (!wp_spa_pod_get_object (pod, NULL,
          "index", "i", &i,
          "name", "s", &n,
          NULL)) {
        continue;
      }

      if (g_strcmp0 (name, n) == 0) {
        index = i;
        break;
      }
    }
  }

  /* If not profile was found, use index 1 for ALSA (no ACP) and Bluez5 */
  if (index < 0) {
    /* Alsa */
    const gchar *api =
        wp_pipewire_object_get_property (proxy, PW_KEY_DEVICE_API);
    if (api && g_str_has_prefix (api, "alsa")) {
      const gchar *acp =
          wp_pipewire_object_get_property (proxy, "device.api.alsa.acp");
      if (!acp || !atoi (acp))
        index = 1;
    }

    /* Bluez5 */
    else if (api && g_str_has_prefix (api, "bluez5")) {
      index = 1;
    }
  }

  /* Set the profile */
  if (index >= 0)
    set_device_profile (self, proxy, index);

  /*
   * Enum available routes
   * This is done after setting the profile as a profile change can change the
   * current route (this is f.e. the case with Alsa ACP devices)
   */
  wp_pipewire_object_enum_params (proxy, "EnumRoute", NULL, NULL,
      (GAsyncReadyCallback) on_device_enum_route_done, self);
}

static void
on_device_added (WpObjectManager *om, WpPipewireObject *proxy, gpointer d)
{
  WpDeviceActivation *self = WP_DEVICE_ACTIVATION (d);

  /* Enum available profiles */
  wp_pipewire_object_enum_params (proxy, "EnumProfile", NULL, NULL,
      (GAsyncReadyCallback) on_device_enum_profile_done, self);
}

static void
on_plugin_added (WpObjectManager *om, WpPlugin *plugin, gpointer d)
{
  WpDeviceActivation *self = WP_DEVICE_ACTIVATION (d);
  g_autoptr (WpPlugin) dp = g_weak_ref_get (&self->default_profile);
  g_autoptr (WpPlugin) dr = g_weak_ref_get (&self->default_routes);
  const gchar *name = wp_plugin_get_name (plugin);

  if (g_strcmp0 (name, "default-profile") == 0) {
    if (dp)
      wp_warning_object (self, "skipping additional default profile plugin");
    else
      g_weak_ref_set (&self->default_profile, plugin);
  } else if (g_strcmp0 (name, "default-routes") == 0) {
    if (dr)
      wp_warning_object (self, "skipping additional default routes plugin");
    else
      g_weak_ref_set (&self->default_routes, plugin);
  }
}

static void
wp_device_activation_enable (WpPlugin * plugin, WpTransition * transition)
{
  WpDeviceActivation *self = WP_DEVICE_ACTIVATION (plugin);
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (plugin));

  /* Create the plugin object manager */
  self->plugins_om = wp_object_manager_new ();
  wp_object_manager_add_interest (self->plugins_om, WP_TYPE_PLUGIN,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "name", "c(ss)",
      "default-profile", "default-routes", NULL);
  g_signal_connect_object (self->plugins_om, "object-added",
      G_CALLBACK (on_plugin_added), self, 0);
  wp_core_install_object_manager (core, self->plugins_om);

  /* Create the devices object manager */
  self->devices_om = wp_object_manager_new ();
  wp_object_manager_add_interest (self->devices_om, WP_TYPE_DEVICE, NULL);
  wp_object_manager_request_object_features (self->devices_om,
      WP_TYPE_DEVICE, WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL);
  g_signal_connect_object (self->devices_om, "object-added",
      G_CALLBACK (on_device_added), self, 0);
  wp_core_install_object_manager (core, self->devices_om);

  wp_object_update_features (WP_OBJECT (self), WP_PLUGIN_FEATURE_ENABLED, 0);
}

static void
wp_device_activation_disable (WpPlugin * plugin)
{
  WpDeviceActivation *self = WP_DEVICE_ACTIVATION (plugin);

  g_clear_object (&self->devices_om);
  g_clear_object (&self->plugins_om);
  g_weak_ref_clear (&self->default_profile);
  g_weak_ref_clear (&self->default_routes);
}

static void
wp_device_activation_init (WpDeviceActivation * self)
{
}

static void
wp_device_activation_class_init (WpDeviceActivationClass * klass)
{
  WpPluginClass *plugin_class = (WpPluginClass *) klass;

  plugin_class->enable = wp_device_activation_enable;
  plugin_class->disable = wp_device_activation_disable;
}

WP_PLUGIN_EXPORT gboolean
wireplumber__module_init (WpCore * core, GVariant * args, GError ** error)
{
  wp_plugin_register (g_object_new (wp_device_activation_get_type (),
      "name", "device-activation",
      "core", core,
      NULL));
  return TRUE;
}
