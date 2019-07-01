/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * module-pw-audio-softdsp-endpoint provides a WpEndpoint implementation
 * that wraps an audio device node in pipewire and plugs a DSP node, as well
 * as optional merger+volume nodes that are used as entry points for the
 * various streams that this endpoint may have
 */

#include <wp/wp.h>
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/pod/builder.h>
#include <spa/param/props.h>

#define MIN_QUANTUM_SIZE  64
#define MAX_QUANTUM_SIZE  1024

struct _WpPwAudioSoftdspEndpoint
{
  WpEndpoint parent;

  /* The global-id this endpoint refers to */
  guint global_id;

  /* The task to signal the endpoint is initialized */
  GTask *init_task;

  /* The remote pipewire */
  WpRemotePipewire *remote_pipewire;

  /* Handler */
  gulong proxy_dsp_done_handler_id;

  /* temporary method to select which endpoint
   * is going to be the default input/output */
  gboolean selected;

  /* Direction */
  enum pw_direction direction;

  /* Proxies */
  WpProxyNode *proxy_node;
  WpProxyPort *proxy_port;
  WpProxyNode *proxy_dsp;
  GPtrArray *proxies_dsp_port;

  /* Volume */
  gfloat master_volume;
  gboolean master_mute;

  /* DSP */
  struct spa_hook dsp_listener;
  struct pw_proxy *link_proxy;
};

enum {
  PROP_0,
  PROP_GLOBAL_ID,
};

enum {
  CONTROL_VOLUME = 0,
  CONTROL_MUTE,
  CONTROL_SELECTED,
};

static GAsyncInitableIface *wp_endpoint_parent_interface = NULL;
static void wp_endpoint_async_initable_init (gpointer iface,
    gpointer iface_data);

G_DECLARE_FINAL_TYPE (WpPwAudioSoftdspEndpoint, endpoint,
    WP_PW, AUDIO_SOFTDSP_ENDPOINT, WpEndpoint)

G_DEFINE_TYPE_WITH_CODE (WpPwAudioSoftdspEndpoint, endpoint, WP_TYPE_ENDPOINT,
    G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE,
                           wp_endpoint_async_initable_init))

static void
proxies_dsp_port_foreach_func(gpointer data, gpointer user_data)
{
  GVariantBuilder *b = user_data;
  g_variant_builder_add (b, "t", data);
}

static gboolean
endpoint_prepare_link (WpEndpoint * ep, guint32 stream_id,
    WpEndpointLink * link, GVariant ** properties, GError ** error)
{
  WpPwAudioSoftdspEndpoint *self = WP_PW_AUDIO_SOFTDSP_ENDPOINT (ep);
  const struct pw_node_info *dsp_info = NULL;
  GVariantBuilder b, *b_ports;
  GVariant *v_ports;

  /* Get the dsp info */
  dsp_info = wp_proxy_node_get_info(self->proxy_dsp);
  g_return_val_if_fail (dsp_info, FALSE);

  /* Create a variant array with all the ports */
  b_ports = g_variant_builder_new (G_VARIANT_TYPE ("at"));
  g_ptr_array_foreach(self->proxies_dsp_port, proxies_dsp_port_foreach_func,
      b_ports);
  v_ports = g_variant_builder_end (b_ports);

  /* Set the properties */
  g_variant_builder_init (&b, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&b, "{sv}", "node-id",
      g_variant_new_uint32 (dsp_info->id));
  g_variant_builder_add (&b, "{sv}", "ports", v_ports);
  *properties = g_variant_builder_end (&b);

  return TRUE;
}

static void
on_dsp_running(WpPwAudioSoftdspEndpoint *self)
{
  struct pw_properties *props;
  const struct pw_node_info *node_info = NULL;
  const struct pw_node_info *dsp_info = NULL;

  /* Return if the node has already been linked */
  g_return_if_fail (!self->link_proxy);

  /* Get the node info */
  node_info = wp_proxy_node_get_info(self->proxy_node);
  g_return_if_fail (node_info);

  /* Get the dsp info */
  dsp_info = wp_proxy_node_get_info(self->proxy_dsp);
  g_return_if_fail (dsp_info);

  /* Create new properties */
  props = pw_properties_new(NULL, NULL);

  /* Set the new properties */
  pw_properties_set(props, PW_LINK_PROP_PASSIVE, "true");
  if (self->direction == PW_DIRECTION_OUTPUT) {
    pw_properties_setf(props, PW_LINK_OUTPUT_NODE_ID, "%d", dsp_info->id);
    pw_properties_setf(props, PW_LINK_OUTPUT_PORT_ID, "%d", -1);
    pw_properties_setf(props, PW_LINK_INPUT_NODE_ID, "%d", node_info->id);
    pw_properties_setf(props, PW_LINK_INPUT_PORT_ID, "%d", -1);
  } else {
    pw_properties_setf(props, PW_LINK_OUTPUT_NODE_ID, "%d", node_info->id);
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
on_dsp_idle (WpPwAudioSoftdspEndpoint *self)
{
  if (self->link_proxy != NULL) {
    g_debug ("%p unlinking DSP from node", self);
    pw_proxy_destroy (self->link_proxy);
    self->link_proxy = NULL;
  }
}

static void
dsp_node_event_info (void *data, const struct pw_node_info *info)
{
  WpPwAudioSoftdspEndpoint *self = data;

  /* Handle the different states */
  switch (info->state) {
  case PW_NODE_STATE_IDLE:
    on_dsp_idle (self);
    break;
  case PW_NODE_STATE_RUNNING:
    on_dsp_running (self);
    break;
  case PW_NODE_STATE_SUSPENDED:
    break;
  default:
    break;
  }
}

static void
dsp_node_event_param (void *object, int seq, uint32_t id,
    uint32_t index, uint32_t next, const struct spa_pod *param)
{
  WpPwAudioSoftdspEndpoint *self = WP_PW_AUDIO_SOFTDSP_ENDPOINT (object);

  switch (id) {
    case SPA_PARAM_Props:
    {
      struct spa_pod_prop *prop;
      struct spa_pod_object *obj = (struct spa_pod_object *) param;
      float volume = self->master_volume;
      bool mute = self->master_mute;

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

      g_debug ("WpEndpoint:%p param event, vol:(%lf -> %f) mute:(%d -> %d)",
          self, self->master_volume, volume, self->master_mute, mute);

      if (self->master_volume != volume) {
        self->master_volume = volume;
        wp_endpoint_notify_control_value (WP_ENDPOINT (self), CONTROL_VOLUME);
      }
      if (self->master_mute != mute) {
        self->master_mute = mute;
        wp_endpoint_notify_control_value (WP_ENDPOINT (self), CONTROL_MUTE);
      }

      break;
    }
    default:
      break;
  }
}

static const struct pw_node_proxy_events dsp_node_events = {
  PW_VERSION_NODE_PROXY_EVENTS,
  .info = dsp_node_event_info,
  .param = dsp_node_event_param,
};

static void
on_proxy_dsp_done(WpProxy *proxy, gpointer data)
{
  WpPwAudioSoftdspEndpoint *self = data;

  /* Don't do anything if the endpoint has already been initialized */
  if (!self->init_task)
    return;

  /* Finish the creation of the endpoint */
  g_task_return_boolean (self->init_task, TRUE);
  g_clear_object(&self->init_task);
}

static void
on_proxy_dsp_created(GObject *initable, GAsyncResult *res, gpointer data)
{
  WpPwAudioSoftdspEndpoint *self = data;
  struct pw_node_proxy *dsp_proxy = NULL;
  const struct spa_audio_info_raw *port_format;
  struct spa_audio_info_raw format;
  uint8_t buf[1024];
  struct spa_pod_builder pod_builder = { 0, };
  struct spa_pod *param;

  /* Get the proxy dsp */
  self->proxy_dsp = wp_proxy_node_new_finish(initable, res, NULL);
  g_return_if_fail (self->proxy_dsp);

  /* Add a custom dsp listener */
  dsp_proxy = wp_proxy_get_pw_proxy(WP_PROXY(self->proxy_dsp));
  g_return_if_fail (dsp_proxy);
  pw_node_proxy_add_listener(dsp_proxy, &self->dsp_listener,
      &dsp_node_events, self);

  /* Emit the props param */
  pw_node_proxy_enum_params (dsp_proxy, 0, SPA_PARAM_Props, 0, -1, NULL);

  /* Get the port format */
  port_format = wp_proxy_port_get_format(self->proxy_port);
  g_return_if_fail (port_format);
  format = *port_format;

  /* Build the param profile */
  spa_pod_builder_init(&pod_builder, buf, sizeof(buf));
  param = spa_format_audio_raw_build(&pod_builder, SPA_PARAM_Format, &format);
  param = spa_pod_builder_add_object(&pod_builder,
      SPA_TYPE_OBJECT_ParamProfile, SPA_PARAM_Profile,
      SPA_PARAM_PROFILE_direction,  SPA_POD_Id(pw_direction_reverse(self->direction)),
      SPA_PARAM_PROFILE_format,     SPA_POD_Pod(param));

  /* Set the param profile to emit the dsp ports */
  pw_node_proxy_set_param(dsp_proxy, SPA_PARAM_Profile, 0, param);
}

static void
emit_audio_dsp_node (WpPwAudioSoftdspEndpoint *self)
{
  struct pw_properties *props;
  const char *dsp_name = NULL;
  struct pw_node_proxy *dsp_proxy = NULL;
  const struct pw_node_info *node_info;

  /* Get the node info */
  node_info = wp_proxy_node_get_info(self->proxy_node);
  g_return_if_fail (node_info);

  /* Get the properties */
  props = pw_properties_new_dict(node_info->props);
  g_return_if_fail (props);

  /* Get the DSP name */
  dsp_name = pw_properties_get(props, "device.nick");
  if (!dsp_name)
    dsp_name = node_info->name;

  /* Set the properties */
  pw_properties_set(props, "audio-dsp.name", dsp_name);
  pw_properties_setf(props, "audio-dsp.direction", "%d", self->direction);
  pw_properties_setf(props, "audio-dsp.maxbuffer", "%ld",
      MAX_QUANTUM_SIZE * sizeof(float));

  /* Create the proxy dsp async */
  dsp_proxy = wp_remote_pipewire_create_object(self->remote_pipewire,
      "audio-dsp", PW_TYPE_INTERFACE_Node, &props->dict);
  wp_proxy_node_new(pw_proxy_get_id((struct pw_proxy *)dsp_proxy), dsp_proxy,
      on_proxy_dsp_created, self);

  /* Clean up */
  pw_properties_free(props);
}

static void
on_proxy_node_created(GObject *initable, GAsyncResult *res, gpointer data)
{
  WpPwAudioSoftdspEndpoint *self = data;
  GVariantDict d;
  g_autofree gchar *name = NULL;
  const struct spa_dict *props;

  /* Get the proxy node */
  self->proxy_node = wp_proxy_node_new_finish(initable, res, NULL);
  g_return_if_fail (self->proxy_node);

  /* Give a proper name to this endpoint based on ALSA properties */
  props = wp_proxy_node_get_info (self->proxy_node)->props;
  name = g_strdup_printf ("%s on %s (%s / node %d)",
      spa_dict_lookup (props, "alsa.pcm.name"),
      spa_dict_lookup (props, "alsa.card.name"),
      spa_dict_lookup (props, "alsa.device"),
      wp_proxy_node_get_info (self->proxy_node)->id);
  g_object_set (self, "name", name, NULL);

  /* Emit the audio DSP node */
  emit_audio_dsp_node(self);

  g_variant_dict_init (&d, NULL);
  g_variant_dict_insert (&d, "id", "u", 0);
  g_variant_dict_insert (&d, "name", "s", "default");
  wp_endpoint_register_stream (WP_ENDPOINT (self), g_variant_dict_end (&d));

  self->master_volume = 1.0;
  g_variant_dict_init (&d, NULL);
  g_variant_dict_insert (&d, "id", "u", CONTROL_VOLUME);
  g_variant_dict_insert (&d, "name", "s", "volume");
  g_variant_dict_insert (&d, "type", "s", "d");
  g_variant_dict_insert (&d, "range", "(dd)", 0.0, 1.0);
  g_variant_dict_insert (&d, "default-value", "d", self->master_volume);
  wp_endpoint_register_control (WP_ENDPOINT (self), g_variant_dict_end (&d));

  self->master_mute = FALSE;
  g_variant_dict_init (&d, NULL);
  g_variant_dict_insert (&d, "id", "u", CONTROL_MUTE);
  g_variant_dict_insert (&d, "name", "s", "mute");
  g_variant_dict_insert (&d, "type", "s", "b");
  g_variant_dict_insert (&d, "default-value", "b", self->master_mute);
  wp_endpoint_register_control (WP_ENDPOINT (self), g_variant_dict_end (&d));

  self->selected = FALSE;
  g_variant_dict_init (&d, NULL);
  g_variant_dict_insert (&d, "id", "u", CONTROL_SELECTED);
  g_variant_dict_insert (&d, "name", "s", "selected");
  g_variant_dict_insert (&d, "type", "s", "b");
  g_variant_dict_insert (&d, "default-value", "b", self->selected);
  wp_endpoint_register_control (WP_ENDPOINT (self), g_variant_dict_end (&d));
}

static void
on_proxy_port_created(GObject *initable, GAsyncResult *res, gpointer data)
{
  WpPwAudioSoftdspEndpoint *self = data;
  struct pw_node_proxy *node_proxy = NULL;

  /* Get the proxy port */
  self->proxy_port = wp_proxy_port_new_finish(initable, res, NULL);
  g_return_if_fail (self->proxy_port);

  /* Create the proxy node async */
  node_proxy = wp_remote_pipewire_proxy_bind (self->remote_pipewire,
      self->global_id, PW_TYPE_INTERFACE_Node);
  g_return_if_fail(node_proxy);
  wp_proxy_node_new(self->global_id, node_proxy, on_proxy_node_created, self);
}

static void
handle_node_port(WpPwAudioSoftdspEndpoint *self, guint id, guint parent_id,
  const struct spa_dict *props)
{
  struct pw_port_proxy *port_proxy = NULL;

  /* Alsa nodes should have 1 port only, so make sure proxy_port is not set */
  if (self->proxy_port != 0)
    return;

  /* Create the proxy port async */
  port_proxy = wp_remote_pipewire_proxy_bind (self->remote_pipewire, id,
    PW_TYPE_INTERFACE_Port);
  g_return_if_fail(port_proxy);
  wp_proxy_port_new(id, port_proxy, on_proxy_port_created, self);
}

static void
on_proxy_dsp_port_created(GObject *initable, GAsyncResult *res, gpointer data)
{
  WpPwAudioSoftdspEndpoint *self = data;
  WpProxyPort *proxy_dsp_port = NULL;

  /* Get the proxy dsp port */
  proxy_dsp_port = wp_proxy_port_new_finish(initable, res, NULL);
  g_return_if_fail (proxy_dsp_port);

  /* Add the proxy dsp port to the array */
  g_return_if_fail (self->proxies_dsp_port);
  g_ptr_array_add(self->proxies_dsp_port, proxy_dsp_port);

  /* Register a callback to know when all the dsp ports have been emitted */
  if (!self->proxy_dsp_done_handler_id) {
    self->proxy_dsp_done_handler_id = g_signal_connect_object(self->proxy_dsp,
        "done", (GCallback)on_proxy_dsp_done, self, 0);
    wp_proxy_sync (WP_PROXY(self->proxy_dsp));
  }
}

static void
handle_dsp_port(WpPwAudioSoftdspEndpoint *self, guint id, guint parent_id,
  const struct spa_dict *props)
{
  struct pw_port_proxy *port_proxy = NULL;

  /* Create the proxy dsp port async */
  port_proxy = wp_remote_pipewire_proxy_bind (self->remote_pipewire, id,
      PW_TYPE_INTERFACE_Port);
  g_return_if_fail(port_proxy);
  wp_proxy_port_new(id, port_proxy, on_proxy_dsp_port_created, self);
}

static void
on_port_added(WpRemotePipewire *rp, guint id, guint parent_id, gconstpointer p,
    gpointer d)
{
  WpPwAudioSoftdspEndpoint *self = d;
  const struct spa_dict *props = p;
  const struct pw_node_info *dsp_info = NULL;

  /* Check if it is a node port and handle it */
  if (self->global_id == parent_id) {
    handle_node_port(self, id, parent_id, props);
    return;
  }

  /* Otherwise, check if it is a dsp port and handle it */
  if (!self->proxy_dsp)
    return;
  dsp_info = wp_proxy_node_get_info (self->proxy_dsp);
  if (!dsp_info || dsp_info->id != parent_id)
    return;
  handle_dsp_port(self, id, parent_id, props);
}

static void
endpoint_finalize (GObject * object)
{
  WpPwAudioSoftdspEndpoint *self = WP_PW_AUDIO_SOFTDSP_ENDPOINT (object);

  /* Destroy the proxies port */
  if (self->proxies_dsp_port) {
    g_ptr_array_free(self->proxies_dsp_port, TRUE);
    self->proxies_dsp_port = NULL;
  }

  /* Destroy the proxy node */
  g_clear_object(&self->proxy_node);

  /* Destroy the proxy port */
  g_clear_object(&self->proxy_port);

  /* Destroy the proxy dsp */
  g_clear_object(&self->proxy_dsp);

  /* Destroy the done task */
  g_clear_object(&self->init_task);

  G_OBJECT_CLASS (endpoint_parent_class)->finalize (object);
}

static void
endpoint_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpPwAudioSoftdspEndpoint *self = WP_PW_AUDIO_SOFTDSP_ENDPOINT (object);

  switch (property_id) {
  case PROP_GLOBAL_ID:
    self->global_id = g_value_get_uint(value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
endpoint_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpPwAudioSoftdspEndpoint *self = WP_PW_AUDIO_SOFTDSP_ENDPOINT (object);

  switch (property_id) {
  case PROP_GLOBAL_ID:
    g_value_set_uint (value, self->global_id);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static GVariant *
endpoint_get_control_value (WpEndpoint * ep, guint32 control_id)
{
  WpPwAudioSoftdspEndpoint *self = WP_PW_AUDIO_SOFTDSP_ENDPOINT (ep);

  switch (control_id) {
    case CONTROL_VOLUME:
      return g_variant_new_double (self->master_volume);
    case CONTROL_MUTE:
      return g_variant_new_boolean (self->master_mute);
    case CONTROL_SELECTED:
      return g_variant_new_boolean (self->selected);
    default:
      g_warning ("Unknown control id %u", control_id);
      return NULL;
  }
}

static gboolean
endpoint_set_control_value (WpEndpoint * ep, guint32 control_id,
    GVariant * value)
{
  WpPwAudioSoftdspEndpoint *self = WP_PW_AUDIO_SOFTDSP_ENDPOINT (ep);
  char buf[1024];
  struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof(buf));
  struct pw_node_proxy *dsp_proxy = NULL;
  float volume;
  bool mute;

  /* Get the pipewire dsp proxy */
  g_return_val_if_fail (self->proxy_dsp, FALSE);
  dsp_proxy = wp_proxy_get_pw_proxy (WP_PROXY(self->proxy_dsp));
  g_return_val_if_fail (dsp_proxy, FALSE);

  switch (control_id) {
    case CONTROL_VOLUME:
      volume = g_variant_get_double (value);

      g_debug("WpEndpoint:%p set volume control (%u) value, vol:%f", self,
          control_id, volume);

      pw_node_proxy_set_param (dsp_proxy,
          SPA_PARAM_Props, 0,
          spa_pod_builder_add_object (&b,
              SPA_TYPE_OBJECT_Props, SPA_PARAM_Props,
              SPA_PROP_volume, SPA_POD_Float(volume),
              NULL));
      pw_node_proxy_enum_params (dsp_proxy, 0, SPA_PARAM_Props, 0, -1,
          NULL);
      break;

    case CONTROL_MUTE:
      mute = g_variant_get_boolean (value);

      g_debug("WpEndpoint:%p set mute control (%u) value, mute:%d", self,
          control_id, mute);

      pw_node_proxy_set_param (dsp_proxy,
          SPA_PARAM_Props, 0,
          spa_pod_builder_add_object (&b,
              SPA_TYPE_OBJECT_Props, SPA_PARAM_Props,
              SPA_PROP_mute, SPA_POD_Bool(mute),
              NULL));
      pw_node_proxy_enum_params (dsp_proxy, 0, SPA_PARAM_Props, 0, -1,
          NULL);
      break;

    case CONTROL_SELECTED:
      self->selected = g_variant_get_boolean (value);
      wp_endpoint_notify_control_value (ep, CONTROL_SELECTED);
      break;

    default:
      g_warning ("Unknown control id %u", control_id);
      return FALSE;
  }

  return TRUE;
}

static void
wp_endpoint_init_async (GAsyncInitable *initable, int io_priority,
    GCancellable *cancellable, GAsyncReadyCallback callback, gpointer data)
{
  WpPwAudioSoftdspEndpoint *self = WP_PW_AUDIO_SOFTDSP_ENDPOINT (initable);
  g_autoptr (WpCore) core = wp_endpoint_get_core(WP_ENDPOINT(self));
  const gchar *media_class = wp_endpoint_get_media_class (WP_ENDPOINT (self));

  /* Create the async task */
  self->init_task = g_task_new (initable, cancellable, callback, data);

  /* Init the proxies_dsp_port array */
  self->proxies_dsp_port = g_ptr_array_new_full(4, (GDestroyNotify)g_object_unref);

  /* Set the direction */
  if (g_str_has_suffix (media_class, "Source"))
    self->direction = PW_DIRECTION_INPUT;
  else if (g_str_has_suffix (media_class, "Sink"))
    self->direction = PW_DIRECTION_OUTPUT;
  else
    g_critical ("failed to parse direction");

  /* Register a port_added callback */
  self->remote_pipewire = wp_core_get_global (core, WP_GLOBAL_REMOTE_PIPEWIRE);
  g_return_if_fail(self->remote_pipewire);
  g_signal_connect_object(self->remote_pipewire, "global-added::port",
      (GCallback)on_port_added, self, 0);

  /* Call the parent interface */
  wp_endpoint_parent_interface->init_async (initable, io_priority, cancellable,
      callback, data);
}

static void
wp_endpoint_async_initable_init (gpointer iface, gpointer iface_data)
{
  GAsyncInitableIface *ai_iface = iface;

  /* Set the parent interface */
  wp_endpoint_parent_interface = g_type_interface_peek_parent (iface);

  /* Only set the init_async */
  ai_iface->init_async = wp_endpoint_init_async;
}

static void
endpoint_init (WpPwAudioSoftdspEndpoint * self)
{
}

static void
endpoint_class_init (WpPwAudioSoftdspEndpointClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpEndpointClass *endpoint_class = (WpEndpointClass *) klass;

  object_class->finalize = endpoint_finalize;
  object_class->set_property = endpoint_set_property;
  object_class->get_property = endpoint_get_property;

  endpoint_class->prepare_link = endpoint_prepare_link;
  endpoint_class->get_control_value = endpoint_get_control_value;
  endpoint_class->set_control_value = endpoint_set_control_value;

  /* Instal the properties */
  g_object_class_install_property (object_class, PROP_GLOBAL_ID,
      g_param_spec_uint ("global-id", "global-id",
          "The global Id this endpoint refers to", 0, G_MAXUINT, 0,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

void
endpoint_factory (WpFactory * factory, GType type, GVariant * properties,
  GAsyncReadyCallback ready, gpointer user_data)
{
  g_autoptr (WpCore) core = NULL;
  const gchar *media_class;
  guint global_id;

  /* Make sure the type is correct */
  g_return_if_fail(type == WP_TYPE_ENDPOINT);

  /* Get the Core */
  core = wp_factory_get_core(factory);
  g_return_if_fail (core);

  /* Get the properties */
  if (!g_variant_lookup (properties, "media-class", "&s", &media_class))
      return;
  if (!g_variant_lookup (properties, "global-id", "u", &global_id))
      return;

  /* Create and return the softdsp endpoint object */
  g_async_initable_new_async (
      endpoint_get_type (), G_PRIORITY_DEFAULT, NULL, ready, user_data,
      "core", core,
      "media-class", media_class,
      "global-id", global_id,
      NULL);
}

void
wireplumber__module_init (WpModule * module, WpCore * core, GVariant * args)
{
  /* Register the softdsp endpoint */
  wp_factory_new (core, "pw-audio-softdsp-endpoint", endpoint_factory);
}
