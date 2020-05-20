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

enum {
  STEP_VERIFY_CONFIG = WP_TRANSITION_STEP_CUSTOM_START,
  STEP_ENSURE_NODE_FEATURES,
};

struct _WpSiSimpleNodeEndpoint
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

static void si_simple_node_endpoint_endpoint_init (WpSiEndpointInterface * iface);
static void si_simple_node_endpoint_stream_init (WpSiStreamInterface * iface);
static void si_simple_node_endpoint_port_info_init (WpSiPortInfoInterface * iface);

G_DECLARE_FINAL_TYPE(WpSiSimpleNodeEndpoint, si_simple_node_endpoint,
                     WP, SI_SIMPLE_NODE_ENDPOINT, WpSessionItem)
G_DEFINE_TYPE_WITH_CODE (WpSiSimpleNodeEndpoint, si_simple_node_endpoint, WP_TYPE_SESSION_ITEM,
    G_IMPLEMENT_INTERFACE (WP_TYPE_SI_ENDPOINT, si_simple_node_endpoint_endpoint_init)
    G_IMPLEMENT_INTERFACE (WP_TYPE_SI_STREAM, si_simple_node_endpoint_stream_init)
    G_IMPLEMENT_INTERFACE (WP_TYPE_SI_PORT_INFO, si_simple_node_endpoint_port_info_init))

static void
si_simple_node_endpoint_init (WpSiSimpleNodeEndpoint * self)
{
}

static void
si_simple_node_endpoint_reset (WpSessionItem * item)
{
  WpSiSimpleNodeEndpoint *self = WP_SI_SIMPLE_NODE_ENDPOINT (item);

  /* unexport & deactivate first */
  WP_SESSION_ITEM_CLASS (si_simple_node_endpoint_parent_class)->reset (item);

  g_clear_object (&self->node);
  self->name[0] = '\0';
  self->media_class[0] = '\0';
  self->role[0] = '\0';
  self->priority = 0;
  self->direction = WP_DIRECTION_INPUT;
  wp_session_item_clear_flag (item, WP_SI_FLAG_CONFIGURED);
}

static gpointer
si_simple_node_endpoint_get_associated_proxy (WpSessionItem * item,
    GType proxy_type)
{
  WpSiSimpleNodeEndpoint *self = WP_SI_SIMPLE_NODE_ENDPOINT (item);

  if (proxy_type == WP_TYPE_NODE)
    return self->node ? g_object_ref (self->node) : NULL;

  return WP_SESSION_ITEM_CLASS (si_simple_node_endpoint_parent_class)->
      get_associated_proxy (item, proxy_type);
}

static GVariant *
si_simple_node_endpoint_get_configuration (WpSessionItem * item)
{
  WpSiSimpleNodeEndpoint *self = WP_SI_SIMPLE_NODE_ENDPOINT (item);
  GVariantBuilder b;

  /* Set the properties */
  g_variant_builder_init (&b, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&b, "{sv}",
      "node", g_variant_new_uint64 ((guint64) self->node));
  g_variant_builder_add (&b, "{sv}",
      "name", g_variant_new_string (self->name));
  g_variant_builder_add (&b, "{sv}",
      "media-class", g_variant_new_string (self->media_class));
  g_variant_builder_add (&b, "{sv}",
      "role", g_variant_new_string (self->role));
  g_variant_builder_add (&b, "{sv}",
      "priority", g_variant_new_uint32 (self->priority));
  g_variant_builder_add (&b, "{sv}",
      "direction", g_variant_new_byte (self->direction));
  return g_variant_builder_end (&b);
}

static gboolean
si_simple_node_endpoint_configure (WpSessionItem * item, GVariant * args)
{
  WpSiSimpleNodeEndpoint *self = WP_SI_SIMPLE_NODE_ENDPOINT (item);
  guint64 node_i;
  const gchar *tmp_str;
  g_autoptr (WpProperties) props = NULL;

  if (wp_session_item_get_flags (item) & (WP_SI_FLAG_ACTIVATING | WP_SI_FLAG_ACTIVE))
    return FALSE;

  /* reset previous config */
  g_clear_object (&self->node);
  self->name[0] = '\0';
  self->media_class[0] = '\0';
  self->role[0] = '\0';
  self->priority = 0;
  self->direction = WP_DIRECTION_INPUT;

  if (!g_variant_lookup (args, "node", "t", &node_i))
    return FALSE;

  g_return_val_if_fail (WP_IS_NODE (GUINT_TO_POINTER (node_i)), FALSE);

  self->node = g_object_ref (GUINT_TO_POINTER (node_i));
  props = wp_proxy_get_properties (WP_PROXY (self->node));

  if (g_variant_lookup (args, "name", "&s", &tmp_str)) {
    strncpy (self->name, tmp_str, sizeof (self->name) - 1);
  } else {
    tmp_str = wp_properties_get (props, PW_KEY_NODE_DESCRIPTION);
    if (G_UNLIKELY (!tmp_str))
      tmp_str = wp_properties_get (props, PW_KEY_NODE_NAME);
    if (G_LIKELY (tmp_str))
      strncpy (self->name, tmp_str, sizeof (self->name) - 1);
  }

  if (g_variant_lookup (args, "media-class", "&s", &tmp_str)) {
    strncpy (self->media_class, tmp_str, sizeof (self->media_class) - 1);
  } else {
    tmp_str = wp_properties_get (props, PW_KEY_MEDIA_CLASS);
    if (G_LIKELY (tmp_str))
      strncpy (self->media_class, tmp_str, sizeof (self->media_class) - 1);
  }

  if (g_variant_lookup (args, "role", "&s", &tmp_str)) {
    strncpy (self->role, tmp_str, sizeof (self->role) - 1);
  } else {
    tmp_str = wp_properties_get (props, PW_KEY_MEDIA_ROLE);
    if (tmp_str)
      strncpy (self->role, tmp_str, sizeof (self->role) - 1);
  }

  g_variant_lookup (args, "priority", "u", &self->priority);

  if (strstr (self->media_class, "Source") ||
      strstr (self->media_class, "Output"))
    self->direction = WP_DIRECTION_OUTPUT;

  wp_session_item_set_flag (item, WP_SI_FLAG_CONFIGURED);

  return TRUE;
}

static guint
si_simple_node_endpoint_activate_get_next_step (WpSessionItem * item,
     WpTransition * transition, guint step)
{
  switch (step) {
    case WP_TRANSITION_STEP_NONE:
      return STEP_VERIFY_CONFIG;

    case STEP_VERIFY_CONFIG:
      return STEP_ENSURE_NODE_FEATURES;

    case STEP_ENSURE_NODE_FEATURES:
      return WP_TRANSITION_STEP_NONE;

    default:
      return WP_TRANSITION_STEP_ERROR;
  }
}

static void
on_node_augmented (WpProxy * node, GAsyncResult * res, WpTransition * transition)
{
  g_autoptr (GError) error = NULL;
  if (!wp_proxy_augment_finish (node, res, &error)) {
    wp_transition_return_error (transition, g_steal_pointer (&error));
    return;
  }

  wp_transition_advance (transition);
}

static void
si_simple_node_endpoint_activate_execute_step (WpSessionItem * item,
    WpTransition * transition, guint step)
{
  WpSiSimpleNodeEndpoint *self = WP_SI_SIMPLE_NODE_ENDPOINT (item);

  switch (step) {
    case STEP_VERIFY_CONFIG:
      if (G_UNLIKELY (!(wp_session_item_get_flags (item) & WP_SI_FLAG_CONFIGURED))) {
        wp_transition_return_error (transition,
            g_error_new (WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVARIANT,
                "si-simple-node-endpoint: cannot activate item without it "
                "being configured first"));
      }
      wp_transition_advance (transition);
      break;

    case STEP_ENSURE_NODE_FEATURES:
      wp_proxy_augment (WP_PROXY (self->node), WP_NODE_FEATURES_STANDARD,
          NULL, (GAsyncReadyCallback) on_node_augmented, transition);
      break;

    default:
      g_return_if_reached ();
  }
}

static void
si_simple_node_endpoint_class_init (WpSiSimpleNodeEndpointClass * klass)
{
  WpSessionItemClass *si_class = (WpSessionItemClass *) klass;

  si_class->reset = si_simple_node_endpoint_reset;
  si_class->get_associated_proxy = si_simple_node_endpoint_get_associated_proxy;
  si_class->configure = si_simple_node_endpoint_configure;
  si_class->get_configuration = si_simple_node_endpoint_get_configuration;
  si_class->activate_get_next_step =
      si_simple_node_endpoint_activate_get_next_step;
  si_class->activate_execute_step =
      si_simple_node_endpoint_activate_execute_step;
}

static GVariant *
si_simple_node_endpoint_get_registration_info (WpSiEndpoint * item)
{
  WpSiSimpleNodeEndpoint *self = WP_SI_SIMPLE_NODE_ENDPOINT (item);
  GVariantBuilder b;

  g_variant_builder_init (&b, G_VARIANT_TYPE ("(ssya{ss})"));
  g_variant_builder_add (&b, "s", self->name);
  g_variant_builder_add (&b, "s", self->media_class);
  g_variant_builder_add (&b, "y", (guchar) self->direction);
  g_variant_builder_add (&b, "a{ss}", NULL);

  return g_variant_builder_end (&b);
}

static WpProperties *
si_simple_node_endpoint_get_properties (WpSiEndpoint * item)
{
  WpSiSimpleNodeEndpoint *self = WP_SI_SIMPLE_NODE_ENDPOINT (item);
  g_autoptr (WpProperties) node_props = NULL;
  WpProperties *result;

  result = wp_properties_new (
      PW_KEY_MEDIA_ROLE, self->role,
      NULL);
  wp_properties_setf (result, "endpoint.priority", "%u", self->priority);

  /* copy useful properties from the node */
  node_props = wp_proxy_get_properties (WP_PROXY (self->node));
  wp_properties_update_keys (result, node_props,
      PW_KEY_DEVICE_ID,
      PW_KEY_NODE_TARGET,
      NULL);

  /* associate with the node */
  wp_properties_setf (result, PW_KEY_NODE_ID, "%d",
      wp_proxy_get_bound_id (WP_PROXY (self->node)));

  /* propagate the device icon, if this is a device */
  const gchar *icon = wp_properties_get (node_props, PW_KEY_DEVICE_ICON_NAME);
  if (icon)
    wp_properties_set (result, PW_KEY_ENDPOINT_ICON_NAME, icon);

  /* endpoint.client.id: the id of the client that created the node
   * Not to be confused with client.id, which will also be set on the endpoint
   * to the id of the client object that creates the endpoint (wireplumber) */
  const gchar *client_id = wp_properties_get (node_props, PW_KEY_CLIENT_ID);
  if (client_id)
    wp_properties_set (result, PW_KEY_ENDPOINT_CLIENT_ID, client_id);

  return result;
}

static guint
si_simple_node_endpoint_get_n_streams (WpSiEndpoint * item)
{
  return 1;
}

static WpSiStream *
si_simple_node_endpoint_get_stream (WpSiEndpoint * item, guint index)
{
  g_return_val_if_fail (index == 0, NULL);
  return WP_SI_STREAM (item);
}

static void
si_simple_node_endpoint_endpoint_init (WpSiEndpointInterface * iface)
{
  iface->get_registration_info = si_simple_node_endpoint_get_registration_info;
  iface->get_properties = si_simple_node_endpoint_get_properties;
  iface->get_n_streams = si_simple_node_endpoint_get_n_streams;
  iface->get_stream = si_simple_node_endpoint_get_stream;
}

static GVariant *
si_simple_node_endpoint_get_stream_registration_info (WpSiStream * self)
{
  GVariantBuilder b;

  g_variant_builder_init (&b, G_VARIANT_TYPE ("(sa{ss})"));
  g_variant_builder_add (&b, "s", "default");
  g_variant_builder_add (&b, "a{ss}", NULL);

  return g_variant_builder_end (&b);
}

static WpSiEndpoint *
si_simple_node_endpoint_get_stream_parent_endpoint (WpSiStream * self)
{
  return WP_SI_ENDPOINT (g_object_ref (self));
}

static void
si_simple_node_endpoint_stream_init (WpSiStreamInterface * iface)
{
  iface->get_registration_info =
      si_simple_node_endpoint_get_stream_registration_info;
  iface->get_parent_endpoint =
      si_simple_node_endpoint_get_stream_parent_endpoint;
}

static GVariant *
si_simple_node_endpoint_get_ports (WpSiPortInfo * item, const gchar * context)
{
  WpSiSimpleNodeEndpoint *self = WP_SI_SIMPLE_NODE_ENDPOINT (item);
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

  for (it = wp_node_iterate_ports (self->node);
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
    props = wp_proxy_get_properties (WP_PROXY (port));
    str = wp_properties_get (props, PW_KEY_PORT_MONITOR);
    is_monitor = str && pw_properties_parse_bool (str);
    if (is_monitor != monitor_context)
      continue;

    port_id = wp_proxy_get_bound_id (WP_PROXY (port));

    /* try to find the audio channel; if channel is NULL, this will silently
       leave the channel_id to its default value, 0 */
    channel = wp_properties_get (props, PW_KEY_AUDIO_CHANNEL);
    wp_spa_type_get_by_nick (WP_SPA_TYPE_TABLE_AUDIO_CHANNEL, channel,
        &channel_id, NULL, NULL);

    g_variant_builder_add (&b, "(uuu)", node_id, port_id, channel_id);
  }

  return g_variant_builder_end (&b);
}

static void
si_simple_node_endpoint_port_info_init (WpSiPortInfoInterface * iface)
{
  iface->get_ports = si_simple_node_endpoint_get_ports;
}

WP_PLUGIN_EXPORT void
wireplumber__module_init (WpModule * module, WpCore * core, GVariant * args)
{
  GVariantBuilder b;

  g_variant_builder_init (&b, G_VARIANT_TYPE ("a(ssymv)"));
  g_variant_builder_add (&b, "(ssymv)", "node", "t",
      WP_SI_CONFIG_OPTION_WRITEABLE | WP_SI_CONFIG_OPTION_REQUIRED, NULL);
  g_variant_builder_add (&b, "(ssymv)", "name", "s",
      WP_SI_CONFIG_OPTION_WRITEABLE, NULL);
  g_variant_builder_add (&b, "(ssymv)", "media-class", "s",
      WP_SI_CONFIG_OPTION_WRITEABLE, NULL);
  g_variant_builder_add (&b, "(ssymv)", "role", "s",
      WP_SI_CONFIG_OPTION_WRITEABLE, NULL);
  g_variant_builder_add (&b, "(ssymv)", "priority", "u",
      WP_SI_CONFIG_OPTION_WRITEABLE, NULL);
  g_variant_builder_add (&b, "(ssymv)", "direction", "y", 0, NULL);

  wp_si_factory_register (core, wp_si_factory_new_simple (
      "si-simple-node-endpoint", si_simple_node_endpoint_get_type (),
      g_variant_builder_end (&b)));
}
