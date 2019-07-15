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

#include "module-pw-audio-softdsp-endpoint/dsp.h"

#define MIN_QUANTUM_SIZE  64
#define MAX_QUANTUM_SIZE  1024
#define CONTROL_SELECTED 0

struct _WpPwAudioSoftdspEndpoint
{
  WpEndpoint parent;

  /* Properties */
  guint global_id;
  GVariant *streams;

  guint stream_count;
  gboolean selected;

  /* The task to signal the endpoint is initialized */
  GTask *init_task;
  gboolean init_abort;

  /* The remote pipewire */
  WpRemotePipewire *remote_pipewire;

  /* Direction */
  enum pw_direction direction;

  /* Proxies */
  WpProxyNode *proxy_node;
  WpProxyPort *proxy_port;

  /* Audio Dsp */
  WpPwAudioDsp *converter;
  GPtrArray *dsps;
};

enum {
  PROP_0,
  PROP_GLOBAL_ID,
  PROP_STREAMS,
};

static GAsyncInitableIface *wp_endpoint_parent_interface = NULL;
static void wp_endpoint_async_initable_init (gpointer iface,
    gpointer iface_data);

G_DECLARE_FINAL_TYPE (WpPwAudioSoftdspEndpoint, endpoint,
    WP_PW, AUDIO_SOFTDSP_ENDPOINT, WpEndpoint)

G_DEFINE_TYPE_WITH_CODE (WpPwAudioSoftdspEndpoint, endpoint, WP_TYPE_ENDPOINT,
    G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE,
                           wp_endpoint_async_initable_init))

typedef GObject* (*WpObjectNewFinishFunc)(GObject *initable, GAsyncResult *res,
    GError **error);

static GObject *
object_safe_new_finish(WpPwAudioSoftdspEndpoint * self, GObject *initable,
    GAsyncResult *res, WpObjectNewFinishFunc new_finish_func)
{
  GObject *object = NULL;
  GError *error = NULL;

  /* Return NULL if we are already aborting */
  if (self->init_abort)
    return NULL;

  /* Get the object */
  object = G_OBJECT (new_finish_func (initable, res, &error));
  g_return_val_if_fail (object, NULL);

  /* Check for error */
  if (error) {
    g_clear_object (&object);
    g_warning ("WpPwAudioSoftdspEndpoint:%p Aborting construction", self);
    self->init_abort = TRUE;
    g_task_return_error (self->init_task, error);
    g_clear_object (&self->init_task);
    return NULL;
  }

  return object;
}

static gboolean
endpoint_prepare_link (WpEndpoint * ep, guint32 stream_id,
    WpEndpointLink * link, GVariant ** properties, GError ** error)
{
  WpPwAudioSoftdspEndpoint *self = WP_PW_AUDIO_SOFTDSP_ENDPOINT (ep);
  WpPwAudioDsp *stream = NULL;

  /* Make sure the stream Id is valid */
  g_return_val_if_fail(stream_id < self->dsps->len, FALSE);

  /* Make sure the stream is valid */
  stream = g_ptr_array_index (self->dsps, stream_id);
  g_return_val_if_fail(stream, FALSE);

  /* Prepare the link */
  return wp_pw_audio_dsp_prepare_link (stream, properties, error);
}

static void
finish_endpoint_creation(WpPwAudioSoftdspEndpoint *self)
{
  /* Don't do anything if the endpoint has already been initialized */
  if (!self->init_task)
    return;

  /* Finish the creation of the endpoint */
  g_task_return_boolean (self->init_task, TRUE);
  g_clear_object(&self->init_task);
}

static void
on_audio_dsp_stream_created(GObject *initable, GAsyncResult *res, gpointer data)
{
  WpPwAudioSoftdspEndpoint *self = data;
  WpPwAudioDsp *dsp = NULL;
  guint stream_id = 0;
  g_autofree gchar *name = NULL;

  /* Get the stream */
  dsp = WP_PW_AUDIO_DSP (object_safe_new_finish (self, initable, res,
      (WpObjectNewFinishFunc)wp_pw_audio_dsp_new_finish));
  if (!dsp)
    return;

  /* Get the stream id */
  g_object_get (dsp, "id", &stream_id, "name", &name, NULL);
  g_return_if_fail (stream_id >= 0);

  /* Set the streams */
  g_ptr_array_insert (self->dsps, stream_id, dsp);

  g_debug ("%s:%p Created stream %u %s", G_OBJECT_TYPE_NAME (self), self,
      stream_id, name);

  /* Finish the endpoint creation when all the streams are created */
  if (--self->stream_count == 0)
    finish_endpoint_creation(self);
}

static void
on_audio_dsp_converter_created(GObject *initable, GAsyncResult *res,
    gpointer data)
{
  WpPwAudioSoftdspEndpoint *self = data;
  g_autoptr (WpCore) core = wp_endpoint_get_core(WP_ENDPOINT(self));
  const struct pw_node_info *target = NULL;
  GVariantDict d;
  GVariantIter iter;
  const gchar *stream;
  int i;

  /* Get the proxy dsp converter */
  self->converter = WP_PW_AUDIO_DSP (object_safe_new_finish (self, initable,
      res, (WpObjectNewFinishFunc)wp_pw_audio_dsp_new_finish));
  if (!self->converter)
    return;

  /* Get the target and format */
  target = wp_pw_audio_dsp_get_info (self->converter);
  g_return_if_fail (target);

  /* Create the audio dsp streams */
  g_variant_iter_init (&iter, self->streams);
  for (i = 0; g_variant_iter_next (&iter, "&s", &stream); i++) {
    wp_pw_audio_dsp_new (WP_ENDPOINT(self), i, stream, self->direction, FALSE,
        target, on_audio_dsp_stream_created, self);

    /* Register the stream */
    g_variant_dict_init (&d, NULL);
    g_variant_dict_insert (&d, "id", "u", i);
    g_variant_dict_insert (&d, "name", "s", stream);
    wp_endpoint_register_stream (WP_ENDPOINT (self), g_variant_dict_end (&d));
  }
  self->stream_count = i;
}

static void
on_proxy_node_created(GObject *initable, GAsyncResult *res, gpointer data)
{
  WpPwAudioSoftdspEndpoint *self = data;
  g_autoptr (WpCore) core = wp_endpoint_get_core(WP_ENDPOINT(self));
  g_autofree gchar *name = NULL;
  const struct spa_dict *props;
  const struct pw_node_info *target = NULL;

  /* Get the proxy node */
  self->proxy_node = WP_PROXY_NODE (object_safe_new_finish (self, initable,
      res, (WpObjectNewFinishFunc)wp_proxy_node_new_finish));
  if (!self->proxy_node)
    return;

  /* Give a proper name to this endpoint based on ALSA properties */
  props = wp_proxy_node_get_info (self->proxy_node)->props;
  if (0 == g_strcmp0(spa_dict_lookup (props, "device.api"), "alsa")) {
    name = g_strdup_printf ("%s on %s (%s / node %d)",
        spa_dict_lookup (props, "alsa.pcm.name"),
        spa_dict_lookup (props, "alsa.card.name"),
        spa_dict_lookup (props, "alsa.device"),
        wp_proxy_node_get_info (self->proxy_node)->id);
    g_object_set (self, "name", name, NULL);
  }

  /* Create the converter proxy */
  target = wp_proxy_node_get_info (self->proxy_node);
  g_return_if_fail (target);
  wp_pw_audio_dsp_new (WP_ENDPOINT(self), WP_STREAM_ID_NONE, "master",
      self->direction, TRUE, target, on_audio_dsp_converter_created, self);
}

static void
on_proxy_port_created(GObject *initable, GAsyncResult *res, gpointer data)
{
  WpPwAudioSoftdspEndpoint *self = data;
  struct pw_node_proxy *node_proxy = NULL;

  /* Get the proxy port */
  self->proxy_port = WP_PROXY_PORT (object_safe_new_finish (self, initable, res,
      (WpObjectNewFinishFunc)wp_proxy_port_new_finish));
  if (!self->proxy_port)
    return;

  /* Create the proxy node async */
  node_proxy = wp_remote_pipewire_proxy_bind (self->remote_pipewire,
      self->global_id, PW_TYPE_INTERFACE_Node);
  g_return_if_fail(node_proxy);
  wp_proxy_node_new(self->global_id, node_proxy, on_proxy_node_created, self);
}

static void
on_port_added(WpRemotePipewire *rp, guint id, guint parent_id, gconstpointer p,
    gpointer d)
{
  WpPwAudioSoftdspEndpoint *self = d;
  struct pw_port_proxy *port_proxy = NULL;

  /* Don't do anything if we are aborting */
  if (self->init_abort)
    return;

  /* Check if it is a node port and handle it */
  if (self->global_id != parent_id)
    return;

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
endpoint_finalize (GObject * object)
{
  WpPwAudioSoftdspEndpoint *self = WP_PW_AUDIO_SOFTDSP_ENDPOINT (object);

  g_clear_pointer(&self->streams, g_variant_unref);

  /* Destroy the proxy node */
  g_clear_object(&self->proxy_node);

  /* Destroy the proxy port */
  g_clear_object(&self->proxy_port);

  /* Destroy the proxy dsp converter */
  g_clear_object(&self->converter);

  /* Destroy all the proxy dsp streams */
  g_clear_pointer (&self->dsps, g_ptr_array_unref);

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
  case PROP_STREAMS:
    self->streams = g_value_dup_variant(value);
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
  case PROP_STREAMS:
    g_value_set_variant (value, self->streams);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static GVariant *
endpoint_get_control_value (WpEndpoint * ep, guint32 id)
{
  WpPwAudioSoftdspEndpoint *self = WP_PW_AUDIO_SOFTDSP_ENDPOINT (ep);
  guint stream_id, control_id;
  WpPwAudioDsp *stream = NULL;

  if (id == CONTROL_SELECTED)
    return g_variant_new_boolean (self->selected);

  wp_pw_audio_dsp_id_decode (id, &stream_id, &control_id);

  /* Check if it is the master stream */
  if (stream_id == WP_STREAM_ID_NONE)
    return wp_pw_audio_dsp_get_control_value (self->converter, control_id);

  /* Otherwise get the stream_id and control_id */
  g_return_val_if_fail (stream_id < self->dsps->len, NULL);
  stream = g_ptr_array_index (self->dsps, stream_id);
  g_return_val_if_fail (stream, NULL);
  return wp_pw_audio_dsp_get_control_value (stream, control_id);
}

static gboolean
endpoint_set_control_value (WpEndpoint * ep, guint32 id, GVariant * value)
{
  WpPwAudioSoftdspEndpoint *self = WP_PW_AUDIO_SOFTDSP_ENDPOINT (ep);
  guint stream_id, control_id;
  WpPwAudioDsp *stream = NULL;

  if (id == CONTROL_SELECTED) {
    self->selected = g_variant_get_boolean (value);
    wp_endpoint_notify_control_value (ep, CONTROL_SELECTED);
    return TRUE;
  }

  wp_pw_audio_dsp_id_decode (id, &stream_id, &control_id);

  /* Check if it is the master stream */
  if (stream_id == WP_STREAM_ID_NONE)
    return wp_pw_audio_dsp_set_control_value (self->converter, control_id, value);

  /* Otherwise get the stream_id and control_id */
  g_return_val_if_fail (stream_id < self->dsps->len, FALSE);
  stream = g_ptr_array_index (self->dsps, stream_id);
  g_return_val_if_fail (stream, FALSE);
  return wp_pw_audio_dsp_set_control_value (stream, control_id, value);
}

static void
wp_endpoint_init_async (GAsyncInitable *initable, int io_priority,
    GCancellable *cancellable, GAsyncReadyCallback callback, gpointer data)
{
  WpPwAudioSoftdspEndpoint *self = WP_PW_AUDIO_SOFTDSP_ENDPOINT (initable);
  g_autoptr (WpCore) core = wp_endpoint_get_core(WP_ENDPOINT(self));
  const gchar *media_class = wp_endpoint_get_media_class (WP_ENDPOINT (self));
  GVariantDict d;

  /* Create the async task */
  self->init_task = g_task_new (initable, cancellable, callback, data);

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

  /* Register the selected control */
  self->selected = FALSE;
  g_variant_dict_init (&d, NULL);
  g_variant_dict_insert (&d, "id", "u", CONTROL_SELECTED);
  g_variant_dict_insert (&d, "name", "s", "selected");
  g_variant_dict_insert (&d, "type", "s", "b");
  g_variant_dict_insert (&d, "default-value", "b", self->selected);
  wp_endpoint_register_control (WP_ENDPOINT (self), g_variant_dict_end (&d));

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
  self->init_abort = FALSE;
  self->dsps = g_ptr_array_new_with_free_func (g_object_unref);
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

  g_object_class_install_property (object_class, PROP_STREAMS,
      g_param_spec_variant ("streams", "streams",
          "The stream names for the streams to create",
          G_VARIANT_TYPE ("as"), NULL,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

void
endpoint_factory (WpFactory * factory, GType type, GVariant * properties,
  GAsyncReadyCallback ready, gpointer user_data)
{
  g_autoptr (WpCore) core = NULL;
  const gchar *name, *media_class;
  guint global_id;
  g_autoptr (GVariant) streams = NULL;

  /* Make sure the type is correct */
  g_return_if_fail(type == WP_TYPE_ENDPOINT);

  /* Get the Core */
  core = wp_factory_get_core(factory);
  g_return_if_fail (core);

  /* Get the properties */
  if (!g_variant_lookup (properties, "name", "&s", &name))
      return;
  if (!g_variant_lookup (properties, "media-class", "&s", &media_class))
      return;
  if (!g_variant_lookup (properties, "global-id", "u", &global_id))
      return;
  if (!(streams = g_variant_lookup_value (properties, "streams",
          G_VARIANT_TYPE ("as"))))
      return;

  /* Create and return the softdsp endpoint object */
  g_async_initable_new_async (
      endpoint_get_type (), G_PRIORITY_DEFAULT, NULL, ready, user_data,
      "core", core,
      "name", name,
      "media-class", media_class,
      "global-id", global_id,
      "streams", streams,
      NULL);
}

void
wireplumber__module_init (WpModule * module, WpCore * core, GVariant * args)
{
  /* Register the softdsp endpoint */
  wp_factory_new (core, "pw-audio-softdsp-endpoint", endpoint_factory);
}
