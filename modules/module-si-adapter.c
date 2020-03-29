/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
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
#include <spa/param/props.h>

#include "module-pipewire/algorithms.h"

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
  gboolean control_port;
  gboolean monitor;
  WpDirection direction;
  struct spa_audio_info_raw format;

  WpObjectManager *ports_om;
};

static void si_adapter_multi_endpoint_init (WpSiMultiEndpointInterface * iface);
static void si_adapter_endpoint_init (WpSiEndpointInterface * iface);
static void si_adapter_stream_init (WpSiStreamInterface * iface);

G_DECLARE_FINAL_TYPE(WpSiAdapter, si_adapter, WP, SI_ADAPTER, WpSessionItem)
G_DEFINE_TYPE_WITH_CODE (WpSiAdapter, si_adapter, WP_TYPE_SESSION_ITEM,
    G_IMPLEMENT_INTERFACE (WP_TYPE_SI_MULTI_ENDPOINT, si_adapter_multi_endpoint_init)
    G_IMPLEMENT_INTERFACE (WP_TYPE_SI_ENDPOINT, si_adapter_endpoint_init)
    G_IMPLEMENT_INTERFACE (WP_TYPE_SI_STREAM, si_adapter_stream_init))

static void
si_adapter_init (WpSiAdapter * self)
{
}

static void
si_adapter_finalize (GObject * object)
{
  WpSiAdapter *self = WP_SI_ADAPTER (object);

  g_clear_object (&self->node);

  G_OBJECT_CLASS (si_adapter_parent_class)->finalize (object);
}

static void
si_adapter_reset (WpSessionItem * item)
{
  WpSiAdapter *self = WP_SI_ADAPTER (item);

  g_clear_object (&self->ports_om);
  wp_session_item_clear_flag (item, WP_SI_FLAG_CONFIGURED);

  WP_SESSION_ITEM_CLASS (si_adapter_parent_class)->reset (item);
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

  if (strstr (self->media_class, "Source") ||
      strstr (self->media_class, "Output"))
    self->direction = WP_DIRECTION_OUTPUT;

  g_variant_lookup (args, "priority", "u", &self->priority);
  g_variant_lookup (args, "enable-control-port", "b", &self->control_port);
  g_variant_lookup (args, "enable-monitor", "b", &self->monitor);

  return TRUE;
}

static guint
si_adapter_get_next_step (WpSessionItem * item,
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
  g_autoptr (GPtrArray) formats = NULL;
  g_autoptr (GError) error = NULL;

  formats = wp_proxy_enum_params_collect_finish (proxy, res, &error);
  if (error) {
    wp_transition_return_error (transition, g_steal_pointer (&error));
    return;
  }

  if (!choose_sensible_raw_audio_format (formats, &self->format)) {
    uint32_t media_type, media_subtype;
    struct spa_pod *param;

    g_warning ("failed to choose a sensible audio format");

    /* fall back to spa_pod_fixate */
    if (formats->len == 0 ||
        !(param = g_ptr_array_index (formats, 0)) ||
        spa_format_parse (param, &media_type, &media_subtype) < 0 ||
        media_type != SPA_MEDIA_TYPE_audio ||
        media_subtype != SPA_MEDIA_SUBTYPE_raw) {
      wp_transition_return_error (transition,
          g_error_new (WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_OPERATION_FAILED,
              "node does not support audio/raw format"));
      return;
    }

    spa_pod_fixate (param);
    spa_format_audio_raw_parse (param, &self->format);
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
on_ports_changed (WpObjectManager *om, WpTransition * transition)
{
  WpSiAdapter *self = wp_transition_get_source_object (transition);

  g_debug ("%s:%p port config done", G_OBJECT_TYPE_NAME (self), self);

  wp_transition_advance (transition);
}

static void
si_adapter_execute_step (WpSessionItem * item, WpTransition * transition,
    guint step)
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
      wp_proxy_enum_params_collect (WP_PROXY (self->node),
          SPA_PARAM_EnumFormat, 0, -1, NULL, NULL,
          (GAsyncReadyCallback) on_node_enum_format_done, transition);
      break;

    case STEP_CONFIGURE_PORTS: {
      uint8_t buf[1024];
      struct spa_pod_builder pod_builder = SPA_POD_BUILDER_INIT (buf, sizeof(buf));
      struct spa_pod *param;

      /* set the chosen device/client format on the node */
      param = spa_format_audio_raw_build (&pod_builder, SPA_PARAM_Format,
          &self->format);
      wp_proxy_set_param (WP_PROXY (self->node), SPA_PARAM_Format, 0, param);

      /* now choose the DSP format: keep the chanels but use F32 plannar @ 48K */
      self->format.format = SPA_AUDIO_FORMAT_F32P;
      self->format.rate = 48000;

      param = spa_format_audio_raw_build (&pod_builder,
          SPA_PARAM_Format, &self->format);
      param = spa_pod_builder_add_object (&pod_builder,
          SPA_TYPE_OBJECT_ParamPortConfig,  SPA_PARAM_PortConfig,
          SPA_PARAM_PORT_CONFIG_direction,  SPA_POD_Id(self->direction),
          SPA_PARAM_PORT_CONFIG_mode,       SPA_POD_Id(SPA_PARAM_PORT_CONFIG_MODE_dsp),
          SPA_PARAM_PORT_CONFIG_monitor,    SPA_POD_Bool(self->monitor),
          SPA_PARAM_PORT_CONFIG_control,    SPA_POD_Bool(self->control_port),
          SPA_PARAM_PORT_CONFIG_format,     SPA_POD_Pod(param));

      wp_proxy_set_param (WP_PROXY (self->node), SPA_PARAM_PortConfig, 0, param);

      g_autoptr (WpCore) core = wp_proxy_get_core (WP_PROXY (self->node));
      wp_core_sync (core, NULL,
          (GAsyncReadyCallback) on_ports_configuration_done, transition);
      break;
    }
    case STEP_GET_PORTS: {
      GVariantBuilder b;
      self->ports_om = wp_object_manager_new ();

      /* set a constraint: the port's "node.id" must match
        the stream's underlying node id */
      g_variant_builder_init (&b, G_VARIANT_TYPE ("aa{sv}"));
      g_variant_builder_open (&b, G_VARIANT_TYPE_VARDICT);
      g_variant_builder_add (&b, "{sv}", "type",
          g_variant_new_int32 (WP_OBJECT_MANAGER_CONSTRAINT_PW_GLOBAL_PROPERTY));
      g_variant_builder_add (&b, "{sv}", "name",
          g_variant_new_string (PW_KEY_NODE_ID));
      g_variant_builder_add (&b, "{sv}", "value",
          g_variant_new_take_string (g_strdup_printf ("%u",
              wp_proxy_get_bound_id (WP_PROXY (self->node)))));
      g_variant_builder_close (&b);

      /* declare interest on ports with this constraint */
      wp_object_manager_add_interest (self->ports_om, WP_TYPE_PORT,
          g_variant_builder_end (&b), WP_PROXY_FEATURES_STANDARD);

      g_signal_connect_object (self->ports_om, "objects-changed",
          (GCallback) on_ports_changed, transition, 0);

      /* install the object manager */
      g_autoptr (WpCore) core = wp_proxy_get_core (WP_PROXY (self->node));
      wp_core_install_object_manager (core, self->ports_om);
      break;
    }
    default:
      WP_SESSION_ITEM_GET_CLASS (si_adapter_parent_class)->execute_step (item,
          transition, step);
      break;
  }
}

static void
si_adapter_class_init (WpSiAdapterClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpSessionItemClass *si_class = (WpSessionItemClass *) klass;

  object_class->finalize = si_adapter_finalize;

  si_class->configure = si_adapter_configure;
  si_class->get_configuration = si_adapter_get_configuration;
  si_class->get_next_step = si_adapter_get_next_step;
  si_class->execute_step = si_adapter_execute_step;
  si_class->reset = si_adapter_reset;
}

static guint
si_adapter_get_n_endpoints (WpSiMultiEndpoint * item)
{
  return 1;
}

static WpSiEndpoint *
si_adapter_get_endpoint (WpSiMultiEndpoint * item, guint index)
{
  g_return_val_if_fail (index == 0, NULL);
  return WP_SI_ENDPOINT (item);
}

static void
si_adapter_multi_endpoint_init (WpSiMultiEndpointInterface * iface)
{
  iface->get_n_endpoints = si_adapter_get_n_endpoints;
  iface->get_endpoint = si_adapter_get_endpoint;
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
      "endpoint.priority", self->priority,
      NULL);

  /* copy useful properties from the node */
  node_props = wp_proxy_get_properties (WP_PROXY (self->node));
  wp_properties_copy_keys (node_props, result,
      PW_KEY_DEVICE_ID,
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
  return WP_SI_ENDPOINT (self);
}

static void
si_adapter_stream_init (WpSiStreamInterface * iface)
{
  iface->get_registration_info = si_adapter_get_stream_registration_info;
  iface->get_properties = si_adapter_get_stream_properties;
  iface->get_parent_endpoint = si_adapter_get_stream_parent_endpoint;
}

WP_PLUGIN_EXPORT void
wireplumber__module_init (WpModule * module, WpCore * core, GVariant * args)
{
  GVariantBuilder b;

  g_variant_builder_init (&b, G_VARIANT_TYPE_VARDICT);
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
  g_variant_builder_add (&b, "(ssymv)", "enable-control-port", "b",
      WP_SI_CONFIG_OPTION_WRITEABLE, NULL);
  g_variant_builder_add (&b, "(ssymv)", "enable-monitor", "b",
      WP_SI_CONFIG_OPTION_WRITEABLE, NULL);
  g_variant_builder_add (&b, "(ssymv)", "direction", "y", 0, NULL);
  g_variant_builder_add (&b, "(ssymv)", "channels", "u", 0, NULL);

  wp_si_factory_register (core, wp_si_factory_new_simple (
      "si-adapter", si_adapter_get_type (), g_variant_builder_end (&b)));
}
