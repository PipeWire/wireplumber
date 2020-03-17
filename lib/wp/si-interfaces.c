/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * SECTION: WpSiInterfaces
 * @title: WpSessionItem Interfaces
 */

#include "si-interfaces.h"

/**
 * WpSiEndpoint:
 *
 * An interface for session items that implement a PipeWire endpoint.
 */
G_DEFINE_INTERFACE (WpSiEndpoint, wp_si_endpoint, WP_TYPE_SESSION_ITEM)

static void
wp_si_endpoint_default_init (WpSiEndpointInterface * iface)
{
}

/**
 * wp_si_endpoint_get_name: (virtual get_name)
 * @self: the session item
 *
 * Returns: (transfer none): the name of the endpoint
 */
const gchar *
wp_si_endpoint_get_name (WpSiEndpoint * self)
{
  g_return_val_if_fail (WP_IS_SI_ENDPOINT (self), NULL);
  g_return_val_if_fail (WP_SI_ENDPOINT_GET_IFACE (self)->get_name, NULL);

  return WP_SI_ENDPOINT_GET_IFACE (self)->get_name (self);
}

/**
 * wp_si_endpoint_get_media_class: (virtual get_media_class)
 * @self: the session item
 *
 * Returns: (transfer none): the media class of the endpoint
 */
const gchar *
wp_si_endpoint_get_media_class (WpSiEndpoint * self)
{
  g_return_val_if_fail (WP_IS_SI_ENDPOINT (self), NULL);
  g_return_val_if_fail (WP_SI_ENDPOINT_GET_IFACE (self)->get_media_class, NULL);

  return WP_SI_ENDPOINT_GET_IFACE (self)->get_media_class (self);
}

/**
 * wp_si_endpoint_get_direction: (virtual get_direction)
 * @self: the session item
 *
 * Returns: the direction of the endpoint
 */
WpDirection
wp_si_endpoint_get_direction (WpSiEndpoint * self)
{
  g_return_val_if_fail (WP_IS_SI_ENDPOINT (self), 0);
  g_return_val_if_fail (WP_SI_ENDPOINT_GET_IFACE (self)->get_direction, 0);

  return WP_SI_ENDPOINT_GET_IFACE (self)->get_direction (self);
}

/**
 * wp_si_endpoint_get_priority: (virtual get_priority)
 * @self: the session item
 *
 * Returns: the priority of the endpoint
 */
guint
wp_si_endpoint_get_priority (WpSiEndpoint * self)
{
  g_return_val_if_fail (WP_IS_SI_ENDPOINT (self), 0);
  g_return_val_if_fail (WP_SI_ENDPOINT_GET_IFACE (self)->get_priority, 0);

  return WP_SI_ENDPOINT_GET_IFACE (self)->get_priority (self);
}

/**
 * wp_si_endpoint_get_properties: (virtual get_properties)
 * @self: the session item
 *
 * Returns: (transfer full) (nullable): the properties of the endpoint
 */
WpProperties *
wp_si_endpoint_get_properties (WpSiEndpoint * self)
{
  g_return_val_if_fail (WP_IS_SI_ENDPOINT (self), NULL);
  g_return_val_if_fail (WP_SI_ENDPOINT_GET_IFACE (self)->get_properties, NULL);

  return WP_SI_ENDPOINT_GET_IFACE (self)->get_properties (self);
}

/**
 * wp_si_endpoint_get_n_streams: (virtual get_n_streams)
 * @self: the session item
 *
 * Returns: the number of streams in the endpoint
 */
guint
wp_si_endpoint_get_n_streams (WpSiEndpoint * self)
{
  g_return_val_if_fail (WP_IS_SI_ENDPOINT (self), 0);
  g_return_val_if_fail (WP_SI_ENDPOINT_GET_IFACE (self)->get_n_streams, 0);

  return WP_SI_ENDPOINT_GET_IFACE (self)->get_n_streams (self);
}

/**
 * wp_si_endpoint_get_stream: (virtual get_stream)
 * @self: the session item
 * @index: the stream index, from 0 up to and excluding
 *   wp_si_endpoint_get_n_streams()
 *
 * Returns: (transfer none): the stream at @index
 */
WpSiStream *
wp_si_endpoint_get_stream (WpSiEndpoint * self, guint index)
{
  g_return_val_if_fail (WP_IS_SI_ENDPOINT (self), NULL);
  g_return_val_if_fail (WP_SI_ENDPOINT_GET_IFACE (self)->get_stream, NULL);

  return WP_SI_ENDPOINT_GET_IFACE (self)->get_stream (self, index);
}

/**
 * WpSiMultiEndpoint:
 *
 * An interface for session items that provide multiple PipeWire endpoints.
 *
 * This is useful for items that need to expose more than one endpoints while
 * managing the same nodes underneath. For example, an audio playback device
 * may have one input endpoint for sending audio to the device and one output
 * endpoint for monitoring (exposing the adapter's monitor ports).
 *
 * If an item implements both #WpSiMultiEndpoint and #WpSiEndpoint, then the
 * managing session will only inspect the #WpSiMultiEndpoint interface in
 * order to determine which endpoints to export. Effectively this means that
 * such an item should also include itself in the list of endpoints that
 * it exposes through #WpSiMultiEndpoint in order to be exported to PipeWire.
 */
G_DEFINE_INTERFACE (WpSiMultiEndpoint, wp_si_multi_endpoint, WP_TYPE_SESSION_ITEM)

static void
wp_si_multi_endpoint_default_init (WpSiMultiEndpointInterface * iface)
{
}

/**
 * wp_si_multi_endpoint_get_n_endpoints: (virtual get_n_endpoints)
 * @self: the session item
 *
 * Returns: the number of endpoints exposed by this item
 */
guint
wp_si_multi_endpoint_get_n_endpoints (WpSiMultiEndpoint * self)
{
  g_return_val_if_fail (WP_IS_SI_MULTI_ENDPOINT (self), 0);
  g_return_val_if_fail (WP_SI_MULTI_ENDPOINT_GET_IFACE (self)->get_n_endpoints, 0);

  return WP_SI_MULTI_ENDPOINT_GET_IFACE (self)->get_n_endpoints (self);
}

/**
 * wp_si_multi_endpoint_get_endpoint: (virtual get_endpoint)
 * @self: the session item
 * @index: the endpoint index, from 0 up to and excluding
 *   wp_si_multi_endpoint_get_n_endpoints()
 *
 * Returns: (transfer none): the endpoint at @index
 */
WpSiEndpoint *
wp_si_multi_endpoint_get_endpoint (WpSiMultiEndpoint * self, guint index)
{
  g_return_val_if_fail (WP_IS_SI_MULTI_ENDPOINT (self), NULL);
  g_return_val_if_fail (WP_SI_MULTI_ENDPOINT_GET_IFACE (self)->get_endpoint, NULL);

  return WP_SI_MULTI_ENDPOINT_GET_IFACE (self)->get_endpoint (self, index);
}

/**
 * WpSiStream:
 *
 * An interface for session items that provide a PipeWire endpoint stream.
 */
G_DEFINE_INTERFACE (WpSiStream, wp_si_stream, WP_TYPE_SESSION_ITEM)

static void
wp_si_stream_default_init (WpSiStreamInterface * iface)
{
}

/**
 * wp_si_stream_get_name: (virtual get_name)
 * @self: the session item
 *
 * Returns: (transfer none): the name of the stream
 */
const gchar *
wp_si_stream_get_name (WpSiStream * self)
{
  g_return_val_if_fail (WP_IS_SI_STREAM (self), NULL);
  g_return_val_if_fail (WP_SI_STREAM_GET_IFACE (self)->get_name, NULL);

  return WP_SI_STREAM_GET_IFACE (self)->get_name (self);
}

/**
 * wp_si_stream_get_properties: (virtual get_properties)
 * @self: the session item
 *
 * Returns: (transfer full) (nullable): the properties of the stream
 */
WpProperties *
wp_si_stream_get_properties (WpSiStream * self)
{
  g_return_val_if_fail (WP_IS_SI_STREAM (self), NULL);
  g_return_val_if_fail (WP_SI_STREAM_GET_IFACE (self)->get_properties, NULL);

  return WP_SI_STREAM_GET_IFACE (self)->get_properties (self);
}

/**
 * wp_si_stream_get_parent_endpoint: (virtual get_parent_endpoint)
 * @self: the session item
 *
 * Returns: (transfer none): the endpoint that this stream belongs to
 */
WpSiEndpoint *
wp_si_stream_get_parent_endpoint (WpSiStream * self)
{
  g_return_val_if_fail (WP_IS_SI_STREAM (self), NULL);
  g_return_val_if_fail (WP_SI_STREAM_GET_IFACE (self)->get_parent_endpoint, NULL);

  return WP_SI_STREAM_GET_IFACE (self)->get_parent_endpoint (self);
}

/**
 * WpSiLink:
 *
 * An interface for session items that provide a PipeWire endpoint link.
 */
G_DEFINE_INTERFACE (WpSiLink, wp_si_link, WP_TYPE_SESSION_ITEM)

static void
wp_si_link_default_init (WpSiLinkInterface * iface)
{
}

/**
 * wp_si_link_get_out_stream: (virtual get_out_stream)
 * @self: the session item
 *
 * Returns: (transfer none): the output stream that is linked by this link
 */
WpSiStream *
wp_si_link_get_out_stream (WpSiLink * self)
{
  g_return_val_if_fail (WP_IS_SI_LINK (self), NULL);
  g_return_val_if_fail (WP_SI_LINK_GET_IFACE (self)->get_out_stream, NULL);

  return WP_SI_LINK_GET_IFACE (self)->get_out_stream (self);
}

/**
 * wp_si_link_get_in_stream: (virtual get_in_stream)
 * @self: the session item
 *
 * Returns: (transfer none): the input stream that is linked by this link
 */
WpSiStream *
wp_si_link_get_in_stream (WpSiLink * self)
{
  g_return_val_if_fail (WP_IS_SI_LINK (self), NULL);
  g_return_val_if_fail (WP_SI_LINK_GET_IFACE (self)->get_in_stream, NULL);

  return WP_SI_LINK_GET_IFACE (self)->get_in_stream (self);
}
