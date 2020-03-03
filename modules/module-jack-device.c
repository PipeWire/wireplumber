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

struct module_data
{
  WpDevice *jack_device;
};

static void
augment_done (GObject * proxy, GAsyncResult * res, gpointer user_data)
{
  g_autoptr (GError) error = NULL;
  if (!wp_proxy_augment_finish (WP_PROXY (proxy), res, &error)) {
    g_warning ("%s", error->message);
  }
}

static void
create_jack_device (WpCore *core, struct module_data *data)
{
  WpProperties *props = NULL;
  g_return_if_fail (data);

  /* Create the jack device props */
  props = wp_properties_new (
        SPA_KEY_FACTORY_NAME, SPA_NAME_API_JACK_DEVICE,
        SPA_KEY_NODE_NAME, "JACK-Device",
        NULL);

  /* Create the jack device */
  data->jack_device = wp_device_new_from_factory (core, "spa-device-factory",
      props);

  /* Augment */
  wp_proxy_augment (WP_PROXY (data->jack_device), WP_PROXY_FEATURES_STANDARD,
      NULL, augment_done, NULL);
}

static void
module_destroy (gpointer d)
{
  struct module_data *data = d;

  g_clear_object (&data->jack_device);

  g_slice_free (struct module_data, data);
}

WP_PLUGIN_EXPORT void
wireplumber__module_init (WpModule * module, WpCore * core, GVariant * args)
{
  struct module_data *data = g_slice_new0 (struct module_data);
  wp_module_set_destroy_callback (module, module_destroy, data);

  /* Create the Jack Device when core is connected */
  g_signal_connect (core, "connected", (GCallback) create_jack_device, data);
}
