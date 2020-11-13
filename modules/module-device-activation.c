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

  WpObjectManager *devices_om;
};

G_DECLARE_FINAL_TYPE (WpDeviceActivation, wp_device_activation, WP,
    DEVICE_ACTIVATION, WpPlugin)
G_DEFINE_TYPE (WpDeviceActivation, wp_device_activation, WP_TYPE_PLUGIN)

static void
set_device_profile (WpPipewireObject *device, gint index)
{
  g_return_if_fail (device);
  g_autoptr (WpSpaPod) profile = wp_spa_pod_new_object (
      "Profile", "Profile",
      "index", "i", index,
      NULL);
  wp_debug_object (device, "set profile %d", index);
  wp_pipewire_object_set_param (device, "Profile", profile);
}

static void
on_device_enum_profile_done (WpPipewireObject *proxy, GAsyncResult *res,
    WpDeviceActivation *self)
{
  g_autoptr (WpIterator) profiles = NULL;
  g_auto (GValue) item = G_VALUE_INIT;
  g_autoptr (GError) error = NULL;
  guint profile_index = 1;

  profiles = wp_pipewire_object_enum_params_finish (proxy, res, &error);
  if (error) {
    wp_warning_object (self, "failed to enum profiles in bluetooth device");
    return;
  }

  /* Get the first available profile */
  for (; wp_iterator_next (profiles, &item); g_value_unset (&item)) {
    WpSpaPod *pod = g_value_get_boxed (&item);
    gint index = 0;
    const gchar *name = NULL;

    g_return_if_fail (pod);
    g_return_if_fail (wp_spa_pod_is_object (pod));

    /* Parse */
    if (!wp_spa_pod_get_object (pod,
        "Profile", NULL,
        "index", "i", &index,
        "name", "s", &name,
        NULL)) {
      wp_warning_object (self, "bluetooth profile does not have index / name");
      continue;
    }
    wp_info_object (self, "bluez profile found: %s (%d)", name, index);

    /* TODO: we assume the last profile is the one with highest priority */
    profile_index = index;
  }

  set_device_profile (proxy, profile_index);
}

static void
on_device_added (WpObjectManager *om, WpPipewireObject *proxy, gpointer d)
{
  WpDeviceActivation *self = WP_DEVICE_ACTIVATION (d);
  const gchar *device_api =
      wp_pipewire_object_get_property (proxy, PW_KEY_DEVICE_API);
  g_return_if_fail (device_api);

  wp_debug_object (self, "device " WP_OBJECT_FORMAT " added, api '%s'",
      WP_OBJECT_ARGS (proxy), device_api);

  /* ALSA */
  if (g_str_has_prefix (device_api, "alsa")) {
    set_device_profile (proxy, 1);
  }

  /* Bluez5 */
  else if (g_str_has_prefix (device_api, "bluez5")) {
    /* Enum available bluetooth profiles */
    wp_pipewire_object_enum_params (proxy, "EnumProfile", NULL, NULL,
          (GAsyncReadyCallback) on_device_enum_profile_done, self);
  }

  /* Video */
  else if (g_str_has_prefix (device_api, "v4l2")) {
    /* No need to activate video devices */
  }
}

static void
wp_device_activation_activate (WpPlugin * plugin)
{
  WpDeviceActivation *self = WP_DEVICE_ACTIVATION (plugin);
  g_autoptr (WpCore) core = wp_plugin_get_core (WP_PLUGIN (self));

  /* Create the devices object manager */
  self->devices_om = wp_object_manager_new ();
  wp_object_manager_add_interest (self->devices_om, WP_TYPE_DEVICE, NULL);
  wp_object_manager_request_object_features (self->devices_om,
      WP_TYPE_DEVICE, WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL);
  g_signal_connect_object (self->devices_om, "object-added",
      G_CALLBACK (on_device_added), self, 0);
  wp_core_install_object_manager (core, self->devices_om);
}

static void
wp_device_activation_deactivate (WpPlugin * plugin)
{
  WpDeviceActivation *self = WP_DEVICE_ACTIVATION (plugin);

  g_clear_object (&self->devices_om);
}

static void
wp_device_activation_init (WpDeviceActivation * self)
{
}

static void
wp_device_activation_class_init (WpDeviceActivationClass * klass)
{
  WpPluginClass *plugin_class = (WpPluginClass *) klass;

  plugin_class->activate = wp_device_activation_activate;
  plugin_class->deactivate = wp_device_activation_deactivate;
}


WP_PLUGIN_EXPORT void
wireplumber__module_init (WpModule * module, WpCore * core, GVariant * args)
{
  wp_plugin_register (g_object_new (wp_device_activation_get_type (),
      "name", "device-activation",
      "module", module,
      NULL));
}
