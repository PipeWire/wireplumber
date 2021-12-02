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
  WpObjectManager *plugins_om;
  WpObjectManager *devices_om;
};

G_DECLARE_FINAL_TYPE (WpDeviceActivation, wp_device_activation, WP,
    DEVICE_ACTIVATION, WpPlugin)
G_DEFINE_TYPE (WpDeviceActivation, wp_device_activation, WP_TYPE_PLUGIN)

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
handle_device_profiles (WpDeviceActivation *self, WpPipewireObject *proxy,
    WpIterator *profiles)
{
  g_autoptr (WpPlugin) dp = g_weak_ref_get (&self->default_profile);
  g_auto (GValue) item = G_VALUE_INIT;
  const gchar *def_name = NULL;
  gint best_idx = -1, unk_idx = -1, off_idx = -1;
  gint best_prio = 0, unk_prio = 0;

  /* Get the default profile name if default-profile module is loaded */
  if (dp)
    g_signal_emit_by_name (dp, "get-profile", WP_DEVICE (proxy), &def_name);

  /* Find the best profile index */
  for (; wp_iterator_next (profiles, &item); g_value_unset (&item)) {
    WpSpaPod *pod = g_value_get_boxed (&item);
    gint idx, prio = 0;
    guint32 avail = SPA_PARAM_AVAILABILITY_unknown;
    const gchar *name;

    /* Parse */
    if (!wp_spa_pod_get_object (pod, NULL,
        "index", "i", &idx,
        "name", "s", &name,
        "priority", "?i", &prio,
        "available", "?I", &avail,
        NULL)) {
      continue;
    }

    /* If default profile name is found and is available, select it. Otherwise
     * find the best profile */
    if (avail == SPA_PARAM_AVAILABILITY_yes &&
        def_name && g_strcmp0 (def_name, name) == 0) {
      best_idx = idx;
      break;
    }

    if (g_strcmp0 (name, "pro-audio") == 0)
      continue;

    if (g_strcmp0 (name, "off") == 0) {
      off_idx = idx;
    } else if (avail == SPA_PARAM_AVAILABILITY_yes) {
      if (best_idx == -1 || prio > best_prio) {
        best_prio = prio;
        best_idx = idx;
      }
    } else if (avail != SPA_PARAM_AVAILABILITY_no) {
      if (unk_idx == -1 || prio > unk_prio) {
        unk_prio = prio;
        unk_idx = idx;
      }
    }
  }

  /* Set the profile */
  if (best_idx != -1)
    set_device_profile (self, proxy, best_idx);
  else if (unk_idx != -1)
    set_device_profile (self, proxy, unk_idx);
  else if (off_idx != -1)
    set_device_profile (self, proxy, off_idx);
}

static void
on_device_added (WpObjectManager *om, WpPipewireObject *proxy, gpointer d)
{
  WpDeviceActivation *self = WP_DEVICE_ACTIVATION (d);
  g_autoptr (WpIterator) profiles = NULL;

  /* Enum available profiles */
  profiles = wp_pipewire_object_enum_params_sync (proxy, "EnumProfile", NULL);
  if (!profiles)
    return;

  handle_device_profiles (self, proxy, profiles);
}

static void
on_plugin_added (WpObjectManager *om, WpPlugin *plugin, gpointer d)
{
  WpDeviceActivation *self = WP_DEVICE_ACTIVATION (d);
  g_autoptr (WpPlugin) dp = g_weak_ref_get (&self->default_profile);
  const gchar *name = wp_plugin_get_name (plugin);

  if (g_strcmp0 (name, "default-profile") == 0) {
    if (dp)
      wp_warning_object (self, "skipping additional default profile plugin");
    else
      g_weak_ref_set (&self->default_profile, plugin);
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
      WP_CONSTRAINT_TYPE_G_PROPERTY, "name", "=s", "default-profile", NULL);
  g_signal_connect_object (self->plugins_om, "object-added",
      G_CALLBACK (on_plugin_added), self, 0);
  wp_core_install_object_manager (core, self->plugins_om);

  /* Create the devices object manager */
  self->devices_om = wp_object_manager_new ();
  wp_object_manager_add_interest (self->devices_om, WP_TYPE_DEVICE, NULL);
  wp_object_manager_request_object_features (self->devices_om,
      WP_TYPE_DEVICE, WP_PIPEWIRE_OBJECT_FEATURES_ALL);
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
