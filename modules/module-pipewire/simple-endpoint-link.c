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

  /* The core proxy */
  struct pw_core_proxy *core_proxy;
};

G_DECLARE_FINAL_TYPE (WpPipewireSimpleEndpointLink,
    simple_endpoint_link, WP_PIPEWIRE, SIMPLE_ENDPOINT_LINK, WpEndpointLink)

G_DEFINE_TYPE (WpPipewireSimpleEndpointLink,
    simple_endpoint_link, WP_TYPE_ENDPOINT_LINK)

static void
simple_endpoint_link_init (WpPipewireSimpleEndpointLink * self)
{
}

static gboolean
simple_endpoint_link_create (WpEndpointLink * epl, GVariant * src_data,
    GVariant * sink_data, GError ** error)
{
  WpPipewireSimpleEndpointLink *self = WP_PIPEWIRE_SIMPLE_ENDPOINT_LINK(epl);
  struct pw_properties *props;
  guint32 output_node_id, input_node_id, output_port_id, input_port_id;

  /* Get the node ids and port ids */
  if (!g_variant_lookup (src_data, "node-id", "u", &output_node_id))
      return FALSE;
  if (!g_variant_lookup (src_data, "node-port-id", "u", &output_port_id))
      return FALSE;
  if (!g_variant_lookup (sink_data, "node-id", "u", &input_node_id))
      return FALSE;
  if (!g_variant_lookup (sink_data, "node-port-id", "u", &input_port_id))
      return FALSE;

  /* Create the properties */
  props = pw_properties_new(NULL, NULL);
  pw_properties_setf(props, PW_LINK_OUTPUT_NODE_ID, "%d", output_node_id);
  pw_properties_setf(props, PW_LINK_OUTPUT_PORT_ID, "%d", output_port_id);
  pw_properties_setf(props, PW_LINK_INPUT_NODE_ID, "%d", input_node_id);
  pw_properties_setf(props, PW_LINK_INPUT_PORT_ID, "%d", input_port_id);

  /* Create the link */
  pw_core_proxy_create_object(self->core_proxy, "link-factory",
      PW_TYPE_INTERFACE_Link, PW_VERSION_LINK, &props->dict, 0);

  /* Clean up */
  pw_properties_free(props);

  return TRUE;
}

static void
simple_endpoint_link_destroy (WpEndpointLink * self)
{
  /* TODO destroy pw_links */
}

static void
simple_endpoint_link_class_init (WpPipewireSimpleEndpointLinkClass * klass)
{
  WpEndpointLinkClass *link_class = (WpEndpointLinkClass *) klass;

  link_class->create = simple_endpoint_link_create;
  link_class->destroy = simple_endpoint_link_destroy;
}

gpointer
simple_endpoint_link_factory (WpFactory * factory, GType type,
    GVariant * properties)
{
  WpCore *wp_core = NULL;
  struct pw_remote *remote;

  /* Make sure the type is an endpoint link */
  if (type != WP_TYPE_ENDPOINT_LINK)
    return NULL;

  /* Get the WirePlumber core */
  wp_core = wp_factory_get_core(factory);
  if (!wp_core) {
    g_warning("failed to get wireplumbe core. Skipping...");
    return NULL;
  }

  /* Get the remote */
  remote = wp_core_get_global(wp_core, WP_GLOBAL_PW_REMOTE);
  if (!remote) {
    g_warning("failed to get core remote. Skipping...");
    return NULL;
  }

  /* Create the endpoint link */
  WpPipewireSimpleEndpointLink *epl = g_object_new (
      simple_endpoint_link_get_type (), NULL);

  /* Set the core proxy */
  epl->core_proxy = pw_remote_get_core_proxy(remote);
  if (!epl->core_proxy) {
    g_warning("failed to get core proxy. Skipping...");
    return NULL;
  }

  return epl;
}
