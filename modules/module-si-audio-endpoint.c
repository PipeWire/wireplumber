/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>
#include <pipewire/pipewire.h>
#include <spa/param/format.h>
#include <spa/param/audio/raw.h>
#include <spa/param/param.h>

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
  gboolean disable_dsp;

  /* activation */
  WpNode *node;
  WpSiAdapter *adapter;
};

static void si_audio_endpoint_endpoint_init (WpSiEndpointInterface * iface);
static void si_audio_endpoint_linkable_init (WpSiLinkableInterface * iface);
static void si_audio_endpoint_adapter_init (WpSiAdapterInterface * iface);

G_DECLARE_FINAL_TYPE(WpSiAudioEndpoint, si_audio_endpoint, WP,
    SI_AUDIO_ENDPOINT, WpSessionItem)
G_DEFINE_TYPE_WITH_CODE (WpSiAudioEndpoint, si_audio_endpoint,
    WP_TYPE_SESSION_ITEM,
    G_IMPLEMENT_INTERFACE (WP_TYPE_SI_ENDPOINT, si_audio_endpoint_endpoint_init)
    G_IMPLEMENT_INTERFACE (WP_TYPE_SI_LINKABLE,
        si_audio_endpoint_linkable_init)
    G_IMPLEMENT_INTERFACE (WP_TYPE_SI_ADAPTER, si_audio_endpoint_adapter_init))

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
  self->media_class[0] = '\0';
  self->direction = WP_DIRECTION_INPUT;
  self->role[0] = '\0';
  self->priority = 0;
  self->disable_dsp = FALSE;

  WP_SESSION_ITEM_CLASS (si_audio_endpoint_parent_class)->reset (item);
}

static gboolean
si_audio_endpoint_configure (WpSessionItem * item, WpProperties *p)
{
  WpSiAudioEndpoint *self = WP_SI_AUDIO_ENDPOINT (item);
  g_autoptr (WpProperties) si_props = wp_properties_ensure_unique_owner (p);
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

  str = wp_properties_get (si_props, "item.features.no-dsp");
  self->disable_dsp = str && pw_properties_parse_bool (str);

  wp_properties_set (si_props, "item.factory.name", SI_FACTORY_NAME);
  wp_session_item_set_properties (WP_SESSION_ITEM (self),
      g_steal_pointer (&si_props));
  return TRUE;
}

static gpointer
si_audio_endpoint_get_associated_proxy (WpSessionItem * item, GType proxy_type)
{
  WpSiAudioEndpoint *self = WP_SI_AUDIO_ENDPOINT (item);

  return wp_session_item_get_associated_proxy (
      WP_SESSION_ITEM (self->adapter), proxy_type);
}

static void
si_audio_endpoint_disable_active (WpSessionItem *si)
{
  WpSiAudioEndpoint *self = WP_SI_AUDIO_ENDPOINT (si);

  g_clear_object (&self->adapter);
  g_clear_object (&self->node);
  wp_object_update_features (WP_OBJECT (self), 0,
      WP_SESSION_ITEM_FEATURE_ACTIVE);
}

static void
si_audio_endpoint_disable_exported (WpSessionItem *si)
{
  WpSiAudioEndpoint *self = WP_SI_AUDIO_ENDPOINT (si);

  wp_object_update_features (WP_OBJECT (self), 0,
      WP_SESSION_ITEM_FEATURE_EXPORTED);
}

static void
on_adapter_activate_done (WpObject * adapter, GAsyncResult * res,
    WpTransition * transition)
{
  WpSiAudioEndpoint *self = wp_transition_get_source_object (transition);
  g_autoptr (GError) error = NULL;

  if (!wp_object_activate_finish (adapter, res, &error)) {
    wp_transition_return_error (transition, g_steal_pointer (&error));
    return;
  }

  wp_object_update_features (WP_OBJECT (self),
      WP_SESSION_ITEM_FEATURE_ACTIVE, 0);
}

static void
on_adapter_port_state_changed (WpSiAdapter *item,
    WpSiAdapterPortsState old_state, WpSiAdapterPortsState new_state,
    WpSiAudioEndpoint *self)
{
  g_signal_emit_by_name (self, "adapter-ports-state-changed", old_state,
      new_state);
}

static void
on_node_activate_done (WpObject * node, GAsyncResult * res,
    WpTransition * transition)
{
  WpSiAudioEndpoint *self = wp_transition_get_source_object (transition);
  g_autoptr (GError) error = NULL;
  g_autoptr (WpCore) core = NULL;
  g_autoptr (WpProperties) props = NULL;

  if (!wp_object_activate_finish (node, res, &error)) {
    wp_transition_return_error (transition, g_steal_pointer (&error));
    return;
  }

  /* create adapter */
  core = wp_object_get_core (WP_OBJECT (self));
  self->adapter = WP_SI_ADAPTER (wp_session_item_make (core,
      "si-audio-adapter"));
  if (!self->adapter) {
    wp_transition_return_error (transition,
        g_error_new (WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVARIANT,
            "si-audio-endpoint: could not create si-audio-adapter"));
  }

  /* Forward adapter-ports-state-changed signal */
  g_signal_connect_object (self->adapter, "adapter-ports-state-changed",
      G_CALLBACK (on_adapter_port_state_changed), self, 0);

  /* configure adapter */
  props = wp_properties_new_empty ();
  wp_properties_setf (props, "item.node", "%p", node);
  wp_properties_set (props, "name", self->name);
  wp_properties_set (props, "media.class", "Audio/Sink");
  wp_properties_set (props, "item.features.no-format", "true");
  wp_properties_set (props, "item.features.monitor", "true");
  if (self->disable_dsp)
    wp_properties_set (props, "item.features.no-dsp", "true");
  if (!wp_session_item_configure (WP_SESSION_ITEM (self->adapter),
      g_steal_pointer (&props))) {
    wp_transition_return_error (transition,
        g_error_new (WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVARIANT,
            "si-audio-endpoint: could not configure si-audio-adapter"));
  }

  /* activate adapter */
  wp_object_activate (WP_OBJECT (self->adapter), WP_SESSION_ITEM_FEATURE_ACTIVE,
      NULL, (GAsyncReadyCallback) on_adapter_activate_done, transition);
}

static void
si_audio_endpoint_enable_active (WpSessionItem *si, WpTransition *transition)
{
  WpSiAudioEndpoint *self = WP_SI_AUDIO_ENDPOINT (si);
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));
  g_autofree gchar *name = g_strdup_printf ("control.%s", self->name);
  g_autofree gchar *desc = g_strdup_printf ("%s %s Endpoint", self->role,
      (self->direction == WP_DIRECTION_OUTPUT) ? "Capture" : "Playback");
  g_autofree gchar *media = g_strdup_printf ("Audio/%s",
      (self->direction == WP_DIRECTION_OUTPUT) ? "Source" : "Sink");

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
          PW_KEY_MEDIA_CLASS, media,
          PW_KEY_FACTORY_NAME, "support.null-audio-sink",
          PW_KEY_NODE_DESCRIPTION, desc,
          "monitor.channel-volumes", "true",
          "wireplumber.is-endpoint", "true",
          NULL));
  if (!self->node) {
    wp_transition_return_error (transition,
        g_error_new (WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVARIANT,
            "si-audio-endpoint: could not create null-audio-sink node"));
    return;
  }

  /* activate node */
  wp_object_activate (WP_OBJECT (self->node),
      WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL | WP_NODE_FEATURE_PORTS, NULL,
      (GAsyncReadyCallback) on_node_activate_done, transition);
}

static void
si_audio_endpoint_enable_exported (WpSessionItem *si, WpTransition *transition)
{
  WpSiAudioEndpoint *self = WP_SI_AUDIO_ENDPOINT (si);

  wp_object_update_features (WP_OBJECT (self),
      WP_SESSION_ITEM_FEATURE_EXPORTED, 0);
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
  g_autoptr (WpNode) node = wp_session_item_get_associated_proxy (
      WP_SESSION_ITEM (self->adapter), WP_TYPE_NODE);
  WpProperties *result = wp_properties_new_empty ();

  wp_properties_set (result, "endpoint.name", self->name);
  wp_properties_setf (result, "endpoint.priority", "%u", self->priority);
  wp_properties_setf (result, "endpoint.description", "%s: %s",
      (self->direction == WP_DIRECTION_OUTPUT) ? "Capture" : "Playback",
      self->role);
  wp_properties_set (result, "media.role", self->role);

  /* associate with the node */
  g_return_val_if_fail (node, NULL);
  wp_properties_setf (result, PW_KEY_NODE_ID, "%d",
      wp_proxy_get_bound_id (WP_PROXY (node)));

  return result;
}

static void
si_audio_endpoint_endpoint_init (WpSiEndpointInterface * iface)
{
  iface->get_registration_info = si_audio_endpoint_get_registration_info;
  iface->get_properties = si_audio_endpoint_get_properties;
}

static GVariant *
si_audio_endpoint_get_ports (WpSiLinkable * item, const gchar * context)
{
  WpSiAudioEndpoint *self = WP_SI_AUDIO_ENDPOINT (item);
  return wp_si_linkable_get_ports (WP_SI_LINKABLE (self->adapter), context);
}

static void
si_audio_endpoint_linkable_init (WpSiLinkableInterface * iface)
{
  iface->get_ports = si_audio_endpoint_get_ports;
}

static WpSiAdapterPortsState
si_audio_endpoint_get_ports_state (WpSiAdapter * item)
{
  WpSiAudioEndpoint *self = WP_SI_AUDIO_ENDPOINT (item);
  return wp_si_adapter_get_ports_state (self->adapter);
}

static WpSpaPod *
si_audio_endpoint_get_ports_format (WpSiAdapter * item, const gchar **mode)
{
  WpSiAudioEndpoint *self = WP_SI_AUDIO_ENDPOINT (item);
  return wp_si_adapter_get_ports_format (self->adapter, mode);
}

static void
si_audio_endpoint_set_ports_format (WpSiAdapter * item, WpSpaPod *f,
    const gchar *mode, GAsyncReadyCallback callback, gpointer data)
{
  WpSiAudioEndpoint *self = WP_SI_AUDIO_ENDPOINT (item);
  wp_si_adapter_set_ports_format (self->adapter, f, mode, callback, data);
}

static gboolean
si_audio_endpoint_set_ports_format_finish (WpSiAdapter * item,
    GAsyncResult * res, GError ** error)
{
  WpSiAudioEndpoint *self = WP_SI_AUDIO_ENDPOINT (item);
  return wp_si_adapter_set_ports_format_finish (self->adapter, res, error);
}

static void
si_audio_endpoint_adapter_init (WpSiAdapterInterface * iface)
{
  iface->get_ports_state = si_audio_endpoint_get_ports_state;
  iface->get_ports_format = si_audio_endpoint_get_ports_format;
  iface->set_ports_format = si_audio_endpoint_set_ports_format;
  iface->set_ports_format_finish = si_audio_endpoint_set_ports_format_finish;
}

WP_PLUGIN_EXPORT gboolean
wireplumber__module_init (WpCore * core, GVariant * args, GError ** error)
{
  wp_si_factory_register (core, wp_si_factory_new_simple (SI_FACTORY_NAME,
      si_audio_endpoint_get_type ()));
  return TRUE;
}
