/* WirePlumber
 *
 * Copyright © 2022 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>

/*
 * Module subscribes for certain object manager events to and pushes them as
 * events on to the Event Stack.
 */

enum {
  ACTION_GET_OBJECT_MANAGER,
  ACTION_PUSH_EVENT,
  ACTION_SCHEDULE_RESCAN,
  N_SIGNALS
};

typedef enum {
  OBJECT_TYPE_PORT,
  OBJECT_TYPE_LINK,
  OBJECT_TYPE_NODE,
  OBJECT_TYPE_SESSION_ITEM,
  OBJECT_TYPE_ENDPOINT,
  OBJECT_TYPE_CLIENT,
  OBJECT_TYPE_DEVICE,
  OBJECT_TYPE_METADATA,
  N_OBJECT_TYPES,
  OBJECT_TYPE_INVALID = N_OBJECT_TYPES
} ObjectType;

struct _WpStandardEventSource
{
  WpPlugin parent;
  WpObjectManager *oms[N_OBJECT_TYPES];
  WpEventHook *rescan_done_hook;
  gboolean rescan_scheduled;
  gint n_oms_installed;
};

static guint signals[N_SIGNALS] = {0};

G_DECLARE_FINAL_TYPE (WpStandardEventSource, wp_standard_event_source,
                      WP, STANDARD_EVENT_SOURCE, WpPlugin)
G_DEFINE_TYPE (WpStandardEventSource, wp_standard_event_source, WP_TYPE_PLUGIN)

static void
wp_standard_event_source_init (WpStandardEventSource * self)
{
}

static GType
object_type_to_gtype (ObjectType type)
{
  switch (type) {
    case OBJECT_TYPE_PORT: return WP_TYPE_PORT;
    case OBJECT_TYPE_LINK: return WP_TYPE_LINK;
    case OBJECT_TYPE_NODE: return WP_TYPE_NODE;
    case OBJECT_TYPE_SESSION_ITEM: return WP_TYPE_SESSION_ITEM;
    case OBJECT_TYPE_ENDPOINT: return WP_TYPE_ENDPOINT;
    case OBJECT_TYPE_CLIENT: return WP_TYPE_CLIENT;
    case OBJECT_TYPE_DEVICE: return WP_TYPE_DEVICE;
    case OBJECT_TYPE_METADATA: return WP_TYPE_METADATA;
    default:
      g_assert_not_reached ();
  }
}

static ObjectType
type_str_to_object_type (const gchar * type_str)
{
  if (!g_strcmp0 (type_str, "port"))
    return OBJECT_TYPE_PORT;
  else if (!g_strcmp0 (type_str, "link"))
    return OBJECT_TYPE_LINK;
  else if (!g_strcmp0 (type_str, "node"))
    return OBJECT_TYPE_NODE;
  else if (!g_strcmp0 (type_str, "session-item"))
    return OBJECT_TYPE_SESSION_ITEM;
  else if (!g_strcmp0 (type_str, "endpoint"))
    return OBJECT_TYPE_ENDPOINT;
  else if (!g_strcmp0 (type_str, "client"))
    return OBJECT_TYPE_CLIENT;
  else if (!g_strcmp0 (type_str, "device"))
    return OBJECT_TYPE_DEVICE;
  else if (!g_strcmp0 (type_str, "metadata"))
    return OBJECT_TYPE_METADATA;
  else
    return OBJECT_TYPE_INVALID;
}

static const gchar *
get_object_type (gpointer obj, WpProperties **properties)
{
  /* keeping these sorted by the frequency of events related to these objects */
  if (WP_IS_PORT (obj))
    return "port";
  else if (WP_IS_LINK (obj))
    return "link";
  else if (WP_IS_NODE (obj))
    return "node";
  else if (WP_IS_SESSION_ITEM (obj)) {
    if (*properties == NULL)
      *properties = wp_properties_new_empty();

    if (WP_IS_SI_LINKABLE (obj)) {
      wp_properties_set (*properties,
          "event.session-item.interface", "linkable");
    } else if (WP_IS_SI_LINK (obj)) {
      wp_properties_set (*properties,
          "event.session-item.interface", "link");
    }
    return "session-item";
  }
  else if (WP_IS_ENDPOINT (obj))
    return "endpoint";
  else if (WP_IS_CLIENT (obj))
    return "client";
  else if (WP_IS_DEVICE (obj))
    return "device";
  else if (WP_IS_METADATA (obj))
    return "metadata";

  wp_debug_object (obj, "Unknown global proxy type");
  return G_OBJECT_TYPE_NAME (obj);
}

static gint
get_default_event_priority (const gchar *event_type)
{
  if (!g_strcmp0 (event_type, "find-target-si-and-link"))
    return 500;
  else if (!g_strcmp0 (event_type, "rescan-session"))
    return -500;
  else if (!g_strcmp0 (event_type, "node-state-changed"))
    return 50;
  else if (!g_strcmp0 (event_type, "metadata-changed"))
    return 50;
  else if (g_str_has_suffix (event_type, "-params-changed"))
    return 50;
  else if (g_str_has_prefix (event_type, "client-"))
    return 200;
  else if (g_str_has_prefix (event_type, "device-"))
    return 170;
  else if (g_str_has_prefix (event_type, "port-"))
    return 150;
  else if (g_str_has_prefix (event_type, "node-"))
    return 130;
  else if (g_str_has_prefix (event_type, "session-item-"))
    return 110;
  else if (g_str_has_suffix (event_type, "-added") ||
           g_str_has_suffix (event_type, "-removed"))
    return 20;

  wp_debug ("Unknown event type: %s, using priority 0", event_type);
  return 0;
}

static WpObjectManager *
wp_standard_event_get_object_manager (WpStandardEventSource *self,
    const gchar * type_str)
{
  ObjectType type = type_str_to_object_type (type_str);

  if (G_UNLIKELY (type == OBJECT_TYPE_INVALID)) {
    wp_critical_object (self, "object type '%s' is not valid", type_str);
    return NULL;
  }

  g_return_val_if_fail (self->oms[type], NULL);
  return g_object_ref (self->oms[type]);
}

static void
wp_standard_event_source_push_event (WpStandardEventSource *self,
    const gchar *event_type, gpointer subject, WpProperties *misc_properties)
{
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));
  g_return_if_fail (core);
  g_autoptr (WpEventDispatcher) dispatcher =
      wp_event_dispatcher_get_instance (core);
  g_return_if_fail (dispatcher);
  g_autoptr (WpProperties) properties = wp_properties_new_empty ();
  g_autofree gchar *full_event_type = NULL;

  const gchar *subject_type =
      subject ? get_object_type (subject, &properties) : NULL;

  if (subject_type) {
    wp_properties_set (properties, "event.subject.type", subject_type);
    full_event_type = g_strdup_printf ("%s-%s", subject_type, event_type);
    event_type = full_event_type;
  }
  if (misc_properties)
    wp_properties_add (properties, misc_properties);

  gint priority = get_default_event_priority (event_type);

  wp_debug_object (self,
      "pushing event '%s', prio %d, subject " WP_OBJECT_FORMAT " (%s)",
      event_type, priority, WP_OBJECT_ARGS (subject), subject_type);

  wp_event_dispatcher_push_event (dispatcher, wp_event_new (
      event_type, priority, g_steal_pointer (&properties),
      G_OBJECT (self), G_OBJECT (subject)));
}

static void
wp_standard_event_source_schedule_rescan (WpStandardEventSource *self)
{
  if (!self->rescan_scheduled) {
    wp_standard_event_source_push_event (self, "rescan-session", NULL, NULL);
    self->rescan_scheduled = TRUE;
  }
}

static void
on_metadata_changed (WpMetadata *obj, guint32 subject,
    const gchar *key, const gchar *spa_type, const gchar *value,
    WpStandardEventSource *self)
{
  g_autoptr (WpProperties) properties = wp_properties_new_empty ();
  wp_properties_setf (properties, "event.subject.id", "%u", subject);
  wp_properties_set (properties, "event.subject.key", key);
  wp_properties_set (properties, "event.subject.spa_type", spa_type);
  wp_properties_set (properties, "event.subject.value", value);

  wp_standard_event_source_push_event (self, "changed", obj, properties);
}

static void
on_params_changed (WpPipewireObject *obj, const gchar *id,
    WpStandardEventSource *self)
{
  g_autoptr (WpProperties) properties = wp_properties_new_empty ();
  wp_properties_set (properties, "event.subject.param-id", id);

  wp_standard_event_source_push_event (self, "params-changed", obj, properties);
}

static void
on_node_state_changed (WpNode *obj, WpNodeState old_state,
    WpNodeState new_state, WpStandardEventSource *self)
{
  g_autoptr (WpProperties) properties = wp_properties_new (
      "event.subject.old-state", g_enum_to_string (WP_TYPE_NODE_STATE, old_state),
      "event.subject.new-state", g_enum_to_string (WP_TYPE_NODE_STATE, new_state),
      NULL);
  wp_standard_event_source_push_event (self, "state-changed", obj,
      properties);
}

static void
on_object_added (WpObjectManager *om, WpObject *obj, WpStandardEventSource *self)
{
  wp_standard_event_source_push_event (self, "added", obj, NULL);

  if (WP_IS_PIPEWIRE_OBJECT (obj)) {
    g_signal_connect_object (obj, "params-changed",
        G_CALLBACK (on_params_changed), self, 0);
  }

  if (WP_IS_NODE (obj)) {
    g_signal_connect_object (obj, "state-changed",
        G_CALLBACK (on_node_state_changed), self, 0);
  }
  else if (WP_IS_METADATA (obj)) {
    g_signal_connect_object (obj, "changed",
        G_CALLBACK (on_metadata_changed), self, 0);
  }
}

static void
on_object_removed (WpObjectManager *om, WpObject *obj, WpStandardEventSource *self)
{
  wp_standard_event_source_push_event (self, "removed", obj, NULL);
}

static void
on_om_installed (WpObjectManager * om, WpStandardEventSource * self)
{
  if (++self->n_oms_installed == N_OBJECT_TYPES)
    wp_object_update_features (WP_OBJECT (self), WP_PLUGIN_FEATURE_ENABLED, 0);
}

static void
on_rescan_done (WpEvent * event, WpStandardEventSource * self)
{
  self->rescan_scheduled = FALSE;
}

static void
wp_standard_event_source_enable (WpPlugin * plugin, WpTransition * transition)
{
  WpStandardEventSource * self = WP_STANDARD_EVENT_SOURCE (plugin);
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (plugin));
  g_return_if_fail (core);
  g_autoptr (WpEventDispatcher) dispatcher =
      wp_event_dispatcher_get_instance (core);
  g_return_if_fail (dispatcher);

  /* install object managers */
  self->n_oms_installed = 0;
  for (gint i = 0; i < N_OBJECT_TYPES; i++) {
    GType gtype = object_type_to_gtype (i);
    self->oms[i] = wp_object_manager_new ();
    wp_object_manager_add_interest (self->oms[i], gtype, NULL);
    wp_object_manager_request_object_features (self->oms[i],
        gtype, WP_OBJECT_FEATURES_ALL);
    g_signal_connect_object (self->oms[i], "object-added",
        G_CALLBACK (on_object_added), self, 0);
    g_signal_connect_object (self->oms[i], "object-removed",
        G_CALLBACK (on_object_removed), self, 0);
    g_signal_connect_object (self->oms[i], "installed",
        G_CALLBACK (on_om_installed), self, 0);
    wp_core_install_object_manager (core, self->oms[i]);
  }

  /* install hook to restore the rescan_scheduled state after rescanning */
  self->rescan_done_hook = wp_simple_event_hook_new (
      "rescan-done@std-event-source",
      WP_EVENT_HOOK_PRIORITY_LOWEST, WP_EVENT_HOOK_EXEC_TYPE_ON_EVENT,
      g_cclosure_new_object ((GCallback) on_rescan_done, G_OBJECT (self)));
  wp_interest_event_hook_add_interest (
      WP_INTEREST_EVENT_HOOK (self->rescan_done_hook),
      WP_CONSTRAINT_TYPE_PW_PROPERTY, "event.type", "=s", "rescan-session",
      NULL);
  wp_event_dispatcher_register_hook (dispatcher, self->rescan_done_hook);
}

static void
wp_standard_event_source_disable (WpPlugin * plugin)
{
  WpStandardEventSource * self = WP_STANDARD_EVENT_SOURCE (plugin);
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (plugin));
  g_return_if_fail (core);
  g_autoptr (WpEventDispatcher) dispatcher =
      wp_event_dispatcher_get_instance (core);

  for (gint i = 0; i < N_OBJECT_TYPES; i++)
    g_clear_object (&self->oms[i]);

  if (dispatcher)
    wp_event_dispatcher_unregister_hook (dispatcher, self->rescan_done_hook);
  g_clear_object (&self->rescan_done_hook);
}

static void
wp_standard_event_source_class_init (WpStandardEventSourceClass * klass)
{
  WpPluginClass *plugin_class = (WpPluginClass *) klass;

  plugin_class->enable = wp_standard_event_source_enable;
  plugin_class->disable = wp_standard_event_source_disable;

  signals[ACTION_GET_OBJECT_MANAGER] = g_signal_new_class_handler (
      "get-object-manager", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      (GCallback) wp_standard_event_get_object_manager,
      NULL, NULL, NULL, WP_TYPE_OBJECT_MANAGER, 1, G_TYPE_STRING);

  signals[ACTION_PUSH_EVENT] = g_signal_new_class_handler (
      "push-event", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      (GCallback) wp_standard_event_source_push_event,
      NULL, NULL, NULL, G_TYPE_NONE, 3,
      G_TYPE_STRING, WP_TYPE_OBJECT, WP_TYPE_PROPERTIES);

  signals[ACTION_SCHEDULE_RESCAN] = g_signal_new_class_handler (
      "schedule-rescan", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      (GCallback) wp_standard_event_source_schedule_rescan,
      NULL, NULL, NULL, G_TYPE_NONE, 0);
}

WP_PLUGIN_EXPORT gboolean
wireplumber__module_init (WpCore * core, GVariant * args, GError ** error)
{
  wp_plugin_register (g_object_new (wp_standard_event_source_get_type (),
          "name", "standard-event-source",
          "core", core,
          NULL));
  return TRUE;
}
