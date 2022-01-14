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

#define COMPILING_MODULE_DEFAULT_NODES 1
#include "module-default-nodes/common.h"

#define NAME "default-nodes"
#define DEFAULT_SAVE_INTERVAL_MS 1000
#define DEFAULT_USE_PERSISTENT_STORAGE TRUE

enum {
  PROP_0,
  PROP_SAVE_INTERVAL_MS,
  PROP_USE_PERSISTENT_STORAGE,
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
    gint n_ports = direction == WP_DIRECTION_INPUT ?
        wp_node_get_n_input_ports (node, NULL) :
        wp_node_get_n_output_ports (node, NULL);
    if (n_ports > 0) {
      const gchar *name = wp_pipewire_object_get_property (
          WP_PIPEWIRE_OBJECT (node), PW_KEY_NODE_NAME);
      const gchar *prio_str = wp_pipewire_object_get_property (
          WP_PIPEWIRE_OBJECT (node), PW_KEY_PRIORITY_SESSION);
      gint prio = prio_str ? atoi (prio_str) : -1;

      if (!node_has_available_routes (self, node))
        continue;

      if (name && node_name && g_strcmp0 (name, node_name) == 0)
        prio += 10000;

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
  gchar buf[1024];

  node = find_best_node (self, node_t);
  if (node)
    node_name = wp_pipewire_object_get_property (WP_PIPEWIRE_OBJECT (node),
        PW_KEY_NODE_NAME);

  /* store it in the metadata if it was changed */
  if (node && node_name &&
      g_strcmp0 (node_name, self->defaults[node_t].value) != 0)
  {
    g_free (self->defaults[node_t].value);
    self->defaults[node_t].value = g_strdup (node_name);

    wp_info_object (self, "set default node for %s: %s",
        NODE_TYPE_STR[node_t], node_name);

    g_snprintf (buf, sizeof(buf), "{ \"name\": \"%s\" }", node_name);
    wp_metadata_set (m, 0, DEFAULT_KEY[node_t], "Spa:String:JSON", buf);
  } else if (!node && self->defaults[node_t].value) {
    g_clear_pointer (&self->defaults[node_t].value, g_free);
    wp_info_object (self, "unset default node for %s", NODE_TYPE_STR[node_t]);
    wp_metadata_set (m, 0, DEFAULT_KEY[node_t], NULL, NULL);
  }
}

static guint
get_device_total_nodes (WpPipewireObject * proxy)
{
  g_autoptr (WpIterator) profiles = NULL;
  g_auto (GValue) item = G_VALUE_INIT;

  profiles = wp_pipewire_object_enum_params_sync (proxy, "Profile", NULL);
  if (!profiles)
    return 0;

  for (; wp_iterator_next (profiles, &item); g_value_unset (&item)) {
    WpSpaPod *pod = g_value_get_boxed (&item);
    gint idx = -1;
    const gchar *name = NULL;
    g_autoptr (WpSpaPod) classes = NULL;

    /* Parse */
    if (!wp_spa_pod_get_object (pod, NULL,
        "index", "i", &idx,
        "name", "s", &name,
        "classes", "?P", &classes,
        NULL))
      continue;
    if (!classes)
      continue;

    /* Parse profile classes */
    {
      g_autoptr (WpIterator) it = wp_spa_pod_new_iterator (classes);
      g_auto (GValue) v = G_VALUE_INIT;
      gint total_nodes = 0;
      for (; wp_iterator_next (it, &v); g_value_unset (&v)) {
        WpSpaPod *entry = g_value_get_boxed (&v);
        g_autoptr (WpSpaPodParser) pp = NULL;
        const gchar *media_class = NULL;
        gint n_nodes = 0;
        g_return_val_if_fail (entry, 0);
        if (!wp_spa_pod_is_struct (entry))
          continue;
        pp = wp_spa_pod_parser_new_struct (entry);
        g_return_val_if_fail (pp, 0);
        g_return_val_if_fail (wp_spa_pod_parser_get_string (pp, &media_class), 0);
        g_return_val_if_fail (wp_spa_pod_parser_get_int (pp, &n_nodes), 0);
        wp_spa_pod_parser_end (pp);

        total_nodes += n_nodes;
      }

      if (total_nodes > 0)
        return total_nodes;
    }
  }

  return 0;
}

static gboolean
nodes_ready (WpDefaultNodes * self)
{
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) val = G_VALUE_INIT;

  /* Get the total number of nodes for each device and make sure they exist
   * and have at least 1 port */
  it = wp_object_manager_new_filtered_iterator (self->rescan_om,
      WP_TYPE_DEVICE, NULL);
  for (; wp_iterator_next (it, &val); g_value_unset (&val)) {
    WpPipewireObject *device = g_value_get_object (&val);
    guint total_nodes = get_device_total_nodes (device);
    if (total_nodes > 0) {
      guint32 device_id = wp_proxy_get_bound_id (WP_PROXY (device));
      g_autoptr (WpIterator) node_it = NULL;
      g_auto (GValue) node_val = G_VALUE_INIT;
      guint ready_nodes = 0;

      node_it = wp_object_manager_new_filtered_iterator (self->rescan_om,
          WP_TYPE_NODE, WP_CONSTRAINT_TYPE_PW_PROPERTY,
          PW_KEY_DEVICE_ID, "=i", device_id, NULL);
      for (; wp_iterator_next (node_it, &node_val); g_value_unset (&node_val)) {
        WpPipewireObject *node = g_value_get_object (&node_val);
        g_autoptr (WpPort) port =
              wp_object_manager_lookup (self->rescan_om,
              WP_TYPE_PORT, WP_CONSTRAINT_TYPE_PW_PROPERTY,
              PW_KEY_NODE_ID, "=u", wp_proxy_get_bound_id (WP_PROXY (node)),
              NULL);
        if (port)
          ready_nodes++;
      }

      if (ready_nodes < total_nodes) {
        const gchar *device_name = wp_pipewire_object_get_property (
            WP_PIPEWIRE_OBJECT (device), PW_KEY_DEVICE_NAME);
        wp_debug_object (self, "device '%s' is not ready (%d/%d)", device_name,
            ready_nodes, total_nodes);
        return FALSE;
      }
    }
  }

  /* Make sure Audio and Video virtual sources have ports */
  {
    g_autoptr (WpIterator) node_it = NULL;
    g_auto (GValue) node_val = G_VALUE_INIT;
    node_it = wp_object_manager_new_filtered_iterator (self->rescan_om,
        WP_TYPE_NODE, WP_CONSTRAINT_TYPE_PW_PROPERTY, PW_KEY_DEVICE_ID, "-",
        NULL);
    for (; wp_iterator_next (node_it, &node_val); g_value_unset (&node_val)) {
      WpPipewireObject *node = g_value_get_object (&node_val);
      const gchar *media_class = wp_pipewire_object_get_property (
          WP_PIPEWIRE_OBJECT (node), PW_KEY_MEDIA_CLASS);
      g_autoptr (WpPort) port =
          wp_object_manager_lookup (self->rescan_om,
          WP_TYPE_PORT, WP_CONSTRAINT_TYPE_PW_PROPERTY,
          PW_KEY_NODE_ID, "=u", wp_proxy_get_bound_id (WP_PROXY (node)),
          NULL);
      if (!port &&
          (g_strcmp0 ("Audio/Source/Virtual", media_class) == 0 ||
           g_strcmp0 ("Video/Source/Virtual", media_class) == 0)) {
        const gchar *node_name = wp_pipewire_object_get_property (
            WP_PIPEWIRE_OBJECT (node), PW_KEY_NODE_NAME);
        wp_debug_object (self, "virtual node '%s' is not ready", node_name);
        return FALSE;
      }
    }
  }

  return TRUE;
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

  /* Make sure nodes are ready for current profile */
  if (!nodes_ready (self))
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
  gchar name[1024];

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

    if (value && !g_strcmp0 (type, "Spa:String:JSON") &&
        json_object_find (value, "name", name, sizeof(name)) == 0)
    {
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
on_metadata_added (WpObjectManager *om, WpMetadata *metadata, gpointer d)
{
  WpDefaultNodes * self = WP_DEFAULT_NODES (d);
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));
  g_return_if_fail (core);

  for (gint i = 0; i < N_DEFAULT_NODES; i++) {
    gchar buf[1024];
    if (self->defaults[i].config_value) {
      g_snprintf (buf, sizeof(buf), "{ \"name\": \"%s\" }",
          self->defaults[i].config_value);
      wp_metadata_set (metadata, 0, DEFAULT_CONFIG_KEY[i], "Spa:String:JSON",
          buf);
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
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_default_nodes_class_init (WpDefaultNodesClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpPluginClass *plugin_class = (WpPluginClass *) klass;

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
}

WP_PLUGIN_EXPORT gboolean
wireplumber__module_init (WpCore * core, GVariant * args, GError ** error)
{
  guint save_interval_ms = DEFAULT_SAVE_INTERVAL_MS;
  gboolean use_persistent_storage = DEFAULT_USE_PERSISTENT_STORAGE;

  if (args) {
    g_variant_lookup (args, "save-interval-ms", "u", &save_interval_ms);
    g_variant_lookup (args, "use-persistent-storage", "b",
        &use_persistent_storage);
  }

  wp_plugin_register (g_object_new (wp_default_nodes_get_type (),
          "name", NAME,
          "core", core,
          "save-interval-ms", save_interval_ms,
          "use-persistent-storage", use_persistent_storage,
          NULL));
  return TRUE;
}
