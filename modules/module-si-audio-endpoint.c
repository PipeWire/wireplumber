/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>
#include <pipewire/pipewire.h>
#include <pipewire/extensions/session-manager/keys.h>
#include <spa/param/audio/raw.h>

#define SI_FACTORY_NAME "si-audio-endpoint"

struct _WpSiAudioEndpoint
{
  WpSessionItem parent;

  /* configuration */
  gchar name[96];
  gchar media_class[32];
  WpDirection direction;
  gchar role[32];
  guint priority;
  WpSession *session;

  /* activation */
  WpNode *node;

  /* export */
  WpImplEndpoint *impl_endpoint;
};

static void si_audio_endpoint_endpoint_init (WpSiEndpointInterface * iface);
static void si_audio_endpoint_port_info_init (WpSiPortInfoInterface * iface);

G_DECLARE_FINAL_TYPE(WpSiAudioEndpoint, si_audio_endpoint, WP,
    SI_AUDIO_ENDPOINT, WpSessionItem)
G_DEFINE_TYPE_WITH_CODE (WpSiAudioEndpoint, si_audio_endpoint,
    WP_TYPE_SESSION_ITEM,
    G_IMPLEMENT_INTERFACE (WP_TYPE_SI_ENDPOINT, si_audio_endpoint_endpoint_init)
    G_IMPLEMENT_INTERFACE (WP_TYPE_SI_PORT_INFO,
      si_audio_endpoint_port_info_init))

static void
si_audio_endpoint_init (WpSiAudioEndpoint * self)
{
}

static void
si_audio_endpoint_reset (WpSessionItem * item)
{
  WpSiAudioEndpoint *self = WP_SI_AUDIO_ENDPOINT (item);

  /* deactivate first */
  wp_object_deactivate (WP_OBJECT (self),
      WP_SESSION_ITEM_FEATURE_ACTIVE | WP_SESSION_ITEM_FEATURE_EXPORTED);

  /* reset */
  self->name[0] = '\0';
  self->direction = WP_DIRECTION_INPUT;
  self->role[0] = '\0';
  self->priority = 0;
  g_clear_object (&self->session);

  WP_SESSION_ITEM_CLASS (si_audio_endpoint_parent_class)->reset (item);
}

static gboolean
si_audio_endpoint_configure (WpSessionItem * item, WpProperties *p)
{
  WpSiAudioEndpoint *self = WP_SI_AUDIO_ENDPOINT (item);
  g_autoptr (WpProperties) si_props = wp_properties_ensure_unique_owner (p);
  WpSession *session = NULL;
  const gchar *str;

  /* reset previous config */
  si_audio_endpoint_reset (item);

  str = wp_properties_get (si_props, "name");
  if (!str)
    return FALSE;
  strncpy (self->name, str, sizeof (self->name) - 1);

  str = wp_properties_get (si_props, "media.class");
  if (!str)
    return FALSE;
  strncpy (self->media_class, str, sizeof (self->media_class) - 1);

  if (strstr (self->media_class, "Source") ||
      strstr (self->media_class, "Output"))
    self->direction = WP_DIRECTION_OUTPUT;
  wp_properties_setf (si_props, "direction", "%u", self->direction);

  str = wp_properties_get (si_props, "role");
  if (str) {
    strncpy (self->role, str, sizeof (self->role) - 1);
  } else {
    strncpy (self->role, "Unknown", sizeof (self->role) - 1);
    wp_properties_set (si_props, "role", self->role);
  }

  str = wp_properties_get (si_props, "priority");
  if (str && sscanf(str, "%u", &self->priority) != 1)
    return FALSE;
  if (!str)
    wp_properties_setf (si_props, "priority", "%u", self->priority);

  /* session is optional (only needed if we want to export) */
  str = wp_properties_get (si_props, "session");
  if (str && (sscanf(str, "%p", &session) != 1 || !WP_IS_SESSION (session)))
    return FALSE;
  if (!str)
    wp_properties_setf (si_props, "session", "%p", session);

  if (session)
    self->session = g_object_ref (session);

  wp_properties_set (si_props, "si.factory.name", SI_FACTORY_NAME);
  wp_session_item_set_properties (WP_SESSION_ITEM (self),
      g_steal_pointer (&si_props));
  return TRUE;
}

static gpointer
si_audio_endpoint_get_associated_proxy (WpSessionItem * item, GType proxy_type)
{
  WpSiAudioEndpoint *self = WP_SI_AUDIO_ENDPOINT (item);

  if (proxy_type == WP_TYPE_NODE)
    return self->node ? g_object_ref (self->node) : NULL;
  if (proxy_type == WP_TYPE_SESSION)
    return self->session ? g_object_ref (self->session) : NULL;
  else if (proxy_type == WP_TYPE_ENDPOINT)
    return self->impl_endpoint ? g_object_ref (self->impl_endpoint) : NULL;

  return NULL;
}

static void
si_audio_endpoint_disable_active (WpSessionItem *si)
{
  WpSiAudioEndpoint *self = WP_SI_AUDIO_ENDPOINT (si);

  if (self->node)
    wp_object_deactivate (WP_OBJECT (self->node), WP_OBJECT_FEATURES_ALL);
  g_clear_object (&self->node);
  wp_object_update_features (WP_OBJECT (self), 0,
      WP_SESSION_ITEM_FEATURE_ACTIVE);
}

static void
si_audio_endpoint_disable_exported (WpSessionItem *si)
{
  WpSiAudioEndpoint *self = WP_SI_AUDIO_ENDPOINT (si);

  g_clear_object (&self->impl_endpoint);
  wp_object_update_features (WP_OBJECT (self), 0,
      WP_SESSION_ITEM_FEATURE_EXPORTED);
}

static void
on_node_activate_done (WpObject * node, GAsyncResult * res,
    WpTransition * transition)
{
  WpSiAudioEndpoint *self = wp_transition_get_source_object (transition);
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));
  g_autoptr (GError) error = NULL;

  if (!wp_object_activate_finish (node, res, &error)) {
    wp_transition_return_error (transition, g_steal_pointer (&error));
    return;
  }

  wp_object_update_features (WP_OBJECT (self),
      WP_SESSION_ITEM_FEATURE_ACTIVE, 0);
}

static WpSpaPod *
build_2_channels_audio_format ()
{
  g_autoptr (WpSpaPod) channels = NULL;

  {
    g_autoptr (WpSpaPodBuilder) b = wp_spa_pod_builder_new_array ();
    wp_spa_pod_builder_add_id (b, SPA_AUDIO_CHANNEL_FL);
    wp_spa_pod_builder_add_id (b, SPA_AUDIO_CHANNEL_FR);
    channels = wp_spa_pod_builder_end (b);
  }

  return wp_spa_pod_new_object (
      "Spa:Pod:Object:Param:Format", "Format",
      "mediaType",    "K", "audio",
      "mediaSubtype", "K", "raw",
      "format",       "K", "F32P",
      "rate",         "i", 48000,
      "channels",     "i", 2,
      "position",     "P", channels,
      NULL);
}

static void
si_audio_endpoint_enable_active (WpSessionItem *si, WpTransition *transition)
{
  WpSiAudioEndpoint *self = WP_SI_AUDIO_ENDPOINT (si);
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));
  g_autoptr (WpSpaPod) format = NULL;
  g_autofree gchar *name =g_strdup_printf ("control.%s", self->name);
  g_autofree gchar *desc =g_strdup_printf ("Control for %s", self->name);

  if (!wp_session_item_is_configured (si)) {
    wp_transition_return_error (transition,
        g_error_new (WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVARIANT,
            "si-audio-endpoint: item is not configured"));
    return;
  }

  /* create the node */
  self->node = wp_node_new_from_factory (core, "adapter",
      wp_properties_new (
          PW_KEY_NODE_NAME, name,
          PW_KEY_MEDIA_CLASS, "Audio/Duplex",
          PW_KEY_FACTORY_NAME, "support.null-audio-sink",
          PW_KEY_NODE_DESCRIPTION, desc,
          SPA_KEY_AUDIO_POSITION, "FL,FR",
          NULL));
  if (!self->node) {
    wp_transition_return_error (transition,
        g_error_new (WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVARIANT,
            "si-audio-endpoint: could not create null-audio-sink node"));
    return;
  }

  /* TODO: for now we always configure ports to be 2 channels at 48KHz */
  format = build_2_channels_audio_format ();
  wp_pipewire_object_set_param (WP_PIPEWIRE_OBJECT (self->node),
      "PortConfig", 0,
      wp_spa_pod_new_object (
          "Spa:Pod:Object:Param:PortConfig", "PortConfig",
          "direction",  "I", WP_DIRECTION_INPUT,
          "mode",       "K", "dsp",
          "monitor",    "b", TRUE,
          "format",     "P", format,
          NULL));

  /* activate node */
  wp_object_activate (WP_OBJECT (self->node),
      WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL | WP_NODE_FEATURE_PORTS, NULL,
      (GAsyncReadyCallback) on_node_activate_done, transition);
}

static void
on_impl_endpoint_activated (WpObject * object, GAsyncResult * res,
    WpTransition * transition)
{
  WpSiAudioEndpoint *self = wp_transition_get_source_object (transition);
  g_autoptr (GError) error = NULL;

  if (!wp_object_activate_finish (object, res, &error)) {
    wp_transition_return_error (transition, g_steal_pointer (&error));
    return;
  }

  wp_object_update_features (WP_OBJECT (self),
      WP_SESSION_ITEM_FEATURE_EXPORTED, 0);
}

static void
si_audio_endpoint_enable_exported (WpSessionItem *si, WpTransition *transition)
{
  WpSiAudioEndpoint *self = WP_SI_AUDIO_ENDPOINT (si);
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));

  self->impl_endpoint = wp_impl_endpoint_new (core, WP_SI_ENDPOINT (self));

  g_signal_connect_object (self->impl_endpoint, "pw-proxy-destroyed",
      G_CALLBACK (wp_session_item_handle_proxy_destroyed), self, 0);

  wp_object_activate (WP_OBJECT (self->impl_endpoint),
      WP_OBJECT_FEATURES_ALL, NULL,
      (GAsyncReadyCallback) on_impl_endpoint_activated, transition);
}

static void
si_audio_endpoint_class_init (WpSiAudioEndpointClass * klass)
{
  WpSessionItemClass *si_class = (WpSessionItemClass *) klass;

  si_class->reset = si_audio_endpoint_reset;
  si_class->configure = si_audio_endpoint_configure;
  si_class->get_associated_proxy = si_audio_endpoint_get_associated_proxy;
  si_class->disable_active = si_audio_endpoint_disable_active;
  si_class->disable_exported = si_audio_endpoint_disable_exported;
  si_class->enable_active = si_audio_endpoint_enable_active;
  si_class->enable_exported = si_audio_endpoint_enable_exported;
}

static GVariant *
si_audio_endpoint_get_registration_info (WpSiEndpoint * item)
{
  WpSiAudioEndpoint *self = WP_SI_AUDIO_ENDPOINT (item);
  GVariantBuilder b;

  g_variant_builder_init (&b, G_VARIANT_TYPE ("(ssya{ss})"));
  g_variant_builder_add (&b, "s", self->name);
  g_variant_builder_add (&b, "s", self->media_class);
  g_variant_builder_add (&b, "y", self->direction);
  g_variant_builder_add (&b, "a{ss}", NULL);

  return g_variant_builder_end (&b);
}

static WpProperties *
si_audio_endpoint_get_properties (WpSiEndpoint * item)
{
  WpSiAudioEndpoint *self = WP_SI_AUDIO_ENDPOINT (item);
  WpProperties *result = wp_properties_new_empty ();

  wp_properties_set (result, "endpoint.name", self->name);
  wp_properties_setf (result, "endpoint.priority", "%u", self->priority);
  wp_properties_set (result, "endpoint.description", "Audio Endpoint");
  wp_properties_setf (result, PW_KEY_ENDPOINT_AUTOCONNECT, "%d", FALSE);
  wp_properties_set (result, PW_KEY_ENDPOINT_CLIENT_ID, NULL);

  /* associate with the node */
  wp_properties_setf (result, PW_KEY_NODE_ID, "%d",
      wp_proxy_get_bound_id (WP_PROXY (self->node)));

  return result;
}

static void
si_audio_endpoint_endpoint_init (WpSiEndpointInterface * iface)
{
  iface->get_registration_info = si_audio_endpoint_get_registration_info;
  iface->get_properties = si_audio_endpoint_get_properties;
}

static GVariant *
si_audio_endpoint_get_ports (WpSiPortInfo * item, const gchar * context)
{
  WpSiAudioEndpoint *self = WP_SI_AUDIO_ENDPOINT (item);
  g_auto (GVariantBuilder) b = G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE_ARRAY);
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) val = G_VALUE_INIT;
  WpDirection direction = self->direction;
  guint32 node_id;

  /* context can only be either NULL or "reverse" */
  if (!g_strcmp0 (context, "reverse")) {
    direction = (self->direction == WP_DIRECTION_INPUT) ?
        WP_DIRECTION_OUTPUT : WP_DIRECTION_INPUT;
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
    const gchar *channel;
    guint32 port_id, channel_id = 0;

    if (wp_port_get_direction (port) != direction)
      continue;

    port_id = wp_proxy_get_bound_id (WP_PROXY (port));

    /* try to find the audio channel; if channel is NULL, this will silently
       leave the channel_id to its default value, 0 */
    props = wp_pipewire_object_get_properties (WP_PIPEWIRE_OBJECT (port));
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
si_audio_endpoint_port_info_init (WpSiPortInfoInterface * iface)
{
  iface->get_ports = si_audio_endpoint_get_ports;
}

WP_PLUGIN_EXPORT gboolean
wireplumber__module_init (WpCore * core, GVariant * args, GError ** error)
{
  wp_si_factory_register (core, wp_si_factory_new_simple (SI_FACTORY_NAME,
      si_audio_endpoint_get_type ()));
  return TRUE;
}
