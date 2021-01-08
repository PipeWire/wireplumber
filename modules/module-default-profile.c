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

G_DECLARE_DERIVABLE_TYPE (WpDefaultProfile, wp_default_profile, WP,
    DEFAULT_PROFILE, WpPlugin)

struct _WpDefaultProfileClass
{
  WpPluginClass parent_class;

  void (*get_profile) (WpDefaultProfile *self, WpPipewireObject *device,
      const char **curr_profile);
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
    if (!wp_spa_pod_get_object (pod,
        "Profile", NULL,
        "index", "i", &index,
        "name", "s", &name,
        NULL)) {
      continue;
    }

    if (g_strcmp0 (name, lookup_name) == 0)
      return index;
  }

  return -1;
}

static gboolean
timeout_save_callback (WpDefaultProfile *self)
{
  WpDefaultProfilePrivate *priv =
      wp_default_profile_get_instance_private (self);

  if (!wp_state_save (priv->state, "group", priv->profiles))
    wp_warning_object (self, "could not save profiles");

  return G_SOURCE_REMOVE;
}

static void
timeout_save_profiles (WpDefaultProfile *self, guint ms)
{
  WpDefaultProfilePrivate *priv =
      wp_default_profile_get_instance_private (self);
  g_autoptr (WpCore) core = wp_plugin_get_core (WP_PLUGIN (self));

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

static void
wp_default_profile_get_profile (WpDefaultProfile *self,
    WpPipewireObject *device, const gchar **curr_profile)
{
  WpDefaultProfilePrivate *priv =
      wp_default_profile_get_instance_private (self);
  const gchar *dev_name = NULL;

  g_return_if_fail (device);
  g_return_if_fail (curr_profile);
  g_return_if_fail (priv->profiles);

  /* Get the device name */
  dev_name = wp_pipewire_object_get_property (device, PW_KEY_DEVICE_NAME);
  g_return_if_fail (dev_name);

  /* Get the profile */
  *curr_profile = wp_properties_get (priv->profiles, dev_name);
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
on_device_profile_notified (WpPipewireObject *device, GAsyncResult *res,
    WpDefaultProfile *self)
{
  g_autoptr (WpIterator) profiles = NULL;
  g_autoptr (GError) error = NULL;
  g_auto (GValue) item = G_VALUE_INIT;
  const gchar *name = NULL;
  gint index = 0;

  /* Finish */
  profiles = wp_pipewire_object_enum_params_finish (device, res, &error);
  if (error) {
    wp_warning_object (self, "failed to get current profile on device");
    return;
  }

  /* Ignore empty profile notifications */
  if (!wp_iterator_next (profiles, &item))
    return;

  /* Parse the profile */
  WpSpaPod *pod = g_value_get_boxed (&item);
  if (!wp_spa_pod_get_object (pod,
      "Profile", NULL,
      "index", "i", &index,
      "name", "s", &name,
      NULL)) {
    wp_warning_object (self, "failed to parse current profile");
    return;
  }

  g_value_unset (&item);

  /* Update the profile */
  update_profile (self, device, name);
}

static void
on_device_param_info_notified (WpPipewireObject * device, GParamSpec * param,
    WpDefaultProfile *self)
{
  /* Check the profile every time the params have changed */
  wp_pipewire_object_enum_params (device, "Profile", NULL, NULL,
      (GAsyncReadyCallback) on_device_profile_notified, self);
}

static void
on_device_enum_profile_done (WpPipewireObject *device, GAsyncResult *res,
    WpDefaultProfile *self)
{
  g_autoptr (WpIterator) profiles = NULL;
  g_autoptr (GError) error = NULL;

  /* Finish */
  profiles = wp_pipewire_object_enum_params_finish (device, res, &error);
  if (error) {
    wp_warning_object (self, "failed to enum profiles in device "
        WP_OBJECT_FORMAT, WP_OBJECT_ARGS (device));
    return;
  }

  /* Keep a reference of the profiles in the device object */
  g_object_set_qdata_full (G_OBJECT (device), profiles_quark (),
        g_steal_pointer (&profiles), (GDestroyNotify) wp_iterator_unref);

  /* Watch for param info changes */
  g_signal_connect_object (device, "notify::param-info",
      G_CALLBACK (on_device_param_info_notified), self, 0);
}

static void
on_device_added (WpObjectManager *om, WpPipewireObject *proxy, gpointer d)
{
  WpDefaultProfile *self = WP_DEFAULT_PROFILE (d);

  wp_debug_object (self, "device " WP_OBJECT_FORMAT " added",
      WP_OBJECT_ARGS (proxy));

  /* Enum available profiles */
  wp_pipewire_object_enum_params (proxy, "EnumProfile", NULL, NULL,
      (GAsyncReadyCallback) on_device_enum_profile_done, self);
}

static void
wp_default_profile_activate (WpPlugin * plugin)
{
  g_autoptr (WpCore) core = wp_plugin_get_core (plugin);
  WpDefaultProfile *self = WP_DEFAULT_PROFILE (plugin);
  WpDefaultProfilePrivate *priv =
      wp_default_profile_get_instance_private (self);

  /* Create the devices object manager */
  priv->devices_om = wp_object_manager_new ();
  wp_object_manager_add_interest (priv->devices_om, WP_TYPE_DEVICE, NULL);
  wp_object_manager_request_object_features (priv->devices_om,
      WP_TYPE_DEVICE, WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL);
  g_signal_connect_object (priv->devices_om, "object-added",
      G_CALLBACK (on_device_added), self, 0);
  wp_core_install_object_manager (core, priv->devices_om);
}

static void
wp_default_profile_deactivate (WpPlugin * plugin)
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
}

static void
wp_default_profile_init (WpDefaultProfile * self)
{
  WpDefaultProfilePrivate *priv =
      wp_default_profile_get_instance_private (self);

  priv->state = wp_state_new (STATE_NAME);

  /* Load the saved profiles */
  priv->profiles = wp_state_load (priv->state, "group");
  if (!priv->profiles) {
    wp_warning_object (self, "could not load profiles");
    return;
  }
}

static void
wp_default_profile_class_init (WpDefaultProfileClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpPluginClass *plugin_class = (WpPluginClass *) klass;

  object_class->finalize = wp_default_profile_finalize;
  plugin_class->activate = wp_default_profile_activate;
  plugin_class->deactivate = wp_default_profile_deactivate;

  klass->get_profile = wp_default_profile_get_profile;

  /* Signals */
  signals[SIGNAL_GET_PROFILE] = g_signal_new ("get-profile",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET (WpDefaultProfileClass, get_profile), NULL, NULL,
      NULL, G_TYPE_NONE, 2, WP_TYPE_DEVICE, G_TYPE_POINTER);
}

WP_PLUGIN_EXPORT void
wireplumber__module_init (WpModule * module, WpCore * core, GVariant * args)
{
  wp_plugin_register (g_object_new (wp_default_profile_get_type (),
      "name", STATE_NAME,
      "module", module,
      NULL));
}
