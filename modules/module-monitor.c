/* WirePlumber
 *
 * Copyright © 2019 Wim Taymans
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>

#include <pipewire/pipewire.h>
#include <spa/utils/keys.h>

static void
setup_device_props (WpMonitor *self, WpProperties *p, WpModule *module)
{
  const gchar *s, *d, *api;

  api = wp_properties_get (p, SPA_KEY_DEVICE_API);

  /* set the device name if it's not already set */
  if (!wp_properties_get (p, SPA_KEY_DEVICE_NAME)) {
    if ((s = wp_properties_get (p, SPA_KEY_DEVICE_BUS_ID)) == NULL) {
      if ((s = wp_properties_get (p, SPA_KEY_DEVICE_BUS_PATH)) == NULL) {
        s = wp_properties_get (p, WP_MONITOR_KEY_OBJECT_ID);
      }
    }

    if (!g_strcmp0 (api, "alsa")) {
      /* what we call a "device" in pipewire, in alsa it's a "card",
         so make it clear to avoid confusion */
      wp_properties_setf (p, PW_KEY_DEVICE_NAME, "alsa_card.%s", s);
    } else {
      wp_properties_setf (p, PW_KEY_DEVICE_NAME, "%s_device.%s", api, s);
    }
  }

  /* set the device description if it's not already set */
  if (!wp_properties_get (p, PW_KEY_DEVICE_DESCRIPTION)) {
    d = NULL;

    if (!g_strcmp0 (api, "alsa")) {
      if ((s = wp_properties_get (p, PW_KEY_DEVICE_FORM_FACTOR))
            && !g_strcmp0 (s, "internal"))
        d = "Built-in Audio";
      else if ((s = wp_properties_get (p, PW_KEY_DEVICE_CLASS))
            && !g_strcmp0 (s, "modem"))
        d = "Modem";
    }

    if (!d)
      d = wp_properties_get (p, PW_KEY_DEVICE_PRODUCT_NAME);
    if (!d)
      d = "Unknown device";

    wp_properties_set (p, PW_KEY_DEVICE_DESCRIPTION, d);
  }

  /* set the icon name for ALSA - TODO for other APIs */
  if (!wp_properties_get (p, PW_KEY_DEVICE_ICON_NAME)
        && !g_strcmp0 (api, "alsa"))
  {
    d = NULL;

    if ((s = wp_properties_get (p, PW_KEY_DEVICE_FORM_FACTOR))) {
      if (!g_strcmp0 (s, "microphone"))
        d = "audio-input-microphone";
      else if (!g_strcmp0 (s, "webcam"))
        d = "camera-web";
      else if (!g_strcmp0 (s, "computer"))
        d = "computer";
      else if (!g_strcmp0 (s, "handset"))
        d = "phone";
      else if (!g_strcmp0 (s, "portable"))
        d = "multimedia-player";
      else if (!g_strcmp0 (s, "tv"))
        d = "video-display";
      else if (!g_strcmp0 (s, "headset"))
        d = "audio-headset";
      else if (!g_strcmp0 (s, "headphone"))
        d = "audio-headphones";
      else if (!g_strcmp0 (s, "speaker"))
        d = "audio-speakers";
      else if (!g_strcmp0 (s, "hands-free"))
        d = "audio-handsfree";
    }
    if (!d)
      if ((s = wp_properties_get (p, PW_KEY_DEVICE_CLASS))
            && !g_strcmp0 (s, "modem"))
          d = "modem";

    if (!d)
      d = "audio-card";

    s = wp_properties_get (p, PW_KEY_DEVICE_BUS);

    wp_properties_setf (p, PW_KEY_DEVICE_ICON_NAME,
        "%s-analog%s%s", d, s ? "-" : "", s);
  }
}

static void
setup_node_props (WpMonitor *self, WpProperties *dev_props,
    WpProperties *node_props, WpModule *module)
{
  const gchar *api, *name, *description, *factory;

  /* Make the device properties directly available on the node */
  wp_properties_copy_keys (dev_props, node_props,
      SPA_KEY_DEVICE_API,
      SPA_KEY_DEVICE_NAME,
      SPA_KEY_DEVICE_ALIAS,
      SPA_KEY_DEVICE_NICK,
      SPA_KEY_DEVICE_DESCRIPTION,
      SPA_KEY_DEVICE_ICON,
      SPA_KEY_DEVICE_ICON_NAME,
      SPA_KEY_DEVICE_PLUGGED_USEC,
      SPA_KEY_DEVICE_BUS_ID,
      SPA_KEY_DEVICE_BUS_PATH,
      SPA_KEY_DEVICE_BUS,
      SPA_KEY_DEVICE_SUBSYSTEM,
      SPA_KEY_DEVICE_SYSFS_PATH,
      SPA_KEY_DEVICE_VENDOR_ID,
      SPA_KEY_DEVICE_VENDOR_NAME,
      SPA_KEY_DEVICE_PRODUCT_ID,
      SPA_KEY_DEVICE_PRODUCT_NAME,
      SPA_KEY_DEVICE_SERIAL,
      SPA_KEY_DEVICE_CLASS,
      SPA_KEY_DEVICE_CAPABILITIES,
      SPA_KEY_DEVICE_FORM_FACTOR,
      PW_KEY_DEVICE_INTENDED_ROLES,
      NULL);

  /* get some strings that we are going to need below */
  api = wp_properties_get (node_props, SPA_KEY_DEVICE_API);
  factory = wp_properties_get (node_props, PW_KEY_FACTORY_NAME);

  name = wp_properties_get (node_props, SPA_KEY_DEVICE_NAME);
  if (G_UNLIKELY (!name))
    name = wp_properties_get (node_props, SPA_KEY_DEVICE_NICK);
  if (G_UNLIKELY (!name))
    name = wp_properties_get (node_props, SPA_KEY_DEVICE_ALIAS);
  if (G_UNLIKELY (!name))
    name = "unknown-device";

  description = wp_properties_get (node_props, SPA_KEY_DEVICE_DESCRIPTION);
  if (!description)
    description = name;

  /* set ALSA specific properties */
  if (!g_strcmp0 (api, "alsa")) {
    const gchar *str;

    /* compose the node name */
    str = wp_properties_get (node_props, SPA_KEY_API_ALSA_PCM_ID);
    wp_properties_setf (node_props, PW_KEY_NODE_NAME, "%s/%s/%s",
        factory, name, str);

    /* compose the node description */
    str = wp_properties_get (node_props, SPA_KEY_API_ALSA_PCM_NAME);
    wp_properties_setf (node_props, PW_KEY_NODE_DESCRIPTION, "%s: %s",
        description, str);

    wp_properties_copy_keys (dev_props, node_props,
        SPA_KEY_API_ALSA_CARD,
        SPA_KEY_API_ALSA_CARD_ID,
        SPA_KEY_API_ALSA_CARD_COMPONENTS,
        SPA_KEY_API_ALSA_CARD_DRIVER,
        SPA_KEY_API_ALSA_CARD_NAME,
        SPA_KEY_API_ALSA_CARD_LONGNAME,
        SPA_KEY_API_ALSA_CARD_MIXERNAME,
        NULL);

  /* set BLUEZ 5 specific properties */
  } else if (!g_strcmp0 (api, "bluez5")) {
    const gchar *profile =
        wp_properties_get (node_props, SPA_KEY_API_BLUEZ5_PROFILE);

    /* compose the node name */
    wp_properties_setf (node_props, PW_KEY_NODE_NAME, "%s/%s/%s",
        factory, name, profile);

    /* compose the node description */
    wp_properties_setf (node_props, PW_KEY_NODE_DESCRIPTION, "%s (%s)",
        description, profile);

    wp_properties_copy_keys (dev_props, node_props,
        SPA_KEY_API_BLUEZ5_PATH,
        SPA_KEY_API_BLUEZ5_ADDRESS,
        NULL);

  /* set node properties for other APIs */
  } else {
    wp_properties_setf (node_props, PW_KEY_NODE_NAME, "%s/%s",
        factory, name);
    wp_properties_set (node_props, PW_KEY_NODE_DESCRIPTION, description);
  }
}

static void
start_monitor (WpMonitor *monitor)
{
  g_autoptr (GError) error = NULL;

  if (!wp_monitor_start (monitor, &error)) {
    g_message ("Failed to start monitor: %s", error->message);
  }
}

void
wireplumber__module_init (WpModule * module, WpCore * core, GVariant * args)
{
  WpMonitor *monitor;
  const gchar *factory = NULL;
  WpMonitorFlags flags = 0;
  GVariantIter *iter;

  if (!g_variant_lookup (args, "factory", "&s", &factory)) {
    g_message ("Failed to load monitor: no 'factory' key specified");
    return;
  }

  if (g_variant_lookup (args, "flags", "as", &iter)) {
    gchar *flag_str = NULL;
    GFlagsValue *flag_val = NULL;
    GFlagsClass *flag_class = g_type_class_ref (WP_TYPE_MONITOR_FLAGS);

    while (g_variant_iter_loop (iter, "s", &flag_str)) {
      flag_val = g_flags_get_value_by_nick (flag_class, flag_str);
      if (flag_val)
        flags |= flag_val->value;
    }
    g_variant_iter_free (iter);
  }

  monitor = wp_monitor_new (core, factory, NULL, flags);

  g_signal_connect (monitor, "setup-device-props",
      (GCallback) setup_device_props, module);
  g_signal_connect (monitor, "setup-node-props",
      (GCallback) setup_node_props, module);

  wp_module_set_destroy_callback (module, g_object_unref, monitor);

  /* Start the monitor when the connected callback is triggered */
  g_signal_connect_object (core, "remote-state-changed::connected",
      (GCallback) start_monitor, monitor, G_CONNECT_SWAPPED);
}
