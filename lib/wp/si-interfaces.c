/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */
/*!
 * @file si-interfaces.c
 */

/*!
 * @section si_interfaces_section Session Item Interfaces
 *
 * @struct WpSiAcquisition
 * @section si_acquisition_section WpSiAcquisition
 *
 * @brief This interface provides a way to request an item for linking before doing
 * so. This allows item implementations to apply internal policy rules.
 *
 * A [WpSiAcquisition](@ref si_acquisition_section) is associated directly
 * with a [WpSiLinkable](@ref si_linkable_section) via
 * wp_si_linkable_get_acquisition(). In order to allow switching
 * policies, it is recommended that port info implementations use a separate
 * session item to implement this interface and allow replacing it.
 *
 * @struct WpSiEndpoint
 * @section si_endpoint_section WpSiEndpoint
 *
 * @brief An interface for session items that implement a PipeWire endpoint.
 *
 * @section si_endpoint_signals_section Signals
 *
 * @b endpoint-properties-changed
 *
 * @code
 * endpoint_properties_changed_callback (WpSiEndpoint * self,
 *                                       gpointer user_data)
 * @endcode
 *
 * Parameters:
 *
 * @arg `self` - the session
 * @arg `user_data`
 *
 * Flags: Run Last
 *
 * @struct WpSiLink
 * @section si_link_section WpSiLink
 *
 * @brief An interface for session items that provide a PipeWire endpoint link.
 *
 * @section si_link_signals_section Signals
 *
 * @b link-properties-changed
 *
 * @code
 * link_properties_changed_callback (WpSiLink * self,
 *                                   gpointer user_data)
 * @endcode
 *
 * Parameters:
 *
 * @arg `self` - the session
 * @arg `user_data`
 *
 * Flags: Run Last
 *
 * WpSiLinkable:
 *
 * @struct WpSiLinkable
 * @section si_linkable_section WpSiLinkable
 *
 * @brief An interface for retrieving PipeWire port information from a session item.
 * This information is used to create links in the nodes graph.
 *
 * This is normally implemented by the same session items that implement
 * [WpSiEndpoint](@ref si_endpoint_section). The standard link implementation expects to be able to cast
 * a [WpSiEndpoint](@ref si_endpoint_section) into a [WpSiLinkable](@ref si_linkable_section).
 *
 */

#define G_LOG_DOMAIN "wp-si-interfaces"

#include "si-interfaces.h"
#include "wpenums.h"

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

/*!
 * @memberof WpSiEndpoint
 * @param self: the session item
 *
 * @brief This should return information that is used for registering the endpoint,
 * as a GVariant tuple of type (ssya{ss}) that contains, in order:
 *  - s: the endpoint's name
 *  - s: the media class
 *  - y: the direction
 *  - a{ss}: additional properties to be added to the list of global properties
 *
 * @returns (transfer full): registration info for the endpoint
 */

GVariant *
wp_si_endpoint_get_registration_info (WpSiEndpoint * self)
{
  g_return_val_if_fail (WP_IS_SI_ENDPOINT (self), NULL);
  g_return_val_if_fail (WP_SI_ENDPOINT_GET_IFACE (self)->get_registration_info, NULL);

  return WP_SI_ENDPOINT_GET_IFACE (self)->get_registration_info (self);
}

/*!
 * @memberof WpSiEndpoint
 * @param self: the session item
 *
 * @returns (transfer full) (nullable): the properties of the endpoint
 */

WpProperties *
wp_si_endpoint_get_properties (WpSiEndpoint * self)
{
  g_return_val_if_fail (WP_IS_SI_ENDPOINT (self), NULL);
  g_return_val_if_fail (WP_SI_ENDPOINT_GET_IFACE (self)->get_properties, NULL);

  return WP_SI_ENDPOINT_GET_IFACE (self)->get_properties (self);
}


G_DEFINE_INTERFACE (WpSiAdapter, wp_si_adapter, WP_TYPE_SESSION_ITEM)

static void
wp_si_adapter_default_init (WpSiAdapterInterface * iface)
{
}

/**
 * wp_si_adapter_get_ports_format: (virtual get_ports_format)
 * @self: the session item
 * @mode: (out) (nullable): the mode
 *
 * Returns: (transfer full): The format used to configure the ports of the
 * adapter session item. Some items automatically choose a format when being
 * activated, others never set a format on activation and the user needs to
 * manually set it externally with wp_si_adapter_set_ports_format().
 */
WpSpaPod *
wp_si_adapter_get_ports_format (WpSiAdapter * self, const gchar **mode)
{
  g_return_val_if_fail (WP_IS_SI_ADAPTER (self), NULL);
  g_return_val_if_fail (WP_SI_ADAPTER_GET_IFACE (self)->get_ports_format, NULL);

  return WP_SI_ADAPTER_GET_IFACE (self)->get_ports_format (self, mode);
}

/**
 * wp_si_adapter_set_ports_format: (virtual set_ports_format)
 * @self: the session item
 * @format: (transfer full) (nullable): the format to be set
 * @mode (nullable): the mode
 * @callback: (scope async): the callback to call when the operation is done
 * @data: (closure): user data for @callback
 *
 * Sets the format and configures the adapter session item ports using the
 * given format. The result of the operation can be checked using the
 * wp_si_adapter_set_ports_format_finish() API. If format is NULL, the adapter
 * will be configured with the default format. If mode is NULL, the adapter
 * will use "dsp" mode.
 */
void
wp_si_adapter_set_ports_format (WpSiAdapter * self, WpSpaPod *format,
    const gchar *mode, GAsyncReadyCallback callback, gpointer data)
{
  g_return_if_fail (WP_IS_SI_ADAPTER (self));
  g_return_if_fail (WP_SI_ADAPTER_GET_IFACE (self)->set_ports_format);

  WP_SI_ADAPTER_GET_IFACE (self)->set_ports_format (self, format, mode,
      callback, data);
}

/**
 * wp_si_adapter_set_ports_format_finish: (virtual set_ports_format_finish)
 * @self: the session item
 * @res: the async result
 * @error: (out) (optional): the operation's error, if it occurred
 *
 * Finishes the operation started by wp_si_adapter_set_format().
 * This is meant to be called in the callback that was passed to that method.
 *
 * Returns: %TRUE on success, %FALSE if there was an error
 */
gboolean
wp_si_adapter_set_ports_format_finish (WpSiAdapter * self, GAsyncResult * res,
      GError ** error)
{
  g_return_val_if_fail (WP_IS_SI_ADAPTER (self), FALSE);
  g_return_val_if_fail (WP_SI_ADAPTER_GET_IFACE (self)->set_ports_format_finish,
     FALSE);

  return WP_SI_ADAPTER_GET_IFACE (self)->set_ports_format_finish (self, res,
     error);
}


G_DEFINE_INTERFACE (WpSiLinkable, wp_si_linkable, WP_TYPE_SESSION_ITEM)

static WpSiAcquisition *
wp_si_linkable_default_get_acquisition (WpSiLinkable * self)
{
  return NULL;
}

static void
wp_si_linkable_default_init (WpSiLinkableInterface * iface)
{
  iface->get_acquisition = wp_si_linkable_default_get_acquisition;
}

/*!
 * @memberof WpSiLinkable
 * @param self: the session item
 * @param context: (nullable): an optional context for the ports
 *
 * @brief This method returns a variant of type "a(uuu)", where each tuple in the
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
 * The @em context argument can be used to get different sets of ports from
 * the item. The following well-known contexts are defined:
 *   - %NULL: get the standard ports to be linked
 *   - "monitor": get the monitor ports
 *   - "control": get the control port
 *   - "reverse": get the reverse direction ports, if this item controls a
 *                filter node, which would have ports on both directions
 *
 * Contexts other than %NULL may only be used internally to ease the
 * implementation of more complex item relationships. For example, a
 * [WpSessionItem](@ref session_item_section) that is in control of an
 * input (sink) adapter node may implement [WpSiLinkable](@ref si_linkable_section)
 * where the %NULL context will return the standard
 * input ports and the "monitor" context will return the adapter's monitor
 * ports. When linking this item to another item, the %NULL context
 * will always be used, but the item may internally spawn a secondary
 * [WpSessionItem](@ref session_item_section) that implements the "monitor" item.
 * That secondary item may implement [WpSiLinkable](@ref si_linkable_section),
 * chaining calls to the [WpSiLinkable](@ref si_linkable_section)
 * of the original item using the "monitor" context. This way, the monitor
 * [WpSessionItem](@ref session_item_section) does not need to share control of the
 * underlying node; it only proxies calls to satisfy the API.
 *
 * @returns (transfer full): a
 * <a href="https://developer.gnome.org/glib/stable/glib-GVariant.html#GVariant">
 * GVariant</a> containing information about the
 *   ports of this item
 */

GVariant *
wp_si_linkable_get_ports (WpSiLinkable * self, const gchar * context)
{
  g_return_val_if_fail (WP_IS_SI_LINKABLE (self), NULL);
  g_return_val_if_fail (WP_SI_LINKABLE_GET_IFACE (self)->get_ports, NULL);

  return WP_SI_LINKABLE_GET_IFACE (self)->get_ports (self, context);
}

/*!
 * @memberof WpSiLinkable
 * @param self: the session item
 *
 * @returns (transfer none) (nullable): the acquisition interface associated
 *   with this item, or %NULL if this item does not require acquiring items
 *   before linking them
 */

WpSiAcquisition *
wp_si_linkable_get_acquisition (WpSiLinkable * self)
{
  g_return_val_if_fail (WP_IS_SI_LINKABLE (self), NULL);
  g_return_val_if_fail (
      WP_SI_LINKABLE_GET_IFACE (self)->get_acquisition, NULL);

  return WP_SI_LINKABLE_GET_IFACE (self)->get_acquisition (self);
}

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

/*!
 * @memberof WpSiLink
 * @param self: the session item
 *
 * @brief This should return information that is used for registering the link,
 * as a GVariant of type a{ss} that contains additional properties to be
 * added to the list of global properties
 *
 * @returns (transfer full): registration info for the link
 */

GVariant *
wp_si_link_get_registration_info (WpSiLink * self)
{
  g_return_val_if_fail (WP_IS_SI_LINK (self), NULL);
  g_return_val_if_fail (WP_SI_LINK_GET_IFACE (self)->get_registration_info, NULL);

  return WP_SI_LINK_GET_IFACE (self)->get_registration_info (self);
}

/*!
 * @memberof WpSiLink
 * @param self: the session item
 *
 * @returns (transfer full) (nullable): the properties of the link
 */

WpProperties *
wp_si_link_get_properties (WpSiLink * self)
{
  g_return_val_if_fail (WP_IS_SI_LINK (self), NULL);
  g_return_val_if_fail (WP_SI_LINK_GET_IFACE (self)->get_properties, NULL);

  return WP_SI_LINK_GET_IFACE (self)->get_properties (self);
}

/*!
 * @memberof WpSiLink
 * @param self: the session item
 *
 * @returns (transfer none): the output item that is linked by this link
 */
WpSiLinkable *
wp_si_link_get_out_item (WpSiLink * self)
{
  g_return_val_if_fail (WP_IS_SI_LINK (self), NULL);
  g_return_val_if_fail (WP_SI_LINK_GET_IFACE (self)->get_out_item, NULL);

  return WP_SI_LINK_GET_IFACE (self)->get_out_item (self);
}

/*!
 * @memberof WpSiLink
 * @param self: the session item
 *
 * @returns (transfer none): the input item that is linked by this link
 */
WpSiLinkable *
wp_si_link_get_in_item (WpSiLink * self)
{
  g_return_val_if_fail (WP_IS_SI_LINK (self), NULL);
  g_return_val_if_fail (WP_SI_LINK_GET_IFACE (self)->get_in_item, NULL);

  return WP_SI_LINK_GET_IFACE (self)->get_in_item (self);
}

G_DEFINE_INTERFACE (WpSiAcquisition, wp_si_acquisition, WP_TYPE_SESSION_ITEM)

static void
wp_si_acquisition_default_init (WpSiAcquisitionInterface * iface)
{
}

/*!
 * @memberof WpSiAcquisition
 * @param self: the session item
 * @param acquisitor: the link that is trying to acquire a port info item
 * @param item: the item that is being acquired
 * @param callback: (scope async): the callback to call when the operation is done
 * @param data: (closure): user data for @em callback
 *
 * @brief Acquires the @em item for linking by @em acquisitor.
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
    WpSiLinkable * item, GAsyncReadyCallback callback, gpointer data)
{
  g_return_if_fail (WP_IS_SI_ACQUISITION (self));
  g_return_if_fail (WP_SI_ACQUISITION_GET_IFACE (self)->acquire);

  WP_SI_ACQUISITION_GET_IFACE (self)->acquire (self, acquisitor, item, callback,
      data);
}

/*!
 * @memberof WpSiAcquisition
 * @param self: the session item
 * @param res: the async result
 * @param error: (out) (optional): the operation's error, if it occurred
 *
 * @brief Finishes the operation started by wp_si_acquisition_acquire().
 * This is meant to be called in the callback that was passed to that method.
 *
 * @returns %TRUE on success, %FALSE if there was an error
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

/*!
 * @memberof WpSiAcquisition
 * @param self: the session item
 * @param acquisitor: the link that had previously acquired the item
 * @param item: the port info that is being released
 *
 * @brief Releases the @em item, which means that it is being unlinked.
 */

void
wp_si_acquisition_release (WpSiAcquisition * self, WpSiLink * acquisitor,
    WpSiLinkable * item)
{
  g_return_if_fail (WP_IS_SI_ACQUISITION (self));
  g_return_if_fail (WP_SI_ACQUISITION_GET_IFACE (self)->release);

  WP_SI_ACQUISITION_GET_IFACE (self)->release (self, acquisitor, item);
}
