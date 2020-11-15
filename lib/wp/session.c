/* WirePlumber
 *
 * Copyright © 2019-2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * SECTION: session
 * @title: PipeWire Session
 */

#define G_LOG_DOMAIN "wp-session"

#include "session.h"
#include "error.h"
#include "wpenums.h"
#include "private/impl-endpoint.h"
#include "private/pipewire-object-mixin.h"

#include <pipewire/extensions/session-manager.h>
#include <pipewire/extensions/session-manager/introspect-funcs.h>

enum {
  SIGNAL_DEFAULT_ENDPOINT_CHANGED,
  SIGNAL_ENDPOINTS_CHANGED,
  SIGNAL_LINKS_CHANGED,
  N_SIGNALS,
};

static guint32 signals[N_SIGNALS] = {0};

typedef struct _WpSessionPrivate WpSessionPrivate;
struct _WpSessionPrivate
{
  WpProperties *properties;
  struct pw_session_info *info;
  struct pw_session *iface;
  struct spa_hook listener;
  WpObjectManager *endpoints_om;
  WpObjectManager *links_om;
};

static void wp_session_pipewire_object_interface_init (WpPipewireObjectInterface * iface);

/**
 * WpSession:
 *
 * The #WpSession class allows accessing the properties and methods of a
 * PipeWire session object (`struct pw_session` from the session-manager
 * extension).
 *
 * A #WpSession is constructed internally when a new session appears on the
 * PipeWire registry and it is made available through the #WpObjectManager API.
 */
G_DEFINE_TYPE_WITH_CODE (WpSession, wp_session, WP_TYPE_GLOBAL_PROXY,
    G_ADD_PRIVATE (WpSession)
    G_IMPLEMENT_INTERFACE (WP_TYPE_PIPEWIRE_OBJECT, wp_session_pipewire_object_interface_init));

static void
wp_session_init (WpSession * self)
{
}

static void
wp_session_on_endpoints_om_installed (WpObjectManager *endpoints_om,
    WpSession * self)
{
  wp_object_update_features (WP_OBJECT (self), WP_SESSION_FEATURE_ENDPOINTS, 0);
}

static void
wp_session_emit_endpoints_changed (WpObjectManager *endpoints_om,
    WpSession * self)
{
  g_signal_emit (self, signals[SIGNAL_ENDPOINTS_CHANGED], 0);
}

static void
wp_session_on_links_om_installed (WpObjectManager *links_om, WpSession * self)
{
  wp_object_update_features (WP_OBJECT (self), WP_SESSION_FEATURE_LINKS, 0);
}

static void
wp_session_emit_links_changed (WpObjectManager *links_om, WpSession * self)
{
  g_signal_emit (self, signals[SIGNAL_LINKS_CHANGED], 0);
}

static void
wp_session_enable_features_endpoints_links (WpSession * self,
    WpObjectFeatures missing)
{
  WpSessionPrivate *priv = wp_session_get_instance_private (self);
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));
  guint32 bound_id = wp_proxy_get_bound_id (WP_PROXY (self));

  if (missing & WP_SESSION_FEATURE_ENDPOINTS) {
    wp_debug_object (self, "enabling WP_SESSION_FEATURE_ENDPOINTS, bound_id:%u",
        bound_id);

    priv->endpoints_om = wp_object_manager_new ();
    /* proxy endpoint -> check for session.id in global properties */
    wp_object_manager_add_interest (priv->endpoints_om,
        WP_TYPE_ENDPOINT,
        WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY, PW_KEY_SESSION_ID, "=u", bound_id,
        NULL);
    /* impl endpoint -> check for session.id in standard properties */
    wp_object_manager_add_interest (priv->endpoints_om,
        WP_TYPE_IMPL_ENDPOINT,
        WP_CONSTRAINT_TYPE_PW_PROPERTY, PW_KEY_SESSION_ID, "=u", bound_id,
        NULL);

    wp_object_manager_request_object_features (priv->endpoints_om,
        WP_TYPE_ENDPOINT, WP_OBJECT_FEATURES_ALL);

    g_signal_connect_object (priv->endpoints_om, "installed",
        G_CALLBACK (wp_session_on_endpoints_om_installed), self, 0);
    g_signal_connect_object (priv->endpoints_om, "objects-changed",
        G_CALLBACK (wp_session_emit_endpoints_changed), self, 0);

    wp_core_install_object_manager (core, priv->endpoints_om);
  }

  if (missing & WP_SESSION_FEATURE_LINKS) {
    wp_debug_object (self, "enabling WP_SESSION_FEATURE_LINKS, bound_id:%u",
        bound_id);

    priv->links_om = wp_object_manager_new ();
    /* proxy link -> check for session.id in global properties */
    wp_object_manager_add_interest (priv->links_om,
        WP_TYPE_ENDPOINT_LINK,
        WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY, PW_KEY_SESSION_ID, "=u", bound_id,
        NULL);
    /* impl link -> check for session.id in standard properties */
    wp_object_manager_add_interest (priv->links_om,
        WP_TYPE_IMPL_ENDPOINT_LINK,
        WP_CONSTRAINT_TYPE_PW_PROPERTY, PW_KEY_SESSION_ID, "=u", bound_id,
        NULL);

    wp_object_manager_request_object_features (priv->links_om,
        WP_TYPE_ENDPOINT_LINK, WP_OBJECT_FEATURES_ALL);

    g_signal_connect_object (priv->links_om, "installed",
        G_CALLBACK (wp_session_on_links_om_installed), self, 0);
    g_signal_connect_object (priv->links_om, "objects-changed",
        G_CALLBACK (wp_session_emit_links_changed), self, 0);

    wp_core_install_object_manager (core, priv->links_om);
  }
}

static WpObjectFeatures
wp_session_get_supported_features (WpObject * object)
{
  WpSession *self = WP_SESSION (object);
  WpSessionPrivate *priv = wp_session_get_instance_private (self);

  return
      WP_PROXY_FEATURE_BOUND |
      WP_SESSION_FEATURE_ENDPOINTS |
      WP_SESSION_FEATURE_LINKS |
      WP_PIPEWIRE_OBJECT_FEATURE_INFO |
      wp_pipewire_object_mixin_param_info_to_features (
          priv->info ? priv->info->params : NULL,
          priv->info ? priv->info->n_params : 0);
}

enum {
  STEP_CHILDREN = WP_PIPEWIRE_OBJECT_MIXIN_STEP_CUSTOM_START,
};

static guint
wp_session_activate_get_next_step (WpObject * object,
    WpFeatureActivationTransition * transition, guint step,
    WpObjectFeatures missing)
{
  step = wp_pipewire_object_mixin_activate_get_next_step (object, transition,
      step, missing);

  /* extend the mixin's state machine; when the only remaining feature(s) to
     enable are FEATURE_ENDPOINTS or FEATURE_LINKS, advance to STEP_CHILDREN */
  if (step == WP_PIPEWIRE_OBJECT_MIXIN_STEP_CACHE_INFO &&
      (missing & ~(WP_SESSION_FEATURE_ENDPOINTS | WP_SESSION_FEATURE_LINKS)) == 0)
    return STEP_CHILDREN;

  return step;
}

static void
wp_session_activate_execute_step (WpObject * object,
    WpFeatureActivationTransition * transition, guint step,
    WpObjectFeatures missing)
{
  switch (step) {
  case WP_PIPEWIRE_OBJECT_MIXIN_STEP_CACHE_INFO:
    wp_pipewire_object_mixin_cache_info (object, transition);
    break;
  case STEP_CHILDREN:
    wp_session_enable_features_endpoints_links (WP_SESSION (object),
        missing);
    break;
  default:
    WP_OBJECT_CLASS (wp_session_parent_class)->
        activate_execute_step (object, transition, step, missing);
    break;
  }
}

static void
wp_session_deactivate (WpObject * object, WpObjectFeatures features)
{
  WpSession *self = WP_SESSION (object);
  WpSessionPrivate *priv = wp_session_get_instance_private (self);

  wp_pipewire_object_mixin_deactivate (object, features);

  if (features & WP_SESSION_FEATURE_ENDPOINTS) {
    g_clear_object (&priv->endpoints_om);
    wp_object_update_features (object, 0, WP_SESSION_FEATURE_ENDPOINTS);
  }
  if (features & WP_SESSION_FEATURE_LINKS) {
    g_clear_object (&priv->links_om);
    wp_object_update_features (object, 0, WP_SESSION_FEATURE_LINKS);
  }

  WP_OBJECT_CLASS (wp_session_parent_class)->deactivate (object, features);
}

static void
session_event_info (void *data, const struct pw_session_info *info)
{
  WpSession *self = WP_SESSION (data);
  WpSessionPrivate *priv = wp_session_get_instance_private (self);

  priv->info = pw_session_info_update (priv->info, info);

  if (info->change_mask & PW_SESSION_CHANGE_MASK_PROPS) {
    g_clear_pointer (&priv->properties, wp_properties_unref);
    priv->properties = wp_properties_new_wrap_dict (priv->info->props);
  }

  wp_object_update_features (WP_OBJECT (self),
      WP_PIPEWIRE_OBJECT_FEATURE_INFO, 0);

  wp_pipewire_object_mixin_handle_event_info (self, info,
      PW_SESSION_CHANGE_MASK_PROPS, PW_SESSION_CHANGE_MASK_PARAMS);
}

static const struct pw_session_events session_events = {
  PW_VERSION_SESSION_EVENTS,
  .info = session_event_info,
  .param = wp_pipewire_object_mixin_handle_event_param,
};

static void
wp_session_pw_proxy_created (WpProxy * proxy, struct pw_proxy * pw_proxy)
{
  WpSession *self = WP_SESSION (proxy);
  WpSessionPrivate *priv = wp_session_get_instance_private (self);

  priv->iface = (struct pw_session *) pw_proxy;
  pw_session_add_listener (priv->iface, &priv->listener, &session_events, self);
}

static void
wp_session_pw_proxy_destroyed (WpProxy * proxy)
{
  WpSession *self = WP_SESSION (proxy);
  WpSessionPrivate *priv = wp_session_get_instance_private (self);

  g_clear_pointer (&priv->properties, wp_properties_unref);
  g_clear_pointer (&priv->info, pw_session_info_free);
  g_clear_object (&priv->endpoints_om);
  g_clear_object (&priv->links_om);
  wp_object_update_features (WP_OBJECT (self), 0,
      WP_PIPEWIRE_OBJECT_FEATURE_INFO |
      WP_SESSION_FEATURE_ENDPOINTS |
      WP_SESSION_FEATURE_LINKS);

  wp_pipewire_object_mixin_deactivate (WP_OBJECT (proxy),
      WP_OBJECT_FEATURES_ALL);
}

static void
wp_session_class_init (WpSessionClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpObjectClass *wpobject_class = (WpObjectClass *) klass;
  WpProxyClass *proxy_class = (WpProxyClass *) klass;

  object_class->get_property = wp_pipewire_object_mixin_get_property;

  wpobject_class->get_supported_features = wp_session_get_supported_features;
  wpobject_class->activate_get_next_step = wp_session_activate_get_next_step;
  wpobject_class->activate_execute_step = wp_session_activate_execute_step;
  wpobject_class->deactivate = wp_session_deactivate;

  proxy_class->pw_iface_type = PW_TYPE_INTERFACE_Session;
  proxy_class->pw_iface_version = PW_VERSION_SESSION;
  proxy_class->pw_proxy_created = wp_session_pw_proxy_created;
  proxy_class->pw_proxy_destroyed = wp_session_pw_proxy_destroyed;

  wp_pipewire_object_mixin_class_override_properties (object_class);

  /**
   * WpSession::default-endpoint-changed:
   * @self: the session
   * @direction: the endpoint direction
   * @id: the endpoint's bound id
   *
   * Emitted when the default endpoint of a specific direction changes.
   * The passed @id is the bound id (wp_proxy_get_bound_id()) of the new
   * default endpoint.
   */
  signals[SIGNAL_DEFAULT_ENDPOINT_CHANGED] = g_signal_new (
      "default-endpoint-changed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 2,
      WP_TYPE_DIRECTION, G_TYPE_UINT);

  /**
   * WpSession::endpoints-changed:
   * @self: the session
   *
   * Emitted when the sessions's endpoints change. This is only emitted
   * when %WP_SESSION_FEATURE_ENDPOINTS is enabled.
   */
  signals[SIGNAL_ENDPOINTS_CHANGED] = g_signal_new (
      "endpoints-changed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);

  /**
   * WpSession::links-changed:
   * @self: the session
   *
   * Emitted when the session's links change. This is only emitted
   * when %WP_SESSION_FEATURE_LINKS is enabled.
   */
  signals[SIGNAL_LINKS_CHANGED] = g_signal_new (
      "links-changed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static gconstpointer
wp_session_get_native_info (WpPipewireObject * obj)
{
  WpSession *self = WP_SESSION (obj);
  WpSessionPrivate *priv = wp_session_get_instance_private (self);

  return priv->info;
}

static WpProperties *
wp_session_get_properties (WpPipewireObject * obj)
{
  WpSession *self = WP_SESSION (obj);
  WpSessionPrivate *priv = wp_session_get_instance_private (self);

  return wp_properties_ref (priv->properties);
}

static GVariant *
wp_session_get_param_info (WpPipewireObject * obj)
{
  WpSession *self = WP_SESSION (obj);
  WpSessionPrivate *priv = wp_session_get_instance_private (self);

  return wp_pipewire_object_mixin_param_info_to_gvariant (priv->info->params,
      priv->info->n_params);
}

static void
wp_session_enum_params (WpPipewireObject * obj, const gchar * id,
    WpSpaPod *filter, GCancellable * cancellable,
    GAsyncReadyCallback callback, gpointer user_data)
{
  wp_pipewire_object_mixin_enum_params (pw_session, obj, id, filter,
      cancellable, callback, user_data);
}

static void
wp_session_set_param (WpPipewireObject * obj, const gchar * id,
    WpSpaPod * param)
{
  wp_pipewire_object_mixin_set_param (pw_session, obj, id, param);
}

static void
wp_session_pipewire_object_interface_init (
    WpPipewireObjectInterface * iface)
{
  iface->get_native_info = wp_session_get_native_info;
  iface->get_properties = wp_session_get_properties;
  iface->get_param_info = wp_session_get_param_info;
  iface->enum_params = wp_session_enum_params;
  iface->enum_params_finish = wp_pipewire_object_mixin_enum_params_finish;
  iface->enum_cached_params = wp_pipewire_object_mixin_enum_cached_params;
  iface->set_param = wp_session_set_param;
}

/**
 * wp_session_get_name:
 * @self: the session
 *
 * Requires %WP_PIPEWIRE_OBJECT_FEATURE_INFO
 *
 * Returns: (transfer none): the (unique) name of the session
 */
const gchar *
wp_session_get_name (WpSession * self)
{
  g_return_val_if_fail (WP_IS_SESSION (self), NULL);
  g_return_val_if_fail (wp_object_get_active_features (WP_OBJECT (self)) &
          WP_PIPEWIRE_OBJECT_FEATURE_INFO, NULL);

  WpSessionPrivate *priv = wp_session_get_instance_private (self);
  return wp_properties_get (priv->properties, "session.name");
}

/**
 * wp_session_get_n_endpoints:
 * @self: the session
 *
 * Requires %WP_SESSION_FEATURE_ENDPOINTS
 *
 * Returns: the number of endpoints of this session
 */
guint
wp_session_get_n_endpoints (WpSession * self)
{
  g_return_val_if_fail (WP_IS_SESSION (self), 0);
  g_return_val_if_fail (wp_object_get_active_features (WP_OBJECT (self)) &
          WP_SESSION_FEATURE_ENDPOINTS, 0);

  WpSessionPrivate *priv = wp_session_get_instance_private (self);
  return wp_object_manager_get_n_objects (priv->endpoints_om);
}

/**
 * wp_session_iterate_endpoints:
 * @self: the session
 *
 * Requires %WP_SESSION_FEATURE_ENDPOINTS
 *
 * Returns: (transfer full): a #WpIterator that iterates over all
 *   the endpoints that belong to this session
 */
WpIterator *
wp_session_iterate_endpoints (WpSession * self)
{
  g_return_val_if_fail (WP_IS_SESSION (self), NULL);
  g_return_val_if_fail (wp_object_get_active_features (WP_OBJECT (self)) &
          WP_SESSION_FEATURE_ENDPOINTS, NULL);

  WpSessionPrivate *priv = wp_session_get_instance_private (self);
  return wp_object_manager_iterate (priv->endpoints_om);
}

/**
 * wp_session_iterate_endpoints_filtered:
 * @self: the session
 * @...: a list of constraints, terminated by %NULL
 *
 * Requires %WP_SESSION_FEATURE_ENDPOINTS
 *
 * The constraints specified in the variable arguments must follow the rules
 * documented in wp_object_interest_new().
 *
 * Returns: (transfer full): a #WpIterator that iterates over all
 *   the endpoints that belong to this session and match the constraints
 */
WpIterator *
wp_session_iterate_endpoints_filtered (WpSession * self, ...)
{
  WpObjectInterest *interest;
  va_list args;
  va_start (args, self);
  interest = wp_object_interest_new_valist (WP_TYPE_ENDPOINT, &args);
  va_end (args);
  return wp_session_iterate_endpoints_filtered_full (self, interest);
}

/**
 * wp_session_iterate_endpoints_filtered_full: (rename-to wp_session_iterate_endpoints_filtered)
 * @self: the session
 * @interest: (transfer full): the interest
 *
 * Requires %WP_SESSION_FEATURE_ENDPOINTS
 *
 * Returns: (transfer full): a #WpIterator that iterates over all
 *   the endpoints that belong to this session and match the @interest
 */
WpIterator *
wp_session_iterate_endpoints_filtered_full (WpSession * self,
    WpObjectInterest * interest)
{
  g_return_val_if_fail (WP_IS_SESSION (self), NULL);
  g_return_val_if_fail (wp_object_get_active_features (WP_OBJECT (self)) &
          WP_SESSION_FEATURE_ENDPOINTS, NULL);

  WpSessionPrivate *priv = wp_session_get_instance_private (self);
  return wp_object_manager_iterate_filtered_full (priv->endpoints_om, interest);
}

/**
 * wp_session_lookup_endpoint:
 * @self: the session
 * @...: a list of constraints, terminated by %NULL
 *
 * Requires %WP_SESSION_FEATURE_ENDPOINTS
 *
 * The constraints specified in the variable arguments must follow the rules
 * documented in wp_object_interest_new().
 *
 * Returns: (transfer full) (nullable): the first endpoint that matches the
 *    constraints, or %NULL if there is no such endpoint
 */
WpEndpoint *
wp_session_lookup_endpoint (WpSession * self, ...)
{
  WpObjectInterest *interest;
  va_list args;
  va_start (args, self);
  interest = wp_object_interest_new_valist (WP_TYPE_ENDPOINT, &args);
  va_end (args);
  return wp_session_lookup_endpoint_full (self, interest);
}

/**
 * wp_session_lookup_endpoint_full: (rename-to wp_session_lookup_endpoint)
 * @self: the session
 * @interest: (transfer full): the interest
 *
 * Requires %WP_SESSION_FEATURE_ENDPOINTS
 *
 * Returns: (transfer full) (nullable): the first endpoint that matches the
 *    @interest, or %NULL if there is no such endpoint
 */
WpEndpoint *
wp_session_lookup_endpoint_full (WpSession * self, WpObjectInterest * interest)
{
  g_return_val_if_fail (WP_IS_SESSION (self), NULL);
  g_return_val_if_fail (wp_object_get_active_features (WP_OBJECT (self)) &
          WP_SESSION_FEATURE_ENDPOINTS, NULL);

  WpSessionPrivate *priv = wp_session_get_instance_private (self);
  return (WpEndpoint *)
      wp_object_manager_lookup_full (priv->endpoints_om, interest);
}

/**
 * wp_session_get_n_links:
 * @self: the session
 *
 * Requires %WP_SESSION_FEATURE_LINKS
 *
 * Returns: the number of endpoint links of this session
 */
guint
wp_session_get_n_links (WpSession * self)
{
  g_return_val_if_fail (WP_IS_SESSION (self), 0);
  g_return_val_if_fail (wp_object_get_active_features (WP_OBJECT (self)) &
          WP_SESSION_FEATURE_LINKS, 0);

  WpSessionPrivate *priv = wp_session_get_instance_private (self);
  return wp_object_manager_get_n_objects (priv->links_om);
}

/**
 * wp_session_iterate_links:
 * @self: the session
 *
 * Requires %WP_SESSION_FEATURE_LINKS
 *
 * Returns: (transfer full): a #WpIterator that iterates over all
 *   the endpoint links that belong to this session
 */
WpIterator *
wp_session_iterate_links (WpSession * self)
{
  g_return_val_if_fail (WP_IS_SESSION (self), NULL);
  g_return_val_if_fail (wp_object_get_active_features (WP_OBJECT (self)) &
          WP_SESSION_FEATURE_LINKS, NULL);

  WpSessionPrivate *priv = wp_session_get_instance_private (self);
  return wp_object_manager_iterate (priv->links_om);
}

/**
 * wp_session_iterate_links_filtered:
 * @self: the session
 * @...: a list of constraints, terminated by %NULL
 *
 * Requires %WP_SESSION_FEATURE_LINKS
 *
 * The constraints specified in the variable arguments must follow the rules
 * documented in wp_object_interest_new().
 *
 * Returns: (transfer full): a #WpIterator that iterates over all
 *   the links that belong to this session and match the constraints
 */
WpIterator *
wp_session_iterate_links_filtered (WpSession * self, ...)
{
  WpObjectInterest *interest;
  va_list args;
  va_start (args, self);
  interest = wp_object_interest_new_valist (WP_TYPE_ENDPOINT_LINK, &args);
  va_end (args);
  return wp_session_iterate_links_filtered_full (self, interest);
}

/**
 * wp_session_iterate_links_filtered_full: (rename-to wp_session_iterate_links_filtered)
 * @self: the session
 * @interest: (transfer full): the interest
 *
 * Requires %WP_SESSION_FEATURE_LINKS
 *
 * Returns: (transfer full): a #WpIterator that iterates over all
 *   the links that belong to this session and match the @interest
 */
WpIterator *
wp_session_iterate_links_filtered_full (WpSession * self,
    WpObjectInterest * interest)
{
  g_return_val_if_fail (WP_IS_SESSION (self), NULL);
  g_return_val_if_fail (wp_object_get_active_features (WP_OBJECT (self)) &
          WP_SESSION_FEATURE_LINKS, NULL);

  WpSessionPrivate *priv = wp_session_get_instance_private (self);
  return wp_object_manager_iterate_filtered_full (priv->links_om, interest);
}

/**
 * wp_session_lookup_link:
 * @self: the session
 * @...: a list of constraints, terminated by %NULL
 *
 * Requires %WP_SESSION_FEATURE_LINKS
 *
 * The constraints specified in the variable arguments must follow the rules
 * documented in wp_object_interest_new().
 *
 * Returns: (transfer full) (nullable): the first link that matches the
 *    constraints, or %NULL if there is no such link
 */
WpEndpointLink *
wp_session_lookup_link (WpSession * self, ...)
{
  WpObjectInterest *interest;
  va_list args;
  va_start (args, self);
  interest = wp_object_interest_new_valist (WP_TYPE_ENDPOINT_LINK, &args);
  va_end (args);
  return wp_session_lookup_link_full (self, interest);
}

/**
 * wp_session_lookup_link_full: (rename-to wp_session_lookup_link)
 * @self: the session
 * @interest: (transfer full): the interest
 *
 * Requires %WP_SESSION_FEATURE_LINKS
 *
 * Returns: (transfer full) (nullable): the first link that matches the
 *    @interest, or %NULL if there is no such link
 */
WpEndpointLink *
wp_session_lookup_link_full (WpSession * self, WpObjectInterest * interest)
{
  g_return_val_if_fail (WP_IS_SESSION (self), NULL);
  g_return_val_if_fail (wp_object_get_active_features (WP_OBJECT (self)) &
          WP_SESSION_FEATURE_LINKS, NULL);

  WpSessionPrivate *priv = wp_session_get_instance_private (self);
  return (WpEndpointLink *)
      wp_object_manager_lookup_full (priv->links_om, interest);
}

/* WpImplSession */

typedef struct _WpImplSession WpImplSession;
struct _WpImplSession
{
  WpSession parent;

  struct spa_interface iface;
  struct spa_hook_list hooks;
  struct pw_session_info info;
};

/**
 * WpImplSession:
 *
 * A #WpImplSession allows implementing a session and exporting it to PipeWire.
 * To export a #WpImplSession, activate %WP_PROXY_FEATURE_BOUND.
 */
G_DEFINE_TYPE (WpImplSession, wp_impl_session, WP_TYPE_SESSION)

#define pw_session_emit(hooks,method,version,...) \
    spa_hook_list_call_simple(hooks, struct pw_session_events, \
        method, version, ##__VA_ARGS__)

#define pw_session_emit_info(hooks,...)  pw_session_emit(hooks, info, 0, ##__VA_ARGS__)
#define pw_session_emit_param(hooks,...) pw_session_emit(hooks, param, 0, ##__VA_ARGS__)

static int
impl_add_listener(void *object,
    struct spa_hook *listener,
    const struct pw_session_events *events,
    void *data)
{
  WpImplSession *self = WP_IMPL_SESSION (object);
  struct spa_hook_list save;

  spa_hook_list_isolate (&self->hooks, &save, listener, events, data);

  self->info.change_mask = PW_SESSION_CHANGE_MASK_ALL;
  pw_session_emit_info (&self->hooks, &self->info);
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

static const struct pw_session_methods impl_session = {
  PW_VERSION_SESSION_METHODS,
  .add_listener = impl_add_listener,
  .subscribe_params = impl_subscribe_params,
  .enum_params = impl_enum_params,
  .set_param = impl_set_param,
};

static void
wp_impl_session_init (WpImplSession * self)
{
  /* reuse the parent's private to optimize memory usage and to be able
     to re-use some of the parent's methods without reimplementing them */
  WpSessionPrivate *priv = wp_session_get_instance_private (WP_SESSION (self));

  self->iface = SPA_INTERFACE_INIT (
      PW_TYPE_INTERFACE_Session,
      PW_VERSION_SESSION,
      &impl_session, self);
  spa_hook_list_init (&self->hooks);

  priv->iface = (struct pw_session *) &self->iface;

  /* prepare INFO */
  priv->properties = wp_properties_new_empty ();
  self->info.version = PW_VERSION_SESSION_INFO;
  self->info.props =
      (struct spa_dict *) wp_properties_peek_dict (priv->properties);
  self->info.params = NULL;
  self->info.n_params = 0;
  priv->info = &self->info;
}

static void
wp_impl_session_finalize (GObject * object)
{
  WpSessionPrivate *priv = wp_session_get_instance_private (WP_SESSION (object));

  g_clear_pointer (&priv->properties, wp_properties_unref);

  G_OBJECT_CLASS (wp_impl_session_parent_class)->finalize (object);
}

static void
wp_impl_session_activate_execute_step (WpObject * object,
    WpFeatureActivationTransition * transition, guint step,
    WpObjectFeatures missing)
{
  WpImplSession *self = WP_IMPL_SESSION (object);
  WpSessionPrivate *priv = wp_session_get_instance_private (WP_SESSION (self));

  switch (step) {
  case WP_PIPEWIRE_OBJECT_MIXIN_STEP_BIND: {
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

    /* make sure these props are not present; they are added by the server */
    wp_properties_set (priv->properties, PW_KEY_OBJECT_ID, NULL);
    wp_properties_set (priv->properties, PW_KEY_CLIENT_ID, NULL);
    wp_properties_set (priv->properties, PW_KEY_FACTORY_ID, NULL);

    wp_proxy_set_pw_proxy (WP_PROXY (self), pw_core_export (pw_core,
            PW_TYPE_INTERFACE_Session,
            wp_properties_peek_dict (priv->properties),
            priv->iface, 0));

    wp_object_update_features (WP_OBJECT (self),
        WP_PIPEWIRE_OBJECT_FEATURE_INFO, 0);
    break;
  }
  default:
    WP_OBJECT_CLASS (wp_impl_session_parent_class)->
        activate_execute_step (object, transition, step, missing);
    break;
  }
}

static void
wp_impl_session_pw_proxy_destroyed (WpProxy * proxy)
{
  WpImplSession *self = WP_IMPL_SESSION (proxy);
  WpSessionPrivate *priv = wp_session_get_instance_private (WP_SESSION (self));

  g_clear_object (&priv->endpoints_om);
  g_clear_object (&priv->links_om);
  wp_object_update_features (WP_OBJECT (self), 0,
      WP_PIPEWIRE_OBJECT_FEATURE_INFO |
      WP_SESSION_FEATURE_ENDPOINTS |
      WP_SESSION_FEATURE_LINKS);
}

static void
wp_impl_session_class_init (WpImplSessionClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpObjectClass *wpobject_class = (WpObjectClass *) klass;
  WpProxyClass *proxy_class = (WpProxyClass *) klass;

  object_class->finalize = wp_impl_session_finalize;

  wpobject_class->activate_execute_step = wp_impl_session_activate_execute_step;

  proxy_class->pw_proxy_created = NULL;
  proxy_class->pw_proxy_destroyed = wp_impl_session_pw_proxy_destroyed;
}

/**
 * wp_impl_session_new:
 * @core: the #WpCore
 *
 * Returns: (transfer full): the newly constructed session implementation
 */
WpImplSession *
wp_impl_session_new (WpCore * core)
{
  g_return_val_if_fail (WP_IS_CORE (core), NULL);

  return g_object_new (WP_TYPE_IMPL_SESSION,
      "core", core,
      NULL);
}

/**
 * wp_impl_session_set_property:
 * @self: the session implementation
 * @key: a property key
 * @value: a property value
 *
 * Sets the specified property on the PipeWire properties of the session.
 *
 * If this property is set before exporting the session, then it is also used
 * in the construction process of the session object and appears as a global
 * property.
 */
void
wp_impl_session_set_property (WpImplSession * self,
    const gchar * key, const gchar * value)
{
  WpSessionPrivate *priv;

  g_return_if_fail (WP_IS_IMPL_SESSION (self));
  priv = wp_session_get_instance_private (WP_SESSION (self));

  wp_properties_set (priv->properties, key, value);

  g_object_notify (G_OBJECT (self), "properties");

  /* update only after the session has been exported */
  if (wp_object_get_active_features (WP_OBJECT (self)) & WP_PROXY_FEATURE_BOUND) {
    self->info.change_mask = PW_SESSION_CHANGE_MASK_PROPS;
    pw_session_emit_info (&self->hooks, &self->info);
    self->info.change_mask = 0;
  }
}

/**
 * wp_impl_session_update_properties:
 * @self: the session implementation
 * @updates: a set of properties to add or update in the session's properties
 *
 * Adds or updates the values of the PipeWire properties of the session
 * using the properties in @updates as a source.
 *
 * If the properties are set before exporting the session, then they are also
 * used in the construction process of the session object and appear as
 * global properties.
 */
void
wp_impl_session_update_properties (WpImplSession * self,
    WpProperties * updates)
{
  WpSessionPrivate *priv;

  g_return_if_fail (WP_IS_IMPL_SESSION (self));
  priv = wp_session_get_instance_private (WP_SESSION (self));

  wp_properties_update (priv->properties, updates);

  g_object_notify (G_OBJECT (self), "properties");

  /* update only after the session has been exported */
  if (wp_object_get_active_features (WP_OBJECT (self)) & WP_PROXY_FEATURE_BOUND) {
    self->info.change_mask = PW_SESSION_CHANGE_MASK_PROPS;
    pw_session_emit_info (&self->hooks, &self->info);
    self->info.change_mask = 0;
  }
}
