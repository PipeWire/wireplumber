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
  PROP_0,
  PROP_OBJECT_MANAGER,
};

struct _WpStandardEventSource
{
  WpPlugin parent;
  WpObjectManager *om;
};

G_DECLARE_FINAL_TYPE (WpStandardEventSource, wp_standard_event_source,
                      WP, STANDARD_EVENT_SOURCE, WpPlugin)
G_DEFINE_TYPE (WpStandardEventSource, wp_standard_event_source, WP_TYPE_PLUGIN)

static void
wp_standard_event_source_init (WpStandardEventSource * self)
{
}

static void
wp_standard_event_source_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpStandardEventSource *self = WP_STANDARD_EVENT_SOURCE (object);

  switch (property_id) {
  case PROP_OBJECT_MANAGER:
    g_value_set_object (value, self->om);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
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
  else if (WP_IS_FACTORY (obj))
    return "factory";

  wp_debug_object (obj, "Unknown global proxy type");
  return G_OBJECT_TYPE_NAME (obj);
}

static gint
get_default_event_priority (const gchar *event_type, const gchar *subject_type)
{
  if (!g_strcmp0 (event_type, "object-added") ||
      !g_strcmp0 (event_type, "object-removed"))
  {
    if (!g_strcmp0 (subject_type, "client"))
      return 200;
    else if (!g_strcmp0 (subject_type, "device"))
      return 170;
    else if (!g_strcmp0 (subject_type, "port"))
      return 150;
    else if (!g_strcmp0 (subject_type, "node"))
      return 130;
    else if (!g_strcmp0 (subject_type, "session-item"))
      return 110;
    else
      return 20;
  }
  else if (!g_strcmp0 (event_type, "find-si-target-and-link"))
    return 500;
  else if (!g_strcmp0 (event_type, "rescan-session"))
    return -500;
  else if (!g_strcmp0 (event_type, "node-state-changed"))
    return 50;
  else if (!g_strcmp0 (event_type, "params-changed"))
    return 50;
  else if (!g_strcmp0 (event_type, "metadata-changed"))
    return 50;

  wp_debug ("Unknown event type: %s, using priority 0", event_type);
  return 0;
}

static void
wp_standard_event_source_push_event (WpStandardEventSource *self,
    const gchar *event_type, const gchar *subject_type,
    WpProperties *misc_properties, gpointer subject)
{
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));
  g_return_if_fail (core);
  g_autoptr (WpEventDispatcher) dispatcher =
      wp_event_dispatcher_get_instance (core);
  g_return_if_fail (dispatcher);

  gint priority = get_default_event_priority (event_type, subject_type);
  g_autoptr (WpProperties) properties = wp_properties_new_empty ();

  if (subject_type)
    wp_properties_set (properties, "event.subject.type", subject_type);
  if (misc_properties)
    wp_properties_add (properties, misc_properties);

  wp_event_dispatcher_push_event (dispatcher, wp_event_new (
      event_type, priority, g_steal_pointer (&properties),
      G_OBJECT (self), G_OBJECT (subject)));
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

  wp_standard_event_source_push_event (self,
      "metadata-changed", "metadata", properties, obj);
}

static void
on_params_changed (WpPipewireObject *obj, const gchar *id,
    WpStandardEventSource *self)
{
  g_autoptr (WpProperties) properties = wp_properties_new_empty ();
  const gchar *subject_type = get_object_type (obj, &properties);

  wp_properties_set (properties, "event.subject.param-id", id);

  wp_standard_event_source_push_event (self,
      "params-changed", subject_type, properties, obj);
}

static void
on_node_state_changed (WpNode *obj, WpNodeState old_state,
    WpNodeState new_state, WpStandardEventSource *self)
{
  g_autoptr (WpProperties) properties = wp_properties_new (
      "event.subject.old-state", g_enum_to_string (WP_TYPE_NODE_STATE, old_state),
      "event.subject.new-state", g_enum_to_string (WP_TYPE_NODE_STATE, new_state),
      NULL);
  wp_standard_event_source_push_event (self,
      "node-state-changed", "node", properties, obj);
}

static void
on_object_added (WpObjectManager *om, WpObject *obj, WpStandardEventSource *self)
{
  g_autoptr (WpProperties) properties = NULL;
  const gchar *subject_type = get_object_type (obj, &properties);

  wp_standard_event_source_push_event (self,
      "object-added", subject_type, properties, obj);

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
  g_autoptr (WpProperties) properties = NULL;
  const gchar *subject_type = get_object_type (obj, &properties);

  wp_standard_event_source_push_event (self,
      "object-removed", subject_type, properties, obj);
}

static void
on_om_installed (WpObjectManager * om, WpStandardEventSource * self)
{
  wp_object_update_features (WP_OBJECT (self), WP_PLUGIN_FEATURE_ENABLED, 0);
}

static void
wp_standard_event_source_enable (WpPlugin * plugin, WpTransition * transition)
{
  WpStandardEventSource * self = WP_STANDARD_EVENT_SOURCE (plugin);
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (plugin));
  g_return_if_fail (core);

  self->om = wp_object_manager_new ();
  wp_object_manager_add_interest (self->om, WP_TYPE_GLOBAL_PROXY, NULL);
  wp_object_manager_add_interest (self->om, WP_TYPE_SESSION_ITEM, NULL);
  wp_object_manager_request_object_features (self->om,
      WP_TYPE_GLOBAL_PROXY, WP_OBJECT_FEATURES_ALL);
  g_signal_connect_object (self->om, "object-added",
      G_CALLBACK (on_object_added), self, 0);
  g_signal_connect_object (self->om, "object-removed",
      G_CALLBACK (on_object_removed), self, 0);
  g_signal_connect_object (self->om, "installed",
      G_CALLBACK (on_om_installed), self, 0);
  wp_core_install_object_manager (core, self->om);
}

static void
wp_standard_event_source_disable (WpPlugin * plugin)
{
  WpStandardEventSource * self = WP_STANDARD_EVENT_SOURCE (plugin);
  g_clear_object (&self->om);
}

static void
wp_standard_event_source_class_init (WpStandardEventSourceClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpPluginClass *plugin_class = (WpPluginClass *) klass;

  object_class->get_property = wp_standard_event_source_get_property;

  plugin_class->enable = wp_standard_event_source_enable;
  plugin_class->disable = wp_standard_event_source_disable;

  g_object_class_install_property (object_class, PROP_OBJECT_MANAGER,
      g_param_spec_string ("object-manager", "object-manager",
          "The WpObjectManager instance that is used to generate events", NULL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
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
