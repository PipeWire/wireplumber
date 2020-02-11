/* WirePlumber
 *
 * Copyright © 2019-2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "session.h"
#include "private.h"
#include "error.h"
#include "wpenums.h"

#include <pipewire/pipewire.h>
#include <pipewire/extensions/session-manager.h>
#include <spa/pod/builder.h>
#include <spa/pod/parser.h>

enum {
  SIGNAL_DEFAULT_ENDPOINT_CHANGED,
  N_SIGNALS,
};

static guint32 signals[N_SIGNALS] = {0};

/* helpers */

static struct pw_session_info *
session_info_update (struct pw_session_info *info,
    WpProperties ** props_storage,
    const struct pw_session_info *update)
{
  if (update == NULL)
    return info;

  if (info == NULL) {
    info = calloc(1, sizeof(struct pw_session_info));
    if (info == NULL)
      return NULL;

    info->id = update->id;
  }
  info->change_mask = update->change_mask;

  if (update->change_mask & PW_SESSION_CHANGE_MASK_PROPS) {
    if (*props_storage)
      wp_properties_unref (*props_storage);
    *props_storage = wp_properties_new_copy_dict (update->props);
    info->props = (struct spa_dict *) wp_properties_peek_dict (*props_storage);
  }
  if (update->change_mask & PW_SESSION_CHANGE_MASK_PARAMS) {
    info->n_params = update->n_params;
    free((void *) info->params);
    if (update->params) {
      size_t size = info->n_params * sizeof(struct spa_param_info);
      info->params = malloc(size);
      memcpy(info->params, update->params, size);
    }
    else
      info->params = NULL;
  }
  return info;
}

static void
session_info_free (struct pw_session_info *info)
{
  free((void *) info->params);
  free(info);
}

/* WpSession */

typedef struct _WpSessionPrivate WpSessionPrivate;
struct _WpSessionPrivate
{
  WpProperties *properties;
  WpSpaProps spa_props;
  struct pw_session_info *info;
  struct spa_hook listener;
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

  g_clear_pointer (&priv->info, session_info_free);
  g_clear_pointer (&priv->properties, wp_properties_unref);
  wp_spa_props_clear (&priv->spa_props);

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
    guint32 num, const struct spa_pod *filter)
{
  struct pw_session *pwp;
  int session_enum_params_result;

  pwp = (struct pw_session *) wp_proxy_get_pw_proxy (self);
  session_enum_params_result = pw_session_enum_params (pwp, 0, id, start, num,
      filter);
  g_warn_if_fail (session_enum_params_result >= 0);

  return session_enum_params_result;
}

static gint
wp_session_subscribe_params (WpProxy * self, guint32 n_ids, guint32 *ids)
{
  struct pw_session *pwp;
  int session_subscribe_params_result;

  pwp = (struct pw_session *) wp_proxy_get_pw_proxy (self);
  session_subscribe_params_result = pw_session_subscribe_params (pwp, ids,
      n_ids);
  g_warn_if_fail (session_subscribe_params_result >= 0);

  return session_subscribe_params_result;
}

static gint
wp_session_set_param (WpProxy * self, guint32 id, guint32 flags,
    const struct spa_pod *param)
{
  struct pw_session *pwp;
  int session_set_param_result;

  pwp = (struct pw_session *) wp_proxy_get_pw_proxy (self);
  session_set_param_result = pw_session_set_param (pwp, id, flags, param);
  g_warn_if_fail (session_set_param_result >= 0);

  return session_set_param_result;
}

static void
session_event_info (void *data, const struct pw_session_info *info)
{
  WpSession *self = WP_SESSION (data);
  WpSessionPrivate *priv = wp_session_get_instance_private (self);

  priv->info = session_info_update (priv->info, &priv->properties, info);
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
  pw_session_add_listener ((struct pw_session *) pw_proxy,
      &priv->listener, &session_events, self);
}

static void
wp_session_param (WpProxy * proxy, gint seq, guint32 id, guint32 index,
    guint32 next, const struct spa_pod *param)
{
  WpSession *self = WP_SESSION (proxy);
  WpSessionPrivate *priv = wp_session_get_instance_private (self);
  g_autoptr (GArray) changed_ids = NULL;
  guint32 prop_id;
  gint32 value;

  switch (id) {
  case SPA_PARAM_PropInfo:
    wp_spa_props_register_from_prop_info (&priv->spa_props, param);
    break;
  case SPA_PARAM_Props:
    changed_ids = g_array_new (FALSE, FALSE, sizeof (uint32_t));
    wp_spa_props_store_from_props (&priv->spa_props, param, changed_ids);

    for (guint i = 0; i < changed_ids->len; i++) {
      prop_id = g_array_index (changed_ids, uint32_t, i);
      param = wp_spa_props_get_stored (&priv->spa_props, prop_id);
      if (spa_pod_get_int (param, &value) == 0) {
        g_signal_emit (self, signals[SIGNAL_DEFAULT_ENDPOINT_CHANGED], 0,
            prop_id, value);
      }
    }

    wp_proxy_set_feature_ready (WP_PROXY (self),
        WP_SESSION_FEATURE_DEFAULT_ENDPOINT);
    break;
  }
}

static void
wp_session_augment (WpProxy * proxy, WpProxyFeatures features)
{
  /* call the parent impl first to ensure we have a pw proxy if necessary */
  WP_PROXY_CLASS (wp_session_parent_class)->augment (proxy, features);

  if (features & WP_SESSION_FEATURE_DEFAULT_ENDPOINT) {
    struct pw_session *pw_proxy = NULL;
    uint32_t ids[] = { SPA_PARAM_Props };

    pw_proxy = (struct pw_session *) wp_proxy_get_pw_proxy (proxy);
    if (!pw_proxy)
      return;

    pw_session_enum_params (pw_proxy, 0, SPA_PARAM_PropInfo, 0, -1, NULL);
    pw_session_subscribe_params (pw_proxy, ids, SPA_N_ELEMENTS (ids));
  }
}

static guint32
get_default_endpoint (WpSession * self, WpDefaultEndpointType type)
{
  WpSessionPrivate *priv = wp_session_get_instance_private (self);
  const struct spa_pod *pod;
  gint32 value;

  pod = wp_spa_props_get_stored (&priv->spa_props, type);
  if (pod && spa_pod_get_int (pod, &value) == 0)
    return (guint32) value;
  return 0;
}

static void
set_default_endpoint (WpSession * self, WpDefaultEndpointType type, guint32 id)
{
  char buf[1024];
  struct spa_pod_builder b = SPA_POD_BUILDER_INIT (buf, sizeof (buf));
  struct pw_session *pw_proxy = NULL;

  /* set the default endpoint id as a property param on the session;
     our spa_props cache will be updated by the param event */

  pw_proxy = (struct pw_session *) wp_proxy_get_pw_proxy (WP_PROXY (self));
  pw_session_set_param (pw_proxy,
      SPA_PARAM_Props, 0,
      spa_pod_builder_add_object (&b,
          SPA_TYPE_OBJECT_Props, SPA_PARAM_Props,
          type, SPA_POD_Int (id)));
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
  proxy_class->enum_params = wp_session_enum_params;
  proxy_class->subscribe_params = wp_session_subscribe_params;
  proxy_class->set_param = wp_session_set_param;

  proxy_class->pw_proxy_created = wp_session_pw_proxy_created;
  proxy_class->param = wp_session_param;

  klass->get_default_endpoint = get_default_endpoint;
  klass->set_default_endpoint = set_default_endpoint;

  signals[SIGNAL_DEFAULT_ENDPOINT_CHANGED] = g_signal_new (
      "default-endpoint-changed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 2,
      WP_TYPE_DEFAULT_ENDPOINT_TYPE, G_TYPE_UINT);
}

guint32
wp_session_get_default_endpoint (WpSession * self,
    WpDefaultEndpointType type)
{
  g_return_val_if_fail (WP_IS_SESSION (self), SPA_ID_INVALID);
  g_return_val_if_fail (WP_SESSION_GET_CLASS (self)->get_default_endpoint,
      SPA_ID_INVALID);

  return WP_SESSION_GET_CLASS (self)->get_default_endpoint (self, type);
}

void
wp_session_set_default_endpoint (WpSession * self,
    WpDefaultEndpointType type, guint32 id)
{
  g_return_if_fail (WP_IS_SESSION (self));
  g_return_if_fail (WP_SESSION_GET_CLASS (self)->set_default_endpoint);

  WP_SESSION_GET_CLASS (self)->set_default_endpoint (self, type, id);
}

/* WpImplSession */

typedef struct _WpImplSessionPrivate WpImplSessionPrivate;
struct _WpImplSessionPrivate
{
  WpSessionPrivate *pp;
  struct pw_session_info info;
  struct spa_param_info param_info[2];
};

G_DEFINE_TYPE_WITH_PRIVATE (WpImplSession, wp_impl_session, WP_TYPE_SESSION)

static void
wp_impl_session_init (WpImplSession * self)
{
  WpImplSessionPrivate *priv = wp_impl_session_get_instance_private (self);

  /* store a pointer to the parent's private; we use that structure
    as well to optimize memory usage and to be able to re-use some of the
    parent's methods without reimplementing them */
  priv->pp = wp_session_get_instance_private (WP_SESSION (self));

  priv->pp->properties = wp_properties_new_empty ();

  priv->param_info[0] = SPA_PARAM_INFO (SPA_PARAM_Props, SPA_PARAM_INFO_READWRITE);
  priv->param_info[1] = SPA_PARAM_INFO (SPA_PARAM_PropInfo, SPA_PARAM_INFO_READ);

  priv->info.version = PW_VERSION_SESSION_INFO;
  priv->info.props =
      (struct spa_dict *) wp_properties_peek_dict (priv->pp->properties);
  priv->info.params = priv->param_info;
  priv->info.n_params = SPA_N_ELEMENTS (priv->param_info);
  priv->pp->info = &priv->info;
  wp_proxy_set_feature_ready (WP_PROXY (self), WP_PROXY_FEATURE_INFO);

  wp_spa_props_register (&priv->pp->spa_props,
      WP_DEFAULT_ENDPOINT_TYPE_AUDIO_SOURCE,
      "Default Audio Source", SPA_POD_Int (0));
  wp_spa_props_register (&priv->pp->spa_props,
      WP_DEFAULT_ENDPOINT_TYPE_AUDIO_SINK,
      "Default Audio Sink", SPA_POD_Int (0));
  wp_spa_props_register (&priv->pp->spa_props,
      WP_DEFAULT_ENDPOINT_TYPE_VIDEO_SOURCE,
      "Default Video Source", SPA_POD_Int (0));
  wp_proxy_set_feature_ready (WP_PROXY (self),
      WP_SESSION_FEATURE_DEFAULT_ENDPOINT);
}

static void
wp_impl_session_finalize (GObject * object)
{
  WpImplSessionPrivate *priv =
      wp_impl_session_get_instance_private (WP_IMPL_SESSION (object));

  /* set to NULL to prevent parent's finalize from calling free() on it */
  priv->pp->info = NULL;

  G_OBJECT_CLASS (wp_impl_session_parent_class)->finalize (object);
}

static void
client_session_update (WpImplSession * self, guint32 change_mask,
    guint32 info_change_mask)
{
  WpImplSessionPrivate *priv =
      wp_impl_session_get_instance_private (self);
  char buf[1024];
  struct spa_pod_builder b = SPA_POD_BUILDER_INIT (buf, sizeof (buf));
  struct pw_client_session *pw_proxy = NULL;
  struct pw_session_info *info = NULL;
  g_autoptr (GPtrArray) params = NULL;

  pw_proxy = (struct pw_client_session *) wp_proxy_get_pw_proxy (WP_PROXY (self));

  if (change_mask & PW_CLIENT_SESSION_UPDATE_PARAMS) {
    params = wp_spa_props_build_all_pods (&priv->pp->spa_props, &b);
  }
  if (change_mask & PW_CLIENT_SESSION_UPDATE_INFO) {
    info = &priv->info;
    info->change_mask = info_change_mask;
  }

  pw_client_session_update (pw_proxy,
      change_mask,
      params ? params->len : 0,
      (const struct spa_pod **) (params ? params->pdata : NULL),
      info);

  if (info)
    info->change_mask = 0;
}

static int
client_session_set_param (void *object,
    uint32_t id, uint32_t flags, const struct spa_pod *param)
{
  WpImplSession *self = WP_IMPL_SESSION (object);
  WpImplSessionPrivate *priv =
      wp_impl_session_get_instance_private (self);
  g_autoptr (GArray) changed_ids = NULL;
  guint32 prop_id;
  gint32 value;

  if (id != SPA_PARAM_Props)
    return -ENOENT;

  changed_ids = g_array_new (FALSE, FALSE, sizeof (guint32));
  wp_spa_props_store_from_props (&priv->pp->spa_props, param, changed_ids);

  for (guint i = 0; i < changed_ids->len; i++) {
    prop_id = g_array_index (changed_ids, guint32, i);
    param = wp_spa_props_get_stored (&priv->pp->spa_props, prop_id);
    if (spa_pod_get_int (param, &value) == 0) {
      g_signal_emit (self, signals[SIGNAL_DEFAULT_ENDPOINT_CHANGED], 0,
          prop_id, value);
    }
  }

  client_session_update (self, PW_CLIENT_SESSION_UPDATE_PARAMS, 0);

  return 0;
}

static struct pw_client_session_events client_session_events = {
  PW_VERSION_CLIENT_SESSION_EVENTS,
  .set_param = client_session_set_param,
};

static void
wp_impl_session_augment (WpProxy * proxy, WpProxyFeatures features)
{
  WpImplSession *self = WP_IMPL_SESSION (proxy);
  WpImplSessionPrivate *priv = wp_impl_session_get_instance_private (self);

  /* if any of the default features is requested, make sure BOUND
     is also requested, as they all depend on binding the session */
  if (features & WP_PROXY_FEATURES_STANDARD)
    features |= WP_PROXY_FEATURE_BOUND;

  if (features & WP_PROXY_FEATURE_BOUND) {
    g_autoptr (WpCore) core = wp_proxy_get_core (proxy);
    struct pw_core *pw_core = wp_core_get_pw_core (core);
    struct pw_proxy *pw_proxy = NULL;

    /* no pw_core -> we are not connected */
    if (!pw_core) {
      wp_proxy_augment_error (proxy, g_error_new (WP_DOMAIN_LIBRARY,
            WP_LIBRARY_ERROR_OPERATION_FAILED,
            "The WirePlumber core is not connected; "
            "object cannot be exported to PipeWire"));
      return;
    }

    /* make sure these props are not present; they are added by the server */
    wp_properties_set (priv->pp->properties, PW_KEY_OBJECT_ID, NULL);
    wp_properties_set (priv->pp->properties, PW_KEY_CLIENT_ID, NULL);
    wp_properties_set (priv->pp->properties, PW_KEY_FACTORY_ID, NULL);

    pw_proxy = pw_core_create_object (pw_core, "client-session",
        PW_TYPE_INTERFACE_ClientSession, PW_VERSION_CLIENT_SESSION,
        wp_properties_peek_dict (priv->pp->properties), 0);
    wp_proxy_set_pw_proxy (proxy, pw_proxy);

    pw_client_session_add_listener (pw_proxy, &priv->pp->listener,
        &client_session_events, self);

    client_session_update (WP_IMPL_SESSION (self),
        PW_CLIENT_SESSION_UPDATE_PARAMS | PW_CLIENT_SESSION_UPDATE_INFO,
        PW_SESSION_CHANGE_MASK_ALL);
  }
}

static gint
wp_impl_session_set_param (WpProxy * self, guint32 id, guint32 flags,
    const struct spa_pod *param)
{
  return client_session_set_param (self, id, flags, param);
}

static void
wp_impl_session_set_default_endpoint (WpSession * session,
    WpDefaultEndpointType type, guint32 id)
{
  WpImplSession *self = WP_IMPL_SESSION (session);
  WpImplSessionPrivate *priv = wp_impl_session_get_instance_private (self);

  wp_spa_props_store (&priv->pp->spa_props, type, SPA_POD_Int (id));

  g_signal_emit (session, signals[SIGNAL_DEFAULT_ENDPOINT_CHANGED], 0, type, id);

  /* update only after the session has been exported */
  if (wp_proxy_get_features (WP_PROXY (self)) & WP_PROXY_FEATURE_BOUND) {
    client_session_update (WP_IMPL_SESSION (session),
      PW_CLIENT_SESSION_UPDATE_PARAMS, 0);
  }
}

static void
wp_impl_session_class_init (WpImplSessionClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpProxyClass *proxy_class = (WpProxyClass *) klass;
  WpSessionClass *session_class = (WpSessionClass *) klass;

  object_class->finalize = wp_impl_session_finalize;

  proxy_class->augment = wp_impl_session_augment;
  proxy_class->enum_params = NULL;
  proxy_class->subscribe_params = NULL;
  proxy_class->set_param = wp_impl_session_set_param;

  proxy_class->pw_proxy_created = NULL;
  proxy_class->param = NULL;

  session_class->set_default_endpoint = wp_impl_session_set_default_endpoint;
}

WpImplSession *
wp_impl_session_new (WpCore * core)
{
  g_return_val_if_fail (WP_IS_CORE (core), NULL);

  return g_object_new (WP_TYPE_IMPL_SESSION,
      "core", core,
      NULL);
}

void
wp_impl_session_set_property (WpImplSession * self,
    const gchar * key, const gchar * value)
{
  WpImplSessionPrivate *priv;

  g_return_if_fail (WP_IS_IMPL_SESSION (self));
  priv = wp_impl_session_get_instance_private (self);

  wp_properties_set (priv->pp->properties, key, value);

  g_object_notify (G_OBJECT (self), "properties");

  /* update only after the session has been exported */
  if (wp_proxy_get_features (WP_PROXY (self)) & WP_PROXY_FEATURE_BOUND) {
    client_session_update (self, PW_CLIENT_SESSION_UPDATE_INFO,
        PW_SESSION_CHANGE_MASK_PROPS);
  }
}

void
wp_impl_session_update_properties (WpImplSession * self,
    WpProperties * updates)
{
  WpImplSessionPrivate *priv;

  g_return_if_fail (WP_IS_IMPL_SESSION (self));
  priv = wp_impl_session_get_instance_private (self);

  wp_properties_update_from_dict (priv->pp->properties,
      wp_properties_peek_dict (updates));

  g_object_notify (G_OBJECT (self), "properties");

  /* update only after the session has been exported */
  if (wp_proxy_get_features (WP_PROXY (self)) & WP_PROXY_FEATURE_BOUND) {
    client_session_update (self, PW_CLIENT_SESSION_UPDATE_INFO,
        PW_SESSION_CHANGE_MASK_PROPS);
  }
}
