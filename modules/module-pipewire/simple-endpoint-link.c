/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
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
simple_endpoint_link_create (WpEndpointLink * self, GVariant * src_data,
    GVariant * sink_data, GError ** error)
{
  /* TODO create pw_links based on the nodes & ports described in src/sink_data */
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
  if (type != WP_TYPE_ENDPOINT_LINK)
    return NULL;
  return g_object_new (simple_endpoint_link_get_type (), NULL);
}
