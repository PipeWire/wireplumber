/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <spa/param/props.h>
#include <pipewire/pipewire.h>
#include <spa/debug/types.h>
#include <spa/pod/builder.h>
#include <spa/pod/iter.h>
#include <spa/param/audio/type-info.h>

#include "stream.h"

typedef struct _WpAudioStreamPrivate WpAudioStreamPrivate;
struct _WpAudioStreamPrivate
{
  GObject parent;

  GTask *init_task;

  /* Props */
  GWeakRef endpoint;
  guint id;
  gchar *name;
  enum pw_direction direction;

  /* Stream Proxy */
  WpProxyNode *proxy;

  WpObjectManager *ports_om;
  GVariantBuilder port_vb;
  gboolean port_config_done;

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
  PROP_PROXY_NODE,
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
audio_stream_event_param (WpProxy *proxy, int seq, uint32_t id,
    uint32_t index, uint32_t next, const struct spa_pod *param,
    WpAudioStream *self)
{
  WpAudioStreamPrivate *priv = wp_audio_stream_get_instance_private (self);
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

static void
on_ports_changed (WpObjectManager *om, WpAudioStream *self)
{
  WpAudioStreamPrivate *priv = wp_audio_stream_get_instance_private (self);

  if (priv->port_config_done) {
    g_debug ("%s:%p port config done", G_OBJECT_TYPE_NAME (self), self);
    wp_audio_stream_init_task_finish (self, NULL);
    g_signal_handlers_disconnect_by_func (priv->ports_om, on_ports_changed,
        self);
  }
}

static void
on_node_proxy_augmented (WpProxy * proxy, GAsyncResult * res,
    WpAudioStream * self)
{
  WpAudioStreamPrivate *priv = wp_audio_stream_get_instance_private (self);
  g_autoptr (GError) error = NULL;
  g_autoptr (WpCore) core = NULL;
  GVariantBuilder b;
  const struct pw_node_info *info = NULL;
  g_autofree gchar *node_id = NULL;

  wp_proxy_augment_finish (proxy, res, &error);
  if (error) {
    g_warning ("WpAudioStream:%p Node proxy failed to augment: %s", self,
        error->message);
    wp_audio_stream_init_task_finish (self, g_steal_pointer (&error));
    return;
  }

  g_signal_connect_object (proxy, "param",
      (GCallback) audio_stream_event_param, self, 0);
  wp_proxy_node_subscribe_params (WP_PROXY_NODE (proxy), 1, SPA_PARAM_Props);

  priv->ports_om = wp_object_manager_new ();

  /* Get the node id */
  info = wp_proxy_node_get_info (WP_PROXY_NODE (proxy));
  node_id = g_strdup_printf ("%u", info->id);

  /* set a constraint: the port's "node.id" must match
     the stream's underlying node id */
  g_variant_builder_init (&b, G_VARIANT_TYPE ("aa{sv}"));
  g_variant_builder_open (&b, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&b, "{sv}", "type",
      g_variant_new_int32 (WP_OBJECT_MANAGER_CONSTRAINT_PW_GLOBAL_PROPERTY));
  g_variant_builder_add (&b, "{sv}", "name",
      g_variant_new_string (PW_KEY_NODE_ID));
  g_variant_builder_add (&b, "{sv}", "value",
      g_variant_new_string (node_id));
  g_variant_builder_close (&b);

  /* declare interest on ports with this constraint */
  wp_object_manager_add_proxy_interest (priv->ports_om, PW_TYPE_INTERFACE_Port,
      g_variant_builder_end (&b),
      WP_PROXY_FEATURE_PW_PROXY | WP_PROXY_FEATURE_INFO);

  g_signal_connect (priv->ports_om, "objects-changed",
      (GCallback) on_ports_changed, self);

  /* install the object manager */
  g_object_get (proxy, "core", &core, NULL);
  wp_core_install_object_manager (core, priv->ports_om);
}

static void
wp_audio_stream_finalize (GObject * object)
{
  WpAudioStreamPrivate *priv =
      wp_audio_stream_get_instance_private (WP_AUDIO_STREAM (object));

  g_clear_object (&priv->proxy);
  g_clear_object (&priv->ports_om);

  /* Clear the endpoint weak reference */
  g_weak_ref_clear (&priv->endpoint);

  /* Clear the name */
  g_clear_pointer (&priv->name, g_free);

  g_clear_object (&priv->init_task);

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
  case PROP_PROXY_NODE:
    priv->proxy = g_value_dup_object (value);
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
  case PROP_PROXY_NODE:
    g_value_set_object (value, priv->proxy);
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
  g_autoptr (WpCore) core = wp_audio_stream_get_core (self);
  GVariantDict d;

  g_debug ("WpEndpoint:%p init stream %s (%s:%p)", ep, priv->name,
      G_OBJECT_TYPE_NAME (self), self);

  priv->init_task = g_task_new (initable, cancellable, callback, data);

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

  g_return_if_fail (priv->proxy);
  wp_proxy_augment (WP_PROXY (priv->proxy),
      WP_PROXY_FEATURE_PW_PROXY | WP_PROXY_FEATURE_INFO, NULL,
      (GAsyncReadyCallback) on_node_proxy_augmented, self);
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
  g_object_class_install_property (object_class, PROP_PROXY_NODE,
      g_param_spec_object ("proxy-node", "proxy-node",
          "The node proxy of the stream", WP_TYPE_PROXY_NODE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
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

WpProxyNode *
wp_audio_stream_get_proxy_node (WpAudioStream * self)
{
  WpAudioStreamPrivate *priv = wp_audio_stream_get_instance_private (self);

  return priv->proxy;
}

const struct pw_node_info *
wp_audio_stream_get_info (WpAudioStream * self)
{
  WpAudioStreamPrivate *priv = wp_audio_stream_get_instance_private (self);

  return wp_proxy_node_get_info (priv->proxy);
}

static void
port_proxies_foreach_func(gpointer data, gpointer user_data)
{
  WpAudioStream *self = user_data;
  WpAudioStreamPrivate *priv = wp_audio_stream_get_instance_private (self);
  WpProxyPort *port = data;
  const struct pw_node_info *node_info;
  const struct pw_port_info *port_info;
  g_autoptr (WpProperties) props = NULL;
  const gchar *channel;
  uint32_t channel_n = SPA_AUDIO_CHANNEL_UNKNOWN;

  node_info = wp_proxy_node_get_info (priv->proxy);
  g_return_if_fail (node_info);

  port_info = wp_proxy_port_get_info (port);
  g_return_if_fail (port_info);

  props = wp_proxy_port_get_properties (port);
  channel = wp_properties_get (props, PW_KEY_AUDIO_CHANNEL);
  if (channel) {
    const struct spa_type_info *t = spa_type_audio_channel;
    for (; t && t->name; t++) {
      const char *name = t->name + strlen(SPA_TYPE_INFO_AUDIO_CHANNEL_BASE);
      if (!g_strcmp0 (channel, name)) {
        channel_n = t->type;
        break;
      }
    }
  }

  /* tuple format:
      uint32 node_id;
      uint32 port_id;
      uint32 channel;  // enum spa_audio_channel
      uint8 direction; // enum spa_direction
   */
  g_variant_builder_add (&priv->port_vb, "(uuuy)", node_info->id,
      port_info->id, channel_n, (guint8) port_info->direction);
}

gboolean
wp_audio_stream_prepare_link (WpAudioStream * self, GVariant ** properties,
    GError ** error)
{
  WpAudioStreamPrivate *priv = wp_audio_stream_get_instance_private (self);
  g_autoptr (GPtrArray) port_proxies =
      wp_object_manager_get_objects (priv->ports_om, 0);

  /* Create a variant array with all the ports */
  g_variant_builder_init (&priv->port_vb, G_VARIANT_TYPE ("a(uuuy)"));
  g_ptr_array_foreach (port_proxies, port_proxies_foreach_func, self);
  *properties = g_variant_builder_end (&priv->port_vb);

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
      wp_proxy_node_set_param (priv->proxy,
          SPA_PARAM_Props, 0,
          spa_pod_builder_add_object (&b,
              SPA_TYPE_OBJECT_Props, SPA_PARAM_Props,
              SPA_PROP_volume, SPA_POD_Float(volume),
              NULL));
      break;

    case CONTROL_MUTE:
      mute = g_variant_get_boolean (value);
      wp_proxy_node_set_param (priv->proxy,
          SPA_PARAM_Props, 0,
          spa_pod_builder_add_object (&b,
              SPA_TYPE_OBJECT_Props, SPA_PARAM_Props,
              SPA_PROP_mute, SPA_POD_Bool(mute),
              NULL));
      break;

    default:
      g_warning ("Unknown control id %u", control_id);
      return FALSE;
  }

  return TRUE;
}

WpCore *
wp_audio_stream_get_core (WpAudioStream * self)
{
  WpAudioStreamPrivate *priv = wp_audio_stream_get_instance_private (self);
  g_autoptr (WpEndpoint) ep = NULL;
  g_autoptr (WpCore) core = NULL;

  ep = g_weak_ref_get (&priv->endpoint);
  core = wp_endpoint_get_core (ep);
  return g_steal_pointer (&core);
}

void
wp_audio_stream_init_task_finish (WpAudioStream * self, GError * err)
{
  WpAudioStreamPrivate *priv = wp_audio_stream_get_instance_private (self);
  g_autoptr (GError) error = err;

  if (!priv->init_task)
    return;

  if (error)
    g_task_return_error (priv->init_task, g_steal_pointer (&error));
  else
    g_task_return_boolean (priv->init_task, TRUE);

  g_clear_object (&priv->init_task);
}

void
wp_audio_stream_set_port_config (WpAudioStream * self,
    const struct spa_pod * param)
{
  WpAudioStreamPrivate *priv = wp_audio_stream_get_instance_private (self);

  wp_proxy_node_set_param (priv->proxy, SPA_PARAM_PortConfig, 0, param);
}

void
wp_audio_stream_finish_port_config (WpAudioStream * self)
{
  WpAudioStreamPrivate *priv = wp_audio_stream_get_instance_private (self);
  priv->port_config_done = TRUE;
}
