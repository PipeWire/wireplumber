/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>
#include <pipewire/pipewire.h>

struct _WpVideoEndpoint
{
  WpBaseEndpoint parent;

  /* Properties */
  WpNode *node;
  char *role;

  /* The task to signal the endpoint is initialized */
  GTask *init_task;

  GVariantBuilder port_vb;
  WpObjectManager *ports_om;
};

enum {
  PROP_0,
  PROP_PROXY_NODE,
  PROP_ROLE,
};

static GAsyncInitableIface *async_initable_parent_interface = NULL;
static void wp_video_endpoint_async_initable_init (gpointer iface,
    gpointer iface_data);

G_DECLARE_FINAL_TYPE (WpVideoEndpoint, wp_video_endpoint, WP, VIDEO_ENDPOINT,
    WpBaseEndpoint)

G_DEFINE_TYPE_WITH_CODE (WpVideoEndpoint, wp_video_endpoint,
    WP_TYPE_BASE_ENDPOINT, G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE,
      wp_video_endpoint_async_initable_init))

static WpProperties *
wp_video_endpoint_get_properties (WpBaseEndpoint * ep)
{
  WpVideoEndpoint *self = WP_VIDEO_ENDPOINT (ep);

  return wp_proxy_get_properties (WP_PROXY (self->node));
}

static const char *
wp_video_endpoint_get_role (WpBaseEndpoint *ep)
{
  WpVideoEndpoint *self = WP_VIDEO_ENDPOINT (ep);

  return self->role;
}

static guint32
wp_video_endpoint_get_global_id (WpBaseEndpoint *ep)
{
  WpVideoEndpoint *self = WP_VIDEO_ENDPOINT (ep);

  return wp_proxy_get_bound_id (WP_PROXY (self->node));
}

static void
port_proxies_foreach_func (gpointer data, gpointer user_data)
{
  WpProxy *port = WP_PROXY (data);
  WpVideoEndpoint *self = WP_VIDEO_ENDPOINT (user_data);
  const struct pw_node_info *node_info;
  const struct pw_port_info *port_info;

  node_info = wp_proxy_get_info (WP_PROXY (self->node));
  g_return_if_fail (node_info);

  port_info = wp_proxy_get_info (port);
  g_return_if_fail (port_info);

  /* tuple format:
      uint32 node_id;
      uint32 port_id;
      uint32 channel;  // always 0 for video
      uint8 direction; // enum spa_direction
   */
  g_variant_builder_add (&self->port_vb, "(uuuy)", node_info->id,
    port_info->id, 0, (guint8) port_info->direction);
}

static gboolean
wp_video_endpoint_prepare_link (WpBaseEndpoint * ep, guint32 stream_id,
    WpBaseEndpointLink * link, GVariant ** properties, GError ** error)
{
  WpVideoEndpoint *self = WP_VIDEO_ENDPOINT (ep);
  g_autoptr (GPtrArray) port_proxies = wp_object_manager_get_objects (
      self->ports_om, 0);

  /* Create a variant array with all the ports */
  g_variant_builder_init (&self->port_vb, G_VARIANT_TYPE ("a(uuuy)"));
  g_ptr_array_foreach (port_proxies, port_proxies_foreach_func, self);
  *properties = g_variant_builder_end (&self->port_vb);

  return TRUE;
}

static void
wp_video_endpoint_begin_fade (WpBaseEndpoint * ep, guint32 stream_id,
    guint duration, gfloat step, guint direction, guint type,
    GCancellable * cancellable, GAsyncReadyCallback callback, gpointer data)
{
  /* TODO: apply a video fade effect */
  g_autoptr (GTask) task = g_task_new (ep, cancellable, callback, data);
  g_task_return_boolean (task, TRUE);
}

static void
wp_video_endpoint_finalize (GObject * object)
{
  WpVideoEndpoint *self = WP_VIDEO_ENDPOINT (object);

  g_clear_object (&self->init_task);

  /* Destroy the done task */
  g_clear_object (&self->init_task);

  /* Props */
  g_clear_object (&self->node);
  g_free (self->role);

  G_OBJECT_CLASS (wp_video_endpoint_parent_class)->finalize (object);
}

static void
wp_video_endpoint_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpVideoEndpoint *self = WP_VIDEO_ENDPOINT (object);

  switch (property_id) {
  case PROP_PROXY_NODE:
    self->node = g_value_dup_object (value);
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
wp_video_endpoint_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpVideoEndpoint *self = WP_VIDEO_ENDPOINT (object);

  switch (property_id) {
  case PROP_PROXY_NODE:
    g_value_set_object (value, self->node);
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
wp_video_endpoint_init_task_finish (WpVideoEndpoint * self, GError * err)
{
  g_autoptr (GError) error = err;

  if (!self->init_task)
    return;

  if (error)
    g_task_return_error (self->init_task, g_steal_pointer (&error));
  else
    g_task_return_boolean (self->init_task, TRUE);

  g_clear_object (&self->init_task);
}

static void
on_ports_changed (WpObjectManager *om, WpVideoEndpoint *self)
{
  wp_video_endpoint_init_task_finish (self, NULL);
  g_signal_handlers_disconnect_by_func (self->ports_om, on_ports_changed, self);
}

static void
on_node_proxy_augmented (WpProxy * proxy, GAsyncResult * res,
    WpVideoEndpoint * self)
{
  g_autoptr (WpCore) core = wp_base_endpoint_get_core (WP_BASE_ENDPOINT(self));
  guint32 id;
  g_autofree gchar *id_str = NULL;
  GVariantBuilder b;

  /* Get the bound id */
  id = wp_proxy_get_bound_id (proxy);
  id_str = g_strdup_printf ("%u", id);

  /* Create the ports object manager and set the port constrains */
  self->ports_om = wp_object_manager_new ();
  g_variant_builder_init (&b, G_VARIANT_TYPE ("aa{sv}"));
  g_variant_builder_open (&b, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&b, "{sv}", "type",
      g_variant_new_int32 (WP_OBJECT_MANAGER_CONSTRAINT_PW_GLOBAL_PROPERTY));
  g_variant_builder_add (&b, "{sv}", "name",
      g_variant_new_string (PW_KEY_NODE_ID));
  g_variant_builder_add (&b, "{sv}", "value",
      g_variant_new_string (id_str));
  g_variant_builder_close (&b);
  wp_object_manager_add_interest (self->ports_om, WP_TYPE_PORT,
      g_variant_builder_end (&b),
      WP_PROXY_FEATURE_PW_PROXY | WP_PROXY_FEATURE_INFO);

  /* Add a callback to know when ports have been changed */
  g_signal_connect (self->ports_om, "objects-changed",
      (GCallback) on_ports_changed, self);

  /* Install the object manager */
  wp_core_install_object_manager (core, self->ports_om);
}

static void
wp_video_endpoint_init_async (GAsyncInitable *initable, int io_priority,
    GCancellable *cancellable, GAsyncReadyCallback callback, gpointer data)
{
  WpVideoEndpoint *self = WP_VIDEO_ENDPOINT (initable);

  /* Create the async task */
  self->init_task = g_task_new (initable, cancellable, callback, data);

  /* Augment the proxy */
  wp_proxy_augment (WP_PROXY (self->node), WP_PROXY_FEATURES_STANDARD,
      cancellable, (GAsyncReadyCallback) on_node_proxy_augmented, self);


  /* Call the parent interface */
  async_initable_parent_interface->init_async (initable, io_priority, cancellable,
      callback, data);
}

static void
wp_video_endpoint_async_initable_init (gpointer iface, gpointer iface_data)
{
  GAsyncInitableIface *ai_iface = iface;

  /* Set the parent interface */
  async_initable_parent_interface = g_type_interface_peek_parent (iface);

  /* Only set the init_async */
  ai_iface->init_async = wp_video_endpoint_init_async;
}

static void
wp_video_endpoint_init (WpVideoEndpoint * self)
{
}

static void
wp_video_endpoint_class_init (WpVideoEndpointClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpBaseEndpointClass *base_endpoint_class = (WpBaseEndpointClass *) klass;

  object_class->finalize = wp_video_endpoint_finalize;
  object_class->set_property = wp_video_endpoint_set_property;
  object_class->get_property = wp_video_endpoint_get_property;

  base_endpoint_class->get_properties = wp_video_endpoint_get_properties;
  base_endpoint_class->get_role = wp_video_endpoint_get_role;
  base_endpoint_class->get_global_id = wp_video_endpoint_get_global_id;
  base_endpoint_class->prepare_link = wp_video_endpoint_prepare_link;
  base_endpoint_class->begin_fade = wp_video_endpoint_begin_fade;

  /* Instal the properties */
  g_object_class_install_property (object_class, PROP_PROXY_NODE,
      g_param_spec_object ("node", "node",
          "The node this endpoint refers to", WP_TYPE_NODE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_ROLE,
      g_param_spec_string ("role", "role", "The role of the wrapped node", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

void
wp_video_endpoint_factory (WpFactory * factory, GType type,
    GVariant * properties, GAsyncReadyCallback ready, gpointer user_data)
{
  g_autoptr (WpCore) core = NULL;
  const gchar *name, *media_class;
  guint direction, priority;
  guint64 node;

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

  /* Create and return the video endpoint object */
  g_async_initable_new_async (
      wp_video_endpoint_get_type (), G_PRIORITY_DEFAULT, NULL, ready, user_data,
      "core", core,
      "name", name,
      "media-class", media_class,
      "direction", direction,
      "priority", priority,
      "node", (gpointer) node,
      NULL);
}
