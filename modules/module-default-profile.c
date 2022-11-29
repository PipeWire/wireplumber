/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>
#include <pipewire/pipewire.h>

#define STATE_NAME "default-profile"
#define SAVE_INTERVAL_MS 1000

G_DEFINE_QUARK (wp-module-default-profile-profiles, profiles);

/* Signals */
enum
{
  SIGNAL_GET_PROFILE,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

/*
 * Module caches the profile selected for a device and restores it when the
 * device appears afresh. The cached profile is remembered across reboots.
 * It provides an API for modules and scripts to query the default profile.
 */

/*
 * settings file: device.conf
 */

G_DECLARE_DERIVABLE_TYPE (WpDefaultProfile, wp_default_profile, WP,
    DEFAULT_PROFILE, WpPlugin)

struct _WpDefaultProfileClass
{
  WpPluginClass parent_class;

  gchar *(*get_profile) (WpDefaultProfile *self,
      WpPipewireObject *device);
};

typedef struct _WpDefaultProfilePrivate WpDefaultProfilePrivate;
struct _WpDefaultProfilePrivate
{
  WpState *state;
  WpProperties *profiles;
  GSource *timeout_source;

  WpObjectManager *devices_om;
};

G_DEFINE_TYPE_WITH_PRIVATE (WpDefaultProfile, wp_default_profile,
    WP_TYPE_PLUGIN)

static gint
find_device_profile (WpPipewireObject *device, const gchar *lookup_name)
{
  WpIterator *profiles = NULL;
  g_auto (GValue) item = G_VALUE_INIT;

  profiles = g_object_get_qdata (G_OBJECT (device), profiles_quark ());
  g_return_val_if_fail (profiles, -1);

  wp_iterator_reset (profiles);
  for (; wp_iterator_next (profiles, &item); g_value_unset (&item)) {
    WpSpaPod *pod = g_value_get_boxed (&item);
    gint index = 0;
    const gchar *name = NULL;

    /* Parse */
    if (!wp_spa_pod_get_object (pod, NULL,
        "index", "i", &index,
        "name", "s", &name,
        NULL))
      continue;

    if (g_strcmp0 (name, lookup_name) == 0) {
      g_value_unset (&item);
      return index;
    }
  }

  return -1;
}

static gboolean
timeout_save_callback (WpDefaultProfile *self)
{
  WpDefaultProfilePrivate *priv =
      wp_default_profile_get_instance_private (self);
  g_autoptr (GError) error = NULL;

  if (!wp_state_save (priv->state, priv->profiles, &error))
    wp_warning_object (self, "%s", error->message);

  return G_SOURCE_REMOVE;
}

static void
timeout_save_profiles (WpDefaultProfile *self, guint ms)
{
  WpDefaultProfilePrivate *priv =
      wp_default_profile_get_instance_private (self);
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));

  g_return_if_fail (core);
  g_return_if_fail (priv->profiles);

  /* Clear the current timeout callback */
  if (priv->timeout_source)
      g_source_destroy (priv->timeout_source);
  g_clear_pointer (&priv->timeout_source, g_source_unref);

  /* Add the timeout callback */
  wp_core_timeout_add_closure (core, &priv->timeout_source, ms,
      g_cclosure_new_object (G_CALLBACK (timeout_save_callback),
      G_OBJECT (self)));
}

static gchar *
wp_default_profile_get_profile (WpDefaultProfile *self,
    WpPipewireObject *device)
{
  WpDefaultProfilePrivate *priv =
      wp_default_profile_get_instance_private (self);
  const gchar *dev_name = NULL;
  const gchar *profile_name = NULL;

  g_return_val_if_fail (device, NULL);
  g_return_val_if_fail (priv->profiles, NULL);

  /* Get the device name */
  dev_name = wp_pipewire_object_get_property (device, PW_KEY_DEVICE_NAME);
  g_return_val_if_fail (dev_name, NULL);

  /* Get the profile */
  profile_name = wp_properties_get (priv->profiles, dev_name);
  return profile_name ? g_strdup (profile_name) : NULL;
}

static void
update_profile (WpDefaultProfile *self, WpPipewireObject *device,
    const gchar *new_profile)
{
  WpDefaultProfilePrivate *priv =
      wp_default_profile_get_instance_private (self);
  const gchar *dev_name, *curr_profile = NULL;
  gint index;

  g_return_if_fail (new_profile);
  g_return_if_fail (priv->profiles);

  /* Get the device name */
  dev_name = wp_pipewire_object_get_property (device, PW_KEY_DEVICE_NAME);
  g_return_if_fail (dev_name);

  /* Check if the new profile is the same as the current one */
  curr_profile = wp_properties_get (priv->profiles, dev_name);
  if (curr_profile && g_strcmp0 (curr_profile, new_profile) == 0)
    return;

  /* Make sure the profile is valid */
  index = find_device_profile (device, new_profile);
  if (index < 0) {
    wp_info_object (self, "profile '%s' (%d) is not valid on device '%s'",
        new_profile, index, dev_name);
    return;
  }

  /* Otherwise update the profile and add timeout save callback */
  wp_properties_set (priv->profiles, dev_name, new_profile);
  timeout_save_profiles (self, SAVE_INTERVAL_MS);

  wp_info_object (self, "updated profile '%s' (%d) on device '%s'", new_profile,
      index, dev_name);
}

static void
handle_profile (WpDefaultProfile *self, WpPipewireObject * device,
    WpIterator *profiles)
{
  g_auto (GValue) item = G_VALUE_INIT;

  for (; wp_iterator_next (profiles, &item); g_value_unset (&item)) {
    WpSpaPod *pod = g_value_get_boxed (&item);
    const gchar *name = NULL;
    gint index = 0;
    gboolean save = FALSE;

    if (!wp_spa_pod_get_object (pod, NULL,
        "index", "i", &index,
        "name", "s", &name,
        "save", "?b", &save,
        NULL))
      continue;

    if (save)
      update_profile (self, device, name);
  }
}

static void
on_device_params_changed (WpPipewireObject * proxy, const gchar *param_name,
    WpDefaultProfile *self)
{
  g_autoptr (WpIterator) profiles = NULL;

  if (g_strcmp0 (param_name, "Profile") == 0) {
    profiles = wp_pipewire_object_enum_params_sync (proxy, "Profile", NULL);
    if (profiles)
      handle_profile (self, proxy, profiles);
  } else if (g_strcmp0 (param_name, "EnumProfile") == 0) {
    profiles = wp_pipewire_object_enum_params_sync (proxy, "EnumProfile", NULL);
    if (profiles)
      g_object_set_qdata_full (G_OBJECT (proxy), profiles_quark (),
          g_steal_pointer (&profiles), (GDestroyNotify) wp_iterator_unref);
  }
}

static void
on_device_params_changed_hook (WpEvent *event, gpointer d)
{
  WpDefaultProfile *self = WP_DEFAULT_PROFILE (d);
  g_autoptr (GObject) subject = wp_event_get_subject (event);
  WpPipewireObject *proxy = WP_PIPEWIRE_OBJECT (subject);

  g_autoptr (WpProperties) p = wp_event_get_properties (event);
  const gchar *param = wp_properties_get (p, "event.subject.param-id");

  on_device_params_changed (proxy, param, self);
}

static void
on_device_added (WpEvent *event, gpointer d)
{
  WpDefaultProfile *self = WP_DEFAULT_PROFILE (d);
  g_autoptr (GObject) subject = wp_event_get_subject (event);
  WpPipewireObject *proxy = WP_PIPEWIRE_OBJECT (subject);

  on_device_params_changed (proxy, "EnumProfile", self);
}

static void
wp_default_profile_enable (WpPlugin * plugin, WpTransition * transition)
{
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (plugin));
  g_return_if_fail (core);
  WpDefaultProfile *self = WP_DEFAULT_PROFILE (plugin);
  WpDefaultProfilePrivate *priv =
      wp_default_profile_get_instance_private (self);
  g_autoptr (WpEventDispatcher) dispatcher =
      wp_event_dispatcher_get_instance (core);
  g_return_if_fail (dispatcher);
  g_autoptr (WpEventHook) hook = NULL;

  /* Create the devices object manager */
  priv->devices_om = wp_object_manager_new ();
  wp_object_manager_add_interest (priv->devices_om, WP_TYPE_DEVICE, NULL);
  wp_object_manager_request_object_features (priv->devices_om,
      WP_TYPE_DEVICE, WP_PIPEWIRE_OBJECT_FEATURES_ALL);
  wp_core_install_object_manager (core, priv->devices_om);

  wp_object_update_features (WP_OBJECT (self), WP_PLUGIN_FEATURE_ENABLED, 0);

  /* device added */
  hook = wp_simple_event_hook_new ("device-added@m-default-profile",
      NULL, NULL,
      g_cclosure_new ((GCallback) on_device_added, self, NULL));
  wp_interest_event_hook_add_interest (WP_INTEREST_EVENT_HOOK (hook),
      WP_CONSTRAINT_TYPE_PW_PROPERTY, "event.type", "=s", "device-added",
      NULL);
  wp_event_dispatcher_register_hook (dispatcher, hook);
  g_clear_object (&hook);

  /* device params changed */
  hook = wp_simple_event_hook_new ("device-parms-changed@m-default-profile",
      NULL, NULL,
      g_cclosure_new ((GCallback) on_device_params_changed_hook, self, NULL));
  wp_interest_event_hook_add_interest (WP_INTEREST_EVENT_HOOK (hook),
      WP_CONSTRAINT_TYPE_PW_PROPERTY, "event.type", "=s", "device-params-changed",
      WP_CONSTRAINT_TYPE_PW_PROPERTY, "event.subject.param-id", "=s", "EnumProfile",
      NULL);
  wp_interest_event_hook_add_interest (WP_INTEREST_EVENT_HOOK (hook),
    WP_CONSTRAINT_TYPE_PW_PROPERTY, "event.type", "=s", "device-params-changed",
    WP_CONSTRAINT_TYPE_PW_PROPERTY, "event.subject.param-id", "=s", "Profile",
    NULL);
  wp_event_dispatcher_register_hook (dispatcher, hook);
  g_clear_object (&hook);
}

static void
wp_default_profile_disable (WpPlugin * plugin)
{
  WpDefaultProfile *self = WP_DEFAULT_PROFILE (plugin);
  WpDefaultProfilePrivate *priv =
      wp_default_profile_get_instance_private (self);

  g_clear_object (&priv->devices_om);
}

static void
wp_default_profile_finalize (GObject * object)
{
  WpDefaultProfile *self = WP_DEFAULT_PROFILE (object);
  WpDefaultProfilePrivate *priv =
      wp_default_profile_get_instance_private (self);

  /* Clear the current timeout callback */
  if (priv->timeout_source)
      g_source_destroy (priv->timeout_source);
  g_clear_pointer (&priv->timeout_source, g_source_unref);

  g_clear_pointer (&priv->profiles, wp_properties_unref);
  g_clear_object (&priv->state);

  G_OBJECT_CLASS (wp_default_profile_parent_class)->finalize (object);
}

static void
wp_default_profile_init (WpDefaultProfile * self)
{
  WpDefaultProfilePrivate *priv =
      wp_default_profile_get_instance_private (self);

  priv->state = wp_state_new (STATE_NAME);

  /* Load the saved profiles */
  priv->profiles = wp_state_load (priv->state);
}

static void
wp_default_profile_class_init (WpDefaultProfileClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpPluginClass *plugin_class = (WpPluginClass *) klass;

  object_class->finalize = wp_default_profile_finalize;
  plugin_class->enable = wp_default_profile_enable;
  plugin_class->disable = wp_default_profile_disable;

  klass->get_profile = wp_default_profile_get_profile;

  /* Signals */
  signals[SIGNAL_GET_PROFILE] = g_signal_new ("get-profile",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET (WpDefaultProfileClass, get_profile), NULL, NULL,
      NULL, G_TYPE_STRING, 1, WP_TYPE_DEVICE);
}

WP_PLUGIN_EXPORT gboolean
wireplumber__module_init (WpCore * core, GVariant * args, GError ** error)
{
  wp_plugin_register (g_object_new (wp_default_profile_get_type (),
      "name", STATE_NAME,
      "core", core,
      NULL));
  return TRUE;
}
