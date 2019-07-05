/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/pod/builder.h>
#include <spa/param/props.h>

#include "dsp.h"

#define MIN_QUANTUM_SIZE  64
#define MAX_QUANTUM_SIZE  1024

enum {
  PROP_0,
  PROP_ENDPOINT,
  PROP_ID,
  PROP_NAME,
  PROP_DIRECTION,
  PROP_TARGET,
  PROP_FORMAT,
};

enum {
  CONTROL_VOLUME = 0,
  CONTROL_MUTE,
  N_CONTROLS,
};

struct _WpPwAudioDsp
{
  GObject parent;

  /* The task to signal the audio dsp is initialized */
  GTask *init_task;

  /* The remote pipewire */
  WpRemotePipewire *remote_pipewire;

  /* Handler */
  gulong proxy_done_handler_id;

  /* Props */
  GWeakRef endpoint;
  guint id;
  gchar *name;
  enum pw_direction direction;
  const struct pw_node_info *target;
  const struct spa_audio_info_raw *format;

  /* Proxies */
  WpProxyNode *proxy;
  GPtrArray *port_proxies;
  struct pw_proxy *link_proxy;

  /* Listener */
  struct spa_hook listener;

  /* Volume */
  gfloat volume;
  gboolean mute;
};

static void wp_pw_audio_dsp_async_initable_init (gpointer iface,
    gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (WpPwAudioDsp, wp_pw_audio_dsp, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE,
                           wp_pw_audio_dsp_async_initable_init))

guint
wp_pw_audio_dsp_id_encode (guint stream_id, guint control_id)
{
  g_return_val_if_fail (control_id < N_CONTROLS, 0);

  /* encode NONE as 0 and everything else with +1 */
  /* NONE is MAX_UINT, so +1 will do the trick */
  stream_id += 1;

  /* Encode the stream and control Ids. The first ID is reserved
   * for the "selected" control, registered in the endpoint */
  return 1 + (stream_id * N_CONTROLS) + control_id;
}

void
wp_pw_audio_dsp_id_decode (guint id, guint *stream_id, guint *control_id)
{
  guint s_id, c_id;

  g_return_if_fail (id >= 1);
  id -= 1;

  /* Decode the stream and control Ids */
  s_id = (id / N_CONTROLS) - 1;
  c_id = id % N_CONTROLS;

  /* Set the output params */
  if (stream_id)
    *stream_id = s_id;
  if (control_id)
    *control_id = c_id;
}

static void
register_controls (WpPwAudioDsp * self)
{
  GVariantDict d;
  g_autoptr (WpEndpoint) ep = g_weak_ref_get (&self->endpoint);
  g_return_if_fail (ep);

  /* Register the volume control */
  g_variant_dict_init (&d, NULL);
  g_variant_dict_insert (&d, "id", "u",
      wp_pw_audio_dsp_id_encode (self->id, CONTROL_VOLUME));
  if (self->id != WP_STREAM_ID_NONE)
    g_variant_dict_insert (&d, "stream-id", "u", self->id);
  g_variant_dict_insert (&d, "name", "s", "volume");
  g_variant_dict_insert (&d, "type", "s", "d");
  g_variant_dict_insert (&d, "range", "(dd)", 0.0, 1.0);
  g_variant_dict_insert (&d, "default-value", "d", self->volume);
  wp_endpoint_register_control (ep, g_variant_dict_end (&d));

  /* Register the mute control */
  g_variant_dict_init (&d, NULL);
  g_variant_dict_insert (&d, "id", "u",
      wp_pw_audio_dsp_id_encode (self->id, CONTROL_MUTE));
  if (self->id != WP_STREAM_ID_NONE)
    g_variant_dict_insert (&d, "stream-id", "u", self->id);
  g_variant_dict_insert (&d, "name", "s", "mute");
  g_variant_dict_insert (&d, "type", "s", "b");
  g_variant_dict_insert (&d, "default-value", "b", self->mute);
  wp_endpoint_register_control (ep, g_variant_dict_end (&d));
}

static void
on_audio_dsp_done(WpProxy *proxy, gpointer data)
{
  WpPwAudioDsp *self = data;

  /* Don't do anything if the endpoint has already been initialized */
  if (!self->init_task)
    return;

  /* Register the controls */
  register_controls (self);

  /* Finish the creation of the audio dsp */
  g_task_return_boolean (self->init_task, TRUE);
  g_clear_object(&self->init_task);
}

static void
on_audio_dsp_port_created(GObject *initable, GAsyncResult *res,
    gpointer data)
{
  WpPwAudioDsp *self = data;
  WpProxyPort *port_proxy = NULL;

  /* Get the proxy port */
  port_proxy = wp_proxy_port_new_finish(initable, res, NULL);
  g_return_if_fail (port_proxy);

  /* Add the proxy port to the array */
  g_return_if_fail (self->port_proxies);
  g_ptr_array_add(self->port_proxies, port_proxy);

  /* Register a callback to know when all the dsp ports have been emitted */
  if (!self->proxy_done_handler_id) {
    self->proxy_done_handler_id = g_signal_connect_object(self->proxy,
        "done", (GCallback)on_audio_dsp_done, self, 0);
    wp_proxy_sync (WP_PROXY(self->proxy));
  }
}

static void
on_audio_dsp_port_added(WpRemotePipewire *rp, guint id, guint parent_id,
    gconstpointer p, gpointer d)
{
  WpPwAudioDsp *self = d;
  const struct pw_node_info *dsp_info = NULL;
  struct pw_port_proxy *port_proxy = NULL;

  /* Make sure the port belongs to this audio dsp */
  if (!self->proxy)
    return;
  dsp_info = wp_proxy_node_get_info (self->proxy);
  if (!dsp_info || dsp_info->id != parent_id)
    return;

  /* Create the audio dsp port async */
  port_proxy = wp_remote_pipewire_proxy_bind (self->remote_pipewire, id,
      PW_TYPE_INTERFACE_Port);
  g_return_if_fail(port_proxy);
  wp_proxy_port_new(id, port_proxy, on_audio_dsp_port_created, self);
}

static void
on_audio_dsp_running(WpPwAudioDsp *self)
{
  struct pw_properties *props;
  const struct pw_node_info *dsp_info = NULL;

  /* Return if the node has already been linked */
  if (self->link_proxy)
    return;

  /* Get the dsp info */
  dsp_info = wp_proxy_node_get_info(self->proxy);
  g_return_if_fail (dsp_info);

  /* Create new properties */
  props = pw_properties_new(NULL, NULL);

  /* Set the new properties */
  pw_properties_set(props, PW_LINK_PROP_PASSIVE, "true");
  if (self->direction == PW_DIRECTION_OUTPUT) {
    pw_properties_setf(props, PW_LINK_OUTPUT_NODE_ID, "%d", dsp_info->id);
    pw_properties_setf(props, PW_LINK_OUTPUT_PORT_ID, "%d", -1);
    pw_properties_setf(props, PW_LINK_INPUT_NODE_ID, "%d", self->target->id);
    pw_properties_setf(props, PW_LINK_INPUT_PORT_ID, "%d", -1);
  } else {
    pw_properties_setf(props, PW_LINK_OUTPUT_NODE_ID, "%d", self->target->id);
    pw_properties_setf(props, PW_LINK_OUTPUT_PORT_ID, "%d", -1);
    pw_properties_setf(props, PW_LINK_INPUT_NODE_ID, "%d", dsp_info->id);
    pw_properties_setf(props, PW_LINK_INPUT_PORT_ID, "%d", -1);
  }

  g_debug ("%p linking DSP to node", self);

  /* Create the link */
  self->link_proxy = wp_remote_pipewire_create_object(self->remote_pipewire,
      "link-factory", PW_TYPE_INTERFACE_Link, &props->dict);

  /* Clean up */
  pw_properties_free(props);
}

static void
on_audio_dsp_idle (WpPwAudioDsp *self)
{
  if (self->link_proxy != NULL) {
    pw_proxy_destroy (self->link_proxy);
    self->link_proxy = NULL;
  }
}

static void
audio_dsp_event_info (void *data, const struct pw_node_info *info)
{
  WpPwAudioDsp *self = data;

  /* Handle the different states */
  switch (info->state) {
  case PW_NODE_STATE_IDLE:
    on_audio_dsp_idle (self);
    break;
  case PW_NODE_STATE_RUNNING:
    on_audio_dsp_running (self);
    break;
  case PW_NODE_STATE_SUSPENDED:
    break;
  default:
    break;
  }
}

static void
audio_dsp_event_param (void *object, int seq, uint32_t id,
    uint32_t index, uint32_t next, const struct spa_pod *param)
{
  WpPwAudioDsp *self = WP_PW_AUDIO_DSP (object);

  switch (id) {
    case SPA_PARAM_Props:
    {
      struct spa_pod_prop *prop;
      struct spa_pod_object *obj = (struct spa_pod_object *) param;
      float volume = self->volume;
      bool mute = self->mute;

      SPA_POD_OBJECT_FOREACH(obj, prop) {
        switch (prop->key) {
        case SPA_PROP_volume:
          spa_pod_get_float(&prop->value, &volume);
          break;
        case SPA_PROP_mute:
          spa_pod_get_bool(&prop->value, &mute);
          break;
        default:
          break;
        }
      }

      g_debug ("WpPwAudioDsp:%p param event, vol:(%lf -> %f) mute:(%d -> %d)",
          self, self->volume, volume, self->mute, mute);

      if (self->volume != volume) {
        self->volume = volume;
        wp_endpoint_notify_control_value (WP_ENDPOINT (self),
            wp_pw_audio_dsp_id_encode (self->id, CONTROL_VOLUME));
      }
      if (self->mute != mute) {
        self->mute = mute;
        wp_endpoint_notify_control_value (WP_ENDPOINT (self),
            wp_pw_audio_dsp_id_encode (self->id, CONTROL_MUTE));
      }

      break;
    }
    default:
      break;
  }
}

static const struct pw_node_proxy_events audio_dsp_proxy_events = {
  PW_VERSION_NODE_PROXY_EVENTS,
  .info = audio_dsp_event_info,
  .param = audio_dsp_event_param,
};

static void
on_audio_dsp_proxy_created(GObject *initable, GAsyncResult *res,
    gpointer data)
{
  WpPwAudioDsp *self = data;
  struct pw_node_proxy *pw_proxy = NULL;
  struct spa_audio_info_raw format;
  uint8_t buf[1024];
  struct spa_pod_builder pod_builder = { 0, };
  struct spa_pod *param;

  /* Get the audio dsp proxy */
  self->proxy = wp_proxy_node_new_finish(initable, res, NULL);
  g_return_if_fail (self->proxy);

  /* Add a custom dsp listener */
  pw_proxy = wp_proxy_get_pw_proxy(WP_PROXY(self->proxy));
  g_return_if_fail (pw_proxy);
  pw_node_proxy_add_listener(pw_proxy, &self->listener,
      &audio_dsp_proxy_events, self);

  /* Emit the props param */
  pw_node_proxy_enum_params (pw_proxy, 0, SPA_PARAM_Props, 0, -1, NULL);

  /* Get the port format */
  g_return_if_fail (self->format);
  format = *self->format;

  /* Emit the ports */
  spa_pod_builder_init(&pod_builder, buf, sizeof(buf));
  param = spa_format_audio_raw_build(&pod_builder, SPA_PARAM_Format, &format);
  param = spa_pod_builder_add_object(&pod_builder,
      SPA_TYPE_OBJECT_ParamProfile, SPA_PARAM_Profile,
      SPA_PARAM_PROFILE_direction,  SPA_POD_Id(pw_direction_reverse(self->direction)),
      SPA_PARAM_PROFILE_format,     SPA_POD_Pod(param));
  pw_node_proxy_set_param(pw_proxy, SPA_PARAM_Profile, 0, param);
}

static void
wp_pw_audio_dsp_finalize (GObject * object)
{
  WpPwAudioDsp *self = WP_PW_AUDIO_DSP (object);

  /* Props */
  g_weak_ref_clear (&self->endpoint);
  g_free (self->name);

  /* Destroy the init task */
  g_clear_object(&self->init_task);

  /* Destroy the proxy dsp */
  g_clear_object(&self->proxy);

  /* Destroy the proxies port */
  if (self->port_proxies) {
    g_ptr_array_free(self->port_proxies, TRUE);
    self->port_proxies = NULL;
  }

  G_OBJECT_CLASS (wp_pw_audio_dsp_parent_class)->finalize (object);
}

static void
wp_pw_audio_dsp_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpPwAudioDsp *self = WP_PW_AUDIO_DSP (object);

  switch (property_id) {
  case PROP_ENDPOINT:
    g_weak_ref_set (&self->endpoint, g_value_get_object (value));
    break;
  case PROP_ID:
    self->id = g_value_get_uint(value);
    break;
  case PROP_NAME:
    self->name = g_value_dup_string (value);
    break;
  case PROP_DIRECTION:
    self->direction = g_value_get_uint(value);
    break;
  case PROP_TARGET:
    self->target = g_value_get_pointer(value);
    break;
  case PROP_FORMAT:
    self->format = g_value_get_pointer(value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_pw_audio_dsp_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpPwAudioDsp *self = WP_PW_AUDIO_DSP (object);

  switch (property_id) {
    case PROP_ENDPOINT:
    g_value_take_object (value, g_weak_ref_get (&self->endpoint));
    break;
  case PROP_ID:
    g_value_set_uint (value, self->id);
    break;
  case PROP_NAME:
    g_value_set_string (value, self->name);
    break;
  case PROP_DIRECTION:
    g_value_set_uint (value, self->direction);
    break;
  case PROP_TARGET:
    g_value_set_pointer (value, (gpointer)self->target);
    break;
  case PROP_FORMAT:
    g_value_set_pointer (value, (gpointer)self->format);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_pw_audio_dsp_init_async (GAsyncInitable *initable, int io_priority,
    GCancellable *cancellable, GAsyncReadyCallback callback, gpointer data)
{
  WpPwAudioDsp *self = WP_PW_AUDIO_DSP (initable);
  struct pw_properties *props;
  struct pw_node_proxy *proxy;

  /* Set the remote pipewire */
  g_autoptr (WpEndpoint) ep = g_weak_ref_get (&self->endpoint);
  g_return_if_fail(ep);
  g_autoptr (WpCore) wp_core = wp_endpoint_get_core(ep);
  g_return_if_fail(wp_core);
  self->remote_pipewire =
      wp_core_get_global (wp_core, WP_GLOBAL_REMOTE_PIPEWIRE);
  g_return_if_fail(self->remote_pipewire);

  /* Create the async task */
  self->init_task = g_task_new (initable, cancellable, callback, data);

  /* Init the list of port proxies */
  self->port_proxies = g_ptr_array_new_full(4, (GDestroyNotify)g_object_unref);

  /* Set the default volume */
  self->volume = 1.0;
  self->mute = FALSE;

  /* Create the properties */
  props = pw_properties_new_dict(self->target->props);
  g_return_if_fail (props);

  /* Set the properties */
  pw_properties_set(props, "audio-dsp.name",
      self->name ? self->name : "Audio-DSP");
  pw_properties_setf(props, "audio-dsp.direction", "%d", self->direction);
  pw_properties_setf(props, "audio-dsp.maxbuffer", "%ld",
      MAX_QUANTUM_SIZE * sizeof(float));

  /* Register a port_added callback */
  g_signal_connect_object(self->remote_pipewire, "global-added::port",
      (GCallback)on_audio_dsp_port_added, self, 0);

  /* Create the proxy async */
  proxy = wp_remote_pipewire_create_object(self->remote_pipewire,
      "audio-dsp", PW_TYPE_INTERFACE_Node, &props->dict);
  wp_proxy_node_new(pw_proxy_get_id((struct pw_proxy *)proxy), proxy,
      on_audio_dsp_proxy_created, self);

  /* Clean up */
  pw_properties_free(props);
}

static gboolean
wp_pw_audio_dsp_init_finish (GAsyncInitable *initable, GAsyncResult *result,
    GError **error)
{
  g_return_val_if_fail (g_task_is_valid (result, initable), FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

static void
wp_pw_audio_dsp_async_initable_init (gpointer iface, gpointer iface_data)
{
  GAsyncInitableIface *ai_iface = iface;

  ai_iface->init_async = wp_pw_audio_dsp_init_async;
  ai_iface->init_finish = wp_pw_audio_dsp_init_finish;
}

static void
wp_pw_audio_dsp_init (WpPwAudioDsp * self)
{
}

static void
wp_pw_audio_dsp_class_init (WpPwAudioDspClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;

  object_class->finalize = wp_pw_audio_dsp_finalize;
  object_class->set_property = wp_pw_audio_dsp_set_property;
  object_class->get_property = wp_pw_audio_dsp_get_property;

  /* Install the properties */
  g_object_class_install_property (object_class, PROP_ENDPOINT,
      g_param_spec_object ("endpoint", "endpoint",
          "The endpoint this audio DSP belongs to", WP_TYPE_ENDPOINT,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class, PROP_ID,
      g_param_spec_uint ("id", "id", "The Id of the audio DSP", 0, G_MAXUINT, 0,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class, PROP_NAME,
      g_param_spec_string ("name", "name", "The name of the audio DSP", NULL,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class, PROP_DIRECTION,
      g_param_spec_uint ("direction", "direction",
          "The direction of the audio DSP", 0, 1, 0,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class, PROP_TARGET,
      g_param_spec_pointer ("target", "target",
          "The target node info of the audio DSP",
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class, PROP_FORMAT,
      g_param_spec_pointer ("format", "format",
          "The format of the audio DSP ports",
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

void
wp_pw_audio_dsp_new (WpEndpoint *endpoint, guint id, const char *name,
    enum pw_direction direction, const struct pw_node_info *target,
    const struct spa_audio_info_raw *format, GAsyncReadyCallback callback,
    gpointer user_data)
{
  g_async_initable_new_async (
      wp_pw_audio_dsp_get_type (), G_PRIORITY_DEFAULT, NULL,
      callback, user_data,
      "endpoint", endpoint,
      "id", id,
      "name", name,
      "direction", direction,
      "target", target,
      "format", format,
      NULL);
}

WpPwAudioDsp *
wp_pw_audio_dsp_new_finish (GObject *initable, GAsyncResult *res,
    GError **error)
{
  GAsyncInitable *ai = G_ASYNC_INITABLE(initable);
  return WP_PW_AUDIO_DSP(g_async_initable_new_finish(ai, res, error));
}

const struct pw_node_info *
wp_pw_audio_dsp_get_info (WpPwAudioDsp * self)
{
  return wp_proxy_node_get_info(self->proxy);
}

static void
port_proxies_foreach_func(gpointer data, gpointer user_data)
{
  GVariantBuilder *b = user_data;
  g_variant_builder_add (b, "t", data);
}

gboolean
wp_pw_audio_dsp_prepare_link (WpPwAudioDsp * self, GVariant ** properties,
    GError ** error) {
  const struct pw_node_info *info = NULL;
  GVariantBuilder b, *b_ports;
  GVariant *v_ports;

  /* Get the proxy node info */
  info = wp_proxy_node_get_info(self->proxy);
  g_return_val_if_fail (info, FALSE);

  /* Create a variant array with all the ports */
  b_ports = g_variant_builder_new (G_VARIANT_TYPE ("at"));
  g_ptr_array_foreach(self->port_proxies, port_proxies_foreach_func,
      b_ports);
  v_ports = g_variant_builder_end (b_ports);

  /* Set the properties */
  g_variant_builder_init (&b, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&b, "{sv}", "node-id",
      g_variant_new_uint32 (info->id));
  g_variant_builder_add (&b, "{sv}", "ports", v_ports);
  *properties = g_variant_builder_end (&b);

  return TRUE;
}

GVariant *
wp_pw_audio_dsp_get_control_value (WpPwAudioDsp * self, guint32 control_id)
{
  switch (control_id) {
    case CONTROL_VOLUME:
      return g_variant_new_double (self->volume);
    case CONTROL_MUTE:
      return g_variant_new_boolean (self->mute);
    default:
      g_warning ("Unknown control id %u", control_id);
      return NULL;
  }
}

gboolean
wp_pw_audio_dsp_set_control_value (WpPwAudioDsp * self, guint32 control_id,
    GVariant * value)
{
  char buf[1024];
  struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof(buf));
  struct pw_node_proxy *pw_proxy = NULL;
  float volume;
  bool mute;

  /* Get the pipewire dsp proxy */
  g_return_val_if_fail (self->proxy, FALSE);
  pw_proxy = wp_proxy_get_pw_proxy (WP_PROXY(self->proxy));
  g_return_val_if_fail (pw_proxy, FALSE);

  switch (control_id) {
    case CONTROL_VOLUME:
      volume = g_variant_get_double (value);

      g_debug("WpPwAudioDsp:%p set volume control (%u) value, vol:%f", self,
          control_id, volume);

      pw_node_proxy_set_param (pw_proxy,
          SPA_PARAM_Props, 0,
          spa_pod_builder_add_object (&b,
              SPA_TYPE_OBJECT_Props, SPA_PARAM_Props,
              SPA_PROP_volume, SPA_POD_Float(volume),
              NULL));
      pw_node_proxy_enum_params (pw_proxy, 0, SPA_PARAM_Props, 0, -1,
          NULL);
      break;

    case CONTROL_MUTE:
      mute = g_variant_get_boolean (value);

      g_debug("WpPwAudioDsp:%p set mute control (%u) value, mute:%d", self,
          control_id, mute);

      pw_node_proxy_set_param (pw_proxy,
          SPA_PARAM_Props, 0,
          spa_pod_builder_add_object (&b,
              SPA_TYPE_OBJECT_Props, SPA_PARAM_Props,
              SPA_PROP_mute, SPA_POD_Bool(mute),
              NULL));
      pw_node_proxy_enum_params (pw_proxy, 0, SPA_PARAM_Props, 0, -1,
          NULL);
      break;

    default:
      g_warning ("Unknown control id %u", control_id);
      return FALSE;
  }

  return TRUE;
}
