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
#include <spa/monitor/device.h>
#include <spa/pod/builder.h>

G_DEFINE_QUARK (wp-module-monitor-id, id);
G_DEFINE_QUARK (wp-module-monitor-children, children);

typedef enum {
  FLAG_LOCAL_NODES = (1 << 0),
  FLAG_USE_ADAPTER = (1 << 1),
} MonitorFlags;

static const struct {
  MonitorFlags flag;
  const gchar *name;
} flag_names[] = {
  { FLAG_LOCAL_NODES, "local-nodes" },
  { FLAG_USE_ADAPTER, "use-adapter" },
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

  WpSpaDevice *monitor;
};

G_DECLARE_FINAL_TYPE (WpMonitor, wp_monitor, WP, MONITOR, WpPlugin)
G_DEFINE_TYPE (WpMonitor, wp_monitor, WP_TYPE_PLUGIN)

static void on_object_info (WpSpaDevice * device,
    guint id, GType type, const gchar * spa_factory,
    WpProperties * props, WpProperties * parent_props,
    WpMonitor * self);

static void
setup_device_props (WpProperties *p)
{
  const gchar *s, *d, *api;

  api = wp_properties_get (p, SPA_KEY_DEVICE_API);

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
augment_done (GObject * proxy, GAsyncResult * res, gpointer user_data)
{
  WpMonitor *self = user_data;

  g_autoptr (GError) error = NULL;
  if (!wp_proxy_augment_finish (WP_PROXY (proxy), res, &error)) {
    wp_warning_object (self, "%s", error->message);
  }
}

static void
free_children (GList * children)
{
  g_list_free_full (children, g_object_unref);
}

static void
find_child (GObject * parent, guint32 id, GList ** children, GList ** link,
    GObject ** child)
{
  *children = g_object_steal_qdata (parent, children_quark ());

  /* Find the child */
  for (*link = *children; *link != NULL; *link = g_list_next (*link)) {
    *child = G_OBJECT ((*link)->data);
    guint32 child_id = GPOINTER_TO_UINT (g_object_get_qdata (*child, id_quark ()));
    if (id == child_id)
      break;
  }
}

static void
create_node (WpMonitor * self, WpSpaDevice * parent, GList ** children,
    guint id, const gchar * spa_factory, WpProperties * props,
    WpProperties * parent_props)
{
  GObject *node = NULL;
  const gchar *pw_factory_name;

  wp_debug_object (self, "%s new node %u (%s)", self->factory, id, spa_factory);

  /* use the adapter instead of spa-node-factory if requested */
  pw_factory_name =
      (self->flags & FLAG_USE_ADAPTER) ? "adapter" : "spa-node-factory";

  props = wp_properties_copy (props);
  wp_properties_set (props, SPA_KEY_FACTORY_NAME, spa_factory);

  /* add device id property */
  {
    guint32 device_id = wp_spa_device_get_bound_id (parent);
    wp_properties_setf (props, PW_KEY_DEVICE_ID, "%u", device_id);
  }

  setup_node_props (parent_props, props);

  /* create the node using the local core */
  node = (self->flags & FLAG_LOCAL_NODES) ?
      (GObject *) wp_impl_node_new_from_pw_factory (self->local_core,
          pw_factory_name, props) :
      (GObject *) wp_node_new_from_factory (self->local_core, pw_factory_name,
          props);
  if (!node)
    return;

  /* export to pipewire by requesting FEATURE_BOUND */
  if (WP_IS_IMPL_NODE (node))
    wp_impl_node_export (WP_IMPL_NODE (node));
  else
    wp_proxy_augment (WP_PROXY (node), WP_PROXY_FEATURES_STANDARD, NULL,
        augment_done, self);

  g_object_set_qdata (G_OBJECT (node), id_quark (), GUINT_TO_POINTER (id));
  *children = g_list_prepend (*children, node);
}

static void
device_created (GObject * device, GAsyncResult * res, gpointer user_data)
{
  WpMonitor * self = user_data;
  g_autoptr (GError) error = NULL;

  if (!wp_spa_device_export_finish (WP_SPA_DEVICE (device), res, &error)) {
    wp_warning_object (self, "%s", error->message);
    return;
  }

  wp_spa_device_activate (WP_SPA_DEVICE (device));
}

static void
create_device (WpMonitor * self, WpSpaDevice * parent, GList ** children,
    guint id, const gchar * spa_factory, WpProperties * props)
{
  WpSpaDevice *device;

  wp_debug_object (self, "%s new device %u", self->factory, id);

  props = wp_properties_copy (props);
  setup_device_props (props);

  if (!(device = wp_spa_device_new_from_spa_factory (self->local_core,
      spa_factory, props)))
    return;

  g_signal_connect (device, "object-info", (GCallback) on_object_info, self);

  wp_spa_device_export (device, NULL, device_created, self);

  g_object_set_qdata (G_OBJECT (device), id_quark (), GUINT_TO_POINTER (id));
  *children = g_list_prepend (*children, device);
}

static void
on_object_info (WpSpaDevice * device,
    guint id, GType type, const gchar * spa_factory,
    WpProperties * props, WpProperties * parent_props,
    WpMonitor * self)
{
  GList *children = NULL;
  GList *link = NULL;
  GObject *child = NULL;

  /* Find the child */
  find_child (G_OBJECT (device), id, &children, &link, &child);

  /* new object, construct... */
  if (type != G_TYPE_NONE && !link) {
    if (type == WP_TYPE_DEVICE) {
      create_device (self, device, &children, id, spa_factory, props);
    } else if (type == WP_TYPE_NODE) {
      create_node (self, device, &children, id, spa_factory, props,
          parent_props);
    } else {
      wp_debug_object (self, "%s got device object-info for unknown object "
          "type %s", self->factory, g_type_name (type));
    }
  }
  /* object removed, delete... */
  else if (type == G_TYPE_NONE && link) {
    g_object_unref (child);
    children = g_list_delete_link (children, link);
  }

  /* put back the children */
  g_object_set_qdata_full (G_OBJECT (device), children_quark (), children,
      (GDestroyNotify) free_children);
}

static void
wp_monitor_activate (WpPlugin * plugin)
{
  WpMonitor *self = WP_MONITOR (plugin);

  if (!wp_core_connect (self->local_core)) {
    wp_warning_object (plugin, "failed to connect monitor core");
    return;
  }

  /* create the monitor and handle onject-info callback */
  self->monitor = wp_spa_device_new_from_spa_factory (self->local_core,
      self->factory, NULL);
  g_signal_connect (self->monitor, "object-info", (GCallback) on_object_info,
      self);

  /* activate directly; exporting the monitor device is buggy */
  wp_spa_device_activate (self->monitor);
}

static void
wp_monitor_deactivate (WpPlugin * plugin)
{
  WpMonitor *self = WP_MONITOR (plugin);

  g_clear_object (&self->monitor);
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
          for (gint i = 0; i < SPA_N_ELEMENTS (flag_names); i++) {
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
