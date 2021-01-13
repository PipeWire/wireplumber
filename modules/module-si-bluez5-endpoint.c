/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <pipewire/pipewire.h>
#include <spa/utils/keys.h>
#include <spa/utils/names.h>

#include <wp/wp.h>

enum {
  STEP_VERIFY_CONFIG = WP_TRANSITION_STEP_CUSTOM_START,
  STEP_ACTIVATE_STREAM_A2DP,
  STEP_ACTIVATE_STREAM_SCO,
};

enum {
  STREAM_ID_A2DP = 0,
  STREAM_ID_SCO = 1,
};

static guint32
get_stream_id_from_profile_name (const gchar *profile_name) {
  if (g_str_has_prefix (profile_name, "a2dp"))
    return STREAM_ID_A2DP;
  else if (g_str_has_prefix (profile_name, "hsp") ||
      g_str_has_prefix (profile_name, "hfp"))
    return STREAM_ID_SCO;
  g_return_val_if_reached (SPA_ID_INVALID);
}

static gint
get_profile_id_from_stream_id (guint32 stream_id)
{
  switch (stream_id) {
  case STREAM_ID_A2DP:
    return 1;
  case STREAM_ID_SCO:
    return 2;
  default:
    break;
  }
  g_return_val_if_reached (0);
}

struct _WpSiBluez5Endpoint
{
  WpSessionBin parent;

  /* configuration */
  WpDevice *device;
  WpDirection direction;
  WpSessionItem *streams[2];
  guint priority;
  guint32 stream_id;
  gchar name[96];
  gboolean control_port;
  gboolean monitor;

  gint64 last_switch;
};

static void si_bluez5_endpoint_endpoint_init (WpSiEndpointInterface * iface);
static void si_bluez5_endpoint_stream_acquisition_init (WpSiStreamAcquisitionInterface * iface);

G_DECLARE_FINAL_TYPE(WpSiBluez5Endpoint, si_bluez5_endpoint,
                     WP, SI_BLUEZ5_ENDPOINT, WpSessionBin)
G_DEFINE_TYPE_WITH_CODE (WpSiBluez5Endpoint, si_bluez5_endpoint, WP_TYPE_SESSION_BIN,
    G_IMPLEMENT_INTERFACE (WP_TYPE_SI_ENDPOINT, si_bluez5_endpoint_endpoint_init)
    G_IMPLEMENT_INTERFACE (WP_TYPE_SI_STREAM_ACQUISITION, si_bluez5_endpoint_stream_acquisition_init))

static void
si_bluez5_endpoint_init (WpSiBluez5Endpoint * self)
{
}

static void
si_bluez5_endpoint_reset (WpSessionItem * item)
{
  WpSiBluez5Endpoint *self = WP_SI_BLUEZ5_ENDPOINT (item);

  /* unexport & deactivate first */
  WP_SESSION_ITEM_CLASS (si_bluez5_endpoint_parent_class)->reset (item);

  g_clear_object (&self->streams[STREAM_ID_A2DP]);
  g_clear_object (&self->streams[STREAM_ID_SCO]);
  self->direction = WP_DIRECTION_INPUT;
  self->stream_id = 0;
  self->name[0] = '\0';
  self->control_port = FALSE;
  self->monitor = FALSE;

  wp_session_item_clear_flag (item, WP_SI_FLAG_CONFIGURED);
}

static gpointer
si_bluez5_endpoint_get_associated_proxy (WpSessionItem * item, GType proxy_type)
{
  WpSiBluez5Endpoint *self = WP_SI_BLUEZ5_ENDPOINT (item);

  if (proxy_type == WP_TYPE_NODE) {
    if (self->streams[STREAM_ID_A2DP])
      return wp_session_item_get_associated_proxy (
          self->streams[STREAM_ID_A2DP], proxy_type);
    if (self->streams[STREAM_ID_SCO])
      return wp_session_item_get_associated_proxy (
          self->streams[STREAM_ID_SCO], proxy_type);
    return NULL;
  }

  return WP_SESSION_ITEM_CLASS (si_bluez5_endpoint_parent_class)->
      get_associated_proxy (item, proxy_type);
}

static GVariant *
si_bluez5_endpoint_get_configuration (WpSessionItem * item)
{
  WpSiBluez5Endpoint *self = WP_SI_BLUEZ5_ENDPOINT (item);
  g_autoptr (WpNode) node = NULL;
  GVariantBuilder b;

  /* Get the bluez5 node */
  if (self->streams[self->stream_id])
    node = wp_session_item_get_associated_proxy (self->streams[self->stream_id],
        WP_TYPE_NODE);

  /* Set the properties */
  g_variant_builder_init (&b, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&b, "{sv}", "device",
      g_variant_new_uint64 ((guint64) self->device));
  g_variant_builder_add (&b, "{sv}", "name",
      g_variant_new_string (self->name));
  g_variant_builder_add (&b, "{sv}", "direction",
      g_variant_new_uint32 (self->direction));
  g_variant_builder_add (&b, "{sv}", "s2dp-stream",
      g_variant_new_boolean (self->streams[STREAM_ID_A2DP] != NULL));
  g_variant_builder_add (&b, "{sv}", "sco-stream",
      g_variant_new_boolean (self->streams[STREAM_ID_SCO] != NULL));
  g_variant_builder_add (&b, "{sv}", "node",
      g_variant_new_uint64 ((guint64) node));
  g_variant_builder_add (&b, "{sv}", "priority",
      g_variant_new_uint32 (self->priority));
  g_variant_builder_add (&b, "{sv}", "enable-control-port",
      g_variant_new_boolean (self->control_port));
  g_variant_builder_add (&b, "{sv}", "enable-monitor",
      g_variant_new_boolean (self->monitor));
  return g_variant_builder_end (&b);
}

static void
si_bluez5_endpoint_add_stream (WpSiBluez5Endpoint * self, guint32 stream_id,
    const gchar *name, WpNode *node)
{
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self->device));
  g_auto (GVariantBuilder) b = G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE_VARDICT);

  /* Bluez5 stream or Fake stream */
  if (self->stream_id == stream_id && node) {
    self->streams[stream_id] = wp_session_item_make (core, "si-adapter");
    g_variant_builder_add (&b, "{sv}", "node",
        g_variant_new_uint64 ((guint64) node));
    g_variant_builder_add (&b, "{sv}", "name",
        g_variant_new_string (name));
    g_variant_builder_add (&b, "{sv}", "enable-control-port",
        g_variant_new_boolean (self->control_port));
    g_variant_builder_add (&b, "{sv}", "enable-monitor",
        g_variant_new_boolean (self->monitor));
  }
  else {
    self->streams[stream_id] = wp_session_item_make (core, "si-fake-stream");
    g_variant_builder_add (&b, "{sv}", "name",
        g_variant_new_string (name));
  }

  /* Configure */
  wp_session_item_configure (self->streams[stream_id],
        g_variant_builder_end (&b));

  /* Add stream to the bin */
  wp_session_bin_add (WP_SESSION_BIN (self),
      g_object_ref (self->streams[stream_id]));
}

static gboolean
si_bluez5_endpoint_configure (WpSessionItem * item, GVariant * args)
{
  WpSiBluez5Endpoint *self = WP_SI_BLUEZ5_ENDPOINT (item);
  guint64 device_i, node_i;
  const gchar *name;
  WpNode *node = NULL;
  gboolean a2dp_stream = FALSE, sco_stream = FALSE;

  if (wp_session_item_get_flags (item) & (WP_SI_FLAG_ACTIVATING | WP_SI_FLAG_ACTIVE))
    return FALSE;

  /* reset previous config */
  si_bluez5_endpoint_reset (WP_SESSION_ITEM (self));

  /* get the device */
  if (!g_variant_lookup (args, "device", "t", &device_i))
    return FALSE;
  g_return_val_if_fail (WP_IS_DEVICE (GUINT_TO_POINTER (device_i)), FALSE);
  self->device = g_object_ref (GUINT_TO_POINTER (device_i));

  /* get the name */
  if (!g_variant_lookup (args, "name", "&s", &name))
    return FALSE;
  strncpy (self->name, name, sizeof (self->name) - 1);

  /* get the node */
  if (g_variant_lookup (args, "node", "t", &node_i))
    node = GUINT_TO_POINTER (node_i);

  /* get the direction */
  if (node) {
    const gchar *media_class = wp_pipewire_object_get_property (
        WP_PIPEWIRE_OBJECT (node), PW_KEY_MEDIA_CLASS);
    self->direction = g_strcmp0 (media_class, "Audio/Sink") == 0 ?
        WP_DIRECTION_INPUT : WP_DIRECTION_OUTPUT;
  } else {
    if (!g_variant_lookup (args, "direction", "u", &self->direction)) {
      wp_warning_object (self, "direction not specified");
      return FALSE;
    }
  }

  /* get the a2dp-stream value */
  if (!g_variant_lookup (args, "a2dp-stream", "b", &a2dp_stream))
    return FALSE;
  if (!g_variant_lookup (args, "sco-stream", "b", &sco_stream))
    return FALSE;
  if (!a2dp_stream && !sco_stream)
    return FALSE;

  /* get priority, control-port and monitor */
  g_variant_lookup (args, "priority", "b", &self->priority);
  g_variant_lookup (args, "enable-control-port", "b", &self->control_port);
  g_variant_lookup (args, "enable-monitor", "b", &self->monitor);

  /* get the current stream id */
  if (node) {
    self->stream_id = get_stream_id_from_profile_name (
        wp_pipewire_object_get_property (WP_PIPEWIRE_OBJECT (node),
            SPA_KEY_API_BLUEZ5_PROFILE));
  } else {
    /* Otherwise, the device is set with the oposite profile */
    if (a2dp_stream && !sco_stream)
      self->stream_id = STREAM_ID_SCO;
    else if (sco_stream && !a2dp_stream)
      self->stream_id = STREAM_ID_A2DP;
    else
      return FALSE;
  }

  /* create the streams and add them into the bin */
  if (a2dp_stream)
    si_bluez5_endpoint_add_stream (self, STREAM_ID_A2DP, "Multimedia", node);
  if (sco_stream)
    si_bluez5_endpoint_add_stream (self, STREAM_ID_SCO, "Call", node);

  /* update last profile switch time */
  self->last_switch = g_get_monotonic_time ();

  wp_session_item_set_flag (item, WP_SI_FLAG_CONFIGURED);
  return TRUE;
}

static guint
si_bluez5_endpoint_activate_get_next_step (WpSessionItem * item,
     WpTransition * transition, guint step)
{
  switch (step) {
    case WP_TRANSITION_STEP_NONE:
      return STEP_VERIFY_CONFIG;

    case STEP_VERIFY_CONFIG:
      return STEP_ACTIVATE_STREAM_A2DP;

    case STEP_ACTIVATE_STREAM_A2DP:
      return STEP_ACTIVATE_STREAM_SCO;

    case STEP_ACTIVATE_STREAM_SCO:
      return WP_TRANSITION_STEP_NONE;

    default:
      return WP_TRANSITION_STEP_ERROR;
  }
}

static void
on_item_activated (WpSessionItem * item, GAsyncResult * res,
    WpTransition *transition)
{
  g_autoptr (GError) error = NULL;

  if (!wp_session_item_activate_finish (item, res, &error)) {
    wp_transition_return_error (transition, g_steal_pointer (&error));
    return;
  }

  wp_transition_advance (transition);
}

static void
activate_stream (WpSiBluez5Endpoint *self, guint32 id, WpTransition *transition)
{
  if (self->streams[id])
    wp_session_item_activate (self->streams[id],
      (GAsyncReadyCallback) on_item_activated, transition);
  else
    wp_transition_advance (transition);
}

static void
si_bluez5_endpoint_activate_execute_step (WpSessionItem * item,
    WpTransition * transition, guint step)
{
  WpSiBluez5Endpoint *self = WP_SI_BLUEZ5_ENDPOINT (item);

  switch (step) {
    case STEP_VERIFY_CONFIG:
      if (G_UNLIKELY (!(wp_session_item_get_flags (item) & WP_SI_FLAG_CONFIGURED))) {
        wp_transition_return_error (transition,
            g_error_new (WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVARIANT,
                "si-bluez5-endpoint: cannot activate item without it "
                "being configured first"));
      }
      wp_transition_advance (transition);
      break;

    case STEP_ACTIVATE_STREAM_A2DP:
      activate_stream (self, STREAM_ID_A2DP, transition);
      break;

    case STEP_ACTIVATE_STREAM_SCO:
      activate_stream (self, STREAM_ID_SCO, transition);
      break;

    default:
      g_return_if_reached ();
  }
}

static void
si_bluez5_endpoint_activate_rollback (WpSessionItem * item)
{
  WpSiBluez5Endpoint *self = WP_SI_BLUEZ5_ENDPOINT (item);
  g_autoptr (WpIterator) it = wp_session_bin_iterate (WP_SESSION_BIN (self));
  g_auto (GValue) val = G_VALUE_INIT;

  /* deactivate all items */
  for (; wp_iterator_next (it, &val); g_value_unset (&val))
    wp_session_item_deactivate (g_value_get_object (&val));
}

static void
si_bluez5_endpoint_class_init (WpSiBluez5EndpointClass * klass)
{
  WpSessionItemClass *si_class = (WpSessionItemClass *) klass;

  si_class->reset = si_bluez5_endpoint_reset;
  si_class->get_associated_proxy = si_bluez5_endpoint_get_associated_proxy;
  si_class->configure = si_bluez5_endpoint_configure;
  si_class->get_configuration = si_bluez5_endpoint_get_configuration;
  si_class->activate_get_next_step = si_bluez5_endpoint_activate_get_next_step;
  si_class->activate_execute_step = si_bluez5_endpoint_activate_execute_step;
  si_class->activate_rollback = si_bluez5_endpoint_activate_rollback;
}

static GVariant *
si_bluez5_endpoint_get_registration_info (WpSiEndpoint * item)
{
  WpSiBluez5Endpoint *self = WP_SI_BLUEZ5_ENDPOINT (item);
  GVariantBuilder b;

  g_variant_builder_init (&b, G_VARIANT_TYPE ("(ssya{ss})"));
  g_variant_builder_add (&b, "s", self->name);
  g_variant_builder_add (&b, "s", self->direction == WP_DIRECTION_INPUT ?
          "Audio/Sink" : "Audio/Source");
  g_variant_builder_add (&b, "y", self->direction);
  g_variant_builder_add (&b, "a{ss}", NULL);

  return g_variant_builder_end (&b);
}

static WpProperties *
si_bluez5_endpoint_get_properties (WpSiEndpoint * item)
{
  WpSiBluez5Endpoint *self = WP_SI_BLUEZ5_ENDPOINT (item);
  WpProperties *properties;
  g_autofree gchar *description = NULL;

  properties =
      wp_pipewire_object_get_properties (WP_PIPEWIRE_OBJECT (self->device));
  properties = wp_properties_ensure_unique_owner (properties);
  description = g_strdup_printf ("Bluez5-%s of %s",
      self->direction == WP_DIRECTION_INPUT ? "Sink" : "Source",
      wp_properties_get (properties, PW_KEY_DEVICE_NAME));
  wp_properties_set (properties, "endpoint.description", description);
  wp_properties_setf (properties, "endpoint.priority", "%d", self->priority);

  return properties;
}

static guint
si_bluez5_endpoint_get_n_streams (WpSiEndpoint * item)
{
  WpSiBluez5Endpoint *self = WP_SI_BLUEZ5_ENDPOINT (item);
  guint res = 0;
  if (self->streams[STREAM_ID_A2DP])
    res++;
  if (self->streams[STREAM_ID_SCO])
    res++;
  return res;
}

static WpSiStream *
si_bluez5_endpoint_get_stream (WpSiEndpoint * item, guint index)
{
  WpSiBluez5Endpoint *self = WP_SI_BLUEZ5_ENDPOINT (item);
  guint n_streams = si_bluez5_endpoint_get_n_streams (item);
  WpSessionItem *res = NULL;
  g_return_val_if_fail (index < n_streams, NULL);
  if (!self->streams[STREAM_ID_A2DP])
    index++;
  res = self->streams[index];
  g_return_val_if_fail (res, NULL);
  return WP_SI_STREAM (g_object_ref (res));
}

static WpSiStreamAcquisition *
si_bluez5_endpoint_get_stream_acquisition (WpSiEndpoint * self)
{
  return WP_SI_STREAM_ACQUISITION (self);
}

static void
si_bluez5_endpoint_endpoint_init (WpSiEndpointInterface * iface)
{
  iface->get_registration_info = si_bluez5_endpoint_get_registration_info;
  iface->get_properties = si_bluez5_endpoint_get_properties;
  iface->get_n_streams = si_bluez5_endpoint_get_n_streams;
  iface->get_stream = si_bluez5_endpoint_get_stream;
  iface->get_stream_acquisition = si_bluez5_endpoint_get_stream_acquisition;
}

static void
set_device_profile (WpDevice *device, gint index)
{
  g_return_if_fail (device);
  g_autoptr (WpSpaPod) profile = wp_spa_pod_new_object (
      "Spa:Pod:Object:Param:Profile", "Profile",
      "index", "i", index,
      NULL);
  wp_pipewire_object_set_param (WP_PIPEWIRE_OBJECT (device),
      "Profile", 0, profile);
}

static void
abort_acquisition (WpSessionItem * ac, GTask *task, const gchar *msg)
{
  g_autoptr (WpGlobalProxy) link = NULL;

  /* return error to abort the link activation */
  g_task_return_new_error (task, WP_DOMAIN_LIBRARY,
      WP_LIBRARY_ERROR_OPERATION_FAILED, "%s", msg);

  /* destroy the link */
  link = wp_session_item_get_associated_proxy (ac, WP_TYPE_ENDPOINT_LINK);
  g_return_if_fail (link);
  wp_global_proxy_request_destroy (link);
}

static void
si_bluez5_endpoint_stream_acquisition_acquire (WpSiStreamAcquisition * sa,
    WpSiLink * acquisitor, WpSiStream * stream, GAsyncReadyCallback callback,
    gpointer data)
{
  WpSiBluez5Endpoint *self = WP_SI_BLUEZ5_ENDPOINT (sa);
  g_autoptr (GTask) task = NULL;
  gint64 now = g_get_monotonic_time ();

  /* create the task */
  task = g_task_new (self, NULL, callback, data);

  /* accept acquisition if stream is valid */
  if (WP_SESSION_ITEM (stream) == self->streams[self->stream_id]) {
    g_task_return_boolean (task, TRUE);
    return;
  }

  /* abort acquisition if we changed the bluez5 profile less than a second ago.
   * This prevents an switch profile infinite loop caused by the policy module
   * when 2 or more clients with different profiles want to link with the same
   * bluez5 endpoint */
  if (now - self->last_switch < 1000000) {
    abort_acquisition (WP_SESSION_ITEM (acquisitor), task,
        "already switched bluez5 profile recently");
    return;
  }

  /* switch profile */
  set_device_profile (self->device,
      get_profile_id_from_stream_id (self->stream_id) == 1 ? 2 : 1);

  /* update last switch time */
  self->last_switch = now;

  /* abort acquisition */
  abort_acquisition (WP_SESSION_ITEM (acquisitor), task,
      "new bluez5 profile set");
}

static void
si_bluez5_endpoint_stream_acquisition_release (WpSiStreamAcquisition * sa,
    WpSiLink * acquisitor, WpSiStream * stream)
{
}

static gboolean
si_bluez5_endpoint_stream_acquisition_acquire_finish (
    WpSiStreamAcquisition * sa, GAsyncResult * res, GError ** error)
{
  g_return_val_if_fail (WP_IS_SI_STREAM_ACQUISITION (sa), FALSE);
  g_return_val_if_fail (g_task_is_valid (res, sa), FALSE);

  return g_task_propagate_boolean (G_TASK (res), error);
}

static void
si_bluez5_endpoint_stream_acquisition_init (
    WpSiStreamAcquisitionInterface * iface)
{
  iface->acquire = si_bluez5_endpoint_stream_acquisition_acquire;
  iface->acquire_finish = si_bluez5_endpoint_stream_acquisition_acquire_finish;
  iface->release = si_bluez5_endpoint_stream_acquisition_release;
}

WP_PLUGIN_EXPORT void
wireplumber__module_init (WpModule * module, WpCore * core, GVariant * args)
{
  GVariantBuilder b;

  g_variant_builder_init (&b, G_VARIANT_TYPE ("a(ssymv)"));
  g_variant_builder_add (&b, "(ssymv)", "device", "t",
      WP_SI_CONFIG_OPTION_WRITEABLE | WP_SI_CONFIG_OPTION_REQUIRED, NULL);
  g_variant_builder_add (&b, "(ssymv)", "name", "s",
      WP_SI_CONFIG_OPTION_WRITEABLE | WP_SI_CONFIG_OPTION_REQUIRED, NULL);
  g_variant_builder_add (&b, "(ssymv)", "direction", "u",
      WP_SI_CONFIG_OPTION_WRITEABLE | WP_SI_CONFIG_OPTION_REQUIRED, NULL);
  g_variant_builder_add (&b, "(ssymv)", "a2dp-stream", "b",
      WP_SI_CONFIG_OPTION_WRITEABLE | WP_SI_CONFIG_OPTION_REQUIRED, NULL);
  g_variant_builder_add (&b, "(ssymv)", "sco-stream", "b",
      WP_SI_CONFIG_OPTION_WRITEABLE | WP_SI_CONFIG_OPTION_REQUIRED, NULL);
  g_variant_builder_add (&b, "(ssymv)", "node", "t",
      WP_SI_CONFIG_OPTION_WRITEABLE, NULL);
  g_variant_builder_add (&b, "(ssymv)", "priority", "u",
      WP_SI_CONFIG_OPTION_WRITEABLE, NULL);
  g_variant_builder_add (&b, "(ssymv)", "enable-control-port", "b",
      WP_SI_CONFIG_OPTION_WRITEABLE, NULL);
  g_variant_builder_add (&b, "(ssymv)", "enable-monitor", "b",
      WP_SI_CONFIG_OPTION_WRITEABLE, NULL);

  wp_si_factory_register (core, wp_si_factory_new_simple (
      "si-bluez5-endpoint", si_bluez5_endpoint_get_type (),
      g_variant_builder_end (&b)));
}
