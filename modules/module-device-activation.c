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

#include "module-device-activation/reserve-device.h"
#include "module-device-activation/reserve-node.h"

G_DEFINE_QUARK (wp-module-device-activation-reserve, reserve);

enum {
  PROP_0,
  PROP_MODE,
};

struct _WpDeviceActivation
{
  WpPlugin parent;

  /* Props */
  gchar *mode;

  WpObjectManager *spa_devices_om;
  WpObjectManager *nodes_om;
};

G_DECLARE_FINAL_TYPE (WpDeviceActivation, wp_device_activation, WP,
    DEVICE_ACTIVATION, WpPlugin)
G_DEFINE_TYPE (WpDeviceActivation, wp_device_activation, WP_TYPE_PLUGIN)

static void
on_node_state_changed (WpNode * node, WpNodeState old, WpNodeState curr,
    WpReserveNode * node_data)
{
  g_return_if_fail (node_data);

  switch (curr) {
  case WP_NODE_STATE_IDLE:
    /* Release reservation after 3 seconds */
    wp_reserve_node_timeout_release (node_data, 3000);
    break;
  case WP_NODE_STATE_RUNNING:
    /* Clear pending timeout if any and acquire reservation */
    wp_reserve_node_acquire (node_data);
    break;
  default:
    break;
  }
}

static void
add_reserve_node_data (WpDeviceActivation * self, WpProxy *node,
    WpProxy *device)
{
  WpReserveDevice *device_data = NULL;
  g_autoptr (WpReserveNode) node_data = NULL;

  /* Only add reservation data on nodes whose device has reservation data */
  device_data = g_object_get_qdata (G_OBJECT (device), reserve_quark ());
  if (!device_data)
    return;

  /* Create the node reservation data */
  node_data = wp_reserve_node_new (node, device_data);

  /* Set the reserve node data on the node */
  g_object_set_qdata_full (G_OBJECT (node), reserve_quark (), node_data,
      g_object_unref);

  /* Handle the info signal */
  g_signal_connect_object (WP_NODE (node), "state-changed",
      (GCallback) on_node_state_changed, node_data, 0);
}

static void
on_node_added (WpObjectManager *om, WpProxy *proxy, gpointer d)
{
  WpDeviceActivation *self = WP_DEVICE_ACTIVATION (d);
  const gchar *device_id = NULL;
  g_autoptr (WpProxy) device = NULL;

  /* Get the device associated with the node */
  device_id = wp_proxy_get_property (proxy, PW_KEY_DEVICE_ID);
  if (!device_id)
    return;
  device = wp_object_manager_lookup (self->spa_devices_om, WP_TYPE_DEVICE,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "bound-id", "=i", atoi (device_id), NULL);
  if (!device) {
    wp_warning_object (self, "cannot find device for node reservation data");
    return;
  }

  /* Add reserve data */
  add_reserve_node_data (self, proxy, device);
}

static void
add_reserve_device_data (WpDeviceActivation * self, WpProxy *device,
    gint card_id)
{
  g_autoptr (WpCore) core = wp_proxy_get_core (WP_PROXY (device));
  g_autoptr (WpProperties) props = wp_proxy_get_properties (device);
  const char *app_dev_name = NULL;
  g_autoptr (WpDbusDeviceReservation) reservation = NULL;
  g_autoptr (WpReserveDevice) device_data = NULL;

  app_dev_name = wp_properties_get (props, SPA_KEY_API_ALSA_PATH);

  /* Create the dbus device reservation */
  reservation = wp_dbus_device_reservation_new (card_id,
      PIPEWIRE_APPLICATION_NAME, 10, app_dev_name);

  /* Create the reserve device data */
  device_data = wp_reserve_device_new (device, reservation);

  /* Set the reserve device data on the device */
  g_object_set_qdata_full (G_OBJECT (device), reserve_quark (),
      g_steal_pointer (&device_data), g_object_unref);
}

static void
set_device_profile (WpProxy *device, gint index)
{
  g_return_if_fail (device);
  g_autoptr (WpSpaPod) profile = wp_spa_pod_new_object (
      "Profile", "Profile",
      "index", "i", index,
      NULL);
  wp_debug_object (device, "set profile %d", index);
  wp_proxy_set_param (device, "Profile", profile);
}

static void
on_device_enum_profile_done (WpProxy *proxy, GAsyncResult *res,
    WpDeviceActivation *self)
{
  g_autoptr (WpIterator) profiles = NULL;
  g_auto (GValue) item = G_VALUE_INIT;
  g_autoptr (GError) error = NULL;
  guint profile_index = 1;

  profiles = wp_proxy_enum_params_finish (proxy, res, &error);
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
        "ParamProfile", NULL,
        "index", "i", &index,
        "name", "s", &name,
        NULL)) {
      wp_warning_object (self, "bluetooth profile does not have index / name");
      continue;
    }

    /* TODO: for now we always use the first profile available */
    profile_index = index;
    break;
  }

  /* TODO: Currently, it seems that the bluetooth device allways returns an
   * empty list of profiles when doing EnumProfile, so for now we use a default
   * profile with index 1 if the list is empty. We should return an error
   * if none of them were found */
  set_device_profile (proxy, profile_index);
}

static void
on_device_added (WpObjectManager *om, WpProxy *proxy, gpointer d)
{
  WpDeviceActivation *self = WP_DEVICE_ACTIVATION (d);
  const gchar *device_api = wp_proxy_get_property (proxy, PW_KEY_DEVICE_API);
  g_return_if_fail (device_api);

  wp_debug_object (self, "device " WP_OBJECT_FORMAT " added, api '%s'",
      WP_OBJECT_ARGS (proxy), device_api);

  /* ALSA */
  if (g_str_has_prefix (device_api, "alsa")) {
    const gchar *id = wp_proxy_get_property (proxy, SPA_KEY_API_ALSA_CARD);
    /* If "dbus" mode and Id is valid, let dbus handle the activation,
     * otherwise always activate the device */
    if (self->mode && g_strcmp0 (self->mode, "dbus") == 0 && id) {
      add_reserve_device_data (self, proxy, atoi (id));
    } else {
      set_device_profile (proxy, 1);
    }
  }

  /* Bluez5 */
  else if (g_str_has_prefix (device_api, "bluez5")) {
    /* Enum available bluetooth profiles */
    wp_proxy_enum_params (WP_PROXY (proxy), "EnumProfile", NULL, NULL,
          (GAsyncReadyCallback) on_device_enum_profile_done, self);
  }

  /* Video */
  else if (g_str_has_prefix (device_api, "v4l2")) {
    /* No need to activate video devices */
  }
}

static void
activate_sync (WpCore *core, GAsyncResult *res, WpDeviceActivation *self)
{
  g_autoptr (GError) error = NULL;

  /* Check for errors */
  if (!wp_core_sync_finish (core, res, &error)) {
    wp_warning_object (self, "core sync error: %s", error->message);
    return;
  }

  /* Create the devices object manager and handle the device added signal */
  self->spa_devices_om = wp_object_manager_new ();
  wp_object_manager_add_interest (self->spa_devices_om, WP_TYPE_DEVICE,
      NULL);
  wp_object_manager_request_proxy_features (self->spa_devices_om,
      WP_TYPE_DEVICE, WP_PROXY_FEATURES_STANDARD);
  g_signal_connect_object (self->spa_devices_om, "object-added",
      G_CALLBACK (on_device_added), self, 0);
  wp_core_install_object_manager (core, self->spa_devices_om);

  /* Create the nodes object manager and handle the node added signal */
  self->nodes_om = wp_object_manager_new ();
  wp_object_manager_add_interest (self->nodes_om, WP_TYPE_NODE, NULL);
  wp_object_manager_request_proxy_features (self->nodes_om, WP_TYPE_NODE,
      WP_PROXY_FEATURES_STANDARD);
  g_signal_connect_object (self->nodes_om, "object-added",
      G_CALLBACK (on_node_added), self, 0);
  wp_core_install_object_manager (core, self->nodes_om);
}

static void
wp_device_activation_activate (WpPlugin * plugin)
{
  WpDeviceActivation *self = WP_DEVICE_ACTIVATION (plugin);
  g_autoptr (WpCore) core = wp_plugin_get_core (WP_PLUGIN (self));

  /* Sync to make sure jack device is exported before activating the plugin */
  wp_core_sync (core, NULL, (GAsyncReadyCallback) activate_sync, self);
}

static void
wp_device_activation_deactivate (WpPlugin * plugin)
{
  WpDeviceActivation *self = WP_DEVICE_ACTIVATION (plugin);

  g_clear_object (&self->nodes_om);
  g_clear_object (&self->spa_devices_om);
}

static void
wp_monitor_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpDeviceActivation *self = WP_DEVICE_ACTIVATION (object);

  switch (property_id) {
  case PROP_MODE:
    g_clear_pointer (&self->mode, g_free);
    self->mode = g_value_dup_string (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_monitor_get_property (GObject * object, guint property_id, GValue * value,
    GParamSpec * pspec)
{
  WpDeviceActivation *self = WP_DEVICE_ACTIVATION (object);

  switch (property_id) {
  case PROP_MODE:
    g_value_set_string (value, self->mode);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_monitor_finalize (GObject * object)
{
  WpDeviceActivation *self = WP_DEVICE_ACTIVATION (object);

  g_clear_pointer (&self->mode, g_free);

  G_OBJECT_CLASS (wp_device_activation_parent_class)->finalize (object);
}

static void
wp_device_activation_init (WpDeviceActivation * self)
{
}

static void
wp_device_activation_class_init (WpDeviceActivationClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpPluginClass *plugin_class = (WpPluginClass *) klass;

  object_class->finalize = wp_monitor_finalize;
  object_class->set_property = wp_monitor_set_property;
  object_class->get_property = wp_monitor_get_property;

  plugin_class->activate = wp_device_activation_activate;
  plugin_class->deactivate = wp_device_activation_deactivate;

  /* Properties */
  g_object_class_install_property (object_class, PROP_MODE,
      g_param_spec_string ("mode", "mode",
          "The mode to activate devices", NULL,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
}


WP_PLUGIN_EXPORT void
wireplumber__module_init (WpModule * module, WpCore * core, GVariant * args)
{
  const gchar *mode = NULL;

  /* Get the mode */
  g_variant_lookup (args, "mode", "s", &mode);

  wp_plugin_register (g_object_new (wp_device_activation_get_type (),
      "module", module,
      "mode", mode,
      NULL));
}
