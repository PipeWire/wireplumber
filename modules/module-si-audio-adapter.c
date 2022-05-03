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
#include <spa/param/audio/format-utils.h>
#include <spa/param/param.h>

#define SI_FACTORY_NAME "si-audio-adapter"

struct _WpSiAudioAdapter
{
  WpSessionItem parent;

  /* configuration */
  WpNode *node;
  WpPort *port;  /* only used for passthrough or convert mode */
  gboolean no_format;
  gboolean control_port;
  gboolean monitor;
  gboolean disable_dsp;
  WpDirection portconfig_direction;
  gboolean is_device;
  gboolean dont_remix;
  gboolean is_autoconnect;
  gboolean have_encoded;
  gboolean encoded_only;
  gboolean is_unpositioned;
  struct spa_audio_info_raw raw_format;

  gulong ports_changed_sigid;

  WpSpaPod *format;
  gchar mode[32];
  GTask *format_task;
  WpSiAdapterPortsState ports_state;
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
si_audio_adapter_set_ports_state (WpSiAudioAdapter *self, WpSiAdapterPortsState
    new_state)
{
  if (self->ports_state != new_state) {
    WpSiAdapterPortsState old_state = self->ports_state;
    self->ports_state = new_state;
    g_signal_emit_by_name (self, "adapter-ports-state-changed", old_state,
        new_state);
  }
}

static void
si_audio_adapter_reset (WpSessionItem * item)
{
  WpSiAudioAdapter *self = WP_SI_AUDIO_ADAPTER (item);

  /* deactivate first */
  wp_object_deactivate (WP_OBJECT (self), WP_SESSION_ITEM_FEATURE_ACTIVE);

  /* reset */
  g_clear_object (&self->node);
  g_clear_object (&self->port);
  self->no_format = FALSE;
  self->control_port = FALSE;
  self->monitor = FALSE;
  self->disable_dsp = FALSE;
  self->portconfig_direction = WP_DIRECTION_INPUT;
  self->is_device = FALSE;
  self->dont_remix = FALSE;
  self->is_autoconnect = FALSE;
  self->have_encoded = FALSE;
  self->encoded_only = FALSE;
  spa_memzero (&self->raw_format, sizeof(struct spa_audio_info_raw));
  if (self->format_task) {
    g_task_return_new_error (self->format_task, WP_DOMAIN_LIBRARY,
        WP_LIBRARY_ERROR_OPERATION_FAILED,
        "item deactivated before format set");
    g_clear_object (&self->format_task);
  }
  g_clear_pointer (&self->format, wp_spa_pod_unref);
  self->mode[0] = '\0';
  si_audio_adapter_set_ports_state (self, WP_SI_ADAPTER_PORTS_STATE_NONE);

  WP_SESSION_ITEM_CLASS (si_audio_adapter_parent_class)->reset (item);
}

static guint
si_audio_adapter_get_default_clock_rate (WpSiAudioAdapter * self)
{
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));
  g_autoptr (WpProperties) props = NULL;
  const gchar *rate_str = NULL;
  g_return_val_if_fail (core, 48000);
  props = wp_core_get_remote_properties (core);
  g_return_val_if_fail (props, 48000);
  rate_str = wp_properties_get (props, "default.clock.rate");
  return rate_str ? atoi (rate_str) : 48000;
}

static gboolean
is_unpositioned (struct spa_audio_info_raw *info)
{
  uint32_t i;
  if (SPA_FLAG_IS_SET(info->flags, SPA_AUDIO_FLAG_UNPOSITIONED))
    return TRUE;
  for (i = 0; i < info->channels; i++)
    if (info->position[i] >= SPA_AUDIO_CHANNEL_START_Aux &&
        info->position[i] <= SPA_AUDIO_CHANNEL_LAST_Aux)
      return TRUE;
  return FALSE;
}

static gboolean
si_audio_adapter_find_format (WpSiAudioAdapter * self, WpNode * node)
{
  g_autoptr (WpIterator) formats = NULL;
  g_auto (GValue) value = G_VALUE_INIT;
  gboolean have_format = FALSE;

  formats = wp_pipewire_object_enum_params_sync (WP_PIPEWIRE_OBJECT (node),
      "EnumFormat", NULL);
  if (!formats)
    return FALSE;

  for (; wp_iterator_next (formats, &value); g_value_unset (&value)) {
    WpSpaPod *pod = g_value_get_boxed (&value);
    uint32_t mtype, msubtype;

    if (!wp_spa_pod_is_object (pod)) {
      wp_warning_object (self,
          "non-object POD appeared on formats list; this node is buggy");
      continue;
    }

    if (!wp_spa_pod_get_object (pod, NULL,
        "mediaType", "I", &mtype,
        "mediaSubtype", "I", &msubtype,
        NULL)) {
      wp_warning_object (self, "format does not have media type / subtype");
      continue;
    }

    if (mtype != SPA_MEDIA_TYPE_audio)
      continue;

    switch (msubtype) {
    case SPA_MEDIA_SUBTYPE_raw: {
      struct spa_audio_info_raw raw_format;
      struct spa_pod *position = NULL;
      wp_spa_pod_fixate (pod);

      spa_zero(raw_format);
      if (spa_pod_parse_object(wp_spa_pod_get_spa_pod (pod),
                               SPA_TYPE_OBJECT_Format, NULL,
                               SPA_FORMAT_AUDIO_format,   SPA_POD_OPT_Id(&raw_format.format),
                               SPA_FORMAT_AUDIO_rate,     SPA_POD_OPT_Int(&raw_format.rate),
                               SPA_FORMAT_AUDIO_channels, SPA_POD_OPT_Int(&raw_format.channels),
                               SPA_FORMAT_AUDIO_position, SPA_POD_OPT_Pod(&position)) < 0)
        continue;

      if (position == NULL ||
          !spa_pod_copy_array(position, SPA_TYPE_Id, raw_format.position, SPA_AUDIO_MAX_CHANNELS))
        SPA_FLAG_SET(raw_format.flags, SPA_AUDIO_FLAG_UNPOSITIONED);

      if (self->raw_format.channels < raw_format.channels) {
        self->raw_format = raw_format;
        if (is_unpositioned(&raw_format))
          self->is_unpositioned = TRUE;
      }
      have_format = TRUE;
      break;
    }
    case SPA_MEDIA_SUBTYPE_iec958:
    case SPA_MEDIA_SUBTYPE_dsd:
      wp_info_object (self, "passthrough IEC958/DSD node %d found",
          wp_proxy_get_bound_id (WP_PROXY (node)));
      self->have_encoded = TRUE;
      break;
    default:
      break;
    }
  }
  if (!have_format && self->have_encoded) {
    wp_info_object (self, ".. passthrough IEC958/DSD only");
    self->encoded_only = TRUE;
    have_format = TRUE;
  }

  return have_format;
}

static void
on_proxy_destroyed (WpNode * proxy, WpSiAudioAdapter * self)
{
  if (self->node == proxy) {
    wp_object_abort_activation (WP_OBJECT (self), "proxy destroyed");
    si_audio_adapter_reset (WP_SESSION_ITEM (self));
  }
}

static gboolean
si_audio_adapter_configure (WpSessionItem * item, WpProperties *p)
{
  WpSiAudioAdapter *self = WP_SI_AUDIO_ADAPTER (item);
  g_autoptr (WpProperties) si_props = wp_properties_ensure_unique_owner (p);
  WpNode *node = NULL;
  const gchar *str;

  /* reset previous config */
  si_audio_adapter_reset (item);

  str = wp_properties_get (si_props, "item.node");
  if (!str || sscanf(str, "%p", &node) != 1 || !WP_IS_NODE (node))
    return FALSE;

  str = wp_properties_get (si_props, PW_KEY_MEDIA_CLASS);
  if (!str)
    return FALSE;
  if ((strstr (str, "Source") || strstr (str, "Output"))
        && !strstr (str, "Virtual")) {
    self->portconfig_direction = WP_DIRECTION_OUTPUT;
  }

  str = wp_properties_get (si_props, "item.features.no-format");
  self->no_format = str && pw_properties_parse_bool (str);
  if (!self->no_format && !si_audio_adapter_find_format (self, node)) {
    wp_message_object (item, "no usable format found for node %d",
        wp_proxy_get_bound_id (WP_PROXY (node)));
    return FALSE;
  }

  str = wp_properties_get (si_props, "item.features.control-port");
  self->control_port = str && pw_properties_parse_bool (str);

  str = wp_properties_get (si_props, "item.features.monitor");
  self->monitor = str && pw_properties_parse_bool (str);

  str = wp_properties_get (si_props, "item.features.no-dsp");
  self->disable_dsp = str && pw_properties_parse_bool (str);

  str = wp_properties_get (si_props, "item.node.type");
  self->is_device = !g_strcmp0 (str, "device");

  str = wp_properties_get (si_props, PW_KEY_STREAM_DONT_REMIX);
  self->dont_remix = str && pw_properties_parse_bool (str);

  str = wp_properties_get (si_props, PW_KEY_NODE_AUTOCONNECT);
  self->is_autoconnect = str && pw_properties_parse_bool (str);

  self->node = g_object_ref (node);
  g_signal_connect_object (self->node, "pw-proxy-destroyed",
      G_CALLBACK (on_proxy_destroyed), self, 0);

  wp_properties_set (si_props, "item.node.supports-encoded-fmts",
      self->have_encoded ? "true" : "false");

  wp_properties_set (si_props, "item.node.encoded-only",
      self->encoded_only ? "true" : "false");

  wp_properties_set (si_props, "item.node.unpositioned",
      self->is_unpositioned ? "true" : "false");

  wp_properties_set (si_props, "item.factory.name", SI_FACTORY_NAME);
  wp_session_item_set_properties (item, g_steal_pointer (&si_props));
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
  g_autoptr (WpSpaPod) position = pos;
  g_autoptr (WpSpaPodBuilder) b = NULL;

  g_return_val_if_fail (channels > 0, NULL);

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
  wp_spa_pod_builder_add_int (b, si_audio_adapter_get_default_clock_rate (self));
  wp_spa_pod_builder_add_property (b, "channels");
  wp_spa_pod_builder_add_int (b, channels);
  if (position) {
    wp_spa_pod_builder_add_property (b, "position");
    wp_spa_pod_builder_add_pod (b, position);
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
  if (!mode || g_strcmp0 (mode, "dsp") == 0)
    format = SPA_AUDIO_FORMAT_F32P;

  return build_adapter_format (self, format, 2, NULL);
}

static void
on_format_set (GObject *obj, GAsyncResult * res, gpointer p)
{
  g_autoptr(WpTransition) transition = p;
  WpSiAudioAdapter *self = wp_transition_get_source_object (transition);
  g_autoptr (GError) error = NULL;

  if (wp_transition_get_completed (transition))
    return;

  wp_si_adapter_set_ports_format_finish (WP_SI_ADAPTER (self), res, &error);
  if (error) {
    wp_transition_return_error (transition, g_steal_pointer (&error));
    return;
  }

  wp_object_update_features (WP_OBJECT (self),
      WP_SESSION_ITEM_FEATURE_ACTIVE, 0);
}

static void
si_audio_adapter_configure_node (WpSiAudioAdapter *self,
    WpTransition * transition)
{
  g_autoptr (WpSpaPod) format = NULL;
  g_autoptr (WpSpaPod) ports_format = NULL;
  const gchar *mode = NULL;

  /* set the chosen format on the node */
  format = format_audio_raw_build (&self->raw_format);
  wp_pipewire_object_set_param (WP_PIPEWIRE_OBJECT (self->node), "Format", 0,
      wp_spa_pod_ref (format));

  /* build the ports format */
  if (self->disable_dsp) {
    mode = "passthrough";
    ports_format = g_steal_pointer (&format);
  } else {
    mode = "dsp";
    ports_format = build_adapter_dsp_format (self, format);
    if (!ports_format) {
        wp_transition_return_error (transition,
          g_error_new (WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_OPERATION_FAILED,
              "failed to build ports format"));
      return;
    }
  }

  /* set chosen format in the ports */
  wp_si_adapter_set_ports_format (WP_SI_ADAPTER (self),
      g_steal_pointer (&ports_format), mode, on_format_set, g_object_ref (transition));
}

static void
on_port_param_info (WpPipewireObject * port, GParamSpec * param,
    WpSiAudioAdapter *self)
{
  /* finish the task started by _set_ports_format() */
  if (self->format_task) {
    g_autoptr (GTask) t = g_steal_pointer (&self->format_task);
    si_audio_adapter_set_ports_state (self,
        WP_SI_ADAPTER_PORTS_STATE_CONFIGURED);
    g_task_return_boolean (t, TRUE);
  }
}

static void
on_node_ports_changed (WpObject * node, WpSiAudioAdapter *self)
{
  /* clear port and handler */
  if (self->port) {
    g_signal_handlers_disconnect_by_func (self->port, on_port_param_info, self);
    g_clear_object (&self->port);
  }

  if (wp_node_get_n_ports (self->node) > 0) {
    /* if non DSP mode, listen for param-info on the single port in order to
     * be notified of format changed events */
    if (g_strcmp0 (self->mode, "dsp") != 0) {
      self->port = wp_node_lookup_port (self->node,
          WP_CONSTRAINT_TYPE_PW_PROPERTY, "port.direction", "=s",
          self->portconfig_direction == WP_DIRECTION_INPUT ? "in" : "out",
          NULL);
      if (self->port)
        g_signal_connect_object (self->port, "notify::param-info",
            G_CALLBACK (on_port_param_info), self, 0);
    }

    /* finish the task started by _set_ports_format() */
    if (self->format_task) {
      g_autoptr (GTask) t = g_steal_pointer (&self->format_task);
      si_audio_adapter_set_ports_state (self,
          WP_SI_ADAPTER_PORTS_STATE_CONFIGURED);
      g_task_return_boolean (t, TRUE);
    }
  }
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

  self->ports_changed_sigid = g_signal_connect_object (self->node,
      "ports-changed", (GCallback) on_node_ports_changed, self, 0);

  /* If device node, enum available formats and set one of them */
  if (!self->no_format && (self->is_device || self->dont_remix ||
      !self->is_autoconnect || self->disable_dsp || self->is_unpositioned))
    si_audio_adapter_configure_node (self, transition);

  /* Otherwise just finish activating */
  else
    wp_object_update_features (WP_OBJECT (self),
          WP_SESSION_ITEM_FEATURE_ACTIVE, 0);
}

static void
si_audio_adapter_disable_active (WpSessionItem *si)
{
  WpSiAudioAdapter *self = WP_SI_AUDIO_ADAPTER (si);

  if (self->ports_changed_sigid) {
    g_signal_handler_disconnect (self->node, self->ports_changed_sigid);
    self->ports_changed_sigid = 0;
  }

  wp_object_update_features (WP_OBJECT (self), 0,
      WP_SESSION_ITEM_FEATURE_ACTIVE);
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

static WpSiAdapterPortsState
si_audio_adapter_get_ports_state (WpSiAdapter * item)
{
  WpSiAudioAdapter *self = WP_SI_AUDIO_ADAPTER (item);
  return self->ports_state;
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
  g_autoptr (WpSpaPod) format = f;
  g_autoptr (GTask) task = g_task_new (self, NULL, callback, data);
  guint32 active = 0;

  /* cancel previous task if any */
  if (self->format_task) {
    g_autoptr (GTask) t = g_steal_pointer (&self->format_task);
    g_task_return_new_error (t, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVARIANT,
        "setting new format before previous done");
  }

  /* build default format if NULL was given */
  if (!format && !g_strcmp0 (mode, "dsp")) {
    format = build_adapter_default_format (self, mode);
    if (!format) {
      g_task_return_new_error (task, WP_DOMAIN_LIBRARY,
          WP_LIBRARY_ERROR_OPERATION_FAILED,
          "failed to build default format, aborting set format operation");
      return;
    }
  }

  /* make sure the node has WP_NODE_FEATURE_PORTS */
  active = wp_object_get_active_features (WP_OBJECT (self->node));
  if (G_UNLIKELY (!(active & WP_NODE_FEATURE_PORTS))) {
    g_task_return_new_error (task, WP_DOMAIN_LIBRARY,
        WP_LIBRARY_ERROR_OPERATION_FAILED,
        "node feature ports is not enabled, aborting set format operation");
    return;
  }

  /* skip reconfiguring if the same mode & format are requested */
  if (!g_strcmp0 (mode, self->mode) &&
      ((format == NULL && self->format == NULL) ||
        wp_spa_pod_equal (format, self->format))) {
    g_task_return_boolean (task, TRUE);
    return;
  }

  /* ensure the node is suspended */
  if (wp_node_get_state (self->node, NULL) >= WP_NODE_STATE_IDLE)
    wp_node_send_command (self->node, "Suspend");

  /* set task, format and mode */
  self->format_task = g_steal_pointer (&task);
  g_clear_pointer (&self->format, wp_spa_pod_unref);
  self->format = g_steal_pointer (&format);
  strncpy (self->mode, mode ? mode : "dsp", sizeof (self->mode) - 1);

  si_audio_adapter_set_ports_state (self,
      WP_SI_ADAPTER_PORTS_STATE_CONFIGURING);

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
  iface->get_ports_state = si_audio_adapter_get_ports_state;
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
  WpDirection direction;
  guint32 node_id;

  if (!g_strcmp0 (context, "output")) {
    direction = WP_DIRECTION_OUTPUT;
  }
  else if (!g_strcmp0 (context, "input")) {
    direction = WP_DIRECTION_INPUT;
  }
  else {
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
