/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * SECTION: Endpoint
 *
 * An endpoint is an abstraction layer that represents a physical place where
 * audio can be routed to/from.
 *
 * Examples of endpoints on a desktop-like system:
 * * Laptop speakers
 * * Laptop webcam
 * * USB microphone
 * * Docking station stereo jack port
 * * USB 5.1 Digital audio output
 *
 * Examples of endpoints on a car:
 * * Driver seat speakers
 * * Front right seat microphone array
 * * Rear left seat headphones
 * * Bluetooth phone gateway
 * * All speakers
 *
 * In ALSA terms, an endpoint may be representing an ALSA subdevice 1-to-1
 * (therefore a single alsa-source/alsa-sink node in pipewire),
 * but it may as well be representing a part of this subdevice (for instance,
 * only the front stereo channels, or only the rear stereo), or it may represent
 * a combination of devices (for instance, playing to all speakers of a system
 * while they are plugged on different sound cards).
 *
 * An endpoint is not necessarily tied to a device that is present on this
 * system using ALSA or V4L. It may also represent a hardware device that
 * can be accessed in some hardware-specific path and is not accessible to
 * applications through pipewire. In this case, the endpoint can only used
 * for controlling the hardware, or - if the appropriate EndpointLink object
 * is also implemented - it can be used to route media from some other
 * hardware endpoint.
 *
 * ## Streams
 *
 * An endpoint can contain multiple streams, which represent different,
 * controllable paths that can be used to reach this endpoint.
 * Streams can be used to implement grouping of applications based on their
 * role or other things.
 *
 * Examples of streams on an audio output endpoint: "multimedia", "radio",
 * "phone". In this example, an audio player would be routed through the
 * "multimedia" stream, for instance, while a voip app would be routed through
 * "phone". This would allow lowering the volume of the audio player while the
 * call is in progress by using the standard volume control of the "multimedia"
 * stream.
 *
 * Examples of streams on an audio capture endpoint: "standard",
 * "voice recognition". In this example, the "standard" capture gives a
 * real-time capture from the microphone, while "voice recognition" gives a
 * slightly delayed and DSP-optimized for speech input, which can be used
 * as input in a voice recognition engine.
 *
 * A stream is described as a dictionary GVariant (a{sv}) with the following
 * standard keys available:
 * "id": the id of the stream
 * "name": the name of the stream
 *
 * ## Controls
 *
 * An endpoint can have multiple controls, which can control anything in the
 * path of media. Typically, audio streams have volume and mute controls, while
 * video streams have hue, brightness, contrast, etc... Controls can be linked
 * to a specific stream, but may as well be global and apply to all streams
 * of the endpoint. This can be used to implement a master volume, for instance.
 *
 * A control is described as a dictionary GVariant (a{sv}) with the following
 * standard keys available:
 * "id": the id of the control
 * "stream-id": the id of the stream that this control applies to
 * "name": the name of the control
 * "type": a GVariant type string
 * "range": a tuple (min, max)
 * "default-value": the default value
 */

#include "base-endpoint.h"
#include "error.h"
#include "factory.h"
#include "private.h"

typedef struct _WpBaseEndpointPrivate WpBaseEndpointPrivate;
struct _WpBaseEndpointPrivate
{
  gchar *name;
  gchar media_class[40];
  guint direction;
  guint64 creation_time;
  guint priority;
  GPtrArray *streams;
  GPtrArray *controls;
  GPtrArray *links;
  GWeakRef core;
};

enum {
  PROP_0,
  PROP_CORE,
  PROP_NAME,
  PROP_MEDIA_CLASS,
  PROP_DIRECTION,
  PROP_CREATION_TIME,
  PROP_PRIORITY,
};

enum {
  SIGNAL_NOTIFY_CONTROL_VALUE,
  NUM_SIGNALS
};

static guint32 signals[NUM_SIGNALS];

static void wp_base_endpoint_async_initable_init (gpointer iface,
    gpointer iface_data);

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (WpBaseEndpoint, wp_base_endpoint, G_TYPE_OBJECT,
  G_ADD_PRIVATE (WpBaseEndpoint)
  G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE,
                         wp_base_endpoint_async_initable_init))

static void
wp_base_endpoint_init_async (GAsyncInitable *initable, int io_priority,
    GCancellable *cancellable, GAsyncReadyCallback callback, gpointer data)
{
}

static gboolean
wp_base_endpoint_init_finish (GAsyncInitable *initable, GAsyncResult *result,
    GError **error)
{
  g_return_val_if_fail (g_task_is_valid (result, initable), FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

static void
wp_base_endpoint_async_initable_init (gpointer iface, gpointer iface_data)
{
  GAsyncInitableIface *ai_iface = iface;

  ai_iface->init_async = wp_base_endpoint_init_async;
  ai_iface->init_finish = wp_base_endpoint_init_finish;
}

static void
wp_base_endpoint_init (WpBaseEndpoint * self)
{
  WpBaseEndpointPrivate *priv = wp_base_endpoint_get_instance_private (self);

  g_weak_ref_init (&priv->core, NULL);

  priv->creation_time = (guint64) g_get_monotonic_time ();
  priv->streams =
      g_ptr_array_new_with_free_func ((GDestroyNotify) g_variant_unref);
  priv->controls =
      g_ptr_array_new_with_free_func ((GDestroyNotify) g_variant_unref);
  priv->links = g_ptr_array_new_with_free_func (g_object_unref);
}

static void
wp_base_endpoint_dispose (GObject * object)
{
  WpBaseEndpointPrivate *priv =
      wp_base_endpoint_get_instance_private (WP_BASE_ENDPOINT (object));
  gint i;

  /* wp_base_endpoint_link_destroy removes elements from the array,
   * so traversing in reverse order is faster and less complicated */
  for (i = priv->links->len - 1; i >= 0; i--) {
    wp_base_endpoint_link_destroy (g_ptr_array_index (priv->links, i));
  }

  G_OBJECT_CLASS (wp_base_endpoint_parent_class)->dispose (object);
}

static void
wp_base_endpoint_finalize (GObject * object)
{
  WpBaseEndpointPrivate *priv =
      wp_base_endpoint_get_instance_private (WP_BASE_ENDPOINT (object));

  g_debug ("%s:%p destroyed: %s", G_OBJECT_TYPE_NAME (object), object,
      priv->name);

  g_ptr_array_unref (priv->streams);
  g_ptr_array_unref (priv->controls);
  g_ptr_array_unref (priv->links);
  g_free (priv->name);
  g_weak_ref_clear (&priv->core);

  G_OBJECT_CLASS (wp_base_endpoint_parent_class)->finalize (object);
}

static void
wp_base_endpoint_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpBaseEndpointPrivate *priv =
      wp_base_endpoint_get_instance_private (WP_BASE_ENDPOINT (object));

  switch (property_id) {
  case PROP_CORE:
    g_weak_ref_set (&priv->core, g_value_get_object (value));
    break;
  case PROP_NAME:
    priv->name = g_value_dup_string (value);
    break;
  case PROP_MEDIA_CLASS:
    strncpy (priv->media_class, g_value_get_string (value),
        sizeof (priv->media_class) - 1);
    break;
  case PROP_DIRECTION:
    priv->direction = g_value_get_uint(value);
    break;
  case PROP_CREATION_TIME:
    priv->creation_time = g_value_get_uint64(value);
    break;
  case PROP_PRIORITY:
    priv->priority = g_value_get_uint(value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_base_endpoint_get_property (GObject * object, guint property_id, GValue * value,
    GParamSpec * pspec)
{
  WpBaseEndpointPrivate *priv =
      wp_base_endpoint_get_instance_private (WP_BASE_ENDPOINT (object));

  switch (property_id) {
  case PROP_CORE:
    g_value_take_object (value, g_weak_ref_get (&priv->core));
    break;
  case PROP_NAME:
    g_value_set_string (value, priv->name);
    break;
  case PROP_MEDIA_CLASS:
    g_value_set_string (value, priv->media_class);
    break;
  case PROP_DIRECTION:
    g_value_set_uint (value, priv->direction);
    break;
  case PROP_CREATION_TIME:
    g_value_set_uint64 (value, priv->creation_time);
    break;
  case PROP_PRIORITY:
    g_value_set_uint (value, priv->priority);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_base_endpoint_class_init (WpBaseEndpointClass * klass)
{
  GObjectClass * object_class = (GObjectClass *) klass;

  object_class->dispose = wp_base_endpoint_dispose;
  object_class->finalize = wp_base_endpoint_finalize;
  object_class->get_property = wp_base_endpoint_get_property;
  object_class->set_property = wp_base_endpoint_set_property;

  g_object_class_install_property (object_class, PROP_CORE,
      g_param_spec_object ("core", "core", "The wireplumber core",
          WP_TYPE_CORE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  /**
   * WpBaseEndpoint::name:
   * The name of the endpoint.
   */
  g_object_class_install_property (object_class, PROP_NAME,
      g_param_spec_string ("name", "name", "The name of the endpoint", NULL,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  /**
   * WpBaseEndpoint::media-class:
   * The media class describes the type of media that this endpoint handles.
   * This should be the same as PipeWire media class strings.
   * For instance:
   * * Audio/Sink
   * * Audio/Source
   * * Video/Source
   * * Stream/Audio/Source
   */
  g_object_class_install_property (object_class, PROP_MEDIA_CLASS,
      g_param_spec_string ("media-class", "media-class",
          "The media class of the endpoint", NULL,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  signals[SIGNAL_NOTIFY_CONTROL_VALUE] = g_signal_new ("notify-control-value",
      G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
      G_TYPE_NONE, 1, G_TYPE_UINT);

  /**
   * WpBaseEndpoint::direction:
   * The direction of the endpoint: input = 0, output = 1.
   */
  g_object_class_install_property (object_class, PROP_DIRECTION,
      g_param_spec_uint ("direction", "direction",
          "The direction of the endpoint", 0, 1, 0,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  /**
   * WpBaseEndpoint::creation-time:
   * The creation time of the endpoint in monotonic time
   */
  g_object_class_install_property (object_class, PROP_CREATION_TIME,
      g_param_spec_uint64 ("creation-time", "creation-time",
          "The time that this endpoint was created, in monotonic time",
          0, G_MAXUINT64, 0, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /**
   * WpBaseEndpoint::priority:
   * The priority of the endpoint:
   */
  g_object_class_install_property (object_class, PROP_PRIORITY,
      g_param_spec_uint ("priority", "priority",
          "The priority of the endpoint", 0, G_MAXUINT, 0,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

/**
 * wp_base_endpoint_new_finish:
 * @initable: the #GAsyncInitable from the callback
 * @res: the #GAsyncResult from the callback
 * @error: return location for errors, or NULL to ignore
 *
 * Finishes the async construction of #WpBaseEndpoint.
 */
WpBaseEndpoint *
wp_base_endpoint_new_finish (GObject *initable, GAsyncResult *res, GError **error)
{
  GAsyncInitable *ai = G_ASYNC_INITABLE(initable);
  return WP_BASE_ENDPOINT(g_async_initable_new_finish(ai, res, error));
}

/**
 * wp_base_endpoint_register:
 * @self: the endpoint
 *
 * Registers the endpoint on the #WpCore.
 */
void
wp_base_endpoint_register (WpBaseEndpoint * self)
{
  WpBaseEndpointPrivate *priv;
  g_autoptr (WpCore) core = NULL;

  g_return_if_fail (WP_IS_BASE_ENDPOINT (self));

  priv = wp_base_endpoint_get_instance_private (self);
  core = g_weak_ref_get (&priv->core);
  g_return_if_fail (core != NULL);

  g_info ("WpBaseEndpoint:%p registering '%s' (%s)", self, priv->name,
      priv->media_class);

  wp_registry_register_object (wp_core_get_registry (core), g_object_ref (self));
}

/**
 * wp_base_endpoint_unregister:
 * @self: the endpoint
 *
 * Unregisters the endpoint from the session manager, if it was registered
 * and the session manager object still exists
 */
void
wp_base_endpoint_unregister (WpBaseEndpoint * self)
{
  WpBaseEndpointPrivate *priv;
  g_autoptr (WpCore) core = NULL;

  g_return_if_fail (WP_IS_BASE_ENDPOINT (self));

  priv = wp_base_endpoint_get_instance_private (self);

  /* unlink before unregistering so that policy modules
   * can find dangling unlinked endpoints */
  for (gint i = priv->links->len - 1; i >= 0; i--)
    wp_base_endpoint_link_destroy (g_ptr_array_index (priv->links, i));

  core = g_weak_ref_get (&priv->core);
  if (core) {
    g_info ("WpBaseEndpoint:%p unregistering '%s' (%s)", self, priv->name,
        priv->media_class);

    wp_registry_remove_object (wp_core_get_registry (core), self);
  }
}

/**
 * wp_base_endpoint_get_core:
 * @self: the endpoint
 *
 * Returns: (transfer full): the core on which this endpoint is registered
 */
WpCore *
wp_base_endpoint_get_core (WpBaseEndpoint * self)
{
  WpBaseEndpointPrivate *priv;

  g_return_val_if_fail (WP_IS_BASE_ENDPOINT (self), NULL);

  priv = wp_base_endpoint_get_instance_private (self);
  return g_weak_ref_get (&priv->core);
}

const gchar *
wp_base_endpoint_get_name (WpBaseEndpoint * self)
{
  WpBaseEndpointPrivate *priv;

  g_return_val_if_fail (WP_IS_BASE_ENDPOINT (self), NULL);

  priv = wp_base_endpoint_get_instance_private (self);
  return priv->name;
}

const gchar *
wp_base_endpoint_get_media_class (WpBaseEndpoint * self)
{
  WpBaseEndpointPrivate *priv;

  g_return_val_if_fail (WP_IS_BASE_ENDPOINT (self), NULL);

  priv = wp_base_endpoint_get_instance_private (self);
  return priv->media_class;
}

guint
wp_base_endpoint_get_direction (WpBaseEndpoint * self)
{
  WpBaseEndpointPrivate *priv;

  g_return_val_if_fail (WP_IS_BASE_ENDPOINT (self), -1);

  priv = wp_base_endpoint_get_instance_private (self);
  return priv->direction;
}

guint64
wp_base_endpoint_get_creation_time (WpBaseEndpoint * self)
{
  WpBaseEndpointPrivate *priv;

  g_return_val_if_fail (WP_IS_BASE_ENDPOINT (self), -1);

  priv = wp_base_endpoint_get_instance_private (self);
  return priv->creation_time;
}

guint32
wp_base_endpoint_get_priority (WpBaseEndpoint * self)
{
  WpBaseEndpointPrivate *priv;

  g_return_val_if_fail (WP_IS_BASE_ENDPOINT (self), -1);

  priv = wp_base_endpoint_get_instance_private (self);
  return priv->priority;
}

WpProperties *
wp_base_endpoint_get_properties (WpBaseEndpoint * self)
{
  g_return_val_if_fail (WP_IS_BASE_ENDPOINT (self), NULL);

  if (WP_BASE_ENDPOINT_GET_CLASS (self)->get_properties)
    return WP_BASE_ENDPOINT_GET_CLASS (self)->get_properties (self);

  return NULL;
}

const char *
wp_base_endpoint_get_role (WpBaseEndpoint * self)
{
  g_return_val_if_fail (WP_IS_BASE_ENDPOINT (self), NULL);

  if (WP_BASE_ENDPOINT_GET_CLASS (self)->get_role)
    return WP_BASE_ENDPOINT_GET_CLASS (self)->get_role (self);

  return NULL;
}

guint32
wp_base_endpoint_get_global_id (WpBaseEndpoint * self)
{
  g_return_val_if_fail (WP_IS_BASE_ENDPOINT (self), -1);

  if (WP_BASE_ENDPOINT_GET_CLASS (self)->get_global_id)
    return WP_BASE_ENDPOINT_GET_CLASS (self)->get_global_id (self);

  return -1;
}

void
wp_base_endpoint_begin_fade (WpBaseEndpoint * self, guint32 stream_id,
    guint duration, gfloat step, guint direction, guint type,
    GCancellable * cancellable, GAsyncReadyCallback callback, gpointer data)
{
  g_return_if_fail (WP_IS_BASE_ENDPOINT (self));

  if (WP_BASE_ENDPOINT_GET_CLASS (self)->begin_fade)
    WP_BASE_ENDPOINT_GET_CLASS (self)->begin_fade (self, stream_id,
        duration, step, direction, type, cancellable, callback, data);
}

/**
 * wp_base_endpoint_register_stream:
 * @self: the endpoint
 * @stream: (transfer floating): a dictionary GVariant with the stream info
 */
void
wp_base_endpoint_register_stream (WpBaseEndpoint * self, GVariant * stream)
{
  WpBaseEndpointPrivate *priv;

  g_return_if_fail (WP_IS_BASE_ENDPOINT (self));
  g_return_if_fail (g_variant_is_of_type (stream, G_VARIANT_TYPE_VARDICT));

  priv = wp_base_endpoint_get_instance_private (self);
  g_ptr_array_add (priv->streams, g_variant_ref_sink (stream));
}

GVariant *
wp_base_endpoint_get_stream (WpBaseEndpoint * self, guint32 stream_id)
{
  WpBaseEndpointPrivate *priv;
  guint32 id;
  gint i;

  g_return_val_if_fail (WP_IS_BASE_ENDPOINT (self), NULL);

  priv = wp_base_endpoint_get_instance_private (self);
  for (i = 0; i < priv->streams->len; i++) {
    GVariant *v = g_ptr_array_index (priv->streams, i);
    if (g_variant_lookup (v, "id", "u", &id) && id == stream_id) {
      return g_variant_ref (v);
    }
  }

  return NULL;
}

/**
 * wp_base_endpoint_list_streams:
 * @self: the endpoint
 *
 * Returns: (transfer floating): a floating GVariant that contains an array of
 *    dictionaries (aa{sv}) where each dictionary contains information about
 *    a single stream
 */
GVariant *
wp_base_endpoint_list_streams (WpBaseEndpoint * self)
{
  WpBaseEndpointPrivate *priv;

  g_return_val_if_fail (WP_IS_BASE_ENDPOINT (self), NULL);

  priv = wp_base_endpoint_get_instance_private (self);
  return g_variant_new_array (G_VARIANT_TYPE_VARDICT,
      (GVariant * const *) priv->streams->pdata, priv->streams->len);
}

guint32
wp_base_endpoint_find_stream (WpBaseEndpoint * self, const gchar * name)
{
  WpBaseEndpointPrivate *priv;
  const gchar *tmp = NULL;
  guint32 id = WP_STREAM_ID_NONE;
  gint i;

  g_return_val_if_fail (WP_IS_BASE_ENDPOINT (self), WP_STREAM_ID_NONE);
  g_return_val_if_fail (name != NULL, WP_STREAM_ID_NONE);

  priv = wp_base_endpoint_get_instance_private (self);
  for (i = 0; i < priv->streams->len; i++) {
    GVariant *v = g_ptr_array_index (priv->streams, i);
    if (g_variant_lookup (v, "name", "&s", &tmp) && !g_strcmp0 (tmp, name)) {
      /* found, return the id */
      g_variant_lookup (v, "id", "u", &id);
      break;
    }
  }

  return id;
}

/**
 * wp_base_endpoint_register_control:
 * @self: the endpoint
 * @control: (transfer floating): a dictionary GVariant with the control info
 */
void
wp_base_endpoint_register_control (WpBaseEndpoint * self, GVariant * control)
{
  WpBaseEndpointPrivate *priv;

  g_return_if_fail (WP_IS_BASE_ENDPOINT (self));
  g_return_if_fail (g_variant_is_of_type (control, G_VARIANT_TYPE_VARDICT));

  priv = wp_base_endpoint_get_instance_private (self);
  g_ptr_array_add (priv->controls, g_variant_ref_sink (control));
}

GVariant *
wp_base_endpoint_get_control (WpBaseEndpoint * self, guint32 control_id)
{
  WpBaseEndpointPrivate *priv;
  guint32 id;
  gint i;

  g_return_val_if_fail (WP_IS_BASE_ENDPOINT (self), NULL);

  priv = wp_base_endpoint_get_instance_private (self);
  for (i = 0; i < priv->controls->len; i++) {
    GVariant *v = g_ptr_array_index (priv->controls, i);
    if (g_variant_lookup (v, "id", "u", &id) && id == control_id) {
      return g_variant_ref (v);
    }
  }

  return NULL;
}

/**
 * wp_base_endpoint_list_controls:
 * @self: the endpoint
 *
 * Returns: (transfer floating): a floating GVariant that contains an array of
 *    dictionaries (aa{sv}) where each dictionary contains information about
 *    a single control
 */
GVariant *
wp_base_endpoint_list_controls (WpBaseEndpoint * self)
{
  WpBaseEndpointPrivate *priv;

  g_return_val_if_fail (WP_IS_BASE_ENDPOINT (self), NULL);

  priv = wp_base_endpoint_get_instance_private (self);
  return g_variant_new_array (G_VARIANT_TYPE_VARDICT,
      (GVariant * const *) priv->controls->pdata, priv->controls->len);
}

guint32
wp_base_endpoint_find_control (WpBaseEndpoint * self, guint32 stream_id,
    const gchar * name)
{
  WpBaseEndpointPrivate *priv;
  const gchar *tmp = NULL;
  guint32 tmp_id = WP_STREAM_ID_NONE;
  guint32 id = WP_CONTROL_ID_NONE;
  gint i;

  g_return_val_if_fail (WP_IS_BASE_ENDPOINT (self), WP_CONTROL_ID_NONE);
  g_return_val_if_fail (name != NULL, WP_CONTROL_ID_NONE);

  priv = wp_base_endpoint_get_instance_private (self);
  for (i = 0; i < priv->controls->len; i++) {
    GVariant *v = g_ptr_array_index (priv->controls, i);

    /*
     * if the stream-id exists, it must match @stream_id
     * if it doesn't exist, then @stream_id must be NONE
     */
    if (g_variant_lookup (v, "stream-id", "u", &tmp_id)) {
      if (stream_id != tmp_id)
        continue;
    } else if (stream_id != WP_STREAM_ID_NONE) {
      continue;
    }

    if (g_variant_lookup (v, "name", "&s", &tmp) && !g_strcmp0 (tmp, name)) {
      /* found, return the id */
      g_variant_lookup (v, "id", "u", &id);
      break;
    }
  }

  return id;
}

/**
 * wp_base_endpoint_get_control_value: (virtual get_control_value)
 * @self: the endpoint
 * @control_id: the id of the control to set
 *
 * Returns a GVariant that holds the value of the control. The type
 * should be the same type specified in the control variant's "type" field.
 *
 * On error, NULL will be returned.
 *
 * Returns: (transfer floating) (nullable): a dictionary GVariant containing
 *    the control value
 */
GVariant *
wp_base_endpoint_get_control_value (WpBaseEndpoint * self, guint32 control_id)
{
  g_return_val_if_fail (WP_IS_BASE_ENDPOINT (self), NULL);

  if (WP_BASE_ENDPOINT_GET_CLASS (self)->get_control_value)
    return WP_BASE_ENDPOINT_GET_CLASS (self)->get_control_value (self, control_id);
  else
    return NULL;
}

/**
 * wp_base_endpoint_set_control_value: (virtual set_control_value)
 * @self: the endpoint
 * @control_id: the id of the control to set
 * @value: (transfer none): the value to set on the control
 *
 * Sets the @value on the specified control. The implementation should
 * call wp_base_endpoint_notify_control_value() if the value has been changed
 * in order to signal the change.
 *
 * Returns: TRUE on success, FALSE on failure
 */
gboolean
wp_base_endpoint_set_control_value (WpBaseEndpoint * self, guint32 control_id,
    GVariant * value)
{
  gboolean ret = FALSE;

  g_return_val_if_fail (WP_IS_BASE_ENDPOINT (self), FALSE);

  if (WP_BASE_ENDPOINT_GET_CLASS (self)->set_control_value)
    ret = WP_BASE_ENDPOINT_GET_CLASS (self)->set_control_value (self, control_id,
        value);

  if (g_variant_is_floating (value))
    g_variant_unref (value);

  return ret;
}

/**
 * wp_base_endpoint_notify_control_value:
 * @self: the endpoint
 * @control_id: the id of the control
 *
 * Emits the "notify-control-value" signal so that others can be informed
 * about a value change in some of the controls. This is meant to be used
 * by subclasses only.
 */
void
wp_base_endpoint_notify_control_value (WpBaseEndpoint * self, guint32 control_id)
{
  g_return_if_fail (WP_IS_BASE_ENDPOINT (self));
  g_signal_emit (self, signals[SIGNAL_NOTIFY_CONTROL_VALUE], 0, control_id);
}

/**
 * wp_base_endpoint_is_linked:
 * @self: the endpoint
 *
 * Returns: TRUE if there is at least one link associated with this endpoint
 */
gboolean
wp_base_endpoint_is_linked (WpBaseEndpoint * self)
{
  WpBaseEndpointPrivate *priv;

  g_return_val_if_fail (WP_IS_BASE_ENDPOINT (self), FALSE);

  priv = wp_base_endpoint_get_instance_private (self);
  return (priv->links->len > 0);
}

/**
 * wp_base_endpoint_get_links:
 * @self: the endpoint
 *
 * Returns: (transfer none) (element-type WpBaseEndpointLink): an array of
 *    #WpBaseEndpointLink objects that are currently associated with this endpoint
 */
GPtrArray *
wp_base_endpoint_get_links (WpBaseEndpoint * self)
{
  WpBaseEndpointPrivate *priv;

  g_return_val_if_fail (WP_IS_BASE_ENDPOINT (self), NULL);

  priv = wp_base_endpoint_get_instance_private (self);
  return priv->links;
}

/**
 * wp_base_endpoint_unlink:
 * @self: the endpoint
 *
 * Unlinks all the endpoints linked to this endpoint
 */
void
wp_base_endpoint_unlink (WpBaseEndpoint * self)
{
  WpBaseEndpointPrivate *priv;
  gint i;

  g_return_if_fail (WP_IS_BASE_ENDPOINT (self));

  priv = wp_base_endpoint_get_instance_private (self);

  for (i = priv->links->len - 1; i >= 0; i--)
    wp_base_endpoint_link_destroy (g_ptr_array_index (priv->links, i));
}


typedef struct _WpBaseEndpointLinkPrivate WpBaseEndpointLinkPrivate;
struct _WpBaseEndpointLinkPrivate
{
  GWeakRef src;
  guint32 src_stream;
  GWeakRef sink;
  guint32 sink_stream;
  gboolean keep;
};

enum {
  LINKPROP_0,
  LINKPROP_SRC,
  LINKPROP_SRC_STREAM,
  LINKPROP_SINK,
  LINKPROP_SINK_STREAM,
  LINKPROP_KEEP,
};

static void wp_base_endpoint_link_async_initable_init (gpointer iface,
    gpointer iface_data);

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (WpBaseEndpointLink, wp_base_endpoint_link, G_TYPE_OBJECT,
    G_ADD_PRIVATE (WpBaseEndpointLink)
    G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE,
                           wp_base_endpoint_link_async_initable_init))

static void
endpoint_link_finalize (GObject * object)
{
  WpBaseEndpointLinkPrivate *priv =
      wp_base_endpoint_link_get_instance_private (WP_BASE_ENDPOINT_LINK (object));

  /* Clear the endpoint weak reaferences */
  g_weak_ref_clear(&priv->src);
  g_weak_ref_clear(&priv->sink);

  G_OBJECT_CLASS (wp_base_endpoint_link_parent_class)->finalize(object);
}

static void
endpoint_link_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpBaseEndpointLinkPrivate *priv =
      wp_base_endpoint_link_get_instance_private (WP_BASE_ENDPOINT_LINK (object));

  switch (property_id) {
  case LINKPROP_SRC:
    g_weak_ref_set (&priv->src, g_value_get_object (value));
    break;
  case LINKPROP_SRC_STREAM:
    priv->src_stream = g_value_get_uint(value);
    break;
  case LINKPROP_SINK:
    g_weak_ref_set (&priv->sink, g_value_get_object (value));
    break;
  case LINKPROP_SINK_STREAM:
    priv->sink_stream = g_value_get_uint(value);
    break;
  case LINKPROP_KEEP:
    priv->keep = g_value_get_boolean (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
endpoint_link_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpBaseEndpointLinkPrivate *priv =
      wp_base_endpoint_link_get_instance_private (WP_BASE_ENDPOINT_LINK (object));

  switch (property_id) {
  case LINKPROP_SRC:
    g_value_take_object (value, g_weak_ref_get (&priv->src));
    break;
  case LINKPROP_SRC_STREAM:
    g_value_set_uint (value, priv->src_stream);
    break;
  case LINKPROP_SINK:
    g_value_take_object (value, g_weak_ref_get (&priv->sink));
    break;
  case LINKPROP_SINK_STREAM:
    g_value_set_uint (value, priv->sink_stream);
    break;
  case LINKPROP_KEEP:
    g_value_set_boolean (value, priv->keep);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_base_endpoint_link_init_async (GAsyncInitable *initable, int io_priority,
    GCancellable *cancellable, GAsyncReadyCallback callback, gpointer data)
{
  WpBaseEndpointLink *link = WP_BASE_ENDPOINT_LINK(initable);
  WpBaseEndpointLinkPrivate *priv =
      wp_base_endpoint_link_get_instance_private (WP_BASE_ENDPOINT_LINK (initable));
  g_autoptr (WpBaseEndpoint) src = g_weak_ref_get (&priv->src);
  g_autoptr (WpBaseEndpoint) sink = g_weak_ref_get (&priv->sink);
  g_autoptr (GVariant) src_props = NULL;
  g_autoptr (GVariant) sink_props = NULL;
  WpBaseEndpointPrivate *endpoint_priv;

  /* Prepare the endpoints */
  if (!WP_BASE_ENDPOINT_GET_CLASS (src)->prepare_link (src, priv->src_stream, link,
      &src_props, NULL)) {
    g_critical ("Failed to prepare link on source endpoint");
    return;
  }
  if (!WP_BASE_ENDPOINT_GET_CLASS (sink)->prepare_link (sink, priv->sink_stream,
      link, &sink_props, NULL)) {
    g_critical ("Failed to prepare link on sink endpoint");
    return;
  }

  /* Create the link */
  g_return_if_fail (WP_BASE_ENDPOINT_LINK_GET_CLASS (link)->create);
  if (!WP_BASE_ENDPOINT_LINK_GET_CLASS (link)->create (link, src_props,
      sink_props, NULL)) {
    g_critical ("Failed to create link in src and sink endpoints");
    return;
  }

  /* Register the link on the endpoints */
  endpoint_priv = wp_base_endpoint_get_instance_private (src);
  g_ptr_array_add (endpoint_priv->links, g_object_ref (link));
  endpoint_priv = wp_base_endpoint_get_instance_private (sink);
  g_ptr_array_add (endpoint_priv->links, g_object_ref (link));
}

static gboolean
wp_base_endpoint_link_init_finish (GAsyncInitable *initable, GAsyncResult *result,
    GError **error)
{
  g_return_val_if_fail (g_task_is_valid (result, initable), FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

static void
wp_base_endpoint_link_async_initable_init (gpointer iface, gpointer iface_data)
{
  GAsyncInitableIface *ai_iface = iface;

  ai_iface->init_async = wp_base_endpoint_link_init_async;
  ai_iface->init_finish = wp_base_endpoint_link_init_finish;
}

static void
wp_base_endpoint_link_init (WpBaseEndpointLink * self)
{
  WpBaseEndpointLinkPrivate *priv = wp_base_endpoint_link_get_instance_private (self);

  /* Init the endpoint weak references */
  g_weak_ref_init (&priv->src, NULL);
  g_weak_ref_init (&priv->sink, NULL);
}

static void
wp_base_endpoint_link_class_init (WpBaseEndpointLinkClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;

  object_class->finalize = endpoint_link_finalize;
  object_class->set_property = endpoint_link_set_property;
  object_class->get_property = endpoint_link_get_property;

  g_object_class_install_property (object_class, LINKPROP_SRC,
      g_param_spec_object ("src", "src", "The src endpoint", WP_TYPE_BASE_ENDPOINT,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class, LINKPROP_SRC_STREAM,
      g_param_spec_uint ("src-stream", "src-stream", "The src stream",
          0, G_MAXUINT, 0,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class, LINKPROP_SINK,
      g_param_spec_object ("sink", "sink", "The sink endpoint", WP_TYPE_BASE_ENDPOINT,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class, LINKPROP_SINK_STREAM,
      g_param_spec_uint ("sink-stream", "sink-stream", "The sink stream",
          0, G_MAXUINT, 0,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class, LINKPROP_KEEP,
      g_param_spec_boolean ("keep", "keep", "If we want to keep the link",
          FALSE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

/**
 * wp_base_endpoint_link_get_source_endpoint:
 * @self: the endpoint
 *
 * Gets the source endpoint of the link
 *
 * Returns: (transfer full): the source endpoint
 */
WpBaseEndpoint *
wp_base_endpoint_link_get_source_endpoint (WpBaseEndpointLink * self)
{
  WpBaseEndpointLinkPrivate *priv;

  g_return_val_if_fail (WP_IS_BASE_ENDPOINT_LINK (self), NULL);

  priv = wp_base_endpoint_link_get_instance_private (self);
  return g_weak_ref_get (&priv->src);
}

guint32
wp_base_endpoint_link_get_source_stream (WpBaseEndpointLink * self)
{
  WpBaseEndpointLinkPrivate *priv;

  g_return_val_if_fail (WP_IS_BASE_ENDPOINT_LINK (self), 0);

  priv = wp_base_endpoint_link_get_instance_private (self);
  return priv->src_stream;
}

/**
 * wp_base_endpoint_link_get_sink_endpoint:
 * @self: the endpoint
 *
 * Gets the sink endpoint of the link
 *
 * Returns: (transfer full): the sink endpoint
 */
WpBaseEndpoint *
wp_base_endpoint_link_get_sink_endpoint (WpBaseEndpointLink * self)
{
  WpBaseEndpointLinkPrivate *priv;

  g_return_val_if_fail (WP_IS_BASE_ENDPOINT_LINK (self), NULL);

  priv = wp_base_endpoint_link_get_instance_private (self);
  return g_weak_ref_get (&priv->sink);
}

guint32
wp_base_endpoint_link_get_sink_stream (WpBaseEndpointLink * self)
{
  WpBaseEndpointLinkPrivate *priv;

  g_return_val_if_fail (WP_IS_BASE_ENDPOINT_LINK (self), 0);

  priv = wp_base_endpoint_link_get_instance_private (self);
  return priv->sink_stream;
}

gboolean
wp_base_endpoint_link_is_kept (WpBaseEndpointLink * self)
{
  WpBaseEndpointLinkPrivate *priv;

  g_return_val_if_fail (WP_IS_BASE_ENDPOINT_LINK (self), 0);

  priv = wp_base_endpoint_link_get_instance_private (self);
  return priv->keep;
}

void
wp_base_endpoint_link_new (WpCore * core, WpBaseEndpoint * src, guint32 src_stream,
    WpBaseEndpoint * sink, guint32 sink_stream, gboolean keep,
    GAsyncReadyCallback ready, gpointer data)
{
  const gchar *src_factory = NULL, *sink_factory = NULL;
  GVariantBuilder b;
  g_autoptr (GVariant) link_props = NULL;

  g_return_if_fail (WP_IS_BASE_ENDPOINT (src));
  g_return_if_fail (WP_IS_BASE_ENDPOINT (sink));
  g_return_if_fail (WP_BASE_ENDPOINT_GET_CLASS (src)->prepare_link);
  g_return_if_fail (WP_BASE_ENDPOINT_GET_CLASS (sink)->prepare_link);

  /* find the factory */

  if (WP_BASE_ENDPOINT_GET_CLASS (src)->get_endpoint_link_factory)
    src_factory = WP_BASE_ENDPOINT_GET_CLASS (src)->get_endpoint_link_factory (src);
  if (WP_BASE_ENDPOINT_GET_CLASS (sink)->get_endpoint_link_factory)
    sink_factory = WP_BASE_ENDPOINT_GET_CLASS (sink)->get_endpoint_link_factory (sink);

  if (src_factory || sink_factory) {
    if (src_factory && sink_factory && strcmp (src_factory, sink_factory) != 0) {
      g_critical ("It is not possible to link endpoints that both specify"
          "different custom link factories");
      return;
    } else if (sink_factory)
      src_factory = sink_factory;
  } else {
    src_factory = "pipewire-simple-endpoint-link";
  }

  /* Build the properties */
  g_variant_builder_init (&b, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&b, "{sv}", "src",
      g_variant_new_uint64 ((guint64)src));
  g_variant_builder_add (&b, "{sv}", "src-stream",
      g_variant_new_uint32 (src_stream));
  g_variant_builder_add (&b, "{sv}", "sink",
      g_variant_new_uint64 ((guint64)sink));
  g_variant_builder_add (&b, "{sv}", "sink-stream",
      g_variant_new_uint32 (sink_stream));
  g_variant_builder_add (&b, "{sv}", "keep",
      g_variant_new_boolean (keep));
  link_props = g_variant_builder_end (&b);

  /* Create the link object async */
  wp_factory_make (core, src_factory, WP_TYPE_BASE_ENDPOINT_LINK, link_props, ready,
      data);
}

WpBaseEndpointLink *
wp_base_endpoint_link_new_finish (GObject *initable, GAsyncResult *res,
    GError **error)
{
  GAsyncInitable *ai = G_ASYNC_INITABLE(initable);
  return WP_BASE_ENDPOINT_LINK(g_async_initable_new_finish(ai, res, error));
}

void
wp_base_endpoint_link_destroy (WpBaseEndpointLink * self)
{
  WpBaseEndpointLinkPrivate *priv;
  WpBaseEndpointPrivate *endpoint_priv;
  g_autoptr (WpBaseEndpoint) src = NULL;
  g_autoptr (WpBaseEndpoint) sink = NULL;

  g_return_if_fail (WP_IS_BASE_ENDPOINT_LINK (self));
  g_return_if_fail (WP_BASE_ENDPOINT_LINK_GET_CLASS (self)->destroy);

  priv = wp_base_endpoint_link_get_instance_private (self);
  src = g_weak_ref_get (&priv->src);
  sink = g_weak_ref_get (&priv->sink);

  WP_BASE_ENDPOINT_LINK_GET_CLASS (self)->destroy (self);

  if (src && WP_BASE_ENDPOINT_GET_CLASS (src)->release_link)
    WP_BASE_ENDPOINT_GET_CLASS (src)->release_link (src, self);
  if (sink && WP_BASE_ENDPOINT_GET_CLASS (sink)->release_link)
    WP_BASE_ENDPOINT_GET_CLASS (sink)->release_link (sink, self);

  if (src) {
    endpoint_priv = wp_base_endpoint_get_instance_private (src);
    g_ptr_array_remove_fast (endpoint_priv->links, self);
  }
  if (sink) {
    endpoint_priv = wp_base_endpoint_get_instance_private (sink);
    g_ptr_array_remove_fast (endpoint_priv->links, self);
  }
}
