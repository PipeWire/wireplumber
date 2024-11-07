/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "proxy-interfaces.h"
#include "properties.h"
#include "log.h"

WP_DEFINE_LOCAL_LOG_TOPIC ("wp-proxy-ifaces")

/*! \defgroup wppipewireobject WpPipewireObject */
/*!
 * \struct WpPipewireObject
 *
 * An interface for standard PipeWire objects.
 *
 * The common characteristic of all objects that implement this interface is
 * the presence of an "info" structure that contains additional properties
 * for this object (in the form of a spa_dict / pw_properties) and optionally
 * also some parameters that can be enumerated and set on the object.
 *
 * \gproperties
 *
 * \gproperty{properties, WpProperties *, G_PARAM_READABLE,
 *   The properties of the pipewire object}
 *
 * \gproperty{param-info, GVariant * (a{ss}), G_PARAM_READABLE,
 *   The param info of the object}
 *
 * \gproperty{native-info, gpointer, G_PARAM_READABLE,
 *   The native info structure}
 *
 * \gsignals
 *
 * \par params-changed
 * \parblock
 * \code
 * params_changed_callback (WpPipewireObject * self,
 *                          const gchar *id,
 *                          gpointer user_data)
 * \endcode
 *
 * Emitted when the params for id have changed. On proxies that cache params
 * from a remote object, this is emitted after the cached values have changed.
 *
 * You can expect this to be emitted only when the relevant
 * WP_PIPEWIRE_OBJECT_FEATURE_PARAM_* has been activated.
 *
 * Parameters:
 * - `id` - the parameter id as a string (ex "Props", "EnumRoute")
 *
 * Flags: G_SIGNAL_RUN_FIRST
 * \endparblock
 */

G_DEFINE_INTERFACE (WpPipewireObject, wp_pipewire_object, WP_TYPE_PROXY)

static void
wp_pipewire_object_default_init (WpPipewireObjectInterface * iface)
{
  g_object_interface_install_property (iface,
      g_param_spec_pointer ("native-info", "native-info",
          "The native info structure",
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_interface_install_property (iface,
      g_param_spec_boxed ("properties", "properties",
          "The properties of the pipewire object", WP_TYPE_PROPERTIES,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_interface_install_property (iface,
      g_param_spec_variant ("param-info", "param-info",
          "The param info of the object", G_VARIANT_TYPE ("a{ss}"), NULL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_signal_new ("params-changed", G_TYPE_FROM_INTERFACE (iface),
      G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_STRING);
}

/*!
 * \brief Retrieves the native info structure of this object
 * (pw_node_info, pw_port_info, etc...)
 *
 * \remark Requires WP_PIPEWIRE_OBJECT_FEATURE_INFO
 *
 * \ingroup wppipewireobject
 * \param self the pipewire object
 * \returns (nullable): the native pipewire info structure of this object
 */
gconstpointer
wp_pipewire_object_get_native_info (WpPipewireObject * self)
{
  g_return_val_if_fail (WP_IS_PIPEWIRE_OBJECT (self), NULL);
  g_return_val_if_fail (WP_PIPEWIRE_OBJECT_GET_IFACE (self)->get_native_info,
      NULL);

  return WP_PIPEWIRE_OBJECT_GET_IFACE (self)->get_native_info (self);
}

/*!
 * \brief Retrieves the PipeWire properties of this object
 *
 * \remark Requires WP_PIPEWIRE_OBJECT_FEATURE_INFO
 *
 * \ingroup wppipewireobject
 * \param self the pipewire object
 * \returns (transfer full): the pipewire properties of this object;
 *   normally these are the properties that are part of the info structure
 */
WpProperties *
wp_pipewire_object_get_properties (WpPipewireObject * self)
{
  g_return_val_if_fail (WP_IS_PIPEWIRE_OBJECT (self), NULL);
  g_return_val_if_fail (WP_PIPEWIRE_OBJECT_GET_IFACE (self)->get_properties,
      NULL);

  return WP_PIPEWIRE_OBJECT_GET_IFACE (self)->get_properties (self);
}

/*!
 * \brief Iterates over the object's PipeWire properties
 *
 * \remark Requires WP_PIPEWIRE_OBJECT_FEATURE_INFO
 *
 * \ingroup wppipewireobject
 * \param self the pipewire object
 * \returns (transfer full): an iterator that iterates over the pipewire
 *   properties of this object. Use wp_properties_iterator_item_get_key() and
 *   wp_properties_iterator_item_get_value() to parse the items returned by
 *   this iterator.
 */
WpIterator *
wp_pipewire_object_new_properties_iterator (WpPipewireObject * self)
{
  g_autoptr (WpProperties) properties =
      wp_pipewire_object_get_properties (self);
  return properties ? wp_properties_new_iterator (properties) : NULL;
}

/*!
 * \brief Returns the value of a single pipewire property.
 *
 * This is the same as getting the whole properties structure with
 * wp_pipewire_object_get_properties() and accessing a single property with
 * wp_properties_get(), but saves one call and having to clean up the
 * WpProperties reference count afterwards.
 *
 * The value is owned by the proxy, but it is guaranteed to stay alive
 * until execution returns back to the event loop.
 *
 * \remark Requires WP_PIPEWIRE_OBJECT_FEATURE_INFO
 *
 * \ingroup wppipewireobject
 * \param self the pipewire object
 * \param key the property name
 * \returns (transfer none) (nullable): the value of the pipewire property
 *   \a key or NULL if the property doesn't exist
 */
const gchar *
wp_pipewire_object_get_property (WpPipewireObject * self, const gchar * key)
{
  g_autoptr (WpProperties) properties =
      wp_pipewire_object_get_properties (self);
  return properties ? wp_properties_get (properties, key) : NULL;
}

/*!
 * \brief Returns the available parameters of this pipewire object.
 *
 * The return value is a GVariant of type `a{ss}`, where the key of each map
 * entry is a spa param type id (the same ids that you can pass in
 * wp_pipewire_object_enum_params()) and the value is a string that can
 * contain the following letters, each of them representing a flag:
 *   - `r`: the param is readable (`SPA_PARAM_INFO_READ`)
 *   - `w`: the param is writable (`SPA_PARAM_INFO_WRITE`)
 *
 * For params that are readable, you can query them with
 * wp_pipewire_object_enum_params()
 *
 * Params that are writable can be set with wp_pipewire_object_set_param()
 *
 * \remark Requires WP_PIPEWIRE_OBJECT_FEATURE_INFO
 *
 * \ingroup wppipewireobject
 * \param self the pipewire object
 * \returns (transfer full) (nullable): a variant of type `a{ss}` or NULL
 *   if the object does not support params at all
 */
GVariant *
wp_pipewire_object_get_param_info (WpPipewireObject * self)
{
  g_return_val_if_fail (WP_IS_PIPEWIRE_OBJECT (self), NULL);
  g_return_val_if_fail (WP_PIPEWIRE_OBJECT_GET_IFACE (self)->get_param_info,
      NULL);

  return WP_PIPEWIRE_OBJECT_GET_IFACE (self)->get_param_info (self);
}

/*!
 * \brief Enumerate object parameters.
 *
 * This will asynchronously return the result, or an error, by calling the
 * given \a callback. The result is going to be a WpIterator containing
 * WpSpaPod objects, which can be retrieved
 * with wp_pipewire_object_enum_params_finish().
 *
 * \ingroup wppipewireobject
 * \param self the pipewire object
 * \param id (nullable): the parameter id to enumerate or NULL for all parameters
 * \param filter (nullable): a param filter or NULL
 * \param cancellable (nullable): a cancellable for the async operation
 * \param callback (scope async): a callback to call with the result
 * \param user_data (closure): data to pass to \a callback
 */
void
wp_pipewire_object_enum_params (WpPipewireObject * self, const gchar * id,
    WpSpaPod *filter, GCancellable * cancellable,
    GAsyncReadyCallback callback, gpointer user_data)
{
  g_return_if_fail (WP_IS_PIPEWIRE_OBJECT (self));
  g_return_if_fail (WP_PIPEWIRE_OBJECT_GET_IFACE (self)->enum_params);

  WP_PIPEWIRE_OBJECT_GET_IFACE (self)->enum_params (self, id, filter,
      cancellable, callback, user_data);
}

/*!
 * \brief Finishes an asynchronous parameter enumeration operation.
 *
 * \ingroup wppipewireobject
 * \param self the pipewire object
 * \param res the async result
 * \param error (out) (optional): the reported error of the operation, if any
 * \returns (transfer full) (nullable): an iterator to iterate over the
 *   collected params, or NULL if the operation resulted in error;
 *   the items in the iterator are WpSpaPod
 */
WpIterator *
wp_pipewire_object_enum_params_finish (WpPipewireObject * self,
    GAsyncResult * res, GError ** error)
{
  g_return_val_if_fail (WP_IS_PIPEWIRE_OBJECT (self), NULL);
  g_return_val_if_fail (WP_PIPEWIRE_OBJECT_GET_IFACE (self)->enum_params_finish,
      NULL);

  return WP_PIPEWIRE_OBJECT_GET_IFACE (self)->enum_params_finish (self, res,
      error);
}

/*!
 * \brief This method can be used to retrieve object parameters in a synchronous
 * way (in contrast with wp_pipewire_object_enum_params(), which is async).
 *
 * The `WP_PIPEWIRE_OBJECT_FEATURE_PARAM_<something>` feature
 * that corresponds to the specified \a id must have been activated earlier.
 * These features enable monitoring and caching of params underneath, so that
 * they are always available for retrieval with this method.
 *
 * Note, however, that cached params may be out-of-date if they have changed
 * very recently on the remote object and the caching mechanism hasn't been
 * able to update them yet, so if you really need up-to-date information you
 * should only rely on wp_pipewire_object_enum_params() instead.
 *
 * \ingroup wppipewireobject
 * \param self the pipewire object
 * \param id the parameter id to enumerate
 * \param filter (nullable): a param filter or NULL
 * \returns (transfer full) (nullable): an iterator to iterate over cached
 *    parameters, or NULL if parameters for this \a id are not cached;
 *    the items in the iterator are WpSpaPod
 */
WpIterator *
wp_pipewire_object_enum_params_sync (WpPipewireObject * self,
    const gchar * id, WpSpaPod * filter)
{
  g_return_val_if_fail (WP_IS_PIPEWIRE_OBJECT (self), NULL);
  g_return_val_if_fail (WP_PIPEWIRE_OBJECT_GET_IFACE (self)->enum_params_sync,
      NULL);

  return WP_PIPEWIRE_OBJECT_GET_IFACE (self)->enum_params_sync (self, id,
      filter);
}

/*!
 * \brief Sets a parameter on the object.
 *
 * \ingroup wppipewireobject
 * \param self the pipewire object
 * \param id the parameter id to set
 * \param flags optional flags or 0
 * \param param (transfer full): the parameter to set
 * \returns TRUE on success, FALSE if setting the param failed
 */
gboolean
wp_pipewire_object_set_param (WpPipewireObject * self, const gchar * id,
    guint32 flags, WpSpaPod * param)
{
  g_return_val_if_fail (WP_IS_PIPEWIRE_OBJECT (self), FALSE);
  g_return_val_if_fail (WP_PIPEWIRE_OBJECT_GET_IFACE (self)->set_param, FALSE);

  return WP_PIPEWIRE_OBJECT_GET_IFACE (self)->set_param (self, id, flags,
      param);
}
