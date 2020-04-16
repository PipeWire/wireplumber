/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * module-pw-audio-softdsp-endpoint provides a WpBaseEndpoint implementation
 * that wraps an audio device node in pipewire and plugs a DSP node, as well
 * as optional merger+volume nodes that are used as entry points for the
 * various streams that this endpoint may have
 */

#include <wp/wp.h>
#include <pipewire/pipewire.h>
#include <pipewire/extensions/session-manager.h>
#include <spa/param/audio/format-utils.h>
#include <spa/pod/builder.h>
#include <spa/param/props.h>
#include <spa/utils/keys.h>

#include "audio-softdsp-endpoint/stream.h"
#include "audio-softdsp-endpoint/adapter.h"
#include "audio-softdsp-endpoint/convert.h"

#define MIN_QUANTUM_SIZE  64
#define MAX_QUANTUM_SIZE  1024
#define CONTROL_SELECTED 0

struct _WpPwAudioSoftdspEndpoint
{
  WpBaseEndpoint parent;

  /* Properties */
  WpNode *node;
  GVariant *streams;
  char *role;

  guint stream_count;
  gboolean selected;

  /* The task to signal the endpoint is initialized */
  GTask *init_task;

  /* Audio Streams */
  WpAudioStream *adapter;
  GPtrArray *converters;

  // WpImplEndpoint *impl_ep;
};

enum {
  PROP_0,
  PROP_PROXY_NODE,
  PROP_STREAMS,
  PROP_ROLE,
};

static GAsyncInitableIface *async_initable_parent_interface = NULL;
static void endpoint_async_initable_init (gpointer iface,
    gpointer iface_data);

G_DECLARE_FINAL_TYPE (WpPwAudioSoftdspEndpoint, endpoint,
    WP_PW, AUDIO_SOFTDSP_ENDPOINT, WpBaseEndpoint)

G_DEFINE_TYPE_WITH_CODE (WpPwAudioSoftdspEndpoint, endpoint, WP_TYPE_BASE_ENDPOINT,
    G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE,
                           endpoint_async_initable_init))

static WpProperties *
endpoint_get_properties (WpBaseEndpoint * ep)
{
  WpPwAudioSoftdspEndpoint *self = WP_PW_AUDIO_SOFTDSP_ENDPOINT (ep);

  return wp_proxy_get_properties (WP_PROXY (self->node));
}

static const char *
endpoint_get_role (WpBaseEndpoint *ep)
{
  WpPwAudioSoftdspEndpoint *self = WP_PW_AUDIO_SOFTDSP_ENDPOINT (ep);

  return self->role;
}

static guint32
endpoint_get_global_id (WpBaseEndpoint *ep)
{
  return SPA_ID_INVALID; //wp_proxy_get_bound_id (WP_PROXY (self->impl_ep));
}

static gboolean
endpoint_prepare_link (WpBaseEndpoint * ep, guint32 stream_id,
    WpBaseEndpointLink * link, GVariant ** properties, GError ** error)
{
  WpPwAudioSoftdspEndpoint *self = WP_PW_AUDIO_SOFTDSP_ENDPOINT (ep);
  WpAudioStream *stream = NULL;

  /* Link with the adapter if stream id is none */
  if (stream_id == WP_STREAM_ID_NONE)
    return wp_audio_stream_prepare_link (self->adapter, properties, error);

  /* Make sure the stream Id is valid */
  g_return_val_if_fail(stream_id < self->converters->len, FALSE);

  /* Make sure the stream is valid */
  stream = g_ptr_array_index (self->converters, stream_id);
  g_return_val_if_fail(stream, FALSE);

  /* Prepare the link */
  return wp_audio_stream_prepare_link (stream, properties, error);
}

static void
endpoint_begin_fade (WpBaseEndpoint * ep, guint32 stream_id, guint duration,
    gfloat step, guint direction, guint type, GCancellable * cancellable,
    GAsyncReadyCallback callback, gpointer data)
{
  WpPwAudioSoftdspEndpoint *self = WP_PW_AUDIO_SOFTDSP_ENDPOINT (ep);
  WpAudioStream *stream = NULL;

  /* Fade the adapter if stream id is none */
  if (stream_id == WP_STREAM_ID_NONE) {
    wp_audio_stream_begin_fade (self->adapter, duration, step, direction,
        type, cancellable, callback, data);
    return;
  }

  /* Make sure the stream Id is valid */
  g_return_if_fail(stream_id < self->converters->len);

  /* Make sure the stream is valid */
  stream = g_ptr_array_index (self->converters, stream_id);
  g_return_if_fail(stream);

  /* Begin fade */
  wp_audio_stream_begin_fade (stream, duration, step, direction, type,
      cancellable, callback, data);
}

#if 0
static void
on_exported_control_changed (WpEndpoint * ep, const gchar *id_name,
    WpPwAudioSoftdspEndpoint *self)
{
  g_autoptr (WpSpaPod) ctrl = NULL;

  if (g_strcmp0 (id_name, "volume") == 0) {
    gfloat vol;
    ctrl = wp_endpoint_get_control (ep, id_name);
    wp_spa_pod_get_float (ctrl, &vol);
    wp_audio_stream_set_volume (self->adapter, vol);
  } else if (g_strcmp0 (id_name, "mute") == 0) {
    gboolean m;
    ctrl = wp_endpoint_get_control (ep, id_name);
    wp_spa_pod_get_boolean (ctrl, &m);
    wp_audio_stream_set_mute (self->adapter, m);
  }
}

static void
on_adapter_control_changed (WpAudioStream * s, const gchar *id_name,
    WpPwAudioSoftdspEndpoint *self)
{
  /* block to avoid recursion - WpEndpoint emits the "control-changed"
     signal when we change the value here */
  g_signal_handlers_block_by_func (self->impl_ep,
      on_exported_control_changed, self);

  if (g_strcmp0 (id_name, "volume") == 0) {
    gfloat vol = wp_audio_stream_get_volume (s);
    g_autoptr (WpSpaPod) vol_ctrl = wp_spa_pod_new_float (vol);
    wp_endpoint_set_control (WP_ENDPOINT (self->impl_ep), id_name, vol_ctrl);
  } else if (g_strcmp0 (id_name, "mute") == 0) {
    gboolean m = wp_audio_stream_get_mute (s);
    g_autoptr (WpSpaPod) m_ctrl = wp_spa_pod_new_boolean (m);
    wp_endpoint_set_control (WP_ENDPOINT (self->impl_ep), id_name, m_ctrl);
  }

  g_signal_handlers_unblock_by_func (self->impl_ep,
      on_exported_control_changed, self);
}

static void
on_endpoint_exported (GObject * impl_ep, GAsyncResult *res, gpointer data)
{
  WpPwAudioSoftdspEndpoint *self = WP_PW_AUDIO_SOFTDSP_ENDPOINT (data);
  GError *error = NULL;

  g_return_if_fail (self->init_task);

  /* Get the object */
  wp_proxy_augment_finish (WP_PROXY (impl_ep), res, &error);
  if (error) {
    g_warning ("WpPwAudioSoftdspEndpoint:%p Aborting construction", self);
    g_task_return_error (self->init_task, error);
    g_clear_object (&self->init_task);
  } else {
    /* Finish the creation of the endpoint */
    g_task_return_boolean (self->init_task, TRUE);
    g_clear_object(&self->init_task);
  }
}

static void
do_export (WpPwAudioSoftdspEndpoint *self)
{
  g_autoptr (WpCore) core = wp_base_endpoint_get_core (WP_BASE_ENDPOINT (self));
  g_autoptr (WpProperties) props = NULL;
  g_autoptr (WpProperties) extra_props = NULL;
  g_autoptr (WpSpaPod) ctrl = NULL;

  g_return_if_fail (!self->impl_ep);

  self->impl_ep = wp_impl_endpoint_new (core);

  wp_impl_endpoint_register_control (self->impl_ep, "volume");
  wp_impl_endpoint_register_control (self->impl_ep, "mute");
  // wp_impl_endpoint_register_control (self->impl_ep, "channelVolumes");

  props = wp_proxy_get_properties (WP_PROXY (self->node));

  extra_props = wp_properties_new_empty ();
  wp_properties_setf (extra_props, PW_KEY_NODE_ID, "%d",
      wp_proxy_get_bound_id (WP_PROXY (self->node)));
  wp_properties_set (extra_props, PW_KEY_ENDPOINT_CLIENT_ID,
      wp_properties_get (props, PW_KEY_CLIENT_ID));
  wp_properties_setf (extra_props, "endpoint.priority", "%d",
      wp_base_endpoint_get_priority (WP_BASE_ENDPOINT (self)));

  wp_impl_endpoint_update_properties (self->impl_ep, props);
  wp_impl_endpoint_update_properties (self->impl_ep, extra_props);

  wp_impl_endpoint_set_name (self->impl_ep,
      wp_base_endpoint_get_name (WP_BASE_ENDPOINT (self)));
  wp_impl_endpoint_set_media_class (self->impl_ep,
      wp_base_endpoint_get_media_class (WP_BASE_ENDPOINT (self)));
  wp_impl_endpoint_set_direction (self->impl_ep,
      wp_base_endpoint_get_direction (WP_BASE_ENDPOINT (self)));

  ctrl = wp_spa_pod_new_float (wp_audio_stream_get_volume (self->adapter));
  wp_endpoint_set_control (WP_ENDPOINT (self->impl_ep), "volume", ctrl);
  ctrl = wp_spa_pod_new_boolean (wp_audio_stream_get_mute (self->adapter));
  wp_endpoint_set_control (WP_ENDPOINT (self->impl_ep), "mute", ctrl);

  g_signal_connect_object (self->impl_ep, "control-changed",
      (GCallback) on_exported_control_changed, self, 0);
  g_signal_connect_object (self->adapter, "control-changed",
      (GCallback) on_adapter_control_changed, self, 0);

  wp_proxy_augment (WP_PROXY (self->impl_ep), WP_PROXY_FEATURE_BOUND,
      NULL, on_endpoint_exported, self);
}
#endif

static void
on_audio_convert_created(GObject *initable, GAsyncResult *res, gpointer data)
{
  WpPwAudioSoftdspEndpoint *self = data;
  g_autoptr (GError) error = NULL;
  WpAudioStream *convert = NULL;
  guint stream_id = 0;
  g_autofree gchar *name = NULL;

  /* Get the audio convert */
  convert = wp_audio_stream_new_finish (initable, res, &error);
  if (error) {
    g_warning ("WpPwAudioSoftdspEndpoint:%p Could not create convert: %s\n",
        self, error->message);
    return;
  }
  g_return_if_fail (convert);

  /* Get the stream id */
  g_object_get (convert, "id", &stream_id, "name", &name, NULL);
  g_return_if_fail (stream_id >= 0);

  /* Set the streams */
  g_ptr_array_insert (self->converters, stream_id, convert);

  g_debug ("%s:%p Created audio convert %u %s", G_OBJECT_TYPE_NAME (self), self,
      stream_id, name);

  /* Finish the endpoint creation when all the streams are created */
  if (--self->stream_count == 0) {
    g_task_return_boolean (self->init_task, TRUE);
    g_clear_object(&self->init_task);
  }
}

static void
on_audio_adapter_created(GObject *initable, GAsyncResult *res,
    gpointer data)
{
  WpPwAudioSoftdspEndpoint *self = data;
  enum pw_direction direction = wp_base_endpoint_get_direction(WP_BASE_ENDPOINT(self));
  g_autoptr (WpCore) core = wp_base_endpoint_get_core(WP_BASE_ENDPOINT(self));
  g_autoptr (WpProperties) props = NULL;
  g_autoptr (GError) error = NULL;
  const struct spa_audio_info_raw *format;
  g_autofree gchar *name = NULL;
  GVariantDict d;
  GVariantIter iter;
  const gchar *stream;
  guint priority;
  int i;

  /* Get the proxy adapter */
  self->adapter = wp_audio_stream_new_finish (initable, res, &error);
  if (error) {
    g_warning ("WpPwAudioSoftdspEndpoint:%p Could not create adapter: %s\n",
        self, error->message);
    return;
  }
  g_return_if_fail (self->adapter);

  props = wp_proxy_get_properties (WP_PROXY (self->node));

  /* Set the role */
  self->role = g_strdup (wp_properties_get (props, PW_KEY_MEDIA_ROLE));

  /* Just finish if no streams need to be created */
  if (!self->streams) {
    g_task_return_boolean (self->init_task, TRUE);
    g_clear_object(&self->init_task);
    return;
  }

  /* Get the adapter format */
  format = wp_audio_adapter_get_format (WP_AUDIO_ADAPTER (self->adapter));
  g_return_if_fail (format);

  /* Create the audio converters */
  g_variant_iter_init (&iter, self->streams);
  for (i = 0; g_variant_iter_next (&iter, "(&su)", &stream, &priority); i++) {
    wp_audio_convert_new (WP_BASE_ENDPOINT(self), i, stream, direction,
        self->adapter, format, on_audio_convert_created, self);

    /* Register the stream */
    g_variant_dict_init (&d, NULL);
    g_variant_dict_insert (&d, "id", "u", i);
    g_variant_dict_insert (&d, "name", "s", stream);
    g_variant_dict_insert (&d, "priority", "u", priority);
    wp_base_endpoint_register_stream (WP_BASE_ENDPOINT (self),
        g_variant_dict_end (&d));
  }
  self->stream_count = i;
}

static void
endpoint_finalize (GObject * object)
{
  WpPwAudioSoftdspEndpoint *self = WP_PW_AUDIO_SOFTDSP_ENDPOINT (object);

  // g_clear_object (&self->impl_ep);

  g_clear_pointer(&self->streams, g_variant_unref);

  /* Destroy the proxy adapter */
  g_clear_object(&self->adapter);

  /* Destroy all the converters */
  g_clear_pointer (&self->converters, g_ptr_array_unref);

  /* Destroy the done task */
  g_clear_object(&self->init_task);

  g_clear_object(&self->node);
  g_free (self->role);

  G_OBJECT_CLASS (endpoint_parent_class)->finalize (object);
}

static void
endpoint_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpPwAudioSoftdspEndpoint *self = WP_PW_AUDIO_SOFTDSP_ENDPOINT (object);

  switch (property_id) {
  case PROP_PROXY_NODE:
    self->node = g_value_dup_object (value);
    break;
  case PROP_STREAMS:
    self->streams = g_value_dup_variant(value);
    break;
  case PROP_ROLE:
    g_free (self->role);
    self->role = g_value_dup_string (value);
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
  case PROP_PROXY_NODE:
    g_value_set_object (value, self->node);
    break;
  case PROP_STREAMS:
    g_value_set_variant (value, self->streams);
    break;
  case PROP_ROLE:
    g_value_set_string (value, self->role);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
endpoint_init_async (GAsyncInitable *initable, int io_priority,
    GCancellable *cancellable, GAsyncReadyCallback callback, gpointer data)
{
  WpPwAudioSoftdspEndpoint *self = WP_PW_AUDIO_SOFTDSP_ENDPOINT (initable);
  enum pw_direction direction = wp_base_endpoint_get_direction(WP_BASE_ENDPOINT(self));
  g_autoptr (WpCore) core = wp_base_endpoint_get_core(WP_BASE_ENDPOINT(self));
  GVariantDict d;

  /* Create the async task */
  self->init_task = g_task_new (initable, cancellable, callback, data);

  /* Create the adapter proxy */
  wp_audio_adapter_new (WP_BASE_ENDPOINT(self), WP_STREAM_ID_NONE, "master",
      direction, self->node, FALSE, on_audio_adapter_created, self);

  /* Register the selected control */
  self->selected = FALSE;
  g_variant_dict_init (&d, NULL);
  g_variant_dict_insert (&d, "id", "u", CONTROL_SELECTED);
  g_variant_dict_insert (&d, "name", "s", "selected");
  g_variant_dict_insert (&d, "type", "s", "b");
  g_variant_dict_insert (&d, "default-value", "b", self->selected);
  wp_base_endpoint_register_control (WP_BASE_ENDPOINT (self), g_variant_dict_end (&d));

  /* Call the parent interface */
  async_initable_parent_interface->init_async (initable, io_priority, cancellable,
      callback, data);
}

static void
endpoint_async_initable_init (gpointer iface, gpointer iface_data)
{
  GAsyncInitableIface *ai_iface = iface;

  /* Set the parent interface */
  async_initable_parent_interface = g_type_interface_peek_parent (iface);

  /* Only set the init_async */
  ai_iface->init_async = endpoint_init_async;
}

static void
endpoint_init (WpPwAudioSoftdspEndpoint * self)
{
  self->converters = g_ptr_array_new_with_free_func (g_object_unref);
}

static void
endpoint_class_init (WpPwAudioSoftdspEndpointClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpBaseEndpointClass *endpoint_class = (WpBaseEndpointClass *) klass;

  object_class->finalize = endpoint_finalize;
  object_class->set_property = endpoint_set_property;
  object_class->get_property = endpoint_get_property;

  endpoint_class->get_properties = endpoint_get_properties;
  endpoint_class->get_role = endpoint_get_role;
  endpoint_class->get_global_id = endpoint_get_global_id;
  endpoint_class->prepare_link = endpoint_prepare_link;
  endpoint_class->begin_fade = endpoint_begin_fade;

  /* Instal the properties */
  g_object_class_install_property (object_class, PROP_PROXY_NODE,
      g_param_spec_object ("node", "node",
          "The node this endpoint refers to", WP_TYPE_NODE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_STREAMS,
      g_param_spec_variant ("streams", "streams",
          "The stream names for the streams to create",
          G_VARIANT_TYPE ("a(su)"), NULL,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_ROLE,
      g_param_spec_string ("role", "role", "The role of the wrapped node", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

void
audio_softdsp_endpoint_factory (WpFactory * factory, GType type, GVariant * properties,
  GAsyncReadyCallback ready, gpointer user_data)
{
  g_autoptr (WpCore) core = NULL;
  const gchar *name, *media_class;
  guint direction, priority;
  guint64 node;
  g_autoptr (GVariant) streams = NULL;

  /* Make sure the type is correct */
  g_return_if_fail(type == WP_TYPE_BASE_ENDPOINT);

  /* Get the Core */
  core = wp_factory_get_core(factory);
  g_return_if_fail (core);

  /* Get the properties */
  if (!g_variant_lookup (properties, "name", "&s", &name))
      return;
  if (!g_variant_lookup (properties, "media-class", "&s", &media_class))
      return;
  if (!g_variant_lookup (properties, "direction", "u", &direction))
      return;
  if (!g_variant_lookup (properties, "priority", "u", &priority))
      return;
  if (!g_variant_lookup (properties, "node", "t", &node))
      return;
  streams = g_variant_lookup_value (properties, "streams",
      G_VARIANT_TYPE ("a(su)"));

  /* Create and return the softdsp endpoint object */
  g_async_initable_new_async (
      endpoint_get_type (), G_PRIORITY_DEFAULT, NULL, ready, user_data,
      "core", core,
      "name", name,
      "media-class", media_class,
      "direction", direction,
      "priority", priority,
      "node", (gpointer) node,
      "streams", streams,
      NULL);
}
