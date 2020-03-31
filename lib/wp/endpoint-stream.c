/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * SECTION: WpEndpointStream
 *
 * The #WpEndpointStream class allows accessing the properties and methods of a
 * PipeWire endpoint stream object (`struct pw_endpoint_stream` from the
 * session-manager extension).
 *
 * A #WpEndpointStream is constructed internally when a new endpoint appears on
 * the PipeWire registry and it is made available through the #WpObjectManager
 * API.
 */

#include "endpoint-stream.h"
#include "private.h"
#include "error.h"

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

/* WpEndpointStream */

typedef struct _WpEndpointStreamPrivate WpEndpointStreamPrivate;
struct _WpEndpointStreamPrivate
{
  WpProperties *properties;
  WpSpaProps spa_props;
  struct pw_endpoint_stream_info *info;
  struct pw_endpoint_stream *iface;
  struct spa_hook listener;
};

G_DEFINE_TYPE_WITH_PRIVATE (WpEndpointStream, wp_endpoint_stream, WP_TYPE_PROXY)

static void
wp_endpoint_stream_init (WpEndpointStream * self)
{
}

static void
wp_endpoint_stream_finalize (GObject * object)
{
  WpEndpointStream *self = WP_ENDPOINT_STREAM (object);
  WpEndpointStreamPrivate *priv = wp_endpoint_stream_get_instance_private (self);

  g_clear_pointer (&priv->properties, wp_properties_unref);
  g_clear_pointer (&priv->info, pw_endpoint_stream_info_free);
  wp_spa_props_clear (&priv->spa_props);

  G_OBJECT_CLASS (wp_endpoint_stream_parent_class)->finalize (object);
}

static void
wp_endpoint_stream_augment (WpProxy * proxy, WpProxyFeatures features)
{
  /* call the parent impl first to ensure we have a pw proxy if necessary */
  WP_PROXY_CLASS (wp_endpoint_stream_parent_class)->augment (proxy, features);

  if (features & WP_ENDPOINT_STREAM_FEATURE_CONTROLS) {
    struct pw_endpoint_stream *pw_proxy = NULL;
    uint32_t ids[] = { SPA_PARAM_Props };

    pw_proxy = (struct pw_endpoint_stream *) wp_proxy_get_pw_proxy (proxy);
    if (!pw_proxy)
      return;

    pw_endpoint_stream_enum_params (pw_proxy, 0, SPA_PARAM_PropInfo, 0, -1, NULL);
    pw_endpoint_stream_subscribe_params (pw_proxy, ids, SPA_N_ELEMENTS (ids));
  }
}

static gconstpointer
wp_endpoint_stream_get_info (WpProxy * proxy)
{
  WpEndpointStream *self = WP_ENDPOINT_STREAM (proxy);
  WpEndpointStreamPrivate *priv = wp_endpoint_stream_get_instance_private (self);

  return priv->info;
}

static WpProperties *
wp_endpoint_stream_get_properties (WpProxy * proxy)
{
  WpEndpointStream *self = WP_ENDPOINT_STREAM (proxy);
  WpEndpointStreamPrivate *priv = wp_endpoint_stream_get_instance_private (self);

  return wp_properties_ref (priv->properties);
}

static gint
wp_endpoint_stream_enum_params (WpProxy * self, guint32 id, guint32 start,
    guint32 num, const struct spa_pod *filter)
{
  WpEndpointStreamPrivate *priv =
      wp_endpoint_stream_get_instance_private (WP_ENDPOINT_STREAM (self));
  int endpoint_stream_enum_params_result;

  endpoint_stream_enum_params_result =
      pw_endpoint_stream_enum_params (priv->iface, 0, id, start, num, filter);
  g_warn_if_fail (endpoint_stream_enum_params_result >= 0);

  return endpoint_stream_enum_params_result;
}

static gint
wp_endpoint_stream_subscribe_params (WpProxy * self, guint32 n_ids, guint32 *ids)
{
  WpEndpointStreamPrivate *priv =
      wp_endpoint_stream_get_instance_private (WP_ENDPOINT_STREAM (self));
  int endpoint_stream_subscribe_params_result;

  endpoint_stream_subscribe_params_result =
      pw_endpoint_stream_subscribe_params (priv->iface, ids, n_ids);
  g_warn_if_fail (endpoint_stream_subscribe_params_result >= 0);

  return endpoint_stream_subscribe_params_result;
}

static gint
wp_endpoint_stream_set_param (WpProxy * self, guint32 id, guint32 flags,
    const struct spa_pod *param)
{
  WpEndpointStreamPrivate *priv =
      wp_endpoint_stream_get_instance_private (WP_ENDPOINT_STREAM (self));
  int endpoint_stream_set_param_result;

  endpoint_stream_set_param_result =
      pw_endpoint_stream_set_param (priv->iface, id, flags, param);
  g_warn_if_fail (endpoint_stream_set_param_result >= 0);

  return endpoint_stream_set_param_result;
}

static void
endpoint_stream_event_info (void *data, const struct pw_endpoint_stream_info *info)
{
  WpEndpointStream *self = WP_ENDPOINT_STREAM (data);
  WpEndpointStreamPrivate *priv = wp_endpoint_stream_get_instance_private (self);

  priv->info = pw_endpoint_stream_info_update (priv->info, info);

  if (info->change_mask & PW_ENDPOINT_STREAM_CHANGE_MASK_PROPS) {
    g_clear_pointer (&priv->properties, wp_properties_unref);
    priv->properties = wp_properties_new_wrap_dict (priv->info->props);
  }

  wp_proxy_set_feature_ready (WP_PROXY (self), WP_PROXY_FEATURE_INFO);
  g_object_notify (G_OBJECT (self), "info");

  if (info->change_mask & PW_ENDPOINT_STREAM_CHANGE_MASK_PROPS)
    g_object_notify (G_OBJECT (self), "properties");
}

static const struct pw_endpoint_stream_events endpoint_stream_events = {
  PW_VERSION_ENDPOINT_STREAM_EVENTS,
  .info = endpoint_stream_event_info,
  .param = wp_proxy_handle_event_param,
};

static void
wp_endpoint_stream_pw_proxy_created (WpProxy * proxy, struct pw_proxy * pw_proxy)
{
  WpEndpointStream *self = WP_ENDPOINT_STREAM (proxy);
  WpEndpointStreamPrivate *priv = wp_endpoint_stream_get_instance_private (self);

  priv->iface = (struct pw_endpoint_stream *) pw_proxy;
  pw_endpoint_stream_add_listener (priv->iface, &priv->listener,
      &endpoint_stream_events, self);
}

static void
wp_endpoint_stream_param (WpProxy * proxy, gint seq, guint32 id, guint32 index,
    guint32 next, const struct spa_pod *param)
{
  WpEndpointStream *self = WP_ENDPOINT_STREAM (proxy);
  WpEndpointStreamPrivate *priv = wp_endpoint_stream_get_instance_private (self);
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
        WP_ENDPOINT_STREAM_FEATURE_CONTROLS);
    break;
  }
}

static const gchar *
get_name (WpEndpointStream * self)
{
  WpEndpointStreamPrivate *priv = wp_endpoint_stream_get_instance_private (self);
  return priv->info->name;
}

static const struct spa_pod *
get_control (WpEndpointStream * self, guint32 control_id)
{
  WpEndpointStreamPrivate *priv = wp_endpoint_stream_get_instance_private (self);
  return wp_spa_props_get_stored (&priv->spa_props, control_id);
}

static gboolean
set_control (WpEndpointStream * self, guint32 control_id,
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
wp_endpoint_stream_class_init (WpEndpointStreamClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpProxyClass *proxy_class = (WpProxyClass *) klass;

  object_class->finalize = wp_endpoint_stream_finalize;

  proxy_class->pw_iface_type = PW_TYPE_INTERFACE_EndpointStream;
  proxy_class->pw_iface_version = PW_VERSION_ENDPOINT_STREAM;

  proxy_class->augment = wp_endpoint_stream_augment;
  proxy_class->get_info = wp_endpoint_stream_get_info;
  proxy_class->get_properties = wp_endpoint_stream_get_properties;
  proxy_class->enum_params = wp_endpoint_stream_enum_params;
  proxy_class->subscribe_params = wp_endpoint_stream_subscribe_params;
  proxy_class->set_param = wp_endpoint_stream_set_param;

  proxy_class->pw_proxy_created = wp_endpoint_stream_pw_proxy_created;
  proxy_class->param = wp_endpoint_stream_param;

  klass->get_name = get_name;
  klass->get_control = get_control;
  klass->set_control = set_control;

  /**
   * WpEndpointStream::control-changed:
   * @self: the endpoint stream
   * @control: the control that changed (a #WpEndpointControl)
   *
   * Emitted when an endpoint stream control changes value
   */
  signals[SIGNAL_CONTROL_CHANGED] = g_signal_new (
      "control-changed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_UINT);
}

/**
 * wp_endpoint_stream_get_name:
 * @self: the endpoint stream
 *
 * Returns: the name of the endpoint stream
 */
const gchar *
wp_endpoint_stream_get_name (WpEndpointStream * self)
{
  g_return_val_if_fail (WP_IS_ENDPOINT_STREAM (self), NULL);
  g_return_val_if_fail (WP_ENDPOINT_STREAM_GET_CLASS (self)->get_name, NULL);

  return WP_ENDPOINT_STREAM_GET_CLASS (self)->get_name (self);
}

/**
 * wp_endpoint_stream_get_control:
 * @self: the endpoint stream
 * @control_id: the control id (a #WpEndpointControl)
 *
 * Returns: (transfer none) (nullable): the `spa_pod` containing the value
 *   of this control, or %NULL if @control_id does not exist on this endpoint
 *   stream
 */
const struct spa_pod *
wp_endpoint_stream_get_control (WpEndpointStream * self, guint32 control_id)
{
  g_return_val_if_fail (WP_IS_ENDPOINT_STREAM (self), NULL);
  g_return_val_if_fail (WP_ENDPOINT_STREAM_GET_CLASS (self)->get_control, NULL);

  return WP_ENDPOINT_STREAM_GET_CLASS (self)->get_control (self, control_id);
}

/**
 * wp_endpoint_stream_get_control_boolean:
 * @self: the endpoint stream
 * @control_id: the control id (a #WpEndpointControl)
 * @value: (out): the boolean value of this control
 *
 * Returns: %TRUE on success, %FALSE if the control does not exist on this
 *   endpoint stream or if it is not a boolean
 */
gboolean
wp_endpoint_stream_get_control_boolean (WpEndpointStream * self,
    guint32 control_id, gboolean * value)
{
  const struct spa_pod *pod = wp_endpoint_stream_get_control (self, control_id);
  bool val;
  if (pod && spa_pod_get_bool (pod, &val) == 0) {
    *value = val;
    return TRUE;
  }
  return FALSE;
}

/**
 * wp_endpoint_stream_get_control_int:
 * @self: the endpoint stream
 * @control_id: the control id (a #WpEndpointControl)
 * @value: (out): the integer value of this control
 *
 * Returns: %TRUE on success, %FALSE if the control does not exist on this
 *   endpoint stream or if it is not an integer
 */
gboolean
wp_endpoint_stream_get_control_int (WpEndpointStream * self, guint32 control_id,
    gint * value)
{
  const struct spa_pod *pod = wp_endpoint_stream_get_control (self, control_id);
  return (pod && spa_pod_get_int (pod, value) == 0);
}

/**
 * wp_endpoint_stream_get_control_float:
 * @self: the endpoint stream
 * @control_id: the control id (a #WpEndpointControl)
 * @value: (out): the floating-point number value of this control
 *
 * Returns: %TRUE on success, %FALSE if the control does not exist on this
 *   endpoint stream or if it is not a floating-point number
 */
gboolean
wp_endpoint_stream_get_control_float (WpEndpointStream * self, guint32 control_id,
    gfloat * value)
{
  const struct spa_pod *pod = wp_endpoint_stream_get_control (self, control_id);
  return (pod && spa_pod_get_float (pod, value) == 0);
}

/**
 * wp_endpoint_stream_set_control:
 * @self: the endpoint stream
 * @control_id: the control id (a #WpEndpointControl)
 * @value: the new value for this control, as a `spa_pod`
 *
 * Returns: %TRUE on success, %FALSE if an error occurred
 */
gboolean
wp_endpoint_stream_set_control (WpEndpointStream * self, guint32 control_id,
    const struct spa_pod * value)
{
  g_return_val_if_fail (WP_IS_ENDPOINT_STREAM (self), FALSE);
  g_return_val_if_fail (WP_ENDPOINT_STREAM_GET_CLASS (self)->set_control, FALSE);

  return WP_ENDPOINT_STREAM_GET_CLASS (self)->set_control (self, control_id,
      value);
}

/**
 * wp_endpoint_stream_set_control_boolean:
 * @self: the endpoint stream
 * @control_id: the control id (a #WpEndpointControl)
 * @value: the new value for this control, as a boolean
 *
 * Returns: %TRUE on success, %FALSE if an error occurred
 */
gboolean
wp_endpoint_stream_set_control_boolean (WpEndpointStream * self,
    guint32 control_id, gboolean value)
{
  gchar buffer[512];
  return wp_endpoint_stream_set_control (self, control_id,
      wp_spa_props_build_pod (buffer, sizeof (buffer), SPA_POD_Bool (value), 0));
}

/**
 * wp_endpoint_stream_set_control_int:
 * @self: the endpoint stream
 * @control_id: the control id (a #WpEndpointControl)
 * @value: the new value for this control, as an integer
 *
 * Returns: %TRUE on success, %FALSE if an error occurred
 */
gboolean
wp_endpoint_stream_set_control_int (WpEndpointStream * self, guint32 control_id,
    gint value)
{
  gchar buffer[512];
  return wp_endpoint_stream_set_control (self, control_id,
      wp_spa_props_build_pod (buffer, sizeof (buffer), SPA_POD_Int (value), 0));
}

/**
 * wp_endpoint_stream_set_control_float:
 * @self: the endpoint stream
 * @control_id: the control id (a #WpEndpointControl)
 * @value: the new value for this control, as a floating-point number
 *
 * Returns: %TRUE on success, %FALSE if an error occurred
 */
gboolean
wp_endpoint_stream_set_control_float (WpEndpointStream * self,
    guint32 control_id, gfloat value)
{
  gchar buffer[512];
  return wp_endpoint_stream_set_control (self, control_id,
      wp_spa_props_build_pod (buffer, sizeof (buffer), SPA_POD_Float (value), 0));
}

/* WpImplEndpointStream */

enum {
  IMPL_PROP_0,
  IMPL_PROP_ITEM,
};

struct _WpImplEndpointStream
{
  WpEndpointStream parent;

  struct spa_interface iface;
  struct spa_hook_list hooks;
  struct pw_endpoint_stream_info info;
  gboolean subscribed;

  WpSiStream *item;
};

G_DEFINE_TYPE (WpImplEndpointStream, wp_impl_endpoint_stream, WP_TYPE_ENDPOINT_STREAM)

#define pw_endpoint_stream_emit(hooks,method,version,...) \
    spa_hook_list_call_simple(hooks, struct pw_endpoint_stream_events, \
        method, version, ##__VA_ARGS__)

#define pw_endpoint_stream_emit_info(hooks,...)  \
    pw_endpoint_stream_emit(hooks, info, 0, ##__VA_ARGS__)
#define pw_endpoint_stream_emit_param(hooks,...) \
    pw_endpoint_stream_emit(hooks, param, 0, ##__VA_ARGS__)

static struct spa_param_info impl_param_info[] = {
  SPA_PARAM_INFO (SPA_PARAM_Props, SPA_PARAM_INFO_READWRITE),
  SPA_PARAM_INFO (SPA_PARAM_PropInfo, SPA_PARAM_INFO_READ)
};

static int
impl_add_listener(void *object,
    struct spa_hook *listener,
    const struct pw_endpoint_stream_events *events,
    void *data)
{
  WpImplEndpointStream *self = WP_IMPL_ENDPOINT_STREAM (object);
  struct spa_hook_list save;

  spa_hook_list_isolate (&self->hooks, &save, listener, events, data);

  self->info.change_mask = PW_ENDPOINT_STREAM_CHANGE_MASK_ALL
      & ~PW_ENDPOINT_STREAM_CHANGE_MASK_LINK_PARAMS;
  pw_endpoint_stream_emit_info (&self->hooks, &self->info);
  self->info.change_mask = 0;

  spa_hook_list_join (&self->hooks, &save);
  return 0;
}

static int
impl_enum_params (void *object, int seq,
    uint32_t id, uint32_t start, uint32_t num,
    const struct spa_pod *filter)
{
  WpImplEndpointStream *self = WP_IMPL_ENDPOINT_STREAM (object);
  WpEndpointStreamPrivate *priv =
      wp_endpoint_stream_get_instance_private (WP_ENDPOINT_STREAM (self));
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
          pw_endpoint_stream_emit_param (&self->hooks, seq, id, i, i+1, result);
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
          pw_endpoint_stream_emit_param (&self->hooks, seq, id, 0, 1, result);
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
  WpImplEndpointStream *self = WP_IMPL_ENDPOINT_STREAM (object);

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
  WpImplEndpointStream *self = WP_IMPL_ENDPOINT_STREAM (object);
  WpEndpointStreamPrivate *priv =
      wp_endpoint_stream_get_instance_private (WP_ENDPOINT_STREAM (self));
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

static const struct pw_endpoint_stream_methods impl_endpoint_stream = {
  PW_VERSION_ENDPOINT_STREAM_METHODS,
  .add_listener = impl_add_listener,
  .subscribe_params = impl_subscribe_params,
  .enum_params = impl_enum_params,
  .set_param = impl_set_param,
};

static void
populate_properties (WpImplEndpointStream * self, WpProperties *global_props)
{
  WpEndpointStreamPrivate *priv =
      wp_endpoint_stream_get_instance_private (WP_ENDPOINT_STREAM (self));

  g_clear_pointer (&priv->properties, wp_properties_unref);
  priv->properties = wp_si_stream_get_properties (self->item);
  priv->properties = wp_properties_ensure_unique_owner (priv->properties);
  wp_properties_update (priv->properties, global_props);

  self->info.props = priv->properties ?
      (struct spa_dict *) wp_properties_peek_dict (priv->properties) : NULL;

  g_object_notify (G_OBJECT (self), "properties");
}

static void
on_si_stream_properties_changed (WpSiStream * item, WpImplEndpointStream * self)
{
  populate_properties (self, wp_proxy_get_global_properties (WP_PROXY (self)));

  self->info.change_mask = PW_ENDPOINT_STREAM_CHANGE_MASK_PROPS;
  pw_endpoint_stream_emit_info (&self->hooks, &self->info);
  self->info.change_mask = 0;
}

static void
wp_impl_endpoint_stream_init (WpImplEndpointStream * self)
{
  /* reuse the parent's private to optimize memory usage and to be able
     to re-use some of the parent's methods without reimplementing them */
  WpEndpointStreamPrivate *priv =
      wp_endpoint_stream_get_instance_private (WP_ENDPOINT_STREAM (self));

  self->iface = SPA_INTERFACE_INIT (
      PW_TYPE_INTERFACE_EndpointStream,
      PW_VERSION_ENDPOINT_STREAM,
      &impl_endpoint_stream, self);
  spa_hook_list_init (&self->hooks);

  priv->iface = (struct pw_endpoint_stream *) &self->iface;

  wp_proxy_set_feature_ready (WP_PROXY (self), WP_ENDPOINT_STREAM_FEATURE_CONTROLS);
}

static void
wp_impl_endpoint_stream_finalize (GObject * object)
{
  WpImplEndpointStream *self = WP_IMPL_ENDPOINT_STREAM (object);
  WpEndpointStreamPrivate *priv =
      wp_endpoint_stream_get_instance_private (WP_ENDPOINT_STREAM (self));

  g_free (self->info.name);
  priv->info = NULL;

  G_OBJECT_CLASS (wp_impl_endpoint_stream_parent_class)->finalize (object);
}

static void
wp_impl_endpoint_stream_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpImplEndpointStream *self = WP_IMPL_ENDPOINT_STREAM (object);

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
wp_impl_endpoint_stream_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpImplEndpointStream *self = WP_IMPL_ENDPOINT_STREAM (object);

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
wp_impl_endpoint_stream_augment (WpProxy * proxy, WpProxyFeatures features)
{
  WpImplEndpointStream *self = WP_IMPL_ENDPOINT_STREAM (proxy);
  WpEndpointStreamPrivate *priv =
      wp_endpoint_stream_get_instance_private (WP_ENDPOINT_STREAM (self));
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
    const gchar *key, *value;

    /* get info from the interface */
    info = wp_si_stream_get_registration_info (self->item);
    g_variant_get (info, "(sa{ss})", &self->info.name, &immutable_props);

    /* associate with the endpoint */
    self->info.endpoint_id = wp_session_item_get_associated_proxy_id (
        WP_SESSION_ITEM (self->item), WP_TYPE_ENDPOINT);

    /* construct export properties (these will come back through
       the registry and appear in wp_proxy_get_global_properties) */
    props = wp_properties_new (
        PW_KEY_ENDPOINT_STREAM_NAME, self->info.name,
        NULL);
    wp_properties_setf (props, PW_KEY_ENDPOINT_ID, "%d", self->info.endpoint_id);

    /* populate immutable (global) properties */
    while (g_variant_iter_next (immutable_props, "{&s&s}", &key, &value))
      wp_properties_set (props, key, value);

    /* populate standard properties */
    populate_properties (self, props);

    /* subscribe to changes */
    g_signal_connect_object (self->item, "stream-properties-changed",
        G_CALLBACK (on_si_stream_properties_changed), self, 0);

    /* finalize info struct */
    self->info.version = PW_VERSION_ENDPOINT_STREAM_INFO;
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
            PW_TYPE_INTERFACE_EndpointStream,
            wp_properties_peek_dict (props),
            priv->iface, 0));
  }
}

static void
wp_impl_endpoint_stream_class_init (WpImplEndpointStreamClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpProxyClass *proxy_class = (WpProxyClass *) klass;

  object_class->finalize = wp_impl_endpoint_stream_finalize;
  object_class->set_property = wp_impl_endpoint_stream_set_property;
  object_class->get_property = wp_impl_endpoint_stream_get_property;

  proxy_class->augment = wp_impl_endpoint_stream_augment;

  proxy_class->pw_proxy_created = NULL;
  proxy_class->param = NULL;

  g_object_class_install_property (object_class, IMPL_PROP_ITEM,
      g_param_spec_object ("item", "item", "item", WP_TYPE_SI_STREAM,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

WpImplEndpointStream *
wp_impl_endpoint_stream_new (WpCore * core, WpSiStream * item)
{
  g_return_val_if_fail (WP_IS_CORE (core), NULL);

  return g_object_new (WP_TYPE_IMPL_ENDPOINT_STREAM,
      "core", core,
      "item", item,
      NULL);
}
