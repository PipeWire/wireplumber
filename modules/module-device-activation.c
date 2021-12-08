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

G_DEFINE_QUARK (wp-module-device-activation-best-profile, best_profile);
G_DEFINE_QUARK (wp-module-device-activation-active-profile, active_profile);

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
  gpointer active_ptr = NULL;

  g_return_if_fail (device);

  /* Make sure the profile we want to set is not active */
  active_ptr = g_object_get_qdata (G_OBJECT (device), active_profile_quark ());
  if (active_ptr && GPOINTER_TO_INT (active_ptr) - 1 == index) {
    wp_info_object (self, "profile %d is already active", index);
    return;
  }

  /* Set profile */
  wp_pipewire_object_set_param (device, "Profile", 0,
      wp_spa_pod_new_object (
          "Spa:Pod:Object:Param:Profile", "Profile",
          "index", "i", index,
          NULL));

  wp_info_object (self, "profile %d set on device " WP_OBJECT_FORMAT, index,
      WP_OBJECT_ARGS (device));
}

static gint
find_active_profile (WpPipewireObject *proxy, gboolean *off)
{
  g_autoptr (WpIterator) profiles = NULL;
  g_auto (GValue) item = G_VALUE_INIT;
  gint idx = -1, prio = 0;
  guint32 avail = SPA_PARAM_AVAILABILITY_unknown;
  const gchar *name;

  /* Get current profile */
  profiles = wp_pipewire_object_enum_params_sync (proxy, "Profile", NULL);
  if (!profiles)
    return idx;

  for (; wp_iterator_next (profiles, &item); g_value_unset (&item)) {
    WpSpaPod *pod = g_value_get_boxed (&item);
    if (!wp_spa_pod_get_object (pod, NULL,
        "index", "i", &idx,
        "name", "s", &name,
        "priority", "?i", &prio,
        "available", "?I", &avail,
        NULL))
      continue;

    g_value_unset (&item);
    break;
  }

  if (off)
    *off = idx >= 0 && g_strcmp0 (name, "off") == 0;

  return idx;
}

static gint
find_best_profile (WpIterator *profiles)
{
  g_auto (GValue) item = G_VALUE_INIT;
  gint best_idx = -1, unk_idx = -1, off_idx = -1;
  gint best_prio = 0, unk_prio = 0;

  for (; wp_iterator_next (profiles, &item); g_value_unset (&item)) {
    WpSpaPod *pod = g_value_get_boxed (&item);
    gint idx, prio = 0;
    guint32 avail = SPA_PARAM_AVAILABILITY_unknown;
    const gchar *name;

    if (!wp_spa_pod_get_object (pod, NULL,
        "index", "i", &idx,
        "name", "s", &name,
        "priority", "?i", &prio,
        "available", "?I", &avail,
        NULL)) {
      continue;
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

  if (best_idx != -1)
    return best_idx;
  else if (unk_idx != -1)
    return unk_idx;
  else if (off_idx != -1)
    return off_idx;
  return -1;
}

static gint
find_default_profile (WpDeviceActivation *self, WpPipewireObject *proxy,
    WpIterator *profiles, gboolean *available)
{
  g_autoptr (WpPlugin) dp = g_weak_ref_get (&self->default_profile);
  g_auto (GValue) item = G_VALUE_INIT;
  const gchar *def_name = NULL;

  /* Get the default profile name if default-profile module is loaded */
  if (dp)
    g_signal_emit_by_name (dp, "get-profile", WP_DEVICE (proxy), &def_name);
  if (!def_name)
    return -1;

  /* Find the best profile index */
  for (; wp_iterator_next (profiles, &item); g_value_unset (&item)) {
    WpSpaPod *pod = g_value_get_boxed (&item);
    gint idx = -1, prio = 0;
    guint32 avail = SPA_PARAM_AVAILABILITY_unknown;
    const gchar *name = NULL;

    /* Parse */
    if (!wp_spa_pod_get_object (pod, NULL,
        "index", "i", &idx,
        "name", "s", &name,
        "priority", "?i", &prio,
        "available", "?I", &avail,
        NULL))
      continue;

    /* Check if the profile name is the default one */
    if (g_strcmp0 (def_name, name) == 0) {
      if (available)
        *available = avail;
      g_value_unset (&item);
      return idx;
    }
  }

  return -1;
}

static gint
handle_active_profile (WpDeviceActivation *self, WpPipewireObject *proxy,
    WpIterator *profiles, gboolean *changed, gboolean *off)
{
  gpointer active_ptr = NULL;
  gint new_active = -1;
  gint local_changed = FALSE;

  /* Find the new active profile */
  new_active = find_active_profile (proxy, off);
  if (new_active < 0) {
    wp_info_object (self, "cannot find active profile");
    return new_active;
  }

  /* Update active profile if changed */
  active_ptr = g_object_get_qdata (G_OBJECT (proxy), active_profile_quark ());
  local_changed = !active_ptr || GPOINTER_TO_INT (active_ptr) - 1 != new_active;
  if (local_changed) {
    wp_info_object (self, "active profile changed to: %d", new_active);
    g_object_set_qdata (G_OBJECT (proxy), active_profile_quark (),
        GINT_TO_POINTER (new_active + 1));
  }

  if (changed)
    *changed = local_changed;

  return new_active;
}

static gint
handle_best_profile (WpDeviceActivation *self, WpPipewireObject *proxy,
    WpIterator *profiles, gboolean *changed)
{
  gpointer best_ptr = NULL;
  gint new_best = -1;
  gboolean local_changed = FALSE;

  /* Get the new best profile index */
  new_best = find_best_profile (profiles);
  if (new_best < 0) {
    wp_info_object (self, "cannot find best profile");
    return new_best;
  }

  /* Update best profile if changed */
  best_ptr = g_object_get_qdata (G_OBJECT (proxy), best_profile_quark ());
  local_changed = !best_ptr || GPOINTER_TO_INT (best_ptr) - 1 != new_best;
  if (local_changed) {
    wp_info_object (self, "found new best profile: %d", new_best);
    g_object_set_qdata (G_OBJECT (proxy), best_profile_quark (),
        GINT_TO_POINTER (new_best + 1));
  }

  if (changed)
    *changed = local_changed;

  return new_best;
}

static void
handle_enum_profiles (WpDeviceActivation *self, WpPipewireObject *proxy,
    WpIterator *profiles)
{
  gint active_idx = FALSE, best_idx = FALSE;
  gboolean active_changed = FALSE, best_changed = FALSE, active_off = FALSE;

  /* Set default device if active profile changed to off */
  active_idx = handle_active_profile (self, proxy, profiles, &active_changed,
      &active_off);
  if (active_idx >= 0 && active_changed && active_off) {
    gboolean default_avail = FALSE;
    gint default_idx = -1;
    default_idx = find_default_profile (self, proxy, profiles, &default_avail);
    if (default_idx >= 0) {
      if (default_avail == SPA_PARAM_AVAILABILITY_no) {
        wp_info_object (self, "default profile %d unavailable", default_idx);
      } else {
        wp_info_object (self, "found default profile: %d", default_idx);
        set_device_profile (self, proxy, default_idx);
        return;
      }
    } else {
      wp_info_object (self, "cannot find default profile");
    }
  }

  /* Otherwise just set the best profile if changed */
  best_idx = handle_best_profile (self, proxy, profiles, &best_changed);
  if (best_idx >= 0 && best_changed)
    set_device_profile (self, proxy, best_idx);
  else if (best_idx >= 0)
    wp_info_object (self, "best profile already set: %d", best_idx);
  else
    wp_info_object (self, "best profile not found");
}

static void
on_device_params_changed (WpPipewireObject * proxy, const gchar *param_name,
    WpDeviceActivation *self)
{
  if (g_strcmp0 (param_name, "EnumProfile") == 0) {
    g_autoptr (WpIterator) profiles = NULL;
    profiles = wp_pipewire_object_enum_params_sync (proxy, "EnumProfile", NULL);
    if (profiles)
      handle_enum_profiles (self, proxy, profiles);
  }
}

static void
on_device_added (WpObjectManager *om, WpPipewireObject *proxy, gpointer d)
{
  WpDeviceActivation *self = WP_DEVICE_ACTIVATION (d);
  g_autoptr (WpIterator) profiles = NULL;

  g_signal_connect_object (proxy, "params-changed",
      G_CALLBACK (on_device_params_changed), self, 0);

  on_device_params_changed (proxy, "EnumProfile", self);
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
