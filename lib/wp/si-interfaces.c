/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * SECTION: si-interfaces
 * @title: Session Item Interfaces
 */

#define G_LOG_DOMAIN "wp-si-interfaces"

#include "si-interfaces.h"
#include "wpenums.h"

/**
 * WpSiEndpoint:
 *
 * An interface for session items that implement a PipeWire endpoint.
 */
G_DEFINE_INTERFACE (WpSiEndpoint, wp_si_endpoint, WP_TYPE_SESSION_ITEM)

static WpProperties *
wp_si_endpoint_default_get_properties (WpSiEndpoint * self)
{
  return NULL;
}

static void
wp_si_endpoint_default_init (WpSiEndpointInterface * iface)
{
  iface->get_properties = wp_si_endpoint_default_get_properties;

  g_signal_new ("endpoint-properties-changed", G_TYPE_FROM_INTERFACE (iface),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

/**
 * wp_si_endpoint_get_registration_info: (virtual get_registration_info)
 * @self: the session item
 *
 * This should return information that is used for registering the endpoint,
 * as a GVariant tuple of type (ssya{ss}) that contains, in order:
 *  - s: the endpoint's name
 *  - s: the media class
 *  - y: the direction
 *  - a{ss}: additional properties to be added to the list of global properties
 *
 * Returns: (transfer full): registration info for the endpoint
 */
GVariant *
wp_si_endpoint_get_registration_info (WpSiEndpoint * self)
{
  g_return_val_if_fail (WP_IS_SI_ENDPOINT (self), NULL);
  g_return_val_if_fail (WP_SI_ENDPOINT_GET_IFACE (self)->get_registration_info, NULL);

  return WP_SI_ENDPOINT_GET_IFACE (self)->get_registration_info (self);
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
 * WpSiPortInfo:
 *
 * An interface for retrieving PipeWire port information from a session item.
 * This information is used to create links in the nodes graph.
 */
G_DEFINE_INTERFACE (WpSiPortInfo, wp_si_port_info, WP_TYPE_SESSION_ITEM)

static WpSiAcquisition *
wp_si_port_info_default_get_acquisition (WpSiPortInfo * self)
{
  return NULL;
}

static void
wp_si_port_info_default_init (WpSiPortInfoInterface * iface)
{
  iface->get_acquisition = wp_si_port_info_default_get_acquisition;
}

/**
 * wp_si_port_info_get_ports: (virtual get_ports)
 * @self: the session item
 * @context: (nullable): an optional context for the ports
 *
 * This method returns a variant of type "a(uuu)", where each tuple in the
 * array contains the following information:
 *   - u: (guint32) node id
 *   - u: (guint32) port id (the port must belong on the node specified above)
 *   - u: (guint32) the audio channel (enum spa_audio_channel) that this port
 *        makes available, or 0 for non-audio content
 *
 * The order in which ports appear in this array is important when no channel
 * information is available. The link implementation should link the ports
 * in the order they appear. This is normally a good enough substitute for
 * channel matching.
 *
 * The @context argument can be used to get different sets of ports from
 * the item. The following well-known contexts are defined:
 *   - %NULL: get the standard ports to be linked
 *   - "monitor": get the monitor ports
 *   - "control": get the control port
 *   - "reverse": get the reverse direction ports, if this item controls a
 *                filter node, which would have ports on both directions
 *
 * Contexts other than %NULL may only be used internally to ease the
 * implementation of more complex item relationships. For example, a
 * #WpSessionItem that is in control of an input (sink) adapter node may
 * implement #WpSiPortInfo where the %NULL context will return the standard
 * input ports and the "monitor" context will return the adapter's monitor
 * ports. When linking this item to another item, the %NULL context
 * will always be used, but the item may internally spawn a secondary
 * #WpSessionItem that implements the "monitor" item. That secondary
 * item may implement #WpSiPortInfo, chaining calls to the #WpSiPortInfo
 * of the original item using the "monitor" context. This way, the monitor
 * #WpSessionItem does not need to share control of the underlying node; it
 * only proxies calls to satisfy the API.
 *
 * Returns: (transfer full): a #GVariant containing information about the
 *   ports of this item
 */
GVariant *
wp_si_port_info_get_ports (WpSiPortInfo * self, const gchar * context)
{
  g_return_val_if_fail (WP_IS_SI_PORT_INFO (self), NULL);
  g_return_val_if_fail (WP_SI_PORT_INFO_GET_IFACE (self)->get_ports, NULL);

  return WP_SI_PORT_INFO_GET_IFACE (self)->get_ports (self, context);
}

/**
 * wp_si_port_info_get_acquisition: (virtual get_acquisition)
 * @self: the session item
 *
 * Returns: (transfer none) (nullable): the acquisition interface associated
 *   with this item, or %NULL if this item does not require acquiring items
 *   before linking them
 */
WpSiAcquisition *
wp_si_port_info_get_acquisition (WpSiPortInfo * self)
{
  g_return_val_if_fail (WP_IS_SI_PORT_INFO (self), NULL);
  g_return_val_if_fail (
      WP_SI_PORT_INFO_GET_IFACE (self)->get_acquisition, NULL);

  return WP_SI_PORT_INFO_GET_IFACE (self)->get_acquisition (self);
}

/**
 * WpSiLink:
 *
 * An interface for session items that provide a PipeWire endpoint link.
 */
G_DEFINE_INTERFACE (WpSiLink, wp_si_link, WP_TYPE_SESSION_ITEM)

static WpProperties *
wp_si_link_default_get_properties (WpSiLink * self)
{
  return NULL;
}

static void
wp_si_link_default_init (WpSiLinkInterface * iface)
{
  iface->get_properties = wp_si_link_default_get_properties;

  g_signal_new ("link-properties-changed", G_TYPE_FROM_INTERFACE (iface),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

/**
 * wp_si_link_get_registration_info: (virtual get_registration_info)
 * @self: the session item
 *
 * This should return information that is used for registering the link,
 * as a GVariant of type a{ss} that contains additional properties to be
 * added to the list of global properties
 *
 * Returns: (transfer full): registration info for the link
 */
GVariant *
wp_si_link_get_registration_info (WpSiLink * self)
{
  g_return_val_if_fail (WP_IS_SI_LINK (self), NULL);
  g_return_val_if_fail (WP_SI_LINK_GET_IFACE (self)->get_registration_info, NULL);

  return WP_SI_LINK_GET_IFACE (self)->get_registration_info (self);
}

/**
 * wp_si_link_get_properties: (virtual get_properties)
 * @self: the session item
 *
 * Returns: (transfer full) (nullable): the properties of the link
 */
WpProperties *
wp_si_link_get_properties (WpSiLink * self)
{
  g_return_val_if_fail (WP_IS_SI_LINK (self), NULL);
  g_return_val_if_fail (WP_SI_LINK_GET_IFACE (self)->get_properties, NULL);

  return WP_SI_LINK_GET_IFACE (self)->get_properties (self);
}

/**
 * wp_si_link_get_out_item: (virtual get_out_item)
 * @self: the session item
 *
 * Returns: (transfer none): the output item that is linked by this link
 */
WpSiPortInfo *
wp_si_link_get_out_item (WpSiLink * self)
{
  g_return_val_if_fail (WP_IS_SI_LINK (self), NULL);
  g_return_val_if_fail (WP_SI_LINK_GET_IFACE (self)->get_out_item, NULL);

  return WP_SI_LINK_GET_IFACE (self)->get_out_item (self);
}

/**
 * wp_si_link_get_in_item: (virtual get_in_item)
 * @self: the session item
 *
 * Returns: (transfer none): the input item that is linked by this link
 */
WpSiPortInfo *
wp_si_link_get_in_item (WpSiLink * self)
{
  g_return_val_if_fail (WP_IS_SI_LINK (self), NULL);
  g_return_val_if_fail (WP_SI_LINK_GET_IFACE (self)->get_in_item, NULL);

  return WP_SI_LINK_GET_IFACE (self)->get_in_item (self);
}

/**
 * WpSiAcquisition:
 *
 * This interface provides a way to request an item for linking before doing
 * so. This allows item implementations to apply internal policy rules.
 *
 * A #WpSiAcquisition is associated directly with a #WpSiPortInfo via
 * wp_si_port_info_get_acquisition(). In order to allow switching policies, it
 * is recommended that port info implementations use a separate
 * session item to implement this interface and allow replacing it.
 */
G_DEFINE_INTERFACE (WpSiAcquisition, wp_si_acquisition, WP_TYPE_SESSION_ITEM)

static void
wp_si_acquisition_default_init (WpSiAcquisitionInterface * iface)
{
}

/**
 * wp_si_acquisition_acquire: (virtual acquire)
 * @self: the session item
 * @acquisitor: the link that is trying to acquire a port info item
 * @item: the item that is being acquired
 * @callback: (scope async): the callback to call when the operation is done
 * @data: (closure): user data for @callback
 *
 * Acquires the @item for linking by @acquisitor.
 *
 * When a link is not allowed by policy, this operation should return
 * an error.
 *
 * When a link needs to be delayed for a short amount of time (ex. to apply
 * a fade out effect on another item), this operation should finish with a
 * delay. It is safe to assume that after this operation completes,
 * the item will be linked immediately.
 */
void
wp_si_acquisition_acquire (WpSiAcquisition * self, WpSiLink * acquisitor,
    WpSiPortInfo * item, GAsyncReadyCallback callback, gpointer data)
{
  g_return_if_fail (WP_IS_SI_ACQUISITION (self));
  g_return_if_fail (WP_SI_ACQUISITION_GET_IFACE (self)->acquire);

  WP_SI_ACQUISITION_GET_IFACE (self)->acquire (self, acquisitor, item, callback,
      data);
}

/**
 * wp_si_acquisition_acquire_finish: (virtual acquire_finish)
 * @self: the session item
 * @res: the async result
 * @error: (out) (optional): the operation's error, if it occurred
 *
 * Finishes the operation started by wp_si_acquisition_acquire().
 * This is meant to be called in the callback that was passed to that method.
 *
 * Returns: %TRUE on success, %FALSE if there was an error
 */
gboolean
wp_si_acquisition_acquire_finish (WpSiAcquisition * self, GAsyncResult * res,
    GError ** error)
{
  g_return_val_if_fail (WP_IS_SI_ACQUISITION (self), FALSE);
  g_return_val_if_fail (WP_SI_ACQUISITION_GET_IFACE (self)->acquire_finish,
      FALSE);

  return WP_SI_ACQUISITION_GET_IFACE (self)->acquire_finish (self, res, error);
}

/**
 * wp_si_acquisition_release: (virtual release)
 * @self: the session item
 * @acquisitor: the link that had previously acquired the item
 * @item: the port info item that is being released
 *
 * Releases the @item, which means that it is being unlinked.
 */
void
wp_si_acquisition_release (WpSiAcquisition * self, WpSiLink * acquisitor,
    WpSiPortInfo * item)
{
  g_return_if_fail (WP_IS_SI_ACQUISITION (self));
  g_return_if_fail (WP_SI_ACQUISITION_GET_IFACE (self)->release);

  WP_SI_ACQUISITION_GET_IFACE (self)->release (self, acquisitor, item);
}
