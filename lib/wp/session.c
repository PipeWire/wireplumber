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

#include "spa-type.h"
#include "spa-pod.h"
#include "session.h"
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

static gint
wp_session_enum_params (WpProxy * self, guint32 id, guint32 start,
    guint32 num, const WpSpaPod * filter)
{
  WpSessionPrivate *priv = wp_session_get_instance_private (WP_SESSION (self));
  int session_enum_params_result;

  session_enum_params_result = pw_session_enum_params (priv->iface, 0, id,
      start, num, wp_spa_pod_get_spa_pod (filter));
  g_warn_if_fail (session_enum_params_result >= 0);

  return session_enum_params_result;
}

static gint
wp_session_subscribe_params (WpProxy * self, guint32 n_ids, guint32 *ids)
{
  WpSessionPrivate *priv = wp_session_get_instance_private (WP_SESSION (self));
  int session_subscribe_params_result;

  session_subscribe_params_result = pw_session_subscribe_params (priv->iface,
      ids, n_ids);
  g_warn_if_fail (session_subscribe_params_result >= 0);

  return session_subscribe_params_result;
}

static gint
wp_session_set_param (WpProxy * self, guint32 id, guint32 flags,
    const WpSpaPod *param)
{
  WpSessionPrivate *priv = wp_session_get_instance_private (WP_SESSION (self));
  int session_set_param_result;

  session_set_param_result = pw_session_set_param (priv->iface, id, flags,
      wp_spa_pod_get_spa_pod (param));
  g_warn_if_fail (session_set_param_result >= 0);

  return session_set_param_result;
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
wp_session_enable_feature_endpoints (WpSession * self, guint32 bound_id)
{
  WpSessionPrivate *priv = wp_session_get_instance_private (self);
  g_autoptr (WpCore) core = wp_proxy_get_core (WP_PROXY (self));
  GVariantBuilder b;

  /* proxy endpoint -> check for session.id in global properties */
  g_variant_builder_init (&b, G_VARIANT_TYPE ("aa{sv}"));
  g_variant_builder_open (&b, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&b, "{sv}", "type",
      g_variant_new_int32 (WP_OBJECT_MANAGER_CONSTRAINT_PW_GLOBAL_PROPERTY));
  g_variant_builder_add (&b, "{sv}", "name",
      g_variant_new_string (PW_KEY_SESSION_ID));
  g_variant_builder_add (&b, "{sv}", "value",
      g_variant_new_take_string (g_strdup_printf ("%u", bound_id)));
  g_variant_builder_close (&b);

  wp_object_manager_add_interest (priv->endpoints_om,
      WP_TYPE_ENDPOINT,
      g_variant_builder_end (&b),
      WP_ENDPOINT_FEATURES_STANDARD);

  /* impl endpoint -> check for session.id in standard properties */
  g_variant_builder_init (&b, G_VARIANT_TYPE ("aa{sv}"));
  g_variant_builder_open (&b, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&b, "{sv}", "type",
      g_variant_new_int32 (WP_OBJECT_MANAGER_CONSTRAINT_PW_PROPERTY));
  g_variant_builder_add (&b, "{sv}", "name",
      g_variant_new_string (PW_KEY_SESSION_ID));
  g_variant_builder_add (&b, "{sv}", "value",
      g_variant_new_take_string (g_strdup_printf ("%u", bound_id)));
  g_variant_builder_close (&b);

  wp_object_manager_add_interest (priv->endpoints_om,
      WP_TYPE_IMPL_ENDPOINT,
      g_variant_builder_end (&b),
      WP_ENDPOINT_FEATURES_STANDARD);

  wp_core_install_object_manager (core, priv->endpoints_om);
  wp_proxy_set_feature_ready (WP_PROXY (self), WP_SESSION_FEATURE_ENDPOINTS);
}

static void
wp_session_enable_feature_links (WpSession * self, guint32 bound_id)
{
  WpSessionPrivate *priv = wp_session_get_instance_private (self);
  g_autoptr (WpCore) core = wp_proxy_get_core (WP_PROXY (self));
  GVariantBuilder b;

  /* proxy link -> check for session.id in global properties */
  g_variant_builder_init (&b, G_VARIANT_TYPE ("aa{sv}"));
  g_variant_builder_open (&b, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&b, "{sv}", "type",
      g_variant_new_int32 (WP_OBJECT_MANAGER_CONSTRAINT_PW_GLOBAL_PROPERTY));
  g_variant_builder_add (&b, "{sv}", "name",
      g_variant_new_string (PW_KEY_SESSION_ID));
  g_variant_builder_add (&b, "{sv}", "value",
      g_variant_new_take_string (g_strdup_printf ("%u", bound_id)));
  g_variant_builder_close (&b);

  wp_object_manager_add_interest (priv->links_om,
      WP_TYPE_ENDPOINT_LINK,
      g_variant_builder_end (&b),
      WP_PROXY_FEATURES_STANDARD);

  /* impl link -> check for session.id in standard properties */
  g_variant_builder_init (&b, G_VARIANT_TYPE ("aa{sv}"));
  g_variant_builder_open (&b, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&b, "{sv}", "type",
      g_variant_new_int32 (WP_OBJECT_MANAGER_CONSTRAINT_PW_PROPERTY));
  g_variant_builder_add (&b, "{sv}", "name",
      g_variant_new_string (PW_KEY_SESSION_ID));
  g_variant_builder_add (&b, "{sv}", "value",
      g_variant_new_take_string (g_strdup_printf ("%u", bound_id)));
  g_variant_builder_close (&b);

  wp_object_manager_add_interest (priv->links_om,
      WP_TYPE_IMPL_ENDPOINT_LINK,
      g_variant_builder_end (&b),
      WP_PROXY_FEATURES_STANDARD);

  wp_core_install_object_manager (core, priv->links_om);
  wp_proxy_set_feature_ready (WP_PROXY (self), WP_SESSION_FEATURE_LINKS);
}

static void
wp_session_bound (WpProxy * proxy, guint32 id)
{
  WpSession *self = WP_SESSION (proxy);
  WpSessionPrivate *priv = wp_session_get_instance_private (self);

  if (priv->endpoints_om)
    wp_session_enable_feature_endpoints (self, id);
  if (priv->links_om)
    wp_session_enable_feature_links (self, id);
}

static void
wp_session_control_changed (WpProxy * proxy, const char * id_name)
{
  WpSession *self = WP_SESSION (proxy);
  WpSpaProps *controls = wp_proxy_get_spa_props (WP_PROXY (self));
  g_autoptr (WpSpaPod) pod = wp_spa_props_get_stored (controls, id_name);
  gint value;
  if (wp_spa_pod_get_int (pod, &value))
    g_signal_emit (self, signals[SIGNAL_DEFAULT_ENDPOINT_CHANGED], 0, id_name,
        value);
}

static void
wp_session_augment (WpProxy * proxy, WpProxyFeatures features)
{
  WpSessionPrivate *priv = wp_session_get_instance_private (WP_SESSION (proxy));

  /* call the parent impl first to ensure we have a pw proxy if necessary */
  WP_PROXY_CLASS (wp_session_parent_class)->augment (proxy, features);

  if (features & WP_PROXY_FEATURE_CONTROLS) {
    struct pw_session *pw_proxy = NULL;
    uint32_t ids[] = { SPA_PARAM_Props };

    pw_proxy = (struct pw_session *) wp_proxy_get_pw_proxy (proxy);
    if (!pw_proxy)
      return;

    pw_session_enum_params (pw_proxy, 0, SPA_PARAM_PropInfo, 0, -1, NULL);
    pw_session_subscribe_params (pw_proxy, ids, SPA_N_ELEMENTS (ids));
  }

  if (features & WP_SESSION_FEATURE_ENDPOINTS) {
    priv->endpoints_om = wp_object_manager_new ();

    /* if we are already bound, enable right away;
       else, continue in the bound() event */
    if (wp_proxy_get_features (proxy) & WP_PROXY_FEATURE_BOUND) {
      wp_session_enable_feature_endpoints (WP_SESSION (proxy),
          wp_proxy_get_bound_id (proxy));
    }
  }

  if (features & WP_SESSION_FEATURE_LINKS) {
    priv->links_om = wp_object_manager_new ();

    /* if we are already bound, enable right away;
       else, continue in the bound() event */
    if (wp_proxy_get_features (proxy) & WP_PROXY_FEATURE_BOUND) {
      wp_session_enable_feature_links (WP_SESSION (proxy),
          wp_proxy_get_bound_id (proxy));
    }
  }
}

static guint32
get_default_endpoint (WpSession * self, const gchar * id_name)
{
  g_autoptr (WpSpaPod) pod = wp_proxy_get_control (WP_PROXY (self), id_name);
  gint32 value;

  if (pod && wp_spa_pod_get_int (pod, &value))
    return (guint32) value;
  return 0;
}

static void
set_default_endpoint (WpSession * self, const gchar * id_name, guint32 id)
{
  g_autoptr (WpSpaPod) param = wp_spa_pod_new_int (id);
  wp_proxy_set_control (WP_PROXY (self), id_name, param);
}

static void
wp_session_class_init (WpSessionClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpProxyClass *proxy_class = (WpProxyClass *) klass;

  /* Register custom wireplumber session types */
  wp_spa_type_register (WP_SPA_TYPE_TABLE_PROPS,
      "Wp:Session:Default:Endpoint:Audio:Source",
      "wp-session-default-endpoint-audio-source");
  wp_spa_type_register (WP_SPA_TYPE_TABLE_PROPS,
      "Wp:Session:Default:Endpoint:Audio:Sink",
      "wp-session-default-endpoint-audio-sink");
  wp_spa_type_register (WP_SPA_TYPE_TABLE_PROPS,
      "Wp:Session:Default:Endpoint:Video:Source",
      "wp-session-default-endpoint-video-source");

  object_class->finalize = wp_session_finalize;

  proxy_class->pw_iface_type = PW_TYPE_INTERFACE_Session;
  proxy_class->pw_iface_version = PW_VERSION_SESSION;

  proxy_class->augment = wp_session_augment;
  proxy_class->get_info = wp_session_get_info;
  proxy_class->get_properties = wp_session_get_properties;
  proxy_class->enum_params = wp_session_enum_params;
  proxy_class->subscribe_params = wp_session_subscribe_params;
  proxy_class->set_param = wp_session_set_param;

  proxy_class->pw_proxy_created = wp_session_pw_proxy_created;
  proxy_class->bound = wp_session_bound;
  proxy_class->control_changed = wp_session_control_changed;

  klass->get_default_endpoint = get_default_endpoint;
  klass->set_default_endpoint = set_default_endpoint;

  /**
   * WpSession::default-endpoint-changed:
   * @self: the session
   * @type: the endpoint type
   * @id: the endpoint's bound id
   *
   * Emitted when the default endpoint of a specific type changes.
   * The passed @id is the bound id (wp_proxy_get_bound_id()) of the new
   * default endpoint.
   */
  signals[SIGNAL_DEFAULT_ENDPOINT_CHANGED] = g_signal_new (
      "default-endpoint-changed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 2,
      G_TYPE_STRING, G_TYPE_UINT);
}

/**
 * wp_session_get_default_endpoint:
 * @self: the session
 * @id_name: the endpoint id name
 *
 * Returns: the bound id of the default endpoint of this @type
 */
guint32
wp_session_get_default_endpoint (WpSession * self,
    const gchar * id_name)
{
  g_return_val_if_fail (WP_IS_SESSION (self), SPA_ID_INVALID);
  g_return_val_if_fail (WP_SESSION_GET_CLASS (self)->get_default_endpoint,
      SPA_ID_INVALID);

  return WP_SESSION_GET_CLASS (self)->get_default_endpoint (self, id_name);
}

/**
 * wp_session_set_default_endpoint:
 * @self: the session
 * @id_name: the endpoint id name
 * @id: the bound id of the endpoint to set as the default for this @type
 *
 * Sets the default endpoint for this @type to be the one identified with @id
 */
void
wp_session_set_default_endpoint (WpSession * self, const char * id_name,
    guint32 id)
{
  g_return_if_fail (WP_IS_SESSION (self));
  g_return_if_fail (WP_SESSION_GET_CLASS (self)->set_default_endpoint);

  WP_SESSION_GET_CLASS (self)->set_default_endpoint (self, id_name, id);
}

/**
 * wp_session_get_n_endpoints:
 * @self: the session
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
 * wp_session_find_endpoint:
 * @self: the session
 * @bound_id: the bound id of the endpoint object to find
 *
 * Returns: (transfer full) (nullable): the endpoint that has the given
 *    @bound_id, or %NULL if there is no such endpoint
 */
WpEndpoint *
wp_session_find_endpoint (WpSession * self, guint32 bound_id)
{
  g_return_val_if_fail (WP_IS_SESSION (self), NULL);
  g_return_val_if_fail (wp_proxy_get_features (WP_PROXY (self)) &
          WP_SESSION_FEATURE_ENDPOINTS, NULL);

  WpSessionPrivate *priv = wp_session_get_instance_private (self);
  return (WpEndpoint *)
      wp_object_manager_find_proxy (priv->endpoints_om, bound_id);
}

/**
 * wp_session_iterate_endpoints:
 * @self: the session
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
 * wp_session_get_n_links:
 * @self: the session
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
 * wp_session_find_link:
 * @self: the session
 * @bound_id: the bound id of the link object to find
 *
 * Returns: (transfer full) (nullable): the endpoint link that has the given
 *    @bound_id, or %NULL if there is no such endpoint link
 */
WpEndpointLink *
wp_session_find_link (WpSession * self, guint32 bound_id)
{
  g_return_val_if_fail (WP_IS_SESSION (self), NULL);
  g_return_val_if_fail (wp_proxy_get_features (WP_PROXY (self)) &
          WP_SESSION_FEATURE_LINKS, NULL);

  WpSessionPrivate *priv = wp_session_get_instance_private (self);
  return (WpEndpointLink *)
      wp_object_manager_find_proxy (priv->links_om, bound_id);
}

/**
 * wp_session_iterate_links:
 * @self: the session
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
  WpSpaProps *controls = wp_proxy_get_spa_props (WP_PROXY (self));

  switch (id) {
    case SPA_PARAM_PropInfo: {
      g_autoptr (GPtrArray) params = wp_spa_props_build_propinfo (controls);

      for (guint i = start; i < params->len; i++) {
        WpSpaPod *pod = g_ptr_array_index (params, i);
        const struct spa_pod *param = wp_spa_pod_get_spa_pod (pod);
        pw_session_emit_param (&self->hooks, seq, id, i, i+1, param);
        wp_proxy_handle_event_param (self, seq, id, i, i+1, param);
        if (++count == num)
          break;
      }
      break;
    }
    case SPA_PARAM_Props: {
      if (start == 0) {
        g_autoptr (WpSpaPod) pod = wp_spa_props_build_props (controls);
        const struct spa_pod *param = wp_spa_pod_get_spa_pod (pod);
        if (spa_pod_filter (&b, &result, param, filter) == 0) {
          pw_session_emit_param (&self->hooks, seq, id, 0, 1, result);
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
  g_autoptr (GPtrArray) changed_ids = NULL;
  WpSpaProps *controls = wp_proxy_get_spa_props (WP_PROXY (self));

  if (id != SPA_PARAM_Props)
    return -ENOENT;

  changed_ids = g_ptr_array_new_with_free_func (g_free);
  g_autoptr (WpSpaPod) pod = wp_spa_pod_new_regular_wrap_copy (param);
  wp_spa_props_store_from_props (controls, pod, changed_ids);

  /* notify subscribers */
  if (self->subscribed)
    impl_enum_params (self, 1, SPA_PARAM_Props, 0, UINT32_MAX, NULL);

  /* notify controls locally */
  for (guint i = 0; i < changed_ids->len; i++) {
    const gchar * prop_id = g_ptr_array_index (changed_ids, i);
    WP_PROXY_GET_CLASS (WP_PROXY (self))->control_changed (WP_PROXY (self),
        prop_id);
  }

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
  WpSpaProps *controls = wp_proxy_get_spa_props (WP_PROXY (self));

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

  /* prepare default endpoint */
  wp_spa_props_register (controls,
      "wp-session-default-endpoint-audio-source",
      "Default Audio Source", wp_spa_pod_new_int (0));
  wp_spa_props_register (controls,
      "wp-session-default-endpoint-audio-sink",
      "Default Audio Sink", wp_spa_pod_new_int (0));
  wp_spa_props_register (controls,
      "wp-session-default-endpoint-video-source",
      "Default Video Source", wp_spa_pod_new_int (0));

  wp_proxy_set_feature_ready (WP_PROXY (self),
      WP_PROXY_FEATURE_CONTROLS);
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

  if (features & WP_SESSION_FEATURE_ENDPOINTS) {
    priv->endpoints_om = wp_object_manager_new ();

    /* if we are already bound, enable right away;
       else, continue in the bound() event */
    if (wp_proxy_get_features (proxy) & WP_PROXY_FEATURE_BOUND) {
      wp_session_enable_feature_endpoints (WP_SESSION (proxy),
          wp_proxy_get_bound_id (proxy));
    }
  }

  if (features & WP_SESSION_FEATURE_LINKS) {
    priv->links_om = wp_object_manager_new ();

    /* if we are already bound, enable right away;
       else, continue in the bound() event */
    if (wp_proxy_get_features (proxy) & WP_PROXY_FEATURE_BOUND) {
      wp_session_enable_feature_links (WP_SESSION (proxy),
          wp_proxy_get_bound_id (proxy));
    }
  }
}

static void
wp_impl_session_class_init (WpImplSessionClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpProxyClass *proxy_class = (WpProxyClass *) klass;

  object_class->finalize = wp_impl_session_finalize;

  proxy_class->augment = wp_impl_session_augment;
  proxy_class->pw_proxy_created = NULL;
  proxy_class->param = NULL;
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
