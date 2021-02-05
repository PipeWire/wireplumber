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
#include "object-manager.h"
#include "error.h"
#include "debug.h"
#include "wpenums.h"
#include "private/impl-endpoint.h"
#include "private/pipewire-object-mixin.h"

#include <pipewire/extensions/session-manager.h>
#include <pipewire/extensions/session-manager/introspect-funcs.h>

enum {
  SIGNAL_ENDPOINTS_CHANGED,
  SIGNAL_LINKS_CHANGED,
  N_SIGNALS,
};

static guint32 signals[N_SIGNALS] = {0};

typedef struct _WpSessionPrivate WpSessionPrivate;
struct _WpSessionPrivate
{
  WpObjectManager *endpoints_om;
  WpObjectManager *links_om;
};

static void wp_session_pw_object_mixin_priv_interface_init (
    WpPwObjectMixinPrivInterface * iface);

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
    G_IMPLEMENT_INTERFACE (WP_TYPE_PIPEWIRE_OBJECT,
        wp_pw_object_mixin_object_interface_init)
    G_IMPLEMENT_INTERFACE (WP_TYPE_PW_OBJECT_MIXIN_PRIV,
        wp_session_pw_object_mixin_priv_interface_init))

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
  return wp_pw_object_mixin_get_supported_features(object)
      | WP_SESSION_FEATURE_ENDPOINTS
      | WP_SESSION_FEATURE_LINKS;
}

enum {
  STEP_CHILDREN = WP_PW_OBJECT_MIXIN_STEP_CUSTOM_START,
};

static void
wp_session_activate_execute_step (WpObject * object,
    WpFeatureActivationTransition * transition, guint step,
    WpObjectFeatures missing)
{
  switch (step) {
  case WP_PW_OBJECT_MIXIN_STEP_BIND:
  case WP_TRANSITION_STEP_ERROR:
    /* base class can handle BIND and ERROR */
    WP_OBJECT_CLASS (wp_session_parent_class)->
        activate_execute_step (object, transition, step, missing);
    break;
  case WP_PW_OBJECT_MIXIN_STEP_WAIT_INFO:
    /* just wait, info will be emitted anyway after binding */
    break;
  case WP_PW_OBJECT_MIXIN_STEP_CACHE_PARAMS:
    wp_pw_object_mixin_cache_params (object, missing);
    break;
  case STEP_CHILDREN:
    wp_session_enable_features_endpoints_links (WP_SESSION (object),
        missing);
    break;
  default:
    g_assert_not_reached ();
  }
}

static void
wp_session_deactivate (WpObject * object, WpObjectFeatures features)
{
  WpSession *self = WP_SESSION (object);
  WpSessionPrivate *priv = wp_session_get_instance_private (self);

  wp_pw_object_mixin_deactivate (object, features);

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

static const struct pw_session_events session_events = {
  PW_VERSION_SESSION_EVENTS,
  .info = (HandleEventInfoFunc(session)) wp_pw_object_mixin_handle_event_info,
  .param = wp_pw_object_mixin_handle_event_param,
};

static void
wp_session_pw_proxy_created (WpProxy * proxy, struct pw_proxy * pw_proxy)
{
  wp_pw_object_mixin_handle_pw_proxy_created (proxy, pw_proxy,
      session, &session_events);
}

static void
wp_session_pw_proxy_destroyed (WpProxy * proxy)
{
  WpSession *self = WP_SESSION (proxy);
  WpSessionPrivate *priv = wp_session_get_instance_private (self);

  wp_pw_object_mixin_handle_pw_proxy_destroyed (proxy);

  g_clear_object (&priv->endpoints_om);
  g_clear_object (&priv->links_om);
  wp_object_update_features (WP_OBJECT (self), 0,
      WP_SESSION_FEATURE_ENDPOINTS |
      WP_SESSION_FEATURE_LINKS);
}

static void
wp_session_class_init (WpSessionClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpObjectClass *wpobject_class = (WpObjectClass *) klass;
  WpProxyClass *proxy_class = (WpProxyClass *) klass;

  object_class->get_property = wp_pw_object_mixin_get_property;

  wpobject_class->get_supported_features = wp_session_get_supported_features;
  wpobject_class->activate_get_next_step =
      wp_pw_object_mixin_activate_get_next_step;
  wpobject_class->activate_execute_step = wp_session_activate_execute_step;
  wpobject_class->deactivate = wp_session_deactivate;

  proxy_class->pw_iface_type = PW_TYPE_INTERFACE_Session;
  proxy_class->pw_iface_version = PW_VERSION_SESSION;
  proxy_class->pw_proxy_created = wp_session_pw_proxy_created;
  proxy_class->pw_proxy_destroyed = wp_session_pw_proxy_destroyed;

  wp_pw_object_mixin_class_override_properties (object_class);

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

static gint
wp_session_enum_params (gpointer instance, guint32 id,
    guint32 start, guint32 num, WpSpaPod *filter)
{
  WpPwObjectMixinData *d = wp_pw_object_mixin_get_data (instance);
  return pw_session_enum_params (d->iface, 0, id, start, num,
      filter ? wp_spa_pod_get_spa_pod (filter) : NULL);
}

static gint
wp_session_set_param (gpointer instance, guint32 id, guint32 flags,
    WpSpaPod * param)
{
  WpPwObjectMixinData *d = wp_pw_object_mixin_get_data (instance);
  return pw_session_set_param (d->iface, id, flags,
      wp_spa_pod_get_spa_pod (param));
}

static void
wp_session_pw_object_mixin_priv_interface_init (
    WpPwObjectMixinPrivInterface * iface)
{
  wp_pw_object_mixin_priv_interface_info_init (iface, session, SESSION);
  iface->enum_params = wp_session_enum_params;
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

  return wp_pipewire_object_get_property (WP_PIPEWIRE_OBJECT (self),
      "session.name");
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
 * wp_session_new_endpoints_iterator:
 * @self: the session
 *
 * Requires %WP_SESSION_FEATURE_ENDPOINTS
 *
 * Returns: (transfer full): a #WpIterator that iterates over all
 *   the endpoints that belong to this session
 */
WpIterator *
wp_session_new_endpoints_iterator (WpSession * self)
{
  g_return_val_if_fail (WP_IS_SESSION (self), NULL);
  g_return_val_if_fail (wp_object_get_active_features (WP_OBJECT (self)) &
          WP_SESSION_FEATURE_ENDPOINTS, NULL);

  WpSessionPrivate *priv = wp_session_get_instance_private (self);
  return wp_object_manager_new_iterator (priv->endpoints_om);
}

/**
 * wp_session_new_endpoints_filtered_iterator:
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
wp_session_new_endpoints_filtered_iterator (WpSession * self, ...)
{
  WpObjectInterest *interest;
  va_list args;
  va_start (args, self);
  interest = wp_object_interest_new_valist (WP_TYPE_ENDPOINT, &args);
  va_end (args);
  return wp_session_new_endpoints_filtered_iterator_full (self, interest);
}

/**
 * wp_session_new_endpoints_filtered_iterator_full: (rename-to wp_session_new_endpoints_filtered_iterator)
 * @self: the session
 * @interest: (transfer full): the interest
 *
 * Requires %WP_SESSION_FEATURE_ENDPOINTS
 *
 * Returns: (transfer full): a #WpIterator that iterates over all
 *   the endpoints that belong to this session and match the @interest
 */
WpIterator *
wp_session_new_endpoints_filtered_iterator_full (WpSession * self,
    WpObjectInterest * interest)
{
  g_return_val_if_fail (WP_IS_SESSION (self), NULL);
  g_return_val_if_fail (wp_object_get_active_features (WP_OBJECT (self)) &
          WP_SESSION_FEATURE_ENDPOINTS, NULL);

  WpSessionPrivate *priv = wp_session_get_instance_private (self);
  return wp_object_manager_new_filtered_iterator_full (priv->endpoints_om,
      interest);
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
 * wp_session_new_links_iterator:
 * @self: the session
 *
 * Requires %WP_SESSION_FEATURE_LINKS
 *
 * Returns: (transfer full): a #WpIterator that iterates over all
 *   the endpoint links that belong to this session
 */
WpIterator *
wp_session_new_links_iterator (WpSession * self)
{
  g_return_val_if_fail (WP_IS_SESSION (self), NULL);
  g_return_val_if_fail (wp_object_get_active_features (WP_OBJECT (self)) &
          WP_SESSION_FEATURE_LINKS, NULL);

  WpSessionPrivate *priv = wp_session_get_instance_private (self);
  return wp_object_manager_new_iterator (priv->links_om);
}

/**
 * wp_session_new_links_filtered_iterator:
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
wp_session_new_links_filtered_iterator (WpSession * self, ...)
{
  WpObjectInterest *interest;
  va_list args;
  va_start (args, self);
  interest = wp_object_interest_new_valist (WP_TYPE_ENDPOINT_LINK, &args);
  va_end (args);
  return wp_session_new_links_filtered_iterator_full (self, interest);
}

/**
 * wp_session_new_links_filtered_iterator_full: (rename-to wp_session_new_links_filtered_iterator)
 * @self: the session
 * @interest: (transfer full): the interest
 *
 * Requires %WP_SESSION_FEATURE_LINKS
 *
 * Returns: (transfer full): a #WpIterator that iterates over all
 *   the links that belong to this session and match the @interest
 */
WpIterator *
wp_session_new_links_filtered_iterator_full (WpSession * self,
    WpObjectInterest * interest)
{
  g_return_val_if_fail (WP_IS_SESSION (self), NULL);
  g_return_val_if_fail (wp_object_get_active_features (WP_OBJECT (self)) &
          WP_SESSION_FEATURE_LINKS, NULL);

  WpSessionPrivate *priv = wp_session_get_instance_private (self);
  return wp_object_manager_new_filtered_iterator_full (priv->links_om,
      interest);
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
  struct pw_session_info info;
};


static void wp_session_impl_pw_object_mixin_priv_interface_init (
    WpPwObjectMixinPrivInterface * iface);

/**
 * WpImplSession:
 *
 * A #WpImplSession allows implementing a session and exporting it to PipeWire.
 * To export a #WpImplSession, activate %WP_PROXY_FEATURE_BOUND.
 */
G_DEFINE_TYPE_WITH_CODE (WpImplSession, wp_impl_session, WP_TYPE_SESSION,
    G_IMPLEMENT_INTERFACE (WP_TYPE_PW_OBJECT_MIXIN_PRIV,
        wp_session_impl_pw_object_mixin_priv_interface_init))

static const struct pw_session_methods impl_session = {
  PW_VERSION_SESSION_METHODS,
  .add_listener =
      (ImplAddListenerFunc(session)) wp_pw_object_mixin_impl_add_listener,
  .subscribe_params = wp_pw_object_mixin_impl_subscribe_params,
  .enum_params = wp_pw_object_mixin_impl_enum_params,
  .set_param = wp_pw_object_mixin_impl_set_param,
};

static void
wp_impl_session_init (WpImplSession * self)
{
  WpPwObjectMixinData *d = wp_pw_object_mixin_get_data (self);

  self->iface = SPA_INTERFACE_INIT (
      PW_TYPE_INTERFACE_Session,
      PW_VERSION_SESSION,
      &impl_session, self);

  d->info = &self->info;
  d->iface = &self->iface;

  /* prepare INFO */
  d->properties = wp_properties_new_empty ();
  self->info.version = PW_VERSION_SESSION_INFO;
  self->info.props = (struct spa_dict *) wp_properties_peek_dict (d->properties);
  self->info.params = NULL;
  self->info.n_params = 0;

  wp_object_update_features (WP_OBJECT (self),
        WP_PIPEWIRE_OBJECT_FEATURE_INFO, 0);
}

static void
wp_impl_session_dispose (GObject * object)
{
  wp_object_update_features (WP_OBJECT (object), 0,
      WP_PIPEWIRE_OBJECT_FEATURE_INFO);

  G_OBJECT_CLASS (wp_impl_session_parent_class)->dispose (object);
}

static void
wp_impl_session_activate_execute_step (WpObject * object,
    WpFeatureActivationTransition * transition, guint step,
    WpObjectFeatures missing)
{
  WpImplSession *self = WP_IMPL_SESSION (object);
  WpPwObjectMixinData *d = wp_pw_object_mixin_get_data (self);

  switch (step) {
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

    /* make sure these props are not present; they are added by the server */
    wp_properties_set (d->properties, PW_KEY_OBJECT_ID, NULL);
    wp_properties_set (d->properties, PW_KEY_CLIENT_ID, NULL);
    wp_properties_set (d->properties, PW_KEY_FACTORY_ID, NULL);

    wp_proxy_set_pw_proxy (WP_PROXY (self), pw_core_export (pw_core,
            PW_TYPE_INTERFACE_Session,
            wp_properties_peek_dict (d->properties),
            &self->iface, 0));
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
      WP_SESSION_FEATURE_ENDPOINTS |
      WP_SESSION_FEATURE_LINKS);
}

static void
wp_impl_session_class_init (WpImplSessionClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpObjectClass *wpobject_class = (WpObjectClass *) klass;
  WpProxyClass *proxy_class = (WpProxyClass *) klass;

  object_class->dispose = wp_impl_session_dispose;

  wpobject_class->activate_execute_step = wp_impl_session_activate_execute_step;

  proxy_class->pw_proxy_created = NULL;
  proxy_class->pw_proxy_destroyed = wp_impl_session_pw_proxy_destroyed;
}

#define pw_session_emit(hooks,method,version,...) \
    spa_hook_list_call_simple(hooks, struct pw_session_events, \
        method, version, ##__VA_ARGS__)

static void
wp_impl_session_emit_info (struct spa_hook_list * hooks, gconstpointer info)
{
  pw_session_emit (hooks, info, 0, info);
}

static void
wp_impl_session_emit_param (struct spa_hook_list * hooks, int seq,
      guint32 id, guint32 index, guint32 next, const struct spa_pod *param)
{
  pw_session_emit (hooks, param, 0, seq, id, index, next, param);
}

static void
wp_session_impl_pw_object_mixin_priv_interface_init (
    WpPwObjectMixinPrivInterface * iface)
{
  iface->flags |= WP_PW_OBJECT_MIXIN_PRIV_NO_PARAM_CACHE;
  iface->emit_info = wp_impl_session_emit_info;
  iface->emit_param = wp_impl_session_emit_param;
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
  g_return_if_fail (WP_IS_IMPL_SESSION (self));

  WpPwObjectMixinData *d = wp_pw_object_mixin_get_data (self);
  wp_properties_set (d->properties, key, value);
  wp_pw_object_mixin_notify_info (self, PW_SESSION_CHANGE_MASK_PROPS);
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
  g_return_if_fail (WP_IS_IMPL_SESSION (self));

  WpPwObjectMixinData *d = wp_pw_object_mixin_get_data (self);
  wp_properties_update (d->properties, updates);
  wp_pw_object_mixin_notify_info (self, PW_SESSION_CHANGE_MASK_PROPS);
}
