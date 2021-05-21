/* WirePlumber
 *
 * Copyright © 2019-2020 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#define G_LOG_DOMAIN "wp-device"

#include "device.h"
#include "node.h"
#include "core.h"
#include "log.h"
#include "error.h"
#include "private/pipewire-object-mixin.h"

#include <pipewire/impl.h>
#include <spa/debug/types.h>
#include <spa/monitor/device.h>
#include <spa/utils/result.h>

/*! \defgroup wpdevice WpDevice */
/*!
 * \struct WpDevice
 *
 * The WpDevice class allows accessing the properties and methods of a
 * PipeWire device object (`struct pw_device`).
 *
 * A WpDevice is constructed internally when a new device appears on the
 * PipeWire registry and it is made available through the WpObjectManager API.
 * Alternatively, a WpDevice can also be constructed using
 * wp_device_new_from_factory(), which creates a new device object
 * on the remote PipeWire server by calling into a factory.
 *
 */

struct _WpDevice
{
  WpGlobalProxy parent;
};

static void wp_device_pw_object_mixin_priv_interface_init (
    WpPwObjectMixinPrivInterface * iface);

G_DEFINE_TYPE_WITH_CODE (WpDevice, wp_device, WP_TYPE_GLOBAL_PROXY,
    G_IMPLEMENT_INTERFACE (WP_TYPE_PIPEWIRE_OBJECT,
        wp_pw_object_mixin_object_interface_init)
    G_IMPLEMENT_INTERFACE (WP_TYPE_PW_OBJECT_MIXIN_PRIV,
        wp_device_pw_object_mixin_priv_interface_init));

static void
wp_device_init (WpDevice * self)
{
}

static void
wp_device_activate_execute_step (WpObject * object,
    WpFeatureActivationTransition * transition, guint step,
    WpObjectFeatures missing)
{
  switch (step) {
  case WP_PW_OBJECT_MIXIN_STEP_BIND:
  case WP_TRANSITION_STEP_ERROR:
    /* base class can handle BIND and ERROR */
    WP_OBJECT_CLASS (wp_device_parent_class)->
        activate_execute_step (object, transition, step, missing);
    break;
  case WP_PW_OBJECT_MIXIN_STEP_WAIT_INFO:
    /* just wait, info will be emitted anyway after binding */
    break;
  case WP_PW_OBJECT_MIXIN_STEP_CACHE_PARAMS:
    wp_pw_object_mixin_cache_params (object, missing);
    break;
  default:
    g_assert_not_reached ();
  }
}

static void
wp_device_deactivate (WpObject * object, WpObjectFeatures features)
{
  wp_pw_object_mixin_deactivate (object, features);
  WP_OBJECT_CLASS (wp_device_parent_class)->deactivate (object, features);
}

static const struct pw_device_events device_events = {
  PW_VERSION_DEVICE_EVENTS,
  .info = (HandleEventInfoFunc(device)) wp_pw_object_mixin_handle_event_info,
  .param = wp_pw_object_mixin_handle_event_param,
};

static void
wp_device_pw_proxy_created (WpProxy * proxy, struct pw_proxy * pw_proxy)
{
  wp_pw_object_mixin_handle_pw_proxy_created (proxy, pw_proxy,
      device, &device_events);
}

static void
wp_device_pw_proxy_destroyed (WpProxy * proxy)
{
  wp_pw_object_mixin_handle_pw_proxy_destroyed (proxy);

  WP_PROXY_CLASS (wp_device_parent_class)->pw_proxy_destroyed (proxy);
}

static void
wp_device_class_init (WpDeviceClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpObjectClass *wpobject_class = (WpObjectClass *) klass;
  WpProxyClass *proxy_class = (WpProxyClass *) klass;

  object_class->get_property = wp_pw_object_mixin_get_property;

  wpobject_class->get_supported_features =
      wp_pw_object_mixin_get_supported_features;
  wpobject_class->activate_get_next_step =
      wp_pw_object_mixin_activate_get_next_step;
  wpobject_class->activate_execute_step = wp_device_activate_execute_step;
  wpobject_class->deactivate = wp_device_deactivate;

  proxy_class->pw_iface_type = PW_TYPE_INTERFACE_Device;
  proxy_class->pw_iface_version = PW_VERSION_DEVICE;
  proxy_class->pw_proxy_created = wp_device_pw_proxy_created;
  proxy_class->pw_proxy_destroyed = wp_device_pw_proxy_destroyed;

  wp_pw_object_mixin_class_override_properties (object_class);
}

static gint
wp_device_enum_params (gpointer instance, guint32 id,
    guint32 start, guint32 num, WpSpaPod *filter)
{
  WpPwObjectMixinData *d = wp_pw_object_mixin_get_data (instance);
  return pw_device_enum_params (d->iface, 0, id, start, num,
      filter ? wp_spa_pod_get_spa_pod (filter) : NULL);
}

static gint
wp_device_set_param (gpointer instance, guint32 id, guint32 flags,
    WpSpaPod * param)
{
  WpPwObjectMixinData *d = wp_pw_object_mixin_get_data (instance);
  return pw_device_set_param (d->iface, id, flags,
      wp_spa_pod_get_spa_pod (param));
}

static void
wp_device_pw_object_mixin_priv_interface_init (
    WpPwObjectMixinPrivInterface * iface)
{
  wp_pw_object_mixin_priv_interface_info_init (iface, device, DEVICE);
  iface->enum_params = wp_device_enum_params;
  iface->set_param = wp_device_set_param;
}

/*!
 * \brief Constructs a device on the PipeWire server by asking the remote
 * factory \a factory_name to create it.
 *
 * Because of the nature of the PipeWire protocol, this operation completes
 * asynchronously at some point in the future. In order to find out when
 * this is done, you should call wp_object_activate(), requesting at least
 * %WP_PROXY_FEATURE_BOUND. When this feature is ready, the device is ready for
 * use on the server. If the device cannot be created, this activation operation
 * will fail.
 *
 * \ingroup wpdevice
 * \param core the wireplumber core
 * \param factory_name the pipewire factory name to construct the device
 * \param properties (nullable) (transfer full): the properties to pass to the
 *   factory
 * \returns (nullable) (transfer full): the new device or %NULL if the core
 *   is not connected and therefore the device cannot be created
 */

WpDevice *
wp_device_new_from_factory (WpCore * core,
    const gchar * factory_name, WpProperties * properties)
{
  g_autoptr (WpProperties) props = properties;
  return g_object_new (WP_TYPE_DEVICE,
      "core", core,
      "factory-name", factory_name,
      "global-properties", props,
      NULL);
}

/*! \defgroup wpspadevice WpSpaDevice */

struct _WpSpaDevice
{
  WpProxy parent;
  struct spa_handle *handle;
  struct spa_device *device;
  struct spa_hook listener;
  WpProperties *properties;
  GPtrArray *managed_objs;
};

enum {
  PROP_0,
  PROP_SPA_DEVICE_HANDLE,
  PROP_PROPERTIES,
};

enum
{
  SIGNAL_CREATE_OBJECT,
  SIGNAL_OBJECT_REMOVED,
  SPA_DEVICE_LAST_SIGNAL,
};

static guint spa_device_signals[SPA_DEVICE_LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (WpSpaDevice, wp_spa_device, WP_TYPE_PROXY)

static void
object_unref_safe (gpointer object)
{
  if (object)
    g_object_unref (object);
}

static void
wp_spa_device_init (WpSpaDevice * self)
{
  self->properties = wp_properties_new_empty ();
  self->managed_objs = g_ptr_array_new_with_free_func (object_unref_safe);
}

static void
wp_spa_device_constructed (GObject *object)
{
  WpSpaDevice *self = WP_SPA_DEVICE (object);
  gint res;

  g_return_if_fail (self->handle);

  /* Get the handle interface */
  res = spa_handle_get_interface (self->handle, SPA_TYPE_INTERFACE_Device,
      (gpointer *) &self->device);
  if (res < 0) {
    wp_warning_object (self,
        "Could not get device interface from SPA handle: %s",
        spa_strerror (res));
    return;
  }

  G_OBJECT_CLASS (wp_spa_device_parent_class)->constructed (object);
}

static void
wp_spa_device_finalize (GObject * object)
{
  WpSpaDevice *self = WP_SPA_DEVICE (object);

  self->device = NULL;
  g_clear_pointer (&self->handle, pw_unload_spa_handle);
  g_clear_pointer (&self->properties, wp_properties_unref);
  g_clear_pointer (&self->managed_objs, g_ptr_array_unref);

  G_OBJECT_CLASS (wp_spa_device_parent_class)->finalize (object);
}

static void
wp_spa_device_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpSpaDevice *self = WP_SPA_DEVICE (object);

  switch (property_id) {
  case PROP_SPA_DEVICE_HANDLE:
    self->handle = g_value_get_pointer (value);
    break;
  case PROP_PROPERTIES: {
    WpProperties *p = g_value_get_boxed (value);
    if (p)
      wp_properties_update (self->properties, p);
    break;
  }
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_spa_device_get_property (GObject * object, guint property_id, GValue * value,
    GParamSpec * pspec)
{
  WpSpaDevice *self = WP_SPA_DEVICE (object);

  switch (property_id) {
  case PROP_SPA_DEVICE_HANDLE:
    g_value_set_pointer (value, self->handle);
    break;
  case PROP_PROPERTIES:
    g_value_take_boxed (value, wp_properties_ref (self->properties));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
spa_device_event_info (void *data, const struct spa_device_info *info)
{
  WpSpaDevice *self = WP_SPA_DEVICE (data);

  /*
   * This is emited syncrhonously at the time we add the listener and
   * before object_info is emited. It gives us additional properties
   * about the device, like the "api.alsa.card.*" ones that are not
   * set by the monitor
   */
  if (info->change_mask & SPA_DEVICE_CHANGE_MASK_PROPS)
    wp_properties_update_from_dict (self->properties, info->props);
}

static void
spa_device_event_event (void *data, const struct spa_event *event)
{
  WpSpaDevice *self = WP_SPA_DEVICE (data);
  g_autoptr (WpSpaPod) pod =
      wp_spa_pod_new_wrap_const ((const struct spa_pod *) event);
  guint32 id = -1;
  const gchar *type = NULL;
  g_autoptr (WpSpaPod) props = NULL;
  g_autoptr (GObject) child = NULL;

  wp_trace_boxed (WP_TYPE_SPA_POD, pod, "device event");

  if (wp_spa_pod_get_object (pod, &type,
          "Object", "i", &id,
          "Props", "?P", &props,
          NULL))
    child = wp_spa_device_get_managed_object (self, id);

  if (child && !g_strcmp0 (type, "ObjectConfig") &&
      WP_IS_PIPEWIRE_OBJECT (child) && props) {
    wp_pipewire_object_set_param (WP_PIPEWIRE_OBJECT (child), "Props", 0, props);
  }
}

static void
spa_device_event_object_info (void *data, uint32_t id,
    const struct spa_device_object_info *info)
{
  WpSpaDevice *self = WP_SPA_DEVICE (data);

  if (info) {
    const gchar *type;
    g_autoptr (WpProperties) props = NULL;

    type = spa_debug_type_short_name (info->type);
    props = wp_properties_new_wrap_dict (info->props);

    g_signal_emit (self, spa_device_signals[SIGNAL_CREATE_OBJECT], 0,
        id, type, info->factory_name, props);
  }
  else {
    g_signal_emit (self, spa_device_signals[SIGNAL_OBJECT_REMOVED], 0, id);
    wp_spa_device_store_managed_object (self, id, NULL);
  }
}

static const struct spa_device_events spa_device_events = {
  SPA_VERSION_DEVICE_EVENTS,
  .info = spa_device_event_info,
  .event = spa_device_event_event,
  .object_info = spa_device_event_object_info,
};

static WpObjectFeatures
wp_spa_device_get_supported_features (WpObject * object)
{
  return WP_PROXY_FEATURE_BOUND | WP_SPA_DEVICE_FEATURE_ENABLED;
}

enum {
  STEP_EXPORT = WP_TRANSITION_STEP_CUSTOM_START,
  STEP_ADD_DEVICE_LISTENER,
};

static guint
wp_spa_device_activate_get_next_step (WpObject * object,
    WpFeatureActivationTransition * transition, guint step,
    WpObjectFeatures missing)
{
  if (missing & WP_PROXY_FEATURE_BOUND)
    return STEP_EXPORT;
  else if (missing & WP_SPA_DEVICE_FEATURE_ENABLED)
    return STEP_ADD_DEVICE_LISTENER;
  else
    return WP_TRANSITION_STEP_NONE;
}

static void
wp_spa_device_activate_execute_step (WpObject * object,
      WpFeatureActivationTransition * transition, guint step,
      WpObjectFeatures missing)
{
  WpSpaDevice *self = WP_SPA_DEVICE (object);

  switch (step) {
  case STEP_EXPORT: {
    g_autoptr (WpCore) core = wp_object_get_core (object);
    struct pw_core *pw_core = wp_core_get_pw_core (core);
    g_return_if_fail (pw_core);

    wp_proxy_watch_bind_error (WP_PROXY (self), WP_TRANSITION (transition));
    wp_proxy_set_pw_proxy (WP_PROXY (self),
        pw_core_export (pw_core, SPA_TYPE_INTERFACE_Device,
            wp_properties_peek_dict (self->properties),
            self->device, 0));
    break;
  }
  case STEP_ADD_DEVICE_LISTENER: {
    gint res = spa_device_add_listener (self->device, &self->listener,
        &spa_device_events, self);
    if (res < 0)
      wp_transition_return_error (WP_TRANSITION (transition),
          g_error_new (WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_OPERATION_FAILED,
              "failed to activate device: %s", spa_strerror (res)));
    else
      wp_object_update_features (object, WP_SPA_DEVICE_FEATURE_ENABLED, 0);
    break;
  }
  case WP_TRANSITION_STEP_ERROR:
    break;
  default:
    g_assert_not_reached ();
  }
}

static void
wp_spa_device_deactivate (WpObject * object, WpObjectFeatures features)
{
  WP_OBJECT_CLASS (wp_spa_device_parent_class)->deactivate (object, features);

  if (features & WP_SPA_DEVICE_FEATURE_ENABLED) {
    WpSpaDevice *self = WP_SPA_DEVICE (object);
    spa_hook_remove (&self->listener);
    g_ptr_array_set_size (self->managed_objs, 0);
    wp_object_update_features (object, 0, WP_SPA_DEVICE_FEATURE_ENABLED);
  }
}

/*!
 * \struct WpSpaDevice
 *
 * A WpSpaDevice allows running a `spa_device` object locally,
 * loading the implementation from a SPA factory. This is useful to run device
 * monitors inside the session manager and have control over creating the
 * actual nodes that the `spa_device` requests to create.
 *
 * To enable the spa device, call wp_object_activate() requesting
 * WP_SPA_DEVICE_FEATURE_ENABLED.
 *
 * For actual devices (not device monitors) it also possible and desirable
 * to export the device to PipeWire, which can be done by requesting
 * WP_PROXY_FEATURE_BOUND from wp_object_activate(). When exporting, the
 * export should be done before enabling the device, by requesting both
 * features at the same time.
 *
 * \gproperties
 *
 * \gproperty{properties, WpProperties *, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY,
 *   Properties of the spa device}
 *
 * \gproperty{spa-device-handle, gpointer, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY,
 *   The spa device handle}
 *
 * \gsignals
 *
 * \par create-object
 * \parblock
 * \code
 * void
 * create_object_callback (WpSpaDevice * self,
 *                         guint id,
 *                         gchar * type,
 *                         gchar * factory,
 *                         WpProperties * properties,
 *                         gpointer user_data)
 * \endcode
 *
 * This signal is emitted when the device is creating a managed object
 * The handler is expected to actually construct the object using the requested
 * SPA factory and with the given properties. The handler should then store the
 * object with wp_spa_device_store_managed_object. The WpSpaDevice will later
 * unref the reference stored by this function when the managed object is to be
 * destroyed.
 *
 * Parameters:
 * - `id` - the id of the managed object
 * - `type` - the SPA type that the managed object should have
 * - `factory` - the name of the SPA factory to use to construct the managed object
 * - `properties` - additional properties that the managed object should have
 *
 * Flags: G_SIGNAL_RUN_FIRST
 * \endparblock
 *
 * \par remove-object
 * \parblock
 * \code
 * void
 * object_removed_callback (WpSpaDevice * self,
 *                          guint id,
 *                          gpointer user_data)
 * \endcode
 *
 * This signal is emitted when the device has deleted a managed object.
 * The handler may optionally release additional resources associated with this
 * object.
 *
 * It is not necessary to call wp_spa_device_store_managed_object() to remove
 * the managed object, as this is done internally after this signal is fired.
 *
 * Parameters:
 * - `id` - the id of the managed object
 *
 * Flags: G_SIGNAL_RUN_FIRST
 * \endparblock
 */
static void
wp_spa_device_class_init (WpSpaDeviceClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpObjectClass *wpobject_class = (WpObjectClass *) klass;

  object_class->constructed = wp_spa_device_constructed;
  object_class->finalize = wp_spa_device_finalize;
  object_class->set_property = wp_spa_device_set_property;
  object_class->get_property = wp_spa_device_get_property;

  wpobject_class->get_supported_features = wp_spa_device_get_supported_features;
  wpobject_class->activate_get_next_step = wp_spa_device_activate_get_next_step;
  wpobject_class->activate_execute_step = wp_spa_device_activate_execute_step;
  wpobject_class->deactivate = wp_spa_device_deactivate;

  g_object_class_install_property (object_class, PROP_SPA_DEVICE_HANDLE,
      g_param_spec_pointer ("spa-device-handle", "spa-device-handle",
          "The spa device handle",
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_PROPERTIES,
      g_param_spec_boxed ("properties", "properties",
          "Properties of the device", WP_TYPE_PROPERTIES,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  spa_device_signals[SIGNAL_CREATE_OBJECT] = g_signal_new (
      "create-object", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_FIRST,
      0, NULL, NULL, NULL, G_TYPE_NONE, 4, G_TYPE_UINT, G_TYPE_STRING,
      G_TYPE_STRING, WP_TYPE_PROPERTIES);

  spa_device_signals[SIGNAL_OBJECT_REMOVED] = g_signal_new (
      "object-removed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_FIRST,
      0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_UINT);
}

/*!
 * \ingroup wpspadevice
 * \param core the wireplumber core
 * \param spa_device_handle the spa device handle
 * \param properties (nullable) (transfer full): additional properties of the device
 * \returns (transfer full): A new WpSpaDevice
 */
WpSpaDevice *
wp_spa_device_new_wrap (WpCore * core, gpointer spa_device_handle,
    WpProperties * properties)
{
  g_autoptr (WpProperties) props = properties;
  return g_object_new (WP_TYPE_SPA_DEVICE,
      "core", core,
      "spa-device-handle", spa_device_handle,
      "properties", props,
      NULL);
}

/*!
 * \brief Constructs a `SPA_TYPE_INTERFACE_Device` by loading the given SPA
 * \a factory_name.
 *
 * To export this device to the PipeWire server, you need to call
 * wp_object_activate() requesting WP_PROXY_FEATURE_BOUND and
 * wait for the operation to complete.
 *
 * \ingroup wpspadevice
 * \param core the wireplumber core
 * \param factory_name the name of the SPA factory
 * \param properties (nullable) (transfer full): properties to be passed to device
 *    constructor
 * \returns (nullable) (transfer full): A new WpSpaDevice wrapping the
 *   device that was constructed by the factory, or NULL if the factory
 *   does not exist or was unable to construct the device
 */
WpSpaDevice *
wp_spa_device_new_from_spa_factory (WpCore * core,
    const gchar * factory_name, WpProperties * properties)
{
  g_autoptr (WpProperties) props = properties;
  struct pw_context *pw_context = wp_core_get_pw_context (core);
  struct spa_handle *handle = NULL;

  g_return_val_if_fail (pw_context != NULL, NULL);

  /* Load the monitor handle */
  handle = pw_context_load_spa_handle (pw_context, factory_name,
      props ? wp_properties_peek_dict (props) : NULL);
  if (!handle) {
    wp_warning ("SPA handle '%s' could not be loaded; is it installed?",
        factory_name);
    return NULL;
  }

  return wp_spa_device_new_wrap (core, handle, g_steal_pointer (&props));
}

/*!
 * \ingroup wpspadevice
 * \param self the spa device
 * \returns (transfer full): the device properties
 */
WpProperties *
wp_spa_device_get_properties (WpSpaDevice * self)
{
  g_return_val_if_fail (WP_IS_SPA_DEVICE (self), NULL);
  return wp_properties_ref (self->properties);
}

/*!
 * \ingroup wpspadevice
 * \param self the spa device
 * \param id the (device-internal) id of the object to get
 * \returns (transfer full): the managed object associated with \a id
 */
GObject *
wp_spa_device_get_managed_object (WpSpaDevice * self, guint id)
{
  g_return_val_if_fail (WP_IS_SPA_DEVICE (self), NULL);

  GObject *ret = (id < self->managed_objs->len) ?
      g_ptr_array_index (self->managed_objs, id) : NULL;
  return ret ? g_object_ref (ret) : ret;
}

/*!
 * \ingroup wpspadevice
 * \param self the spa device
 * \param id the (device-internal) id of the object
 * \param object (transfer full) (nullable): the object to store or NULL to remove
 *   the managed object associated with \a id
 */
void
wp_spa_device_store_managed_object (WpSpaDevice * self, guint id,
    GObject * object)
{
  g_return_if_fail (WP_IS_SPA_DEVICE (self));

  if (id >= self->managed_objs->len)
    g_ptr_array_set_size (self->managed_objs, id + 1);

  /* replace the item at @em id; g_ptr_array_insert is tempting to use here
     instead, but it's wrong because it will not remove the previous item */
  gpointer *ptr = &g_ptr_array_index (self->managed_objs, id);
  if (*ptr)
    g_object_unref (*ptr);
  *ptr = object;
}
