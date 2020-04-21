/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * SECTION: WpEndpoint
 *
 * The #WpEndpoint class allows accessing the properties and methods of a
 * PipeWire endpoint object (`struct pw_endpoint` from the session-manager
 * extension).
 *
 * A #WpEndpoint is constructed internally when a new endpoint appears on the
 * PipeWire registry and it is made available through the #WpObjectManager API.
 */

#define G_LOG_DOMAIN "wp-endpoint"

#include "endpoint.h"
#include "debug.h"
#include "session.h"
#include "private.h"
#include "error.h"
#include "wpenums.h"
#include "si-factory.h"

#include <pipewire/pipewire.h>
#include <pipewire/extensions/session-manager.h>
#include <pipewire/extensions/session-manager/introspect-funcs.h>

#include <spa/pod/builder.h>
#include <spa/pod/parser.h>
#include <spa/pod/filter.h>

enum {
  SIGNAL_CONTROL_CHANGED,
  N_SIGNALS,
};

static guint32 signals[N_SIGNALS] = {0};

/* WpEndpoint */

typedef struct _WpEndpointPrivate WpEndpointPrivate;
struct _WpEndpointPrivate
{
  WpProperties *properties;
  WpSpaProps spa_props;
  struct pw_endpoint_info *info;
  struct pw_endpoint *iface;
  struct spa_hook listener;
  WpObjectManager *streams_om;
};

G_DEFINE_TYPE_WITH_PRIVATE (WpEndpoint, wp_endpoint, WP_TYPE_PROXY)

static void
wp_endpoint_init (WpEndpoint * self)
{
}

static void
wp_endpoint_finalize (GObject * object)
{
  WpEndpoint *self = WP_ENDPOINT (object);
  WpEndpointPrivate *priv = wp_endpoint_get_instance_private (self);

  g_clear_object (&priv->streams_om);
  g_clear_pointer (&priv->properties, wp_properties_unref);
  g_clear_pointer (&priv->info, pw_endpoint_info_free);
  wp_spa_props_clear (&priv->spa_props);

  G_OBJECT_CLASS (wp_endpoint_parent_class)->finalize (object);
}

static void
wp_endpoint_enable_feature_streams (WpEndpoint * self, guint32 bound_id)
{
  WpEndpointPrivate *priv = wp_endpoint_get_instance_private (self);
  g_autoptr (WpCore) core = wp_proxy_get_core (WP_PROXY (self));
  GVariantBuilder b;

  /* proxy endpoint stream -> check for endpoint.id in global properties */
  g_variant_builder_init (&b, G_VARIANT_TYPE ("aa{sv}"));
  g_variant_builder_open (&b, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&b, "{sv}", "type",
      g_variant_new_int32 (WP_OBJECT_MANAGER_CONSTRAINT_PW_GLOBAL_PROPERTY));
  g_variant_builder_add (&b, "{sv}", "name",
      g_variant_new_string (PW_KEY_ENDPOINT_ID));
  g_variant_builder_add (&b, "{sv}", "value",
      g_variant_new_take_string (g_strdup_printf ("%u", bound_id)));
  g_variant_builder_close (&b);

  wp_object_manager_add_interest (priv->streams_om,
      WP_TYPE_ENDPOINT_STREAM,
      g_variant_builder_end (&b),
      WP_PROXY_FEATURES_STANDARD);

  /* impl endpoint stream -> check for endpoint.id in standard properties */
  g_variant_builder_init (&b, G_VARIANT_TYPE ("aa{sv}"));
  g_variant_builder_open (&b, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&b, "{sv}", "type",
      g_variant_new_int32 (WP_OBJECT_MANAGER_CONSTRAINT_PW_PROPERTY));
  g_variant_builder_add (&b, "{sv}", "name",
      g_variant_new_string (PW_KEY_ENDPOINT_ID));
  g_variant_builder_add (&b, "{sv}", "value",
      g_variant_new_take_string (g_strdup_printf ("%u", bound_id)));
  g_variant_builder_close (&b);

  wp_object_manager_add_interest (priv->streams_om,
      WP_TYPE_IMPL_ENDPOINT_STREAM,
      g_variant_builder_end (&b),
      WP_PROXY_FEATURES_STANDARD);

  wp_core_install_object_manager (core, priv->streams_om);
  wp_proxy_set_feature_ready (WP_PROXY (self), WP_ENDPOINT_FEATURE_STREAMS);
}

static void
wp_endpoint_augment (WpProxy * proxy, WpProxyFeatures features)
{
  /* call the parent impl first to ensure we have a pw proxy if necessary */
  WP_PROXY_CLASS (wp_endpoint_parent_class)->augment (proxy, features);

  if (features & WP_ENDPOINT_FEATURE_CONTROLS) {
    struct pw_endpoint *pw_proxy = NULL;
    uint32_t ids[] = { SPA_PARAM_Props };

    pw_proxy = (struct pw_endpoint *) wp_proxy_get_pw_proxy (proxy);
    if (!pw_proxy)
      return;

    pw_endpoint_enum_params (pw_proxy, 0, SPA_PARAM_PropInfo, 0, -1, NULL);
    pw_endpoint_subscribe_params (pw_proxy, ids, SPA_N_ELEMENTS (ids));
  }

  if (features & WP_ENDPOINT_FEATURE_STREAMS) {
    WpEndpointPrivate *priv =
        wp_endpoint_get_instance_private (WP_ENDPOINT (proxy));

    priv->streams_om = wp_object_manager_new ();

    /* if we are already bound, enable right away;
       else, continue in the bound() event */
    if (wp_proxy_get_features (proxy) & WP_PROXY_FEATURE_BOUND) {
      wp_endpoint_enable_feature_streams (WP_ENDPOINT (proxy),
          wp_proxy_get_bound_id (proxy));
    }
  }
}

static gconstpointer
wp_endpoint_get_info (WpProxy * proxy)
{
  WpEndpoint *self = WP_ENDPOINT (proxy);
  WpEndpointPrivate *priv = wp_endpoint_get_instance_private (self);

  return priv->info;
}

static WpProperties *
wp_endpoint_get_properties (WpProxy * proxy)
{
  WpEndpoint *self = WP_ENDPOINT (proxy);
  WpEndpointPrivate *priv = wp_endpoint_get_instance_private (self);

  return wp_properties_ref (priv->properties);
}

static gint
wp_endpoint_enum_params (WpProxy * self, guint32 id, guint32 start,
    guint32 num, const struct spa_pod *filter)
{
  WpEndpointPrivate *priv =
      wp_endpoint_get_instance_private (WP_ENDPOINT (self));
  int endpoint_enum_params_result;

  endpoint_enum_params_result = pw_endpoint_enum_params (priv->iface, 0, id,
      start, num, filter);
  g_warn_if_fail (endpoint_enum_params_result >= 0);

  return endpoint_enum_params_result;
}

static gint
wp_endpoint_subscribe_params (WpProxy * self, guint32 n_ids, guint32 *ids)
{
  WpEndpointPrivate *priv =
      wp_endpoint_get_instance_private (WP_ENDPOINT (self));
  int endpoint_subscribe_params_result;

  endpoint_subscribe_params_result = pw_endpoint_subscribe_params (priv->iface,
      ids, n_ids);
  g_warn_if_fail (endpoint_subscribe_params_result >= 0);

  return endpoint_subscribe_params_result;
}

static gint
wp_endpoint_set_param (WpProxy * self, guint32 id, guint32 flags,
    const struct spa_pod *param)
{
  WpEndpointPrivate *priv =
      wp_endpoint_get_instance_private (WP_ENDPOINT (self));
  int endpoint_set_param_result;

  endpoint_set_param_result = pw_endpoint_set_param (priv->iface, id, flags,
      param);
  g_warn_if_fail (endpoint_set_param_result >= 0);

  return endpoint_set_param_result;
}

static void
endpoint_event_info (void *data, const struct pw_endpoint_info *info)
{
  WpEndpoint *self = WP_ENDPOINT (data);
  WpEndpointPrivate *priv = wp_endpoint_get_instance_private (self);

  priv->info = pw_endpoint_info_update (priv->info, info);

  if (info->change_mask & PW_ENDPOINT_CHANGE_MASK_PROPS) {
    g_clear_pointer (&priv->properties, wp_properties_unref);
    priv->properties = wp_properties_new_wrap_dict (priv->info->props);
  }

  wp_proxy_set_feature_ready (WP_PROXY (self), WP_PROXY_FEATURE_INFO);
  g_object_notify (G_OBJECT (self), "info");

  if (info->change_mask & PW_ENDPOINT_CHANGE_MASK_PROPS)
    g_object_notify (G_OBJECT (self), "properties");
}

static const struct pw_endpoint_events endpoint_events = {
  PW_VERSION_ENDPOINT_EVENTS,
  .info = endpoint_event_info,
  .param = wp_proxy_handle_event_param,
};

static void
wp_endpoint_pw_proxy_created (WpProxy * proxy, struct pw_proxy * pw_proxy)
{
  WpEndpoint *self = WP_ENDPOINT (proxy);
  WpEndpointPrivate *priv = wp_endpoint_get_instance_private (self);

  priv->iface = (struct pw_endpoint *) pw_proxy;
  pw_endpoint_add_listener (priv->iface, &priv->listener, &endpoint_events,
      self);
}

static void
wp_endpoint_bound (WpProxy * proxy, guint32 id)
{
  WpEndpoint *self = WP_ENDPOINT (proxy);
  WpEndpointPrivate *priv = wp_endpoint_get_instance_private (self);

  if (priv->streams_om)
    wp_endpoint_enable_feature_streams (self, id);
}

static void
wp_endpoint_param (WpProxy * proxy, gint seq, guint32 id, guint32 index,
    guint32 next, const struct spa_pod *param)
{
  WpEndpoint *self = WP_ENDPOINT (proxy);
  WpEndpointPrivate *priv = wp_endpoint_get_instance_private (self);
  g_autoptr (GArray) changed_ids = NULL;
  guint32 prop_id;

  switch (id) {
  case SPA_PARAM_PropInfo:
    wp_spa_props_register_from_prop_info (&priv->spa_props, param);
    break;
  case SPA_PARAM_Props:
    changed_ids = g_array_new (FALSE, FALSE, sizeof (uint32_t));
    wp_spa_props_store_from_props (&priv->spa_props, param, changed_ids);

    for (guint i = 0; i < changed_ids->len; i++) {
      prop_id = g_array_index (changed_ids, uint32_t, i);
      g_signal_emit (self, signals[SIGNAL_CONTROL_CHANGED], 0, prop_id);
    }

    wp_proxy_set_feature_ready (WP_PROXY (self),
        WP_ENDPOINT_FEATURE_CONTROLS);
    break;
  }
}

static const gchar *
get_name (WpEndpoint * self)
{
  WpEndpointPrivate *priv = wp_endpoint_get_instance_private (self);
  return priv->info->name;
}

static const gchar *
get_media_class (WpEndpoint * self)
{
  WpEndpointPrivate *priv = wp_endpoint_get_instance_private (self);
  return priv->info->media_class;
}

static WpDirection
get_direction (WpEndpoint * self)
{
  WpEndpointPrivate *priv = wp_endpoint_get_instance_private (self);
  return priv->info->direction;
}

static const struct spa_pod *
get_control (WpEndpoint * self, guint32 control_id)
{
  WpEndpointPrivate *priv = wp_endpoint_get_instance_private (self);
  return wp_spa_props_get_stored (&priv->spa_props, control_id);
}

static gboolean
set_control (WpEndpoint * self, guint32 control_id,
    const struct spa_pod * pod)
{
  char buf[1024];
  struct spa_pod_builder b = SPA_POD_BUILDER_INIT (buf, sizeof (buf));

  /* our spa_props will be updated by the param event */

  WP_PROXY_GET_CLASS (self)->set_param (WP_PROXY (self), SPA_PARAM_Props, 0,
      spa_pod_builder_add_object (&b,
          SPA_TYPE_OBJECT_Props, SPA_PARAM_Props,
          control_id, SPA_POD_Pod (pod)));

  return TRUE;
}

static void
wp_endpoint_class_init (WpEndpointClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpProxyClass *proxy_class = (WpProxyClass *) klass;

  object_class->finalize = wp_endpoint_finalize;

  proxy_class->pw_iface_type = PW_TYPE_INTERFACE_Endpoint;
  proxy_class->pw_iface_version = PW_VERSION_ENDPOINT;

  proxy_class->augment = wp_endpoint_augment;
  proxy_class->get_info = wp_endpoint_get_info;
  proxy_class->get_properties = wp_endpoint_get_properties;
  proxy_class->enum_params = wp_endpoint_enum_params;
  proxy_class->subscribe_params = wp_endpoint_subscribe_params;
  proxy_class->set_param = wp_endpoint_set_param;

  proxy_class->pw_proxy_created = wp_endpoint_pw_proxy_created;
  proxy_class->bound = wp_endpoint_bound;
  proxy_class->param = wp_endpoint_param;

  klass->get_name = get_name;
  klass->get_media_class = get_media_class;
  klass->get_direction = get_direction;
  klass->get_control = get_control;
  klass->set_control = set_control;

  /**
   * WpEndpoint::control-changed:
   * @self: the endpoint
   * @control: the control that changed (a #WpEndpointControl)
   *
   * Emitted when an endpoint control changes value
   */
  signals[SIGNAL_CONTROL_CHANGED] = g_signal_new (
      "control-changed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_UINT);
}

/**
 * wp_endpoint_get_name:
 * @self: the endpoint
 *
 * Returns: the name of the endpoint
 */
const gchar *
wp_endpoint_get_name (WpEndpoint * self)
{
  g_return_val_if_fail (WP_IS_ENDPOINT (self), NULL);
  g_return_val_if_fail (WP_ENDPOINT_GET_CLASS (self)->get_name, NULL);

  return WP_ENDPOINT_GET_CLASS (self)->get_name (self);
}

/**
 * wp_endpoint_get_media_class:
 * @self: the endpoint
 *
 * Returns: the media class of the endpoint (ex. "Audio/Sink")
 */
const gchar *
wp_endpoint_get_media_class (WpEndpoint * self)
{
  g_return_val_if_fail (WP_IS_ENDPOINT (self), NULL);
  g_return_val_if_fail (WP_ENDPOINT_GET_CLASS (self)->get_media_class, NULL);

  return WP_ENDPOINT_GET_CLASS (self)->get_media_class (self);
}

/**
 * wp_endpoint_get_direction:
 * @self: the endpoint
 *
 * Returns: the direction of this endpoint
 */
WpDirection
wp_endpoint_get_direction (WpEndpoint * self)
{
  g_return_val_if_fail (WP_IS_ENDPOINT (self), 0);
  g_return_val_if_fail (WP_ENDPOINT_GET_CLASS (self)->get_direction, 0);

  return WP_ENDPOINT_GET_CLASS (self)->get_direction (self);
}

/**
 * wp_endpoint_get_control:
 * @self: the endpoint
 * @control_id: the control id (a #WpEndpointControl)
 *
 * Returns: (transfer none) (nullable): the `spa_pod` containing the value
 *   of this control, or %NULL if @control_id does not exist on this endpoint
 */
const struct spa_pod *
wp_endpoint_get_control (WpEndpoint * self, guint32 control_id)
{
  g_return_val_if_fail (WP_IS_ENDPOINT (self), NULL);
  g_return_val_if_fail (WP_ENDPOINT_GET_CLASS (self)->get_control, NULL);

  return WP_ENDPOINT_GET_CLASS (self)->get_control (self, control_id);
}

/**
 * wp_endpoint_get_control_boolean:
 * @self: the endpoint
 * @control_id: the control id (a #WpEndpointControl)
 * @value: (out): the boolean value of this control
 *
 * Returns: %TRUE on success, %FALSE if the control does not exist on this
 *   endpoint or if it is not a boolean
 */
gboolean
wp_endpoint_get_control_boolean (WpEndpoint * self, guint32 control_id,
    gboolean * value)
{
  const struct spa_pod *pod = wp_endpoint_get_control (self, control_id);
  bool val;
  if (pod && spa_pod_get_bool (pod, &val) == 0) {
    *value = val;
    return TRUE;
  }
  return FALSE;
}

/**
 * wp_endpoint_get_control_int:
 * @self: the endpoint
 * @control_id: the control id (a #WpEndpointControl)
 * @value: (out): the integer value of this control
 *
 * Returns: %TRUE on success, %FALSE if the control does not exist on this
 *   endpoint or if it is not an integer
 */
gboolean
wp_endpoint_get_control_int (WpEndpoint * self, guint32 control_id,
    gint * value)
{
  const struct spa_pod *pod = wp_endpoint_get_control (self, control_id);
  return (pod && spa_pod_get_int (pod, value) == 0);
}

/**
 * wp_endpoint_get_control_float:
 * @self: the endpoint
 * @control_id: the control id (a #WpEndpointControl)
 * @value: (out): the floating-point number value of this control
 *
 * Returns: %TRUE on success, %FALSE if the control does not exist on this
 *   endpoint or if it is not a floating-point number
 */
gboolean
wp_endpoint_get_control_float (WpEndpoint * self, guint32 control_id,
    gfloat * value)
{
  const struct spa_pod *pod = wp_endpoint_get_control (self, control_id);
  return (pod && spa_pod_get_float (pod, value) == 0);
}

/**
 * wp_endpoint_set_control:
 * @self: the endpoint
 * @control_id: the control id (a #WpEndpointControl)
 * @value: the new value for this control, as a `spa_pod`
 *
 * Returns: %TRUE on success, %FALSE if an error occurred
 */
gboolean
wp_endpoint_set_control (WpEndpoint * self, guint32 control_id,
    const struct spa_pod * value)
{
  g_return_val_if_fail (WP_IS_ENDPOINT (self), FALSE);
  g_return_val_if_fail (WP_ENDPOINT_GET_CLASS (self)->set_control, FALSE);

  return WP_ENDPOINT_GET_CLASS (self)->set_control (self, control_id, value);
}

/**
 * wp_endpoint_set_control_boolean:
 * @self: the endpoint
 * @control_id: the control id (a #WpEndpointControl)
 * @value: the new value for this control, as a boolean
 *
 * Returns: %TRUE on success, %FALSE if an error occurred
 */
gboolean
wp_endpoint_set_control_boolean (WpEndpoint * self, guint32 control_id,
    gboolean value)
{
  gchar buffer[512];
  return wp_endpoint_set_control (self, control_id, wp_spa_props_build_pod (
          buffer, sizeof (buffer), SPA_POD_Bool (value), 0));
}

/**
 * wp_endpoint_set_control_int:
 * @self: the endpoint
 * @control_id: the control id (a #WpEndpointControl)
 * @value: the new value for this control, as an integer
 *
 * Returns: %TRUE on success, %FALSE if an error occurred
 */
gboolean
wp_endpoint_set_control_int (WpEndpoint * self, guint32 control_id,
    gint value)
{
  gchar buffer[512];
  return wp_endpoint_set_control (self, control_id, wp_spa_props_build_pod (
          buffer, sizeof (buffer), SPA_POD_Int (value), 0));
}

/**
 * wp_endpoint_set_control_float:
 * @self: the endpoint
 * @control_id: the control id (a #WpEndpointControl)
 * @value: the new value for this control, as a floating-point number
 *
 * Returns: %TRUE on success, %FALSE if an error occurred
 */
gboolean
wp_endpoint_set_control_float (WpEndpoint * self, guint32 control_id,
    gfloat value)
{
  gchar buffer[512];
  return wp_endpoint_set_control (self, control_id, wp_spa_props_build_pod (
          buffer, sizeof (buffer), SPA_POD_Float (value), 0));
}

/**
 * wp_endpoint_get_n_streams:
 * @self: the endpoint
 *
 * Returns: the number of streams of this endpoint
 */
guint
wp_endpoint_get_n_streams (WpEndpoint * self)
{
  g_return_val_if_fail (WP_IS_ENDPOINT (self), 0);
  g_return_val_if_fail (wp_proxy_get_features (WP_PROXY (self)) &
          WP_ENDPOINT_FEATURE_STREAMS, 0);

  WpEndpointPrivate *priv = wp_endpoint_get_instance_private (self);
  return wp_object_manager_get_n_objects (priv->streams_om);
}

/**
 * wp_endpoint_find_stream:
 * @self: the endpoint
 * @bound_id: the bound id of the stream object to find
 *
 * Returns: (transfer full) (nullable): the endpoint stream that has the given
 *    @bound_id, or %NULL if there is no such stream
 */
WpEndpointStream *
wp_endpoint_find_stream (WpEndpoint * self, guint32 bound_id)
{
  g_return_val_if_fail (WP_IS_ENDPOINT (self), NULL);
  g_return_val_if_fail (wp_proxy_get_features (WP_PROXY (self)) &
          WP_ENDPOINT_FEATURE_STREAMS, NULL);

  WpEndpointPrivate *priv = wp_endpoint_get_instance_private (self);
  return (WpEndpointStream *)
      wp_object_manager_find_proxy (priv->streams_om, bound_id);
}

/**
 * wp_endpoint_iterate_streams:
 * @self: the endpoint
 *
 * Returns: (transfer full): a #WpIterator that iterates over all
 *   the endpoint streams that belong to this endpoint
 */
WpIterator *
wp_endpoint_iterate_streams (WpEndpoint * self)
{
  g_return_val_if_fail (WP_IS_ENDPOINT (self), NULL);
  g_return_val_if_fail (wp_proxy_get_features (WP_PROXY (self)) &
          WP_ENDPOINT_FEATURE_STREAMS, NULL);

  WpEndpointPrivate *priv = wp_endpoint_get_instance_private (self);
  return wp_object_manager_iterate (priv->streams_om);
}

/* WpImplEndpoint */

enum {
  IMPL_PROP_0,
  IMPL_PROP_ITEM,
};

struct _WpImplEndpoint
{
  WpEndpoint parent;

  struct spa_interface iface;
  struct spa_hook_list hooks;
  struct pw_endpoint_info info;
  gboolean subscribed;

  WpSiEndpoint *item;
};

G_DEFINE_TYPE (WpImplEndpoint, wp_impl_endpoint, WP_TYPE_ENDPOINT)

#define pw_endpoint_emit(hooks,method,version,...) \
    spa_hook_list_call_simple(hooks, struct pw_endpoint_events, \
        method, version, ##__VA_ARGS__)

#define pw_endpoint_emit_info(hooks,...)  pw_endpoint_emit(hooks, info, 0, ##__VA_ARGS__)
#define pw_endpoint_emit_param(hooks,...) pw_endpoint_emit(hooks, param, 0, ##__VA_ARGS__)

static struct spa_param_info impl_param_info[] = {
  SPA_PARAM_INFO (SPA_PARAM_Props, SPA_PARAM_INFO_READWRITE),
  SPA_PARAM_INFO (SPA_PARAM_PropInfo, SPA_PARAM_INFO_READ)
};

static int
impl_add_listener(void *object,
    struct spa_hook *listener,
    const struct pw_endpoint_events *events,
    void *data)
{
  WpImplEndpoint *self = WP_IMPL_ENDPOINT (object);
  struct spa_hook_list save;

  spa_hook_list_isolate (&self->hooks, &save, listener, events, data);

  self->info.change_mask = PW_ENDPOINT_CHANGE_MASK_ALL;
  pw_endpoint_emit_info (&self->hooks, &self->info);
  self->info.change_mask = 0;

  spa_hook_list_join (&self->hooks, &save);
  return 0;
}

static int
impl_enum_params (void *object, int seq,
    uint32_t id, uint32_t start, uint32_t num,
    const struct spa_pod *filter)
{
  WpImplEndpoint *self = WP_IMPL_ENDPOINT (object);
  WpEndpointPrivate *priv =
      wp_endpoint_get_instance_private (WP_ENDPOINT (self));
  char buf[1024];
  struct spa_pod_builder b = SPA_POD_BUILDER_INIT (buf, sizeof (buf));
  struct spa_pod *result;
  guint count = 0;

  switch (id) {
    case SPA_PARAM_PropInfo: {
      g_autoptr (GPtrArray) params =
          wp_spa_props_build_propinfo (&priv->spa_props, &b);

      for (guint i = start; i < params->len; i++) {
        struct spa_pod *param = g_ptr_array_index (params, i);

        if (spa_pod_filter (&b, &result, param, filter) == 0) {
          pw_endpoint_emit_param (&self->hooks, seq, id, i, i+1, result);
          wp_proxy_handle_event_param (self, seq, id, i, i+1, result);
          if (++count == num)
            break;
        }
      }
      break;
    }
    case SPA_PARAM_Props: {
      if (start == 0) {
        struct spa_pod *param = wp_spa_props_build_props (&priv->spa_props, &b);
        if (spa_pod_filter (&b, &result, param, filter) == 0) {
          pw_endpoint_emit_param (&self->hooks, seq, id, 0, 1, result);
          wp_proxy_handle_event_param (self, seq, id, 0, 1, result);
        }
      }
      break;
    }
    default:
      return -ENOENT;
  }

  return 0;
}

static int
impl_subscribe_params (void *object, uint32_t *ids, uint32_t n_ids)
{
  WpImplEndpoint *self = WP_IMPL_ENDPOINT (object);

  for (guint i = 0; i < n_ids; i++) {
    if (ids[i] == SPA_PARAM_Props)
      self->subscribed = TRUE;
    impl_enum_params (self, 1, ids[i], 0, UINT32_MAX, NULL);
  }
  return 0;
}

static int
impl_set_param (void *object, uint32_t id, uint32_t flags,
    const struct spa_pod *param)
{
  WpImplEndpoint *self = WP_IMPL_ENDPOINT (object);
  WpEndpointPrivate *priv =
      wp_endpoint_get_instance_private (WP_ENDPOINT (self));
  g_autoptr (GArray) changed_ids = NULL;

  if (id != SPA_PARAM_Props)
    return -ENOENT;

  changed_ids = g_array_new (FALSE, FALSE, sizeof (guint32));
  wp_spa_props_store_from_props (&priv->spa_props, param, changed_ids);

  /* notify subscribers */
  if (self->subscribed)
    impl_enum_params (self, 1, SPA_PARAM_Props, 0, UINT32_MAX, NULL);

  /* notify controls locally */
  for (guint i = 0; i < changed_ids->len; i++) {
    guint32 prop_id = g_array_index (changed_ids, guint32, i);
    g_signal_emit (self, signals[SIGNAL_CONTROL_CHANGED], 0, prop_id);
  }

  return 0;
}

static void
destroy_deconfigured_link (WpSessionItem * link, WpSiFlags flags, gpointer data)
{
  if (!(flags & WP_SI_FLAG_CONFIGURED))
    g_object_unref (link);
}

static void
on_si_link_exported (WpSessionItem * link, GAsyncResult * res, gpointer data)
{
  WpImplEndpoint *self = WP_IMPL_ENDPOINT (data);
  g_autoptr (GError) error = NULL;

  if (!wp_session_item_export_finish (link, res, &error)) {
    wp_warning_object (self, "failed to export link: %s", error->message);
    g_object_unref (link);
    return;
  }

  g_signal_connect (link, "flags-changed",
      G_CALLBACK (destroy_deconfigured_link), NULL);
}

static int
impl_create_link (void *object, const struct spa_dict *props)
{
  WpImplEndpoint *self = WP_IMPL_ENDPOINT (object);
  const gchar *self_ep, *self_stream, *peer_ep, *peer_stream;
  guint32 self_ep_id, self_stream_id, peer_ep_id, peer_stream_id;
  WpSiStream *self_si_stream = NULL;
  g_autoptr (WpSiStream) peer_si_stream = NULL;
  g_autoptr (WpSession) session = NULL;

  /* find the session */
  session = wp_session_item_get_associated_proxy (
      WP_SESSION_ITEM (self->item), WP_TYPE_SESSION);
  g_return_val_if_fail (!session, -ENAVAIL);

  if (self->info.direction == PW_DIRECTION_OUTPUT) {
    self_ep = spa_dict_lookup (props, PW_KEY_ENDPOINT_LINK_OUTPUT_ENDPOINT);
    self_stream = spa_dict_lookup (props, PW_KEY_ENDPOINT_LINK_OUTPUT_STREAM);
    peer_ep = spa_dict_lookup (props, PW_KEY_ENDPOINT_LINK_INPUT_ENDPOINT);
    peer_stream = spa_dict_lookup (props, PW_KEY_ENDPOINT_LINK_INPUT_STREAM);
  } else {
    self_ep = spa_dict_lookup (props, PW_KEY_ENDPOINT_LINK_INPUT_ENDPOINT);
    self_stream = spa_dict_lookup (props, PW_KEY_ENDPOINT_LINK_INPUT_STREAM);
    peer_ep = spa_dict_lookup (props, PW_KEY_ENDPOINT_LINK_OUTPUT_ENDPOINT);
    peer_stream = spa_dict_lookup (props, PW_KEY_ENDPOINT_LINK_OUTPUT_STREAM);
  }

  /* verify arguments */
  if (!peer_ep) {
    wp_warning_object (self,
        "a peer endpoint must be specified at the very least");
    return -EINVAL;
  }
  if (self_ep && ((guint32) atoi (self_ep))
          != wp_proxy_get_bound_id (WP_PROXY (self))) {
    wp_warning_object (self,
        "creating links for other endpoints is now allowed");
    return -EACCES;
  }

  /* convert to int - allow unspecified streams */
  self_ep_id = wp_proxy_get_bound_id (WP_PROXY (self));
  self_stream_id = self_stream ? atoi (self_stream) : SPA_ID_INVALID;
  peer_ep_id = atoi (peer_ep);
  peer_stream_id = peer_stream ? atoi (peer_stream) : SPA_ID_INVALID;

  /* find our stream */
  if (self_stream_id != SPA_ID_INVALID) {
    WpSiStream *tmp;
    guint32 tmp_id;

    for (guint i = 0; i < wp_si_endpoint_get_n_streams (self->item); i++) {
      tmp = wp_si_endpoint_get_stream (self->item, i);
      tmp_id = wp_session_item_get_associated_proxy_id (WP_SESSION_ITEM (tmp),
          WP_TYPE_ENDPOINT_STREAM);

      if (tmp_id == self_stream_id) {
        self_si_stream = tmp;
        break;
      }
    }
  } else {
    self_si_stream = wp_si_endpoint_get_stream (self->item, 0);
  }

  if (!self_si_stream) {
    wp_warning_object (self, "stream %d not found in %d", self_stream_id,
        self_ep_id);
    return -EINVAL;
  }

  /* find the peer stream */
  {
    g_autoptr (WpEndpoint) peer_ep_proxy = NULL;
    g_autoptr (WpEndpointStream) peer_stream_proxy = NULL;

    peer_ep_proxy = wp_session_find_endpoint (session, peer_ep_id);
    if (!peer_ep_proxy) {
      wp_warning_object (self, "endpoint %d not found in session", peer_ep_id);
      return -EINVAL;
    }

    if (peer_stream_id != SPA_ID_INVALID) {
      peer_stream_proxy = wp_endpoint_find_stream (peer_ep_proxy,
          peer_stream_id);
    } else {
      g_autoptr (WpIterator) it = wp_endpoint_iterate_streams (peer_ep_proxy);
      g_auto (GValue) val = G_VALUE_INIT;
      if (wp_iterator_next (it, &val))
        peer_stream_proxy = g_value_dup_object (&val);
    }

    if (!peer_stream_proxy) {
      wp_warning_object (self, "stream %d not found in %d", peer_stream_id,
          peer_ep_id);
      return -EINVAL;
    }

    if (!WP_IS_IMPL_ENDPOINT_LINK (peer_stream_proxy)) {
      /* TODO - if the stream is not implemented by our session manager,
        we can still make things work by calling the peer endpoint's
        create_link() and negotiating ports, while creating a dummy
        WpSiEndpoint / WpSiStream on our end to satisfy the API */
      return -ENAVAIL;
    }

    g_object_get (peer_stream_proxy, "item", &peer_si_stream, NULL);
  }

  /* create the link */
  {
    g_autoptr (WpSessionItem) link = NULL;
    g_autoptr (WpCore) core = NULL;
    GVariantBuilder b;
    guint64 out_stream_i, in_stream_i;

    core = wp_proxy_get_core (WP_PROXY (self));
    link = wp_session_item_make (core, "si-standard-link");
    if (!link) {
      wp_warning_object (self, "si-standard-link factory is not available");
      return -ENAVAIL;
    }

    if (self->info.direction == PW_DIRECTION_OUTPUT) {
      out_stream_i = (guint64) self_si_stream;
      in_stream_i = (guint64) peer_si_stream;
    } else {
      out_stream_i = (guint64) peer_si_stream;
      in_stream_i = (guint64) self_si_stream;
    }

    g_variant_builder_init (&b, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add (&b, "{sv}", "out-stream",
        g_variant_new_uint64 (out_stream_i));
    g_variant_builder_add (&b, "{sv}", "in-stream",
        g_variant_new_uint64 (in_stream_i));
    if (G_UNLIKELY (!wp_session_item_configure (link, g_variant_builder_end (&b)))) {
      g_critical ("si-standard-link configuration failed");
      return -ENAVAIL;
    }

    wp_session_item_export (link, session,
        (GAsyncReadyCallback) on_si_link_exported, self);
    link = NULL;
  }

  return 0;
}

static const struct pw_endpoint_methods impl_endpoint = {
  PW_VERSION_ENDPOINT_METHODS,
  .add_listener = impl_add_listener,
  .subscribe_params = impl_subscribe_params,
  .enum_params = impl_enum_params,
  .set_param = impl_set_param,
  .create_link = impl_create_link,
};

static void
populate_properties (WpImplEndpoint * self, WpProperties *global_props)
{
  WpEndpointPrivate *priv =
      wp_endpoint_get_instance_private (WP_ENDPOINT (self));

  g_clear_pointer (&priv->properties, wp_properties_unref);
  priv->properties = wp_si_endpoint_get_properties (self->item);
  priv->properties = wp_properties_ensure_unique_owner (priv->properties);
  wp_properties_update (priv->properties, global_props);

  self->info.props = priv->properties ?
      (struct spa_dict *) wp_properties_peek_dict (priv->properties) : NULL;

  g_object_notify (G_OBJECT (self), "properties");
}

static void
on_si_endpoint_properties_changed (WpSiEndpoint * item, WpImplEndpoint * self)
{
  populate_properties (self, wp_proxy_get_global_properties (WP_PROXY (self)));

  self->info.change_mask = PW_ENDPOINT_CHANGE_MASK_PROPS;
  pw_endpoint_emit_info (&self->hooks, &self->info);
  self->info.change_mask = 0;
}

static void
wp_impl_endpoint_init (WpImplEndpoint * self)
{
  /* reuse the parent's private to optimize memory usage and to be able
     to re-use some of the parent's methods without reimplementing them */
  WpEndpointPrivate *priv =
      wp_endpoint_get_instance_private (WP_ENDPOINT (self));

  self->iface = SPA_INTERFACE_INIT (
      PW_TYPE_INTERFACE_Endpoint,
      PW_VERSION_ENDPOINT,
      &impl_endpoint, self);
  spa_hook_list_init (&self->hooks);

  priv->iface = (struct pw_endpoint *) &self->iface;

  wp_proxy_set_feature_ready (WP_PROXY (self), WP_ENDPOINT_FEATURE_CONTROLS);
}

static void
wp_impl_endpoint_finalize (GObject * object)
{
  WpImplEndpoint *self = WP_IMPL_ENDPOINT (object);
  WpEndpointPrivate *priv =
      wp_endpoint_get_instance_private (WP_ENDPOINT (self));

  g_free (self->info.name);
  g_free (self->info.media_class);
  priv->info = NULL;

  G_OBJECT_CLASS (wp_impl_endpoint_parent_class)->finalize (object);
}

static void
wp_impl_endpoint_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpImplEndpoint *self = WP_IMPL_ENDPOINT (object);

  switch (property_id) {
  case IMPL_PROP_ITEM:
    self->item = g_value_get_object (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_impl_endpoint_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpImplEndpoint *self = WP_IMPL_ENDPOINT (object);

  switch (property_id) {
  case IMPL_PROP_ITEM:
    g_value_set_object (value, self->item);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_impl_endpoint_augment (WpProxy * proxy, WpProxyFeatures features)
{
  WpImplEndpoint *self = WP_IMPL_ENDPOINT (proxy);
  WpEndpointPrivate *priv =
      wp_endpoint_get_instance_private (WP_ENDPOINT (self));
  g_autoptr (GVariant) info = NULL;
  g_autoptr (GVariantIter) immutable_props = NULL;
  g_autoptr (WpProperties) props = NULL;

  /* PW_PROXY depends on BOUND */
  if (features & WP_PROXY_FEATURE_PW_PROXY)
    features |= WP_PROXY_FEATURE_BOUND;

  /* BOUND depends on INFO */
  if (features & WP_PROXY_FEATURE_BOUND)
    features |= WP_PROXY_FEATURE_INFO;

  if (features & WP_PROXY_FEATURE_INFO) {
    guchar direction;
    const gchar *key, *value;

    /* get info from the interface */
    info = wp_si_endpoint_get_registration_info (self->item);
    g_variant_get (info, "(ssya{ss})", &self->info.name,
        &self->info.media_class, &direction, &immutable_props);

    self->info.direction = (enum pw_direction) direction;
    self->info.n_streams = wp_si_endpoint_get_n_streams (self->item);

    /* associate with the session */
    self->info.session_id = wp_session_item_get_associated_proxy_id (
        WP_SESSION_ITEM (self->item), WP_TYPE_SESSION);

    /* construct export properties (these will come back through
       the registry and appear in wp_proxy_get_global_properties) */
    props = wp_properties_new (
        PW_KEY_ENDPOINT_NAME, self->info.name,
        PW_KEY_MEDIA_CLASS, self->info.media_class,
        NULL);
    wp_properties_setf (props, PW_KEY_SESSION_ID, "%d", self->info.session_id);

    /* populate immutable (global) properties */
    while (g_variant_iter_next (immutable_props, "{&s&s}", &key, &value))
      wp_properties_set (props, key, value);

    /* populate standard properties */
    populate_properties (self, props);

    /* subscribe to changes */
    g_signal_connect_object (self->item, "endpoint-properties-changed",
        G_CALLBACK (on_si_endpoint_properties_changed), self, 0);

    /* finalize info struct */
    self->info.version = PW_VERSION_ENDPOINT_INFO;
    self->info.params = impl_param_info;
    self->info.n_params = SPA_N_ELEMENTS (impl_param_info);
    priv->info = &self->info;
    g_object_notify (G_OBJECT (self), "info");

    wp_proxy_set_feature_ready (WP_PROXY (self), WP_PROXY_FEATURE_INFO);
  }

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

    wp_proxy_set_pw_proxy (proxy, pw_core_export (pw_core,
            PW_TYPE_INTERFACE_Endpoint,
            wp_properties_peek_dict (props),
            priv->iface, 0));
  }

  if (features & WP_ENDPOINT_FEATURE_STREAMS) {
    priv->streams_om = wp_object_manager_new ();

    /* if we are already bound, enable right away;
       else, continue in the bound() event */
    if (wp_proxy_get_features (proxy) & WP_PROXY_FEATURE_BOUND) {
      wp_endpoint_enable_feature_streams (WP_ENDPOINT (proxy),
          wp_proxy_get_bound_id (proxy));
    }
  }
}

static void
wp_impl_endpoint_class_init (WpImplEndpointClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpProxyClass *proxy_class = (WpProxyClass *) klass;

  object_class->finalize = wp_impl_endpoint_finalize;
  object_class->set_property = wp_impl_endpoint_set_property;
  object_class->get_property = wp_impl_endpoint_get_property;

  proxy_class->augment = wp_impl_endpoint_augment;

  proxy_class->pw_proxy_created = NULL;
  proxy_class->param = NULL;

  g_object_class_install_property (object_class, IMPL_PROP_ITEM,
      g_param_spec_object ("item", "item", "item", WP_TYPE_SI_ENDPOINT,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

WpImplEndpoint *
wp_impl_endpoint_new (WpCore * core, WpSiEndpoint * item)
{
  g_return_val_if_fail (WP_IS_CORE (core), NULL);

  return g_object_new (WP_TYPE_IMPL_ENDPOINT,
      "core", core,
      "item", item,
      NULL);
}

#if 0
/**
 * wp_impl_endpoint_register_control:
 * @self: the endpoint implementation
 * @control: the control to make available
 *
 * Registers the specified @control as a SPA property of this endpoint,
 * making it appear to remote clients.
 *
 * This is required to do before setting or getting a value for this control.
 * This is also required to be done before exporting the endpoint.
 */
void
wp_impl_endpoint_register_control (WpImplEndpoint * self,
    WpEndpointControl control)
{
  WpImplEndpointPrivate *priv;

  g_return_if_fail (WP_IS_IMPL_ENDPOINT (self));
  priv = wp_impl_endpoint_get_instance_private (self);

  switch (control) {
  case WP_ENDPOINT_CONTROL_VOLUME:
    wp_spa_props_register (&priv->pp->spa_props, control,
      "Volume", SPA_POD_CHOICE_RANGE_Float (1.0, 0.0, 10.0));
    break;
  case WP_ENDPOINT_CONTROL_MUTE:
    wp_spa_props_register (&priv->pp->spa_props, control,
      "Mute", SPA_POD_CHOICE_Bool (false));
    break;
  case WP_ENDPOINT_CONTROL_CHANNEL_VOLUMES:
    wp_spa_props_register (&priv->pp->spa_props, control,
      "Channel Volumes", SPA_POD_CHOICE_RANGE_Float (1.0, 0.0, 10.0));
    break;
  default:
    g_warning ("Unknown endpoint control: 0x%x", control);
    break;
  }
}
#endif
