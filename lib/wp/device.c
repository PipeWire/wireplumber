/* WirePlumber
 *
 * Copyright © 2019-2020 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "device.h"
#include "node.h"
#include "core.h"
#include "log.h"
#include "error.h"
#include "private/client-context.h"
#include "private/pipewire-object-mixin.h"

#include <pipewire/impl.h>
#include <spa/debug/types.h>
#include <spa/monitor/device.h>
#include <spa/utils/result.h>

WP_DEFINE_LOCAL_LOG_TOPIC ("wp-device")

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
  g_autoptr (WpSpaPod) p = param;
  return pw_device_set_param (d->iface, id, flags,
      wp_spa_pod_get_spa_pod (p));
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
  struct spa_param_info *info_params;
  guint info_n_params;
  GPtrArray *last_enum_params;
  GPtrArray *managed_objs;
  GPtrArray *pending_obj_config;
};

/* the spa_device lives in the client context when there is one, so it runs on
   another thread; see private/client-context.h */
static inline void
wp_spa_device_lock (WpSpaDevice * self)
{
  WpClientContext *ctx = wp_proxy_get_client_context (WP_PROXY (self));
  if (ctx)
    wp_client_context_lock (ctx);
}

static inline void
wp_spa_device_unlock (WpSpaDevice * self)
{
  WpClientContext *ctx = wp_proxy_get_client_context (WP_PROXY (self));
  if (ctx)
    wp_client_context_unlock (ctx);
}

/*
 * TRUE if the spa_device event we are currently handling arrived on the
 * client context's loop thread, so it has to be handed over to the main
 * thread.
 * FALSE when we are the main thread having called into the device with the
 * loop lock held, in which case running the handler directly is both correct
 * and required by the callers that expect synchronous behaviour.
 */
static inline gboolean
wp_spa_device_needs_marshalling (WpSpaDevice * self)
{
  WpClientContext *ctx = wp_proxy_get_client_context (WP_PROXY (self));
  return ctx && wp_client_context_in_thread (ctx);
}

/* takes ownership of @data, which must start with a WpSpaDevice* field */
static void
wp_spa_device_invoke_main (WpSpaDevice * self, GSourceFunc func, gpointer data,
    GDestroyNotify destroy)
{
  WpClientContext *ctx = wp_proxy_get_client_context (WP_PROXY (self));
  *((WpSpaDevice **) data) = g_object_ref (self);
  wp_client_context_invoke_main (ctx, func, data, destroy);
}

enum {
  PROP_0,
  PROP_SPA_DEVICE_HANDLE,
  PROP_PROPERTIES,
  N_PROPS,
};

enum
{
  SIGNAL_CREATE_OBJECT,
  SIGNAL_OBJECT_REMOVED,
  SIGNAL_EVENT,
  SIGNAL_PARAMS_CHANGED,
  SPA_DEVICE_LAST_SIGNAL,
};

static guint spa_device_signals[SPA_DEVICE_LAST_SIGNAL] = { 0 };
static GParamSpec *properties[N_PROPS] = { NULL, };

G_DEFINE_TYPE (WpSpaDevice, wp_spa_device, WP_TYPE_PROXY)

static void
object_unref_safe (gpointer object)
{
  if (object)
    g_object_unref (object);
}

static void
pod_unref_safe (gpointer object)
{
  if (object)
    wp_spa_pod_unref (object);
}

static void
wp_spa_device_init (WpSpaDevice * self)
{
  self->properties = wp_properties_new_empty ();
  self->last_enum_params = g_ptr_array_new_with_free_func (
      (GDestroyNotify)wp_spa_pod_unref);
  self->managed_objs = g_ptr_array_new_with_free_func (object_unref_safe);
  self->pending_obj_config = g_ptr_array_new_with_free_func (pod_unref_safe);
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
  wp_spa_device_lock (self);
  g_clear_pointer (&self->handle, pw_unload_spa_handle);
  wp_spa_device_unlock (self);

  g_clear_pointer (&self->properties, wp_properties_unref);
  g_clear_pointer (&self->last_enum_params, g_ptr_array_unref);
  g_clear_pointer (&self->managed_objs, g_ptr_array_unref);
  g_clear_pointer (&self->pending_obj_config, g_ptr_array_unref);

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

/* always runs on the main thread */
static void
on_device_info (WpSpaDevice * self, guint64 change_mask,
    WpProperties * props, const struct spa_param_info * params, guint n_params)
{
  /*
   * This gives us additional properties about the device, like the
   * "api.alsa.card.*" ones that are not set by the monitor. The spa device
   * emits it before object_info, and the queue preserves that order, so it is
   * still applied before the create-object handlers run.
   */
  if (change_mask & SPA_DEVICE_CHANGE_MASK_PROPS)
    wp_properties_update (self->properties, props);

  /* Emit params-changed when the params have changed */
  if (change_mask & SPA_DEVICE_CHANGE_MASK_PARAMS) {
    for (guint32 i = 0; i < n_params; i++) {
      if (!self->info_params || i >= self->info_n_params ||
          params[i].flags != self->info_params[i].flags) {
        const gchar *name;
        name = wp_spa_id_value_short_name (wp_spa_id_value_from_number (
            "Spa:Enum:ParamId", params[i].id));
        g_signal_emit_by_name (self, "params-changed", name);
      }
    }
  }

  /* Update cached params */
  self->info_params = g_realloc (self->info_params,
      n_params * sizeof(struct spa_param_info));
  memcpy (self->info_params, params, n_params * sizeof(struct spa_param_info));
  self->info_n_params = n_params;
}

typedef struct {
  WpSpaDevice *self;
  guint64 change_mask;
  WpProperties *props;
  struct spa_param_info *params;
  guint n_params;
} DeviceInfoEvent;

static void
device_info_event_free (gpointer data)
{
  DeviceInfoEvent *e = data;
  g_clear_object (&e->self);
  g_clear_pointer (&e->props, wp_properties_unref);
  g_free (e->params);
  g_free (e);
}

static gboolean
dispatch_device_info (gpointer data)
{
  DeviceInfoEvent *e = data;
  on_device_info (e->self, e->change_mask, e->props, e->params, e->n_params);
  return G_SOURCE_REMOVE;
}

static void
spa_device_event_info (void *data, const struct spa_device_info *info)
{
  WpSpaDevice *self = WP_SPA_DEVICE (data);

  if (wp_spa_device_needs_marshalling (self)) {
    /* the spa_device_info is only valid for the duration of this call */
    DeviceInfoEvent *e = g_new0 (DeviceInfoEvent, 1);
    e->change_mask = info->change_mask;
    if (info->change_mask & SPA_DEVICE_CHANGE_MASK_PROPS)
      e->props = wp_properties_new_copy_dict (info->props);
    e->n_params = info->n_params;
    e->params = g_memdup2 (info->params,
        info->n_params * sizeof (struct spa_param_info));
    wp_spa_device_invoke_main (self, dispatch_device_info, e,
        device_info_event_free);
  } else {
    g_autoptr (WpProperties) props =
        (info->change_mask & SPA_DEVICE_CHANGE_MASK_PROPS) ?
            wp_properties_new_wrap_dict (info->props) : NULL;
    on_device_info (self, info->change_mask, props, info->params,
        info->n_params);
  }
}

static void
spa_device_event_result (void *data, int seq, int res, uint32_t type,
    const void *result)
{
  WpSpaDevice *self = WP_SPA_DEVICE (data);
  const struct spa_result_device_params *r = result;

  if (type == SPA_RESULT_TYPE_DEVICE_PARAMS && seq == 1) {
    g_autoptr (WpSpaPod) pod_param = NULL;
    g_autoptr (WpSpaPod) pod_param_copy = NULL;

    pod_param = wp_spa_pod_new_wrap (r->param);
    pod_param_copy = wp_spa_pod_copy (pod_param);

    g_ptr_array_add (self->last_enum_params, g_steal_pointer (&pod_param_copy));
  }
}

static WpSpaPod *
pending_obj_config_pop (WpSpaDevice *self, guint32 id)
{
  if (id < self->pending_obj_config->len)
    return g_steal_pointer (&g_ptr_array_index (self->pending_obj_config, id));
  return NULL;
}

static void
pending_obj_config_set (WpSpaDevice *self, guint32 id, WpSpaPod *props)
{
  if (id >= self->pending_obj_config->len)
    g_ptr_array_set_size (self->pending_obj_config, id + 1);

  gpointer *ptr = &g_ptr_array_index (self->pending_obj_config, id);
  pod_unref_safe (*ptr);
  *ptr = props;
}

static void
append_props (WpSpaPodBuilder *b, WpSpaPod *props, GHashTable *used)
{
  g_autoptr (WpIterator) it = wp_spa_pod_new_iterator (props);
  GValue next = G_VALUE_INIT;

  for (; wp_iterator_next (it, &next); g_value_unset (&next)) {
    WpSpaPod *p = g_value_get_boxed (&next);
    const char *key;
    g_autoptr (WpSpaPod) value = NULL;

    if (!wp_spa_pod_get_property (p, &key, &value))
      continue;
    if (g_hash_table_contains(used, key))
      continue;

    wp_spa_pod_builder_add_property (b, key);
    wp_spa_pod_builder_add_pod (b, value);

    g_hash_table_add (used, (gpointer) g_strdup (key));
  }
}

static WpSpaPod *
merge_props (WpSpaPod *old_props, WpSpaPod *new_props)
{
  g_autoptr (GHashTable) used = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  g_autoptr (WpSpaPodBuilder) b = wp_spa_pod_builder_new_object (
      "Spa:Pod:Object:Param:Props", "Props");

  if (new_props) {
    append_props (b, new_props, used);
    wp_spa_pod_unref (new_props);
  }
  if (old_props) {
    append_props (b, old_props, used);
    wp_spa_pod_unref (old_props);
  }

  return wp_spa_pod_builder_end (b);
}

/* always runs on the main thread */
static void
on_device_event (WpSpaDevice * self, WpSpaPod * pod)
{
  guint32 id = -1;
  const gchar *type = NULL;
  g_autoptr (WpSpaPod) props = NULL;
  g_autoptr (GObject) child = NULL;

  if (wp_spa_pod_get_object (pod, &type,
          "Object", "i", &id,
          "Props", "?P", &props,
          NULL))
    child = wp_spa_device_get_managed_object (self, id);

  if (!g_strcmp0 (type, "ObjectConfig") && props) {
    if (child && WP_IS_PIPEWIRE_OBJECT (child)) {
      wp_pipewire_object_set_param (WP_PIPEWIRE_OBJECT (child), "Props", 0,
          g_steal_pointer (&props));
    } else if (!child) {
      /* Save Props set on ids pending for a managed object */
      WpSpaPod *pending_props = pending_obj_config_pop (self, id);
      if (pending_props) {
        pending_props = merge_props (pending_props, g_steal_pointer(&props));
        pending_obj_config_set (self, id, pending_props);
      }
    }
  }

  g_signal_emit (self, spa_device_signals[SIGNAL_EVENT], 0, pod);
}

typedef struct {
  WpSpaDevice *self;
  WpSpaPod *pod;
} DeviceEvent;

static void
device_event_free (gpointer data)
{
  DeviceEvent *e = data;
  g_clear_object (&e->self);
  g_clear_pointer (&e->pod, wp_spa_pod_unref);
  g_free (e);
}

static gboolean
dispatch_device_event (gpointer data)
{
  DeviceEvent *e = data;
  on_device_event (e->self, e->pod);
  return G_SOURCE_REMOVE;
}

static void
spa_device_event_event (void *data, const struct spa_event *event)
{
  WpSpaDevice *self = WP_SPA_DEVICE (data);
  g_autoptr (WpSpaPod) pod =
      wp_spa_pod_new_wrap_const ((const struct spa_pod *) event);

  wp_trace_boxed (WP_TYPE_SPA_POD, pod, "device event");

  if (wp_spa_device_needs_marshalling (self)) {
    /* the event pod is only valid for the duration of this call */
    DeviceEvent *e = g_new0 (DeviceEvent, 1);
    e->pod = wp_spa_pod_copy (pod);
    wp_spa_device_invoke_main (self, dispatch_device_event, e,
        device_event_free);
  } else {
    on_device_event (self, pod);
  }
}

/* always runs on the main thread */
static void
on_device_object_info (WpSpaDevice * self, guint32 id, const gchar * type,
    const gchar * factory_name, WpProperties * props)
{
  if (type) {
    wp_debug_object (self, "object info: id:%u type:%s factory:%s",
        id, type, factory_name);

    if (id < self->managed_objs->len &&
        g_ptr_array_index (self->managed_objs, id) != NULL) {
      wp_debug_object (self, "object already exists, removing");
      g_signal_emit (self, spa_device_signals[SIGNAL_OBJECT_REMOVED], 0, id);
      wp_spa_device_store_managed_object (self, id, NULL);
    }

    g_signal_emit (self, spa_device_signals[SIGNAL_CREATE_OBJECT], 0,
        id, type, factory_name, props);
  }
  else {
    wp_debug_object (self, "object removed: id:%u", id);
    g_signal_emit (self, spa_device_signals[SIGNAL_OBJECT_REMOVED], 0, id);
    wp_spa_device_store_managed_object (self, id, NULL);
  }
}

typedef struct {
  WpSpaDevice *self;
  guint32 id;
  gchar *type;
  gchar *factory_name;
  WpProperties *props;
} DeviceObjectInfoEvent;

static void
device_object_info_event_free (gpointer data)
{
  DeviceObjectInfoEvent *e = data;
  g_clear_object (&e->self);
  g_free (e->type);
  g_free (e->factory_name);
  g_clear_pointer (&e->props, wp_properties_unref);
  g_free (e);
}

static gboolean
dispatch_device_object_info (gpointer data)
{
  DeviceObjectInfoEvent *e = data;
  on_device_object_info (e->self, e->id, e->type, e->factory_name, e->props);
  return G_SOURCE_REMOVE;
}

static void
spa_device_event_object_info (void *data, uint32_t id,
    const struct spa_device_object_info *info)
{
  WpSpaDevice *self = WP_SPA_DEVICE (data);
  const gchar *type = info ? spa_debug_type_short_name (info->type) : NULL;

  if (wp_spa_device_needs_marshalling (self)) {
    /* the spa_device_object_info is only valid for the duration of this call.
       Note that add/remove pairs for the same id must keep their relative
       order, which the client context's queue guarantees. */
    DeviceObjectInfoEvent *e = g_new0 (DeviceObjectInfoEvent, 1);
    e->id = id;
    if (info) {
      e->type = g_strdup (type);
      e->factory_name = g_strdup (info->factory_name);
      e->props = wp_properties_new_copy_dict (info->props);
    }
    wp_spa_device_invoke_main (self, dispatch_device_object_info, e,
        device_object_info_event_free);
  } else {
    g_autoptr (WpProperties) props =
        info ? wp_properties_new_copy_dict (info->props) : NULL;
    on_device_object_info (self, id, type,
        info ? info->factory_name : NULL, props);
  }
}

static const struct spa_device_events spa_device_events = {
  SPA_VERSION_DEVICE_EVENTS,
  .info = spa_device_event_info,
  .result =  spa_device_event_result,
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

  /* constructed() leaves this NULL if the handle has no Device interface;
     fail the activation instead of crashing further down */
  if (!self->device) {
    wp_transition_return_error (WP_TRANSITION (transition),
        g_error_new (WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_OPERATION_FAILED,
            "SPA handle does not implement a device interface"));
    return;
  }

  switch (step) {
  case STEP_EXPORT: {
    g_autoptr (WpCore) core = wp_object_get_core (object);
    WpClientContext *ctx = wp_proxy_get_client_context (WP_PROXY (self));
    /* the device must be exported on the connection of the context it lives
       in, the same way as WpImplNode */
    struct pw_core *pw_core = ctx ?
        wp_client_context_get_pw_core (ctx) : wp_core_get_pw_core (core);
    g_return_if_fail (pw_core);

    /* export and start listening in one locked section, so that the loop
       thread cannot deliver the "bound" event before we are listening */
    wp_spa_device_lock (self);
    wp_proxy_set_pw_proxy (WP_PROXY (self),
        pw_core_export (pw_core, SPA_TYPE_INTERFACE_Device,
            wp_properties_peek_dict (self->properties),
            self->device, 0));
    wp_spa_device_unlock (self);
    break;
  }
  case STEP_ADD_DEVICE_LISTENER: {
    gint res;

    /* the device emits "info" and the initial burst of "object_info" from
       within this call, on this thread; those are handled directly */
    wp_spa_device_lock (self);
    res = spa_device_add_listener (self->device, &self->listener,
        &spa_device_events, self);
    wp_spa_device_unlock (self);

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

    wp_spa_device_lock (self);
    spa_hook_remove (&self->listener);
    wp_spa_device_unlock (self);

    g_clear_pointer (&self->info_params, g_free);
    self->info_n_params = 0;
    g_ptr_array_set_size (self->last_enum_params, 0);
    g_ptr_array_set_size (self->managed_objs, 0);
    g_ptr_array_set_size (self->pending_obj_config, 0);
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
 * \remarks When the "client-context" component is loaded, which is the case in
 * the default daemon configuration, the SPA handle is loaded in a secondary
 * `pw_context` that runs on its own thread, so that the device is not affected
 * by anything that blocks WirePlumber's main loop. The WpSpaDevice itself stays
 * on the thread that created it: all the signals below are emitted there, in
 * the order in which the `spa_device` emitted them, and
 * wp_spa_device_enum_params_sync() and wp_spa_device_set_param() remain
 * synchronous. Note, though, that the signals are then emitted asynchronously,
 * so a handler may run after the call that caused the event has returned.
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
 * \par object-removed
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

  properties[PROP_SPA_DEVICE_HANDLE] = g_param_spec_pointer ("spa-device-handle", "spa-device-handle",
      "The spa device handle",
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  properties[PROP_PROPERTIES] = g_param_spec_boxed ("properties", "properties",
      "Properties of the device", WP_TYPE_PROPERTIES,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, properties);

  spa_device_signals[SIGNAL_CREATE_OBJECT] = g_signal_new (
      "create-object", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_FIRST,
      0, NULL, NULL, NULL, G_TYPE_NONE, 4, G_TYPE_UINT, G_TYPE_STRING,
      G_TYPE_STRING, WP_TYPE_PROPERTIES);

  spa_device_signals[SIGNAL_OBJECT_REMOVED] = g_signal_new (
      "object-removed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_FIRST,
      0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_UINT);

  spa_device_signals[SIGNAL_EVENT] = g_signal_new (
      "event", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_FIRST,
      0, NULL, NULL, NULL, G_TYPE_NONE, 1, WP_TYPE_SPA_POD);

  spa_device_signals[SIGNAL_PARAMS_CHANGED] = g_signal_new (
      "params-changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_FIRST,
      0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_STRING);
}

/*!
 * \brief Constructs an SPA Device object from an existing device handle.
 *
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
  /* the handle is loaded into the client context, so that the device runs on
     its own thread; pw_core_export() later ties it to that connection */
  g_autoptr (WpClientContext) ctx = wp_client_context_find (core);
  struct pw_context *pw_context = ctx ?
      wp_client_context_get_pw_context (ctx) : wp_core_get_pw_context (core);
  struct spa_handle *handle = NULL;
  WpSpaDevice *self = NULL;

  g_return_val_if_fail (pw_context != NULL, NULL);

  if (ctx)
    wp_client_context_lock (ctx);

  /* Load the monitor handle */
  handle = pw_context_load_spa_handle (pw_context, factory_name,
      props ? wp_properties_peek_dict (props) : NULL);
  if (!handle) {
    wp_notice ("SPA handle '%s' could not be loaded; is it installed?",
        factory_name);
    goto out;
  }

  /* construct while still holding the lock: wp_spa_device_constructed() calls
     into the handle, which the loop thread may otherwise be running already */
  self = wp_spa_device_new_wrap (core, handle, g_steal_pointer (&props));
  if (self)
    wp_proxy_set_client_context (WP_PROXY (self), ctx);

out:
  if (ctx)
    wp_client_context_unlock (ctx);

  return self;
}

/*!
 * \brief Gets the properties of this device.
 *
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
 * \brief This method can be used to retrieve object parameters of the spa
 * device synchronously because the spa device always runs locally.
 *
 * \ingroup wpspadevice
 * \param self the spa device
 * \param id the parameter id to enumerate
 * \param filter (nullable): a param filter or NULL
 * \returns (transfer full) (nullable): an iterator to iterate over cached
 *    parameters, or NULL if parameters for this \a id are not cached;
 *    the items in the iterator are WpSpaPod
 * \since 0.5.15
 */
WpIterator *
wp_spa_device_enum_params_sync (WpSpaDevice * self,
    const gchar * id, WpSpaPod * filter)
{
  g_autoptr (GPtrArray) params = NULL;
  WpSpaIdValue param_id;
  guint32 id_val;
  const struct spa_pod *f;

  g_return_val_if_fail (WP_IS_SPA_DEVICE (self), NULL);
  g_return_val_if_fail (id, NULL);

  /* Translate the id */
  param_id = wp_spa_id_value_from_short_name ("Spa:Enum:ParamId", id);
  if (!param_id)
    return NULL;
  id_val = wp_spa_id_value_number (param_id);

  /* Clear the last enum params */
  g_ptr_array_set_size (self->last_enum_params, 0);

  f = filter ? wp_spa_pod_get_spa_pod (filter) : NULL;

  /* the results are collected by spa_device_event_result() in this call
     stack, which is what keeps this synchronous even when the device runs on
     the client context's thread */
  wp_spa_device_lock (self);
  spa_device_enum_params (self->device, 1, id_val, 0, -1, f);
  wp_spa_device_unlock (self);

  params = g_ptr_array_copy (self->last_enum_params, (GCopyFunc)wp_spa_pod_ref,
      NULL);
  g_ptr_array_set_size (self->last_enum_params, 0);
  return wp_iterator_new_ptr_array (g_steal_pointer (&params), WP_TYPE_SPA_POD);
}

/*!
 * \brief Sets a parameter on the spa device.
 *
 * \ingroup wpspadevice
 * \param self the pipewire object
 * \param id the parameter id to set
 * \param flags optional flags or 0
 * \param param (transfer full): the parameter to set
 * \returns TRUE on success, FALSE if setting the param failed
 * \since 0.5.15
 */
gboolean
wp_spa_device_set_param (WpSpaDevice * self,
    const gchar * id, guint32 flags, WpSpaPod * param)
{
  WpSpaIdValue param_id;
  guint32 id_val;
  const struct spa_pod *p;
  gint res;

  g_return_val_if_fail (WP_IS_SPA_DEVICE (self), FALSE);
  g_return_val_if_fail (id, FALSE);
  g_return_val_if_fail (param, FALSE);

  /* Translate the id */
  param_id = wp_spa_id_value_from_short_name ("Spa:Enum:ParamId", id);
  if (!param_id)
    return FALSE;
  id_val = wp_spa_id_value_number (param_id);

  p = wp_spa_pod_get_spa_pod (param);

  wp_spa_device_lock (self);
  res = spa_device_set_param (self->device, id_val, flags, p);
  wp_spa_device_unlock (self);

  return res >= 0;
}

/*!
 * \brief Iterates through all the objects managed by this device.
 *
 * \ingroup wpspadevice
 * \param self the spa device
 * \returns (transfer full): a WpIterator that iterates over all the objects
 *   managed by this device
 * \since 0.4.11
 */
WpIterator *
wp_spa_device_new_managed_object_iterator (WpSpaDevice * self)
{
  g_return_val_if_fail (WP_IS_SPA_DEVICE (self), NULL);
  return wp_iterator_new_ptr_array (g_ptr_array_ref (self->managed_objs),
      G_TYPE_OBJECT);
}

/*!
 * \brief Gets one of the objects managed by this device.
 *
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
 * \brief Stores or removes a managed object into/from a device.
 *
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

  /* Clear pending status, and set pending props if any */
  g_autoptr(WpSpaPod) props = pending_obj_config_pop (self, id);

  if (props && object && WP_IS_PIPEWIRE_OBJECT (object)) {
    wp_trace_boxed (WP_TYPE_SPA_POD, props, "pending ObjectConfig, object %d", id);
    wp_pipewire_object_set_param (WP_PIPEWIRE_OBJECT (object), "Props", 0,
          g_steal_pointer (&props));
  }
}

/*!
 * \brief Marks a managed object id pending.
 *
 * When an object id is pending, Props from received ObjectConfig events
 * for the id are saved. When \ref wp_spa_device_store_managed_object later sets
 * an object for the id, the saved Props are immediately set on the object and
 * pending status is cleared.
 *
 * If an object is already set for the id, this has no effect.
 *
 * \ingroup wpspadevice
 * \param self the spa device
 * \param id the (device-internal) id of the object
 */
void
wp_spa_device_set_managed_pending (WpSpaDevice * self, guint id)
{
  g_return_if_fail (WP_IS_SPA_DEVICE (self));

  g_autoptr (GObject) obj = wp_spa_device_get_managed_object (self, id);
  if (obj)
    return;

  pending_obj_config_set (self, id,
      wp_spa_pod_new_object ("Spa:Pod:Object:Param:Props", "Props", NULL));
}
