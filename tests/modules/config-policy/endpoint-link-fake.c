/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>

#include "endpoint-fake.h"
#include "endpoint-link-fake.h"

struct _WpEndpointLinkFake
{
  WpEndpointLink parent;
  GTask *init_task;
  guint id;

  /* Props */
  GWeakRef core;
};

enum {
  PROP_0,
  PROP_CORE
};

static GAsyncInitableIface *wp_endpoint_link_fake_parent_interface = NULL;
static void wp_endpoint_link_fake_async_initable_init (gpointer iface,
    gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (WpEndpointLinkFake, wp_endpoint_link_fake,
    WP_TYPE_ENDPOINT_LINK,
    G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE,
                           wp_endpoint_link_fake_async_initable_init))

static gboolean
wp_endpoint_link_fake_create (WpEndpointLink * epl, GVariant * src_data,
    GVariant * sink_data, GError ** error)
{
  return TRUE;
}

static void
wp_endpoint_link_fake_destroy (WpEndpointLink * epl)
{
}

static void
wp_endpoint_link_fake_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpEndpointLinkFake *self = WP_ENDPOINT_LINK_FAKE (object);

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
wp_endpoint_link_fake_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpEndpointLinkFake *self = WP_ENDPOINT_LINK_FAKE (object);

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
wp_endpoint_link_fake_finalize (GObject * object)
{
  WpEndpointLinkFake *self = WP_ENDPOINT_LINK_FAKE (object);
  g_clear_object (&self->init_task);
  g_weak_ref_clear (&self->core);
  G_OBJECT_CLASS (wp_endpoint_link_fake_parent_class)->finalize (object);
}

static void
wp_endpoint_link_fake_finish_creation (WpCore *core, GAsyncResult *res,
    WpEndpointLinkFake *self)
{
  g_task_return_boolean (self->init_task, TRUE);
  g_clear_object (&self->init_task);
}

static void
wp_endpoint_link_fake_init_async (GAsyncInitable *initable, int io_priority,
    GCancellable *cancellable, GAsyncReadyCallback callback, gpointer data)
{
  WpEndpointLinkFake *self = WP_ENDPOINT_LINK_FAKE (initable);

  self->init_task = g_task_new (initable, cancellable, callback, data);

  wp_endpoint_link_fake_parent_interface->init_async (initable,
      io_priority, cancellable, callback, data);

  g_autoptr (WpCore) core = g_weak_ref_get (&self->core);
  if (core)
    wp_core_sync (core, NULL,
        (GAsyncReadyCallback) wp_endpoint_link_fake_finish_creation, self);
}

static void
wp_endpoint_link_fake_async_initable_init (gpointer iface,
    gpointer iface_data)
{
  GAsyncInitableIface *ai_iface = iface;
  wp_endpoint_link_fake_parent_interface = g_type_interface_peek_parent (iface);
  ai_iface->init_async = wp_endpoint_link_fake_init_async;
}

static void
wp_endpoint_link_fake_init (WpEndpointLinkFake * self)
{
  static guint id = 0;
  self->id = id++;
}

static void
wp_endpoint_link_fake_class_init (WpEndpointLinkFakeClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpEndpointLinkClass *link_class = (WpEndpointLinkClass *) klass;

  object_class->finalize = wp_endpoint_link_fake_finalize;
  object_class->get_property = wp_endpoint_link_fake_get_property;
  object_class->set_property = wp_endpoint_link_fake_set_property;

  link_class->create = wp_endpoint_link_fake_create;
  link_class->destroy = wp_endpoint_link_fake_destroy;

  g_object_class_install_property (object_class, PROP_CORE,
      g_param_spec_object ("core", "core", "The wireplumber core",
          WP_TYPE_CORE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

void
wp_endpoint_link_fake_factory (WpFactory * factory, GType type,
    GVariant * properties, GAsyncReadyCallback ready, gpointer data)
{
  g_autoptr (WpCore) core = NULL;
  guint64 src, sink;
  guint src_stream, sink_stream;
  gboolean keep;

  /* Get the Core */
  core = wp_factory_get_core(factory);
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
  if (!g_variant_lookup (properties, "keep", "b", &keep))
      return;

  /* Create the endpoint link */
  g_async_initable_new_async (
      wp_endpoint_link_fake_get_type (), G_PRIORITY_DEFAULT, NULL, ready, data,
      "src", (gpointer)src,
      "src-stream", src_stream,
      "sink", (gpointer)sink,
      "sink-stream", sink_stream,
      "keep", keep,
      "core", core,
      NULL);
}

guint
wp_endpoint_link_fake_get_id (WpEndpointLinkFake *self)
{
  return self->id;
}
