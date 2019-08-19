/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <spa/param/props.h>
#include <pipewire/pipewire.h>

#include "stream.h"

typedef struct _WpAudioStreamPrivate WpAudioStreamPrivate;
struct _WpAudioStreamPrivate
{
  GObject parent;

  /* Props */
  GWeakRef endpoint;
  guint id;
  gchar *name;
  enum pw_direction direction;

  /* Remote Pipewire */
  WpRemotePipewire *remote_pipewire;

  /* Stream Proxy and Listener */
  struct pw_node_proxy *proxy;
  struct spa_hook listener;

  /* Stream Port Proxies */
  GPtrArray *port_proxies;

  /* Stream Controls */
  gfloat volume;
  gboolean mute;
};

enum {
  PROP_0,
  PROP_ENDPOINT,
  PROP_ID,
  PROP_NAME,
  PROP_DIRECTION,
};

enum {
  CONTROL_VOLUME = 0,
  CONTROL_MUTE,
  N_CONTROLS,
};

static void wp_audio_stream_async_initable_init (gpointer iface, gpointer iface_data);

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (WpAudioStream, wp_audio_stream, G_TYPE_OBJECT,
    G_ADD_PRIVATE (WpAudioStream)
    G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE, wp_audio_stream_async_initable_init))

guint
wp_audio_stream_id_encode (guint stream_id, guint control_id)
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
wp_audio_stream_id_decode (guint id, guint *stream_id, guint *control_id)
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
on_audio_stream_port_created(GObject *initable, GAsyncResult *res,
    gpointer data)
{
  WpAudioStream *self = data;
  WpAudioStreamPrivate *priv = wp_audio_stream_get_instance_private (self);
  WpProxyPort *port_proxy = NULL;
  GError *error = NULL;

  /* Get the proxy port */
  port_proxy = WP_PROXY_PORT (wp_proxy_port_new_finish (initable, res, &error));
  if (!port_proxy)
    return;

  /* Check for error */
  if (error) {
    g_warning ("WpAudioStream:%p Stream port failed on creation", self);
    g_clear_object (&port_proxy);
    return;
  }

  /* Add the proxy port to the array */
  g_return_if_fail (priv->port_proxies);
  g_ptr_array_add(priv->port_proxies, port_proxy);
}

static void
on_audio_stream_port_added(WpRemotePipewire *rp, guint id, gconstpointer p,
    gpointer d)
{
  WpAudioStream *self = d;
  const struct pw_node_info *info = NULL;
  struct pw_proxy *proxy = NULL;
  const struct spa_dict *props = p;
  const char *s;
  guint node_id = 0;

  /* Get the node id */
  g_return_if_fail (WP_AUDIO_STREAM_GET_CLASS (self)->get_info);
  info = WP_AUDIO_STREAM_GET_CLASS (self)->get_info (self);
  if (!info)
    return;

  if ((s = spa_dict_lookup(props, PW_KEY_NODE_ID)))
    node_id = atoi(s);

  /* Skip ports that are not owned by this stream */
  if (info->id != node_id)
    return;

  /* Create the port proxy async */
  proxy = wp_remote_pipewire_proxy_bind (rp, id, PW_TYPE_INTERFACE_Port);
  g_return_if_fail(proxy);
  wp_proxy_port_new(id, proxy, on_audio_stream_port_created, self);
}

static void
audio_stream_event_info (void *object, const struct pw_node_info *info)
{
  WpAudioStream *self = object;
  WpAudioStreamPrivate *priv = wp_audio_stream_get_instance_private (self);

  /* Let the derived class handle this it is implemented */
  if (WP_AUDIO_STREAM_GET_CLASS (self)->event_info)
    WP_AUDIO_STREAM_GET_CLASS (self)->event_info (self, info,
        priv->remote_pipewire);
}

static void
audio_stream_event_param (void *object, int seq, uint32_t id,
    uint32_t index, uint32_t next, const struct spa_pod *param)
{
  WpAudioStreamPrivate *priv =
      wp_audio_stream_get_instance_private (WP_AUDIO_STREAM (object));
  g_autoptr (WpEndpoint) ep = g_weak_ref_get (&priv->endpoint);

  switch (id) {
    case SPA_PARAM_Props:
    {
      struct spa_pod_prop *prop;
      struct spa_pod_object *obj = (struct spa_pod_object *) param;
      float volume = priv->volume;
      bool mute = priv->mute;

      SPA_POD_OBJECT_FOREACH(obj, prop) {
        switch (prop->key) {
        case SPA_PROP_volume:
          spa_pod_get_float(&prop->value, &volume);
          if (priv->volume != volume) {
            priv->volume = volume;
            wp_endpoint_notify_control_value (ep,
                wp_audio_stream_id_encode (priv->id, CONTROL_VOLUME));
          }
          break;
        case SPA_PROP_mute:
          spa_pod_get_bool(&prop->value, &mute);
          if (priv->mute != mute) {
            priv->mute = mute;
            wp_endpoint_notify_control_value (ep,
                wp_audio_stream_id_encode (priv->id, CONTROL_MUTE));
          }
          break;
        default:
          break;
        }
      }

      break;
    }
    default:
      break;
  }
}

static const struct pw_node_proxy_events audio_stream_proxy_events = {
  PW_VERSION_NODE_PROXY_EVENTS,
  .info = audio_stream_event_info,
  .param = audio_stream_event_param,
};

static void
wp_audio_stream_finalize (GObject * object)
{
  WpAudioStreamPrivate *priv =
      wp_audio_stream_get_instance_private (WP_AUDIO_STREAM (object));

  /* Clear the endpoint weak reference */
  g_weak_ref_clear (&priv->endpoint);

  /* Clear the name */
  g_free (priv->name);
  priv->name = NULL;

  /* Clear the port proxies */
  if (priv->port_proxies) {
    g_ptr_array_free(priv->port_proxies, TRUE);
    priv->port_proxies = NULL;
  }

  G_OBJECT_CLASS (wp_audio_stream_parent_class)->finalize (object);
}

static void
wp_audio_stream_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpAudioStreamPrivate *priv =
      wp_audio_stream_get_instance_private (WP_AUDIO_STREAM (object));

  switch (property_id) {
  case PROP_ENDPOINT:
    g_weak_ref_set (&priv->endpoint, g_value_get_object (value));
    break;
  case PROP_ID:
    priv->id = g_value_get_uint(value);
    break;
  case PROP_NAME:
    priv->name = g_value_dup_string (value);
    break;
  case PROP_DIRECTION:
    priv->direction = g_value_get_uint(value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_audio_stream_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpAudioStreamPrivate *priv =
      wp_audio_stream_get_instance_private (WP_AUDIO_STREAM (object));

  switch (property_id) {
    case PROP_ENDPOINT:
    g_value_take_object (value, g_weak_ref_get (&priv->endpoint));
    break;
  case PROP_ID:
    g_value_set_uint (value, priv->id);
    break;
  case PROP_NAME:
    g_value_set_string (value, priv->name);
    break;
  case PROP_DIRECTION:
    g_value_set_uint (value, priv->direction);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_audio_stream_init_async (GAsyncInitable *initable, int io_priority,
    GCancellable *cancellable, GAsyncReadyCallback callback, gpointer data)
{
  WpAudioStream *self = WP_AUDIO_STREAM(initable);
  WpAudioStreamPrivate *priv = wp_audio_stream_get_instance_private (self);
  g_autoptr (WpEndpoint) ep = g_weak_ref_get (&priv->endpoint);
  g_autoptr (WpCore) core = wp_endpoint_get_core (ep);
  GVariantDict d;

  /* Set the remote pipewire */
  priv->remote_pipewire = wp_core_get_global (core, WP_GLOBAL_REMOTE_PIPEWIRE);

  /* Init the list of port proxies */
  priv->port_proxies = g_ptr_array_new_full(4, (GDestroyNotify)g_object_unref);

  /* Register the volume control */
  g_variant_dict_init (&d, NULL);
  g_variant_dict_insert (&d, "id", "u",
      wp_audio_stream_id_encode (priv->id, CONTROL_VOLUME));
  if (priv->id != WP_STREAM_ID_NONE)
    g_variant_dict_insert (&d, "stream-id", "u", priv->id);
  g_variant_dict_insert (&d, "name", "s", "volume");
  g_variant_dict_insert (&d, "type", "s", "d");
  g_variant_dict_insert (&d, "range", "(dd)", 0.0, 1.0);
  g_variant_dict_insert (&d, "default-value", "d", priv->volume);
  wp_endpoint_register_control (ep, g_variant_dict_end (&d));

  /* Register the mute control */
  g_variant_dict_init (&d, NULL);
  g_variant_dict_insert (&d, "id", "u",
      wp_audio_stream_id_encode (priv->id, CONTROL_MUTE));
  if (priv->id != WP_STREAM_ID_NONE)
    g_variant_dict_insert (&d, "stream-id", "u", priv->id);
  g_variant_dict_insert (&d, "name", "s", "mute");
  g_variant_dict_insert (&d, "type", "s", "b");
  g_variant_dict_insert (&d, "default-value", "b", priv->mute);
  wp_endpoint_register_control (ep, g_variant_dict_end (&d));

  /* Create and set the proxy */
  g_return_if_fail (WP_AUDIO_STREAM_GET_CLASS (self)->create_proxy);
  priv->proxy = WP_AUDIO_STREAM_GET_CLASS (self)->create_proxy (self,
      priv->remote_pipewire);
  g_return_if_fail (priv->proxy);

  /* Add a custom listener */
  pw_node_proxy_add_listener(priv->proxy, &priv->listener,
      &audio_stream_proxy_events, self);

  /* Register a port_added callback */
  g_signal_connect_object(priv->remote_pipewire, "global-added::port",
      (GCallback)on_audio_stream_port_added, self, 0);
}

static gboolean
wp_audio_stream_init_finish (GAsyncInitable *initable, GAsyncResult *result,
    GError **error)
{
  g_return_val_if_fail (g_task_is_valid (result, initable), FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

static void
wp_audio_stream_async_initable_init (gpointer iface, gpointer iface_data)
{
  GAsyncInitableIface *ai_iface = iface;

  ai_iface->init_async = wp_audio_stream_init_async;
  ai_iface->init_finish = wp_audio_stream_init_finish;
}

static void
wp_audio_stream_init (WpAudioStream * self)
{
  WpAudioStreamPrivate *priv = wp_audio_stream_get_instance_private (self);

  /* Controls */
  priv->volume = 1.0;
  priv->mute = FALSE;
}

static void
wp_audio_stream_class_init (WpAudioStreamClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;

  object_class->finalize = wp_audio_stream_finalize;
  object_class->set_property = wp_audio_stream_set_property;
  object_class->get_property = wp_audio_stream_get_property;

  /* Install the properties */
  g_object_class_install_property (object_class, PROP_ENDPOINT,
      g_param_spec_object ("endpoint", "endpoint",
          "The endpoint this audio stream belongs to", WP_TYPE_ENDPOINT,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class, PROP_ID,
      g_param_spec_uint ("id", "id", "The Id of the audio stream", 0, G_MAXUINT, 0,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class, PROP_NAME,
      g_param_spec_string ("name", "name", "The name of the audio stream", NULL,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class, PROP_DIRECTION,
      g_param_spec_uint ("direction", "direction",
          "The direction of the audio stream", 0, 1, 0,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

WpAudioStream *
wp_audio_stream_new_finish (GObject *initable, GAsyncResult *res,
    GError **error)
{
  GAsyncInitable *ai = G_ASYNC_INITABLE(initable);
  return WP_AUDIO_STREAM (g_async_initable_new_finish(ai, res, error));
}

const char *
wp_audio_stream_get_name (WpAudioStream * self)
{
  WpAudioStreamPrivate *priv = wp_audio_stream_get_instance_private (self);

  return priv->name;
}

enum pw_direction
wp_audio_stream_get_direction (WpAudioStream * self)
{
  WpAudioStreamPrivate *priv = wp_audio_stream_get_instance_private (self);

  return priv->direction;
}

gconstpointer
wp_audio_stream_get_info (WpAudioStream * self)
{
  const struct pw_node_info *info = NULL;

  g_return_val_if_fail (WP_AUDIO_STREAM_GET_CLASS (self)->get_info, NULL);
  info = WP_AUDIO_STREAM_GET_CLASS (self)->get_info (self);

  return info;
}

static void
port_proxies_foreach_func(gpointer data, gpointer user_data)
{
  GVariantBuilder *b = user_data;
  g_variant_builder_add (b, "t", data);
}

gboolean
wp_audio_stream_prepare_link (WpAudioStream * self, GVariant ** properties,
    GError ** error)
{
  WpAudioStreamPrivate *priv = wp_audio_stream_get_instance_private (self);
  const struct pw_node_info *info = NULL;
  GVariantBuilder b, *b_ports;
  GVariant *v_ports;

  /* Get the proxy node id */
  g_return_val_if_fail (WP_AUDIO_STREAM_GET_CLASS (self)->get_info, FALSE);
  info = WP_AUDIO_STREAM_GET_CLASS (self)->get_info (self);
  g_return_val_if_fail (info, FALSE);

  /* Create a variant array with all the ports */
  b_ports = g_variant_builder_new (G_VARIANT_TYPE ("at"));
  g_ptr_array_foreach(priv->port_proxies, port_proxies_foreach_func, b_ports);
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
wp_audio_stream_get_control_value (WpAudioStream * self, guint32 control_id)
{
  WpAudioStreamPrivate *priv = wp_audio_stream_get_instance_private (self);

  switch (control_id) {
    case CONTROL_VOLUME:
      return g_variant_new_double (priv->volume);
    case CONTROL_MUTE:
      return g_variant_new_boolean (priv->mute);
    default:
      g_warning ("Unknown control id %u", control_id);
      return NULL;
  }
}

gboolean
wp_audio_stream_set_control_value (WpAudioStream * self, guint32 control_id,
    GVariant * value)
{
  WpAudioStreamPrivate *priv = wp_audio_stream_get_instance_private (self);

  char buf[1024];
  struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof(buf));
  float volume;
  bool mute;

  /* Make sure the proxy is valid */
  g_return_val_if_fail (priv->proxy, FALSE);

  /* Set the specific constrol */
  switch (control_id) {
    case CONTROL_VOLUME:
      volume = g_variant_get_double (value);
      pw_node_proxy_set_param (priv->proxy,
          SPA_PARAM_Props, 0,
          spa_pod_builder_add_object (&b,
              SPA_TYPE_OBJECT_Props, SPA_PARAM_Props,
              SPA_PROP_volume, SPA_POD_Float(volume),
              NULL));
      pw_node_proxy_enum_params (priv->proxy, 0, SPA_PARAM_Props, 0, -1, NULL);
      break;

    case CONTROL_MUTE:
      mute = g_variant_get_boolean (value);
      pw_node_proxy_set_param (priv->proxy,
          SPA_PARAM_Props, 0,
          spa_pod_builder_add_object (&b,
              SPA_TYPE_OBJECT_Props, SPA_PARAM_Props,
              SPA_PROP_mute, SPA_POD_Bool(mute),
              NULL));
      pw_node_proxy_enum_params (priv->proxy, 0, SPA_PARAM_Props, 0, -1, NULL);
      break;

    default:
      g_warning ("Unknown control id %u", control_id);
      return FALSE;
  }

  return TRUE;
}
