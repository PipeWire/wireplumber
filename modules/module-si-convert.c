/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>
#include <pipewire/pipewire.h>
#include <pipewire/extensions/session-manager.h>

#include <spa/pod/builder.h>
#include <spa/param/format-utils.h>
#include <spa/param/audio/raw.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/audio/type-info.h>
#include <spa/param/props.h>

#include "module-pipewire/algorithms.h"

enum {
  STEP_VERIFY_CONFIG = WP_TRANSITION_STEP_CUSTOM_START,
  STEP_CHOOSE_FORMAT,
  STEP_CONFIGURE_PORTS
};

struct _WpSiConvert
{
  WpSessionItem parent;

  /* configuration */
  WpNode *node;
  WpSessionItem *target;
  gchar name[96];
  gboolean control_port;
  WpDirection direction;
  struct spa_audio_info_raw format;

  GPtrArray *links;
};

static void si_convert_stream_init (WpSiStreamInterface * iface);
static void si_convert_port_info_init (WpSiPortInfoInterface * iface);

G_DECLARE_FINAL_TYPE(WpSiConvert, si_convert, WP, SI_CONVERT, WpSessionItem)
G_DEFINE_TYPE_WITH_CODE (WpSiConvert, si_convert, WP_TYPE_SESSION_ITEM,
    G_IMPLEMENT_INTERFACE (WP_TYPE_SI_STREAM, si_convert_stream_init)
    G_IMPLEMENT_INTERFACE (WP_TYPE_SI_PORT_INFO, si_convert_port_info_init))

static void
si_convert_init (WpSiConvert * self)
{
  self->links = g_ptr_array_new_with_free_func (g_object_unref);
}

static void
si_convert_reset (WpSessionItem * item)
{
  WpSiConvert *self = WP_SI_CONVERT (item);

  /* unexport & deactivate first */
  WP_SESSION_ITEM_CLASS (si_convert_parent_class)->reset (item);

  g_clear_object (&self->node);
  g_clear_object (&self->target);
  self->name[0] = '\0';
  self->control_port = FALSE;
  self->direction = WP_DIRECTION_INPUT;
  g_ptr_array_set_size (self->links, 0);

  wp_session_item_clear_flag (item, WP_SI_FLAG_CONFIGURED);
}

static gpointer
si_convert_get_associated_proxy (WpSessionItem * item, GType proxy_type)
{
  WpSiConvert *self = WP_SI_CONVERT (item);

  if (proxy_type == WP_TYPE_NODE)
    return self->node ? g_object_ref (self->node) : NULL;

  return WP_SESSION_ITEM_CLASS (si_convert_parent_class)->get_associated_proxy (
      item, proxy_type);
}

static gboolean
si_convert_configure (WpSessionItem * item, GVariant * args)
{
  WpSiConvert *self = WP_SI_CONVERT (item);
  guint64 node_i;
  guint64 target_i;
  const gchar *tmp_str;

  if (wp_session_item_get_flags (item) & (WP_SI_FLAG_ACTIVATING | WP_SI_FLAG_ACTIVE))
    return FALSE;

  /* reset previous config */
  si_convert_reset (WP_SESSION_ITEM (self));

  if (!g_variant_lookup (args, "node", "t", &node_i))
    return FALSE;
  g_return_val_if_fail (WP_IS_NODE (GUINT_TO_POINTER (node_i)), FALSE);
  self->node = g_object_ref (GUINT_TO_POINTER (node_i));

  if (!g_variant_lookup (args, "target", "t", &target_i))
    return FALSE;
  g_return_val_if_fail (WP_IS_SESSION_ITEM (GUINT_TO_POINTER (target_i)), FALSE);
  self->target = g_object_ref (GUINT_TO_POINTER (target_i));

  if (g_variant_lookup (args, "name", "&s", &tmp_str))
    strncpy (self->name, tmp_str, sizeof (self->name) - 1);

  g_variant_lookup (args, "direction", "y", &self->direction);
  g_variant_lookup (args, "enable-control-port", "b", &self->control_port);

  return TRUE;
}

static GVariant *
si_convert_get_configuration (WpSessionItem * item)
{
  WpSiConvert *self = WP_SI_CONVERT (item);
  GVariantBuilder b;

  /* Set the properties */
  g_variant_builder_init (&b, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&b, "{sv}",
      "node", g_variant_new_uint64 ((guint64) self->node));
  g_variant_builder_add (&b, "{sv}",
      "target", g_variant_new_uint64 ((guint64) self->target));
  g_variant_builder_add (&b, "{sv}",
      "name", g_variant_new_string (self->name));
  g_variant_builder_add (&b, "{sv}",
      "enable-control-port", g_variant_new_boolean (self->control_port));
  g_variant_builder_add (&b, "{sv}",
      "direction", g_variant_new_byte (self->direction));
  g_variant_builder_add (&b, "{sv}",
      "channels", g_variant_new_uint32 (self->format.channels));
  return g_variant_builder_end (&b);
}

static guint
si_convert_activate_get_next_step (WpSessionItem * item,
    WpTransition * transition, guint step)
{
  switch (step) {
    case WP_TRANSITION_STEP_NONE:
      return STEP_VERIFY_CONFIG;

    case STEP_VERIFY_CONFIG:
    case STEP_CHOOSE_FORMAT:
      return step + 1;

    case STEP_CONFIGURE_PORTS:
      return WP_TRANSITION_STEP_NONE;

    default:
      return WP_TRANSITION_STEP_ERROR;
  }
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
on_link_activated (WpSessionItem * item, GAsyncResult * res, WpSiConvert * self)
{
  g_autoptr (GError) error = NULL;
  gboolean activate_ret = wp_session_item_activate_finish (item, res, &error);
  g_return_if_fail (error);
  g_return_if_fail (activate_ret);
}

static void
on_convert_running (WpSiConvert *self)
{
  g_autoptr (WpCore) core = wp_proxy_get_core (WP_PROXY (self->node));
  g_autoptr (WpSessionItem) link = wp_session_item_make (core,
      "si-standard-link");
  g_auto (GVariantBuilder) b = G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE_VARDICT);

  if (self->direction == WP_DIRECTION_INPUT) {
      g_variant_builder_add (&b, "{sv}", "out-stream",
          g_variant_new_uint64 ((guint64) WP_SI_STREAM (self->target)));
      g_variant_builder_add (&b, "{sv}", "in-streams",
          g_variant_new_uint64 ((guint64) WP_SI_STREAM (self)));
  } else {
      g_variant_builder_add (&b, "{sv}", "out-stream",
          g_variant_new_uint64 ((guint64) WP_SI_STREAM (self)));
      g_variant_builder_add (&b, "{sv}", "in-streams",
          g_variant_new_uint64 ((guint64) WP_SI_STREAM (self->target)));
  }

  wp_session_item_configure (link, g_variant_builder_end (&b));
  wp_session_item_activate (link, (GAsyncReadyCallback) on_link_activated, self);
  g_ptr_array_add (self->links, g_steal_pointer (&self->links));
}

static void
on_node_state_changed (WpNode * node, WpNodeState old, WpNodeState curr,
    WpSiConvert * self)
{
  switch (curr) {
  case WP_NODE_STATE_IDLE:
    g_ptr_array_set_size (self->links, 0);
    break;
  case WP_NODE_STATE_RUNNING:
    on_convert_running (self);
    break;
  case WP_NODE_STATE_SUSPENDED:
  case WP_NODE_STATE_CREATING:
  case WP_NODE_STATE_ERROR:
  default:
    break;
  }
}

static void
si_convert_activate_execute_step (WpSessionItem * item,
    WpTransition * transition, guint step)
{
  WpSiConvert *self = WP_SI_CONVERT (item);

  switch (step) {
    case STEP_VERIFY_CONFIG:
      if (!self->node) {
        wp_transition_return_error (transition,
            g_error_new (WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVARIANT,
                "si-convert: node was not set on the configuration"));
      }
      if (!self->target) {
        wp_transition_return_error (transition,
            g_error_new (WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVARIANT,
                "si-convert: target was not set on the configuration"));
      }
      wp_transition_advance (transition);
      break;

    case STEP_CHOOSE_FORMAT: {
      /* get the channels from the target */
      guint32 channels = 2;
      g_autoptr (GVariant) v = wp_session_item_get_configuration (self->target);
      g_return_if_fail (v);
      g_assert_true (g_variant_lookup (v, "channels", "u", &channels));
      /* set the format with target channels */
      self->format.format = SPA_AUDIO_FORMAT_F32P;
      self->format.rate = 48000;
      self->format.channels = channels;
      wp_transition_advance (transition);
      break;
    }

    case STEP_CONFIGURE_PORTS: {
      g_autoptr (WpSpaPod) format = NULL;
      g_autoptr (WpSpaPod) pod = NULL;

      /* set the chosen device/client format on the node */
      format = format_audio_raw_build (&self->format);

      /* Configure audioconvert to be both merger and splitter; this means it
         will have an equal number of input and output ports and just
         passthrough the same format, but with altered volume.
         In the future we need to consider writing a simpler volume node for
         this, as doing merge + split is heavy for our needs */
      pod = wp_spa_pod_new_object ("PortConfig", "PortConfig",
          "direction",  "I", pw_direction_reverse (self->direction),
          "mode",       "I", SPA_PARAM_PORT_CONFIG_MODE_dsp,
          "format",     "P", format,
          NULL);
      wp_proxy_set_param (WP_PROXY (self->node), SPA_PARAM_PortConfig, 0, pod);

      pod = wp_spa_pod_new_object ("PortConfig", "PortConfig",
          "direction",  "I", self->direction,
          "mode",       "I", SPA_PARAM_PORT_CONFIG_MODE_dsp,
          "monitor",    "b", FALSE,
          "control",    "b", self->control_port,
          "format",     "P", format,
          NULL);
      wp_proxy_set_param (WP_PROXY (self->node), SPA_PARAM_PortConfig, 0, pod);

      /* handle the info callback */
      g_signal_connect_object (self->node, "state-changed",
          (GCallback) on_node_state_changed, self, 0);

      g_autoptr (WpCore) core = wp_proxy_get_core (WP_PROXY (self->node));
      wp_core_sync (core, NULL,
          (GAsyncReadyCallback) on_ports_configuration_done, transition);
      break;
    }
    default:
      g_return_if_reached ();
  }
}

static void
si_convert_activate_rollback (WpSessionItem * item)
{
  WpSiConvert *self = WP_SI_CONVERT (item);

  g_ptr_array_set_size (self->links, 0);
  wp_session_item_clear_flag (item, WP_SI_FLAG_CONFIGURED);
}

static GVariant *
si_convert_get_stream_registration_info (WpSiStream * self)
{
  GVariantBuilder b;

  g_variant_builder_init (&b, G_VARIANT_TYPE ("(sa{ss})"));
  g_variant_builder_add (&b, "s", "default");
  g_variant_builder_add (&b, "a{ss}", NULL);

  return g_variant_builder_end (&b);
}

static WpProperties *
si_convert_get_stream_properties (WpSiStream * self)
{
  return NULL;
}

static WpSiEndpoint *
si_convert_get_stream_parent_endpoint (WpSiStream * self)
{
  return WP_SI_ENDPOINT (wp_session_item_get_parent (WP_SESSION_ITEM (self)));
}

static void
si_convert_stream_init (WpSiStreamInterface * iface)
{
  iface->get_registration_info = si_convert_get_stream_registration_info;
  iface->get_properties = si_convert_get_stream_properties;
  iface->get_parent_endpoint = si_convert_get_stream_parent_endpoint;
}

static GVariant *
si_convert_get_ports (WpSiPortInfo * item, const gchar * context)
{
  WpSiConvert *self = WP_SI_CONVERT (item);
  g_auto (GVariantBuilder) b = G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE_ARRAY);
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) val = G_VALUE_INIT;
  WpDirection direction = self->direction;
  guint32 node_id;

  /* context can only be either NULL or "reverse" */
  if (!g_strcmp0 (context, "reverse")) {
    self->direction = (self->direction == WP_DIRECTION_INPUT) ?
        WP_DIRECTION_OUTPUT : WP_DIRECTION_INPUT;
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
    const gchar *channel;
    guint32 port_id, channel_id = 0;

    if (wp_port_get_direction (port) != direction)
      continue;

    port_id = wp_proxy_get_bound_id (WP_PROXY (port));

    /* try to find the audio channel; if channel is NULL, this will silently
       leave the channel_id to its default value, 0 */
    props = wp_proxy_get_properties (WP_PROXY (port));
    channel = wp_properties_get (props, PW_KEY_AUDIO_CHANNEL);
    wp_spa_type_get_by_nick (WP_SPA_TYPE_TABLE_AUDIO_CHANNEL, channel,
        &channel_id, NULL, NULL);

    g_variant_builder_add (&b, "(uuu)", node_id, port_id, channel_id);
  }

  return g_variant_builder_end (&b);
}

static void
si_convert_port_info_init (WpSiPortInfoInterface * iface)
{
  iface->get_ports = si_convert_get_ports;
}

static void
si_convert_finalize (GObject * object)
{
  WpSiConvert *self = WP_SI_CONVERT (object);
  g_clear_pointer (&self->links, g_ptr_array_unref);
  G_OBJECT_CLASS (si_convert_parent_class)->finalize (object);
}

static void
si_convert_class_init (WpSiConvertClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpSessionItemClass *si_class = (WpSessionItemClass *) klass;

  object_class->finalize = si_convert_finalize;

  si_class->reset = si_convert_reset;
  si_class->get_associated_proxy = si_convert_get_associated_proxy;
  si_class->configure = si_convert_configure;
  si_class->get_configuration = si_convert_get_configuration;
  si_class->activate_get_next_step = si_convert_activate_get_next_step;
  si_class->activate_execute_step = si_convert_activate_execute_step;
  si_class->activate_rollback = si_convert_activate_rollback;
}

WP_PLUGIN_EXPORT void
wireplumber__module_init (WpModule * module, WpCore * core, GVariant * args)
{
  GVariantBuilder b;

  g_variant_builder_init (&b, G_VARIANT_TYPE ("a(ssymv)"));
  g_variant_builder_add (&b, "(ssymv)", "node", "t",
      WP_SI_CONFIG_OPTION_WRITEABLE | WP_SI_CONFIG_OPTION_REQUIRED, NULL);
  g_variant_builder_add (&b, "(ssymv)", "target", "t",
      WP_SI_CONFIG_OPTION_WRITEABLE | WP_SI_CONFIG_OPTION_REQUIRED, NULL);
  g_variant_builder_add (&b, "(ssymv)", "name", "s",
      WP_SI_CONFIG_OPTION_WRITEABLE, NULL);
  g_variant_builder_add (&b, "(ssymv)", "enable-control-port", "b",
      WP_SI_CONFIG_OPTION_WRITEABLE, NULL);
  g_variant_builder_add (&b, "(ssymv)", "direction", "y", 0, NULL);
  g_variant_builder_add (&b, "(ssymv)", "channels", "u", 0, NULL);


  wp_si_factory_register (core, wp_si_factory_new_simple (
      "si-convert", si_convert_get_type (), g_variant_builder_end (&b)));
}
