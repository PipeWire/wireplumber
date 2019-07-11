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

  /* The global-id this endpoint refers to */
  guint global_id;

  gchar *role;

  /* The task to signal the endpoint is initialized */
  GTask *init_task;
  gboolean init_abort;

  /* The remote pipewire */
  WpRemotePipewire *remote_pipewire;

  /* Handler */
  gulong proxy_node_done_handler_id;

  /* Direction */
  enum pw_direction direction;

  /* Proxies */
  WpProxyNode *proxy_node;
  struct spa_hook node_proxy_listener;
  GPtrArray *proxies_port;

  /* controls cache */
  gfloat volume;
  gboolean mute;
};

enum {
  PROP_0,
  PROP_GLOBAL_ID,
  PROP_ROLE,
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

typedef GObject* (*WpObjectNewFinishFunc)(GObject *initable, GAsyncResult *res,
    GError **error);

static GObject *
object_safe_new_finish(WpPipewireSimpleEndpoint * self, GObject *initable,
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
    g_warning ("WpPipewireSimpleEndpoint:%p Aborting construction", self);
    self->init_abort = TRUE;
    g_task_return_error (self->init_task, error);
    g_clear_object (&self->init_task);
    return NULL;
  }

  return object;
}

static void
node_proxy_param (void *object, int seq, uint32_t id,
    uint32_t index, uint32_t next, const struct spa_pod *param)
{
  WpPipewireSimpleEndpoint *self = WP_PIPEWIRE_SIMPLE_ENDPOINT (object);

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

static const struct pw_node_proxy_events node_node_proxy_events = {
  PW_VERSION_NODE_PROXY_EVENTS,
  .param = node_proxy_param,
};

static void
on_all_ports_done(WpProxy *proxy, gpointer data)
{
  WpPipewireSimpleEndpoint *self = data;

  /* Don't do anything if the endpoint has already been initialized */
  if (!self->init_task)
    return;

  /* Finish the creation of the endpoint */
  g_task_return_boolean (self->init_task, TRUE);
  g_clear_object(&self->init_task);
}

static void
on_proxy_port_created(GObject *initable, GAsyncResult *res, gpointer data)
{
  WpPipewireSimpleEndpoint *self = data;
  WpProxyPort *proxy_port = NULL;

  /* Get the proxy port */
  proxy_port = WP_PROXY_PORT (object_safe_new_finish (self, initable, res,
      (WpObjectNewFinishFunc)wp_proxy_port_new_finish));
  if (!proxy_port)
    return;

  /* Add the proxy port to the array */
  g_return_if_fail (self->proxies_port);
  g_ptr_array_add(self->proxies_port, proxy_port);

  /* Register the done callback */
  if (!self->proxy_node_done_handler_id) {
    self->proxy_node_done_handler_id = g_signal_connect_object(self->proxy_node,
        "done", (GCallback)on_all_ports_done, self, 0);
    wp_proxy_sync (WP_PROXY(self->proxy_node));
  }
}

static void
on_port_added(WpRemotePipewire *rp, guint id, guint parent_id, gconstpointer p,
    gpointer d)
{
  WpPipewireSimpleEndpoint *self = d;
  struct pw_port_proxy *port_proxy = NULL;

  /* Don't do anything if we are aborting */
  if (self->init_abort)
    return;

  /* Only handle ports owned by this endpoint */
  if (parent_id != self->global_id)
    return;

  /* Create the proxy port async */
  port_proxy = wp_remote_pipewire_proxy_bind (self->remote_pipewire, id,
    PW_TYPE_INTERFACE_Port);
  g_return_if_fail(port_proxy);
  wp_proxy_port_new(id, port_proxy, on_proxy_port_created, self);
}

static void
emit_endpoint_ports(WpPipewireSimpleEndpoint *self)
{
  struct pw_node_proxy* node_proxy = NULL;
  struct spa_audio_info_raw format = { 0, };
  struct spa_pod *param;
  struct spa_pod_builder pod_builder = { 0, };
  char buf[1024];

  /* Get the pipewire node proxy */
  node_proxy = wp_proxy_get_pw_proxy(WP_PROXY(self->proxy_node));
  g_return_if_fail (node_proxy);

  /* TODO: Assume all clients have this format for now */
  format.format = SPA_AUDIO_FORMAT_F32P;
  format.flags = 1;
  format.rate = 48000;
  format.channels = 2;
  format.position[0] = 0;
  format.position[1] = 0;

  /* Build the param profile */
  spa_pod_builder_init(&pod_builder, buf, sizeof(buf));
  param = spa_format_audio_raw_build(&pod_builder, SPA_PARAM_Format, &format);
  param = spa_pod_builder_add_object(&pod_builder,
      SPA_TYPE_OBJECT_ParamProfile, SPA_PARAM_Profile,
      SPA_PARAM_PROFILE_direction,  SPA_POD_Id(self->direction),
      SPA_PARAM_PROFILE_format,     SPA_POD_Pod(param));

  /* Set the param profile to emit the ports */
  pw_node_proxy_set_param(node_proxy, SPA_PARAM_Profile, 0, param);
}

static void
on_proxy_node_created(GObject *initable, GAsyncResult *res, gpointer data)
{
  WpPipewireSimpleEndpoint *self = data;
  GVariantDict d;
  uint32_t ids[1] = { SPA_PARAM_Props };
  uint32_t n_ids = 1;
  struct pw_node_proxy *node_proxy = NULL;

  /* Get the proxy node */
  self->proxy_node = WP_PROXY_NODE (object_safe_new_finish (self, initable,
      res, (WpObjectNewFinishFunc)wp_proxy_node_new_finish));
  if (!self->proxy_node)
    return;

  self->role = g_strdup (spa_dict_lookup (
          wp_proxy_node_get_info (self->proxy_node)->props, "media.role"));

  /* Emit the ports */
  emit_endpoint_ports(self);

  /* Add a custom node proxy event listener */
  node_proxy = wp_proxy_get_pw_proxy(WP_PROXY(self->proxy_node));
  g_return_if_fail (node_proxy);
  pw_node_proxy_add_listener (node_proxy, &self->node_proxy_listener,
      &node_node_proxy_events, self);
  pw_node_proxy_subscribe_params (node_proxy, ids, n_ids);

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
  const gchar *media_class = wp_endpoint_get_media_class (WP_ENDPOINT (self));
  struct pw_node_proxy *node_proxy = NULL;

  /* Create the async task */
  self->init_task = g_task_new (initable, cancellable, callback, data);

  /* Init the proxies_port array */
  self->proxies_port = g_ptr_array_new_full(2, (GDestroyNotify)g_object_unref);

  /* Set the direction */
  if (g_str_has_prefix (media_class, "Stream/Input"))
    self->direction = PW_DIRECTION_INPUT;
  else if (g_str_has_prefix (media_class, "Stream/Output"))
    self->direction = PW_DIRECTION_OUTPUT;
  else
    g_critical ("failed to parse direction");

  /* Register a port_added callback */
  self->remote_pipewire = wp_core_get_global (core, WP_GLOBAL_REMOTE_PIPEWIRE);
  g_return_if_fail(self->remote_pipewire);
  g_signal_connect_object(self->remote_pipewire, "global-added::port",
    (GCallback)on_port_added, self, 0);

  /* Create the proxy node async */
  node_proxy = wp_remote_pipewire_proxy_bind (self->remote_pipewire,
      self->global_id, PW_TYPE_INTERFACE_Node);
  g_return_if_fail(node_proxy);
  wp_proxy_node_new(self->global_id, node_proxy, on_proxy_node_created, self);

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
  self->init_abort = FALSE;
}

static void
simple_endpoint_finalize (GObject * object)
{
  WpPipewireSimpleEndpoint *self = WP_PIPEWIRE_SIMPLE_ENDPOINT (object);

  /* Destroy the proxies port */
  if (self->proxies_port) {
    g_ptr_array_free(self->proxies_port, TRUE);
    self->proxies_port = NULL;
  }

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
  case PROP_GLOBAL_ID:
    self->global_id = g_value_get_uint(value);
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
simple_endpoint_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpPipewireSimpleEndpoint *self = WP_PIPEWIRE_SIMPLE_ENDPOINT (object);

  switch (property_id) {
  case PROP_GLOBAL_ID:
    g_value_set_uint (value, self->global_id);
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
  g_variant_builder_add (&b, "{sv}", "node-id",
      g_variant_new_uint32 (self->global_id));
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
  struct pw_node_proxy *node_proxy = NULL;

  /* Get the node proxy */
  node_proxy = wp_proxy_get_pw_proxy(WP_PROXY(self->proxy_node));

  switch (control_id) {
    case CONTROL_VOLUME:
      volume = g_variant_get_double (value);

      g_debug("WpEndpoint:%p set volume control (%u) value, vol:%f", self,
          control_id, volume);

      pw_node_proxy_set_param (node_proxy,
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

      pw_node_proxy_set_param (node_proxy,
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

  g_object_class_install_property (object_class, PROP_GLOBAL_ID,
      g_param_spec_uint ("global-id", "global-id",
          "The global Id this endpoint refers to", 0, G_MAXUINT, 0,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_ROLE,
      g_param_spec_string ("role", "role", "The role of the wrapped node", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

void
simple_endpoint_factory (WpFactory * factory, GType type,
    GVariant * properties, GAsyncReadyCallback ready, gpointer user_data)
{
  g_autoptr (WpCore) core = NULL;
  const gchar *name, *media_class;
  guint global_id;

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
  if (!g_variant_lookup (properties, "global-id", "u", &global_id))
      return;

  g_async_initable_new_async (
      simple_endpoint_get_type (), G_PRIORITY_DEFAULT, NULL, ready, user_data,
      "core", core,
      "name", name,
      "media-class", media_class,
      "global-id", global_id,
      NULL);
}
