/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>

#include "endpoint-audiotestsrc.h"

struct _WpEndpointAudiotestsrc
{
  WpBaseEndpoint parent;
  GTask *init_task;
  guint id;

  /* Props */
  WpProxyNode *proxy_node;
  GVariant *streams;
};

enum {
  PROP_0,
  PROP_PROXY_NODE,
  PROP_STREAMS,
};

static GAsyncInitableIface *wp_endpoint_audiotestsrc_parent_interface = NULL;
static void wp_endpoint_audiotestsrc_async_initable_init (gpointer iface,
    gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (WpEndpointAudiotestsrc, wp_endpoint_audiotestsrc,
    WP_TYPE_BASE_ENDPOINT,
    G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE,
        wp_endpoint_audiotestsrc_async_initable_init))

static WpProperties *
wp_endpoint_audiotestsrc_get_properties (WpBaseEndpoint * ep)
{
  WpEndpointAudiotestsrc *self = WP_ENDPOINT_AUDIOTESTSRC (ep);
  return wp_proxy_get_properties (WP_PROXY (self->proxy_node));
}

static const char *
wp_endpoint_audiotestsrc_get_role (WpBaseEndpoint * ep)
{
  return NULL;
}

static guint32
wp_endpoint_audiotestsrc_get_global_id (WpBaseEndpoint * ep)
{
  WpEndpointAudiotestsrc *self = WP_ENDPOINT_AUDIOTESTSRC (ep);
  return self->id;
}

static gboolean
wp_endpoint_audiotestsrc_prepare_link (WpBaseEndpoint * ep, guint32 stream_id,
    WpBaseEndpointLink * link, GVariant ** properties, GError ** error)
{
  return TRUE;
}

static const char *
wp_endpoint_audiotestsrc_get_endpoint_link_factory (WpBaseEndpoint * ep)
{
  return NULL;
}

static void
wp_endpoint_audiotestsrc_constructed (GObject * object)
{
  WpEndpointAudiotestsrc *self = WP_ENDPOINT_AUDIOTESTSRC (object);
  GVariantDict d;
  GVariantIter iter;
  const gchar *stream;
  guint priority;
  int i;

  if (self->streams) {
    g_variant_iter_init (&iter, self->streams);
    for (i = 0; g_variant_iter_next (&iter, "(&su)", &stream, &priority); i++) {
      g_variant_dict_init (&d, NULL);
      g_variant_dict_insert (&d, "id", "u", i);
      g_variant_dict_insert (&d, "name", "s", stream);
      g_variant_dict_insert (&d, "priority", "u", priority);
      wp_base_endpoint_register_stream (WP_BASE_ENDPOINT (self), g_variant_dict_end (&d));
    }
  }

  G_OBJECT_CLASS (wp_endpoint_audiotestsrc_parent_class)->constructed (object);
}

static void
wp_endpoint_audiotestsrc_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpEndpointAudiotestsrc *self = WP_ENDPOINT_AUDIOTESTSRC (object);

  switch (property_id) {
  case PROP_PROXY_NODE:
    self->proxy_node = g_value_dup_object (value);
    break;
  case PROP_STREAMS:
    self->streams = g_value_dup_variant(value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_endpoint_audiotestsrc_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpEndpointAudiotestsrc *self = WP_ENDPOINT_AUDIOTESTSRC (object);

  switch (property_id) {
  case PROP_PROXY_NODE:
    g_value_set_object (value, self->proxy_node);
    break;
  case PROP_STREAMS:
    g_value_set_variant (value, self->streams);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_endpoint_audiotestsrc_finalize (GObject * object)
{
  WpEndpointAudiotestsrc *self = WP_ENDPOINT_AUDIOTESTSRC (object);

  g_clear_object(&self->proxy_node);
  g_clear_pointer(&self->streams, g_variant_unref);

  G_OBJECT_CLASS (wp_endpoint_audiotestsrc_parent_class)->finalize (object);
}

static void
wp_endpoint_audiotestsrc_finish_creation (WpCore *core, GAsyncResult *res,
    WpEndpointAudiotestsrc *self)
{
  g_task_return_boolean (self->init_task, TRUE);
  g_clear_object (&self->init_task);
}

static void
wp_endpoint_audiotestsrc_init_async (GAsyncInitable *initable, int io_priority,
    GCancellable *cancellable, GAsyncReadyCallback callback, gpointer data)
{
  WpEndpointAudiotestsrc *self = WP_ENDPOINT_AUDIOTESTSRC (initable);

  self->init_task = g_task_new (initable, cancellable, callback, data);

  wp_endpoint_audiotestsrc_parent_interface->init_async (initable, io_priority,
      cancellable, callback, data);

  g_autoptr (WpCore) core = wp_base_endpoint_get_core (WP_BASE_ENDPOINT(self));
  if (core)
    wp_core_sync (core, NULL,
        (GAsyncReadyCallback) wp_endpoint_audiotestsrc_finish_creation, self);
}

static void
wp_endpoint_audiotestsrc_async_initable_init (gpointer iface,
    gpointer iface_data)
{
  GAsyncInitableIface *ai_iface = iface;
  wp_endpoint_audiotestsrc_parent_interface =
      g_type_interface_peek_parent (iface);
  ai_iface->init_async = wp_endpoint_audiotestsrc_init_async;
}

static void
wp_endpoint_audiotestsrc_init (WpEndpointAudiotestsrc * self)
{
  static guint id = 0;
  self->id = id++;
}

static void
wp_endpoint_audiotestsrc_class_init (WpEndpointAudiotestsrcClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpBaseEndpointClass *endpoint_class = (WpBaseEndpointClass *) klass;

  object_class->constructed = wp_endpoint_audiotestsrc_constructed;
  object_class->finalize = wp_endpoint_audiotestsrc_finalize;
  object_class->set_property = wp_endpoint_audiotestsrc_set_property;
  object_class->get_property = wp_endpoint_audiotestsrc_get_property;

  endpoint_class->get_properties = wp_endpoint_audiotestsrc_get_properties;
  endpoint_class->get_role = wp_endpoint_audiotestsrc_get_role;
  endpoint_class->get_global_id = wp_endpoint_audiotestsrc_get_global_id;
  endpoint_class->prepare_link = wp_endpoint_audiotestsrc_prepare_link;
  endpoint_class->get_endpoint_link_factory =
      wp_endpoint_audiotestsrc_get_endpoint_link_factory;

  g_object_class_install_property (object_class, PROP_PROXY_NODE,
      g_param_spec_object ("proxy-node", "proxy-node",
          "The node this endpoint refers to", WP_TYPE_PROXY_NODE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_STREAMS,
      g_param_spec_variant ("streams", "streams",
          "The stream names for the streams to register",
          G_VARIANT_TYPE ("a(su)"), NULL,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

void
wp_endpoint_audiotestsrc_factory (WpFactory * factory, GType type,
    GVariant * properties, GAsyncReadyCallback ready, gpointer data)
{
  g_autoptr (WpCore) core = NULL;
  const gchar *name, *media_class;
  guint direction, priority;
  guint64 node;
  g_autoptr (GVariant) streams = NULL;

  core = wp_factory_get_core(factory);
  g_return_if_fail (core);

  if (!g_variant_lookup (properties, "name", "&s", &name))
      return;
  if (!g_variant_lookup (properties, "media-class", "&s", &media_class))
      return;
  if (!g_variant_lookup (properties, "direction", "u", &direction))
      return;
  if (!g_variant_lookup (properties, "priority", "u", &priority))
      return;
  if (!g_variant_lookup (properties, "proxy-node", "t", &node))
      return;
  streams = g_variant_lookup_value (properties, "streams",
      G_VARIANT_TYPE ("a(su)"));

  g_async_initable_new_async (wp_endpoint_audiotestsrc_get_type (),
        G_PRIORITY_DEFAULT, NULL, ready, data,
        "core", core,
        "name", name,
        "media-class", media_class,
        "direction", direction,
        "priority", priority,
        "proxy-node", (gpointer) node,
        "streams", streams,
        NULL);
}
