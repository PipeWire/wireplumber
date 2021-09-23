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
  gboolean control_port;
  gboolean monitor;
  WpDirection direction;
  WpDirection portconfig_direction;
  gboolean is_device;
  gboolean dont_remix;
  gboolean is_autoconnect;
  WpSpaPod *format;
  gchar mode[32];
  GTask *format_task;
};

static void si_audio_adapter_linkable_init (WpSiLinkableInterface * iface);
static void si_audio_adapter_adapter_init (WpSiAdapterInterface * iface);

G_DECLARE_FINAL_TYPE(WpSiAudioAdapter, si_audio_adapter, WP, SI_AUDIO_ADAPTER,
    WpSessionItem)
G_DEFINE_TYPE_WITH_CODE (WpSiAudioAdapter, si_audio_adapter,
    WP_TYPE_SESSION_ITEM,
    G_IMPLEMENT_INTERFACE (WP_TYPE_SI_LINKABLE, si_audio_adapter_linkable_init)
    G_IMPLEMENT_INTERFACE (WP_TYPE_SI_ADAPTER, si_audio_adapter_adapter_init))

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
  self->portconfig_direction = self->direction = WP_DIRECTION_INPUT;
  self->is_device = FALSE;
  self->dont_remix = FALSE;
  self->is_autoconnect = FALSE;
  if (self->format_task) {
    g_task_return_new_error (self->format_task, WP_DOMAIN_LIBRARY,
        WP_LIBRARY_ERROR_OPERATION_FAILED,
        "item deactivated before format set");
    g_clear_object (&self->format_task);
  }
  g_clear_pointer (&self->format, wp_spa_pod_unref);
  self->mode[0] = '\0';

  WP_SESSION_ITEM_CLASS (si_audio_adapter_parent_class)->reset (item);
}

static gboolean
si_audio_adapter_configure (WpSessionItem * item, WpProperties *p)
{
  WpSiAudioAdapter *self = WP_SI_AUDIO_ADAPTER (item);
  g_autoptr (WpProperties) si_props = wp_properties_ensure_unique_owner (p);
  WpNode *node = NULL;
  g_autoptr (WpProperties) node_props = NULL;
  const gchar *str;

  /* reset previous config */
  si_audio_adapter_reset (item);

  str = wp_properties_get (si_props, "node");
  if (!str || sscanf(str, "%p", &node) != 1 || !WP_IS_NODE (node))
    return FALSE;

  node_props = wp_pipewire_object_get_properties (WP_PIPEWIRE_OBJECT (node));

  str = wp_properties_get (node_props, PW_KEY_STREAM_DONT_REMIX);
  self->dont_remix = str && pw_properties_parse_bool (str);

  str = wp_properties_get (node_props, PW_KEY_NODE_AUTOCONNECT);
  self->is_autoconnect = str && pw_properties_parse_bool (str);

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
  self->is_device = strstr (self->media_class, "Stream") == NULL;

  if (strstr (self->media_class, "Source") ||
      strstr (self->media_class, "Output")) {
    self->direction = WP_DIRECTION_OUTPUT;
    self->portconfig_direction = (strstr (self->media_class, "Virtual")) ?
        WP_DIRECTION_INPUT : self->direction;
  }
  wp_properties_setf (si_props, "direction", "%u", self->direction);

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
  wp_properties_setf (si_props, "is.device", "%u", self->is_device);
  wp_properties_setf (si_props, "dont.remix", "%u", self->dont_remix);
  wp_properties_setf (si_props, "is.autoconnect", "%u", self->is_autoconnect);
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

  wp_object_deactivate (WP_OBJECT (self->node), WP_NODE_FEATURE_PORTS);

  wp_object_update_features (WP_OBJECT (self), 0,
      WP_SESSION_ITEM_FEATURE_ACTIVE);
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
build_adapter_format (WpSiAudioAdapter * self, guint32 format, gint channels,
    WpSpaPod *pos)
{
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));
  g_autoptr (WpSpaPod) position = pos;
  g_autoptr (WpProperties) props = NULL;
  g_autoptr (WpSpaPodBuilder) b = NULL;
  const gchar *rate_str = NULL;

  g_return_val_if_fail (channels > 0, NULL);

  /* get the default clock rate */
  g_return_val_if_fail (core, NULL);
  props = wp_core_get_remote_properties (core);
  g_return_val_if_fail (props, NULL);
  rate_str = wp_properties_get (props, "default.clock.rate");

  /* build the position array if not given */
  if (!position) {
    switch (channels) {
    case 1: {
      g_autoptr (WpSpaPodBuilder) pos_b = wp_spa_pod_builder_new_array ();
      wp_spa_pod_builder_add_id (pos_b, SPA_AUDIO_CHANNEL_MONO);
      position = wp_spa_pod_builder_end (pos_b);
      break;
    }
    case 2: {
      g_autoptr (WpSpaPodBuilder) pos_b = wp_spa_pod_builder_new_array ();
      wp_spa_pod_builder_add_id (pos_b, SPA_AUDIO_CHANNEL_FL);
      wp_spa_pod_builder_add_id (pos_b, SPA_AUDIO_CHANNEL_FR);
      position = wp_spa_pod_builder_end (pos_b);
      break;
    }
    default:
      break;
    }
  }

  /* build the format */
  b = wp_spa_pod_builder_new_object ("Spa:Pod:Object:Param:Format", "Format");
  wp_spa_pod_builder_add_property (b, "mediaType");
  wp_spa_pod_builder_add_id (b, SPA_MEDIA_TYPE_audio);
  wp_spa_pod_builder_add_property (b, "mediaSubtype");
  wp_spa_pod_builder_add_id (b, SPA_MEDIA_SUBTYPE_raw);
  wp_spa_pod_builder_add_property (b, "format");
  wp_spa_pod_builder_add_id (b, format);
  wp_spa_pod_builder_add_property (b, "rate");
  wp_spa_pod_builder_add_int (b, rate_str ? atoi (rate_str) : 48000);
  wp_spa_pod_builder_add_property (b, "channels");
  wp_spa_pod_builder_add_int (b, channels);
  if (position) {
    wp_spa_pod_builder_add_property (b, "position");
    wp_spa_pod_builder_add_pod (b, pos);
  }
  return wp_spa_pod_builder_end (b);
}

static WpSpaPod *
build_adapter_dsp_format (WpSiAudioAdapter * self, WpSpaPod *dev_format)
{
  g_autoptr (WpSpaPod) position = NULL;
  gint channels = 2;

  /* parse device format */
  if (dev_format && !parse_adapter_format (dev_format, &channels, &position))
    return NULL;

  /* build F32P with same channels and position as device format */
  return build_adapter_format (self, SPA_AUDIO_FORMAT_F32P, channels,
      g_steal_pointer (&position));
}

static WpSpaPod *
build_adapter_default_format (WpSiAudioAdapter * self, const gchar *mode)
{
  guint32 format = SPA_AUDIO_FORMAT_F32;

  /* if dsp, use plannar format */
  if (g_strcmp0 (mode, "dsp") == 0)
    format = SPA_AUDIO_FORMAT_F32P;

  return build_adapter_format (self, format, 2, NULL);
}

static void
on_format_set (GObject *obj, GAsyncResult * res, gpointer p)
{
  WpTransition *transition = p;
  WpSiAudioAdapter *self = wp_transition_get_source_object (transition);
  g_autoptr (GError) error = NULL;

  wp_si_adapter_set_ports_format_finish (WP_SI_ADAPTER (self), res, &error);
  if (error) {
    wp_transition_return_error (transition, g_steal_pointer (&error));
    return;
  }

  wp_object_update_features (WP_OBJECT (self),
      WP_SESSION_ITEM_FEATURE_ACTIVE, 0);
}

static void
on_node_enum_format_done (WpPipewireObject * proxy, GAsyncResult * res,
    WpTransition * transition)
{
  WpSiAudioAdapter *self = wp_transition_get_source_object (transition);
  g_autoptr (WpIterator) formats = NULL;
  g_autoptr (GError) error = NULL;
  g_autoptr (WpSpaPod) format = NULL;
  g_autoptr (WpSpaPod) ports_format = NULL;
  struct spa_audio_info_raw spa_format;

  formats = wp_pipewire_object_enum_params_finish (proxy, res, &error);
  if (error) {
    wp_transition_return_error (transition, g_steal_pointer (&error));
    return;
  }

  /* chose sensible format */
  if (!choose_sensible_raw_audio_format (formats, 34, &spa_format)) {
    wp_transition_return_error (transition,
        g_error_new (WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_OPERATION_FAILED,
            "failed to choose a sensible audio format"));
    return;
  }

  /* set the chosen format on the node */
  format = format_audio_raw_build (&spa_format);
  wp_pipewire_object_set_param (WP_PIPEWIRE_OBJECT (self->node), "Format", 0,
      wp_spa_pod_ref (format));

  /* build the ports format */
  ports_format = build_adapter_dsp_format (self, format);
  if (!ports_format) {
      wp_transition_return_error (transition,
        g_error_new (WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_OPERATION_FAILED,
            "failed to build ports format"));
    return;
  }

  /* set chosen format in the ports */
  wp_si_adapter_set_ports_format (WP_SI_ADAPTER (self),
      wp_spa_pod_ref (ports_format), "dsp", on_format_set, transition);
}

static void
on_node_ports_changed (WpObject * node, WpSiAudioAdapter *self)
{
  /* finish the task started by _set_ports_format() */
  if (self->format_task && wp_node_get_n_ports (self->node) > 0) {
    g_autoptr (GTask) t = g_steal_pointer (&self->format_task);
    g_task_return_boolean (t, TRUE);
  }
}

static void
on_feature_ports_ready (WpObject * node, GAsyncResult * res,
    WpTransition * transition)
{
  WpSiAudioAdapter *self = wp_transition_get_source_object (transition);
  g_autoptr (GError) error = NULL;

  if (!wp_object_activate_finish (node, res, &error)) {
    wp_transition_return_error (transition, g_steal_pointer (&error));
    return;
  }

  g_signal_connect_object (node, "ports-changed",
      (GCallback) on_node_ports_changed, self, 0);

  /* If device node, enum available formats and set one of them */
  if (self->is_device || self->dont_remix || !self->is_autoconnect)
    wp_pipewire_object_enum_params (WP_PIPEWIRE_OBJECT (self->node),
        "EnumFormat", NULL, NULL,
        (GAsyncReadyCallback) on_node_enum_format_done, transition);

  /* Otherwise just finish activating */
  else
    wp_object_update_features (WP_OBJECT (self),
          WP_SESSION_ITEM_FEATURE_ACTIVE, 0);
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

  if (!(wp_object_get_active_features (WP_OBJECT (self->node))
    & WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL)) {
    wp_transition_return_error (transition,
        g_error_new (WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVARIANT,
        "si-audio-adapter: node minimal feature not enabled"));
    return;
  }

  /* enable ports feature */
  wp_object_activate (WP_OBJECT (self->node), WP_NODE_FEATURE_PORTS,
      NULL, (GAsyncReadyCallback) on_feature_ports_ready, transition);
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

static WpSpaPod *
si_audio_adapter_get_ports_format (WpSiAdapter * item, const gchar **mode)
{
  WpSiAudioAdapter *self = WP_SI_AUDIO_ADAPTER (item);
  if (mode)
    *mode = self->mode;
  return self->format ? wp_spa_pod_ref (self->format) : NULL;
}

static void
si_audio_adapter_set_ports_format (WpSiAdapter * item, WpSpaPod *f,
    const gchar *mode, GAsyncReadyCallback callback, gpointer data)
{
  WpSiAudioAdapter *self = WP_SI_AUDIO_ADAPTER (item);
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));
  g_autoptr (WpSpaPod) format = f;
  guint32 active = 0;

  g_return_if_fail (core);

  /* cancel previous task if any */
  if (self->format_task) {
    g_autoptr (GTask) t = g_steal_pointer (&self->format_task);
    g_task_return_new_error (t, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVARIANT,
        "setting new format before previous done");
  }

  /* build default format if NULL was given */
  if (!format) {
    format = build_adapter_default_format (self, mode);
    g_return_if_fail (format);
  }

  /* create the new task */
  g_return_if_fail (!self->format_task);
  self->format_task = g_task_new (self, NULL, callback, data);

  active = wp_object_get_active_features (WP_OBJECT (self->node));
  if (G_UNLIKELY (!(active & WP_NODE_FEATURE_PORTS))) {
    g_autoptr (GTask) t = g_steal_pointer (&self->format_task);
    g_task_return_new_error (t, WP_DOMAIN_LIBRARY,
        WP_LIBRARY_ERROR_OPERATION_FAILED,
        "node feature ports is not enabled, aborting set format operation");
    return;
  }

  /* set format and mode */
  g_clear_pointer (&self->format, wp_spa_pod_unref);
  self->format = g_steal_pointer (&format);
  strncpy (self->mode, mode ? mode : "dsp", sizeof (self->mode) - 1);

  /* configure DSP with chosen format */
  wp_pipewire_object_set_param (WP_PIPEWIRE_OBJECT (self->node),
      "PortConfig", 0, wp_spa_pod_new_object (
          "Spa:Pod:Object:Param:PortConfig", "PortConfig",
          "direction",  "I", self->portconfig_direction,
          "mode",       "K", self->mode,
          "monitor",    "b", self->monitor,
          "control",    "b", self->control_port,
          "format",     "P", self->format,
          NULL));

  /* the task finishes with new ports being emitted -> on_node_ports_changed */
}

static gboolean
si_audio_adapter_set_ports_format_finish (WpSiAdapter * item,
    GAsyncResult * res, GError ** error)
{
  return g_task_propagate_boolean (G_TASK (res), error);
}

static void
si_audio_adapter_adapter_init (WpSiAdapterInterface * iface)
{
  iface->get_ports_format = si_audio_adapter_get_ports_format;
  iface->set_ports_format = si_audio_adapter_set_ports_format;
  iface->set_ports_format_finish = si_audio_adapter_set_ports_format_finish;
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
