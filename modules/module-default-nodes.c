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
#define N_PREV_CONFIGS 16

/*
 * Module comes up with the default audio and video devices. It looks for
 * changes in user preference and the changes in devices(when new devices like
 * headsets, BT devices, HDMI etc are plugged in or removed). User preference
 * can be expressed via pavuctrl, gnome settings or metadata etc. These apps
 * typically update the default.configured.*(default-configured-nodes) keys.
 * Additionally the user preferences are remembered across reboots.
 */

/*
 * settings file: device.conf
 */

typedef struct _WpDefaultNode WpDefaultNode;
struct _WpDefaultNode
{
  gchar *value;
  gchar *config_value;
  gchar *prev_config_value[N_PREV_CONFIGS];
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
  gint save_interval_ms;
  gboolean use_persistent_storage;
  gboolean auto_echo_cancel;
  gchar *echo_cancel_names [2];
  WpSettings *settings;
  guintptr settings_sub_id;
};

G_DECLARE_FINAL_TYPE (WpDefaultNodes, wp_default_nodes,
                      WP, DEFAULT_NODES, WpPlugin)
G_DEFINE_TYPE (WpDefaultNodes, wp_default_nodes, WP_TYPE_PLUGIN)

static void
init_settings (WpDefaultNodes *self)
{
  self->save_interval_ms = DEFAULT_SAVE_INTERVAL_MS;
  self->use_persistent_storage = DEFAULT_USE_PERSISTENT_STORAGE;
  self->auto_echo_cancel = DEFAULT_AUTO_ECHO_CANCEL;
  self->echo_cancel_names [WP_DIRECTION_INPUT] =
    g_strdup (DEFAULT_ECHO_CANCEL_SOURCE_NAME);
  self->echo_cancel_names [WP_DIRECTION_OUTPUT] =
    g_strdup (DEFAULT_ECHO_CANCEL_SINK_NAME);
}

static void
wp_default_nodes_init (WpDefaultNodes * self)
{
  init_settings (self);
}

static void
update_prev_config_values (WpDefaultNode *def)
{
  gint pos = N_PREV_CONFIGS - 1;

  if (!def->config_value)
    return;

  /* Find if the current configured value is already in the stack */
  for (gint i = 0; i < N_PREV_CONFIGS; ++i) {
    if (!g_strcmp0(def->config_value, def->prev_config_value[i])) {
      pos = i;
      break;
    }
  }

  if (pos == 0)
    return;

  /* Insert on top position */
  g_clear_pointer (&def->prev_config_value[pos], g_free);

  for (gint i = pos; i > 0; --i)
    def->prev_config_value[i] = def->prev_config_value[i-1];

  def->prev_config_value[0] = g_strdup(def->config_value);
}

static void
load_state (WpDefaultNodes * self)
{
  g_autoptr (WpProperties) props = wp_state_load (self->state);
  for (gint i = 0; i < N_DEFAULT_NODES; i++) {
    const gchar *value = wp_properties_get (props, DEFAULT_CONFIG_KEY[i]);

    self->defaults[i].config_value = g_strdup (value);

    for (gint j = 0; j < N_PREV_CONFIGS; ++j) {
      g_autofree gchar *key = g_strdup_printf("%s.%d", DEFAULT_CONFIG_KEY[i], j);

      value = wp_properties_get (props, key);
      self->defaults[i].prev_config_value[j] = g_strdup(value);
    }
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

    for (gint j = 0; j < N_PREV_CONFIGS; ++j) {
      g_autofree gchar *key = g_strdup_printf("%s.%d", DEFAULT_CONFIG_KEY[i], j);

      wp_properties_set (props, key, self->defaults[i].prev_config_value[j]);
    }
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
    const WpDefaultNode *def, WpDirection direction, gint *priority)
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

      if (name && def->config_value && g_strcmp0 (name, def->config_value) == 0) {
        prio += 20000 * (N_PREV_CONFIGS + 1);
      } else if (name) {
        for (gint i = 0; i < N_PREV_CONFIGS; ++i) {
          if (!def->prev_config_value[i])
            continue;

          /* Match by name */
          if (g_strcmp0 (name, def->prev_config_value[i]) == 0) {
            prio += (N_PREV_CONFIGS - i) * 20000;
            break;
          }
        }
      }

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
    const gchar **media_classes, const WpDefaultNode *def, WpDirection direction)
{
  gint highest_prio = -1;
  WpNode *res = NULL;
  for (guint i = 0; media_classes[i]; i++) {
    gint prio = -1;
    WpNode *node = find_best_media_class_node (self, media_classes[i],
        def, direction, &prio);
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
  const WpDefaultNode *def = &self->defaults[node_t];

  switch (node_t) {
  case AUDIO_SINK: {
    const gchar *media_classes[] = {
        "Audio/Sink",
        "Audio/Duplex",
        NULL};
    return find_best_media_classes_node (self, media_classes, def,
        WP_DIRECTION_INPUT);
  }
  case AUDIO_SOURCE: {
    const gchar *media_classes[] = {
        "Audio/Source",
        "Audio/Source/Virtual",
        "Audio/Duplex",
        "Audio/Sink",
        NULL};
    return find_best_media_classes_node (self, media_classes, def,
        WP_DIRECTION_OUTPUT);
  }
  case VIDEO_SOURCE: {
    const gchar *media_classes[] = {
        "Video/Source",
        "Video/Source/Virtual",
        NULL};
    return find_best_media_classes_node (self, media_classes, def,
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
rescan (WpEvent *event, gpointer d)
{
  WpDefaultNodes *self = WP_DEFAULT_NODES (d);
  g_autoptr (WpMetadata) metadata = NULL;

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
on_metadata_changed (WpEvent *event, gpointer d)
{
  WpDefaultNodes * self = WP_DEFAULT_NODES (d);
  gint node_t = -1;
  g_autoptr (GObject) subject = wp_event_get_subject (event);
  WpMetadata *m = WP_METADATA (subject);

  g_autoptr (WpProperties) p = wp_event_get_properties (event);
  guint32 subject_id = atoi (wp_properties_get (p, "event.subject.id"));
  const gchar *key = wp_properties_get (p, "event.subject.key");
  const gchar *type = wp_properties_get (p, "event.subject.spa_type");
  const gchar *value = wp_properties_get (p, "event.subject.value");

  if (subject_id == 0) {
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

    update_prev_config_values (&self->defaults[node_t]);

    wp_debug_object (m, "changed '%s' -> '%s'", key,
        self->defaults[node_t].config_value);

    /* Save state after specific interval */
    timer_start (self);
  }
}

static void
on_metadata_added (WpEvent *event, gpointer d)
{
  WpDefaultNodes * self = WP_DEFAULT_NODES (d);
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));
  g_return_if_fail (core);
  g_autoptr (GObject) subject = wp_event_get_subject (event);
  WpMetadata *metadata = WP_METADATA (subject);

  for (gint i = 0; i < N_DEFAULT_NODES; i++) {
    if (self->defaults[i].config_value) {
      g_autoptr (WpSpaJson) json = wp_spa_json_new_object (
          "name", "s", self->defaults[i].config_value, NULL);
      wp_metadata_set (metadata, 0, DEFAULT_CONFIG_KEY[i], "Spa:String:JSON",
          wp_spa_json_get_data (json));
    }
  }
}

static void
get_settings (WpDefaultNodes *self, const gchar *setting)
{
  if (!setting || (g_str_equal ("device.save-interval-ms", setting)))
  {
    g_autoptr (WpSpaJson) j = wp_settings_get (self->settings,
      "device.save-interval-ms");
    if (j && !wp_spa_json_parse_int (j, &self->save_interval_ms))
      wp_warning ("Failed to parse integer in device.save-interval-ms");
  }

  if (!setting || (g_str_equal ("device.use-persistent-storage", setting)))
  {
    g_autoptr (WpSpaJson) j = wp_settings_get (self->settings,
      "device.use-persistent-storage");
    if (j && !wp_spa_json_parse_boolean (j, &self->use_persistent_storage))
      wp_warning ("Failed to parse boolean in device.use-persistent-storage");
  }

  if (!setting || (g_str_equal ("device.auto-echo-cancel", setting)))
  {
    g_autoptr (WpSpaJson) j = wp_settings_get (self->settings,
      "device.auto-echo-cancel");
    if (j && !wp_spa_json_parse_boolean (j, &self->auto_echo_cancel))
      wp_warning ("Failed to parse boolean in device.auto-echo-cancel");
  }

  if (!setting || (g_str_equal ("device.echo-cancel-sink-name", setting)))
  {
    g_autoptr (WpSpaJson) j = wp_settings_get (self->settings,
      "device.echo-cancel-sink-name");
    if (j) {
      gchar *value = wp_spa_json_parse_string (j);
      if (!value)
        wp_warning ("Failed to parse string in device.echo-cancel-sink-name");
      else {
        g_free (self->echo_cancel_names [WP_DIRECTION_OUTPUT]);
        self->echo_cancel_names [WP_DIRECTION_OUTPUT] = value;
      }
    }
  }

  if (!setting || (g_str_equal ("device.echo-cancel-source-name", setting)))
  {
    g_autoptr (WpSpaJson) j = wp_settings_get (self->settings,
      "device.echo-cancel-source-name");
    if (j) {
      gchar *value = wp_spa_json_parse_string (j);
      if (!value)
        wp_warning ("Failed to parse string in device.echo-cancel-source-name");
      else {
        g_free (self->echo_cancel_names [WP_DIRECTION_INPUT]);
        self->echo_cancel_names [WP_DIRECTION_INPUT] = value;
      }
    }
  }
}

void
wp_settings_changed_callback (WpSettings *obj, const gchar *setting,
  const gchar *raw_value, gpointer user_data)
{
  WpDefaultNodes *self = WP_DEFAULT_NODES (user_data);
  g_return_if_fail (self);
  g_return_if_fail (setting);

  get_settings (self, setting);
}

static void
wp_default_nodes_enable (WpPlugin * plugin, WpTransition * transition)
{
  WpDefaultNodes * self = WP_DEFAULT_NODES (plugin);
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (plugin));
  g_return_if_fail (core);
  g_autoptr (WpEventDispatcher) dispatcher =
      wp_event_dispatcher_get_instance (core);
  g_autoptr (WpEventHook) hook = NULL;
  g_return_if_fail (dispatcher);

  self->settings = wp_settings_get_instance (core, NULL);
  g_return_if_fail (self->settings);
  self->settings_sub_id = wp_settings_subscribe (self->settings, "device*",
      wp_settings_changed_callback, (gpointer) self);

  get_settings (self, NULL);

  /* default metadata added */
  hook = wp_simple_event_hook_new ("metadata-added@default-nodes",
      WP_EVENT_HOOK_DEFAULT_PRIORITY_DEFAULT_METADATA_ADDED_DEFAULT_NODES,
      WP_EVENT_HOOK_EXEC_TYPE_ON_EVENT,
      g_cclosure_new ((GCallback) on_metadata_added, self, NULL));
  wp_interest_event_hook_add_interest (WP_INTEREST_EVENT_HOOK (hook),
      WP_CONSTRAINT_TYPE_PW_PROPERTY, "event.type", "=s", "object-added",
      WP_CONSTRAINT_TYPE_PW_PROPERTY, "event.subject.type", "=s", "metadata",
      WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY, "metadata.name", "=s", "default",
      NULL);
  wp_event_dispatcher_register_hook (dispatcher, hook);
  g_clear_object(&hook);

  /* default metadata changed */
  hook = wp_simple_event_hook_new ("metadata-changed@default-nodes",
      WP_EVENT_HOOK_DEFAULT_PRIORITY_DEFAULT_METADATA_CHANGED_DEFAULT_NODES,
      WP_EVENT_HOOK_EXEC_TYPE_ON_EVENT,
      g_cclosure_new ((GCallback) on_metadata_changed, self, NULL));

  wp_interest_event_hook_add_interest (WP_INTEREST_EVENT_HOOK (hook),
    WP_CONSTRAINT_TYPE_PW_PROPERTY, "event.type", "=s", "object-changed",
    WP_CONSTRAINT_TYPE_PW_PROPERTY, "event.subject.type", "=s", "metadata",
    WP_CONSTRAINT_TYPE_PW_PROPERTY, "event.subject.key", "=s",
        "default.configured.audio.sink",
    WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY, "metadata.name", "=s", "default",
    NULL);

  wp_interest_event_hook_add_interest (WP_INTEREST_EVENT_HOOK (hook),
    WP_CONSTRAINT_TYPE_PW_PROPERTY, "event.type", "=s", "object-changed",
    WP_CONSTRAINT_TYPE_PW_PROPERTY, "event.subject.type", "=s", "metadata",
    WP_CONSTRAINT_TYPE_PW_PROPERTY, "event.subject.key", "=s",
        "default.configured.video.sink",
    WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY, "metadata.name", "=s", "default",
    NULL);

  wp_interest_event_hook_add_interest (WP_INTEREST_EVENT_HOOK (hook),
    WP_CONSTRAINT_TYPE_PW_PROPERTY, "event.type", "=s", "object-changed",
    WP_CONSTRAINT_TYPE_PW_PROPERTY, "event.subject.type", "=s", "metadata",
    WP_CONSTRAINT_TYPE_PW_PROPERTY, "event.subject.key", "=s",
        "default.configured.audio.source",
    WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY, "metadata.name", "=s", "default",
    NULL);

  wp_event_dispatcher_register_hook (dispatcher, hook);
  g_clear_object (&hook);

  /* register rescan hook as an after event */
  hook = wp_simple_event_hook_new("rescan@default-nodes",
      WP_EVENT_HOOK_DEFAULT_PRIORITY_RESCAN_DEFAULT_NODES,
      WP_EVENT_HOOK_EXEC_TYPE_AFTER_EVENTS,
    g_cclosure_new ((GCallback) rescan, self, NULL));

  /* default.configured.audio.sink changed */
  wp_interest_event_hook_add_interest (WP_INTEREST_EVENT_HOOK (hook),
    WP_CONSTRAINT_TYPE_PW_PROPERTY, "event.type", "=s", "object-changed",
    WP_CONSTRAINT_TYPE_PW_PROPERTY, "event.subject.type", "=s", "metadata",
    WP_CONSTRAINT_TYPE_PW_PROPERTY, "event.subject.key", "=s", "default.configured.audio.sink",
    WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY, "metadata.name", "=s", "default",
    NULL);

  /* default.configured.video.sink changed */
  wp_interest_event_hook_add_interest (WP_INTEREST_EVENT_HOOK (hook),
    WP_CONSTRAINT_TYPE_PW_PROPERTY, "event.type", "=s", "object-changed",
    WP_CONSTRAINT_TYPE_PW_PROPERTY, "event.subject.type", "=s", "metadata",
    WP_CONSTRAINT_TYPE_PW_PROPERTY, "event.subject.key", "=s", "default.configured.video.sink",
    WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY, "metadata.name", "=s", "default",
    NULL);

  /* default.configured.audio.source changed */
  wp_interest_event_hook_add_interest (WP_INTEREST_EVENT_HOOK (hook),
    WP_CONSTRAINT_TYPE_PW_PROPERTY, "event.type", "=s", "object-changed",
    WP_CONSTRAINT_TYPE_PW_PROPERTY, "event.subject.type", "=s", "metadata",
    WP_CONSTRAINT_TYPE_PW_PROPERTY, "event.subject.key", "=s", "default.configured.audio.source",
    WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY, "metadata.name", "=s", "default",
    NULL);

  /* new video device node added */
  wp_interest_event_hook_add_interest (WP_INTEREST_EVENT_HOOK (hook),
      WP_CONSTRAINT_TYPE_PW_PROPERTY, "event.type", "=s", "object-added",
      WP_CONSTRAINT_TYPE_PW_PROPERTY, "event.subject.type", "=s", "node",
      WP_CONSTRAINT_TYPE_PW_PROPERTY, "media.class", "#s", "Video/*",
      NULL);

  /* new audio device node added */
  wp_interest_event_hook_add_interest (WP_INTEREST_EVENT_HOOK (hook),
      WP_CONSTRAINT_TYPE_PW_PROPERTY, "event.type", "=s", "object-added",
      WP_CONSTRAINT_TYPE_PW_PROPERTY, "event.subject.type", "=s", "node",
      WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY, "media.class", "#s", "Audio/*",
      NULL);

  /* video device node removed */
  wp_interest_event_hook_add_interest (WP_INTEREST_EVENT_HOOK (hook),
      WP_CONSTRAINT_TYPE_PW_PROPERTY, "event.type", "=s", "object-removed",
      WP_CONSTRAINT_TYPE_PW_PROPERTY, "event.subject.type", "=s", "node",
      WP_CONSTRAINT_TYPE_PW_PROPERTY, "media.class", "#s", "Video/*",
      NULL);

  /* audio device node removed */
  wp_interest_event_hook_add_interest (WP_INTEREST_EVENT_HOOK (hook),
      WP_CONSTRAINT_TYPE_PW_PROPERTY, "event.type", "=s", "object-removed",
      WP_CONSTRAINT_TYPE_PW_PROPERTY, "event.subject.type", "=s", "node",
      WP_CONSTRAINT_TYPE_PW_PROPERTY, "media.class", "#s", "Audio/*",
      NULL);

  wp_event_dispatcher_register_hook (dispatcher, hook);
  g_clear_object (&hook);

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
  wp_core_install_object_manager (core, self->rescan_om);

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

    for (guint j = 0; j < N_PREV_CONFIGS; j++)
      g_clear_pointer (&self->defaults[i].prev_config_value[j], g_free);
  }

  g_clear_object (&self->metadata_om);
  g_clear_object (&self->rescan_om);
  g_clear_object (&self->state);
}


static void
wp_default_nodes_finalize (GObject * object)
{
  WpDefaultNodes * self = WP_DEFAULT_NODES (object);

  g_clear_pointer (&self->echo_cancel_names[WP_DIRECTION_INPUT], g_free);
  g_clear_pointer (&self->echo_cancel_names[WP_DIRECTION_OUTPUT], g_free);

  if (self->settings_sub_id)
    wp_settings_unsubscribe (self->settings, self->settings_sub_id);

  G_OBJECT_CLASS (wp_default_nodes_parent_class)->finalize (object);
}

static void
wp_default_nodes_class_init (WpDefaultNodesClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpPluginClass *plugin_class = (WpPluginClass *) klass;

  object_class->finalize = wp_default_nodes_finalize;

  plugin_class->enable = wp_default_nodes_enable;
  plugin_class->disable = wp_default_nodes_disable;
}

WP_PLUGIN_EXPORT gboolean
wireplumber__module_init (WpCore * core, GVariant * args, GError ** error)
{

  wp_plugin_register (g_object_new (wp_default_nodes_get_type (),
      "name", NAME,
      "core", core,
      NULL));

  return TRUE;
}


