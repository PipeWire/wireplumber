/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>
#include <pipewire/keys.h>
#include <pipewire/properties.h>
#include <pipewire/extensions/session-manager/keys.h>

#define SI_FACTORY_NAME "si-node"

struct _WpSiNode
{
  WpSessionItem parent;

  /* configuration */
  WpNode *node;
  gchar name[96];
  gchar media_class[32];
  gchar role[32];
  guint priority;
  WpDirection direction;
};

static void si_node_port_info_init (WpSiPortInfoInterface * iface);

G_DECLARE_FINAL_TYPE(WpSiNode, si_node, WP, SI_NODE, WpSessionItem)
G_DEFINE_TYPE_WITH_CODE (WpSiNode, si_node, WP_TYPE_SESSION_ITEM,
    G_IMPLEMENT_INTERFACE (WP_TYPE_SI_PORT_INFO, si_node_port_info_init))

static void
si_node_init (WpSiNode * self)
{
}

static void
si_node_reset (WpSessionItem * item)
{
  WpSiNode *self = WP_SI_NODE (item);

  /* deactivate first */
  wp_object_deactivate (WP_OBJECT (self), WP_SESSION_ITEM_FEATURE_ACTIVE);

  /* reset */
  g_clear_object (&self->node);
  self->name[0] = '\0';
  self->media_class[0] = '\0';
  self->role[0] = '\0';
  self->priority = 0;
  self->direction = WP_DIRECTION_INPUT;

  WP_SESSION_ITEM_CLASS (si_node_parent_class)->reset (item);
}

static gboolean
si_node_configure (WpSessionItem * item, WpProperties *p)
{
  WpSiNode *self = WP_SI_NODE (item);
  g_autoptr (WpProperties) si_props = wp_properties_ensure_unique_owner (p);
  WpNode *node = NULL;
  WpProperties *node_props = NULL;
  const gchar *str;

  /* reset previous config */
  si_node_reset (item);

  str = wp_properties_get (si_props, "node");
  if (!str || sscanf(str, "%p", &node) != 1 || !WP_IS_NODE (node))
    return FALSE;

  node_props = wp_pipewire_object_get_properties (WP_PIPEWIRE_OBJECT (node));

  str = wp_properties_get (si_props, "name");
  if (str) {
    strncpy (self->name, str, sizeof (self->name) - 1);
  } else {
    str = wp_properties_get (node_props, PW_KEY_NODE_NAME);
    if (G_LIKELY (str))
      strncpy (self->name, str, sizeof (self->name) - 1);
    else
      strncpy (self->name, "Unknown", sizeof (self->name) - 1);
    wp_properties_set (si_props, "name", self->name);
  }

  str = wp_properties_get (si_props, "media.class");
  if (str) {
    strncpy (self->media_class, str, sizeof (self->media_class) - 1);
  } else {
    str = wp_properties_get (node_props, PW_KEY_MEDIA_CLASS);
    if (G_LIKELY (str))
      strncpy (self->media_class, str, sizeof (self->media_class) - 1);
    else
      strncpy (self->media_class, "Unknown", sizeof (self->media_class) - 1);
    wp_properties_set (si_props, "media.class", self->media_class);
  }

  str = wp_properties_get (si_props, "role");
  if (str) {
    strncpy (self->role, str, sizeof (self->role) - 1);
  } else {
    str = wp_properties_get (node_props, PW_KEY_MEDIA_ROLE);
    if (str)
      strncpy (self->role, str, sizeof (self->role) - 1);
    else
      strncpy (self->role, "Unknown", sizeof (self->role) - 1);
    wp_properties_set (si_props, "role", self->role);
  }

  if (strstr (self->media_class, "Source") ||
      strstr (self->media_class, "Output"))
    self->direction = WP_DIRECTION_OUTPUT;
  wp_properties_setf (si_props, "direction", "%u", self->direction);

  str = wp_properties_get (si_props, "priority");
  if (str && sscanf(str, "%u", &self->priority) != 1)
    return FALSE;
  if (!str)
    wp_properties_setf (si_props, "priority", "%u", self->priority);

  self->node = g_object_ref (node);

  wp_properties_set (si_props, "si.factory.name", SI_FACTORY_NAME);
  wp_session_item_set_properties (WP_SESSION_ITEM (self),
      g_steal_pointer (&si_props));
  return TRUE;
}

static gpointer
si_node_get_associated_proxy (WpSessionItem * item, GType proxy_type)
{
  WpSiNode *self = WP_SI_NODE (item);

  if (proxy_type == WP_TYPE_NODE)
    return self->node ? g_object_ref (self->node) : NULL;

  return NULL;
}

static void
si_node_disable_active (WpSessionItem *si)
{
  WpSiNode *self = WP_SI_NODE (si);

  wp_object_update_features (WP_OBJECT (self), 0,
      WP_SESSION_ITEM_FEATURE_ACTIVE);
}

static void
on_node_activated (WpObject * node, GAsyncResult * res,
    WpTransition * transition)
{
  WpSiNode *self = wp_transition_get_source_object (transition);
  g_autoptr (GError) error = NULL;

  if (!wp_object_activate_finish (node, res, &error)) {
    wp_transition_return_error (transition, g_steal_pointer (&error));
    return;
  }

  wp_object_update_features (WP_OBJECT (self),
      WP_SESSION_ITEM_FEATURE_ACTIVE, 0);
}

static void
si_node_enable_active (WpSessionItem *si, WpTransition *transition)
{
  WpSiNode *self = WP_SI_NODE (si);

  if (!wp_session_item_is_configured (si)) {
    wp_transition_return_error (transition,
        g_error_new (WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVARIANT,
            "si-node: item is not configured"));
    return;
  }

  wp_object_activate (WP_OBJECT (self->node),
      WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL | WP_NODE_FEATURE_PORTS,
      NULL, (GAsyncReadyCallback) on_node_activated, transition);
}

static WpObjectFeatures
si_node_get_supported_features (WpObject * self)
{
  return WP_SESSION_ITEM_FEATURE_ACTIVE;
}

static void
si_node_class_init (WpSiNodeClass * klass)
{
  WpObjectClass * wpobject_class = (WpObjectClass *) klass;
  WpSessionItemClass *si_class = (WpSessionItemClass *) klass;

  wpobject_class->get_supported_features = si_node_get_supported_features;

  si_class->reset = si_node_reset;
  si_class->configure = si_node_configure;
  si_class->get_associated_proxy = si_node_get_associated_proxy;
  si_class->disable_active = si_node_disable_active;
  si_class->enable_active = si_node_enable_active;
}

static GVariant *
si_node_get_ports (WpSiPortInfo * item, const gchar * context)
{
  WpSiNode *self = WP_SI_NODE (item);
  g_auto (GVariantBuilder) b = G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE_ARRAY);
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) val = G_VALUE_INIT;
  WpDirection direction = self->direction;
  gboolean monitor_context = FALSE;
  guint32 node_id;

  /* context can only be NULL, "reverse" or "monitor" */
  if (!g_strcmp0 (context, "reverse")) {
    direction = (self->direction == WP_DIRECTION_INPUT) ?
        WP_DIRECTION_OUTPUT : WP_DIRECTION_INPUT;
  }
  else if (!g_strcmp0 (context, "monitor")) {
    direction = WP_DIRECTION_OUTPUT;
    monitor_context = TRUE;
  }
  else if (context != NULL) {
    /* on any other context, return an empty list of ports */
    return g_variant_new_array (G_VARIANT_TYPE ("(uuu)"), NULL, 0);
  }

  g_variant_builder_init (&b, G_VARIANT_TYPE ("a(uuu)"));
  node_id = wp_proxy_get_bound_id (WP_PROXY (self->node));

  for (it = wp_node_new_ports_iterator (self->node);
       wp_iterator_next (it, &val);
       g_value_unset (&val))
  {
    WpPort *port = g_value_get_object (&val);
    g_autoptr (WpProperties) props = NULL;
    const gchar *str;
    const gchar *channel;
    guint32 port_id, channel_id = 0;
    gboolean is_monitor = FALSE;

    if (wp_port_get_direction (port) != direction)
      continue;

    /* skip monitor ports if not monitor context, or skip non-monitor ports if
     * monitor context */
    props = wp_pipewire_object_get_properties (WP_PIPEWIRE_OBJECT (port));
    str = wp_properties_get (props, PW_KEY_PORT_MONITOR);
    is_monitor = str && pw_properties_parse_bool (str);
    if (is_monitor != monitor_context)
      continue;

    port_id = wp_proxy_get_bound_id (WP_PROXY (port));

    /* try to find the audio channel; if channel is NULL, this will silently
       leave the channel_id to its default value, 0 */
    channel = wp_properties_get (props, PW_KEY_AUDIO_CHANNEL);
    if (channel) {
      WpSpaIdValue idval = wp_spa_id_value_from_short_name (
          "Spa:Enum:AudioChannel", channel);
      if (idval)
        channel_id = wp_spa_id_value_number (idval);
    }

    g_variant_builder_add (&b, "(uuu)", node_id, port_id, channel_id);
  }

  return g_variant_builder_end (&b);
}

static void
si_node_port_info_init (WpSiPortInfoInterface * iface)
{
  iface->get_ports = si_node_get_ports;
}

WP_PLUGIN_EXPORT gboolean
wireplumber__module_init (WpCore * core, GVariant * args, GError ** error)
{
  wp_si_factory_register (core, wp_si_factory_new_simple (SI_FACTORY_NAME,
      si_node_get_type ()));
  return TRUE;
}
