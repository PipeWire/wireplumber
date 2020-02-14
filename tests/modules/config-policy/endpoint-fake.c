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

struct _WpFakeEndpoint
{
  WpBaseEndpoint parent;
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

static GAsyncInitableIface *wp_fake_endpoint_parent_interface = NULL;
static void wp_fake_endpoint_async_initable_init (gpointer iface,
    gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (WpFakeEndpoint, wp_fake_endpoint, WP_TYPE_BASE_ENDPOINT,
    G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE,
        wp_fake_endpoint_async_initable_init))

static WpProperties *
wp_fake_endpoint_get_properties (WpBaseEndpoint * ep)
{
  WpFakeEndpoint *self = WP_FAKE_ENDPOINT (ep);
  return wp_properties_ref (self->props);
}

static const char *
wp_fake_endpoint_get_role (WpBaseEndpoint * ep)
{
  WpFakeEndpoint *self = WP_FAKE_ENDPOINT (ep);
  return self->role;
}

static guint32
wp_fake_endpoint_get_global_id (WpBaseEndpoint * ep)
{
  WpFakeEndpoint *self = WP_FAKE_ENDPOINT (ep);
  return self->id;
}

static gboolean
wp_fake_endpoint_prepare_link (WpBaseEndpoint * ep, guint32 stream_id,
    WpBaseEndpointLink * link, GVariant ** properties, GError ** error)
{
  return TRUE;
}

static void
wp_fake_endpoint_begin_fade (WpBaseEndpoint * ep, guint32 stream_id,
    guint duration, gfloat step, guint direction, guint type,
    GCancellable * cancellable, GAsyncReadyCallback callback, gpointer data)
{
  g_autoptr (GTask) task = g_task_new (ep, cancellable, callback, data);
  g_task_return_boolean (task, TRUE);
}

static const char *
wp_fake_endpoint_get_endpoint_link_factory (WpBaseEndpoint * ep)
{
  return WP_FAKE_ENDPOINT_LINK_FACTORY_NAME;
}

static void
wp_fake_endpoint_constructed (GObject * object)
{
  WpFakeEndpoint *self = WP_FAKE_ENDPOINT (object);
  GVariantDict d;

  for (guint i = 0; i < self->streams; i++) {
    g_autofree gchar *name = g_strdup_printf ("%u", i);
    g_variant_dict_init (&d, NULL);
    g_variant_dict_insert (&d, "id", "u", i);
    g_variant_dict_insert (&d, "name", "s", name);
    g_variant_dict_insert (&d, "priority", "u", i);
    wp_base_endpoint_register_stream (WP_BASE_ENDPOINT (self), g_variant_dict_end (&d));
  }

  G_OBJECT_CLASS (wp_fake_endpoint_parent_class)->constructed (object);
}

static void
wp_fake_endpoint_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpFakeEndpoint *self = WP_FAKE_ENDPOINT (object);

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
wp_fake_endpoint_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpFakeEndpoint *self = WP_FAKE_ENDPOINT (object);

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
wp_fake_endpoint_finalize (GObject * object)
{
  WpFakeEndpoint *self = WP_FAKE_ENDPOINT (object);
  g_clear_pointer (&self->props, wp_properties_unref);
  G_OBJECT_CLASS (wp_fake_endpoint_parent_class)->finalize (object);
}

static gboolean
wp_fake_endpoint_finish_creation (gpointer d)
{
  WpFakeEndpoint *self = d;

  g_task_return_boolean (self->init_task, TRUE);
  g_clear_object (&self->init_task);

  return FALSE;
}

static void
wp_fake_endpoint_init_async (GAsyncInitable *initable, int io_priority,
    GCancellable *cancellable, GAsyncReadyCallback callback, gpointer data)
{
  WpFakeEndpoint *self = WP_FAKE_ENDPOINT (initable);

  self->init_task = g_task_new (initable, cancellable, callback, data);

  wp_fake_endpoint_parent_interface->init_async (initable, io_priority,
      cancellable, callback, data);

  g_autoptr (WpCore) core = wp_base_endpoint_get_core (WP_BASE_ENDPOINT(self));
  g_return_if_fail (core);

  wp_core_idle_add (core, NULL, wp_fake_endpoint_finish_creation, self, NULL);
}

static void
wp_fake_endpoint_async_initable_init (gpointer iface, gpointer iface_data)
{
  GAsyncInitableIface *ai_iface = iface;
  wp_fake_endpoint_parent_interface = g_type_interface_peek_parent (iface);
  ai_iface->init_async = wp_fake_endpoint_init_async;
}

static void
wp_fake_endpoint_init (WpFakeEndpoint * self)
{
  static guint id = 0;
  self->id = id++;
}

static void
wp_fake_endpoint_class_init (WpFakeEndpointClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpBaseEndpointClass *endpoint_class = (WpBaseEndpointClass *) klass;

  object_class->constructed = wp_fake_endpoint_constructed;
  object_class->finalize = wp_fake_endpoint_finalize;
  object_class->set_property = wp_fake_endpoint_set_property;
  object_class->get_property = wp_fake_endpoint_get_property;

  endpoint_class->get_properties = wp_fake_endpoint_get_properties;
  endpoint_class->get_role = wp_fake_endpoint_get_role;
  endpoint_class->get_global_id = wp_fake_endpoint_get_global_id;
  endpoint_class->prepare_link = wp_fake_endpoint_prepare_link;
  endpoint_class->begin_fade = wp_fake_endpoint_begin_fade;
  endpoint_class->get_endpoint_link_factory =
      wp_fake_endpoint_get_endpoint_link_factory;

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
wp_fake_endpoint_new_async (WpCore *core, const char *name,
    const char *media_class, guint direction,
    WpProperties *props, const char *role, guint streams,
    GAsyncReadyCallback ready, gpointer data)
{
    g_async_initable_new_async (
        wp_fake_endpoint_get_type (), G_PRIORITY_DEFAULT, NULL, ready, data,
        "core", core,
        "name", name,
        "media-class", media_class,
        "direction", direction,
        "priority", 0,
        "properties", props,
        "role", role,
        "streams", streams,
        NULL);
}
