/* WirePlumber
 *
 * Copyright © 2019-2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * SECTION: WpSession
 *
 * The #WpSession class allows accessing the properties and methods of a
 * PipeWire session object (`struct pw_session` from the session-manager
 * extension).
 *
 * A #WpSession is constructed internally when a new session appears on the
 * PipeWire registry and it is made available through the #WpObjectManager API.
 *
 * A #WpImplSession allows implementing a session and exporting it to PipeWire,
 * which is done by augmenting the #WpImplSession with %WP_PROXY_FEATURE_BOUND.
 */

#define G_LOG_DOMAIN "wp-session"

#include "session.h"
#include "spa-type.h"
#include "spa-pod.h"
#include "debug.h"
#include "private.h"
#include "error.h"
#include "wpenums.h"

#include <pipewire/pipewire.h>
#include <pipewire/extensions/session-manager.h>
#include <pipewire/extensions/session-manager/introspect-funcs.h>

#include <spa/pod/builder.h>
#include <spa/pod/parser.h>
#include <spa/pod/filter.h>

enum {
  SIGNAL_DEFAULT_ENDPOINT_CHANGED,
  SIGNAL_ENDPOINTS_CHANGED,
  SIGNAL_LINKS_CHANGED,
  N_SIGNALS,
};

static guint32 signals[N_SIGNALS] = {0};

/* WpSession */

typedef struct _WpSessionPrivate WpSessionPrivate;
struct _WpSessionPrivate
{
  WpProperties *properties;
  struct pw_session_info *info;
  struct pw_session *iface;
  struct spa_hook listener;
  WpObjectManager *endpoints_om;
  WpObjectManager *links_om;
  gboolean ft_endpoints_requested;
  gboolean ft_links_requested;
};

G_DEFINE_TYPE_WITH_PRIVATE (WpSession, wp_session, WP_TYPE_PROXY)

static void
wp_session_init (WpSession * self)
{
}

static void
wp_session_finalize (GObject * object)
{
  WpSession *self = WP_SESSION (object);
  WpSessionPrivate *priv = wp_session_get_instance_private (self);

  g_clear_object (&priv->endpoints_om);
  g_clear_object (&priv->links_om);
  g_clear_pointer (&priv->info, pw_session_info_free);
  g_clear_pointer (&priv->properties, wp_properties_unref);

  G_OBJECT_CLASS (wp_session_parent_class)->finalize (object);
}

static void
wp_session_on_endpoints_om_installed (WpObjectManager *endpoints_om,
    WpSession * self)
{
  wp_proxy_set_feature_ready (WP_PROXY (self), WP_SESSION_FEATURE_ENDPOINTS);
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
  wp_proxy_set_feature_ready (WP_PROXY (self), WP_SESSION_FEATURE_LINKS);
}

static void
wp_session_emit_links_changed (WpObjectManager *links_om, WpSession * self)
{
  g_signal_emit (self, signals[SIGNAL_LINKS_CHANGED], 0);
}

static void
wp_session_ensure_features_endpoints_links (WpSession * self, guint32 bound_id)
{
  WpSessionPrivate *priv = wp_session_get_instance_private (self);
  WpProxyFeatures ft = wp_proxy_get_features (WP_PROXY (self));
  g_autoptr (WpCore) core = NULL;

  if (!(ft & WP_PROXY_FEATURE_BOUND))
    return;

  core = wp_proxy_get_core (WP_PROXY (self));
  if (!bound_id)
    bound_id = wp_proxy_get_bound_id (WP_PROXY (self));

  if (priv->ft_endpoints_requested && !priv->endpoints_om) {
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

    wp_object_manager_request_proxy_features (priv->endpoints_om,
        WP_TYPE_ENDPOINT, WP_ENDPOINT_FEATURES_STANDARD);

    g_signal_connect_object (priv->endpoints_om, "installed",
        G_CALLBACK (wp_session_on_endpoints_om_installed), self, 0);
    g_signal_connect_object (priv->endpoints_om, "objects-changed",
        G_CALLBACK (wp_session_emit_endpoints_changed), self, 0);

    wp_core_install_object_manager (core, priv->endpoints_om);
  }

  if (priv->ft_links_requested && !priv->links_om) {
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

    wp_object_manager_request_proxy_features (priv->links_om,
        WP_TYPE_ENDPOINT_LINK, WP_PROXY_FEATURES_STANDARD);

    g_signal_connect_object (priv->links_om, "installed",
        G_CALLBACK (wp_session_on_links_om_installed), self, 0);
    g_signal_connect_object (priv->links_om, "objects-changed",
        G_CALLBACK (wp_session_emit_links_changed), self, 0);

    wp_core_install_object_manager (core, priv->links_om);
  }
}

static gconstpointer
wp_session_get_info (WpProxy * proxy)
{
  WpSession *self = WP_SESSION (proxy);
  WpSessionPrivate *priv = wp_session_get_instance_private (self);

  return priv->info;
}

static WpProperties *
wp_session_get_properties (WpProxy * proxy)
{
  WpSession *self = WP_SESSION (proxy);
  WpSessionPrivate *priv = wp_session_get_instance_private (self);

  return wp_properties_ref (priv->properties);
}

static struct spa_param_info *
wp_session_get_param_info (WpProxy * proxy, guint * n_params)
{
  WpSession *self = WP_SESSION (proxy);
  WpSessionPrivate *priv = wp_session_get_instance_private (self);

  *n_params = priv->info->n_params;
  return priv->info->params;
}

static gint
wp_session_enum_params (WpProxy * self, guint32 id, guint32 start,
    guint32 num, const WpSpaPod * filter)
{
  WpSessionPrivate *priv = wp_session_get_instance_private (WP_SESSION (self));
  return pw_session_enum_params (priv->iface, 0, id,
      start, num, filter ? wp_spa_pod_get_spa_pod (filter) : NULL);
}

static gint
wp_session_subscribe_params (WpProxy * self, guint32 *ids, guint32 n_ids)
{
  WpSessionPrivate *priv = wp_session_get_instance_private (WP_SESSION (self));
  return pw_session_subscribe_params (priv->iface, ids, n_ids);
}

static gint
wp_session_set_param (WpProxy * self, guint32 id, guint32 flags,
    const WpSpaPod *param)
{
  WpSessionPrivate *priv = wp_session_get_instance_private (WP_SESSION (self));
  return pw_session_set_param (priv->iface, id, flags,
      wp_spa_pod_get_spa_pod (param));
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

  wp_proxy_set_feature_ready (WP_PROXY (self), WP_PROXY_FEATURE_INFO);
  g_object_notify (G_OBJECT (self), "info");

  if (info->change_mask & PW_SESSION_CHANGE_MASK_PROPS)
    g_object_notify (G_OBJECT (self), "properties");

  if (info->change_mask & PW_SESSION_CHANGE_MASK_PARAMS)
    g_object_notify (G_OBJECT (self), "param-info");
}

static const struct pw_session_events session_events = {
  PW_VERSION_SESSION_EVENTS,
  .info = session_event_info,
  .param = wp_proxy_handle_event_param,
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
wp_session_bound (WpProxy * proxy, guint32 id)
{
  WpSession *self = WP_SESSION (proxy);
  wp_session_ensure_features_endpoints_links (self, id);
}

static void
wp_session_augment (WpProxy * proxy, WpProxyFeatures features)
{
  WpSession *self = WP_SESSION (proxy);
  WpSessionPrivate *priv = wp_session_get_instance_private (self);

  /* call the parent impl first to ensure we have a pw proxy if necessary */
  WP_PROXY_CLASS (wp_session_parent_class)->augment (proxy, features);

  if (features & (WP_SESSION_FEATURE_ENDPOINTS | WP_SESSION_FEATURE_LINKS)) {
    priv->ft_endpoints_requested = (features & WP_SESSION_FEATURE_ENDPOINTS);
    priv->ft_links_requested = (features & WP_SESSION_FEATURE_LINKS);
    wp_session_ensure_features_endpoints_links (self, 0);
  }
}

static void
wp_session_prop_changed (WpProxy * proxy, const gchar * prop)
{
  WpSession *self = WP_SESSION (proxy);

  if (g_strcmp0 (prop, "Wp:defaultSink") == 0) {
    g_signal_emit (self, signals[SIGNAL_DEFAULT_ENDPOINT_CHANGED], 0,
        WP_DIRECTION_INPUT,
        wp_session_get_default_endpoint (self, WP_DIRECTION_INPUT));
  }
  else if (g_strcmp0 (prop, "Wp:defaultSource") == 0) {
    g_signal_emit (self, signals[SIGNAL_DEFAULT_ENDPOINT_CHANGED], 0,
        WP_DIRECTION_OUTPUT,
        wp_session_get_default_endpoint (self, WP_DIRECTION_OUTPUT));
  }
}

static void
wp_session_class_init (WpSessionClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpProxyClass *proxy_class = (WpProxyClass *) klass;

  object_class->finalize = wp_session_finalize;

  proxy_class->pw_iface_type = PW_TYPE_INTERFACE_Session;
  proxy_class->pw_iface_version = PW_VERSION_SESSION;

  proxy_class->augment = wp_session_augment;
  proxy_class->get_info = wp_session_get_info;
  proxy_class->get_properties = wp_session_get_properties;
  proxy_class->get_param_info = wp_session_get_param_info;
  proxy_class->enum_params = wp_session_enum_params;
  proxy_class->subscribe_params = wp_session_subscribe_params;
  proxy_class->set_param = wp_session_set_param;

  proxy_class->pw_proxy_created = wp_session_pw_proxy_created;
  proxy_class->bound = wp_session_bound;
  proxy_class->prop_changed = wp_session_prop_changed;

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

/**
 * wp_session_get_name:
 * @self: the session
 *
 * Returns: (transfer none): the (unique) name of the session
 */
const gchar *
wp_session_get_name (WpSession * self)
{
  g_return_val_if_fail (WP_IS_SESSION (self), NULL);
  g_return_val_if_fail (wp_proxy_get_features (WP_PROXY (self)) &
          WP_PROXY_FEATURE_INFO, NULL);

  WpSessionPrivate *priv = wp_session_get_instance_private (self);
  return wp_properties_get (priv->properties, "session.name");
}

/**
 * wp_session_get_default_endpoint:
 * @self: the session
 * @direction: the endpoint direction
 *
 * Returns: the bound id of the default endpoint of this @direction
 */
guint32
wp_session_get_default_endpoint (WpSession * self, WpDirection direction)
{
  g_autoptr (WpSpaPod) pod = NULL;
  const gchar *id_name = NULL;
  gint32 value;

  g_return_val_if_fail (WP_IS_SESSION (self), SPA_ID_INVALID);

  switch (direction) {
    case WP_DIRECTION_INPUT:
      id_name = "Wp:defaultSink";
      break;
    case WP_DIRECTION_OUTPUT:
      id_name = "Wp:defaultSource";
      break;
    default:
      g_return_val_if_reached (SPA_ID_INVALID);
      break;
  }

  pod = wp_proxy_get_prop (WP_PROXY (self), id_name);
  if (pod && wp_spa_pod_get_int (pod, &value))
    return (guint32) value;
  return 0;
}

/**
 * wp_session_set_default_endpoint:
 * @self: the session
 * @direction: the endpoint direction
 * @id: the bound id of the endpoint to set as the default for this @direction
 *
 * Sets the default endpoint for this @direction to be the one identified
 * with @id
 */
void
wp_session_set_default_endpoint (WpSession * self, WpDirection direction,
    guint32 id)
{
  const gchar *id_name = NULL;

  g_return_if_fail (WP_IS_SESSION (self));

  switch (direction) {
    case WP_DIRECTION_INPUT:
      id_name = "Wp:defaultSink";
      break;
    case WP_DIRECTION_OUTPUT:
      id_name = "Wp:defaultSource";
      break;
    default:
      g_return_if_reached ();
      break;
  }

  wp_proxy_set_prop (WP_PROXY (self), id_name, wp_spa_pod_new_int (id));
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
  g_return_val_if_fail (wp_proxy_get_features (WP_PROXY (self)) &
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
  g_return_val_if_fail (wp_proxy_get_features (WP_PROXY (self)) &
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
  g_return_val_if_fail (wp_proxy_get_features (WP_PROXY (self)) &
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
  g_return_val_if_fail (wp_proxy_get_features (WP_PROXY (self)) &
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
  g_return_val_if_fail (wp_proxy_get_features (WP_PROXY (self)) &
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
  g_return_val_if_fail (wp_proxy_get_features (WP_PROXY (self)) &
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
  g_return_val_if_fail (wp_proxy_get_features (WP_PROXY (self)) &
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
  g_return_val_if_fail (wp_proxy_get_features (WP_PROXY (self)) &
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
  gboolean subscribed;
};

G_DEFINE_TYPE (WpImplSession, wp_impl_session, WP_TYPE_SESSION)

#define pw_session_emit(hooks,method,version,...) \
    spa_hook_list_call_simple(hooks, struct pw_session_events, \
        method, version, ##__VA_ARGS__)

#define pw_session_emit_info(hooks,...)  pw_session_emit(hooks, info, 0, ##__VA_ARGS__)
#define pw_session_emit_param(hooks,...) pw_session_emit(hooks, param, 0, ##__VA_ARGS__)

static struct spa_param_info impl_param_info[] = {
  SPA_PARAM_INFO (SPA_PARAM_Props, SPA_PARAM_INFO_READWRITE),
  SPA_PARAM_INFO (SPA_PARAM_PropInfo, SPA_PARAM_INFO_READ)
};

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
  WpImplSession *self = WP_IMPL_SESSION (object);
  char buf[1024];
  struct spa_pod_builder b = SPA_POD_BUILDER_INIT (buf, sizeof (buf));
  struct spa_pod *result;
  guint count = 0;
  WpProps *props = wp_proxy_get_props (WP_PROXY (self));

  switch (id) {
    case SPA_PARAM_PropInfo: {
      g_autoptr (WpIterator) params = wp_props_iterate_prop_info (props);
      g_auto (GValue) item = G_VALUE_INIT;
      guint i = 0;

      for (; wp_iterator_next (params, &item); g_value_unset (&item), i++) {
        WpSpaPod *pod = g_value_get_boxed (&item);
        const struct spa_pod *param = wp_spa_pod_get_spa_pod (pod);
        if (spa_pod_filter (&b, &result, param, filter) == 0) {
          pw_session_emit_param (&self->hooks, seq, id, i, i+1, result);
          if (++count == num)
            break;
        }
      }
      break;
    }
    case SPA_PARAM_Props: {
      if (start == 0) {
        g_autoptr (WpSpaPod) pod = wp_props_get_all (props);
        const struct spa_pod *param = wp_spa_pod_get_spa_pod (pod);
        if (spa_pod_filter (&b, &result, param, filter) == 0) {
          pw_session_emit_param (&self->hooks, seq, id, 0, 1, result);
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
  WpImplSession *self = WP_IMPL_SESSION (object);
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
  WpImplSession *self = WP_IMPL_SESSION (object);

  if (id != SPA_PARAM_Props)
    return -ENOENT;

  WpProps *props = wp_proxy_get_props (WP_PROXY (self));
  wp_props_set (props, NULL, wp_spa_pod_new_wrap (param));
  return 0;
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
  WpProps *props;

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
  self->info.params = impl_param_info;
  self->info.n_params = SPA_N_ELEMENTS (impl_param_info);
  priv->info = &self->info;
  g_object_notify (G_OBJECT (self), "info");

  wp_proxy_set_feature_ready (WP_PROXY (self), WP_PROXY_FEATURE_INFO);

  /* prepare props */
  props = wp_props_new (WP_PROPS_MODE_STORE, WP_PROXY (self));
  wp_props_register (props,
      "Wp:defaultSource", "Default Source", wp_spa_pod_new_int (0));
  wp_props_register (props,
      "Wp:defaultSink", "Default Sink", wp_spa_pod_new_int (0));
  wp_proxy_set_props (WP_PROXY (self), props);
  wp_proxy_set_feature_ready (WP_PROXY (self), WP_PROXY_FEATURE_PROPS);
}

static void
wp_impl_session_finalize (GObject * object)
{
  WpSessionPrivate *priv = wp_session_get_instance_private (WP_SESSION (object));

  /* set to NULL to prevent parent's finalize from calling free() on it */
  priv->info = NULL;

  G_OBJECT_CLASS (wp_impl_session_parent_class)->finalize (object);
}

static void
wp_impl_session_augment (WpProxy * proxy, WpProxyFeatures features)
{
  WpImplSession *self = WP_IMPL_SESSION (proxy);
  WpSessionPrivate *priv = wp_session_get_instance_private (WP_SESSION (self));

  /* PW_PROXY depends on BOUND */
  if (features & WP_PROXY_FEATURE_PW_PROXY)
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

    /* make sure these props are not present; they are added by the server */
    wp_properties_set (priv->properties, PW_KEY_OBJECT_ID, NULL);
    wp_properties_set (priv->properties, PW_KEY_CLIENT_ID, NULL);
    wp_properties_set (priv->properties, PW_KEY_FACTORY_ID, NULL);

    wp_proxy_set_pw_proxy (proxy, pw_core_export (pw_core,
            PW_TYPE_INTERFACE_Session,
            wp_properties_peek_dict (priv->properties),
            priv->iface, 0));
  }

  if (features & (WP_SESSION_FEATURE_ENDPOINTS | WP_SESSION_FEATURE_LINKS)) {
    priv->ft_endpoints_requested = (features & WP_SESSION_FEATURE_ENDPOINTS);
    priv->ft_links_requested = (features & WP_SESSION_FEATURE_LINKS);
    wp_session_ensure_features_endpoints_links (WP_SESSION (self), 0);
  }
}

static void
wp_impl_session_prop_changed (WpProxy * proxy, const gchar * prop_name)
{
  WpImplSession *self = WP_IMPL_SESSION (proxy);

  /* notify subscribers */
  if (self->subscribed)
    impl_enum_params (self, 1, SPA_PARAM_Props, 0, UINT32_MAX, NULL);

  WP_PROXY_CLASS (wp_impl_session_parent_class)->prop_changed (proxy, prop_name);
}

static void
wp_impl_session_class_init (WpImplSessionClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpProxyClass *proxy_class = (WpProxyClass *) klass;

  object_class->finalize = wp_impl_session_finalize;

  proxy_class->augment = wp_impl_session_augment;
  proxy_class->enum_params = NULL;
  proxy_class->subscribe_params = NULL;
  proxy_class->pw_proxy_created = NULL;
  proxy_class->prop_changed = wp_impl_session_prop_changed;
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
  if (wp_proxy_get_features (WP_PROXY (self)) & WP_PROXY_FEATURE_BOUND) {
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
  if (wp_proxy_get_features (WP_PROXY (self)) & WP_PROXY_FEATURE_BOUND) {
    self->info.change_mask = PW_SESSION_CHANGE_MASK_PROPS;
    pw_session_emit_info (&self->hooks, &self->info);
    self->info.change_mask = 0;
  }
}
