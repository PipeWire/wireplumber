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

#include "module-si-adapter/algorithms.h"

enum {
  STEP_VERIFY_CONFIG = WP_TRANSITION_STEP_CUSTOM_START,
  STEP_CHOOSE_FORMAT,
  STEP_CONFIGURE_PORTS,
  STEP_GET_PORTS,
};

struct _WpSiAdapter
{
  WpSessionItem parent;

  /* configuration */
  WpNode *node;
  gchar name[96];
  gchar media_class[32];
  gchar role[32];
  guint priority;
  guint preferred_n_channels;
  gboolean control_port;
  gboolean monitor;
  WpDirection direction;
  struct spa_audio_info_raw format;
};

static void si_adapter_endpoint_init (WpSiEndpointInterface * iface);
static void si_adapter_stream_init (WpSiStreamInterface * iface);
static void si_adapter_port_info_init (WpSiPortInfoInterface * iface);

G_DECLARE_FINAL_TYPE(WpSiAdapter, si_adapter, WP, SI_ADAPTER, WpSessionItem)
G_DEFINE_TYPE_WITH_CODE (WpSiAdapter, si_adapter, WP_TYPE_SESSION_ITEM,
    G_IMPLEMENT_INTERFACE (WP_TYPE_SI_ENDPOINT, si_adapter_endpoint_init)
    G_IMPLEMENT_INTERFACE (WP_TYPE_SI_STREAM, si_adapter_stream_init)
    G_IMPLEMENT_INTERFACE (WP_TYPE_SI_PORT_INFO, si_adapter_port_info_init))

static void
si_adapter_init (WpSiAdapter * self)
{
}

static void
si_adapter_reset (WpSessionItem * item)
{
  WpSiAdapter *self = WP_SI_ADAPTER (item);

  /* unexport & deactivate first */
  WP_SESSION_ITEM_CLASS (si_adapter_parent_class)->reset (item);

  g_clear_object (&self->node);
}

static gpointer
si_adapter_get_associated_proxy (WpSessionItem * item, GType proxy_type)
{
  WpSiAdapter *self = WP_SI_ADAPTER (item);

  if (proxy_type == WP_TYPE_NODE)
    return self->node ? g_object_ref (self->node) : NULL;

  return WP_SESSION_ITEM_CLASS (si_adapter_parent_class)->get_associated_proxy (
      item, proxy_type);
}

static GVariant *
si_adapter_get_configuration (WpSessionItem * item)
{
  WpSiAdapter *self = WP_SI_ADAPTER (item);
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
      "preferred-n-channels", g_variant_new_uint32 (self->preferred_n_channels));
  g_variant_builder_add (&b, "{sv}",
      "enable-control-port", g_variant_new_boolean (self->control_port));
  g_variant_builder_add (&b, "{sv}",
      "enable-monitor", g_variant_new_boolean (self->monitor));
  g_variant_builder_add (&b, "{sv}",
      "direction", g_variant_new_byte (self->direction));
  g_variant_builder_add (&b, "{sv}",
      "channels", g_variant_new_uint32 (self->format.channels));
  return g_variant_builder_end (&b);
}

static gboolean
si_adapter_configure (WpSessionItem * item, GVariant * args)
{
  WpSiAdapter *self = WP_SI_ADAPTER (item);
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
  self->control_port = FALSE;
  self->monitor = FALSE;
  self->direction = WP_DIRECTION_INPUT;
  self->preferred_n_channels = 0;

  if (!g_variant_lookup (args, "node", "t", &node_i))
    return FALSE;

  g_return_val_if_fail (WP_IS_NODE (GUINT_TO_POINTER (node_i)), FALSE);

  self->node = g_object_ref (GUINT_TO_POINTER (node_i));
  props = wp_proxy_get_properties (WP_PROXY (self->node));

  if (g_variant_lookup (args, "name", "&s", &tmp_str)) {
    strncpy (self->name, tmp_str, sizeof (self->name) - 1);
  } else {
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

  if (strstr (self->media_class, "Source") ||
      strstr (self->media_class, "Output"))
    self->direction = WP_DIRECTION_OUTPUT;

  g_variant_lookup (args, "priority", "u", &self->priority);
  g_variant_lookup (args, "preferred-n-channels", "u", &self->preferred_n_channels);
  g_variant_lookup (args, "enable-control-port", "b", &self->control_port);
  g_variant_lookup (args, "enable-monitor", "b", &self->monitor);

  return TRUE;
}

static guint
si_adapter_activate_get_next_step (WpSessionItem * item,
     WpTransition * transition, guint step)
{
  switch (step) {
    case WP_TRANSITION_STEP_NONE:
      return STEP_VERIFY_CONFIG;

    case STEP_VERIFY_CONFIG:
    case STEP_CHOOSE_FORMAT:
    case STEP_CONFIGURE_PORTS:
      return step + 1;

    case STEP_GET_PORTS:
      return WP_TRANSITION_STEP_NONE;

    default:
      return WP_TRANSITION_STEP_ERROR;
  }
}

static void
on_node_enum_format_done (WpProxy *proxy, GAsyncResult *res,
    WpTransition * transition)
{
  WpSiAdapter *self = wp_transition_get_source_object (transition);
  g_autoptr (WpIterator) formats = NULL;
  g_autoptr (GError) error = NULL;
  gint pref_chan;

  formats = wp_proxy_enum_params_finish (proxy, res, &error);
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

  wp_session_item_set_flag (WP_SESSION_ITEM (self), WP_SI_FLAG_CONFIGURED);
  wp_transition_advance (transition);
}

static void
on_ports_configuration_done (WpCore * core, GAsyncResult * res,
    WpTransition * transition)
{
  g_autoptr (GError) error = NULL;
  if (!wp_core_sync_finish (core, res, &error)) {
    wp_transition_return_error (transition, g_steal_pointer (&error));
    return;
  }

  wp_transition_advance (transition);
}

static void
on_feature_ports_ready (WpProxy * node, GAsyncResult * res,
    WpTransition * transition)
{
  g_autoptr (GError) error = NULL;
  if (!wp_proxy_augment_finish (node, res, &error)) {
    wp_transition_return_error (transition, g_steal_pointer (&error));
    return;
  }

  wp_transition_advance (transition);
}

static WpSpaPod *
format_audio_raw_build (const struct spa_audio_info_raw *info)
{
  g_autoptr (WpSpaPodBuilder) builder = wp_spa_pod_builder_new_object (
      "Format", "Format");
  wp_spa_pod_builder_add (builder,
      "mediaType",    "I", SPA_MEDIA_TYPE_audio,
      "mediaSubtype", "I", SPA_MEDIA_SUBTYPE_raw,
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
si_adapter_activate_execute_step (WpSessionItem * item,
    WpTransition * transition, guint step)
{
  WpSiAdapter *self = WP_SI_ADAPTER (item);

  switch (step) {
    case STEP_VERIFY_CONFIG:
      if (!self->node) {
        wp_transition_return_error (transition,
            g_error_new (WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVARIANT,
                "si-adapter: node was not set on the configuration"));
      }
      wp_transition_advance (transition);
      break;

    case STEP_CHOOSE_FORMAT:
      wp_proxy_enum_params (WP_PROXY (self->node), "EnumFormat", NULL, NULL,
          (GAsyncReadyCallback) on_node_enum_format_done, transition);
      break;

    case STEP_CONFIGURE_PORTS: {
      g_autoptr (WpSpaPod) format = NULL, port_format = NULL;
      g_autoptr (WpSpaPod) pod = NULL;

      /* set the chosen device/client format on the node */
      format = format_audio_raw_build (&self->format);
      wp_proxy_set_param (WP_PROXY (self->node), "Format", format);

      /* now choose the DSP format: keep the chanels but use F32 plannar @ 48K */
      self->format.format = SPA_AUDIO_FORMAT_F32P;
      self->format.rate = ({
        g_autoptr (WpCore) core = wp_proxy_get_core (WP_PROXY (self->node));
        g_autoptr (WpProperties) props = wp_core_get_remote_properties (core);
        const gchar *rate_str = wp_properties_get (props, "default.clock.rate");
        rate_str ? atoi (rate_str) : 48000;
      });

      wp_debug_object (item, "format: F32P %uch @ %u",
          self->format.channels, self->format.rate);

      port_format = format_audio_raw_build (&self->format);
      pod = wp_spa_pod_new_object ("PortConfig",  "PortConfig",
          "direction",  "I", self->direction,
          "mode",       "I", SPA_PARAM_PORT_CONFIG_MODE_dsp,
          "monitor",    "b", self->monitor,
          "control",    "b", self->control_port,
          "format",     "P", port_format,
          NULL);
      wp_proxy_set_param (WP_PROXY (self->node), "PortConfig", pod);

      g_autoptr (WpCore) core = wp_proxy_get_core (WP_PROXY (self->node));
      wp_core_sync (core, NULL,
          (GAsyncReadyCallback) on_ports_configuration_done, transition);
      break;
    }
    case STEP_GET_PORTS: {
      wp_proxy_augment (WP_PROXY (self->node), WP_NODE_FEATURE_PORTS, NULL,
          (GAsyncReadyCallback) on_feature_ports_ready, transition);
      break;
    }
    default:
      g_return_if_reached ();
  }
}

static void
si_adapter_activate_rollback (WpSessionItem * item)
{
  wp_session_item_clear_flag (item, WP_SI_FLAG_CONFIGURED);
}

static void
si_adapter_class_init (WpSiAdapterClass * klass)
{
  WpSessionItemClass *si_class = (WpSessionItemClass *) klass;

  si_class->reset = si_adapter_reset;
  si_class->get_associated_proxy = si_adapter_get_associated_proxy;
  si_class->configure = si_adapter_configure;
  si_class->get_configuration = si_adapter_get_configuration;
  si_class->activate_get_next_step = si_adapter_activate_get_next_step;
  si_class->activate_execute_step = si_adapter_activate_execute_step;
  si_class->activate_rollback = si_adapter_activate_rollback;
}

static GVariant *
si_adapter_get_registration_info (WpSiEndpoint * item)
{
  WpSiAdapter *self = WP_SI_ADAPTER (item);
  GVariantBuilder b;

  g_variant_builder_init (&b, G_VARIANT_TYPE ("(ssya{ss})"));
  g_variant_builder_add (&b, "s", self->name);
  g_variant_builder_add (&b, "s", self->media_class);
  g_variant_builder_add (&b, "y", (guchar) self->direction);
  g_variant_builder_add (&b, "a{ss}", NULL);

  return g_variant_builder_end (&b);
}

static WpProperties *
si_adapter_get_properties (WpSiEndpoint * item)
{
  WpSiAdapter *self = WP_SI_ADAPTER (item);
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

  wp_properties_set (result, "endpoint.description",
      wp_properties_get (node_props, PW_KEY_NODE_DESCRIPTION));

  /* propagate the device icon, if this is a device */
  const gchar *icon = wp_properties_get (node_props, PW_KEY_DEVICE_ICON_NAME);
  if (icon)
    wp_properties_set (result, PW_KEY_ENDPOINT_ICON_NAME, icon);

  /* endpoint.client.id: the id of the client that created the node
   * Not to be confused with client.id, which will also be set on the endpoint
   * to the id of the client object that creates the endpoint (wireplumber) */
  wp_properties_set (result, PW_KEY_ENDPOINT_CLIENT_ID,
      wp_properties_get (node_props, PW_KEY_CLIENT_ID));

  return result;
}

static guint
si_adapter_get_n_streams (WpSiEndpoint * item)
{
  return 1;
}

static WpSiStream *
si_adapter_get_stream (WpSiEndpoint * item, guint index)
{
  g_return_val_if_fail (index == 0, NULL);
  return WP_SI_STREAM (item);
}

static void
si_adapter_endpoint_init (WpSiEndpointInterface * iface)
{
  iface->get_registration_info = si_adapter_get_registration_info;
  iface->get_properties = si_adapter_get_properties;
  iface->get_n_streams = si_adapter_get_n_streams;
  iface->get_stream = si_adapter_get_stream;
}

static GVariant *
si_adapter_get_stream_registration_info (WpSiStream * self)
{
  GVariantBuilder b;

  g_variant_builder_init (&b, G_VARIANT_TYPE ("(sa{ss})"));
  g_variant_builder_add (&b, "s", "default");
  g_variant_builder_add (&b, "a{ss}", NULL);

  return g_variant_builder_end (&b);
}

static WpProperties *
si_adapter_get_stream_properties (WpSiStream * self)
{
  return NULL;
}

static WpSiEndpoint *
si_adapter_get_stream_parent_endpoint (WpSiStream * self)
{
  WpSessionItem *parent = wp_session_item_get_parent (WP_SESSION_ITEM (self));
  if (!parent)
    parent = g_object_ref (WP_SESSION_ITEM (self));
  return WP_SI_ENDPOINT (parent);
}

static void
si_adapter_stream_init (WpSiStreamInterface * iface)
{
  iface->get_registration_info = si_adapter_get_stream_registration_info;
  iface->get_properties = si_adapter_get_stream_properties;
  iface->get_parent_endpoint = si_adapter_get_stream_parent_endpoint;
}

static GVariant *
si_adapter_get_ports (WpSiPortInfo * item, const gchar * context)
{
  WpSiAdapter *self = WP_SI_ADAPTER (item);
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
si_adapter_port_info_init (WpSiPortInfoInterface * iface)
{
  iface->get_ports = si_adapter_get_ports;
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
  g_variant_builder_add (&b, "(ssymv)", "preferred-n-channels", "u",
      WP_SI_CONFIG_OPTION_WRITEABLE, NULL);
  g_variant_builder_add (&b, "(ssymv)", "enable-control-port", "b",
      WP_SI_CONFIG_OPTION_WRITEABLE, NULL);
  g_variant_builder_add (&b, "(ssymv)", "enable-monitor", "b",
      WP_SI_CONFIG_OPTION_WRITEABLE, NULL);
  g_variant_builder_add (&b, "(ssymv)", "direction", "y", 0, NULL);
  g_variant_builder_add (&b, "(ssymv)", "channels", "u", 0, NULL);

  wp_si_factory_register (core, wp_si_factory_new_simple (
      "si-adapter", si_adapter_get_type (), g_variant_builder_end (&b)));
}
