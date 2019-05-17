/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "endpoint.h"
#include "error.h"
#include "factory.h"

typedef struct _WpEndpointPrivate WpEndpointPrivate;
struct _WpEndpointPrivate
{
  gchar *name;
  gchar media_class[40];
  guint32 active_profile;
  GPtrArray *links;
};

enum {
  PROP_0,
  PROP_NAME,
  PROP_MEDIA_CLASS,
};

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (WpEndpoint, wp_endpoint, G_TYPE_OBJECT)

static void
wp_endpoint_init (WpEndpoint * self)
{
  WpEndpointPrivate *priv = wp_endpoint_get_instance_private (self);

  priv->links = g_ptr_array_new_with_free_func (g_object_unref);
}

static void
wp_endpoint_dispose (GObject * object)
{
  WpEndpointPrivate *priv =
      wp_endpoint_get_instance_private (WP_ENDPOINT (object));
  gint i;

  /* wp_endpoint_link_destroy removes elements from the array,
   * so traversing in reverse order is faster and less complicated */
  for (i = priv->links->len - 1; i >= 0; i--) {
    wp_endpoint_link_destroy (g_ptr_array_index (priv->links, i));
  }

  G_OBJECT_CLASS (wp_endpoint_parent_class)->dispose (object);
}

static void
wp_endpoint_finalize (GObject * object)
{
  WpEndpointPrivate *priv =
      wp_endpoint_get_instance_private (WP_ENDPOINT (object));

  g_ptr_array_unref (priv->links);

  G_OBJECT_CLASS (wp_endpoint_parent_class)->finalize (object);
}

static void
wp_endpoint_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpEndpointPrivate *priv =
      wp_endpoint_get_instance_private (WP_ENDPOINT (object));

  switch (property_id) {
  case PROP_NAME:
    priv->name = g_value_dup_string (value);
    break;
  case PROP_MEDIA_CLASS:
    strncpy (priv->media_class, g_value_get_string (value),
        sizeof (priv->media_class) - 1);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_endpoint_get_property (GObject * object, guint property_id, GValue * value,
    GParamSpec * pspec)
{
  WpEndpointPrivate *priv =
      wp_endpoint_get_instance_private (WP_ENDPOINT (object));

  switch (property_id) {
  case PROP_NAME:
    g_value_set_string (value, priv->name);
    break;
  case PROP_MEDIA_CLASS:
    g_value_set_string (value, priv->media_class);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static guint32
default_get_streams_or_profiles_count (WpEndpoint * self)
{
  return 1;
}

static const gchar *
default_get_stream_or_profile_name (WpEndpoint * self, guint32 id)
{
  return (id == 0) ? "default" : NULL;
}

static gboolean
default_activate_profile (WpEndpoint * self, guint32 profile_id,
    GError ** error)
{
  WpEndpointPrivate *priv = wp_endpoint_get_instance_private (self);

  if (profile_id >= wp_endpoint_get_profiles_count (self)) {
    g_set_error (error, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVALID_ARGUMENT,
        "profile id %d does not exist", profile_id);
    return FALSE;
  }

  priv->active_profile = profile_id;
  return TRUE;
}

static void
wp_endpoint_class_init (WpEndpointClass * klass)
{
  GObjectClass * object_class = (GObjectClass *) klass;

  object_class->dispose = wp_endpoint_dispose;
  object_class->finalize = wp_endpoint_finalize;
  object_class->get_property = wp_endpoint_get_property;
  object_class->set_property = wp_endpoint_set_property;

  klass->get_streams_count = default_get_streams_or_profiles_count;
  klass->get_stream_name = default_get_stream_or_profile_name;
  klass->get_profiles_count = default_get_streams_or_profiles_count;
  klass->get_profile_name = default_get_stream_or_profile_name;
  klass->activate_profile = default_activate_profile;

  g_object_class_install_property (object_class, PROP_NAME,
      g_param_spec_string ("name", "name", "The name of the endpoint", NULL,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_MEDIA_CLASS,
      g_param_spec_string ("media-class", "media-class",
          "The media class of the endpoint", NULL,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
}

const gchar *
wp_endpoint_get_name (WpEndpoint * self)
{
  WpEndpointPrivate *priv;

  g_return_val_if_fail (WP_IS_ENDPOINT (self), NULL);

  priv = wp_endpoint_get_instance_private (self);
  return priv->name;
}

const gchar *
wp_endpoint_get_media_class (WpEndpoint * self)
{
  WpEndpointPrivate *priv;

  g_return_val_if_fail (WP_IS_ENDPOINT (self), NULL);

  priv = wp_endpoint_get_instance_private (self);
  return priv->media_class;
}

guint32
wp_endpoint_get_streams_count (WpEndpoint * self)
{
  g_return_val_if_fail (WP_IS_ENDPOINT (self), 0);
  g_return_val_if_fail (WP_ENDPOINT_GET_CLASS (self)->get_streams_count, 0);

  return WP_ENDPOINT_GET_CLASS (self)->get_streams_count (self);
}

const gchar *
wp_endpoint_get_stream_name (WpEndpoint * self, guint32 stream_id)
{
  g_return_val_if_fail (WP_IS_ENDPOINT (self), NULL);
  g_return_val_if_fail (WP_ENDPOINT_GET_CLASS (self)->get_stream_name, NULL);

  return WP_ENDPOINT_GET_CLASS (self)->get_stream_name (self, stream_id);
}

guint32
wp_endpoint_get_profiles_count (WpEndpoint * self)
{
  g_return_val_if_fail (WP_IS_ENDPOINT (self), 0);
  g_return_val_if_fail (WP_ENDPOINT_GET_CLASS (self)->get_profiles_count, 0);

  return WP_ENDPOINT_GET_CLASS (self)->get_profiles_count (self);
}

const gchar *
wp_endpoint_get_profile_name (WpEndpoint * self, guint32 profile_id)
{
  g_return_val_if_fail (WP_IS_ENDPOINT (self), NULL);
  g_return_val_if_fail (WP_ENDPOINT_GET_CLASS (self)->get_profile_name, NULL);

  return WP_ENDPOINT_GET_CLASS (self)->get_profile_name (self, profile_id);
}

guint32
wp_endpoint_get_active_profile (WpEndpoint * self)
{
  WpEndpointPrivate *priv;

  g_return_val_if_fail (WP_IS_ENDPOINT (self), 0);

  priv = wp_endpoint_get_instance_private (self);
  return priv->active_profile;
}

gboolean
wp_endpoint_activate_profile (WpEndpoint * self, guint32 profile_id,
    GError ** error)
{
  g_return_val_if_fail (WP_IS_ENDPOINT (self), FALSE);
  g_return_val_if_fail (WP_ENDPOINT_GET_CLASS (self)->activate_profile, FALSE);

  return WP_ENDPOINT_GET_CLASS (self)->activate_profile (self, profile_id,
      error);
}

gboolean
wp_endpoint_is_linked (WpEndpoint * self)
{
  WpEndpointPrivate *priv;

  g_return_val_if_fail (WP_IS_ENDPOINT (self), FALSE);

  priv = wp_endpoint_get_instance_private (self);
  return (priv->links->len > 0);
}

GPtrArray *
wp_endpoint_get_links (WpEndpoint * self)
{
  WpEndpointPrivate *priv;

  g_return_val_if_fail (WP_IS_ENDPOINT (self), NULL);

  priv = wp_endpoint_get_instance_private (self);
  return priv->links;
}


typedef struct _WpEndpointLinkPrivate WpEndpointLinkPrivate;
struct _WpEndpointLinkPrivate
{
  WpEndpoint *src;
  guint32 src_stream;
  WpEndpoint *sink;
  guint32 sink_stream;
};

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (WpEndpointLink, wp_endpoint_link, G_TYPE_OBJECT)

static void
wp_endpoint_link_init (WpEndpointLink * self)
{
}

static void
wp_endpoint_link_class_init (WpEndpointLinkClass * klass)
{
}

void
wp_endpoint_link_set_endpoints (WpEndpointLink * self, WpEndpoint * src,
    guint32 src_stream, WpEndpoint * sink, guint32 sink_stream)
{
  WpEndpointLinkPrivate *priv;

  g_return_if_fail (WP_IS_ENDPOINT_LINK (self));
  g_return_if_fail (WP_IS_ENDPOINT (src));
  g_return_if_fail (WP_IS_ENDPOINT (sink));

  priv = wp_endpoint_link_get_instance_private (self);
  priv->src = src;
  priv->src_stream = src_stream;
  priv->sink = sink;
  priv->sink_stream = sink_stream;
}

WpEndpoint *
wp_endpoint_link_get_source_endpoint (WpEndpointLink * self)
{
  WpEndpointLinkPrivate *priv;

  g_return_val_if_fail (WP_IS_ENDPOINT_LINK (self), NULL);

  priv = wp_endpoint_link_get_instance_private (self);
  return priv->src;
}

guint32
wp_endpoint_link_get_source_stream (WpEndpointLink * self)
{
  WpEndpointLinkPrivate *priv;

  g_return_val_if_fail (WP_IS_ENDPOINT_LINK (self), 0);

  priv = wp_endpoint_link_get_instance_private (self);
  return priv->src_stream;
}

WpEndpoint *
wp_endpoint_link_get_sink_endpoint (WpEndpointLink * self)
{
  WpEndpointLinkPrivate *priv;

  g_return_val_if_fail (WP_IS_ENDPOINT_LINK (self), NULL);

  priv = wp_endpoint_link_get_instance_private (self);
  return priv->sink;
}

guint32
wp_endpoint_link_get_sink_stream (WpEndpointLink * self)
{
  WpEndpointLinkPrivate *priv;

  g_return_val_if_fail (WP_IS_ENDPOINT_LINK (self), 0);

  priv = wp_endpoint_link_get_instance_private (self);
  return priv->sink_stream;
}

WpEndpointLink * wp_endpoint_link_new (WpCore * core, WpEndpoint * src,
    guint32 src_stream, WpEndpoint * sink, guint32 sink_stream, GError ** error)
{
  g_autoptr (WpEndpointLink) link = NULL;
  g_autoptr (GVariant) src_props = NULL;
  g_autoptr (GVariant) sink_props = NULL;
  const gchar *src_factory = NULL, *sink_factory = NULL;
  WpEndpointPrivate *endpoint_priv;

  g_return_val_if_fail (WP_IS_ENDPOINT (src), NULL);
  g_return_val_if_fail (WP_IS_ENDPOINT (sink), NULL);
  g_return_val_if_fail (WP_ENDPOINT_GET_CLASS (src)->prepare_link, NULL);
  g_return_val_if_fail (WP_ENDPOINT_GET_CLASS (sink)->prepare_link, NULL);

  /* find the factory */

  if (WP_ENDPOINT_GET_CLASS (src)->get_endpoint_link_factory)
    src_factory = WP_ENDPOINT_GET_CLASS (src)->get_endpoint_link_factory (src);
  if (WP_ENDPOINT_GET_CLASS (sink)->get_endpoint_link_factory)
    sink_factory = WP_ENDPOINT_GET_CLASS (sink)->get_endpoint_link_factory (sink);

  if (src_factory || sink_factory) {
    if (src_factory && sink_factory && strcmp (src_factory, sink_factory) != 0) {
      g_set_error (error, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVALID_ARGUMENT,
          "It is not possible to link endpoints that both specify different "
          "custom link factories");
      return NULL;
    } else if (sink_factory)
      src_factory = sink_factory;
  } else {
    src_factory = "pipewire-simple-endpoint-link";
  }

  /* create link object */

  link = wp_core_make_from_factory (core, src_factory, WP_TYPE_ENDPOINT_LINK,
      NULL);
  if (!link) {
    g_set_error (error, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_OPERATION_FAILED,
        "Failed to create link object from factory %s", src_factory);
    return NULL;
  }
  g_return_val_if_fail (WP_ENDPOINT_LINK_GET_CLASS (link)->create, NULL);

  /* prepare the link */

  wp_endpoint_link_set_endpoints (link, src, src_stream, sink, sink_stream);

  if (!WP_ENDPOINT_GET_CLASS (src)->prepare_link (src, src_stream, link,
          &src_props, error))
    return NULL;
  if (!WP_ENDPOINT_GET_CLASS (src)->prepare_link (sink, sink_stream, link,
          &sink_props, error))
    return NULL;

  /* create the link */

  if (!WP_ENDPOINT_LINK_GET_CLASS (link)->create (link, src_props, sink_props,
          error))
    return NULL;

  /* register the link on the endpoints */

  endpoint_priv = wp_endpoint_get_instance_private (src);
  g_ptr_array_add (endpoint_priv->links, g_object_ref (link));

  endpoint_priv = wp_endpoint_get_instance_private (sink);
  g_ptr_array_add (endpoint_priv->links, g_object_ref (link));

  return link;
}

void
wp_endpoint_link_destroy (WpEndpointLink * self)
{
  WpEndpointLinkPrivate *priv;
  WpEndpointPrivate *endpoint_priv;

  g_return_if_fail (WP_IS_ENDPOINT_LINK (self));
  g_return_if_fail (WP_ENDPOINT_LINK_GET_CLASS (self)->destroy);

  priv = wp_endpoint_link_get_instance_private (self);

  WP_ENDPOINT_LINK_GET_CLASS (self)->destroy (self);
  if (WP_ENDPOINT_GET_CLASS (priv->src)->release_link)
    WP_ENDPOINT_GET_CLASS (priv->src)->release_link (priv->src, self);
  if (WP_ENDPOINT_GET_CLASS (priv->sink)->release_link)
    WP_ENDPOINT_GET_CLASS (priv->sink)->release_link (priv->sink, self);

  endpoint_priv = wp_endpoint_get_instance_private (priv->src);
  g_ptr_array_remove_fast (endpoint_priv->links, self);

  endpoint_priv = wp_endpoint_get_instance_private (priv->sink);
  g_ptr_array_remove_fast (endpoint_priv->links, self);
}
