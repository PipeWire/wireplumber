/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>
#include <pipewire/pipewire.h>
#include <spa/node/keys.h>
#include <spa/utils/names.h>

struct _WpJackDevice
{
  WpPlugin parent;
  WpDevice *jack_device;
};

G_DECLARE_FINAL_TYPE (WpJackDevice, wp_jack_device, WP, JACK_DEVICE, WpPlugin)
G_DEFINE_TYPE (WpJackDevice, wp_jack_device, WP_TYPE_PLUGIN)

static void
augment_done (GObject * proxy, GAsyncResult * res, gpointer user_data)
{
  WpJackDevice *self = WP_JACK_DEVICE (user_data);

  g_autoptr (GError) error = NULL;
  if (!wp_proxy_augment_finish (WP_PROXY (proxy), res, &error)) {
    wp_warning_object (self, "%s", error->message);
  }
}

static void
wp_jack_device_activate (WpPlugin * plugin)
{
  WpJackDevice *self = WP_JACK_DEVICE (plugin);
  g_autoptr (WpCore) core = wp_plugin_get_core (plugin);
  WpProperties *props = NULL;

  g_return_if_fail (core);

  /* Create the jack device props */
  props = wp_properties_new (
        SPA_KEY_FACTORY_NAME, SPA_NAME_API_JACK_DEVICE,
        SPA_KEY_NODE_NAME, "JACK-Device",
        NULL);

  /* Create the jack device */
  self->jack_device = wp_device_new_from_factory (core, "spa-device-factory",
      props);

  /* Augment */
  wp_proxy_augment (WP_PROXY (self->jack_device), WP_PROXY_FEATURES_STANDARD,
      NULL, augment_done, self);
}

static void
wp_jack_device_deactivate (WpPlugin * plugin)
{
  WpJackDevice *self = WP_JACK_DEVICE (plugin);

  g_clear_object (&self->jack_device);
}

static void
wp_jack_device_init (WpJackDevice * self)
{
}

static void
wp_jack_device_class_init (WpJackDeviceClass * klass)
{
  WpPluginClass *plugin_class = (WpPluginClass *) klass;

  plugin_class->activate = wp_jack_device_activate;
  plugin_class->deactivate = wp_jack_device_deactivate;
}

WP_PLUGIN_EXPORT void
wireplumber__module_init (WpModule * module, WpCore * core, GVariant * args)
{
  wp_plugin_register (g_object_new (wp_jack_device_get_type (),
      "module", module,
      NULL));
}
