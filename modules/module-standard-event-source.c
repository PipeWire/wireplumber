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

static gboolean
get_object_type_and_priority (gpointer obj, const gchar **type, gint *priority)
{
  *type = NULL;

  if (WP_IS_FACTORY (obj))
  {
    *type = "factory";
    *priority = 200;
  }
  else if (WP_IS_CLIENT (obj))
  {
    *type = "client";
    *priority = 150;
  }
  else if (WP_IS_LINK (obj))
  {
    *type = "link";
    *priority = 100;
  }
  else if (WP_IS_PORT (obj))
  {
    *type = "port";
    *priority = 90;
  }
  else if (WP_IS_DEVICE (obj))
  {
    *type = "device";
    *priority = 80;
  }
  else if (WP_IS_NODE (obj))
  {
    *type = "node";
    *priority = 70;
  }
  else if (WP_IS_ENDPOINT (obj))
  {
    *type = "endpoint";
    *priority = 60;
  }
  else if (WP_IS_SI_LINKABLE (obj))
  {
    *type = "linkable";
    *priority = 50;
  }
  else if (WP_IS_METADATA (obj))
  {
    *type = "metadata";
    *priority = 40;
  }

  return type != NULL;
}

static void
on_metadata_changed (WpMetadata *obj, guint32 subject,
    const gchar *key, const gchar *spa_type, const gchar *value,
    WpStandardEventSource *self)
{
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));
  g_return_if_fail (core);
  g_autoptr (WpEventDispatcher) dispatcher =
      wp_event_dispatcher_get_instance (core);
  g_return_if_fail (dispatcher);
  g_autoptr (WpProperties) props = wp_properties_new_empty ();
  const gchar *type = NULL;
  gint priority = 0;

  if (G_UNLIKELY (!get_object_type_and_priority (obj, &type, &priority))) {
    wp_critical_object (self, "unknown object type: " WP_OBJECT_FORMAT,
        WP_OBJECT_ARGS (obj));
    return;
  }

  wp_properties_set (props, "event.subject.type", type);
  wp_properties_setf (props, "event.subject.id", "%u", subject);
  wp_properties_set (props, "event.subject.key", key);
  wp_properties_set (props, "event.subject.spa_type", spa_type);
  wp_properties_set (props, "event.subject.value", value);

  wp_event_dispatcher_push_event (dispatcher, wp_event_new (
      "object-changed", priority, g_steal_pointer (&props),
      G_OBJECT (self->om), G_OBJECT (obj)));
}

static void
on_params_changed (WpPipewireObject *obj, const gchar *id,
    WpStandardEventSource *self)
{
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));
  g_return_if_fail (core);
  g_autoptr (WpEventDispatcher) dispatcher =
      wp_event_dispatcher_get_instance (core);
  g_return_if_fail (dispatcher);
  g_autoptr (WpProperties) props = wp_properties_new_empty ();
  const gchar *type = NULL;
  gint priority = 0;

  if (G_UNLIKELY (!get_object_type_and_priority (obj, &type, &priority))) {
    wp_critical_object (self, "unknown object type: " WP_OBJECT_FORMAT,
        WP_OBJECT_ARGS (obj));
    return;
  }

  wp_properties_set (props, "event.subject.type", type);
  wp_properties_set (props, "event.subject.param-id", id);

  wp_event_dispatcher_push_event (dispatcher, wp_event_new (
          "params-changed", priority, g_steal_pointer (&props),
          G_OBJECT (self->om), G_OBJECT (obj)));
}

static void
on_node_state_changed (WpNode *obj, WpNodeState old_state,
    WpNodeState new_state, WpStandardEventSource *self)
{
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));
  g_return_if_fail (core);
  g_autoptr (WpEventDispatcher) dispatcher =
      wp_event_dispatcher_get_instance (core);

  wp_event_dispatcher_push_event (dispatcher, wp_event_new (
          "node-state-changed", 50,
          wp_properties_new (
              "event.subject.old-state", g_enum_to_string (WP_TYPE_NODE_STATE, old_state),
              "event.subject.new-state", g_enum_to_string (WP_TYPE_NODE_STATE, new_state),
              NULL),
          G_OBJECT (self->om), G_OBJECT (obj)));
}

static void
on_object_added (WpObjectManager *om, WpObject *obj, WpStandardEventSource *self)
{
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));
  g_return_if_fail (core);
  g_autoptr (WpEventDispatcher) dispatcher =
      wp_event_dispatcher_get_instance (core);
  const gchar *type = NULL;
  gint priority = 0;

  if (G_UNLIKELY (!get_object_type_and_priority (obj, &type, &priority))) {
    wp_critical_object (self, "unknown object type: " WP_OBJECT_FORMAT,
        WP_OBJECT_ARGS (obj));
    return;
  }

  wp_event_dispatcher_push_event (dispatcher, wp_event_new (
          "object-added", priority,
          wp_properties_new ("event.subject.type", type, NULL),
          G_OBJECT (om), G_OBJECT (obj)));

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
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));
  g_return_if_fail (core);
  g_autoptr (WpEventDispatcher) dispatcher =
      wp_event_dispatcher_get_instance (core);
  const gchar *type = NULL;
  gint priority = 0;

  if (G_UNLIKELY (!get_object_type_and_priority (obj, &type, &priority))) {
    wp_critical_object (self, "unknown object type: " WP_OBJECT_FORMAT,
        WP_OBJECT_ARGS (obj));
    return;
  }

  wp_event_dispatcher_push_event (dispatcher, wp_event_new (
          "object-removed", priority,
          wp_properties_new ("event.subject.type", type, NULL),
          G_OBJECT (om), G_OBJECT (obj)));
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
  wp_object_manager_add_interest (self->om, WP_TYPE_SI_LINKABLE, NULL);
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
  WpPluginClass *plugin_class = (WpPluginClass *) klass;

  plugin_class->enable = wp_standard_event_source_enable;
  plugin_class->disable = wp_standard_event_source_disable;
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
