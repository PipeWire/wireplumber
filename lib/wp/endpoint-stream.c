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
#include "private.h"
#include "private/pipewire-object-mixin.h"

#include <pipewire/extensions/session-manager.h>
#include <pipewire/extensions/session-manager/introspect-funcs.h>

enum {
  PROP_NAME = WP_PIPEWIRE_OBJECT_MIXIN_PROP_CUSTOM_START,
};

typedef struct _WpEndpointStreamPrivate WpEndpointStreamPrivate;
struct _WpEndpointStreamPrivate
{
  WpProperties *properties;
  struct pw_endpoint_stream_info *info;
  struct pw_endpoint_stream *iface;
  struct spa_hook listener;
};

static void wp_endpoint_stream_pipewire_object_interface_init (WpPipewireObjectInterface * iface);

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
    G_ADD_PRIVATE (WpEndpointStream)
    G_IMPLEMENT_INTERFACE (WP_TYPE_PIPEWIRE_OBJECT, wp_endpoint_stream_pipewire_object_interface_init));

static void
wp_endpoint_stream_init (WpEndpointStream * self)
{
}

static void
wp_endpoint_stream_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpEndpointStream *self = WP_ENDPOINT_STREAM (object);
  WpEndpointStreamPrivate *priv = wp_endpoint_stream_get_instance_private (self);

  switch (property_id) {
  case PROP_NAME:
    g_value_set_string (value, priv->info ? priv->info->name : NULL);
    break;
  default:
    wp_pipewire_object_mixin_get_property (object, property_id, value, pspec);
    break;
  }
}

static WpObjectFeatures
wp_endpoint_stream_get_supported_features (WpObject * object)
{
  WpEndpointStream *self = WP_ENDPOINT_STREAM (object);
  WpEndpointStreamPrivate *priv = wp_endpoint_stream_get_instance_private (self);

  return WP_PROXY_FEATURE_BOUND | WP_PIPEWIRE_OBJECT_FEATURE_INFO |
      wp_pipewire_object_mixin_param_info_to_features (
          priv->info ? priv->info->params : NULL,
          priv->info ? priv->info->n_params : 0);
}

static void
wp_endpoint_stream_activate_execute_step (WpObject * object,
    WpFeatureActivationTransition * transition, guint step,
    WpObjectFeatures missing)
{
  switch (step) {
  case WP_PIPEWIRE_OBJECT_MIXIN_STEP_CACHE_INFO:
    wp_pipewire_object_mixin_cache_info (object, transition);
    break;
  default:
    WP_OBJECT_CLASS (wp_endpoint_stream_parent_class)->
        activate_execute_step (object, transition, step, missing);
    break;
  }
}

static void
wp_endpoint_stream_deactivate (WpObject * object, WpObjectFeatures features)
{
  wp_pipewire_object_mixin_deactivate (object, features);

  WP_OBJECT_CLASS (wp_endpoint_stream_parent_class)->deactivate (object, features);
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

  wp_object_update_features (WP_OBJECT (self),
      WP_PIPEWIRE_OBJECT_FEATURE_INFO, 0);

  wp_pipewire_object_mixin_handle_event_info (self, info,
      PW_ENDPOINT_STREAM_CHANGE_MASK_PROPS,
      PW_ENDPOINT_STREAM_CHANGE_MASK_PARAMS);
}

static const struct pw_endpoint_stream_events endpoint_stream_events = {
  PW_VERSION_ENDPOINT_STREAM_EVENTS,
  .info = endpoint_stream_event_info,
  .param = wp_pipewire_object_mixin_handle_event_param,
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
wp_endpoint_stream_pw_proxy_destroyed (WpProxy * proxy)
{
  WpEndpointStream *self = WP_ENDPOINT_STREAM (proxy);
  WpEndpointStreamPrivate *priv = wp_endpoint_stream_get_instance_private (self);

  g_clear_pointer (&priv->properties, wp_properties_unref);
  g_clear_pointer (&priv->info, pw_endpoint_stream_info_free);
  wp_object_update_features (WP_OBJECT (proxy), 0,
      WP_PIPEWIRE_OBJECT_FEATURE_INFO);

  wp_pipewire_object_mixin_deactivate (WP_OBJECT (proxy),
      WP_OBJECT_FEATURES_ALL);
}

static void
wp_endpoint_stream_class_init (WpEndpointStreamClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpObjectClass *wpobject_class = (WpObjectClass *) klass;
  WpProxyClass *proxy_class = (WpProxyClass *) klass;

  object_class->get_property = wp_endpoint_stream_get_property;

  wpobject_class->get_supported_features =
      wp_endpoint_stream_get_supported_features;
  wpobject_class->activate_get_next_step =
      wp_pipewire_object_mixin_activate_get_next_step;
  wpobject_class->activate_execute_step =
      wp_endpoint_stream_activate_execute_step;
  wpobject_class->deactivate = wp_endpoint_stream_deactivate;

  proxy_class->pw_iface_type = PW_TYPE_INTERFACE_EndpointStream;
  proxy_class->pw_iface_version = PW_VERSION_ENDPOINT_STREAM;
  proxy_class->pw_proxy_created = wp_endpoint_stream_pw_proxy_created;
  proxy_class->pw_proxy_destroyed = wp_endpoint_stream_pw_proxy_destroyed;

  wp_pipewire_object_mixin_class_override_properties (object_class);

  /**
   * WpEndpointStream:name:
   *
   * The name of the endpoint stream
   */
  g_object_class_install_property (object_class, PROP_NAME,
      g_param_spec_string ("name", "name", "name", NULL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
}

static gconstpointer
wp_endpoint_stream_get_native_info (WpPipewireObject * obj)
{
  WpEndpointStream *self = WP_ENDPOINT_STREAM (obj);
  WpEndpointStreamPrivate *priv = wp_endpoint_stream_get_instance_private (self);

  return priv->info;
}

static WpProperties *
wp_endpoint_stream_get_properties (WpPipewireObject * obj)
{
  WpEndpointStream *self = WP_ENDPOINT_STREAM (obj);
  WpEndpointStreamPrivate *priv = wp_endpoint_stream_get_instance_private (self);

  return wp_properties_ref (priv->properties);
}

static GVariant *
wp_endpoint_stream_get_param_info (WpPipewireObject * obj)
{
  WpEndpointStream *self = WP_ENDPOINT_STREAM (obj);
  WpEndpointStreamPrivate *priv = wp_endpoint_stream_get_instance_private (self);

  return wp_pipewire_object_mixin_param_info_to_gvariant (priv->info->params,
      priv->info->n_params);
}

static void
wp_endpoint_stream_enum_params (WpPipewireObject * obj, const gchar * id,
    WpSpaPod *filter, GCancellable * cancellable,
    GAsyncReadyCallback callback, gpointer user_data)
{
  wp_pipewire_object_mixin_enum_params (pw_endpoint_stream, obj, id, filter,
      cancellable, callback, user_data);
}

static void
wp_endpoint_stream_set_param (WpPipewireObject * obj, const gchar * id,
    WpSpaPod * param)
{
  wp_pipewire_object_mixin_set_param (pw_endpoint_stream, obj, id, param);
}

static void
wp_endpoint_stream_pipewire_object_interface_init (
    WpPipewireObjectInterface * iface)
{
  iface->get_native_info = wp_endpoint_stream_get_native_info;
  iface->get_properties = wp_endpoint_stream_get_properties;
  iface->get_param_info = wp_endpoint_stream_get_param_info;
  iface->enum_params = wp_endpoint_stream_enum_params;
  iface->enum_params_finish = wp_pipewire_object_mixin_enum_params_finish;
  iface->enum_cached_params = wp_pipewire_object_mixin_enum_cached_params;
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

  WpEndpointStreamPrivate *priv = wp_endpoint_stream_get_instance_private (self);
  return priv->info->name;
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

// static struct spa_param_info impl_param_info[] = {
//   SPA_PARAM_INFO (SPA_PARAM_Props, SPA_PARAM_INFO_READWRITE),
//   SPA_PARAM_INFO (SPA_PARAM_PropInfo, SPA_PARAM_INFO_READ)
// };

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
  if (!priv->properties)
    priv->properties = wp_properties_new_empty ();
  priv->properties = wp_properties_ensure_unique_owner (priv->properties);
  wp_properties_update (priv->properties, global_props);

  self->info.props = priv->properties ?
      (struct spa_dict *) wp_properties_peek_dict (priv->properties) : NULL;
}

static void
on_si_stream_properties_changed (WpSiStream * item, WpImplEndpointStream * self)
{
  populate_properties (self,
      wp_global_proxy_get_global_properties (WP_GLOBAL_PROXY (self)));
  g_object_notify (G_OBJECT (self), "properties");

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
  STEP_ACTIVATE_NODE = WP_PIPEWIRE_OBJECT_MIXIN_STEP_CUSTOM_START,
};

static guint
wp_impl_endpoint_stream_activate_get_next_step (WpObject * object,
    WpFeatureActivationTransition * transition, guint step,
    WpObjectFeatures missing)
{
  WpImplEndpointStream *self = WP_IMPL_ENDPOINT_STREAM (object);

  /* bind if not already bound */
  if (missing & WP_PROXY_FEATURE_BOUND) {
    g_autoptr (WpObject) node = wp_session_item_get_associated_proxy (
        WP_SESSION_ITEM (self->item), WP_TYPE_NODE);

    /* if the item has a node, cache its props so that enum_params works */
    if (node && !(wp_object_get_active_features (node) &
                      WP_PIPEWIRE_OBJECT_FEATURE_PARAM_PROPS))
      return STEP_ACTIVATE_NODE;
    else
      return WP_PIPEWIRE_OBJECT_MIXIN_STEP_BIND;
  }
  /* cache info if supported */
  else
    return WP_PIPEWIRE_OBJECT_MIXIN_STEP_CACHE_INFO;
}

static void
wp_impl_endpoint_stream_node_activated (WpObject * node,
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
wp_impl_endpoint_stream_activate_execute_step (WpObject * object,
    WpFeatureActivationTransition * transition, guint step,
    WpObjectFeatures missing)
{
  WpImplEndpointStream *self = WP_IMPL_ENDPOINT_STREAM (object);
  WpEndpointStreamPrivate *priv =
      wp_endpoint_stream_get_instance_private (WP_ENDPOINT_STREAM (self));

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
  case WP_PIPEWIRE_OBJECT_MIXIN_STEP_BIND: {
    g_autoptr (GVariantIter) immutable_properties = NULL;
    g_autoptr (WpProperties) properties = NULL;
    const gchar *key, *value;
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

    wp_debug_object (self, "exporting");

    /* get info from the interface */
    {
      g_autoptr (GVariant) info = NULL;
      info = wp_si_stream_get_registration_info (self->item);
      g_variant_get (info, "(sa{ss})", &self->info.name, &immutable_properties);

      /* associate with the endpoint */
      self->info.endpoint_id = wp_session_item_get_associated_proxy_id (
          WP_SESSION_ITEM (self->item), WP_TYPE_ENDPOINT);
    }

    /* construct export properties (these will come back through
        the registry and appear in wp_proxy_get_global_properties) */
    properties = wp_properties_new (
        PW_KEY_ENDPOINT_STREAM_NAME, self->info.name,
        NULL);
    wp_properties_setf (properties, PW_KEY_ENDPOINT_ID,
        "%d", self->info.endpoint_id);

    /* populate immutable (global) properties */
    while (g_variant_iter_next (immutable_properties, "{&s&s}", &key, &value))
      wp_properties_set (properties, key, value);

    /* populate standard properties */
    populate_properties (self, properties);

    /* subscribe to changes */
    g_signal_connect_object (self->item, "stream-properties-changed",
        G_CALLBACK (on_si_stream_properties_changed), self, 0);

    /* finalize info struct */
    self->info.version = PW_VERSION_ENDPOINT_STREAM_INFO;
    self->info.params = NULL;
    self->info.n_params = 0;
    priv->info = &self->info;

    /* bind */
    wp_proxy_set_pw_proxy (WP_PROXY (self), pw_core_export (pw_core,
            PW_TYPE_INTERFACE_EndpointStream,
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
    WP_OBJECT_CLASS (wp_impl_endpoint_stream_parent_class)->
        activate_execute_step (object, transition, step, missing);
    break;
  }
}

static void
wp_impl_endpoint_stream_pw_proxy_destroyed (WpProxy * proxy)
{
  WpImplEndpointStream *self = WP_IMPL_ENDPOINT_STREAM (proxy);
  WpEndpointStreamPrivate *priv =
      wp_endpoint_stream_get_instance_private (WP_ENDPOINT_STREAM (self));

  g_signal_handlers_disconnect_by_data (self->item, self);
  g_clear_pointer (&priv->properties, wp_properties_unref);
  g_clear_pointer (&self->info.name, g_free);
  priv->info = NULL;
  wp_object_update_features (WP_OBJECT (proxy), 0,
      WP_PIPEWIRE_OBJECT_FEATURE_INFO
      /*| WP_PIPEWIRE_OBJECT_FEATURE_PARAM_PROPS */);
}

static void
wp_impl_endpoint_stream_class_init (WpImplEndpointStreamClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpObjectClass *wpobject_class = (WpObjectClass *) klass;
  WpProxyClass *proxy_class = (WpProxyClass *) klass;

  object_class->set_property = wp_impl_endpoint_stream_set_property;
  object_class->get_property = wp_impl_endpoint_stream_get_property;

  wpobject_class->activate_get_next_step =
      wp_impl_endpoint_stream_activate_get_next_step;
  wpobject_class->activate_execute_step =
      wp_impl_endpoint_stream_activate_execute_step;

  proxy_class->pw_proxy_created = NULL;
  proxy_class->pw_proxy_destroyed = wp_impl_endpoint_stream_pw_proxy_destroyed;

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
