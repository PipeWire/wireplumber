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
  WpSpaPod *format;
  gchar mode[32];
  GTask *format_task;

  /* activation */
  WpNode *node;

  /* export */
  WpImplEndpoint *impl_endpoint;
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
  self->direction = WP_DIRECTION_INPUT;
  self->role[0] = '\0';
  self->priority = 0;
  if (self->format_task) {
    g_autoptr (GTask) t = g_steal_pointer (&self->format_task);
    g_task_return_new_error (t, WP_DOMAIN_LIBRARY,
        WP_LIBRARY_ERROR_OPERATION_FAILED,
        "item deactivated before format set");
  }
  g_clear_pointer (&self->format, wp_spa_pod_unref);
  self->mode[0] = '\0';

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

  wp_properties_set (si_props, "si.factory.name", SI_FACTORY_NAME);
  wp_properties_setf (si_props, "is.device", "%u", FALSE);
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
  else if (proxy_type == WP_TYPE_ENDPOINT)
    return self->impl_endpoint ? g_object_ref (self->impl_endpoint) : NULL;

  return NULL;
}

static void
si_audio_endpoint_disable_active (WpSessionItem *si)
{
  WpSiAudioEndpoint *self = WP_SI_AUDIO_ENDPOINT (si);

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
  g_autoptr (GError) error = NULL;

  if (!wp_object_activate_finish (node, res, &error)) {
    wp_transition_return_error (transition, g_steal_pointer (&error));
    return;
  }

  wp_object_update_features (WP_OBJECT (self),
      WP_SESSION_ITEM_FEATURE_ACTIVE, 0);
}

static void
si_audio_endpoint_enable_active (WpSessionItem *si, WpTransition *transition)
{
  WpSiAudioEndpoint *self = WP_SI_AUDIO_ENDPOINT (si);
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));
  g_autofree gchar *name = g_strdup_printf ("control.%s", self->name);
  g_autofree gchar *desc = g_strdup_printf ("%s %s Endpoint", self->role,
      (self->direction == WP_DIRECTION_OUTPUT) ? "Capture" : "Playback");

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
          "monitor.channel-volumes", "true",
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
  wp_properties_setf (result, "endpoint.description", "%s: %s",
      (self->direction == WP_DIRECTION_OUTPUT) ? "Capture" : "Playback",
      self->role);
  wp_properties_set (result, "media.role", self->role);

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
si_audio_endpoint_get_ports (WpSiLinkable * item, const gchar * context)
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
si_audio_endpoint_linkable_init (WpSiLinkableInterface * iface)
{
  iface->get_ports = si_audio_endpoint_get_ports;
}

static WpSpaPod *
si_audio_endpoint_get_ports_format (WpSiAdapter * item, const gchar **mode)
{
  WpSiAudioEndpoint *self = WP_SI_AUDIO_ENDPOINT (item);
  if (mode)
    *mode = self->mode;
  return self->format ? wp_spa_pod_ref (self->format) : NULL;
}

static void
on_sync_done (WpCore * core, GAsyncResult * res, WpSiAudioEndpoint *self)
{
  g_autoptr (GError) error = NULL;
  guint32 active = 0;

  if (!wp_core_sync_finish (core, res, &error)) {
    g_autoptr (GTask) t = g_steal_pointer (&self->format_task);
    g_task_return_error (t, g_steal_pointer (&error));
    return;
  }

  active = wp_object_get_active_features (WP_OBJECT (self->node));
  if (!(active & WP_NODE_FEATURE_PORTS)) {
    g_autoptr (GTask) t = g_steal_pointer (&self->format_task);
    g_task_return_new_error (t, WP_DOMAIN_LIBRARY,
        WP_LIBRARY_ERROR_OPERATION_FAILED,
        "node feature ports is not enabled, aborting set format operation");
    return;
  }

  /* The task might be destroyed by set_ports_format before sync is finished.
   * The set_ports_format API returns a task error if there is a pending task
   * so we don't need to do anything here */
  if (!self->format_task)
    return;

  /* make sure ports are available */
  if (wp_node_get_n_ports (self->node) > 0) {
    g_autoptr (GTask) t = g_steal_pointer (&self->format_task);
    g_task_return_boolean (t, TRUE);
  } else {
    wp_core_sync (core, NULL, (GAsyncReadyCallback) on_sync_done, self);
  }
}

static gboolean
parse_adapter_format (WpSpaPod *format, gint *channels,
   WpSpaPod **position)
{
  g_autoptr (WpSpaPodParser) parser = NULL;
  guint32 t = 0, s = 0, f = 0;
  gint r = 0, c = 0;
  g_autoptr (WpSpaPod) p = NULL;

  g_return_val_if_fail (format, FALSE);
  parser = wp_spa_pod_parser_new_object (format, NULL);
  g_return_val_if_fail (parser, FALSE);

  if (!wp_spa_pod_parser_get (parser, "mediaType", "I", &t, NULL) ||
      !wp_spa_pod_parser_get (parser, "mediaSubtype", "I", &s, NULL) ||
      !wp_spa_pod_parser_get (parser, "format", "I", &f, NULL) ||
      !wp_spa_pod_parser_get (parser, "rate", "i", &r, NULL) ||
      !wp_spa_pod_parser_get (parser, "channels", "i", &c, NULL))
    return FALSE;

  /* position is optional */
  wp_spa_pod_parser_get (parser, "position", "P", &p, NULL);

  if (channels)
    *channels = c;
  if (position)
    *position = p ? wp_spa_pod_ref (p) : NULL;

  return TRUE;
}

static WpSpaPod *
build_adapter_format (WpSiAudioEndpoint * self, WpSpaPod *format)
{
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));
  g_autoptr (WpSpaPodBuilder) b = NULL;
  g_autoptr (WpProperties) props = NULL;
  const gchar *rate_str = NULL;
  gint channels = 2;
  g_autoptr (WpSpaPod) position = NULL;

  /* get the default clock rate */
  g_return_val_if_fail (core, NULL);
  props = wp_core_get_remote_properties (core);
  g_return_val_if_fail (props, NULL);
  rate_str = wp_properties_get (props, "default.clock.rate");

  /* get channels and position */
  if (format && !parse_adapter_format (format, &channels, &position))
    return NULL;
  if (!format) {
    g_autoptr (WpSpaPodBuilder) b = wp_spa_pod_builder_new_array ();
    wp_spa_pod_builder_add_id (b, SPA_AUDIO_CHANNEL_FL);
    wp_spa_pod_builder_add_id (b, SPA_AUDIO_CHANNEL_FR);
    position = wp_spa_pod_builder_end (b);
  }

  b = wp_spa_pod_builder_new_object ("Spa:Pod:Object:Param:Format", "Format");
  wp_spa_pod_builder_add_property (b, "mediaType");
  wp_spa_pod_builder_add_id (b, SPA_MEDIA_TYPE_audio);
  wp_spa_pod_builder_add_property (b, "mediaSubtype");
  wp_spa_pod_builder_add_id (b, SPA_MEDIA_SUBTYPE_raw);
  wp_spa_pod_builder_add_property (b, "format");
  wp_spa_pod_builder_add_id (b, SPA_AUDIO_FORMAT_F32P);
  wp_spa_pod_builder_add_property (b, "rate");
  wp_spa_pod_builder_add_int (b, rate_str ? atoi (rate_str) : 48000);
  wp_spa_pod_builder_add_property (b, "channels");
  wp_spa_pod_builder_add_int (b, channels);
  if (position) {
    wp_spa_pod_builder_add_property (b, "position");
    wp_spa_pod_builder_add_pod (b, position);
  }
  return wp_spa_pod_builder_end (b);
}

static void
si_audio_endpoint_set_ports_format (WpSiAdapter * item, WpSpaPod *format,
    const gchar *mode, GAsyncReadyCallback callback, gpointer data)
{
  WpSiAudioEndpoint *self = WP_SI_AUDIO_ENDPOINT (item);
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));
  g_autoptr (WpSpaPod) new_format = NULL;
  g_autoptr (WpSpaPod) pod = NULL;

  g_return_if_fail (core);

  /* cancel previous task if any */
  if (self->format_task) {
    g_autoptr (GTask) t = g_steal_pointer (&self->format_task);
    g_task_return_new_error (t, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVARIANT,
        "setting new format before previous done");
  }

  /* create the new task */
  g_return_if_fail (!self->format_task);
  self->format_task = g_task_new (self, NULL, callback, data);

  /* build new format */
  new_format = build_adapter_format (self, format);
  if (!new_format) {
    g_autoptr (GTask) t = g_steal_pointer (&self->format_task);
    g_task_return_new_error (t, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVARIANT,
        "failed to parse format");
    return;
  }

  /* set format and mode */
  g_clear_pointer (&self->format, wp_spa_pod_unref);
  self->format = g_steal_pointer (&new_format);
  strncpy (self->mode, mode ? mode : "dsp", sizeof (self->mode) - 1);

  /* configure DSP with chosen format */
  pod = wp_spa_pod_new_object (
      "Spa:Pod:Object:Param:PortConfig", "PortConfig",
      "direction",  "I", WP_DIRECTION_INPUT,
      "mode",       "K", self->mode,
      "monitor",    "b", TRUE,
      "format",     "P", self->format,
      NULL);
  wp_pipewire_object_set_param (WP_PIPEWIRE_OBJECT (self->node),
      "PortConfig", 0, pod);

  /* sync until new ports are available */
  wp_core_sync (core, NULL, (GAsyncReadyCallback) on_sync_done, self);
}

static gboolean
si_audio_endpoint_set_ports_format_finish (WpSiAdapter * item,
    GAsyncResult * res, GError ** error)
{
  return g_task_propagate_boolean (G_TASK (res), error);
}

static void
si_audio_endpoint_adapter_init (WpSiAdapterInterface * iface)
{
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
