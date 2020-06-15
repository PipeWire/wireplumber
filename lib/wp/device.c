/* WirePlumber
 *
 * Copyright © 2019-2020 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * SECTION: WpDevice
 *
 * The #WpDevice class allows accessing the properties and methods of a
 * PipeWire device object (`struct pw_device`).
 *
 * A #WpDevice is constructed internally when a new device appears on the
 * PipeWire registry and it is made available through the #WpObjectManager API.
 * Alternatively, a #WpDevice can also be constructed using
 * wp_device_new_from_factory(), which creates a new device object
 * on the remote PipeWire server by calling into a factory.
 *
 * A #WpSpaDevice allows running a `spa_device` object locally,
 * loading the implementation from a SPA factory. This is useful to run device
 * monitors inside the session manager and have control over creating the
 * actual nodes that the `spa_device` requests to create.
 */

#define G_LOG_DOMAIN "wp-device"

#include "device.h"
#include "debug.h"
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

struct spa_param_info *
wp_device_get_param_info (WpProxy * self, guint * n_params)
{
  WpDevicePrivate *priv = wp_device_get_instance_private (WP_DEVICE (self));
  *n_params = priv->info->n_params;
  return priv->info->params;
}

static gint
wp_device_enum_params (WpProxy * self, guint32 id, guint32 start,
    guint32 num, WpSpaPod * filter)
{
  struct pw_device *pwp = (struct pw_device *) wp_proxy_get_pw_proxy (self);
  return pw_device_enum_params (pwp, 0, id, start, num,
      filter ? wp_spa_pod_get_spa_pod (filter) : NULL);
}

static gint
wp_device_subscribe_params (WpProxy * self, guint32 *ids, guint32 n_ids)
{
  struct pw_device *pwp = (struct pw_device *) wp_proxy_get_pw_proxy (self);
  return pw_device_subscribe_params (pwp, ids, n_ids);
}

static gint
wp_device_set_param (WpProxy * self, guint32 id, guint32 flags, WpSpaPod *param)
{
  struct pw_device *pwp = (struct pw_device *) wp_proxy_get_pw_proxy (self);
  return pw_device_set_param (pwp, id, flags,
      wp_spa_pod_get_spa_pod (param));
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

  if (info->change_mask & PW_DEVICE_CHANGE_MASK_PARAMS)
    g_object_notify (G_OBJECT (self), "param-info");
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
  proxy_class->get_param_info = wp_device_get_param_info;
  proxy_class->enum_params = wp_device_enum_params;
  proxy_class->subscribe_params = wp_device_subscribe_params;
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

  if (G_UNLIKELY (!pw_core)) {
    g_critical ("The WirePlumber core is not connected; "
        "device cannot be created");
    return NULL;
  }

  self = g_object_new (WP_TYPE_DEVICE, "core", core, NULL);
  wp_proxy_set_pw_proxy (WP_PROXY (self), pw_core_create_object (pw_core,
          factory_name, PW_TYPE_INTERFACE_Device, PW_VERSION_DEVICE,
          props ? wp_properties_peek_dict (props) : NULL, 0));
  return self;
}


enum {
  PROP_0,
  PROP_CORE,
  PROP_SPA_DEVICE_HANDLE,
  PROP_PROPERTIES,
};

struct _WpSpaDevice
{
  WpProxy parent;
  GWeakRef core;
  struct spa_handle *handle;
  struct spa_device *device;
  struct spa_hook listener;
  WpProperties *properties;
  struct pw_proxy *proxy;
  struct spa_hook proxy_listener;
};

enum
{
  SIGNAL_OBJECT_INFO,
  SPA_DEVICE_LAST_SIGNAL,
};

static guint spa_device_signals[SPA_DEVICE_LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (WpSpaDevice, wp_spa_device, G_TYPE_OBJECT)

static void
wp_spa_device_init (WpSpaDevice * self)
{
  g_weak_ref_init (&self->core, NULL);
  self->properties = wp_properties_new_empty ();
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
  g_weak_ref_clear (&self->core);

  G_OBJECT_CLASS (wp_spa_device_parent_class)->finalize (object);
}

static void
wp_spa_device_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpSpaDevice *self = WP_SPA_DEVICE (object);

  switch (property_id) {
  case PROP_CORE:
    g_weak_ref_set (&self->core, g_value_get_object (value));
    break;
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
  case PROP_CORE:
    g_value_take_object (value, g_weak_ref_get (&self->core));
    break;
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
  .object_info = spa_device_event_object_info
};

static void
wp_spa_device_class_init (WpSpaDeviceClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;

  object_class->constructed = wp_spa_device_constructed;
  object_class->finalize = wp_spa_device_finalize;
  object_class->set_property = wp_spa_device_set_property;
  object_class->get_property = wp_spa_device_get_property;

  g_object_class_install_property (object_class, PROP_CORE,
      g_param_spec_object ("core", "core", "The WpCore", WP_TYPE_CORE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_SPA_DEVICE_HANDLE,
      g_param_spec_pointer ("spa-device-handle", "spa-device-handle",
          "The spa device handle",
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_PROPERTIES,
      g_param_spec_boxed ("properties", "properties",
          "Properties of the device", WP_TYPE_PROPERTIES,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

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
 * wp_spa_device_new_wrap:
 * @core: the wireplumber core
 * @spa_device_handle: the spa device handle
 * @properties: (nullable) (transfer full): additional properties of the device
 *
 * Returns: (transfer full): A new #WpSpaDevice
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

guint32
wp_spa_device_get_bound_id (WpSpaDevice * self)
{
  g_return_val_if_fail (WP_IS_SPA_DEVICE (self), SPA_ID_INVALID);
  return self->proxy ? pw_proxy_get_bound_id (self->proxy) : SPA_ID_INVALID;
}

static void
proxy_event_bound (void *data, uint32_t global_id)
{
  GTask *task = G_TASK (data);
  WpSpaDevice *self = g_task_get_source_object (task);

  spa_hook_remove (&self->proxy_listener);
  g_task_return_boolean (task, TRUE);
  g_object_unref (task);
}

static const struct pw_proxy_events proxy_events = {
  PW_VERSION_PROXY_EVENTS,
  .bound = proxy_event_bound,
};

void
wp_spa_device_export (WpSpaDevice * self, GCancellable * cancellable,
    GAsyncReadyCallback callback, gpointer user_data)
{
  g_autoptr (GTask) task = NULL;

  g_return_if_fail (WP_IS_SPA_DEVICE (self));
  g_return_if_fail (!self->proxy);

  g_autoptr (WpCore) core = g_weak_ref_get (&self->core);
  struct pw_core *pw_core = wp_core_get_pw_core (core);

  g_return_if_fail (pw_core);

  task = g_task_new (self, cancellable, callback, user_data);
  self->proxy = pw_core_export (pw_core,
      SPA_TYPE_INTERFACE_Device,
      wp_properties_peek_dict (self->properties),
      self->device, 0);
  pw_proxy_add_listener (self->proxy, &self->proxy_listener,
      &proxy_events, g_steal_pointer (&task));
}

gboolean
wp_spa_device_export_finish (WpSpaDevice * self, GAsyncResult * res,
    GError ** error)
{
  g_return_val_if_fail (WP_IS_SPA_DEVICE (self), FALSE);
  g_return_val_if_fail (g_task_is_valid (res, self), FALSE);

  return g_task_propagate_boolean (G_TASK (res), error);
}

void
wp_spa_device_activate (WpSpaDevice * self)
{
  g_return_if_fail (WP_IS_SPA_DEVICE (self));

  gint res = spa_device_add_listener (self->device, &self->listener,
        &spa_device_events, self);
  if (res < 0)
    wp_warning_object (self, "failed to activate device: %s",
        spa_strerror (res));
}
