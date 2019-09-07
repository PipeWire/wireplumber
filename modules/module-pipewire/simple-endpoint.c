/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * The simple endpoint is a WpEndpoint implementation that represents
 * all ports of a single direction of a single pipewire node.
 * It can be used to create an Endpoint for a client node or for any
 * other arbitrary node that does not need any kind of internal management.
 */

#include <spa/param/audio/format-utils.h>

#include <wp/wp.h>
#include <pipewire/pipewire.h>
#include <spa/pod/parser.h>
#include <spa/param/props.h>

struct _WpPipewireSimpleEndpoint
{
  WpEndpoint parent;

  /* properties */
  gchar *role;
  guint64 creation_time;
  gchar *target;

  /* The task to signal the endpoint is initialized */
  GTask *init_task;

  /* The remote pipewire */
  WpRemotePipewire *remote_pipewire;

  /* Proxies */
  WpProxyNode *proxy_node;
  GPtrArray *proxies_port;

  /* controls cache */
  gfloat volume;
  gboolean mute;
};

enum {
  PROP_0,
  PROP_PROXY_NODE,
  PROP_ROLE,
  PROP_CREATION_TIME,
  PROP_TARGET,
};

enum {
  CONTROL_VOLUME = 0,
  CONTROL_MUTE,
};

static GAsyncInitableIface *wp_simple_endpoint_parent_interface = NULL;
static void wp_simple_endpoint_async_initable_init (gpointer iface,
    gpointer iface_data);

G_DECLARE_FINAL_TYPE (WpPipewireSimpleEndpoint,
    simple_endpoint, WP_PIPEWIRE, SIMPLE_ENDPOINT, WpEndpoint)

G_DEFINE_TYPE_WITH_CODE (WpPipewireSimpleEndpoint, simple_endpoint,
    WP_TYPE_ENDPOINT,
    G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE,
                           wp_simple_endpoint_async_initable_init))

static gboolean
proxy_safe_augment_finish (WpPipewireSimpleEndpoint * self, WpProxy *proxy,
    GAsyncResult *res)
{
  GError *error = NULL;

  /* Return FALSE if we are already aborting */
  if (!self->init_task)
    return FALSE;

  wp_proxy_augment_finish (proxy, res, &error);
  if (error) {
    g_warning ("WpPipewireSimpleEndpoint:%p Aborting construction", self);
    g_task_return_error (self->init_task, error);
    g_clear_object (&self->init_task);
    return FALSE;
  }

  return TRUE;
}

static void
node_proxy_param (WpProxy *proxy, int seq, uint32_t id,
    uint32_t index, uint32_t next, const struct spa_pod *param,
    WpPipewireSimpleEndpoint *self)
{
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

      g_debug ("WpEndpoint:%p param event, vol:(%lf -> %f) mute:(%d -> %d)",
          self, self->volume, volume, self->mute, mute);

      if (self->volume != volume) {
        self->volume = volume;
        wp_endpoint_notify_control_value (WP_ENDPOINT (self), CONTROL_VOLUME);
      }
      if (self->mute != mute) {
        self->mute = mute;
        wp_endpoint_notify_control_value (WP_ENDPOINT (self), CONTROL_MUTE);
      }

      break;
    }
    default:
      break;
  }
}

static void
on_all_ports_done (WpProxy *proxy, GAsyncResult *res,
    WpPipewireSimpleEndpoint *self)
{
  GError *error = NULL;

  /* return if already aborted */
  if (!self->init_task)
    return;

  wp_proxy_sync_finish (proxy, res, &error);

  if (error)
    g_task_return_error (self->init_task, error);
  else
    g_task_return_boolean (self->init_task, TRUE);

  g_clear_object(&self->init_task);
}

static void
on_proxy_port_augmented (WpProxy *proxy, GAsyncResult *res,
    WpPipewireSimpleEndpoint *self)
{
  if (!proxy_safe_augment_finish (self, proxy, res))
    return;

  /* Add the proxy port to the array */
  g_ptr_array_add(self->proxies_port, g_object_ref (proxy));

  /* Sync with the server and use the task data as a flag to know
     whether we already called sync or not */
  if (!g_task_get_task_data (self->init_task)) {
    wp_proxy_sync (WP_PROXY(self->proxy_node), NULL,
        (GAsyncReadyCallback) on_all_ports_done, self);
    g_task_set_task_data (self->init_task, GUINT_TO_POINTER (1), NULL);
  }
}

static void
on_port_added(WpRemotePipewire *rp, WpProxy *proxy, gpointer d)
{
  WpPipewireSimpleEndpoint *self = d;
  const char *s;
  guint node_id = 0;
  g_autoptr (WpProperties) props = wp_proxy_get_global_properties (proxy);

  /* Don't do anything if we are aborting */
  if (!self->init_task)
    return;

  if ((s = wp_properties_get (props, PW_KEY_NODE_ID)))
    node_id = atoi(s);

  /* Only handle ports owned by this endpoint */
  if (node_id != wp_proxy_get_global_id (WP_PROXY (self->proxy_node)))
    return;

  /* Augment */
  wp_proxy_augment (proxy, WP_PROXY_FEATURE_PW_PROXY | WP_PROXY_FEATURE_INFO,
      NULL, (GAsyncReadyCallback) on_proxy_port_augmented, self);
}

static void
emit_endpoint_ports(WpPipewireSimpleEndpoint *self)
{
  enum pw_direction direction = wp_endpoint_get_direction (WP_ENDPOINT (self));
  struct spa_audio_info_raw format = { 0, };
  struct spa_pod *param;
  char buf[1024];
  struct spa_pod_builder pod_builder = SPA_POD_BUILDER_INIT(buf, sizeof(buf));

  /* The default format for audio clients */
  format.format = SPA_AUDIO_FORMAT_F32P;
  format.flags = 1;
  format.rate = 48000;
  format.channels = 2;
  format.position[0] = SPA_AUDIO_CHANNEL_FL;
  format.position[1] = SPA_AUDIO_CHANNEL_FR;

  /* Build the param profile */
  param = spa_format_audio_raw_build(&pod_builder, SPA_PARAM_Format, &format);
  param = spa_pod_builder_add_object(&pod_builder,
      SPA_TYPE_OBJECT_ParamPortConfig,  SPA_PARAM_PortConfig,
      SPA_PARAM_PORT_CONFIG_direction,  SPA_POD_Id(direction),
      SPA_PARAM_PORT_CONFIG_mode,       SPA_POD_Id(SPA_PARAM_PORT_CONFIG_MODE_dsp),
      SPA_PARAM_PORT_CONFIG_format,     SPA_POD_Pod(param));

  /* Set the param profile to emit the ports */
  wp_proxy_node_set_param (self->proxy_node, SPA_PARAM_PortConfig, 0, param);
}

static void
on_proxy_node_augmented (WpProxy *proxy, GAsyncResult *res, gpointer data)
{
  WpPipewireSimpleEndpoint *self = data;
  GVariantDict d;
  g_autoptr (WpProperties) props = NULL;

  if (!proxy_safe_augment_finish (self, proxy, res))
    return;

  props = wp_proxy_node_get_properties (self->proxy_node);

  /* Set the role and target name */
  self->role = g_strdup (wp_properties_get (props, PW_KEY_MEDIA_ROLE));
  self->target = g_strdup (wp_properties_get (props, "target.name"));

  /* Emit the ports */
  emit_endpoint_ports(self);

  g_signal_connect (self->proxy_node, "param", (GCallback) node_proxy_param,
      self);
  wp_proxy_node_subscribe_params (self->proxy_node, 1, SPA_PARAM_Props);

  g_variant_dict_init (&d, NULL);
  g_variant_dict_insert (&d, "id", "u", 0);
  g_variant_dict_insert (&d, "name", "s", "default");
  wp_endpoint_register_stream (WP_ENDPOINT (self), g_variant_dict_end (&d));

  /* Audio streams have volume & mute controls */
  if (g_strrstr (wp_endpoint_get_media_class (WP_ENDPOINT (self)), "Audio")) {
    g_variant_dict_init (&d, NULL);
    g_variant_dict_insert (&d, "id", "u", CONTROL_VOLUME);
    g_variant_dict_insert (&d, "stream-id", "u", 0);
    g_variant_dict_insert (&d, "name", "s", "volume");
    g_variant_dict_insert (&d, "type", "s", "d");
    g_variant_dict_insert (&d, "range", "(dd)", 0.0, 1.0);
    g_variant_dict_insert (&d, "default-value", "d", 1.0);
    wp_endpoint_register_control (WP_ENDPOINT (self), g_variant_dict_end (&d));

    g_variant_dict_init (&d, NULL);
    g_variant_dict_insert (&d, "id", "u", CONTROL_MUTE);
    g_variant_dict_insert (&d, "stream-id", "u", 0);
    g_variant_dict_insert (&d, "name", "s", "mute");
    g_variant_dict_insert (&d, "type", "s", "b");
    g_variant_dict_insert (&d, "default-value", "b", FALSE);
    wp_endpoint_register_control (WP_ENDPOINT (self), g_variant_dict_end (&d));
  }
}

static void
wp_simple_endpoint_init_async (GAsyncInitable *initable, int io_priority,
    GCancellable *cancellable, GAsyncReadyCallback callback, gpointer data)
{
  WpPipewireSimpleEndpoint *self = WP_PIPEWIRE_SIMPLE_ENDPOINT (initable);
  g_autoptr (WpCore) core = wp_endpoint_get_core(WP_ENDPOINT(self));

  /* Create the async task */
  self->init_task = g_task_new (initable, cancellable, callback, data);

  /* Register a port_added callback */
  self->remote_pipewire = wp_core_get_global (core, WP_GLOBAL_REMOTE_PIPEWIRE);
  g_return_if_fail(self->remote_pipewire);
  g_signal_connect_object(self->remote_pipewire, "global-added::port",
    (GCallback)on_port_added, self, 0);

  /* Augment to get the info */
  wp_proxy_augment (WP_PROXY (self->proxy_node),
      WP_PROXY_FEATURE_PW_PROXY | WP_PROXY_FEATURE_INFO, cancellable,
      (GAsyncReadyCallback) on_proxy_node_augmented, self);

  /* Call the parent interface */
  wp_simple_endpoint_parent_interface->init_async (initable, io_priority,
      cancellable, callback, data);
}

static void
wp_simple_endpoint_async_initable_init (gpointer iface, gpointer iface_data)
{
  GAsyncInitableIface *ai_iface = iface;

  /* Set the parent interface */
  wp_simple_endpoint_parent_interface = g_type_interface_peek_parent (iface);

  /* Only set the init_async */
  ai_iface->init_async = wp_simple_endpoint_init_async;
}

static void
simple_endpoint_init (WpPipewireSimpleEndpoint * self)
{
  self->creation_time = (guint64) g_get_monotonic_time ();
  self->proxies_port = g_ptr_array_new_full(2, (GDestroyNotify)g_object_unref);
}

static void
simple_endpoint_finalize (GObject * object)
{
  WpPipewireSimpleEndpoint *self = WP_PIPEWIRE_SIMPLE_ENDPOINT (object);

  /* Destroy the proxies port */
  g_clear_pointer (&self->proxies_port, g_ptr_array_unref);

  /* Destroy the proxy node */
  g_clear_object(&self->proxy_node);

  /* Destroy the done task */
  g_clear_object(&self->init_task);

  g_free (self->role);

  G_OBJECT_CLASS (simple_endpoint_parent_class)->finalize (object);
}

static void
simple_endpoint_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpPipewireSimpleEndpoint *self = WP_PIPEWIRE_SIMPLE_ENDPOINT (object);

  switch (property_id) {
  case PROP_PROXY_NODE:
    self->proxy_node = g_value_dup_object (value);
    break;
  case PROP_ROLE:
    g_free (self->role);
    self->role = g_value_dup_string (value);
    break;
  case PROP_TARGET:
    g_free (self->target);
    self->target = g_value_dup_string (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
simple_endpoint_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpPipewireSimpleEndpoint *self = WP_PIPEWIRE_SIMPLE_ENDPOINT (object);

  switch (property_id) {
  case PROP_PROXY_NODE:
    g_value_set_object (value, self->proxy_node);
    break;
  case PROP_ROLE:
    g_value_set_string (value, self->role);
    break;
  case PROP_CREATION_TIME:
    g_value_set_uint64 (value, self->creation_time);
    break;
  case PROP_TARGET:
    g_value_set_string (value, self->target);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
proxies_port_foreach_func(gpointer data, gpointer user_data)
{
  GVariantBuilder *b = user_data;
  g_variant_builder_add (b, "t", data);
}

static gboolean
simple_endpoint_prepare_link (WpEndpoint * ep, guint32 stream_id,
    WpEndpointLink * link, GVariant ** properties, GError ** error)
{
  WpPipewireSimpleEndpoint *self = WP_PIPEWIRE_SIMPLE_ENDPOINT (ep);
  GVariantBuilder b, *b_ports;
  GVariant *v_ports;

  /* Create a variant array with all the ports */
  b_ports = g_variant_builder_new (G_VARIANT_TYPE ("at"));
  g_ptr_array_foreach(self->proxies_port, proxies_port_foreach_func, b_ports);
  v_ports = g_variant_builder_end (b_ports);

  /* Set the properties */
  g_variant_builder_init (&b, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&b, "{sv}", "node-id", g_variant_new_uint32 (
          wp_proxy_get_global_id (WP_PROXY (self->proxy_node))));
  g_variant_builder_add (&b, "{sv}", "ports", v_ports);
  *properties = g_variant_builder_end (&b);

  return TRUE;
}

static GVariant *
simple_endpoint_get_control_value (WpEndpoint * ep, guint32 control_id)
{
  WpPipewireSimpleEndpoint *self = WP_PIPEWIRE_SIMPLE_ENDPOINT (ep);

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

static gboolean
simple_endpoint_set_control_value (WpEndpoint * ep, guint32 control_id,
    GVariant * value)
{
  WpPipewireSimpleEndpoint *self = WP_PIPEWIRE_SIMPLE_ENDPOINT (ep);
  char buf[1024];
  struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof(buf));
  float volume;
  bool mute;

  switch (control_id) {
    case CONTROL_VOLUME:
      volume = g_variant_get_double (value);

      g_debug("WpEndpoint:%p set volume control (%u) value, vol:%f", self,
          control_id, volume);

      wp_proxy_node_set_param (self->proxy_node,
          SPA_PARAM_Props, 0,
          spa_pod_builder_add_object (&b,
              SPA_TYPE_OBJECT_Props, SPA_PARAM_Props,
              SPA_PROP_volume, SPA_POD_Float(volume),
              NULL));
      break;

    case CONTROL_MUTE:
      mute = g_variant_get_boolean (value);

      g_debug("WpEndpoint:%p set mute control (%u) value, mute:%d", self,
          control_id, mute);

      wp_proxy_node_set_param (self->proxy_node,
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

static void
simple_endpoint_class_init (WpPipewireSimpleEndpointClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpEndpointClass *endpoint_class = (WpEndpointClass *) klass;

  object_class->finalize = simple_endpoint_finalize;
  object_class->set_property = simple_endpoint_set_property;
  object_class->get_property = simple_endpoint_get_property;

  endpoint_class->prepare_link = simple_endpoint_prepare_link;
  endpoint_class->get_control_value = simple_endpoint_get_control_value;
  endpoint_class->set_control_value = simple_endpoint_set_control_value;

  g_object_class_install_property (object_class, PROP_PROXY_NODE,
      g_param_spec_object ("proxy-node", "proxy-node",
          "The node this endpoint refers to", WP_TYPE_PROXY_NODE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class, PROP_ROLE,
      g_param_spec_string ("role", "role", "The role of the wrapped node", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class, PROP_CREATION_TIME,
      g_param_spec_uint64 ("creation-time", "creation-time",
          "The time that this endpoint was created, in monotonic time",
          0, G_MAXUINT64, 0, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class, PROP_TARGET,
      g_param_spec_string ("target", "target", "The target of the wrapped node", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

void
simple_endpoint_factory (WpFactory * factory, GType type,
    GVariant * properties, GAsyncReadyCallback ready, gpointer user_data)
{
  g_autoptr (WpCore) core = NULL;
  const gchar *name, *media_class;
  guint direction;
  WpProxy *node;

  /* Make sure the type is correct */
  g_return_if_fail (type == WP_TYPE_ENDPOINT);

  /* Get the Core */
  core = wp_factory_get_core (factory);
  g_return_if_fail (core);

  /* Get the properties */
  if (!g_variant_lookup (properties, "name", "&s", &name))
      return;
  if (!g_variant_lookup (properties, "media-class", "&s", &media_class))
      return;
  if (!g_variant_lookup (properties, "direction", "u", &direction))
      return;
  if (!g_variant_lookup (properties, "proxy-node", "t", &node))
      return;

  g_async_initable_new_async (
      simple_endpoint_get_type (), G_PRIORITY_DEFAULT, NULL, ready, user_data,
      "core", core,
      "name", name,
      "media-class", media_class,
      "direction", direction,
      "proxy-node", node,
      NULL);
}
