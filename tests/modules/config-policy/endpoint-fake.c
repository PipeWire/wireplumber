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

struct _WpEndpointFake
{
  WpEndpoint parent;
  GTask *init_task;
  guint id;

  /* Props */
  WpProperties *props;
  char *role;
  guint streams;
};

enum {
  PROP_0,
  PROP_PROPS,
  PROP_ROLE,
  PROP_STREAMS,
};

static GAsyncInitableIface *wp_endpoint_fake_parent_interface = NULL;
static void wp_endpoint_fake_async_initable_init (gpointer iface,
    gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (WpEndpointFake, wp_endpoint_fake, WP_TYPE_ENDPOINT,
    G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE,
        wp_endpoint_fake_async_initable_init))

static WpProperties *
wp_endpoint_fake_get_properties (WpEndpoint * ep)
{
  WpEndpointFake *self = WP_ENDPOINT_FAKE (ep);
  return wp_properties_ref (self->props);
}

static const char *
wp_endpoint_fake_get_role (WpEndpoint * ep)
{
  WpEndpointFake *self = WP_ENDPOINT_FAKE (ep);
  return self->role;
}

static gboolean
wp_endpoint_fake_prepare_link (WpEndpoint * ep, guint32 stream_id,
    WpEndpointLink * link, GVariant ** properties, GError ** error)
{
  return TRUE;
}

static const char *
wp_endpoint_fake_get_endpoint_link_factory (WpEndpoint * ep)
{
  return WP_ENDPOINT_LINK_FAKE_FACTORY_NAME;
}

static void
wp_endpoint_fake_constructed (GObject * object)
{
  WpEndpointFake *self = WP_ENDPOINT_FAKE (object);
  GVariantDict d;

  for (guint i = 0; i < self->streams; i++) {
    g_autofree gchar *name = g_strdup_printf ("%u", i);
    g_variant_dict_init (&d, NULL);
    g_variant_dict_insert (&d, "id", "u", i);
    g_variant_dict_insert (&d, "name", "s", name);
    wp_endpoint_register_stream (WP_ENDPOINT (self), g_variant_dict_end (&d));
  }

  G_OBJECT_CLASS (wp_endpoint_fake_parent_class)->constructed (object);
}

static void
wp_endpoint_fake_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpEndpointFake *self = WP_ENDPOINT_FAKE (object);

  switch (property_id) {
  case PROP_PROPS:
    g_clear_pointer (&self->props, wp_properties_unref);
    self->props = g_value_dup_boxed (value);
    if (!self->props)
      self->props = wp_properties_new_empty ();
    break;
  case PROP_ROLE:
    g_clear_pointer (&self->role, g_free);
    self->role = g_value_dup_string (value);
    break;
  case PROP_STREAMS:
      self->streams = g_value_get_uint (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_endpoint_fake_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpEndpointFake *self = WP_ENDPOINT_FAKE (object);

  switch (property_id) {
  case PROP_PROPS:
    g_value_take_boxed (value, self->props);
    break;
  case PROP_ROLE:
    g_value_set_string (value, self->role);
    break;
  case PROP_STREAMS:
    g_value_set_uint (value, self->streams);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_endpoint_fake_finalize (GObject * object)
{
  WpEndpointFake *self = WP_ENDPOINT_FAKE (object);
  g_clear_pointer (&self->props, wp_properties_unref);
  G_OBJECT_CLASS (wp_endpoint_fake_parent_class)->finalize (object);
}

static void
wp_endpoint_fake_finish_creation (WpCore *core, GAsyncResult *res,
    WpEndpointFake *self)
{
  g_task_return_boolean (self->init_task, TRUE);
  g_clear_object (&self->init_task);
}

static void
wp_endpoint_fake_init_async (GAsyncInitable *initable, int io_priority,
    GCancellable *cancellable, GAsyncReadyCallback callback, gpointer data)
{
  WpEndpointFake *self = WP_ENDPOINT_FAKE (initable);

  self->init_task = g_task_new (initable, cancellable, callback, data);

  wp_endpoint_fake_parent_interface->init_async (initable, io_priority,
      cancellable, callback, data);

  g_autoptr (WpCore) core = wp_endpoint_get_core(WP_ENDPOINT(self));
  if (core)
    wp_core_sync (core, NULL,
        (GAsyncReadyCallback) wp_endpoint_fake_finish_creation, self);
}

static void
wp_endpoint_fake_async_initable_init (gpointer iface, gpointer iface_data)
{
  GAsyncInitableIface *ai_iface = iface;
  wp_endpoint_fake_parent_interface = g_type_interface_peek_parent (iface);
  ai_iface->init_async = wp_endpoint_fake_init_async;
}

static void
wp_endpoint_fake_init (WpEndpointFake * self)
{
  static guint id = 0;
  self->id = id++;
}

static void
wp_endpoint_fake_class_init (WpEndpointFakeClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpEndpointClass *endpoint_class = (WpEndpointClass *) klass;

  object_class->constructed = wp_endpoint_fake_constructed;
  object_class->finalize = wp_endpoint_fake_finalize;
  object_class->set_property = wp_endpoint_fake_set_property;
  object_class->get_property = wp_endpoint_fake_get_property;

  endpoint_class->get_properties = wp_endpoint_fake_get_properties;
  endpoint_class->get_role = wp_endpoint_fake_get_role;
  endpoint_class->prepare_link = wp_endpoint_fake_prepare_link;
  endpoint_class->get_endpoint_link_factory =
      wp_endpoint_fake_get_endpoint_link_factory;

  g_object_class_install_property (object_class, PROP_PROPS,
      g_param_spec_boxed ("properties", "properties",
          "The properties of the fake endpoint", WP_TYPE_PROPERTIES,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_ROLE,
      g_param_spec_string ("role", "role",
          "The role of the fake endpoint", NULL,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_STREAMS,
      g_param_spec_uint ("streams", "streams",
          "The number of streams this endpoint has", 0, G_MAXUINT, 0,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

void
wp_endpoint_fake_new_async (WpCore *core, const char *name,
    const char *media_class, guint direction,
    WpProperties *props, const char *role, guint streams,
    GAsyncReadyCallback ready, gpointer data)
{
    g_async_initable_new_async (
        wp_endpoint_fake_get_type (), G_PRIORITY_DEFAULT, NULL, ready, data,
        "core", core,
        "name", name,
        "media-class", media_class,
        "direction", direction,
        "properties", props,
        "role", role,
        "streams", streams,
        NULL);
}

guint
wp_endpoint_fake_get_id (WpEndpointFake *self)
{
  return self->id;
}
