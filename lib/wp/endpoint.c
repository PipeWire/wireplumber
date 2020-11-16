/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * SECTION: endpoint
 * @title: PIpeWire Endpoint
 */

#define G_LOG_DOMAIN "wp-endpoint"

#include "endpoint.h"
#include "node.h"
#include "session.h"
#include "object-manager.h"
#include "error.h"
#include "wpenums.h"
#include "si-factory.h"
#include "private/impl-endpoint.h"
#include "private/pipewire-object-mixin.h"

#include <pipewire/extensions/session-manager.h>
#include <pipewire/extensions/session-manager/introspect-funcs.h>

enum {
  PROP_NAME = WP_PIPEWIRE_OBJECT_MIXIN_PROP_CUSTOM_START,
  PROP_MEDIA_CLASS,
  PROP_DIRECTION,
};

enum {
  SIGNAL_STREAMS_CHANGED,
  N_SIGNALS,
};

static guint32 signals[N_SIGNALS] = {0};

typedef struct _WpEndpointPrivate WpEndpointPrivate;
struct _WpEndpointPrivate
{
  WpProperties *properties;
  struct pw_endpoint_info *info;
  struct pw_endpoint *iface;
  struct spa_hook listener;
  WpObjectManager *streams_om;
};

static void wp_endpoint_pipewire_object_interface_init (WpPipewireObjectInterface * iface);

/**
 * WpEndpoint:
 *
 * The #WpEndpoint class allows accessing the properties and methods of a
 * PipeWire endpoint object (`struct pw_endpoint` from the session-manager
 * extension).
 *
 * A #WpEndpoint is constructed internally when a new endpoint appears on the
 * PipeWire registry and it is made available through the #WpObjectManager API.
 */
G_DEFINE_TYPE_WITH_CODE (WpEndpoint, wp_endpoint, WP_TYPE_GLOBAL_PROXY,
    G_ADD_PRIVATE (WpEndpoint)
    G_IMPLEMENT_INTERFACE (WP_TYPE_PIPEWIRE_OBJECT, wp_endpoint_pipewire_object_interface_init));

static void
wp_endpoint_init (WpEndpoint * self)
{
}

static void
wp_endpoint_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpEndpoint *self = WP_ENDPOINT (object);
  WpEndpointPrivate *priv = wp_endpoint_get_instance_private (self);

  switch (property_id) {
  case PROP_NAME:
    g_value_set_string (value, priv->info ? priv->info->name : NULL);
    break;
  case PROP_MEDIA_CLASS:
    g_value_set_string (value, priv->info ? priv->info->media_class : NULL);
    break;
  case PROP_DIRECTION:
    g_value_set_enum (value, priv->info ? priv->info->direction : 0);
    break;
  default:
    wp_pipewire_object_mixin_get_property (object, property_id, value, pspec);
    break;
  }
}

static void
wp_endpoint_on_streams_om_installed (WpObjectManager *streams_om,
    WpEndpoint * self)
{
  wp_object_update_features (WP_OBJECT (self), WP_ENDPOINT_FEATURE_STREAMS, 0);
}

static void
wp_endpoint_emit_streams_changed (WpObjectManager *streams_om,
    WpEndpoint * self)
{
  g_signal_emit (self, signals[SIGNAL_STREAMS_CHANGED], 0);
  wp_object_update_features (WP_OBJECT (self), WP_ENDPOINT_FEATURE_STREAMS, 0);
}

static void
wp_endpoint_enable_feature_streams (WpEndpoint * self)
{
  WpEndpointPrivate *priv = wp_endpoint_get_instance_private (self);
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));
  guint32 bound_id = wp_proxy_get_bound_id (WP_PROXY (self));

  wp_debug_object (self, "enabling WP_ENDPOINT_FEATURE_STREAMS, bound_id:%u, "
      "n_streams:%u", bound_id, priv->info->n_streams);

  priv->streams_om = wp_object_manager_new ();
  /* proxy endpoint stream -> check for endpoint.id in global properties */
  wp_object_manager_add_interest (priv->streams_om,
      WP_TYPE_ENDPOINT_STREAM,
      WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY, PW_KEY_ENDPOINT_ID, "=u", bound_id,
      NULL);
  /* impl endpoint stream -> check for endpoint.id in standard properties */
  wp_object_manager_add_interest (priv->streams_om,
      WP_TYPE_IMPL_ENDPOINT_STREAM,
      WP_CONSTRAINT_TYPE_PW_PROPERTY, PW_KEY_ENDPOINT_ID, "=u", bound_id,
      NULL);
  wp_object_manager_request_object_features (priv->streams_om,
      WP_TYPE_ENDPOINT_STREAM, WP_OBJECT_FEATURES_ALL);

  /* endpoints, under normal circumstances, always have streams.
     When we export (self is a WpImplEndpoint), we have to export first
     the endpoint and afterwards the streams (so that the streams can be
     associated with the endpoint's bound id), but then the issue is that
     the "installed" signal gets fired here without any streams being ready
     and we get an endpoint with 0 streams in the WpSession's endpoints
     object manager... so, unless the endpoint really has no streams,
     wait for them to be prepared by waiting for the "objects-changed" only */
  if (G_UNLIKELY (priv->info->n_streams == 0)) {
    g_signal_connect_object (priv->streams_om, "installed",
        G_CALLBACK (wp_endpoint_on_streams_om_installed), self, 0);
  }
  g_signal_connect_object (priv->streams_om, "objects-changed",
      G_CALLBACK (wp_endpoint_emit_streams_changed), self, 0);

  wp_core_install_object_manager (core, priv->streams_om);
}

static WpObjectFeatures
wp_endpoint_get_supported_features (WpObject * object)
{
  WpEndpoint *self = WP_ENDPOINT (object);
  WpEndpointPrivate *priv = wp_endpoint_get_instance_private (self);

  return
      WP_PROXY_FEATURE_BOUND |
      WP_ENDPOINT_FEATURE_STREAMS |
      WP_PIPEWIRE_OBJECT_FEATURE_INFO |
      wp_pipewire_object_mixin_param_info_to_features (
          priv->info ? priv->info->params : NULL,
          priv->info ? priv->info->n_params : 0);
}

enum {
  STEP_STREAMS = WP_PIPEWIRE_OBJECT_MIXIN_STEP_CUSTOM_START,
};

static guint
wp_endpoint_activate_get_next_step (WpObject * object,
    WpFeatureActivationTransition * transition, guint step,
    WpObjectFeatures missing)
{
  step = wp_pipewire_object_mixin_activate_get_next_step (object, transition,
      step, missing);

  /* extend the mixin's state machine; when the only remaining feature to
     enable is FEATURE_STREAMS, advance to STEP_STREAMS */
  if (step == WP_PIPEWIRE_OBJECT_MIXIN_STEP_CACHE_INFO &&
      missing == WP_ENDPOINT_FEATURE_STREAMS)
    return STEP_STREAMS;

  return step;
}

static void
wp_endpoint_activate_execute_step (WpObject * object,
    WpFeatureActivationTransition * transition, guint step,
    WpObjectFeatures missing)
{
  switch (step) {
  case WP_PIPEWIRE_OBJECT_MIXIN_STEP_CACHE_INFO:
    wp_pipewire_object_mixin_cache_info (object, transition);
    break;
  case STEP_STREAMS:
    wp_endpoint_enable_feature_streams (WP_ENDPOINT (object));
    break;
  default:
    WP_OBJECT_CLASS (wp_endpoint_parent_class)->
        activate_execute_step (object, transition, step, missing);
    break;
  }
}

static void
wp_endpoint_deactivate (WpObject * object, WpObjectFeatures features)
{
  WpEndpoint *self = WP_ENDPOINT (object);
  WpEndpointPrivate *priv = wp_endpoint_get_instance_private (self);

  wp_pipewire_object_mixin_deactivate (object, features);

  if (features & WP_ENDPOINT_FEATURE_STREAMS) {
    g_clear_object (&priv->streams_om);
    wp_object_update_features (object, 0, WP_ENDPOINT_FEATURE_STREAMS);
  }

  WP_OBJECT_CLASS (wp_endpoint_parent_class)->deactivate (object, features);
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

  wp_object_update_features (WP_OBJECT (self),
      WP_PIPEWIRE_OBJECT_FEATURE_INFO, 0);

  wp_pipewire_object_mixin_handle_event_info (self, info,
      PW_ENDPOINT_CHANGE_MASK_PROPS, PW_ENDPOINT_CHANGE_MASK_PARAMS);
}

static const struct pw_endpoint_events endpoint_events = {
  PW_VERSION_ENDPOINT_EVENTS,
  .info = endpoint_event_info,
  .param = wp_pipewire_object_mixin_handle_event_param,
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
wp_endpoint_pw_proxy_destroyed (WpProxy * proxy)
{
  WpEndpoint *self = WP_ENDPOINT (proxy);
  WpEndpointPrivate *priv = wp_endpoint_get_instance_private (self);

  g_clear_pointer (&priv->properties, wp_properties_unref);
  g_clear_pointer (&priv->info, pw_endpoint_info_free);
  g_clear_object (&priv->streams_om);
  wp_object_update_features (WP_OBJECT (proxy), 0,
      WP_PIPEWIRE_OBJECT_FEATURE_INFO | WP_ENDPOINT_FEATURE_STREAMS);

  wp_pipewire_object_mixin_deactivate (WP_OBJECT (proxy),
      WP_OBJECT_FEATURES_ALL);
}

static void
wp_endpoint_class_init (WpEndpointClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpObjectClass *wpobject_class = (WpObjectClass *) klass;
  WpProxyClass *proxy_class = (WpProxyClass *) klass;

  object_class->get_property = wp_endpoint_get_property;

  wpobject_class->get_supported_features = wp_endpoint_get_supported_features;
  wpobject_class->activate_get_next_step = wp_endpoint_activate_get_next_step;
  wpobject_class->activate_execute_step = wp_endpoint_activate_execute_step;
  wpobject_class->deactivate = wp_endpoint_deactivate;

  proxy_class->pw_iface_type = PW_TYPE_INTERFACE_Endpoint;
  proxy_class->pw_iface_version = PW_VERSION_ENDPOINT;
  proxy_class->pw_proxy_created = wp_endpoint_pw_proxy_created;
  proxy_class->pw_proxy_destroyed = wp_endpoint_pw_proxy_destroyed;

  wp_pipewire_object_mixin_class_override_properties (object_class);

  /**
   * WpEndpoint::streams-changed:
   * @self: the endpoint
   *
   * Emitted when the endpoints's streams change. This is only emitted
   * when %WP_ENDPOINT_FEATURE_STREAMS is enabled.
   */
  signals[SIGNAL_STREAMS_CHANGED] = g_signal_new (
      "streams-changed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);

  /**
   * WpEndpoint:name:
   *
   * The name of the endpoint
   */
  g_object_class_install_property (object_class, PROP_NAME,
      g_param_spec_string ("name", "name", "name", NULL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /**
   * WpEndpoint:media-class:
   *
   * The media class of the endpoint (ex. "Audio/Sink")
   */
  g_object_class_install_property (object_class, PROP_MEDIA_CLASS,
      g_param_spec_string ("media-class", "media-class", "media-class", NULL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /**
   * WpEndpoint:direction:
   *
   * The direction of the endpoint
   */
  g_object_class_install_property (object_class, PROP_DIRECTION,
      g_param_spec_enum ("direction", "direction", "direction",
          WP_TYPE_DIRECTION, 0, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
}

static gconstpointer
wp_endpoint_get_native_info (WpPipewireObject * obj)
{
  WpEndpoint *self = WP_ENDPOINT (obj);
  WpEndpointPrivate *priv = wp_endpoint_get_instance_private (self);

  return priv->info;
}

static WpProperties *
wp_endpoint_get_properties (WpPipewireObject * obj)
{
  WpEndpoint *self = WP_ENDPOINT (obj);
  WpEndpointPrivate *priv = wp_endpoint_get_instance_private (self);

  return wp_properties_ref (priv->properties);
}

static GVariant *
wp_endpoint_get_param_info (WpPipewireObject * obj)
{
  WpEndpoint *self = WP_ENDPOINT (obj);
  WpEndpointPrivate *priv = wp_endpoint_get_instance_private (self);

  return wp_pipewire_object_mixin_param_info_to_gvariant (priv->info->params,
      priv->info->n_params);
}

static void
wp_endpoint_enum_params (WpPipewireObject * obj, const gchar * id,
    WpSpaPod *filter, GCancellable * cancellable,
    GAsyncReadyCallback callback, gpointer user_data)
{
  wp_pipewire_object_mixin_enum_params (pw_endpoint, obj, id, filter,
      cancellable, callback, user_data);
}

static void
wp_endpoint_set_param (WpPipewireObject * obj, const gchar * id,
    WpSpaPod * param)
{
  wp_pipewire_object_mixin_set_param (pw_endpoint, obj, id, param);
}

static void
wp_endpoint_pipewire_object_interface_init (
    WpPipewireObjectInterface * iface)
{
  iface->get_native_info = wp_endpoint_get_native_info;
  iface->get_properties = wp_endpoint_get_properties;
  iface->get_param_info = wp_endpoint_get_param_info;
  iface->enum_params = wp_endpoint_enum_params;
  iface->enum_params_finish = wp_pipewire_object_mixin_enum_params_finish;
  iface->enum_cached_params = wp_pipewire_object_mixin_enum_cached_params;
  iface->set_param = wp_endpoint_set_param;
}

/**
 * wp_endpoint_get_name:
 * @self: the endpoint
 *
 * Requires %WP_PIPEWIRE_OBJECT_FEATURE_INFO
 *
 * Returns: the name of the endpoint
 */
const gchar *
wp_endpoint_get_name (WpEndpoint * self)
{
  g_return_val_if_fail (WP_IS_ENDPOINT (self), NULL);
  g_return_val_if_fail (wp_object_get_active_features (WP_OBJECT (self)) &
          WP_PIPEWIRE_OBJECT_FEATURE_INFO, NULL);

  WpEndpointPrivate *priv = wp_endpoint_get_instance_private (self);
  return priv->info->name;
}

/**
 * wp_endpoint_get_media_class:
 * @self: the endpoint
 *
 * Requires %WP_PIPEWIRE_OBJECT_FEATURE_INFO
 *
 * Returns: the media class of the endpoint (ex. "Audio/Sink")
 */
const gchar *
wp_endpoint_get_media_class (WpEndpoint * self)
{
  g_return_val_if_fail (WP_IS_ENDPOINT (self), NULL);
  g_return_val_if_fail (wp_object_get_active_features (WP_OBJECT (self)) &
          WP_PIPEWIRE_OBJECT_FEATURE_INFO, NULL);

  WpEndpointPrivate *priv = wp_endpoint_get_instance_private (self);
  return priv->info->media_class;
}

/**
 * wp_endpoint_get_direction:
 * @self: the endpoint
 *
 * Requires %WP_PIPEWIRE_OBJECT_FEATURE_INFO
 *
 * Returns: the direction of this endpoint
 */
WpDirection
wp_endpoint_get_direction (WpEndpoint * self)
{
  g_return_val_if_fail (WP_IS_ENDPOINT (self), 0);
  g_return_val_if_fail (wp_object_get_active_features (WP_OBJECT (self)) &
          WP_PIPEWIRE_OBJECT_FEATURE_INFO, 0);

  WpEndpointPrivate *priv = wp_endpoint_get_instance_private (self);
  return (WpDirection) priv->info->direction;
}

/**
 * wp_endpoint_get_n_streams:
 * @self: the endpoint
 *
 * Requires %WP_ENDPOINT_FEATURE_STREAMS
 *
 * Returns: the number of streams of this endpoint
 */
guint
wp_endpoint_get_n_streams (WpEndpoint * self)
{
  g_return_val_if_fail (WP_IS_ENDPOINT (self), 0);
  g_return_val_if_fail (wp_object_get_active_features (WP_OBJECT (self)) &
          WP_ENDPOINT_FEATURE_STREAMS, 0);

  WpEndpointPrivate *priv = wp_endpoint_get_instance_private (self);
  return wp_object_manager_get_n_objects (priv->streams_om);
}

/**
 * wp_endpoint_iterate_streams:
 * @self: the endpoint
 *
 * Requires %WP_ENDPOINT_FEATURE_STREAMS
 *
 * Returns: (transfer full): a #WpIterator that iterates over all
 *   the endpoint streams that belong to this endpoint
 */
WpIterator *
wp_endpoint_iterate_streams (WpEndpoint * self)
{
  g_return_val_if_fail (WP_IS_ENDPOINT (self), NULL);
  g_return_val_if_fail (wp_object_get_active_features (WP_OBJECT (self)) &
          WP_ENDPOINT_FEATURE_STREAMS, NULL);

  WpEndpointPrivate *priv = wp_endpoint_get_instance_private (self);
  return wp_object_manager_iterate (priv->streams_om);
}

/**
 * wp_endpoint_iterate_streams_filtered:
 * @self: the endpoint
 * @...: a list of constraints, terminated by %NULL
 *
 * Requires %WP_ENDPOINT_FEATURE_STREAMS
 *
 * The constraints specified in the variable arguments must follow the rules
 * documented in wp_object_interest_new().
 *
 * Returns: (transfer full): a #WpIterator that iterates over all
 *   the streams that belong to this endpoint and match the constraints
 */
WpIterator *
wp_endpoint_iterate_streams_filtered (WpEndpoint * self, ...)
{
  WpObjectInterest *interest;
  va_list args;
  va_start (args, self);
  interest = wp_object_interest_new_valist (WP_TYPE_ENDPOINT_STREAM, &args);
  va_end (args);
  return wp_endpoint_iterate_streams_filtered_full (self, interest);
}

/**
 * wp_endpoint_iterate_streams_filtered_full: (rename-to wp_endpoint_iterate_streams_filtered)
 * @self: the endpoint
 * @interest: (transfer full): the interest
 *
 * Requires %WP_ENDPOINT_FEATURE_STREAMS
 *
 * Returns: (transfer full): a #WpIterator that iterates over all
 *   the streams that belong to this endpoint and match the @interest
 */
WpIterator *
wp_endpoint_iterate_streams_filtered_full (WpEndpoint * self,
    WpObjectInterest * interest)
{
  g_return_val_if_fail (WP_IS_ENDPOINT (self), NULL);
  g_return_val_if_fail (wp_object_get_active_features (WP_OBJECT (self)) &
          WP_ENDPOINT_FEATURE_STREAMS, NULL);

  WpEndpointPrivate *priv = wp_endpoint_get_instance_private (self);
  return wp_object_manager_iterate_filtered_full (priv->streams_om, interest);
}

/**
 * wp_endpoint_lookup_stream:
 * @self: the endpoint
 * @...: a list of constraints, terminated by %NULL
 *
 * Requires %WP_ENDPOINT_FEATURE_STREAMS
 *
 * The constraints specified in the variable arguments must follow the rules
 * documented in wp_object_interest_new().
 *
 * Returns: (transfer full) (nullable): the first stream that matches the
 *    constraints, or %NULL if there is no such stream
 */
WpEndpointStream *
wp_endpoint_lookup_stream (WpEndpoint * self, ...)
{
  WpObjectInterest *interest;
  va_list args;
  va_start (args, self);
  interest = wp_object_interest_new_valist (WP_TYPE_ENDPOINT_STREAM, &args);
  va_end (args);
  return wp_endpoint_lookup_stream_full (self, interest);
}

/**
 * wp_endpoint_lookup_stream_full: (rename-to wp_endpoint_lookup_stream)
 * @self: the endpoint
 * @interest: (transfer full): the interest
 *
 * Requires %WP_ENDPOINT_FEATURE_STREAMS
 *
 * Returns: (transfer full) (nullable): the first stream that matches the
 *    @interest, or %NULL if there is no such stream
 */
WpEndpointStream *
wp_endpoint_lookup_stream_full (WpEndpoint * self, WpObjectInterest * interest)
{
  g_return_val_if_fail (WP_IS_ENDPOINT (self), NULL);
  g_return_val_if_fail (wp_object_get_active_features (WP_OBJECT (self)) &
          WP_ENDPOINT_FEATURE_STREAMS, NULL);

  WpEndpointPrivate *priv = wp_endpoint_get_instance_private (self);
  return (WpEndpointStream *)
      wp_object_manager_lookup_full (priv->streams_om, interest);
}

/**
 * wp_endpoint_create_link:
 * @self: the endpoint
 * @props: the link properties
 *
 * Creates a #WpEndpointLink between @self and another endpoint, which
 * must be specified in @props.
 * @props may contain:
 *  - `endpoint-link.output.endpoint`: the bound id of the endpoint
 *        that is in the %WP_DIRECTION_OUTPUT direction
 *  - `endpoint-link.output.stream`: the bound id of the endpoint stream
 *        that is in the %WP_DIRECTION_OUTPUT direction
 *  - `endpoint-link.input.endpoint`: the bound id of the endpoint
 *        that is in the %WP_DIRECTION_INPUT direction
 *  - `endpoint-link.input.stream`: the bound id of the endpoint stream
 *        that is in the %WP_DIRECTION_INPUT direction
 *
 * If either stream id are not specified (or set to -1), then the first
 * available stream of this endpoint is used for the link.
 *
 * The id of @self is not necessary to be specified, so only one of
 * `endpoint-link.output.endpoint`, `endpoint-link.input.endpoint`
 * is actually required.
 */
void
wp_endpoint_create_link (WpEndpoint * self, WpProperties * props)
{
  WpEndpointPrivate *priv =
      wp_endpoint_get_instance_private (WP_ENDPOINT (self));
  int res;

  res = pw_endpoint_create_link (priv->iface, wp_properties_peek_dict (props));
  if (res < 0) {
    wp_warning_object (self, "pw_endpoint_create_link: %d: %s", res,
        spa_strerror (res));
  }
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

  WpSiEndpoint *item;
};

G_DEFINE_TYPE (WpImplEndpoint, wp_impl_endpoint, WP_TYPE_ENDPOINT)

#define pw_endpoint_emit(hooks,method,version,...) \
    spa_hook_list_call_simple(hooks, struct pw_endpoint_events, \
        method, version, ##__VA_ARGS__)

#define pw_endpoint_emit_info(hooks,...)  pw_endpoint_emit(hooks, info, 0, ##__VA_ARGS__)
#define pw_endpoint_emit_param(hooks,...) pw_endpoint_emit(hooks, param, 0, ##__VA_ARGS__)

// static struct spa_param_info impl_param_info[] = {
//   SPA_PARAM_INFO (SPA_PARAM_Props, SPA_PARAM_INFO_READWRITE),
//   SPA_PARAM_INFO (SPA_PARAM_PropInfo, SPA_PARAM_INFO_READ)
// };

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
  return -ENOENT;
}

static int
impl_subscribe_params (void *object, uint32_t *ids, uint32_t n_ids)
{
  return 0;
}

static int
impl_set_param (void *object, uint32_t id, uint32_t flags,
    const struct spa_pod *param)
{
  return -ENOENT;
}

static void
on_si_link_exported (WpSessionItem * link, GAsyncResult * res, gpointer data)
{
  WpImplEndpoint *self = WP_IMPL_ENDPOINT (data);
  g_autoptr (GError) error = NULL;

  if (!wp_session_item_export_finish (link, res, &error)) {
    wp_warning_object (self, "failed to export link: %s", error->message);
    g_object_unref (link);
  }
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
  g_autoptr (WpEndpointStream) self_stream_proxy = NULL;
  g_autoptr (WpEndpoint) peer_ep_proxy = NULL;
  g_autoptr (WpEndpointStream) peer_stream_proxy = NULL;

  /* find the session */
  session = wp_session_item_get_associated_proxy (
      WP_SESSION_ITEM (self->item), WP_TYPE_SESSION);
  g_return_val_if_fail (session, -ENAVAIL);

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

  wp_debug_object (self, "requested link between %s:%s [self] & %s:%s [peer]",
      self_ep, self_stream, peer_ep, peer_stream);

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

  self_stream_proxy = wp_session_item_get_associated_proxy (
      WP_SESSION_ITEM (self_si_stream), WP_TYPE_ENDPOINT_STREAM);

  /* find the peer stream */
  peer_ep_proxy = wp_session_lookup_endpoint (session,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "bound-id", "=u", peer_ep_id, NULL);
  if (!peer_ep_proxy) {
    wp_warning_object (self, "endpoint %d not found in session", peer_ep_id);
    return -EINVAL;
  }

  if (peer_stream_id != SPA_ID_INVALID) {
    peer_stream_proxy = wp_endpoint_lookup_stream (peer_ep_proxy,
        WP_CONSTRAINT_TYPE_G_PROPERTY, "bound-id", "=u", peer_stream_id, NULL);
  } else {
    peer_stream_proxy = wp_endpoint_lookup_stream (peer_ep_proxy, NULL);
  }

  if (!peer_stream_proxy) {
    wp_warning_object (self, "stream %d not found in %d", peer_stream_id,
        peer_ep_id);
    return -EINVAL;
  }

  if (!WP_IS_IMPL_ENDPOINT_STREAM (peer_stream_proxy)) {
    /* TODO - if the stream is not implemented by our session manager,
      we can still make things work by calling the peer endpoint's
      create_link() and negotiating ports, while creating a dummy
      WpSiEndpoint / WpSiStream on our end to satisfy the API */
    return -ENAVAIL;
  }

  g_object_get (peer_stream_proxy, "item", &peer_si_stream, NULL);

  wp_info_object (self, "creating endpoint link between "
      "%s|%s " WP_OBJECT_FORMAT ", %s|%s " WP_OBJECT_FORMAT,
      wp_endpoint_get_name (WP_ENDPOINT (self)),
      wp_endpoint_stream_get_name (self_stream_proxy),
      WP_OBJECT_ARGS (self_si_stream),
      wp_endpoint_get_name (peer_ep_proxy),
      wp_endpoint_stream_get_name (peer_stream_proxy),
      WP_OBJECT_ARGS (peer_si_stream));

  /* create the link */
  {
    g_autoptr (WpSessionItem) link = NULL;
    g_autoptr (WpCore) core = NULL;
    GVariantBuilder b;
    guint64 out_stream_i, in_stream_i;

    core = wp_object_get_core (WP_OBJECT (self));
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
    g_variant_builder_add (&b, "{sv}", "manage-lifetime",
        g_variant_new_boolean (TRUE));
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
  if (!priv->properties)
    priv->properties = wp_properties_new_empty ();
  priv->properties = wp_properties_ensure_unique_owner (priv->properties);
  wp_properties_update (priv->properties, global_props);

  self->info.props = priv->properties ?
      (struct spa_dict *) wp_properties_peek_dict (priv->properties) : NULL;
}

static void
on_si_endpoint_properties_changed (WpSiEndpoint * item, WpImplEndpoint * self)
{
  populate_properties (self,
      wp_global_proxy_get_global_properties (WP_GLOBAL_PROXY (self)));
  g_object_notify (G_OBJECT (self), "properties");

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

enum {
  STEP_ACTIVATE_NODE = STEP_STREAMS + 1,
};

static guint
wp_impl_endpoint_activate_get_next_step (WpObject * object,
    WpFeatureActivationTransition * transition, guint step,
    WpObjectFeatures missing)
{
  WpImplEndpoint *self = WP_IMPL_ENDPOINT (object);

  /* bind if not already bound */
  if (missing & WP_PROXY_FEATURE_BOUND) {
    g_autoptr (WpObject) node = wp_session_item_get_associated_proxy (
        WP_SESSION_ITEM (self->item), WP_TYPE_NODE);

    /* if the item has a node, cache its props so that enum_params works */
    // if (node && !(wp_object_get_active_features (node) &
    //                   WP_PIPEWIRE_OBJECT_FEATURE_PARAM_PROPS))
    //   return STEP_ACTIVATE_NODE;
    // else
      return WP_PIPEWIRE_OBJECT_MIXIN_STEP_BIND;
  }
  /* enable FEATURE_STREAMS when there is nothing else left to activate */
  else if (missing == WP_ENDPOINT_FEATURE_STREAMS)
    return STEP_STREAMS;
  else
    return WP_PIPEWIRE_OBJECT_MIXIN_STEP_CACHE_INFO;
}

static void
wp_impl_endpoint_node_activated (WpObject * node,
    GAsyncResult * res, WpTransition * transition)
{
  g_autoptr (GError) error = NULL;

  if (!wp_object_activate_finish (node, res, &error)) {
    wp_transition_return_error (transition, g_steal_pointer (&error));
    return;
  }

  wp_transition_advance (transition);
}

static void
wp_impl_endpoint_activate_execute_step (WpObject * object,
    WpFeatureActivationTransition * transition, guint step,
    WpObjectFeatures missing)
{
  WpImplEndpoint *self = WP_IMPL_ENDPOINT (object);
  WpEndpointPrivate *priv =
      wp_endpoint_get_instance_private (WP_ENDPOINT (self));

  switch (step) {
  case STEP_ACTIVATE_NODE: {
    g_autoptr (WpObject) node = wp_session_item_get_associated_proxy (
        WP_SESSION_ITEM (self->item), WP_TYPE_NODE);

    wp_object_activate (node,
        WP_PROXY_FEATURE_BOUND /*| WP_PIPEWIRE_OBJECT_FEATURE_PARAM_PROPS */,
        NULL, (GAsyncReadyCallback) wp_impl_endpoint_node_activated,
        transition);
    break;
  }
  case WP_PIPEWIRE_OBJECT_MIXIN_STEP_BIND: {
    g_autoptr (GVariantIter) immutable_properties = NULL;
    g_autoptr (WpProperties) properties = NULL;
    g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));
    struct pw_core *pw_core = wp_core_get_pw_core (core);

    /* no pw_core -> we are not connected */
    if (!pw_core) {
      wp_transition_return_error (WP_TRANSITION (transition), g_error_new (
              WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_OPERATION_FAILED,
              "The WirePlumber core is not connected; "
              "object cannot be exported to PipeWire"));
      return;
    }

    wp_debug_object (self, "exporting");

    /* get info from the interface */
    {
      g_autoptr (GVariant) info = NULL;
      guchar direction;

      info = wp_si_endpoint_get_registration_info (self->item);
      g_variant_get (info, "(ssya{ss})", &self->info.name,
          &self->info.media_class, &direction, &immutable_properties);

      self->info.direction = (enum pw_direction) direction;
      self->info.n_streams = wp_si_endpoint_get_n_streams (self->item);

      /* associate with the session */
      self->info.session_id = wp_session_item_get_associated_proxy_id (
          WP_SESSION_ITEM (self->item), WP_TYPE_SESSION);
    }

    /* construct export properties (these will come back through
      the registry and appear in wp_proxy_get_global_properties) */
    properties = wp_properties_new (
        PW_KEY_ENDPOINT_NAME, self->info.name,
        PW_KEY_MEDIA_CLASS, self->info.media_class,
        NULL);
    wp_properties_setf (properties, PW_KEY_SESSION_ID,
        "%d", self->info.session_id);

    /* populate immutable (global) properties */
    {
      const gchar *key, *value;
      while (g_variant_iter_next (immutable_properties, "{&s&s}", &key, &value))
        wp_properties_set (properties, key, value);
    }

    /* populate standard properties */
    populate_properties (self, properties);

    /* subscribe to changes */
    g_signal_connect_object (self->item, "endpoint-properties-changed",
        G_CALLBACK (on_si_endpoint_properties_changed), self, 0);

    /* finalize info struct */
    self->info.version = PW_VERSION_ENDPOINT_INFO;
    self->info.params = NULL;
    self->info.n_params = 0;
    priv->info = &self->info;

    /* bind */
    wp_proxy_set_pw_proxy (WP_PROXY (self), pw_core_export (pw_core,
            PW_TYPE_INTERFACE_Endpoint,
            wp_properties_peek_dict (properties),
            priv->iface, 0));

    /* notify */
    wp_object_update_features (object,
        WP_PIPEWIRE_OBJECT_FEATURE_INFO
        /*| WP_PIPEWIRE_OBJECT_FEATURE_PARAM_PROPS */, 0);
    g_object_notify (G_OBJECT (self), "properties");
    g_object_notify (G_OBJECT (self), "param-info");

    break;
  }
  default:
    WP_OBJECT_CLASS (wp_impl_endpoint_parent_class)->
        activate_execute_step (object, transition, step, missing);
    break;
  }
}

static void
wp_impl_endpoint_pw_proxy_destroyed (WpProxy * proxy)
{
  WpImplEndpoint *self = WP_IMPL_ENDPOINT (proxy);
  WpEndpointPrivate *priv =
      wp_endpoint_get_instance_private (WP_ENDPOINT (self));

  g_signal_handlers_disconnect_by_data (self->item, self);
  g_clear_pointer (&priv->properties, wp_properties_unref);
  g_clear_pointer (&self->info.name, g_free);
  g_clear_pointer (&self->info.media_class, g_free);
  priv->info = NULL;
  wp_object_update_features (WP_OBJECT (proxy), 0,
      WP_PIPEWIRE_OBJECT_FEATURE_INFO
      /*| WP_PIPEWIRE_OBJECT_FEATURE_PARAM_PROPS */);
}

static void
wp_impl_endpoint_class_init (WpImplEndpointClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpObjectClass *wpobject_class = (WpObjectClass *) klass;
  WpProxyClass *proxy_class = (WpProxyClass *) klass;

  object_class->set_property = wp_impl_endpoint_set_property;
  object_class->get_property = wp_impl_endpoint_get_property;

  wpobject_class->activate_get_next_step =
      wp_impl_endpoint_activate_get_next_step;
  wpobject_class->activate_execute_step =
      wp_impl_endpoint_activate_execute_step;

  proxy_class->pw_proxy_created = NULL;
  proxy_class->pw_proxy_destroyed = wp_impl_endpoint_pw_proxy_destroyed;

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
