/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>
#include <errno.h>
#include <pipewire/pipewire.h>
#include <pipewire/keys.h>

#include "module-default-nodes/common.h"

#define NAME "default-nodes"
#define DEFAULT_SAVE_INTERVAL_MS 1000
#define DEFAULT_USE_PERSISTENT_STORAGE TRUE
#define DEFAULT_AUTO_ECHO_CANCEL TRUE
#define DEFAULT_ECHO_CANCEL_SINK_NAME "echo-cancel-sink"
#define DEFAULT_ECHO_CANCEL_SOURCE_NAME "echo-cancel-source"

enum {
  PROP_0,
  PROP_SAVE_INTERVAL_MS,
  PROP_USE_PERSISTENT_STORAGE,
  PROP_AUTO_ECHO_CANCEL,
  PROP_ECHO_CANCEL_SINK_NAME,
  PROP_ECHO_CANCEL_SOURCE_NAME,
};

typedef struct _WpDefaultNode WpDefaultNode;
struct _WpDefaultNode
{
  gchar *value;
  gchar *config_value;
};

struct _WpDefaultNodes
{
  WpPlugin parent;

  WpState *state;
  WpDefaultNode defaults[N_DEFAULT_NODES];
  WpObjectManager *metadata_om;
  WpObjectManager *rescan_om;
  GSource *timeout_source;

  /* properties */
  guint save_interval_ms;
  gboolean use_persistent_storage;
  gboolean auto_echo_cancel;
  gchar *echo_cancel_names[2];
};

G_DECLARE_FINAL_TYPE (WpDefaultNodes, wp_default_nodes,
                      WP, DEFAULT_NODES, WpPlugin)
G_DEFINE_TYPE (WpDefaultNodes, wp_default_nodes, WP_TYPE_PLUGIN)

static void
wp_default_nodes_init (WpDefaultNodes * self)
{
}

static void
load_state (WpDefaultNodes * self)
{
  g_autoptr (WpProperties) props = wp_state_load (self->state);
  for (gint i = 0; i < N_DEFAULT_NODES; i++) {
    const gchar *value = wp_properties_get (props, DEFAULT_CONFIG_KEY[i]);
    self->defaults[i].config_value = g_strdup (value);
  }
}

static gboolean
timeout_save_state_callback (WpDefaultNodes *self)
{
  g_autoptr (WpProperties) props = wp_properties_new_empty ();
  g_autoptr (GError) error = NULL;

  for (gint i = 0; i < N_DEFAULT_NODES; i++) {
    if (self->defaults[i].config_value)
      wp_properties_set (props, DEFAULT_CONFIG_KEY[i],
          self->defaults[i].config_value);
  }

  if (!wp_state_save (self->state, props, &error))
    wp_warning_object (self, "%s", error->message);

  g_clear_pointer (&self->timeout_source, g_source_unref);
  return G_SOURCE_REMOVE;
}

static void
timer_start (WpDefaultNodes *self)
{
  if (!self->timeout_source && self->use_persistent_storage) {
    g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));
    g_return_if_fail (core);

    /* Add the timeout callback */
    wp_core_timeout_add_closure (core, &self->timeout_source,
        self->save_interval_ms, g_cclosure_new_object (
            G_CALLBACK (timeout_save_state_callback), G_OBJECT (self)));
  }
}

static gboolean
node_has_available_routes (WpDefaultNodes * self, WpNode *node)
{
  const gchar *dev_id_str = wp_pipewire_object_get_property (
          WP_PIPEWIRE_OBJECT (node), PW_KEY_DEVICE_ID);
  const gchar *cpd_str = wp_pipewire_object_get_property (
          WP_PIPEWIRE_OBJECT (node), "card.profile.device");
  gint dev_id = dev_id_str ? atoi (dev_id_str) : -1;
  gint cpd = cpd_str ? atoi (cpd_str) : -1;
  g_autoptr (WpDevice) device = NULL;
  gint found = 0;

  if (dev_id == -1 || cpd == -1)
    return TRUE;

  /* Get the device */
  device = wp_object_manager_lookup (self->rescan_om, WP_TYPE_DEVICE,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "bound-id", "=i", dev_id, NULL);
  if (!device)
    return TRUE;

  /* Check if the current device route supports the node card device profile */
  {
    g_autoptr (WpIterator) routes = NULL;
    g_auto (GValue) val = G_VALUE_INIT;
    routes = wp_pipewire_object_enum_params_sync (WP_PIPEWIRE_OBJECT (device),
        "Route", NULL);
    for (; wp_iterator_next (routes, &val); g_value_unset (&val)) {
      WpSpaPod *route = g_value_get_boxed (&val);
      gint route_device = -1;
      guint32 route_avail = SPA_PARAM_AVAILABILITY_unknown;

      if (!wp_spa_pod_get_object (route, NULL,
          "device", "i", &route_device,
          "available", "?I", &route_avail,
          NULL))
        continue;

      if (route_device != cpd)
        continue;

      if (route_avail == SPA_PARAM_AVAILABILITY_no)
        return FALSE;

      return TRUE;
    }
  }

  /* Check if available routes support the node card device profile */
  {
    g_autoptr (WpIterator) routes = NULL;
    g_auto (GValue) val = G_VALUE_INIT;
    routes = wp_pipewire_object_enum_params_sync (WP_PIPEWIRE_OBJECT (device),
        "EnumRoute", NULL);
    for (; wp_iterator_next (routes, &val); g_value_unset (&val)) {
      WpSpaPod *route = g_value_get_boxed (&val);
      guint32 route_avail = SPA_PARAM_AVAILABILITY_unknown;
      g_autoptr (WpSpaPod) route_devices = NULL;

      if (!wp_spa_pod_get_object (route, NULL,
          "available", "?I", &route_avail,
          "devices", "?P", &route_devices,
          NULL))
        continue;

      {
        g_autoptr (WpIterator) it = wp_spa_pod_new_iterator (route_devices);
        g_auto (GValue) v = G_VALUE_INIT;
        for (; wp_iterator_next (it, &v); g_value_unset (&v)) {
          gint32 *d = (gint32 *)g_value_get_pointer (&v);
          if (d && *d == cpd) {
            found++;
            if (route_avail != SPA_PARAM_AVAILABILITY_no)
              return TRUE;
          }
        }
      }
    }
  }
  /* The node is part of a profile without routes so we assume it
   * is available. This can happen for Pro Audio profiles */
  if (found == 0)
    return TRUE;

  return FALSE;
}

static gboolean
is_echo_cancel_node (WpDefaultNodes * self, WpNode *node, WpDirection direction)
{
  const gchar *name = wp_pipewire_object_get_property (
      WP_PIPEWIRE_OBJECT (node), PW_KEY_NODE_NAME);
  const gchar *virtual_str = wp_pipewire_object_get_property (
      WP_PIPEWIRE_OBJECT (node), PW_KEY_NODE_VIRTUAL);
  gboolean virtual = virtual_str && pw_properties_parse_bool (virtual_str);

  if (!name || !virtual)
    return FALSE;

  return g_strcmp0 (name, self->echo_cancel_names[direction]) == 0;
}

static WpNode *
find_best_media_class_node (WpDefaultNodes * self, const gchar *media_class,
    const gchar *node_name, WpDirection direction, gint *priority)
{
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) val = G_VALUE_INIT;
  gint highest_prio = 0;
  WpNode *res = NULL;

  g_return_val_if_fail (media_class, NULL);

  it = wp_object_manager_new_filtered_iterator (self->rescan_om, WP_TYPE_NODE,
      WP_CONSTRAINT_TYPE_PW_PROPERTY, PW_KEY_MEDIA_CLASS, "=s", media_class,
      NULL);

  for (; wp_iterator_next (it, &val); g_value_unset (&val)) {
    WpNode *node = g_value_get_object (&val);
    g_autoptr (WpPort) port = wp_object_manager_lookup (self->rescan_om,
          WP_TYPE_PORT, WP_CONSTRAINT_TYPE_PW_PROPERTY, PW_KEY_NODE_ID,
          "=u", wp_proxy_get_bound_id (WP_PROXY (node)),
          WP_CONSTRAINT_TYPE_PW_PROPERTY, PW_KEY_PORT_DIRECTION,
          "=s", direction == WP_DIRECTION_INPUT ? "in" : "out",
          NULL);
    if (port) {
      const gchar *name = wp_pipewire_object_get_property (
          WP_PIPEWIRE_OBJECT (node), PW_KEY_NODE_NAME);
      const gchar *prio_str = wp_pipewire_object_get_property (
          WP_PIPEWIRE_OBJECT (node), PW_KEY_PRIORITY_SESSION);
      gint prio = prio_str ? atoi (prio_str) : -1;

      if (!node_has_available_routes (self, node))
        continue;

      if (self->auto_echo_cancel && is_echo_cancel_node (self, node, direction))
        prio += 10000;

      if (name && node_name && g_strcmp0 (name, node_name) == 0)
        prio += 20000;

      if (prio > highest_prio || res == NULL) {
        highest_prio = prio;
        res = node;
      }
    }
  }

  if (priority)
    *priority = highest_prio;
  return res;
}

static WpNode *
find_best_media_classes_node (WpDefaultNodes * self,
    const gchar **media_classes, const gchar *node_name, WpDirection direction)
{
  gint highest_prio = -1;
  WpNode *res = NULL;
  for (guint i = 0; media_classes[i]; i++) {
    gint prio = -1;
    WpNode *node = find_best_media_class_node (self, media_classes[i],
        node_name, direction, &prio);
    if (node && (!res || prio > highest_prio)) {
      highest_prio = prio;
      res = node;
    }
  }
  return res;
}

static WpNode *
find_best_node (WpDefaultNodes * self, gint node_t)
{
  const gchar *name = self->defaults[node_t].config_value;

  switch (node_t) {
  case AUDIO_SINK: {
    const gchar *media_classes[] = {
        "Audio/Sink",
        "Audio/Duplex",
        NULL};
    return find_best_media_classes_node (self, media_classes, name,
        WP_DIRECTION_INPUT);
  }
  case AUDIO_SOURCE: {
    const gchar *media_classes[] = {
        "Audio/Source",
        "Audio/Source/Virtual",
        "Audio/Duplex",
        "Audio/Sink",
        NULL};
    return find_best_media_classes_node (self, media_classes, name,
        WP_DIRECTION_OUTPUT);
  }
  case VIDEO_SOURCE: {
    const gchar *media_classes[] = {
        "Video/Source",
        "Video/Source/Virtual",
        NULL};
    return find_best_media_classes_node (self, media_classes, name,
        WP_DIRECTION_OUTPUT);
  }
  default:
    break;
  }

  return NULL;
}

static void
reevaluate_default_node (WpDefaultNodes * self, WpMetadata *m, gint node_t)
{
  WpNode *node = NULL;
  const gchar *node_name = NULL;

  node = find_best_node (self, node_t);
  if (node)
    node_name = wp_pipewire_object_get_property (WP_PIPEWIRE_OBJECT (node),
        PW_KEY_NODE_NAME);

  /* store it in the metadata if it was changed */
  if (node && node_name &&
      g_strcmp0 (node_name, self->defaults[node_t].value) != 0)
  {
    g_autoptr (WpSpaJson) json = NULL;

    g_free (self->defaults[node_t].value);
    self->defaults[node_t].value = g_strdup (node_name);

    wp_info_object (self, "set default node for %s: %s",
        NODE_TYPE_STR[node_t], node_name);

    json = wp_spa_json_new_object ("name", "s", node_name, NULL);
    wp_metadata_set (m, 0, DEFAULT_KEY[node_t], "Spa:String:JSON",
        wp_spa_json_get_data (json));
  } else if (!node && self->defaults[node_t].value) {
    g_clear_pointer (&self->defaults[node_t].value, g_free);
    wp_info_object (self, "unset default node for %s", NODE_TYPE_STR[node_t]);
    wp_metadata_set (m, 0, DEFAULT_KEY[node_t], NULL, NULL);
  }
}

static void
sync_rescan (WpCore * core, GAsyncResult * res, WpDefaultNodes * self)
{
  g_autoptr (WpMetadata) metadata = NULL;
  g_autoptr (GError) error = NULL;

  if (!wp_core_sync_finish (core, res, &error)) {
    wp_warning_object (self, "core sync error: %s", error->message);
    return;
  }

  /* Get the metadata */
  metadata = wp_object_manager_lookup (self->metadata_om, WP_TYPE_METADATA,
      NULL);
  if (!metadata)
    return;

  wp_trace_object (self, "re-evaluating defaults");
  reevaluate_default_node (self, metadata, AUDIO_SINK);
  reevaluate_default_node (self, metadata, AUDIO_SOURCE);
  reevaluate_default_node (self, metadata, VIDEO_SOURCE);
}

static void
schedule_rescan (WpDefaultNodes * self)
{
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));
  g_return_if_fail (core);

  wp_debug_object (self, "scheduling default nodes rescan");
  wp_core_sync_closure (core, NULL, g_cclosure_new_object (
      G_CALLBACK (sync_rescan), G_OBJECT (self)));
}

static void
on_metadata_changed (WpMetadata *m, guint32 subject,
    const gchar *key, const gchar *type, const gchar *value, gpointer d)
{
  WpDefaultNodes * self = WP_DEFAULT_NODES (d);
  gint node_t = -1;

  if (subject == 0) {
    for (gint i = 0; i < N_DEFAULT_NODES; i++) {
      if (!g_strcmp0 (key, DEFAULT_CONFIG_KEY[i])) {
        node_t = i;
        break;
      }
    }
  }

  if (node_t != -1) {
    g_clear_pointer (&self->defaults[node_t].config_value, g_free);

    if (value && !g_strcmp0 (type, "Spa:String:JSON")) {
      g_autoptr (WpSpaJson) json = wp_spa_json_new_from_string (value);
      g_autofree gchar *name = NULL;
      if (wp_spa_json_object_get (json, "name", "s", &name, NULL))
        self->defaults[node_t].config_value = g_strdup (name);
    }

    wp_debug_object (m, "changed '%s' -> '%s'", key,
        self->defaults[node_t].config_value);

    /* schedule rescan */
    schedule_rescan (self);

    /* Save state after specific interval */
    timer_start (self);
  }
}

static void
on_object_added (WpObjectManager *om, WpPipewireObject *proxy, gpointer d)
{
  WpDefaultNodes * self = WP_DEFAULT_NODES (d);

  if (WP_IS_DEVICE (proxy)) {
    g_signal_connect_object (proxy, "params-changed",
        G_CALLBACK (schedule_rescan), self, G_CONNECT_SWAPPED);
  }
}

static void
on_metadata_added (WpObjectManager *om, WpMetadata *metadata, gpointer d)
{
  WpDefaultNodes * self = WP_DEFAULT_NODES (d);
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));
  g_return_if_fail (core);

  for (gint i = 0; i < N_DEFAULT_NODES; i++) {
    if (self->defaults[i].config_value) {
      g_autoptr (WpSpaJson) json = wp_spa_json_new_object (
          "name", "s", self->defaults[i].config_value, NULL);
      wp_metadata_set (metadata, 0, DEFAULT_CONFIG_KEY[i], "Spa:String:JSON",
          wp_spa_json_get_data (json));
    }
  }

  /* Handle the changed signal */
  g_signal_connect_object (metadata, "changed",
      G_CALLBACK (on_metadata_changed), self, 0);

  /* Create the rescan object manager */
  self->rescan_om = wp_object_manager_new ();
  wp_object_manager_add_interest (self->rescan_om, WP_TYPE_DEVICE, NULL);
  wp_object_manager_add_interest (self->rescan_om, WP_TYPE_NODE, NULL);
  wp_object_manager_add_interest (self->rescan_om, WP_TYPE_PORT, NULL);
  wp_object_manager_request_object_features (self->rescan_om, WP_TYPE_DEVICE,
      WP_OBJECT_FEATURES_ALL);
  wp_object_manager_request_object_features (self->rescan_om, WP_TYPE_NODE,
      WP_OBJECT_FEATURES_ALL);
  wp_object_manager_request_object_features (self->rescan_om, WP_TYPE_PORT,
      WP_OBJECT_FEATURES_ALL);
  g_signal_connect_object (self->rescan_om, "objects-changed",
      G_CALLBACK (schedule_rescan), self, G_CONNECT_SWAPPED);
  g_signal_connect_object (self->rescan_om, "object-added",
      G_CALLBACK (on_object_added), self, 0);
  wp_core_install_object_manager (core, self->rescan_om);
}

static void
wp_default_nodes_enable (WpPlugin * plugin, WpTransition * transition)
{
  WpDefaultNodes * self = WP_DEFAULT_NODES (plugin);
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (plugin));
  g_return_if_fail (core);

  if (self->use_persistent_storage) {
    self->state = wp_state_new (NAME);
    load_state (self);
  }

  /* Create the metadata object manager */
  self->metadata_om = wp_object_manager_new ();
  wp_object_manager_add_interest (self->metadata_om, WP_TYPE_METADATA,
      WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY, "metadata.name", "=s", "default",
      NULL);
  wp_object_manager_request_object_features (self->metadata_om,
      WP_TYPE_METADATA, WP_OBJECT_FEATURES_ALL);
  g_signal_connect_object (self->metadata_om, "object-added",
      G_CALLBACK (on_metadata_added), self, 0);
  wp_core_install_object_manager (core, self->metadata_om);

  wp_object_update_features (WP_OBJECT (self), WP_PLUGIN_FEATURE_ENABLED, 0);
}

static void
wp_default_nodes_disable (WpPlugin * plugin)
{
  WpDefaultNodes * self = WP_DEFAULT_NODES (plugin);

  /* Clear the current timeout callback */
  if (self->timeout_source)
      g_source_destroy (self->timeout_source);
  g_clear_pointer (&self->timeout_source, g_source_unref);

  for (guint i = 0; i < N_DEFAULT_NODES; i++) {
    g_clear_pointer (&self->defaults[i].value, g_free);
    g_clear_pointer (&self->defaults[i].config_value, g_free);
  }

  g_clear_object (&self->metadata_om);
  g_clear_object (&self->rescan_om);
  g_clear_object (&self->state);
}

static void
wp_default_nodes_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpDefaultNodes * self = WP_DEFAULT_NODES (object);

  switch (property_id) {
  case PROP_SAVE_INTERVAL_MS:
    self->save_interval_ms = g_value_get_uint (value);
    break;
  case PROP_USE_PERSISTENT_STORAGE:
    self->use_persistent_storage =  g_value_get_boolean (value);
    break;
  case PROP_AUTO_ECHO_CANCEL:
    self->auto_echo_cancel = g_value_get_boolean (value);
    break;
  case PROP_ECHO_CANCEL_SINK_NAME:
    g_clear_pointer (&self->echo_cancel_names[WP_DIRECTION_INPUT], g_free);
    self->echo_cancel_names[WP_DIRECTION_INPUT] = g_value_dup_string (value);
    break;
  case PROP_ECHO_CANCEL_SOURCE_NAME:
    g_clear_pointer (&self->echo_cancel_names[WP_DIRECTION_OUTPUT], g_free);
    self->echo_cancel_names[WP_DIRECTION_OUTPUT] = g_value_dup_string (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_default_nodes_finalize (GObject * object)
{
  WpDefaultNodes * self = WP_DEFAULT_NODES (object);

  g_clear_pointer (&self->echo_cancel_names[WP_DIRECTION_INPUT], g_free);
  g_clear_pointer (&self->echo_cancel_names[WP_DIRECTION_OUTPUT], g_free);

  G_OBJECT_CLASS (wp_default_nodes_parent_class)->finalize (object);
}

static void
wp_default_nodes_class_init (WpDefaultNodesClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpPluginClass *plugin_class = (WpPluginClass *) klass;

  object_class->finalize = wp_default_nodes_finalize;
  object_class->set_property = wp_default_nodes_set_property;

  plugin_class->enable = wp_default_nodes_enable;
  plugin_class->disable = wp_default_nodes_disable;

  g_object_class_install_property (object_class, PROP_SAVE_INTERVAL_MS,
      g_param_spec_uint ("save-interval-ms", "save-interval-ms",
          "save-interval-ms", 1, G_MAXUINT32, DEFAULT_SAVE_INTERVAL_MS,
          G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_USE_PERSISTENT_STORAGE,
      g_param_spec_boolean ("use-persistent-storage", "use-persistent-storage",
          "use-persistent-storage", DEFAULT_USE_PERSISTENT_STORAGE,
          G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_AUTO_ECHO_CANCEL,
      g_param_spec_boolean ("auto-echo-cancel", "auto-echo-cancel",
          "auto-echo-cancel", DEFAULT_AUTO_ECHO_CANCEL,
          G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_ECHO_CANCEL_SINK_NAME,
      g_param_spec_string ("echo-cancel-sink-name", "echo-cancel-sink-name",
          "echo-cancel-sink-name", DEFAULT_ECHO_CANCEL_SINK_NAME,
          G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_ECHO_CANCEL_SOURCE_NAME,
      g_param_spec_string ("echo-cancel-source-name", "echo-cancel-source-name",
          "echo-cancel-source-name", DEFAULT_ECHO_CANCEL_SOURCE_NAME,
          G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

WP_PLUGIN_EXPORT gboolean
wireplumber__module_init (WpCore * core, GVariant * args, GError ** error)
{
  guint save_interval_ms = DEFAULT_SAVE_INTERVAL_MS;
  gboolean use_persistent_storage = DEFAULT_USE_PERSISTENT_STORAGE;
  gboolean auto_echo_cancel = DEFAULT_AUTO_ECHO_CANCEL;
  const gchar *echo_cancel_sink_name = DEFAULT_ECHO_CANCEL_SINK_NAME;
  const gchar *echo_cancel_source_name = DEFAULT_ECHO_CANCEL_SOURCE_NAME;

  if (args) {
    g_variant_lookup (args, "save-interval-ms", "u", &save_interval_ms);
    g_variant_lookup (args, "use-persistent-storage", "b",
        &use_persistent_storage);
    g_variant_lookup (args, "auto-echo-cancel", "&s", &auto_echo_cancel);
    g_variant_lookup (args, "echo-cancel-sink-name", "&s",
        &echo_cancel_sink_name);
    g_variant_lookup (args, "echo-cancel-source-name", "&s",
        &echo_cancel_source_name);
  }

  wp_plugin_register (g_object_new (wp_default_nodes_get_type (),
          "name", NAME,
          "core", core,
          "save-interval-ms", save_interval_ms,
          "use-persistent-storage", use_persistent_storage,
          "auto-echo-cancel", auto_echo_cancel,
          "echo-cancel-sink-name", echo_cancel_sink_name,
          "echo-cancel-source-name", echo_cancel_source_name,
          NULL));
  return TRUE;
}
