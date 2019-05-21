/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/**
 * The simple endpoint is a WpEndpoint implementation that represents
 * all ports of a single direction of a single pipewire node.
 * It can be used to create an Endpoint for a client node or for any
 * other arbitrary node that does not need any kind of internal management.
 */

#include <wp/wp.h>
#include <pipewire/pipewire.h>

struct _WpPipewireSimpleEndpoint
{
  WpEndpoint parent;
};

G_DECLARE_FINAL_TYPE (WpPipewireSimpleEndpoint,
    simple_endpoint, WP_PIPEWIRE, SIMPLE_ENDPOINT, WpEndpoint)

G_DEFINE_TYPE (WpPipewireSimpleEndpoint, simple_endpoint, WP_TYPE_ENDPOINT)

static void
simple_endpoint_init (WpPipewireSimpleEndpoint * self)
{
  GVariantBuilder b;

  g_variant_builder_init (&b, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&b, "{sv}", "id", g_variant_new_uint32 (0));
  g_variant_builder_add (&b, "{sv}", "name", g_variant_new_string ("default"));
  wp_endpoint_register_stream (WP_ENDPOINT (self), g_variant_builder_end (&b));
}

static gboolean
simple_endpoint_prepare_link (WpEndpoint * self, guint32 stream_id,
    WpEndpointLink * link, GVariant ** properties, GError ** error)
{
  /* TODO: verify that the remote end supports the same media type */
  /* TODO: fill @properties with (node id, array(port ids)) */
}

static void
simple_endpoint_class_init (WpPipewireSimpleEndpointClass * klass)
{
  WpEndpointClass *endpoint_class = (WpEndpointClass *) klass;

  endpoint_class->prepare_link = simple_endpoint_prepare_link;
}

gpointer
simple_endpoint_factory (WpFactory * factory, GType type,
    GVariant * properties)
{
  if (type != WP_TYPE_ENDPOINT)
    return NULL;

  /* TODO: retrieve pw_node* from @properties and keep it
   * TODO: populate media_class and name on the endpoint
   * TODO: potentially choose between subclasses of SimpleEndpoint
   *  in order to add interfaces (volume, color balance, etc)
   */
  return g_object_new (simple_endpoint_get_type (), NULL);
}
