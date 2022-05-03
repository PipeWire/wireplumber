/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#define G_LOG_DOMAIN "wp-si-interfaces"

#include "si-interfaces.h"
#include "wpenums.h"

/*! \defgroup wpsiinterfaces Session Item Interfaces */

/*!
 * \struct WpSiEndpoint
 *
 * An interface for session items that implement a PipeWire endpoint.
 *
 * \gsignals
 *
 * \par endpoint-properties-changed
 * \parblock
 * \code
 * void
 * endpoint_properties_changed_callback (WpSiEndpoint * self,
 *                                       gpointer user_data)
 * \endcode
 * Emitted when the endpoint properties change
 *
 * Flags: G_SIGNAL_RUN_LAST
 * \endparblock
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

/*!
 * \brief This should return information that is used for registering the
 * endpoint.
 *
 * The return value should be a GVariant tuple of type (ssya{ss}) that contains,
 * in order:
 *  - s: the endpoint's name
 *  - s: the media class
 *  - y: the direction
 *  - a{ss}: additional properties to be added to the list of global properties
 *
 * \ingroup wpsiinterfaces
 * \param self the session item
 * \returns (transfer full): registration info for the endpoint
 */
GVariant *
wp_si_endpoint_get_registration_info (WpSiEndpoint * self)
{
  g_return_val_if_fail (WP_IS_SI_ENDPOINT (self), NULL);
  g_return_val_if_fail (WP_SI_ENDPOINT_GET_IFACE (self)->get_registration_info, NULL);

  return WP_SI_ENDPOINT_GET_IFACE (self)->get_registration_info (self);
}

/*!
 * \brief Gets the properties of the endpoint
 *
 * \ingroup wpsiinterfaces
 * \param self the session item
 * \returns (transfer full) (nullable): the properties of the endpoint
 */
WpProperties *
wp_si_endpoint_get_properties (WpSiEndpoint * self)
{
  g_return_val_if_fail (WP_IS_SI_ENDPOINT (self), NULL);
  g_return_val_if_fail (WP_SI_ENDPOINT_GET_IFACE (self)->get_properties, NULL);

  return WP_SI_ENDPOINT_GET_IFACE (self)->get_properties (self);
}

/*!
 * \struct WpSiAdapter
 * An interface for port adapters
 */

G_DEFINE_INTERFACE (WpSiAdapter, wp_si_adapter, WP_TYPE_SESSION_ITEM)

static void
wp_si_adapter_default_init (WpSiAdapterInterface * iface)
{
  g_signal_new ("adapter-ports-state-changed", G_TYPE_FROM_INTERFACE (iface),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE,
      2, WP_TYPE_SI_ADAPTER_PORTS_STATE, WP_TYPE_SI_ADAPTER_PORTS_STATE);
}

/**
 * \brief Gets the ports state
 *
 * \ingroup wpsiinterfaces
 * \param self the session item
 * \returns The state of the ports
 */
WpSiAdapterPortsState
wp_si_adapter_get_ports_state (WpSiAdapter * self)
{
  g_return_val_if_fail (WP_IS_SI_ADAPTER (self), WP_SI_ADAPTER_PORTS_STATE_NONE);
  g_return_val_if_fail (WP_SI_ADAPTER_GET_IFACE (self)->get_ports_state,
      WP_SI_ADAPTER_PORTS_STATE_NONE);

  return WP_SI_ADAPTER_GET_IFACE (self)->get_ports_state (self);
}

/**
 * \brief Gets the format used to configure the adapter session item's ports
 *
 * \ingroup wpsiinterfaces
 * \param self the session item
 * \param mode (out) (nullable): the mode
 * \returns (transfer full): The format used to configure the ports of the
 *   adapter session item. Some items automatically choose a format when being
 *   activated, others never set a format on activation and the user needs to
 *   manually set it externally with wp_si_adapter_set_ports_format().
 */
WpSpaPod *
wp_si_adapter_get_ports_format (WpSiAdapter * self, const gchar **mode)
{
  g_return_val_if_fail (WP_IS_SI_ADAPTER (self), NULL);
  g_return_val_if_fail (WP_SI_ADAPTER_GET_IFACE (self)->get_ports_format, NULL);

  return WP_SI_ADAPTER_GET_IFACE (self)->get_ports_format (self, mode);
}

/*!
 * \brief Sets the format and configures the adapter session item ports using
 * the given format.
 *
 * The result of the operation can be checked using the
 * wp_si_adapter_set_ports_format_finish() API. If format is NULL, the adapter
 * will be configured with the default format. If mode is NULL, the adapter
 * will use "dsp" mode.
 *
 * \ingroup wpsiinterfaces
 * \param self the session item
 * \param format (transfer full) (nullable): the format to be set
 * \param mode (nullable): the mode
 * \param callback (scope async): the callback to call when the operation is done
 * \param data (closure): user data for \a callback
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

/*!
 * \brief Finishes the operation started by wp_si_adapter_set_format().
 * This is meant to be called in the callback that was passed to that method.
 *
 * \ingroup wpsiinterfaces
 * \param self the session item
 * \param res the async result
 * \param error (out) (optional): the operation's error, if it occurred
 * \returns TRUE on success, FALSE if there was an error
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

/*!
 * \struct WpSiLinkable
 *
 * An interface for retrieving PipeWire port information from a session item.
 * This information is used to create links in the nodes graph.
 *
 * This is normally implemented by the same session items that implement
 * WpSiEndpoint. The standard link implementation expects to be able to cast
 * a WpSiEndpoint into a WpSiLinkable.
 */

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
 * \brief This method returns a variant of type "a(uuu)", where each tuple in
 * the array contains the following information:
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
 * The \a context argument can be used to get different sets of ports from
 * the item. The following well-known contexts are defined:
 *   - NULL: get the standard ports to be linked
 *   - "monitor": get the monitor ports
 *   - "control": get the control port
 *   - "reverse": get the reverse direction ports, if this item controls a
 *                filter node, which would have ports on both directions
 *
 * Contexts other than NULL may only be used internally to ease the
 * implementation of more complex item relationships. For example, a
 * WpSessionItem that is in control of an input (sink) adapter node may
 * implement WpSiLinkable where the NULL context will return the standard
 * input ports and the "monitor" context will return the adapter's monitor
 * ports. When linking this item to another item, the NULL context
 * will always be used, but the item may internally spawn a secondary
 * WpSessionItem that implements the "monitor" item. That secondary item
 * may implement WpSiLinkable, chaining calls to the WpSiLinkable
 * of the original item using the "monitor" context. This way, the monitor
 * WpSessionItem does not need to share control of the
 * underlying node; it only proxies calls to satisfy the API.
 *
 * \ingroup wpsiinterfaces
 * \param self the session item
 * \param context (nullable): an optional context for the ports
 * \returns (transfer full): a GVariant containing information about the
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
 * \brief Gets the acquisition interface associated with the item
 *
 * \ingroup wpsiinterfaces
 * \param self the session item
 *
 * \returns (transfer none) (nullable): the acquisition interface associated
 *   with this item, or NULL if this item does not require acquiring items
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


/*!
 * \struct WpSiLink
 *
 * An interface for session items that provide a PipeWire endpoint link.
 *
 * \gsignals
 *
 * \par link-properties-changed
 * \parblock
 * \code
 * void
 * link_properties_changed_callback (WpSiLink * self,
 *                                   gpointer user_data)
 * \endcode
 * Emitted when the properties of the link change
 *
 * Flags: G_SIGNAL_RUN_LAST
 * \endparblock
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

/*!
 * \brief This should return information that is used for registering the link,
 * as a GVariant of type a{ss} that contains additional properties to be
 * added to the list of global properties
 *
 * \ingroup wpsiinterfaces
 * \param self the session item
 * \returns (transfer full): registration info for the link
 */
GVariant *
wp_si_link_get_registration_info (WpSiLink * self)
{
  g_return_val_if_fail (WP_IS_SI_LINK (self), NULL);
  g_return_val_if_fail (WP_SI_LINK_GET_IFACE (self)->get_registration_info, NULL);

  return WP_SI_LINK_GET_IFACE (self)->get_registration_info (self);
}

/*!
 * \brief Gets the properties of the link
 *
 * \ingroup wpsiinterfaces
 * \param self the session item
 * \returns (transfer full) (nullable): the properties of the link
 */
WpProperties *
wp_si_link_get_properties (WpSiLink * self)
{
  g_return_val_if_fail (WP_IS_SI_LINK (self), NULL);
  g_return_val_if_fail (WP_SI_LINK_GET_IFACE (self)->get_properties, NULL);

  return WP_SI_LINK_GET_IFACE (self)->get_properties (self);
}

/*!
 * \brief Gets the output item linked by the link
 *
 * \ingroup wpsiinterfaces
 * \param self the session item
 * \returns (transfer none): the output item that is linked by this link
 */
WpSiLinkable *
wp_si_link_get_out_item (WpSiLink * self)
{
  g_return_val_if_fail (WP_IS_SI_LINK (self), NULL);
  g_return_val_if_fail (WP_SI_LINK_GET_IFACE (self)->get_out_item, NULL);

  return WP_SI_LINK_GET_IFACE (self)->get_out_item (self);
}

/*!
 * \brief Gets the input item linked by the link
 *
 * \ingroup wpsiinterfaces
 * \param self the session item
 * \returns (transfer none): the input item that is linked by this link
 */
WpSiLinkable *
wp_si_link_get_in_item (WpSiLink * self)
{
  g_return_val_if_fail (WP_IS_SI_LINK (self), NULL);
  g_return_val_if_fail (WP_SI_LINK_GET_IFACE (self)->get_in_item, NULL);

  return WP_SI_LINK_GET_IFACE (self)->get_in_item (self);
}

/*!
 * \struct WpSiAcquisition
 *
 * This interface provides a way to request an item for linking before doing
 * so. This allows item implementations to apply internal policy rules.
 *
 * A WpSiAcquisition is associated directly with a WpSiLinkable via
 * wp_si_linkable_get_acquisition(). In order to allow switching
 * policies, it is recommended that port info implementations use a separate
 * session item to implement this interface and allow replacing it.
 */

G_DEFINE_INTERFACE (WpSiAcquisition, wp_si_acquisition, WP_TYPE_SESSION_ITEM)

static void
wp_si_acquisition_default_init (WpSiAcquisitionInterface * iface)
{
}

/*!
 * \brief Acquires the \a item for linking by \a acquisitor.
 *
 * When a link is not allowed by policy, this operation should return
 * an error.
 *
 * When a link needs to be delayed for a short amount of time (ex. to apply
 * a fade out effect on another item), this operation should finish with a
 * delay. It is safe to assume that after this operation completes,
 * the item will be linked immediately.
 *
 * \ingroup wpsiinterfaces
 * \param self the session item
 * \param acquisitor the link that is trying to acquire a port info item
 * \param item the item that is being acquired
 * \param callback (scope async): the callback to call when the operation is done
 * \param data (closure): user data for \a callback
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
 * \brief Finishes the operation started by wp_si_acquisition_acquire().
 * This is meant to be called in the callback that was passed to that method.
 *
 * \ingroup wpsiinterfaces
 * \param self the session item
 * \param res the async result
 * \param error (out) (optional): the operation's error, if it occurred
 * \returns TRUE on success, FALSE if there was an error
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
 * \brief Releases the \a item, which means that it is being unlinked.
 *
 * \ingroup wpsiinterfaces
 * \param self the session item
 * \param acquisitor the link that had previously acquired the item
 * \param item the port info that is being released
 */
void
wp_si_acquisition_release (WpSiAcquisition * self, WpSiLink * acquisitor,
    WpSiLinkable * item)
{
  g_return_if_fail (WP_IS_SI_ACQUISITION (self));
  g_return_if_fail (WP_SI_ACQUISITION_GET_IFACE (self)->release);

  WP_SI_ACQUISITION_GET_IFACE (self)->release (self, acquisitor, item);
}
