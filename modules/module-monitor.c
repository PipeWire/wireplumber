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
#include <spa/utils/names.h>
#include <spa/monitor/device.h>
#include <spa/pod/builder.h>

typedef enum {
  FLAG_LOCAL_NODES = (1 << 0),
  FLAG_USE_ADAPTER = (1 << 1),
  FLAG_USE_ACP = (1 << 2),
} MonitorFlags;

static const struct {
  MonitorFlags flag;
  const gchar *name;
} flag_names[] = {
  { FLAG_LOCAL_NODES, "local-nodes" },
  { FLAG_USE_ADAPTER, "use-adapter" },
  { FLAG_USE_ACP, "use-acp" },
};

enum {
  PROP_0,
  PROP_LOCAL_CORE,
  PROP_FACTORY,
  PROP_FLAGS,
};

struct _WpMonitor
{
  WpPlugin parent;

  /* Props */
  WpCore *local_core;
  gchar *factory;
  MonitorFlags flags;

  GWeakRef dbus_reservation;
  WpObjectManager *plugins_om;
  WpSpaDevice *monitor;
};

G_DECLARE_FINAL_TYPE (WpMonitor, wp_monitor, WP, MONITOR, WpPlugin)
G_DEFINE_TYPE (WpMonitor, wp_monitor, WP_TYPE_PLUGIN)

static void on_create_object (WpSpaDevice * device,
    guint id, const gchar * type, const gchar * spa_factory,
    WpProperties * props, WpMonitor * self);

struct DeviceData {
  WpMonitor *self;
  WpSpaDevice *parent;
  guint id;
  gchar *spa_factory;
  WpProperties *props;
};

static void
device_data_free (gpointer data, GClosure *closure)
{
  struct DeviceData *dd = data;
  g_clear_object (&dd->parent);
  g_clear_pointer (&dd->props, wp_properties_unref);
  g_clear_pointer (&dd->spa_factory, g_free);
  g_slice_free (struct DeviceData, dd);
}

static void
setup_device_props (WpMonitor * self, WpProperties *p)
{
  const gchar *s, *d, *api;

  api = wp_properties_get (p, SPA_KEY_DEVICE_API);

  /* if alsa and ACP, set acp property to true */
  if (!g_strcmp0 (api, "alsa") && (self->flags & FLAG_USE_ACP))
    wp_properties_setf (p, "device.api.alsa.acp", "%d",
        self->flags & FLAG_USE_ACP ? TRUE : FALSE);

  /* set the device name if it's not already set */
  if (!wp_properties_get (p, SPA_KEY_DEVICE_NAME)) {
    if ((s = wp_properties_get (p, SPA_KEY_DEVICE_BUS_ID)) == NULL) {
      if ((s = wp_properties_get (p, SPA_KEY_DEVICE_BUS_PATH)) == NULL) {
        s = "unknown";
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
        "%s-analog%s%s", d, s ? "-" : "", s ? s : "");
  }
}

static void
setup_node_props (WpProperties *dev_props, WpProperties *node_props)
{
  const gchar *api, *devname, *description, *factory;

  /* get some strings that we are going to need below */
  api = wp_properties_get (dev_props, SPA_KEY_DEVICE_API);
  factory = wp_properties_get (node_props, SPA_KEY_FACTORY_NAME);

  devname = wp_properties_get (dev_props, SPA_KEY_DEVICE_NAME);
  if (G_UNLIKELY (!devname))
    devname = wp_properties_get (dev_props, SPA_KEY_DEVICE_NICK);
  if (G_UNLIKELY (!devname))
    devname = wp_properties_get (dev_props, SPA_KEY_DEVICE_ALIAS);
  if (G_UNLIKELY (!devname))
    devname = "unknown-device";

  description = wp_properties_get (dev_props, SPA_KEY_DEVICE_DESCRIPTION);
  if (!description)
    description = devname;

  /* set ALSA specific properties */
  if (!g_strcmp0 (api, "alsa:pcm")) {
    const gchar *pcm_name, *dev, *subdev, *stream;

    /* compose the node name */
    if (!(dev = wp_properties_get (node_props, SPA_KEY_API_ALSA_PCM_DEVICE)))
      dev = "0";
    if (!(subdev = wp_properties_get (node_props, SPA_KEY_API_ALSA_PCM_SUBDEVICE)))
      subdev = "0";
    if (!(stream = wp_properties_get (node_props, SPA_KEY_API_ALSA_PCM_STREAM)))
      stream = "unknown";

    wp_properties_setf (node_props, PW_KEY_NODE_NAME, "%s.%s.%s.%s",
        devname, stream, dev, subdev);

    /* compose the node description */
    pcm_name = wp_properties_get (node_props, SPA_KEY_API_ALSA_PCM_NAME);
    if (!pcm_name)
      pcm_name = wp_properties_get (node_props, SPA_KEY_API_ALSA_PCM_ID);

    if (g_strcmp0 (subdev, "0") != 0)
      wp_properties_setf (node_props, PW_KEY_NODE_DESCRIPTION, "%s (%s %s)",
          description, pcm_name, subdev);
    else
      wp_properties_setf (node_props, PW_KEY_NODE_DESCRIPTION, "%s (%s)",
          description, pcm_name);

  /* set BLUEZ 5 specific properties */
  } else if (!g_strcmp0 (api, "bluez5")) {
    const gchar *profile =
        wp_properties_get (node_props, SPA_KEY_API_BLUEZ5_PROFILE);

    /* compose the node name */
    wp_properties_setf (node_props, PW_KEY_NODE_NAME, "%s.%s.%s",
        factory, devname, profile);

    /* compose the node description */
    wp_properties_setf (node_props, PW_KEY_NODE_DESCRIPTION, "%s (%s)",
        description, profile);

    wp_properties_update_keys (node_props, dev_props,
        SPA_KEY_API_BLUEZ5_PATH,
        SPA_KEY_API_BLUEZ5_ADDRESS,
        NULL);

  /* set node properties for other APIs */
  } else {
    wp_properties_setf (node_props, PW_KEY_NODE_NAME, "%s.%s", factory, devname);
    wp_properties_set (node_props, PW_KEY_NODE_DESCRIPTION, description);
  }
}

static void
activate_done (WpObject * proxy, GAsyncResult * res, gpointer user_data)
{
  WpMonitor *self = user_data;

  g_autoptr (GError) error = NULL;
  if (!wp_object_activate_finish (proxy, res, &error)) {
    wp_warning_object (self, "%s", error->message);
  }
}

static void
create_node (WpMonitor * self, WpSpaDevice * parent, guint id,
    const gchar * spa_factory, WpProperties * props, WpProperties * dev_props)
{
  GObject *node = NULL;
  const gchar *pw_factory_name;

  wp_debug_object (self, WP_OBJECT_FORMAT " new node %u (%s)",
      WP_OBJECT_ARGS (parent), id, spa_factory);

  /* use the adapter instead of spa-node-factory if requested */
  pw_factory_name =
      (self->flags & FLAG_USE_ADAPTER) ? "adapter" : "spa-node-factory";

  props = wp_properties_copy (props);
  wp_properties_set (props, SPA_KEY_FACTORY_NAME, spa_factory);

  /* add device id property */
  {
    guint32 device_id = wp_proxy_get_bound_id (WP_PROXY (parent));
    wp_properties_setf (props, PW_KEY_DEVICE_ID, "%u", device_id);
  }

  setup_node_props (dev_props, props);

  /* create the node using the local core */
  node = (self->flags & FLAG_LOCAL_NODES) ?
      (GObject *) wp_impl_node_new_from_pw_factory (self->local_core,
          pw_factory_name, props) :
      (GObject *) wp_node_new_from_factory (self->local_core, pw_factory_name,
          props);
  if (!node)
    return;

  /* export to pipewire */
  if (WP_IS_IMPL_NODE (node))
    wp_impl_node_export (WP_IMPL_NODE (node));
  else
    wp_object_activate (WP_OBJECT (node), WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL,
        NULL, (GAsyncReadyCallback) activate_done, self);

  wp_spa_device_store_managed_object (parent, id, node);
}

static void
create_device (WpMonitor * self, WpSpaDevice * parent, guint id,
    const gchar * spa_factory, WpProperties * props)
{
  WpSpaDevice *device = NULL;
  const char *factory_name = NULL;

  g_return_if_fail (parent);
  g_return_if_fail (spa_factory);

  factory_name = self->flags & FLAG_USE_ACP ?
      SPA_NAME_API_ALSA_ACP_DEVICE : spa_factory;

  /* Create the device */
  device = wp_spa_device_new_from_spa_factory (self->local_core, factory_name,
      props);
  if (!device)
    return;

  /* Handle create-object singal */
  g_signal_connect (device, "create-object", (GCallback) on_create_object, self);

  /* Export the device */
  wp_object_activate (WP_OBJECT (device), WP_OBJECT_FEATURES_ALL,
      NULL, (GAsyncReadyCallback) activate_done, self);

  wp_spa_device_store_managed_object (parent, id, G_OBJECT (device));

  wp_debug_object (self, "device %p created", device);
}

static void
on_reservation_manage_device (GObject *obj, gboolean create, gpointer data)
{
  struct DeviceData *dd = data;
  g_return_if_fail (dd);

  if (create)
    create_device (dd->self, dd->parent, dd->id, dd->spa_factory,
        dd->props ? wp_properties_ref (dd->props) : NULL);
  else
    wp_spa_device_store_managed_object (dd->parent, dd->id, NULL);
}

static void
maybe_create_device (WpMonitor * self, WpSpaDevice * parent, guint id,
    const gchar * spa_factory, WpProperties * props)
{
  g_autoptr (WpPlugin) dr = g_weak_ref_get (&self->dbus_reservation);
  const gchar *card_id = NULL;

  wp_debug_object (self, "%s new device %u", self->factory, id);

  /* Create the properties */
  props = wp_properties_copy (props);
  setup_device_props (self, props);

  /* If dbus reservation API exists, let dbus manage the device, otherwise just
   * create it and never destroy it */
  card_id = wp_properties_get (props, SPA_KEY_API_ALSA_CARD);
  if (dr && card_id) {
    const gchar *appdev = wp_properties_get (props, SPA_KEY_API_ALSA_PATH);
    g_autoptr (GClosure) closure = NULL;
    struct DeviceData *dd = NULL;

    /* Create the closure */
    dd = g_slice_new0 (struct DeviceData);
    dd->self = self;
    dd->parent = g_object_ref (parent);
    dd->spa_factory = g_strdup (spa_factory);
    dd->props = props;

    /* Create the closure */
    closure = g_cclosure_new (G_CALLBACK (on_reservation_manage_device), dd,
        device_data_free);
    g_object_watch_closure (G_OBJECT (self), closure);

    g_signal_emit_by_name (dr, "create-reservation", atoi (card_id), appdev,
        closure);
  } else {
    create_device (self, parent, id, spa_factory, props);
  }
}

static void
on_create_object (WpSpaDevice * device,
    guint id, const gchar * type, const gchar * spa_factory,
    WpProperties * props, WpMonitor * self)
{
  if (!g_strcmp0 (type, "Device")) {
    maybe_create_device (self, device, id, spa_factory, props);
  } else if (!g_strcmp0 (type, "Node")) {
    g_autoptr (WpProperties) parent_props =
        wp_spa_device_get_properties (device);
    create_node (self, device, id, spa_factory, props, parent_props);
  } else {
    wp_debug_object (self, "%s got device create-object for unknown object "
        "type: %s", self->factory, type);
  }
}

static void
on_plugin_added (WpObjectManager *om, WpPlugin *plugin, gpointer d)
{
  WpMonitor *self = WP_MONITOR (d);
  g_autoptr (WpPlugin) dr = g_weak_ref_get (&self->dbus_reservation);

  if (dr)
    wp_warning_object (self, "skipping additional dbus reservation plugin");
  else
    g_weak_ref_set (&self->dbus_reservation, plugin);
}

static void
wp_monitor_activate (WpPlugin * plugin)
{
  WpMonitor *self = WP_MONITOR (plugin);
  g_autoptr (WpCore) core = wp_plugin_get_core (WP_PLUGIN (self));

  if (!wp_core_connect (self->local_core)) {
    wp_warning_object (plugin, "failed to connect monitor core");
    return;
  }

  /* Create the plugin object manager */
  self->plugins_om = wp_object_manager_new ();
  wp_object_manager_add_interest (self->plugins_om, WP_TYPE_PLUGIN,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "name", "=s", "dbus-reservation",
      NULL);
  g_signal_connect_object (self->plugins_om, "object-added",
      G_CALLBACK (on_plugin_added), self, 0);
  wp_core_install_object_manager (core, self->plugins_om);

  /* create the monitor and handle create-object callback */
  self->monitor = wp_spa_device_new_from_spa_factory (self->local_core,
      self->factory, NULL);
  g_signal_connect (self->monitor, "create-object",
      (GCallback) on_create_object, self);

  /* activate monitor */
  wp_object_activate (WP_OBJECT (self->monitor), WP_SPA_DEVICE_FEATURE_ENABLED,
      NULL, (GAsyncReadyCallback) activate_done, self);
}

static void
wp_monitor_deactivate (WpPlugin * plugin)
{
  WpMonitor *self = WP_MONITOR (plugin);

  g_clear_object (&self->monitor);
  g_clear_object (&self->plugins_om);
  g_weak_ref_clear (&self->dbus_reservation);
}

static void
wp_monitor_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpMonitor *self = WP_MONITOR (object);

  switch (property_id) {
  case PROP_LOCAL_CORE:
    g_clear_object (&self->local_core);
    self->local_core = g_value_dup_object (value);
    break;
  case PROP_FACTORY:
    g_clear_pointer (&self->factory, g_free);
    self->factory = g_value_dup_string (value);
    break;
  case PROP_FLAGS:
    self->flags = g_value_get_uint (value);
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
  WpMonitor *self = WP_MONITOR (object);

  switch (property_id) {
  case PROP_LOCAL_CORE:
    g_value_set_object (value, self->local_core);
    break;
  case PROP_FACTORY:
    g_value_set_string (value, self->factory);
    break;
  case PROP_FLAGS:
    g_value_set_uint (value, self->flags);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_monitor_finalize (GObject * object)
{
  WpMonitor *self = WP_MONITOR (object);

  g_clear_pointer (&self->factory, g_free);
  g_clear_object (&self->local_core);

  G_OBJECT_CLASS (wp_monitor_parent_class)->finalize (object);
}

static void
wp_monitor_init (WpMonitor * self)
{
}

static void
wp_monitor_class_init (WpMonitorClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpPluginClass *plugin_class = (WpPluginClass *) klass;

  object_class->finalize = wp_monitor_finalize;
  object_class->set_property = wp_monitor_set_property;
  object_class->get_property = wp_monitor_get_property;

  plugin_class->activate = wp_monitor_activate;
  plugin_class->deactivate = wp_monitor_deactivate;

  /* Properties */
  g_object_class_install_property (object_class, PROP_LOCAL_CORE,
      g_param_spec_object ("local-core", "local-core", "The local WpCore",
          WP_TYPE_CORE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class, PROP_FACTORY,
      g_param_spec_string ("factory", "factory",
          "The monitor factory name", NULL,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class, PROP_FLAGS,
      g_param_spec_uint ("flags", "flags",
          "The monitor flags", 0, G_MAXUINT, 0,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
}

WP_PLUGIN_EXPORT void
wireplumber__module_init (WpModule * module, WpCore * core, GVariant * args)
{
  GVariantIter iter;
  GVariant *value;
  const gchar *key;
  g_autoptr (WpCore) local_core = NULL;

  if (!args)
    return;

  /* All monitors will share a new core for local objects */
  local_core = wp_core_clone (core);
  wp_core_update_properties (local_core, wp_properties_new (
          PW_KEY_APP_NAME, "WirePlumber (monitor)",
          NULL));

  /* Register all monitors */
  g_variant_iter_init (&iter, args);
  while (g_variant_iter_next (&iter, "{&sv}", &key, &value)) {
    g_autofree char *plugin_name = NULL;
    const gchar *factory = NULL;
    MonitorFlags flags = 0;

    /* Get the factory */
    if (g_variant_lookup (value, "factory", "&s", &factory)) {
      GVariantIter *flags_iter;

      /* Get the flags */
      if (g_variant_lookup (value, "flags", "as", &flags_iter)) {
        const gchar *flag_str = NULL;
        while (g_variant_iter_loop (flags_iter, "&s", &flag_str)) {
          for (guint i = 0; i < SPA_N_ELEMENTS (flag_names); i++) {
            if (!g_strcmp0 (flag_str, flag_names[i].name))
              flags |= flag_names[i].flag;
          }
        }
        g_variant_iter_free (flags_iter);
      }

      /* Register */
      plugin_name = g_strdup_printf ("monitor-%s", factory);
      wp_plugin_register (g_object_new (wp_monitor_get_type (),
          "name", plugin_name,
          "module", module,
          "local-core", local_core,
          "factory", factory,
          "flags", flags,
          NULL));

    } else {
      wp_warning ("no 'factory' key specified for monitor '%s'", key);
    }

    g_variant_unref (value);
  }
}
