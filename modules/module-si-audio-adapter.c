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

#include <spa/param/format.h>
#include <spa/param/audio/raw.h>
#include <spa/param/param.h>

#include "module-si-audio-adapter/audio-utils.h"

#define SI_FACTORY_NAME "si-audio-adapter"

struct _WpSiAudioAdapter
{
  WpSessionItem parent;

  /* configuration */
  WpNode *node;
  gchar name[96];
  gchar media_class[32];
  guint preferred_n_channels;
  gboolean control_port;
  gboolean monitor;
  WpDirection direction;

  /* activate */
  struct spa_audio_info_raw format;
};

static void si_audio_adapter_linkable_init (WpSiLinkableInterface * iface);

G_DECLARE_FINAL_TYPE(WpSiAudioAdapter, si_audio_adapter, WP, SI_AUDIO_ADAPTER,
    WpSessionItem)
G_DEFINE_TYPE_WITH_CODE (WpSiAudioAdapter, si_audio_adapter,
    WP_TYPE_SESSION_ITEM,
    G_IMPLEMENT_INTERFACE (WP_TYPE_SI_LINKABLE, si_audio_adapter_linkable_init))

static void
si_audio_adapter_init (WpSiAudioAdapter * self)
{
}

static void
si_audio_adapter_reset (WpSessionItem * item)
{
  WpSiAudioAdapter *self = WP_SI_AUDIO_ADAPTER (item);

  /* deactivate first */
  wp_object_deactivate (WP_OBJECT (self), WP_SESSION_ITEM_FEATURE_ACTIVE);

  /* reset */
  g_clear_object (&self->node);
  self->name[0] = '\0';
  self->media_class[0] = '\0';
  self->control_port = FALSE;
  self->monitor = FALSE;
  self->direction = WP_DIRECTION_INPUT;
  self->preferred_n_channels = 0;

  WP_SESSION_ITEM_CLASS (si_audio_adapter_parent_class)->reset (item);
}

static gboolean
si_audio_adapter_configure (WpSessionItem * item, WpProperties *p)
{
  WpSiAudioAdapter *self = WP_SI_AUDIO_ADAPTER (item);
  g_autoptr (WpProperties) si_props = wp_properties_ensure_unique_owner (p);
  WpNode *node = NULL;
  WpProperties *node_props = NULL;
  const gchar *str;

  /* reset previous config */
  si_audio_adapter_reset (item);

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

  if (strstr (self->media_class, "Source") ||
      strstr (self->media_class, "Output"))
    self->direction = WP_DIRECTION_OUTPUT;
  wp_properties_setf (si_props, "direction", "%u", self->direction);

  str = wp_properties_get (si_props, "preferred.n.channels");
  if (str && sscanf(str, "%u", &self->preferred_n_channels) != 1)
    return FALSE;
  if (!str)
    wp_properties_setf (si_props, "preferred.n.channels", "%u",
        self->preferred_n_channels);

  str = wp_properties_get (si_props, "enable.control.port");
  if (str && sscanf(str, "%u", &self->control_port) != 1)
    return FALSE;
  if (!str)
    wp_properties_setf (si_props, "enable.control.port", "%u",
        self->control_port);

  str = wp_properties_get (si_props, "enable.monitor");
  if (str && sscanf(str, "%u", &self->monitor) != 1)
    return FALSE;
  if (!str)
    wp_properties_setf (si_props, "enable.monitor", "%u", self->monitor);

  self->node = g_object_ref (node);

  wp_properties_set (si_props, "si.factory.name", SI_FACTORY_NAME);
  wp_session_item_set_properties (WP_SESSION_ITEM (self),
      g_steal_pointer (&si_props));
  return TRUE;
}

static gpointer
si_audio_adapter_get_associated_proxy (WpSessionItem * item, GType proxy_type)
{
  WpSiAudioAdapter *self = WP_SI_AUDIO_ADAPTER (item);

  if (proxy_type == WP_TYPE_NODE)
    return self->node ? g_object_ref (self->node) : NULL;

  return NULL;
}

static void
si_audio_adapter_disable_active (WpSessionItem *si)
{
  WpSiAudioAdapter *self = WP_SI_AUDIO_ADAPTER (si);

  self->format = (struct spa_audio_info_raw){ 0 };
  wp_object_update_features (WP_OBJECT (self), 0,
      WP_SESSION_ITEM_FEATURE_ACTIVE);
}

static void
on_sync_done (WpCore * core, GAsyncResult * res, WpTransition * transition)
{
  WpSiAudioAdapter *self = wp_transition_get_source_object (transition);
  g_autoptr (GError) error = NULL;
  guint32 active = 0;

  if (!wp_core_sync_finish (core, res, &error)) {
    wp_transition_return_error (transition, g_steal_pointer (&error));
    return;
  }

  active = wp_object_get_active_features (WP_OBJECT (self->node));
  if (!(active & WP_NODE_FEATURE_PORTS)) {
    wp_transition_return_error (transition,
        g_error_new (WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_OPERATION_FAILED,
            "node feature ports is not enabled, aborting activation"));
    return;
  }

  /* make sure ports are available */
  if (wp_node_get_n_ports (self->node) > 0)
      wp_object_update_features (WP_OBJECT (self),
          WP_SESSION_ITEM_FEATURE_ACTIVE, 0);
  else
      wp_core_sync (core, NULL, (GAsyncReadyCallback) on_sync_done, transition);
}

static void
on_feature_ports_ready (WpObject * node, GAsyncResult * res,
    WpTransition * transition)
{
  WpSiAudioAdapter *self = wp_transition_get_source_object (transition);
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));
  g_autoptr (GError) error = NULL;

  if (!wp_object_activate_finish (node, res, &error)) {
    wp_transition_return_error (transition, g_steal_pointer (&error));
    return;
  }

  wp_core_sync (core, NULL, (GAsyncReadyCallback) on_sync_done, transition);
}

static void
on_ports_configuration_done (WpCore * core, GAsyncResult * res,
    WpTransition * transition)
{
  WpSiAudioAdapter *self = wp_transition_get_source_object (transition);
  g_autoptr (GError) error = NULL;

  if (!wp_core_sync_finish (core, res, &error)) {
    wp_transition_return_error (transition, g_steal_pointer (&error));
    return;
  }

  wp_object_activate (WP_OBJECT (self->node), WP_NODE_FEATURE_PORTS, NULL,
      (GAsyncReadyCallback) on_feature_ports_ready, transition);
}

static WpSpaPod *
format_audio_raw_build (const struct spa_audio_info_raw *info)
{
  g_autoptr (WpSpaPodBuilder) builder = wp_spa_pod_builder_new_object (
      "Spa:Pod:Object:Param:Format", "Format");
  wp_spa_pod_builder_add (builder,
      "mediaType",    "K", "audio",
      "mediaSubtype", "K", "raw",
      "format",       "I", info->format,
      "rate",         "i", info->rate,
      "channels",     "i", info->channels,
      NULL);

   if (!SPA_FLAG_IS_SET (info->flags, SPA_AUDIO_FLAG_UNPOSITIONED)) {
     /* Build the position array spa pod */
     g_autoptr (WpSpaPodBuilder) position_builder = wp_spa_pod_builder_new_array ();
     for (guint i = 0; i < info->channels; i++)
       wp_spa_pod_builder_add_id (position_builder, info->position[i]);

     /* Add the position property */
     wp_spa_pod_builder_add_property (builder, "position");
     g_autoptr (WpSpaPod) position = wp_spa_pod_builder_end (position_builder);
     wp_spa_pod_builder_add_pod (builder, position);
   }

   return wp_spa_pod_builder_end (builder);
}

static void
si_audio_adapter_configure_ports (WpSiAudioAdapter *self, WpTransition * transition)
{
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));
  g_autoptr (WpSpaPod) format = NULL, port_format = NULL;
  g_autoptr (WpSpaPod) pod = NULL;

  /* set the chosen device/client format on the node */
  format = format_audio_raw_build (&self->format);
  wp_pipewire_object_set_param (WP_PIPEWIRE_OBJECT (self->node),
      "Format", 0, format);

  /* now choose the DSP format: keep the chanels but use F32 plannar @ 48K */
  self->format.format = SPA_AUDIO_FORMAT_F32P;
  self->format.rate = ({
    g_autoptr (WpProperties) props = wp_core_get_remote_properties (core);
    const gchar *rate_str = wp_properties_get (props, "default.clock.rate");
    rate_str ? atoi (rate_str) : 48000;
  });

  wp_debug_object (self, "format: F32P %uch @ %u", self->format.channels,
      self->format.rate);

  port_format = format_audio_raw_build (&self->format);
  pod = wp_spa_pod_new_object (
      "Spa:Pod:Object:Param:PortConfig", "PortConfig",
      "direction",  "I", self->direction,
      "mode",       "K", "dsp",
      "monitor",    "b", self->monitor,
      "control",    "b", self->control_port,
      "format",     "P", port_format,
      NULL);
  wp_pipewire_object_set_param (WP_PIPEWIRE_OBJECT (self->node),
      "PortConfig", 0, pod);

  wp_core_sync (core, NULL,
      (GAsyncReadyCallback) on_ports_configuration_done, transition);
}

static void
on_node_enum_format_done (WpPipewireObject * proxy, GAsyncResult * res,
    WpTransition * transition)
{
  WpSiAudioAdapter *self = wp_transition_get_source_object (transition);
  g_autoptr (WpIterator) formats = NULL;
  g_autoptr (GError) error = NULL;
  gint pref_chan;

  formats = wp_pipewire_object_enum_params_finish (proxy, res, &error);
  if (error) {
    wp_transition_return_error (transition, g_steal_pointer (&error));
    return;
  }

  /* 34 is the max number of channels that SPA knows about
     in the spa_audio_channel enum */
  pref_chan = self->preferred_n_channels ? self->preferred_n_channels : 34;

  if (!choose_sensible_raw_audio_format (formats, pref_chan, &self->format)) {
    wp_warning_object (self, "failed to choose a sensible audio format");
    wp_transition_return_error (transition,
        g_error_new (WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_OPERATION_FAILED,
            "failed to choose a sensible audio format"));
    return;
  }

  si_audio_adapter_configure_ports (self, transition);
}

static void
si_audio_adapter_enable_active (WpSessionItem *si, WpTransition *transition)
{
  WpSiAudioAdapter *self = WP_SI_AUDIO_ADAPTER (si);

  if (!wp_session_item_is_configured (si)) {
    wp_transition_return_error (transition,
        g_error_new (WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVARIANT,
        "si-audio-adapter: item is not configured"));
    return;
  }

  wp_pipewire_object_enum_params (WP_PIPEWIRE_OBJECT (self->node),
      "EnumFormat", NULL, NULL,
      (GAsyncReadyCallback) on_node_enum_format_done, transition);
}

static WpObjectFeatures
si_audio_adapter_get_supported_features (WpObject * self)
{
  return WP_SESSION_ITEM_FEATURE_ACTIVE;
}

static void
si_audio_adapter_class_init (WpSiAudioAdapterClass * klass)
{
  WpObjectClass * wpobject_class = (WpObjectClass *) klass;
  WpSessionItemClass *si_class = (WpSessionItemClass *) klass;

  wpobject_class->get_supported_features =
      si_audio_adapter_get_supported_features;

  si_class->reset = si_audio_adapter_reset;
  si_class->configure = si_audio_adapter_configure;
  si_class->get_associated_proxy = si_audio_adapter_get_associated_proxy;
  si_class->disable_active = si_audio_adapter_disable_active;
  si_class->enable_active = si_audio_adapter_enable_active;
}

static GVariant *
si_audio_adapter_get_ports (WpSiLinkable * item, const gchar * context)
{
  WpSiAudioAdapter *self = WP_SI_AUDIO_ADAPTER (item);
  g_auto (GVariantBuilder) b = G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE_ARRAY);
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) val = G_VALUE_INIT;
  WpDirection direction = self->direction;
  guint32 node_id;

  /* context can only be NULL or "reverse" */
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
    props = wp_pipewire_object_get_properties (WP_PIPEWIRE_OBJECT (port));

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
si_audio_adapter_linkable_init (WpSiLinkableInterface * iface)
{
  iface->get_ports = si_audio_adapter_get_ports;
}

WP_PLUGIN_EXPORT gboolean
wireplumber__module_init (WpCore * core, GVariant * args, GError ** error)
{
  wp_si_factory_register (core, wp_si_factory_new_simple (SI_FACTORY_NAME,
      si_audio_adapter_get_type ()));
  return TRUE;
}
