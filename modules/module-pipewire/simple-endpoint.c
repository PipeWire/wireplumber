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
  struct pw_node_proxy *node;
  struct spa_hook proxy_listener;
};

enum {
  PROP_0,
  PROP_NODE_PROXY,
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

static void
simple_endpoint_finalize (GObject * object)
{
  WpPipewireSimpleEndpoint *self = WP_PIPEWIRE_SIMPLE_ENDPOINT (object);

  if (self->node) {
    spa_hook_remove (&self->proxy_listener);
    pw_proxy_destroy ((struct pw_proxy *) self->node);
  }

  G_OBJECT_CLASS (simple_endpoint_parent_class)->finalize (object);
}

static void
simple_endpoint_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpPipewireSimpleEndpoint *self = WP_PIPEWIRE_SIMPLE_ENDPOINT (object);

  switch (property_id) {
  case PROP_NODE_PROXY:
    self->node = g_value_get_pointer (value);
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
  case PROP_NODE_PROXY:
    g_value_set_pointer (value, self->node);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static gboolean
simple_endpoint_prepare_link (WpEndpoint * self, guint32 stream_id,
    WpEndpointLink * link, GVariant ** properties, GError ** error)
{
  /* TODO: verify that the remote end supports the same media type */
  /* TODO: fill @properties with (node id, array(port ids)) */

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

  g_object_class_install_property (object_class, PROP_NODE_PROXY,
      g_param_spec_pointer ("node-proxy", "node-proxy",
          "Pointer to the pw_node_proxy* to wrap",
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

static void
node_proxy_destroy (void *data)
{
  WpPipewireSimpleEndpoint *self = WP_PIPEWIRE_SIMPLE_ENDPOINT (data);
  self->node = NULL;
  wp_endpoint_unregister (WP_ENDPOINT (self));
}

static const struct pw_proxy_events node_proxy_events = {
  PW_VERSION_PROXY_EVENTS,
  .destroy = node_proxy_destroy,
};

gpointer
simple_endpoint_factory (WpFactory * factory, GType type,
    GVariant * properties)
{
  WpPipewireSimpleEndpoint *ep;
  guint64 proxy;
  const gchar *name;
  const gchar *media_class;

  g_return_val_if_fail (type == WP_TYPE_ENDPOINT, NULL);
  g_return_val_if_fail (properties != NULL, NULL);
  g_return_val_if_fail (g_variant_is_of_type (properties,
          G_VARIANT_TYPE_VARDICT), NULL);

  if (!g_variant_lookup (properties, "name", "&s", &name))
      return NULL;
  if (!g_variant_lookup (properties, "media-class", "&s", &media_class))
      return NULL;
  if (!g_variant_lookup (properties, "node-proxy", "t", &proxy))
      return NULL;

  ep = g_object_new (simple_endpoint_get_type (),
      "name", name,
      "media-class", media_class,
      "node-proxy", (gpointer) proxy,
      NULL);

  pw_proxy_add_listener ((gpointer) proxy, &ep->proxy_listener,
      &node_proxy_events, ep);

  return ep;
}
