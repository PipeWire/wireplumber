/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * SECTION: endpoint-stream
 * @title: PipeWire Endpoint Stream
 */

#define G_LOG_DOMAIN "wp-endpoint-stream"

#include "endpoint-stream.h"
#include "node.h"
#include "error.h"
#include "debug.h"
#include "spa-type.h"
#include "private/impl-endpoint.h"
#include "private/pipewire-object-mixin.h"

#include <pipewire/extensions/session-manager.h>
#include <pipewire/extensions/session-manager/introspect-funcs.h>

enum {
  PROP_NAME = WP_PW_OBJECT_MIXIN_PROP_CUSTOM_START,
};

static void wp_endpoint_stream_pw_object_mixin_priv_interface_init (
    WpPwObjectMixinPrivInterface * iface);

/**
 * WpEndpointStream:
 *
 * The #WpEndpointStream class allows accessing the properties and methods of a
 * PipeWire endpoint stream object (`struct pw_endpoint_stream` from the
 * session-manager extension).
 *
 * A #WpEndpointStream is constructed internally when a new endpoint appears on
 * the PipeWire registry and it is made available through the #WpObjectManager
 * API.
 */
G_DEFINE_TYPE_WITH_CODE (WpEndpointStream, wp_endpoint_stream, WP_TYPE_GLOBAL_PROXY,
    G_IMPLEMENT_INTERFACE (WP_TYPE_PIPEWIRE_OBJECT,
        wp_pw_object_mixin_object_interface_init)
    G_IMPLEMENT_INTERFACE (WP_TYPE_PW_OBJECT_MIXIN_PRIV,
        wp_endpoint_stream_pw_object_mixin_priv_interface_init))

static void
wp_endpoint_stream_init (WpEndpointStream * self)
{
}

static void
wp_endpoint_stream_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpPwObjectMixinData *d = wp_pw_object_mixin_get_data (object);

  switch (property_id) {
  case PROP_NAME:
    g_value_set_string (value, d->info ?
        ((struct pw_endpoint_stream_info *) d->info)->name : NULL);
    break;
  default:
    wp_pw_object_mixin_get_property (object, property_id, value, pspec);
    break;
  }
}

static void
wp_endpoint_stream_activate_execute_step (WpObject * object,
    WpFeatureActivationTransition * transition, guint step,
    WpObjectFeatures missing)
{
  switch (step) {
  case WP_PW_OBJECT_MIXIN_STEP_BIND:
  case WP_TRANSITION_STEP_ERROR:
    /* base class can handle BIND and ERROR */
    WP_OBJECT_CLASS (wp_endpoint_stream_parent_class)->
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
wp_endpoint_stream_deactivate (WpObject * object, WpObjectFeatures features)
{
  wp_pw_object_mixin_deactivate (object, features);
  WP_OBJECT_CLASS (wp_endpoint_stream_parent_class)->deactivate (object, features);
}

static const struct pw_endpoint_stream_events endpoint_stream_events = {
  PW_VERSION_ENDPOINT_STREAM_EVENTS,
  .info = (HandleEventInfoFunc(endpoint_stream)) wp_pw_object_mixin_handle_event_info,
  .param = wp_pw_object_mixin_handle_event_param,
};

static void
wp_endpoint_stream_pw_proxy_created (WpProxy * proxy, struct pw_proxy * pw_proxy)
{
  wp_pw_object_mixin_handle_pw_proxy_created (proxy, pw_proxy,
      endpoint_stream, &endpoint_stream_events);
}

static void
wp_endpoint_stream_class_init (WpEndpointStreamClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpObjectClass *wpobject_class = (WpObjectClass *) klass;
  WpProxyClass *proxy_class = (WpProxyClass *) klass;

  object_class->get_property = wp_endpoint_stream_get_property;

  wpobject_class->get_supported_features =
      wp_pw_object_mixin_get_supported_features;
  wpobject_class->activate_get_next_step =
      wp_pw_object_mixin_activate_get_next_step;
  wpobject_class->activate_execute_step =
      wp_endpoint_stream_activate_execute_step;
  wpobject_class->deactivate = wp_endpoint_stream_deactivate;

  proxy_class->pw_iface_type = PW_TYPE_INTERFACE_EndpointStream;
  proxy_class->pw_iface_version = PW_VERSION_ENDPOINT_STREAM;
  proxy_class->pw_proxy_created = wp_endpoint_stream_pw_proxy_created;
  proxy_class->pw_proxy_destroyed =
      wp_pw_object_mixin_handle_pw_proxy_destroyed;

  wp_pw_object_mixin_class_override_properties (object_class);

  /**
   * WpEndpointStream:name:
   *
   * The name of the endpoint stream
   */
  g_object_class_install_property (object_class, PROP_NAME,
      g_param_spec_string ("name", "name", "name", NULL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
}

static gint
wp_endpoint_stream_enum_params (gpointer instance, guint32 id,
    guint32 start, guint32 num, WpSpaPod *filter)
{
  WpPwObjectMixinData *d = wp_pw_object_mixin_get_data (instance);
  return pw_endpoint_stream_enum_params (d->iface, 0, id, start, num,
      filter ? wp_spa_pod_get_spa_pod (filter) : NULL);
}

static gint
wp_endpoint_stream_set_param (gpointer instance, guint32 id, guint32 flags,
    WpSpaPod * param)
{
  WpPwObjectMixinData *d = wp_pw_object_mixin_get_data (instance);
  return pw_endpoint_stream_set_param (d->iface, id, flags,
      wp_spa_pod_get_spa_pod (param));
}

static void
wp_endpoint_stream_pw_object_mixin_priv_interface_init (
    WpPwObjectMixinPrivInterface * iface)
{
  wp_pw_object_mixin_priv_interface_info_init (iface,
      endpoint_stream, ENDPOINT_STREAM);
  iface->enum_params = wp_endpoint_stream_enum_params;
  iface->set_param = wp_endpoint_stream_set_param;
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
  g_return_val_if_fail (wp_object_get_active_features (WP_OBJECT (self)) &
          WP_PIPEWIRE_OBJECT_FEATURE_INFO, NULL);

  WpPwObjectMixinData *d = wp_pw_object_mixin_get_data (self);
  return ((struct pw_endpoint_stream_info *) d->info)->name;
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
  struct pw_endpoint_stream_info info;
  WpProperties *immutable_props;

  WpSiStream *item;
};

static void wp_endpoint_stream_impl_pw_object_mixin_priv_interface_init (
    WpPwObjectMixinPrivInterface * iface);

G_DEFINE_TYPE_WITH_CODE (WpImplEndpointStream, wp_impl_endpoint_stream, WP_TYPE_ENDPOINT_STREAM,
    G_IMPLEMENT_INTERFACE (WP_TYPE_PW_OBJECT_MIXIN_PRIV,
        wp_endpoint_stream_impl_pw_object_mixin_priv_interface_init))

static struct spa_param_info impl_param_info[] = {
  SPA_PARAM_INFO (SPA_PARAM_Props, SPA_PARAM_INFO_READWRITE),
  SPA_PARAM_INFO (SPA_PARAM_PropInfo, SPA_PARAM_INFO_READ)
};

static const struct pw_endpoint_stream_methods impl_endpoint_stream = {
  PW_VERSION_ENDPOINT_STREAM_METHODS,
  .add_listener =
      (ImplAddListenerFunc(endpoint_stream)) wp_pw_object_mixin_impl_add_listener,
  .subscribe_params = wp_pw_object_mixin_impl_subscribe_params,
  .enum_params = wp_pw_object_mixin_impl_enum_params,
  .set_param = wp_pw_object_mixin_impl_set_param,
};

static void
wp_impl_endpoint_stream_init (WpImplEndpointStream * self)
{
  WpPwObjectMixinData *d = wp_pw_object_mixin_get_data (self);

  self->iface = SPA_INTERFACE_INIT (
      PW_TYPE_INTERFACE_EndpointStream,
      PW_VERSION_ENDPOINT_STREAM,
      &impl_endpoint_stream, self);

  d->info = &self->info;
  d->iface = &self->iface;
}

static void
populate_properties (WpImplEndpointStream * self)
{
  WpPwObjectMixinData *d = wp_pw_object_mixin_get_data (self);

  g_clear_pointer (&d->properties, wp_properties_unref);
  d->properties = wp_si_stream_get_properties (self->item);
  if (!d->properties)
    d->properties = wp_properties_new_empty ();
  d->properties = wp_properties_ensure_unique_owner (d->properties);
  wp_properties_update (d->properties, self->immutable_props);

  self->info.props = (struct spa_dict *) wp_properties_peek_dict (d->properties);
}

static void
on_si_stream_properties_changed (WpSiStream * item, WpImplEndpointStream * self)
{
  populate_properties (self);
  wp_pw_object_mixin_notify_info (self, PW_ENDPOINT_STREAM_CHANGE_MASK_PROPS);
}

static void
on_node_params_changed (WpNode * node, guint32 param_id, WpImplEndpoint * self)
{
  if (param_id == SPA_PARAM_PropInfo || param_id == SPA_PARAM_Props)
    wp_pw_object_mixin_notify_params_changed (self, param_id);
}

static void
wp_impl_endpoint_stream_constructed (GObject * object)
{
  WpImplEndpointStream *self = WP_IMPL_ENDPOINT_STREAM (object);
  g_autoptr (GVariant) info = NULL;
  g_autoptr (GVariantIter) immutable_props = NULL;
  g_autoptr (WpObject) node = NULL;
  const gchar *key, *value;

  self->info.version = PW_VERSION_ENDPOINT_STREAM_INFO;

  /* get info from the interface */
  info = wp_si_stream_get_registration_info (self->item);
  g_variant_get (info, "(sa{ss})", &self->info.name, &immutable_props);

  /* associate with the endpoint */
  self->info.endpoint_id = wp_session_item_get_associated_proxy_id (
      WP_SESSION_ITEM (self->item), WP_TYPE_ENDPOINT);

  /* construct export properties (these will come back through
      the registry and appear in wp_proxy_get_global_properties) */
  self->immutable_props = wp_properties_new (
      PW_KEY_ENDPOINT_STREAM_NAME, self->info.name,
      NULL);
  wp_properties_setf (self->immutable_props, PW_KEY_ENDPOINT_ID,
      "%d", self->info.endpoint_id);

  /* populate immutable (global) properties */
  while (g_variant_iter_next (immutable_props, "{&s&s}", &key, &value))
    wp_properties_set (self->immutable_props, key, value);

  /* populate standard properties */
  populate_properties (self);

  /* subscribe to changes */
  g_signal_connect_object (self->item, "stream-properties-changed",
      G_CALLBACK (on_si_stream_properties_changed), self, 0);

  /* if the item has a node, proxy its ParamProps */
  node = wp_session_item_get_associated_proxy (
      WP_SESSION_ITEM (self->item), WP_TYPE_NODE);
  if (node && (wp_object_get_active_features (node) &
                  WP_PIPEWIRE_OBJECT_FEATURE_PARAM_PROPS)) {
    self->info.params = impl_param_info;
    self->info.n_params = G_N_ELEMENTS (impl_param_info);

    g_signal_connect_object (node, "params-changed",
        G_CALLBACK (on_node_params_changed), self, 0);

    wp_object_update_features (WP_OBJECT (self),
        WP_PIPEWIRE_OBJECT_FEATURE_PARAM_PROPS, 0);
  } else {
    self->info.params = NULL;
    self->info.n_params = 0;
  }

  wp_object_update_features (WP_OBJECT (self),
      WP_PIPEWIRE_OBJECT_FEATURE_INFO, 0);

  G_OBJECT_CLASS (wp_impl_endpoint_stream_parent_class)->constructed (object);
}

static void
wp_impl_endpoint_stream_dispose (GObject * object)
{
  WpImplEndpointStream *self = WP_IMPL_ENDPOINT_STREAM (object);

  g_clear_pointer (&self->immutable_props, wp_properties_unref);
  g_clear_pointer (&self->info.name, g_free);

  wp_object_update_features (WP_OBJECT (self), 0,
      WP_PIPEWIRE_OBJECT_FEATURE_INFO |
      WP_PIPEWIRE_OBJECT_FEATURE_PARAM_PROPS);

  G_OBJECT_CLASS (wp_impl_endpoint_stream_parent_class)->dispose (object);
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

enum {
  STEP_ACTIVATE_NODE = WP_PW_OBJECT_MIXIN_STEP_CUSTOM_START,
};

static guint
wp_impl_endpoint_stream_activate_get_next_step (WpObject * object,
    WpFeatureActivationTransition * transition, guint step,
    WpObjectFeatures missing)
{
  WpImplEndpointStream *self = WP_IMPL_ENDPOINT_STREAM (object);

  /* before anything else, if the item has a node,
     cache its props so that enum_params works */
  if (missing & WP_PIPEWIRE_OBJECT_FEATURES_ALL) {
    g_autoptr (WpObject) node = wp_session_item_get_associated_proxy (
        WP_SESSION_ITEM (self->item), WP_TYPE_NODE);

    if (node && (wp_object_get_supported_features (node) &
                    WP_PIPEWIRE_OBJECT_FEATURE_PARAM_PROPS) &&
               !(wp_object_get_active_features (node) &
                    WP_PIPEWIRE_OBJECT_FEATURE_PARAM_PROPS))
      return STEP_ACTIVATE_NODE;
  }

  return WP_OBJECT_CLASS (wp_impl_endpoint_stream_parent_class)->
      activate_get_next_step (object, transition, step, missing);
}

static void
wp_impl_endpoint_stream_node_activated (WpObject * node,
    GAsyncResult * res, WpTransition * transition)
{
  WpImplEndpointStream *self = wp_transition_get_source_object (transition);
  g_autoptr (GError) error = NULL;

  if (!wp_object_activate_finish (node, res, &error)) {
    wp_transition_return_error (transition, g_steal_pointer (&error));
    return;
  }

  self->info.params = impl_param_info;
  self->info.n_params = G_N_ELEMENTS (impl_param_info);

  g_signal_connect_object (node, "params-changed",
      G_CALLBACK (on_node_params_changed), self, 0);

  wp_object_update_features (WP_OBJECT (self),
      WP_PIPEWIRE_OBJECT_FEATURE_PARAM_PROPS, 0);
  wp_pw_object_mixin_notify_info (self, PW_ENDPOINT_STREAM_CHANGE_MASK_PARAMS);
}

static void
wp_impl_endpoint_stream_activate_execute_step (WpObject * object,
    WpFeatureActivationTransition * transition, guint step,
    WpObjectFeatures missing)
{
  WpImplEndpointStream *self = WP_IMPL_ENDPOINT_STREAM (object);

  switch (step) {
  case STEP_ACTIVATE_NODE: {
    g_autoptr (WpObject) node = wp_session_item_get_associated_proxy (
        WP_SESSION_ITEM (self->item), WP_TYPE_NODE);

    wp_object_activate (node,
        WP_PROXY_FEATURE_BOUND | WP_PIPEWIRE_OBJECT_FEATURE_PARAM_PROPS,
        NULL, (GAsyncReadyCallback) wp_impl_endpoint_stream_node_activated,
        transition);
    break;
  }
  case WP_PW_OBJECT_MIXIN_STEP_BIND: {
    g_autoptr (WpCore) core = wp_object_get_core (object);
    struct pw_core *pw_core = wp_core_get_pw_core (core);

    /* no pw_core -> we are not connected */
    if (!pw_core) {
      wp_transition_return_error (WP_TRANSITION (transition), g_error_new (
              WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_OPERATION_FAILED,
              "The WirePlumber core is not connected; "
              "object cannot be exported to PipeWire"));
      return;
    }

    /* bind */
    wp_proxy_set_pw_proxy (WP_PROXY (self), pw_core_export (pw_core,
            PW_TYPE_INTERFACE_EndpointStream,
            wp_properties_peek_dict (self->immutable_props),
            &self->iface, 0));
    break;
  }
  default:
    WP_OBJECT_CLASS (wp_impl_endpoint_stream_parent_class)->
        activate_execute_step (object, transition, step, missing);
    break;
  }
}

static void
wp_impl_endpoint_stream_class_init (WpImplEndpointStreamClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpObjectClass *wpobject_class = (WpObjectClass *) klass;
  WpProxyClass *proxy_class = (WpProxyClass *) klass;

  object_class->constructed = wp_impl_endpoint_stream_constructed;
  object_class->dispose = wp_impl_endpoint_stream_dispose;
  object_class->set_property = wp_impl_endpoint_stream_set_property;
  object_class->get_property = wp_impl_endpoint_stream_get_property;

  wpobject_class->activate_get_next_step =
      wp_impl_endpoint_stream_activate_get_next_step;
  wpobject_class->activate_execute_step =
      wp_impl_endpoint_stream_activate_execute_step;

  proxy_class->pw_proxy_created = NULL;
  proxy_class->pw_proxy_destroyed = NULL;

  g_object_class_install_property (object_class, IMPL_PROP_ITEM,
      g_param_spec_object ("item", "item", "item", WP_TYPE_SI_STREAM,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

static GPtrArray *
wp_impl_endpoint_stream_enum_params_sync (gpointer instance, guint32 id,
    guint32 start, guint32 num, WpSpaPod *filter)
{
  WpImplEndpointStream *self = WP_IMPL_ENDPOINT_STREAM (instance);
  g_autoptr (WpPipewireObject) node = wp_session_item_get_associated_proxy (
        WP_SESSION_ITEM (self->item), WP_TYPE_NODE);

  if (!node) {
    wp_warning_object (self, "associated node is no longer available");
    return NULL;
  }

  /* bypass a few things, knowing that the node
     caches params in the mixin param store */
  WpPwObjectMixinData *data = wp_pw_object_mixin_get_data (node);
  GPtrArray *params = wp_pw_object_mixin_get_stored_params (data, id);
  /* TODO filter */

  return params;
}

static gint
wp_impl_endpoint_stream_set_param (gpointer instance, guint32 id, guint32 flags,
    WpSpaPod * param)
{
  WpImplEndpointStream *self = WP_IMPL_ENDPOINT_STREAM (instance);
  g_autoptr (WpPipewireObject) node = wp_session_item_get_associated_proxy (
        WP_SESSION_ITEM (self->item), WP_TYPE_NODE);
  const gchar *idstr = NULL;

  if (!node) {
    wp_warning_object (self, "associated node is no longer available");
    return -EPIPE;
  }

  if (!wp_spa_type_get_by_id (WP_SPA_TYPE_TABLE_PARAM, id, NULL, &idstr, NULL)) {
    wp_critical_object (self, "invalid param id: %u", id);
    return -EINVAL;
  }

  return wp_pipewire_object_set_param (node, idstr, flags, param) ? 0 : -EIO;
}

#define pw_endpoint_stream_emit(hooks,method,version,...) \
    spa_hook_list_call_simple(hooks, struct pw_endpoint_stream_events, \
        method, version, ##__VA_ARGS__)

static void
wp_impl_endpoint_stream_emit_info (struct spa_hook_list * hooks, gconstpointer info)
{
  pw_endpoint_stream_emit (hooks, info, 0, info);
}

static void
wp_impl_endpoint_stream_emit_param (struct spa_hook_list * hooks, int seq,
      guint32 id, guint32 index, guint32 next, const struct spa_pod *param)
{
  pw_endpoint_stream_emit (hooks, param, 0, seq, id, index, next, param);
}

static void
wp_endpoint_stream_impl_pw_object_mixin_priv_interface_init (
    WpPwObjectMixinPrivInterface * iface)
{
  iface->flags |= WP_PW_OBJECT_MIXIN_PRIV_NO_PARAM_CACHE;
  iface->enum_params_sync = wp_impl_endpoint_stream_enum_params_sync;
  iface->set_param = wp_impl_endpoint_stream_set_param;
  iface->emit_info = wp_impl_endpoint_stream_emit_info;
  iface->emit_param = wp_impl_endpoint_stream_emit_param;
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
