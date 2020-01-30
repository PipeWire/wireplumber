/* WirePlumber
 *
 * Copyright © 2019-2020 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "device.h"
#include "node.h"
#include "error.h"
#include "private.h"

#include <pipewire/pipewire.h>
#include <pipewire/impl.h>
#include <spa/monitor/device.h>
#include <spa/utils/result.h>

struct _WpDevice
{
  WpProxy parent;
};

typedef struct _WpDevicePrivate WpDevicePrivate;
struct _WpDevicePrivate
{
  struct pw_device_info *info;
  struct spa_hook listener;
};

G_DEFINE_TYPE_WITH_PRIVATE (WpDevice, wp_device, WP_TYPE_PROXY)

static void
wp_device_init (WpDevice * self)
{
}

static void
wp_device_finalize (GObject * object)
{
  WpDevice *self = WP_DEVICE (object);
  WpDevicePrivate *priv = wp_device_get_instance_private (self);

  g_clear_pointer (&priv->info, pw_device_info_free);

  G_OBJECT_CLASS (wp_device_parent_class)->finalize (object);
}

static gconstpointer
wp_device_get_info (WpProxy * self)
{
  WpDevicePrivate *priv = wp_device_get_instance_private (WP_DEVICE (self));
  return priv->info;
}

static WpProperties *
wp_device_get_properties (WpProxy * self)
{
  WpDevicePrivate *priv = wp_device_get_instance_private (WP_DEVICE (self));
  return wp_properties_new_wrap_dict (priv->info->props);
}

static gint
wp_device_enum_params (WpProxy * self, guint32 id, guint32 start,
    guint32 num, const struct spa_pod *filter)
{
  struct pw_device *pwp;
  int device_enum_params_result;

  pwp = (struct pw_device *) wp_proxy_get_pw_proxy (self);
  device_enum_params_result = pw_device_enum_params (pwp, 0, id, start, num,
      filter);
  g_warn_if_fail (device_enum_params_result >= 0);

  return device_enum_params_result;
}

static gint
wp_device_set_param (WpProxy * self, guint32 id, guint32 flags,
    const struct spa_pod *param)
{
  struct pw_device *pwp;
  int device_set_param_result;

  pwp = (struct pw_device *) wp_proxy_get_pw_proxy (self);
  device_set_param_result = pw_device_set_param (pwp, id, flags, param);
  g_warn_if_fail (device_set_param_result >= 0);

  return device_set_param_result;
}

static void
device_event_info(void *data, const struct pw_device_info *info)
{
  WpDevice *self = WP_DEVICE (data);
  WpDevicePrivate *priv = wp_device_get_instance_private (self);

  priv->info = pw_device_info_update (priv->info, info);
  wp_proxy_set_feature_ready (WP_PROXY (self), WP_PROXY_FEATURE_INFO);

  g_object_notify (G_OBJECT (self), "info");

  if (info->change_mask & PW_DEVICE_CHANGE_MASK_PROPS)
    g_object_notify (G_OBJECT (self), "properties");
}

static const struct pw_device_events device_events = {
  PW_VERSION_DEVICE_EVENTS,
  .info = device_event_info,
  .param = wp_proxy_handle_event_param,
};

static void
wp_device_pw_proxy_created (WpProxy * proxy, struct pw_proxy * pw_proxy)
{
  WpDevice *self = WP_DEVICE (proxy);
  WpDevicePrivate *priv = wp_device_get_instance_private (self);
  pw_device_add_listener ((struct pw_device *) pw_proxy,
      &priv->listener, &device_events, self);
}

static void
wp_device_class_init (WpDeviceClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpProxyClass *proxy_class = (WpProxyClass *) klass;

  object_class->finalize = wp_device_finalize;

  proxy_class->pw_iface_type = PW_TYPE_INTERFACE_Device;
  proxy_class->pw_iface_version = PW_VERSION_DEVICE;

  proxy_class->get_info = wp_device_get_info;
  proxy_class->get_properties = wp_device_get_properties;
  proxy_class->enum_params = wp_device_enum_params;
  proxy_class->set_param = wp_device_set_param;

  proxy_class->pw_proxy_created = wp_device_pw_proxy_created;
}

/**
 * wp_device_new_from_factory:
 * @core: the wireplumber core
 * @factory_name: the pipewire factory name to construct the device
 * @properties: (nullable) (transfer full): the properties to pass to the factory
 *
 * Constructs a device on the PipeWire server by asking the remote factory
 * @factory_name to create it.
 *
 * Because of the nature of the PipeWire protocol, this operation completes
 * asynchronously at some point in the future. In order to find out when
 * this is done, you should call wp_proxy_augment(), requesting at least
 * %WP_PROXY_FEATURE_BOUND. When this feature is ready, the device is ready for
 * use on the server. If the device cannot be created, this augment operation
 * will fail.
 *
 * Returns: (nullable) (transfer full): the new device or %NULL if the core
 *   is not connected and therefore the device cannot be created
 */
WpDevice *
wp_device_new_from_factory (WpCore * core,
    const gchar * factory_name, WpProperties * properties)
{
  g_autoptr (WpProperties) props = properties;
  WpDevice *self = NULL;
  struct pw_core *pw_core = wp_core_get_pw_core (core);

  if (!pw_core) {
    g_warning ("The WirePlumber core is not connected; device cannot be created");
    return NULL;
  }

  self = g_object_new (WP_TYPE_DEVICE, "core", core, NULL);
  wp_proxy_set_pw_proxy (WP_PROXY (self), pw_core_create_object (pw_core,
          factory_name, PW_TYPE_INTERFACE_Device, PW_VERSION_DEVICE,
          props ? wp_properties_peek_dict (props) : NULL, 0));
  return self;
}


struct _WpSpaDevice
{
  WpDevice parent;
  struct spa_handle *handle;
  struct spa_device *interface;
  struct spa_hook listener;
  WpProperties *properties;
};

enum
{
  SIGNAL_OBJECT_INFO,
  SPA_DEVICE_LAST_SIGNAL,
};

static guint spa_device_signals[SPA_DEVICE_LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (WpSpaDevice, wp_spa_device, WP_TYPE_PROXY)

static void
wp_spa_device_init (WpSpaDevice * self)
{
}

static void
wp_spa_device_finalize (GObject * object)
{
  WpSpaDevice *self = WP_SPA_DEVICE (object);

  g_clear_pointer (&self->handle, pw_unload_spa_handle);
  g_clear_pointer (&self->properties, wp_properties_unref);

  G_OBJECT_CLASS (wp_spa_device_parent_class)->finalize (object);
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

  wp_proxy_set_feature_ready (WP_PROXY (self), WP_PROXY_FEATURE_INFO);
  wp_proxy_set_feature_ready (WP_PROXY (self), WP_SPA_DEVICE_FEATURE_ACTIVE);
}

static void
spa_device_event_result (void *data, int seq, int res, uint32_t type,
    const void *result)
{
  if (type != SPA_RESULT_TYPE_DEVICE_PARAMS)
    return;

  const struct spa_result_device_params *srdp = result;
  wp_proxy_handle_event_param (WP_PROXY (data), seq, srdp->id, srdp->index,
      srdp->next, srdp->param);
}

static void
spa_device_event_object_info (void *data, uint32_t id,
    const struct spa_device_object_info *info)
{
  WpSpaDevice *self = WP_SPA_DEVICE (data);
  GType type = G_TYPE_NONE;
  g_autoptr (WpProperties) props = NULL;

  if (info) {
    if (!g_strcmp0 (info->type, SPA_TYPE_INTERFACE_Device))
      type = WP_TYPE_DEVICE;
    else if (!g_strcmp0 (info->type, SPA_TYPE_INTERFACE_Node))
      type = WP_TYPE_NODE;

    props = wp_properties_new_wrap_dict (info->props);
  }

  g_signal_emit (self, spa_device_signals[SIGNAL_OBJECT_INFO], 0, id, type,
      info ? info->factory_name : NULL, props, self->properties);
}

static const struct spa_device_events spa_device_events = {
  SPA_VERSION_DEVICE_EVENTS,
  .info = spa_device_event_info,
  .result = spa_device_event_result,
  .object_info = spa_device_event_object_info
};

static void
wp_spa_device_augment (WpProxy * proxy, WpProxyFeatures features)
{
  WpSpaDevice *self = WP_SPA_DEVICE (proxy);

  /* if any of the standard features is requested, make sure BOUND
     is also requested, as they all depend on binding the pw_spa_device */
  if (features & WP_PROXY_FEATURES_STANDARD)
    features |= WP_PROXY_FEATURE_BOUND;

  if (features & WP_PROXY_FEATURE_BOUND) {
    g_autoptr (WpCore) core = wp_proxy_get_core (proxy);
    struct pw_core *pw_core = wp_core_get_pw_core (core);

    /* no pw_core -> we are not connected */
    if (!pw_core) {
      wp_proxy_augment_error (proxy, g_error_new (WP_DOMAIN_LIBRARY,
              WP_LIBRARY_ERROR_OPERATION_FAILED,
              "The WirePlumber core is not connected; "
              "object cannot be exported to PipeWire"));
      return;
    }

    /* export to get a proxy; feature will complete
         when the pw_proxy.bound event will be called. */
    wp_proxy_set_pw_proxy (proxy, pw_core_export (pw_core,
            SPA_TYPE_INTERFACE_Device,
            wp_properties_peek_dict (self->properties),
            self->interface, 0));
  }

  if (features & WP_SPA_DEVICE_FEATURE_ACTIVE) {
    gint res = spa_device_add_listener (self->interface, &self->listener,
        &spa_device_events, self);
    if (res < 0) {
      wp_proxy_augment_error (proxy, g_error_new (WP_DOMAIN_LIBRARY,
              WP_LIBRARY_ERROR_OPERATION_FAILED,
              "failed to initialize device: %s", spa_strerror (res)));
    }
  }
}

static gconstpointer
wp_spa_device_get_info (WpProxy * proxy)
{
  return NULL;
}

static WpProperties *
wp_spa_device_get_properties (WpProxy * proxy)
{
  WpSpaDevice *self = WP_SPA_DEVICE (proxy);
  return wp_properties_ref (self->properties);
}

static gint
wp_spa_device_enum_params (WpProxy * proxy, guint32 id, guint32 start,
    guint32 num, const struct spa_pod *filter)
{
  WpSpaDevice *self = WP_SPA_DEVICE (proxy);
  int device_enum_params_result;

  device_enum_params_result = spa_device_enum_params (self->interface,
      0, id, start, num, filter);
  g_warn_if_fail (device_enum_params_result >= 0);

  return device_enum_params_result;
}

static gint
wp_spa_device_set_param (WpProxy * proxy, guint32 id, guint32 flags,
    const struct spa_pod *param)
{
  WpSpaDevice *self = WP_SPA_DEVICE (proxy);
  int device_set_param_result;

  device_set_param_result = spa_device_set_param (self->interface,
      id, flags, param);
  g_warn_if_fail (device_set_param_result >= 0);

  return device_set_param_result;
}

static void
wp_spa_device_class_init (WpSpaDeviceClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpProxyClass *proxy_class = (WpProxyClass *) klass;

  object_class->finalize = wp_spa_device_finalize;

  proxy_class->augment = wp_spa_device_augment;

  proxy_class->get_info = wp_spa_device_get_info;
  proxy_class->get_properties = wp_spa_device_get_properties;
  proxy_class->enum_params = wp_spa_device_enum_params;
  proxy_class->set_param = wp_spa_device_set_param;

  /**
   * WpSpaDevice::object-info:
   * @self: the #WpSpaDevice
   * @id: the id of the managed object
   * @type: the #WpProxy subclass type that the managed object should have,
   *   or %G_TYPE_NONE if the object is being destroyed
   * @factory: (nullable): the name of the SPA factory to use to construct
   *    the managed object, or %NULL if the object is being destroyed
   * @properties: (nullable): additional properties that the managed object
   *    should have, or %NULL if the object is being destroyed
   * @parent_props: the properties of the device itself
   *
   * This signal is emitted when the device is creating or destroying a managed
   * object. The handler is expected to actually construct or destroy the
   * object using the requested SPA @factory and with the given @properties.
   *
   * The handler may also use @parent_props to enrich the properties set
   * that will be assigned on the object. @parent_props contains all the
   * properties that this device object has.
   *
   * When the object is being created, @type can either be %WP_TYPE_DEVICE
   * or %WP_TYPE_NODE. The handler is free to create a substitute of those,
   * like %WP_TYPE_SPA_DEVICE instead of %WP_TYPE_DEVICE, depending on the
   * use case.
   */
  spa_device_signals[SIGNAL_OBJECT_INFO] = g_signal_new (
      "object-info", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_FIRST,
      0, NULL, NULL, NULL, G_TYPE_NONE, 5, G_TYPE_UINT, G_TYPE_GTYPE,
      G_TYPE_STRING, WP_TYPE_PROPERTIES, WP_TYPE_PROPERTIES);
}

/**
 * wp_spa_device_new_from_spa_factory:
 * @core: the wireplumber core
 * @factory_name: the name of the SPA factory
 * @properties: (nullable) (transfer full): properties to be passed to device
 *    constructor
 *
 * Constructs a `SPA_TYPE_INTERFACE_Device` by loading the given SPA
 * @factory_name.
 *
 * To export this device to the PipeWire server, you need to call
 * wp_proxy_augment() requesting %WP_PROXY_FEATURE_BOUND and
 * wait for the operation to complete.
 *
 * Returns: (nullable) (transfer full): A new #WpSpaDevice wrapping the
 *   device that was constructed by the factory, or %NULL if the factory
 *   does not exist or was unable to construct the device
 */
WpSpaDevice *
wp_spa_device_new_from_spa_factory (WpCore * core,
    const gchar * factory_name, WpProperties * properties)
{
  g_autoptr (WpProperties) props = properties;
  struct pw_context *pw_context = wp_core_get_pw_context (core);
  g_autoptr (WpSpaDevice) self = NULL;
  gint res;

  g_return_val_if_fail (pw_context != NULL, NULL);

  self = g_object_new (WP_TYPE_SPA_DEVICE, "core", core, NULL);

  /* Load the monitor handle */
  self->handle = pw_context_load_spa_handle (pw_context,
      factory_name, props ? wp_properties_peek_dict (props) : NULL);
  if (!self->handle) {
    g_warning ("SPA handle '%s' could not be loaded; is it installed?",
        factory_name);
    return NULL;
  }

  /* Get the handle interface */
  res = spa_handle_get_interface (self->handle, SPA_TYPE_INTERFACE_Device,
      (gpointer *) &self->interface);
  if (res < 0) {
    g_warning ("Could not get device interface from SPA handle: %s",
        spa_strerror (res));
    return NULL;
  }

  self->properties = props ?
      g_steal_pointer (&props) : wp_properties_new_empty ();

  return g_steal_pointer (&self);
}
