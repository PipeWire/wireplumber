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

#include "endpoint.h"
#include "error.h"
#include "factory.h"

typedef struct _WpEndpointPrivate WpEndpointPrivate;
struct _WpEndpointPrivate
{
  gchar *name;
  gchar media_class[40];
  GPtrArray *streams;
  GPtrArray *controls;
  GPtrArray *links;
  WpCore *core;
};

enum {
  PROP_0,
  PROP_NAME,
  PROP_MEDIA_CLASS,
};

enum {
  SIGNAL_NOTIFY_CONTROL_VALUE,
  NUM_SIGNALS
};

static guint32 signals[NUM_SIGNALS];

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (WpEndpoint, wp_endpoint, G_TYPE_OBJECT)

static void
wp_endpoint_init (WpEndpoint * self)
{
  WpEndpointPrivate *priv = wp_endpoint_get_instance_private (self);

  priv->streams =
      g_ptr_array_new_with_free_func ((GDestroyNotify) g_variant_unref);
  priv->controls =
      g_ptr_array_new_with_free_func ((GDestroyNotify) g_variant_unref);
  priv->links = g_ptr_array_new_with_free_func (g_object_unref);
}

static void
wp_endpoint_dispose (GObject * object)
{
  WpEndpointPrivate *priv =
      wp_endpoint_get_instance_private (WP_ENDPOINT (object));
  gint i;

  /* wp_endpoint_link_destroy removes elements from the array,
   * so traversing in reverse order is faster and less complicated */
  for (i = priv->links->len - 1; i >= 0; i--) {
    wp_endpoint_link_destroy (g_ptr_array_index (priv->links, i));
  }

  G_OBJECT_CLASS (wp_endpoint_parent_class)->dispose (object);
}

static void
wp_endpoint_finalize (GObject * object)
{
  WpEndpointPrivate *priv =
      wp_endpoint_get_instance_private (WP_ENDPOINT (object));

  g_ptr_array_unref (priv->streams);
  g_ptr_array_unref (priv->controls);
  g_ptr_array_unref (priv->links);
  g_free (priv->name);

  G_OBJECT_CLASS (wp_endpoint_parent_class)->finalize (object);
}

static void
wp_endpoint_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpEndpointPrivate *priv =
      wp_endpoint_get_instance_private (WP_ENDPOINT (object));

  switch (property_id) {
  case PROP_NAME:
    priv->name = g_value_dup_string (value);
    break;
  case PROP_MEDIA_CLASS:
    strncpy (priv->media_class, g_value_get_string (value),
        sizeof (priv->media_class) - 1);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_endpoint_get_property (GObject * object, guint property_id, GValue * value,
    GParamSpec * pspec)
{
  WpEndpointPrivate *priv =
      wp_endpoint_get_instance_private (WP_ENDPOINT (object));

  switch (property_id) {
  case PROP_NAME:
    g_value_set_string (value, priv->name);
    break;
  case PROP_MEDIA_CLASS:
    g_value_set_string (value, priv->media_class);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_endpoint_class_init (WpEndpointClass * klass)
{
  GObjectClass * object_class = (GObjectClass *) klass;

  object_class->dispose = wp_endpoint_dispose;
  object_class->finalize = wp_endpoint_finalize;
  object_class->get_property = wp_endpoint_get_property;
  object_class->set_property = wp_endpoint_set_property;

  /**
   * WpEndpoint::name:
   * The name of the endpoint.
   */
  g_object_class_install_property (object_class, PROP_NAME,
      g_param_spec_string ("name", "name", "The name of the endpoint", NULL,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  /**
   * WpEndpoint::media-class:
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
}

/**
 * wp_endpoint_register:
 * @self: the endpoint
 * @core: the core
 *
 * Registers the endpoint on the @core.
 */
void
wp_endpoint_register (WpEndpoint * self, WpCore * core)
{
  WpEndpointPrivate *priv;

  g_return_if_fail (WP_IS_ENDPOINT (self));
  g_return_if_fail (WP_IS_CORE (core));

  priv = wp_endpoint_get_instance_private (self);

  g_info ("WpEndpoint:%p registering '%s' (%s)", self, priv->name,
      priv->media_class);

  priv->core = core;
  wp_core_register_global (core, WP_GLOBAL_ENDPOINT, g_object_ref (self),
      g_object_unref);
}

/**
 * wp_endpoint_unregister:
 * @self: the endpoint
 *
 * Unregisters the endpoint from the session manager, if it was registered
 * and the session manager object still exists
 */
void
wp_endpoint_unregister (WpEndpoint * self)
{
  WpEndpointPrivate *priv;

  g_return_if_fail (WP_IS_ENDPOINT (self));

  priv = wp_endpoint_get_instance_private (self);
  if (priv->core) {
    g_info ("WpEndpoint:%p unregistering '%s' (%s)", self, priv->name,
        priv->media_class);

    g_object_ref (self);
    wp_core_remove_global (priv->core, WP_GLOBAL_ENDPOINT, self);
    priv->core = NULL;
    g_object_unref (self);
  }
}

struct endpoints_foreach_data
{
  GPtrArray *result;
  const gchar *lookup;
};

static inline gboolean
media_class_matches (const gchar * media_class, const gchar * lookup)
{
  const gchar *c1 = media_class, *c2 = lookup;

  /* empty lookup matches all classes */
  if (!lookup)
    return TRUE;

  /* compare until we reach the end of the lookup string */
  for (; *c2 != '\0'; c1++, c2++) {
    if (*c1 != *c2)
      return FALSE;
  }

  /* the lookup may not end in a slash, however it must match up
   * to the end of a submedia_class. i.e.:
   * match: media_class: Audio/Source/Virtual
   *        lookup: Audio/Source
   *
   * NO match: media_class: Audio/Source/Virtual
   *           lookup: Audio/Sou
   *
   * if *c1 is not /, also check the previous char, because the lookup
   * may actually end in a slash:
   *
   * match: media_class: Audio/Source/Virtual
   *        lookup: Audio/Source/
   */
  if (!(*c1 == '/' || *c1 == '\0' || *(c1 - 1) == '/'))
    return FALSE;

  return TRUE;
}

static gboolean
find_endpoints (GQuark key, gpointer global, gpointer user_data)
{
  struct endpoints_foreach_data * data = user_data;

  if (key == WP_GLOBAL_ENDPOINT &&
      media_class_matches (wp_endpoint_get_media_class (WP_ENDPOINT (global)),
          data->lookup))
    g_ptr_array_add (data->result, g_object_ref (global));

  return WP_CORE_FOREACH_GLOBAL_CONTINUE;
}

GPtrArray *
wp_endpoint_find (WpCore * core, const gchar * media_class_lookup)
{
  struct endpoints_foreach_data data;
  data.result = g_ptr_array_new_with_free_func (g_object_unref);
  data.lookup = media_class_lookup;
  wp_core_foreach_global (core, find_endpoints, &data);
  return data.result;
}

const gchar *
wp_endpoint_get_name (WpEndpoint * self)
{
  WpEndpointPrivate *priv;

  g_return_val_if_fail (WP_IS_ENDPOINT (self), NULL);

  priv = wp_endpoint_get_instance_private (self);
  return priv->name;
}

const gchar *
wp_endpoint_get_media_class (WpEndpoint * self)
{
  WpEndpointPrivate *priv;

  g_return_val_if_fail (WP_IS_ENDPOINT (self), NULL);

  priv = wp_endpoint_get_instance_private (self);
  return priv->media_class;
}

/**
 * wp_endpoint_register_stream:
 * @self: the endpoint
 * @stream: (transfer floating): a dictionary GVariant with the stream info
 */
void
wp_endpoint_register_stream (WpEndpoint * self, GVariant * stream)
{
  WpEndpointPrivate *priv;

  g_return_if_fail (WP_IS_ENDPOINT (self));
  g_return_if_fail (g_variant_is_of_type (stream, G_VARIANT_TYPE_VARDICT));

  priv = wp_endpoint_get_instance_private (self);
  g_ptr_array_add (priv->streams, g_variant_ref_sink (stream));
}

/**
 * wp_endpoint_list_streams:
 * @self: the endpoint
 *
 * Returns: (transfer floating): a floating GVariant that contains an array of
 *    dictionaries (aa{sv}) where each dictionary contains information about
 *    a single stream
 */
GVariant *
wp_endpoint_list_streams (WpEndpoint * self)
{
  WpEndpointPrivate *priv;

  g_return_val_if_fail (WP_IS_ENDPOINT (self), NULL);

  priv = wp_endpoint_get_instance_private (self);
  return g_variant_new_array (G_VARIANT_TYPE_VARDICT,
      (GVariant * const *) priv->streams->pdata, priv->streams->len);
}

/**
 * wp_endpoint_register_control:
 * @self: the endpoint
 * @control: (transfer floating): a dictionary GVariant with the control info
 */
void
wp_endpoint_register_control (WpEndpoint * self, GVariant * control)
{
  WpEndpointPrivate *priv;

  g_return_if_fail (WP_IS_ENDPOINT (self));
  g_return_if_fail (g_variant_is_of_type (control, G_VARIANT_TYPE_VARDICT));

  priv = wp_endpoint_get_instance_private (self);
  g_ptr_array_add (priv->controls, g_variant_ref_sink (control));
}

/**
 * wp_endpoint_list_controls:
 * @self: the endpoint
 *
 * Returns: (transfer floating): a floating GVariant that contains an array of
 *    dictionaries (aa{sv}) where each dictionary contains information about
 *    a single control
 */
GVariant *
wp_endpoint_list_controls (WpEndpoint * self)
{
  WpEndpointPrivate *priv;

  g_return_val_if_fail (WP_IS_ENDPOINT (self), NULL);

  priv = wp_endpoint_get_instance_private (self);
  return g_variant_new_array (G_VARIANT_TYPE_VARDICT,
      (GVariant * const *) priv->controls->pdata, priv->controls->len);
}

/**
 * wp_endpoint_get_control_value: (virtual get_control_value)
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
wp_endpoint_get_control_value (WpEndpoint * self, guint32 control_id)
{
  g_return_val_if_fail (WP_IS_ENDPOINT (self), NULL);

  if (WP_ENDPOINT_GET_CLASS (self)->get_control_value)
    return WP_ENDPOINT_GET_CLASS (self)->get_control_value (self, control_id);
  else
    return NULL;
}

/**
 * wp_endpoint_set_control_value: (virtual set_control_value)
 * @self: the endpoint
 * @control_id: the id of the control to set
 * @value: (transfer none): the value to set on the control
 *
 * Sets the @value on the specified control. The implementation should
 * call wp_endpoint_notify_control_value() if the value has been changed
 * in order to signal the change.
 *
 * Returns: TRUE on success, FALSE on failure
 */
gboolean
wp_endpoint_set_control_value (WpEndpoint * self, guint32 control_id,
    GVariant * value)
{
  g_return_val_if_fail (WP_IS_ENDPOINT (self), FALSE);

  if (WP_ENDPOINT_GET_CLASS (self)->set_control_value)
    return WP_ENDPOINT_GET_CLASS (self)->set_control_value (self, control_id,
        value);
  else
    return FALSE;
}

/**
 * wp_endpoint_notify_control_value:
 * @self: the endpoint
 * @control_id: the id of the control
 *
 * Emits the "notify-control-value" signal so that others can be informed
 * about a value change in some of the controls. This is meant to be used
 * by subclasses only.
 */
void
wp_endpoint_notify_control_value (WpEndpoint * self, guint32 control_id)
{
  g_return_if_fail (WP_IS_ENDPOINT (self));
  g_signal_emit (self, signals[SIGNAL_NOTIFY_CONTROL_VALUE], 0, control_id);
}

/**
 * wp_endpoint_is_linked:
 * @self: the endpoint
 *
 * Returns: TRUE if there is at least one link associated with this endpoint
 */
gboolean
wp_endpoint_is_linked (WpEndpoint * self)
{
  WpEndpointPrivate *priv;

  g_return_val_if_fail (WP_IS_ENDPOINT (self), FALSE);

  priv = wp_endpoint_get_instance_private (self);
  return (priv->links->len > 0);
}

/**
 * wp_endpoint_get_links:
 * @self: the endpoint
 *
 * Returns: (transfer none) (element-type WpEndpointLink): an array of
 *    #WpEndpointLink objects that are currently associated with this endpoint
 */
GPtrArray *
wp_endpoint_get_links (WpEndpoint * self)
{
  WpEndpointPrivate *priv;

  g_return_val_if_fail (WP_IS_ENDPOINT (self), NULL);

  priv = wp_endpoint_get_instance_private (self);
  return priv->links;
}


typedef struct _WpEndpointLinkPrivate WpEndpointLinkPrivate;
struct _WpEndpointLinkPrivate
{
  WpEndpoint *src;
  guint32 src_stream;
  WpEndpoint *sink;
  guint32 sink_stream;
};

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (WpEndpointLink, wp_endpoint_link, G_TYPE_OBJECT)

static void
wp_endpoint_link_init (WpEndpointLink * self)
{
}

static void
wp_endpoint_link_class_init (WpEndpointLinkClass * klass)
{
}

void
wp_endpoint_link_set_endpoints (WpEndpointLink * self, WpEndpoint * src,
    guint32 src_stream, WpEndpoint * sink, guint32 sink_stream)
{
  WpEndpointLinkPrivate *priv;

  g_return_if_fail (WP_IS_ENDPOINT_LINK (self));
  g_return_if_fail (WP_IS_ENDPOINT (src));
  g_return_if_fail (WP_IS_ENDPOINT (sink));

  priv = wp_endpoint_link_get_instance_private (self);
  priv->src = src;
  priv->src_stream = src_stream;
  priv->sink = sink;
  priv->sink_stream = sink_stream;
}

WpEndpoint *
wp_endpoint_link_get_source_endpoint (WpEndpointLink * self)
{
  WpEndpointLinkPrivate *priv;

  g_return_val_if_fail (WP_IS_ENDPOINT_LINK (self), NULL);

  priv = wp_endpoint_link_get_instance_private (self);
  return priv->src;
}

guint32
wp_endpoint_link_get_source_stream (WpEndpointLink * self)
{
  WpEndpointLinkPrivate *priv;

  g_return_val_if_fail (WP_IS_ENDPOINT_LINK (self), 0);

  priv = wp_endpoint_link_get_instance_private (self);
  return priv->src_stream;
}

WpEndpoint *
wp_endpoint_link_get_sink_endpoint (WpEndpointLink * self)
{
  WpEndpointLinkPrivate *priv;

  g_return_val_if_fail (WP_IS_ENDPOINT_LINK (self), NULL);

  priv = wp_endpoint_link_get_instance_private (self);
  return priv->sink;
}

guint32
wp_endpoint_link_get_sink_stream (WpEndpointLink * self)
{
  WpEndpointLinkPrivate *priv;

  g_return_val_if_fail (WP_IS_ENDPOINT_LINK (self), 0);

  priv = wp_endpoint_link_get_instance_private (self);
  return priv->sink_stream;
}

WpEndpointLink * wp_endpoint_link_new (WpCore * core, WpEndpoint * src,
    guint32 src_stream, WpEndpoint * sink, guint32 sink_stream, GError ** error)
{
  g_autoptr (WpEndpointLink) link = NULL;
  g_autoptr (GVariant) src_props = NULL;
  g_autoptr (GVariant) sink_props = NULL;
  const gchar *src_factory = NULL, *sink_factory = NULL;
  WpEndpointPrivate *endpoint_priv;

  g_return_val_if_fail (WP_IS_ENDPOINT (src), NULL);
  g_return_val_if_fail (WP_IS_ENDPOINT (sink), NULL);
  g_return_val_if_fail (WP_ENDPOINT_GET_CLASS (src)->prepare_link, NULL);
  g_return_val_if_fail (WP_ENDPOINT_GET_CLASS (sink)->prepare_link, NULL);

  /* find the factory */

  if (WP_ENDPOINT_GET_CLASS (src)->get_endpoint_link_factory)
    src_factory = WP_ENDPOINT_GET_CLASS (src)->get_endpoint_link_factory (src);
  if (WP_ENDPOINT_GET_CLASS (sink)->get_endpoint_link_factory)
    sink_factory = WP_ENDPOINT_GET_CLASS (sink)->get_endpoint_link_factory (sink);

  if (src_factory || sink_factory) {
    if (src_factory && sink_factory && strcmp (src_factory, sink_factory) != 0) {
      g_set_error (error, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVALID_ARGUMENT,
          "It is not possible to link endpoints that both specify different "
          "custom link factories");
      return NULL;
    } else if (sink_factory)
      src_factory = sink_factory;
  } else {
    src_factory = "pipewire-simple-endpoint-link";
  }

  /* create link object */

  link = wp_factory_make (core, src_factory, WP_TYPE_ENDPOINT_LINK, NULL);
  if (!link) {
    g_set_error (error, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_OPERATION_FAILED,
        "Failed to create link object from factory %s", src_factory);
    return NULL;
  }
  g_return_val_if_fail (WP_ENDPOINT_LINK_GET_CLASS (link)->create, NULL);

  /* prepare the link */

  wp_endpoint_link_set_endpoints (link, src, src_stream, sink, sink_stream);

  if (!WP_ENDPOINT_GET_CLASS (src)->prepare_link (src, src_stream, link,
          &src_props, error))
    return NULL;
  if (!WP_ENDPOINT_GET_CLASS (sink)->prepare_link (sink, sink_stream, link,
          &sink_props, error))
    return NULL;

  /* create the link */

  if (!WP_ENDPOINT_LINK_GET_CLASS (link)->create (link, src_props, sink_props,
          error))
    return NULL;

  /* register the link on the endpoints */

  endpoint_priv = wp_endpoint_get_instance_private (src);
  g_ptr_array_add (endpoint_priv->links, g_object_ref (link));

  endpoint_priv = wp_endpoint_get_instance_private (sink);
  g_ptr_array_add (endpoint_priv->links, g_object_ref (link));

  return link;
}

void
wp_endpoint_link_destroy (WpEndpointLink * self)
{
  WpEndpointLinkPrivate *priv;
  WpEndpointPrivate *endpoint_priv;

  g_return_if_fail (WP_IS_ENDPOINT_LINK (self));
  g_return_if_fail (WP_ENDPOINT_LINK_GET_CLASS (self)->destroy);

  priv = wp_endpoint_link_get_instance_private (self);

  WP_ENDPOINT_LINK_GET_CLASS (self)->destroy (self);
  if (WP_ENDPOINT_GET_CLASS (priv->src)->release_link)
    WP_ENDPOINT_GET_CLASS (priv->src)->release_link (priv->src, self);
  if (WP_ENDPOINT_GET_CLASS (priv->sink)->release_link)
    WP_ENDPOINT_GET_CLASS (priv->sink)->release_link (priv->sink, self);

  endpoint_priv = wp_endpoint_get_instance_private (priv->src);
  g_ptr_array_remove_fast (endpoint_priv->links, self);

  endpoint_priv = wp_endpoint_get_instance_private (priv->sink);
  g_ptr_array_remove_fast (endpoint_priv->links, self);
}
