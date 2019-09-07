/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * The simple endpoint link is an implementation of WpEndpointLink that
 * expects the two linked endpoints to have nodes in the pipewire graph.
 * When asked to create a link, it creates pw_link objects that will link
 * the ports of the source node to the ports of the sink node.
 *
 * The GVariant data that is passed in create must be of type (uau),
 * which means a tuple with the following fields:
 *  - u: a uint32 that is the ID of a node
 *  - au: an array of uint32 that are the IDs of the ports on this node
 *
 * Linking endpoints with multiple nodes is not supported by this implementation.
 */

#include <wp/wp.h>
#include <pipewire/pipewire.h>

struct _WpPipewireSimpleEndpointLink
{
  WpEndpointLink parent;

  /* Props */
  GWeakRef core;
  guint link_count;

  /* The task to signal the simple endpoint link is initialized */
  GTask *init_task;

  /* Handler */
  gulong proxy_done_handler_id;

  /* The link proxies */
  GPtrArray *link_proxies;
};

enum {
  PROP_0,
  PROP_CORE,
};

static GAsyncInitableIface *wp_simple_endpoint_link_parent_interface = NULL;
static void wp_simple_endpoint_link_async_initable_init (gpointer iface,
    gpointer iface_data);

G_DECLARE_FINAL_TYPE (WpPipewireSimpleEndpointLink,
    simple_endpoint_link, WP_PIPEWIRE, SIMPLE_ENDPOINT_LINK, WpEndpointLink)

G_DEFINE_TYPE_WITH_CODE (WpPipewireSimpleEndpointLink, simple_endpoint_link,
    WP_TYPE_ENDPOINT_LINK,
    G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE,
                           wp_simple_endpoint_link_async_initable_init))

static void
wp_simple_endpoint_link_init_async (GAsyncInitable *initable, int io_priority,
    GCancellable *cancellable, GAsyncReadyCallback callback, gpointer data)
{
  WpPipewireSimpleEndpointLink *self =
      WP_PIPEWIRE_SIMPLE_ENDPOINT_LINK (initable);

  /* Create the async task */
  self->init_task = g_task_new (initable, cancellable, callback, data);

  /* Call the parent interface */
  wp_simple_endpoint_link_parent_interface->init_async (initable,
      io_priority, cancellable, callback, data);
}

static void
wp_simple_endpoint_link_async_initable_init (gpointer iface,
    gpointer iface_data)
{
  GAsyncInitableIface *ai_iface = iface;

  /* Set the parent interface */
  wp_simple_endpoint_link_parent_interface =
      g_type_interface_peek_parent (iface);

  /* Only set the init_async */
  ai_iface->init_async = wp_simple_endpoint_link_init_async;
}

static void
simple_endpoint_link_init (WpPipewireSimpleEndpointLink * self)
{
  /* Init the core weak reference */
  g_weak_ref_init (&self->core, NULL);

  /* Init the list of link proxies */
  self->link_proxies = g_ptr_array_new_full(2, (GDestroyNotify)g_object_unref);
}

static void
simple_endpoint_link_finalize (GObject * object)
{
  WpPipewireSimpleEndpointLink *self = WP_PIPEWIRE_SIMPLE_ENDPOINT_LINK(object);

  g_clear_object (&self->init_task);
  g_clear_pointer (&self->link_proxies, g_ptr_array_unref);
  g_weak_ref_clear (&self->core);
}

static void
simple_endpoint_link_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpPipewireSimpleEndpointLink *self =
      WP_PIPEWIRE_SIMPLE_ENDPOINT_LINK (object);

  switch (property_id) {
  case PROP_CORE:
    g_weak_ref_set (&self->core, g_value_get_object (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
simple_endpoint_link_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpPipewireSimpleEndpointLink *self =
      WP_PIPEWIRE_SIMPLE_ENDPOINT_LINK (object);

  switch (property_id) {
  case PROP_CORE:
    g_value_take_object (value, g_weak_ref_get (&self->core));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
on_proxy_link_augmented (WpProxy *proxy, GAsyncResult *res, gpointer data)
{
  WpPipewireSimpleEndpointLink *self = data;
  g_autoptr (GError) error = NULL;

  wp_proxy_augment_finish (proxy, res, &error);
  if (error && self->init_task) {
    g_task_return_error (self->init_task, g_steal_pointer (&error));
    g_clear_object (&self->init_task);
    return;
  }

  /* Finish the simple endpoint link creation if all links have been created */
  if (--self->link_count == 0 && self->init_task) {
    g_task_return_boolean (self->init_task, TRUE);
    g_clear_object(&self->init_task);
  }
}

static gboolean
simple_endpoint_link_create (WpEndpointLink * epl, GVariant * src_data,
    GVariant * sink_data, GError ** error)
{
  WpPipewireSimpleEndpointLink *self = WP_PIPEWIRE_SIMPLE_ENDPOINT_LINK(epl);
  g_autoptr (WpCore) core = NULL;
  guint32 output_node_id, input_node_id;
  GVariant *src_ports, *sink_ports;
  GVariantIter *out_iter, *in_iter;
  guint64 out_ptr, in_ptr;
  g_autoptr (GHashTable) linked_ports = NULL;
  g_autoptr (WpProperties) props = NULL;
  WpProxy *proxy = NULL;

  core = g_weak_ref_get (&self->core);
  g_return_val_if_fail (core, FALSE);

  /* Get the node ids and port ids */
  if (!g_variant_lookup (src_data, "node-id", "u", &output_node_id))
      return FALSE;
  src_ports = g_variant_lookup_value (src_data, "ports", G_VARIANT_TYPE_ARRAY);
  if (!src_ports)
      return FALSE;
  if (!g_variant_lookup (sink_data, "node-id", "u", &input_node_id))
      return FALSE;
  sink_ports = g_variant_lookup_value (sink_data, "ports", G_VARIANT_TYPE_ARRAY);
  if (!sink_ports)
      return FALSE;

  /* Link all the output ports with the input ports */
  linked_ports = g_hash_table_new (g_direct_hash, g_direct_equal);
  g_variant_get (src_ports, "at", &out_iter);
  while (g_variant_iter_loop (out_iter, "t", &out_ptr)) {
    WpProxyPort *out_p = (gpointer)out_ptr;
    enum pw_direction out_direction = wp_proxy_port_get_info(out_p)->direction;
    guint out_id = wp_proxy_get_global_id(WP_PROXY(out_p));
    if (out_direction == PW_DIRECTION_INPUT)
      continue;

    g_variant_get (sink_ports, "at", &in_iter);
    while (g_variant_iter_loop (in_iter, "t", &in_ptr)) {
      WpProxyPort *in_p = (gpointer)in_ptr;
      enum pw_direction in_direction = wp_proxy_port_get_info(in_p)->direction;
      guint in_id = wp_proxy_get_global_id(WP_PROXY(in_p));
      if (in_direction == PW_DIRECTION_OUTPUT)
        continue;

      /* Skip the ports if they are already linked */
      if (g_hash_table_contains (linked_ports, GUINT_TO_POINTER(in_id)) ||
          g_hash_table_contains (linked_ports, GUINT_TO_POINTER(out_id)))
        continue;

      /* Create the properties */
      props = wp_properties_new_empty ();
      wp_properties_setf(props, PW_KEY_LINK_OUTPUT_NODE, "%d", output_node_id);
      wp_properties_setf(props, PW_KEY_LINK_OUTPUT_PORT, "%d", out_id);
      wp_properties_setf(props, PW_KEY_LINK_INPUT_NODE, "%d", input_node_id);
      wp_properties_setf(props, PW_KEY_LINK_INPUT_PORT, "%d", in_id);

      /* Create the link */
      proxy = wp_core_create_remote_object(core, "link-factory",
          PW_TYPE_INTERFACE_Link, PW_VERSION_LINK_PROXY, props);
      g_return_val_if_fail (proxy, FALSE);
      g_ptr_array_add(self->link_proxies, proxy);

      /* Wait for the link to be created on the server side
         by waiting for the info event, which will be signaled anyway */
      self->link_count++;
      wp_proxy_augment (proxy, WP_PROXY_FEATURE_INFO, NULL,
          (GAsyncReadyCallback) on_proxy_link_augmented, self);

      /* Insert the port ids in the hash tables to know they are linked */
      g_hash_table_add (linked_ports, GUINT_TO_POINTER(in_id));
      g_hash_table_add (linked_ports, GUINT_TO_POINTER(out_id));
    }
    g_variant_iter_free (in_iter);
  }
  g_variant_iter_free (out_iter);

  return TRUE;
}

static void
simple_endpoint_link_destroy (WpEndpointLink * epl)
{
  WpPipewireSimpleEndpointLink *self = WP_PIPEWIRE_SIMPLE_ENDPOINT_LINK(epl);

  g_clear_pointer (&self->link_proxies, g_ptr_array_unref);
}

static void
simple_endpoint_link_class_init (WpPipewireSimpleEndpointLinkClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpEndpointLinkClass *link_class = (WpEndpointLinkClass *) klass;

  object_class->finalize = simple_endpoint_link_finalize;
  object_class->set_property = simple_endpoint_link_set_property;
  object_class->get_property = simple_endpoint_link_get_property;

  link_class->create = simple_endpoint_link_create;
  link_class->destroy = simple_endpoint_link_destroy;

  g_object_class_install_property (object_class, PROP_CORE,
      g_param_spec_object ("core", "core",
          "The wireplumber core object this links belongs to", WP_TYPE_CORE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

void
simple_endpoint_link_factory (WpFactory * factory, GType type,
    GVariant * properties, GAsyncReadyCallback ready, gpointer data)
{
  g_autoptr(WpCore) core = NULL;
  guint64 src, sink;
  guint src_stream, sink_stream;

  /* Make sure the type is an endpoint link */
  g_return_if_fail (type == WP_TYPE_ENDPOINT_LINK);

  /* Get the Core */
  core = wp_factory_get_core (factory);
  g_return_if_fail (core);

  /* Get the properties */
  if (!g_variant_lookup (properties, "src", "t", &src))
      return;
  if (!g_variant_lookup (properties, "src-stream", "u", &src_stream))
      return;
  if (!g_variant_lookup (properties, "sink", "t", &sink))
      return;
  if (!g_variant_lookup (properties, "sink-stream", "u", &sink_stream))
      return;

  /* Create the endpoint link */
  g_async_initable_new_async (
      simple_endpoint_link_get_type (), G_PRIORITY_DEFAULT, NULL, ready, data,
      "src", (gpointer)src,
      "src-stream", src_stream,
      "sink", (gpointer)sink,
      "sink-stream", sink_stream,
      "core", core,
      NULL);
}
